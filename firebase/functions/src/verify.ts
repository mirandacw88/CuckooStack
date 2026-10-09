// Store-side checks: Google Play Developer API for Android purchase tokens, Apple's signed JWS for StoreKit 2.
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { androidpublisher } from "@googleapis/androidpublisher";
import { GoogleAuth } from "google-auth-library";
import { Environment, SignedDataVerifier } from "@apple/app-store-server-library";

export interface Verified { ok: boolean; transactionId: string; environment: string }

// The functions' service account needs "View financial data" in Play Console (Users and permissions).
const play = androidpublisher({ version: "v3", auth: new GoogleAuth({ scopes: ["https://www.googleapis.com/auth/androidpublisher"] }) });

export async function verifyAndroid(packageName: string, productId: string, token: string): Promise<Verified> {
  const res = await play.purchases.products.get({ packageName, productId, token });
  const p = res.data;
  // purchaseState: 0 purchased, 1 cancelled, 2 pending. purchaseType 0 = test (license tester).
  const ok = p.purchaseState === 0;
  return { ok, transactionId: p.orderId ?? token.slice(0, 64), environment: p.purchaseType === 0 ? "test" : "production" };
}

const appleRoots = ["AppleRootCA-G3.cer", "AppleRootCA-G2.cer"].map((f) => readFileSync(join(__dirname, "..", "certs", f)));

// prod: Production first, then Sandbox (App Review and TestFlight buy in the sandbox). staging: Sandbox, then Xcode.
export async function verifyApple(jws: string, bundleId: string, prod: boolean, appAppleId?: number): Promise<Verified> {
  const envs = prod ? [Environment.PRODUCTION, Environment.SANDBOX] : [Environment.SANDBOX, Environment.XCODE];
  let lastError: unknown;
  for (const env of envs) {
    try {
      const v = new SignedDataVerifier(appleRoots, true, env, bundleId, env === Environment.PRODUCTION ? appAppleId : undefined);
      const t = await v.verifyAndDecodeTransaction(jws);
      return { ok: !t.revocationDate, transactionId: t.transactionId ?? "", environment: env };
    } catch (e) {
      lastError = e;
    }
  }
  console.warn("apple verification failed", lastError);
  return { ok: false, transactionId: "", environment: "" };
}
