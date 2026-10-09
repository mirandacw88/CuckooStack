package com.cuckoostack.aerospheregames;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;

import com.google.firebase.FirebaseApp;
import com.google.firebase.analytics.FirebaseAnalytics;
import com.google.firebase.auth.FirebaseAuth;
import com.google.firebase.functions.FirebaseFunctions;
import com.google.firebase.messaging.FirebaseMessaging;
import com.google.firebase.remoteconfig.FirebaseRemoteConfig;
import com.google.firebase.remoteconfig.FirebaseRemoteConfigSettings;
import com.google.firebase.remoteconfig.FirebaseRemoteConfigValue;

import java.util.EnumMap;
import java.util.HashMap;
import java.util.Map;
import java.util.function.Consumer;

/**
 * Firebase for the game: Analytics (events from src/core, ad revenue), Remote Config (Tuning.h overrides),
 * anonymous Auth + the verifyPurchase Cloud Function (firebase/functions). Crashlytics starts by itself.
 *
 * Each build flavour has its own project (google-services.json in app/src/staging or app/src/prod). When that file
 * is missing, Firebase isn't initialised and every call here is a no-op, so the game still runs.
 */
final class FirebaseBridge {
    private static final String TAG = "CuckooStack";

    private final boolean enabled;
    private FirebaseAnalytics analytics;
    private FirebaseRemoteConfig remoteConfig;

    FirebaseBridge(Context context) {
        enabled = !FirebaseApp.getApps(context).isEmpty();
        if (!enabled) { Log.w(TAG, "Firebase: no google-services.json for this flavour; analytics, remote config and purchase checks are off"); return; }
        analytics = FirebaseAnalytics.getInstance(context);
        remoteConfig = FirebaseRemoteConfig.getInstance();
        remoteConfig.setConfigSettingsAsync(new FirebaseRemoteConfigSettings.Builder()
                .setMinimumFetchIntervalInSeconds(BuildConfig.DEBUG ? 60 : 3600).build());
        // values fetched now are activated now but the game reads them at the next launch (Tuning::load), so a
        // session never changes rules halfway through
        remoteConfig.fetchAndActivate().addOnCompleteListener(t -> Log.i(TAG, "Firebase: remote config " + (t.isSuccessful() ? "updated" : "fetch failed")));
    }

    boolean enabled() { return enabled; }

    /** Under-13s: no ad personalisation or ad storage from Analytics either. */
    void setChild(boolean child) {
        if (!enabled) return;
        Map<FirebaseAnalytics.ConsentType, FirebaseAnalytics.ConsentStatus> c = new EnumMap<>(FirebaseAnalytics.ConsentType.class);
        final FirebaseAnalytics.ConsentStatus ad = child ? FirebaseAnalytics.ConsentStatus.DENIED : FirebaseAnalytics.ConsentStatus.GRANTED;
        c.put(FirebaseAnalytics.ConsentType.AD_STORAGE, ad);
        c.put(FirebaseAnalytics.ConsentType.AD_USER_DATA, ad);
        c.put(FirebaseAnalytics.ConsentType.AD_PERSONALIZATION, ad);
        c.put(FirebaseAnalytics.ConsentType.ANALYTICS_STORAGE, FirebaseAnalytics.ConsentStatus.GRANTED);
        analytics.setConsent(c);
        analytics.setUserProperty(FirebaseAnalytics.UserProperty.ALLOW_AD_PERSONALIZATION_SIGNALS, child ? "false" : "true");
    }

    /** kv = {key0, value0, key1, value1, ...}; numeric values are sent as numbers. */
    void logEvent(String name, String[] kv) {
        if (!enabled) return;
        Bundle b = new Bundle();
        for (int i = 0; i + 1 < kv.length; i += 2) {
            final String v = kv[i + 1];
            try { b.putLong(kv[i], Long.parseLong(v)); }
            catch (NumberFormatException e) {
                try { b.putDouble(kv[i], Double.parseDouble(v)); } catch (NumberFormatException e2) { b.putString(kv[i], v); }
            }
        }
        analytics.logEvent(name, b);
    }

    void logEventBundle(String name, Bundle b) { if (enabled) analytics.logEvent(name, b); }

