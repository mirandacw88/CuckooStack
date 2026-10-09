// Friend nudges (13+ players who turned reminders on, so they have a push token):
//   createChallenge  the sharer registers today's distance; the share link carries the returned id (&c=...)
//   challengeBeaten  a friend who opened that link beat it: the sharer gets a push ("Your record fell!")
// Pushes are capped (one per friend per challenge, 5 a day per sharer) and never sent to yourself.
import { FieldValue, getFirestore } from "firebase-admin/firestore";
import { getMessaging } from "firebase-admin/messaging";
import { HttpsError, onCall } from "firebase-functions/v2/https";

const MAX_NUDGES_PER_DAY = 5;
const DAY = /^\d{4}-\d{2}-\d{2}$/;

interface CreateRequest { day: string; meters: number; token: string }
interface BeatenRequest { id: string; meters: number }

export const createChallenge = onCall<CreateRequest>({ region: "us-central1", memory: "256MiB", maxInstances: 20 }, async (req) => {
  if (!req.auth) throw new HttpsError("unauthenticated", "Sign in first.");
  const { day, meters, token } = req.data ?? ({} as CreateRequest);
  if (typeof day !== "string" || !DAY.test(day) || !Number.isInteger(meters) || meters <= 0 || meters > 100000 ||
      typeof token !== "string" || token.length < 20 || token.length > 4096)
    throw new HttpsError("invalid-argument", "day, meters and token are required.");
  const ref = await getFirestore().collection("challenges").add({
    owner: req.auth.uid, token, day, meters, nudges: 0, nudgeDay: day, createdAt: FieldValue.serverTimestamp(),
  });
  return { id: ref.id };
});

export const challengeBeaten = onCall<BeatenRequest>({ region: "us-central1", memory: "256MiB", maxInstances: 20 }, async (req) => {
  if (!req.auth) throw new HttpsError("unauthenticated", "Sign in first.");
  const { id, meters } = req.data ?? ({} as BeatenRequest);
  if (typeof id !== "string" || !/^[A-Za-z0-9]{10,40}$/.test(id) || !Number.isInteger(meters) || meters <= 0)
    throw new HttpsError("invalid-argument", "id and meters are required.");
  const db = getFirestore();
  const ref = db.collection("challenges").doc(id);
  const beater = req.auth.uid;
  const send = await db.runTransaction(async (tx) => {
    const snap = await tx.get(ref);
    if (!snap.exists) return null;
    const c = snap.data() as { owner: string; token: string; day: string; meters: number; nudges: number; nudgeDay: string };
    if (c.owner === beater || meters <= c.meters) return null;            // yourself, or not actually beaten
    const beatRef = ref.collection("beats").doc(beater);
    if ((await tx.get(beatRef)).exists) return null;                       // one nudge per friend per challenge
    // the sharer's daily cap, across all of their challenges
    const ownerRef = db.collection("users").doc(c.owner);
    const owner = (await tx.get(ownerRef)).data() ?? {};
    const today = c.day;
    const sent = owner.nudgeDay === today ? (owner.nudges as number) ?? 0 : 0;
    if (sent >= MAX_NUDGES_PER_DAY) return null;
    tx.set(beatRef, { meters, at: FieldValue.serverTimestamp() });
    tx.set(ownerRef, { nudgeDay: today, nudges: sent + 1 }, { merge: true });
    return { token: c.token, theirs: c.meters };
  });
  if (!send) return { sent: false };
  try {
    await getMessaging().send({
      token: send.token,
      notification: {
        title: "Your record fell!",
        body: `A friend beat your ${send.theirs} m with ${meters} m on today's course. Take it back?`,
      },
      data: { kind: "challenge_beaten", id },
      android: { notification: { channelId: "reminders" } },
    });
    return { sent: true };
  } catch (e) {
    console.warn("nudge push failed", e); // stale token: the sharer reinstalled or turned reminders off
    return { sent: false };
  }
});
