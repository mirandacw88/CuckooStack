package com.cuckoostack.aerospheregames;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.NonNull;

import com.google.android.gms.ads.AdError;
import com.google.android.gms.ads.AdRequest;
import com.google.android.gms.ads.FullScreenContentCallback;
import com.google.android.gms.ads.LoadAdError;
import com.google.android.gms.ads.MobileAds;
import com.google.android.gms.ads.rewarded.RewardedAd;
import com.google.android.gms.ads.rewarded.RewardedAdLoadCallback;
import com.google.android.ump.ConsentInformation;
import com.google.android.ump.ConsentRequestParameters;
import com.google.android.ump.UserMessagingPlatform;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * AdMob rewarded video behind Google's UMP consent flow. Out of lives, the game (src/core/Lives.h) offers "Watch ad ·
 * +3 lives"; it polls {@link #rewardedState()} for the button and {@link #consumeReward()} for the grant via JNI.
 */
final class AdsManager {
    private static final String TAG = "CuckooStack";
    static final int LOADING = 0, READY = 1, UNAVAILABLE = 2; // mirrors cs::RewardedState
    private static final long[] RETRY_MS = {5_000, 15_000, 30_000, 60_000};

    private final Activity activity;
    private final ConsentInformation consent;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicBoolean sdkStarted = new AtomicBoolean(false);
    private final AtomicBoolean rewardEarned = new AtomicBoolean(false);
    private final AtomicInteger state = new AtomicInteger(LOADING);
    private volatile RewardedAd rewarded;
    private volatile boolean loading;
    private int failures;

    AdsManager(Activity activity) {
        this.activity = activity;
        this.consent = UserMessagingPlatform.getConsentInformation(activity);
    }

    /** Consent first (form only where the law requires it), then start the SDK and preload a rewarded ad. */
    void start() {
        ConsentRequestParameters params = new ConsentRequestParameters.Builder().build();
        consent.requestConsentInfoUpdate(activity, params,
                () -> UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity, formError -> {
                    if (formError != null) Log.w(TAG, "Ads: consent form: " + formError.getMessage());
                    startSdkIfAllowed();
                }),
                requestError -> {
                    Log.w(TAG, "Ads: consent update failed: " + requestError.getMessage());
                    startSdkIfAllowed(); // a previous session's consent may still allow ads
                });
        startSdkIfAllowed(); // returning users: consent from the last session applies immediately
    }

    private void startSdkIfAllowed() {
        if (!consent.canRequestAds()) { state.set(UNAVAILABLE); return; }
        if (!sdkStarted.compareAndSet(false, true)) return;
        new Thread(() -> MobileAds.initialize(activity, status -> {
            Log.i(TAG, "Ads: SDK ready (consent " + consent.getConsentStatus() + ")");
            main.post(this::load);
        }), "ads-init").start();
    }

    private void load() {
        if (rewarded != null || loading || !consent.canRequestAds()) return;
        loading = true;
        state.set(LOADING);
        RewardedAd.load(activity, BuildConfig.ADMOB_REWARDED_ID, new AdRequest.Builder().build(), new RewardedAdLoadCallback() {
            @Override
            public void onAdLoaded(@NonNull RewardedAd ad) {
                loading = false;
                failures = 0;
                rewarded = ad;
                state.set(READY);
                Log.i(TAG, "Ads: rewarded loaded");
            }

            @Override
            public void onAdFailedToLoad(@NonNull LoadAdError error) {
                loading = false;
                state.set(UNAVAILABLE);
                final long delay = RETRY_MS[Math.min(failures++, RETRY_MS.length - 1)];
                Log.w(TAG, "Ads: rewarded failed to load (" + error.getMessage() + "), retry in " + delay / 1000 + " s");
                main.postDelayed(AdsManager.this::load, delay);
            }
        });
    }

    int rewardedState() { return state.get(); }

    /** Called from native code. False when no ad is ready. */
    boolean showRewarded() {
        final RewardedAd ad = rewarded;
        if (ad == null) return false;
        rewarded = null;
        state.set(LOADING);
        main.post(() -> {
            ad.setFullScreenContentCallback(new FullScreenContentCallback() {
                @Override
                public void onAdDismissedFullScreenContent() { load(); }

                @Override
                public void onAdFailedToShowFullScreenContent(@NonNull AdError error) {
                    Log.w(TAG, "Ads: rewarded failed to show: " + error.getMessage());
                    load();
                }
            });
            Log.i(TAG, "Ads: showing rewarded");
            ad.show(activity, reward -> {
                Log.i(TAG, "Ads: reward earned");
                rewardEarned.set(true); // granted by the game on its next frame
            });
        });
        return true;
    }

    /** Called from native code every frame: true once per earned reward. */
    boolean consumeReward() { return rewardEarned.getAndSet(false); }

    boolean privacyOptionsRequired() {
        return consent.getPrivacyOptionsRequirementStatus() == ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED;
    }

    void showPrivacyOptions() {
        main.post(() -> UserMessagingPlatform.showPrivacyOptionsForm(activity, error -> {
            if (error != null) Log.w(TAG, "Ads: privacy options: " + error.getMessage());
            startSdkIfAllowed();
        }));
    }
}