    void setUserProperty(String name, String value) { if (enabled) analytics.setUserProperty(name, value); }

    /** NaN when the server doesn't set the key (the game keeps its compiled-in default). */
    double remoteNumber(String key) {
        if (!enabled) return Double.NaN;
        FirebaseRemoteConfigValue v = remoteConfig.getValue(key);
        if (v.getSource() == FirebaseRemoteConfig.VALUE_SOURCE_STATIC) return Double.NaN;
        try { return v.asDouble(); } catch (IllegalArgumentException e) { return Double.NaN; }
    }

    // lazy anonymous sign-in: only players who buy, or share a challenge with reminders on, ever get an account
    private void signedIn(Consumer<Boolean> then) {
        if (!enabled) { then.accept(false); return; }
        final FirebaseAuth auth = FirebaseAuth.getInstance();
        if (auth.getCurrentUser() != null) { then.accept(true); return; }
        auth.signInAnonymously().addOnCompleteListener(t -> {
            if (!t.isSuccessful()) Log.w(TAG, "Firebase: anonymous sign-in failed: " + t.getException());
            then.accept(t.isSuccessful());
        });
    }

    // ---- friend nudges (firebase/functions/src/challenges.ts)
    private volatile String pushToken;
    private volatile String challengeId;

    /** Firebase on and this player gets pushes (13+ with reminders on). Fetches the token in the background. */
    boolean nudgesAvailable(boolean notificationsGranted) {
        if (!enabled || !notificationsGranted) return false;
        if (pushToken == null) FirebaseMessaging.getInstance().getToken().addOnSuccessListener(t -> pushToken = t);
        return pushToken != null;
    }

    void createChallenge(String day, int meters) {
        final String token = pushToken;
        if (token == null) return;
        signedIn(ok -> {
            if (!ok) return;
            Map<String, Object> data = new HashMap<>();
            data.put("day", day); data.put("meters", meters); data.put("token", token);
            FirebaseFunctions.getInstance().getHttpsCallable("createChallenge").call(data).addOnCompleteListener(t -> {
                if (!t.isSuccessful()) { Log.w(TAG, "Challenge: create failed " + t.getException()); return; }
                Object r = t.getResult().getData();
                if (r instanceof Map && ((Map<?, ?>) r).get("id") instanceof String) challengeId = (String) ((Map<?, ?>) r).get("id");
            });
        });
    }

    String pollChallengeId() { final String i = challengeId; challengeId = null; return i; }

    void challengeBeaten(String id, int meters) {
        signedIn(ok -> {
            if (!ok) return;
            Map<String, Object> data = new HashMap<>();
            data.put("id", id); data.put("meters", meters);
            FirebaseFunctions.getInstance().getHttpsCallable("challengeBeaten").call(data)
                    .addOnFailureListener(e -> Log.w(TAG, "Challenge: report failed " + e));
        });
    }

    /**
     * Server-side purchase check (Cloud Function verifyPurchase, which also credits the cloud wallet). Calls back
     * true when verified, false when the server rejects it. With Firebase off or the network down it calls back
     * true: the store already charged the player, and a lost connection must never lose their coins.
     */
    void verifyPurchase(String productId, String token, String orderId, Consumer<Boolean> done) {
        if (!enabled) { done.accept(true); return; }
        final Runnable call = () -> {
            Map<String, Object> data = new HashMap<>();
            data.put("platform", "android");
            data.put("productId", productId);
            data.put("token", token);
            data.put("orderId", orderId);
            FirebaseFunctions.getInstance().getHttpsCallable("verifyPurchase").call(data).addOnCompleteListener(t -> {
                if (t.isSuccessful()) {
                    Object r = t.getResult().getData();
                    boolean ok = r instanceof Map && Boolean.TRUE.equals(((Map<?, ?>) r).get("valid"));
                    Log.i(TAG, "Purchases: verify " + productId + " -> " + ok);
                    done.accept(ok);
                } else {
                    Log.w(TAG, "Purchases: verify unreachable (" + t.getException() + "); crediting anyway");
                    done.accept(true);
                }
            });
        };
        signedIn(ok -> { if (ok) call.run(); else done.accept(true); });
    }
}
