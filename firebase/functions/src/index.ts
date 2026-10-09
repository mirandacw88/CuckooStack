// Cuckoo Stack Cloud Functions (deployed per environment: scripts/firebase_deploy.sh staging|prod).
//
//   verifyPurchase   (callable, signed-in players)  checks a store purchase with Google / Apple, then credits the
//                    player's cloud wallet once (idempotent per store transaction). The game credits its local
//                    wallet when this returns valid.
//   createChallenge  (callable)  registers a shared distance for friend nudges          (challenges.ts)
//   challengeBeaten  (callable)  a friend beat it: push the sharer, capped             (challenges.ts)
import { initializeApp } from "firebase-admin/app";
import { FieldValue, getFirestore } from "firebase-admin/firestore";
import { defineString } from "firebase-functions/params";
import { HttpsError, onCall } from "firebase-functions/v2/https";
import { appIdFor, isProd, product } from "./catalog";
import { verifyAndroid, verifyApple, Verified } from "./verify";

initializeApp();
export { challengeBeaten, createChallenge } from "./challenges";
const db = getFirestore();
const PROJECT = process.env.GCLOUD_PROJECT ?? process.env.GCP_PROJECT ?? "";
// App Store Connect > App Information > Apple ID (a number); needed to verify Production transactions.
const APPLE_APP_ID = defineString("APPLE_APP_ID", { default: "" });

interface VerifyRequest { platform: "android" | "ios"; productId: string; token: string; orderId?: string }

export const verifyPurchase = onCall<VerifyRequest>({ region: "us-central1", memory: "256MiB", maxInstances: 20 }, async (req) => {
  if (!req.auth) throw new HttpsError("unauthenticated", "Sign in first.");
  const { platform, productId, token } = req.data ?? ({} as VerifyRequest);
  if ((platform !== "android" && platform !== "ios") || typeof productId !== "string" || typeof token !== "string" || token.length > 20000)
    throw new HttpsError("invalid-argument", "platform, productId and token are required.");
  const p = product(productId);
  if (!p) throw new HttpsError("invalid-argument", `Unknown product ${productId}.`);

  const appId = appIdFor(PROJECT);
  let v: Verified;
  try {
    v = platform === "android"
      ? await verifyAndroid(appId, productId, token)
      : await verifyApple(token, appId, isProd(PROJECT), APPLE_APP_ID.value() ? Number(APPLE_APP_ID.value()) : undefined);
  } catch (e) {
    // the store couldn't be reached: let the client retry later rather than calling a real purchase invalid
    console.error("store check failed", platform, productId, e);
    throw new HttpsError("unavailable", "Store verification is unavailable right now.");
  }
  if (!v.ok) return { valid: false };

  const uid = req.auth.uid;
  const purchaseRef = db.collection("purchases").doc(`${platform}_${v.transactionId}`);
  const userRef = db.collection("users").doc(uid);
  const credited = await db.runTransaction(async (tx) => {
    const seen = await tx.get(purchaseRef);
    if (seen.exists) return false; // the same store transaction never pays twice
    tx.set(purchaseRef, { uid, platform, productId, environment: v.environment, at: FieldValue.serverTimestamp() });
    const update: Record<string, unknown> = {
      coins: FieldValue.increment(p.coins),
      purchases: FieldValue.increment(1),
      updatedAt: FieldValue.serverTimestamp(),
    };
    if (p.type === "non_consumable") update.owned = FieldValue.arrayUnion(productId, ...(p.grants ?? []));
    tx.set(userRef, update, { merge: true });
    return true;
  });
  return { valid: true, credited };
});
