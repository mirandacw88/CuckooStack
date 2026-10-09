package com.cuckoostack.aerospheregames;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.NonNull;

import com.google.android.gms.ads.AdError;
import com.google.android.gms.ads.AdRequest;
import com.google.android.gms.ads.AdValue;
import com.google.android.gms.ads.FullScreenContentCallback;
import com.google.android.gms.ads.LoadAdError;
import com.google.android.gms.ads.MobileAds;
import com.google.android.gms.ads.RequestConfiguration;
import com.google.android.gms.ads.interstitial.InterstitialAd;
import com.google.android.gms.ads.interstitial.InterstitialAdLoadCallback;
import com.google.android.gms.ads.rewarded.RewardedAd;
import com.google.android.gms.ads.rewarded.RewardedAdLoadCallback;
import com.google.android.gms.ads.rewarded.ServerSideVerificationOptions;
import com.google.android.ump.ConsentInformation;
import com.google.android.ump.ConsentRequestParameters;
import com.google.android.ump.UserMessagingPlatform;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * AdMob behind Google's UMP consent flow: rewarded video (every opt-in reward in src/core/Monetization.h) and the
 * interstitial between runs. The game decides when; this class only loads, shows and reports.
 *
 * Nothing starts until both the splash is gone ({@link #start()}) and the player's age group is known
 * ({@link #setAudience(boolean)}, from the game's age screen): under-13s get child-directed, G-rated ads.
 * Native code polls {@link #rewardedState()}, {@link #consumeReward()} and {@link #adShowing()} via JNI.
 */
final class AdsManager {
    private static final String TAG = "CuckooStack";
    static final int LOADING = 0, READY = 1, UNAVAILABLE = 2; // mirrors cs::RewardedState
    private static final long[] RETRY_MS = {5_000, 15_000, 30_000, 60_000};

    private final Activity activity;
    private final FirebaseBridge firebase;
    private final ConsentInformation consent;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicBoolean sdkStarted = new AtomicBoolean(false);
    private final AtomicBoolean rewardEarned = new AtomicBoolean(false);
    private final AtomicInteger state = new AtomicInteger(LOADING);
    private volatile RewardedAd rewarded;
    private volatile InterstitialAd interstitial;
    private volatile boolean showing;
    private boolean loadingRewarded, loadingInterstitial, splashDone;
    private int audience = -1; // -1 unknown, 0 teen/adult, 1 child
    private int rewardedFailures, interstitialFailures;

    AdsManager(Activity activity, FirebaseBridge firebase) {
        this.activity = activity;
        this.firebase = firebase;
        this.consent = UserMessagingPlatform.getConsentInformation(activity);
    }

    /** The splash is gone: the consent form may show now (once the audience is known too). */
    void start() {
        main.post(() -> { splashDone = true; maybeBegin(); });
    }

    /** From the game's age screen (and on every launch once known). Must come before the SDK starts. */
    void setAudience(boolean child) {
        main.post(() -> {
            if (audience >= 0) return; // fixed for this process: the SDK reads it once at start
            audience = child ? 1 : 0;
            maybeBegin();
        });
    }

    private void maybeBegin() {
        if (!splashDone || audience < 0 || sdkStarted.get()) return;
        RequestConfiguration.Builder rc = MobileAds.getRequestConfiguration().toBuilder();
        if (audience == 1) {
            rc.setTagForChildDirectedTreatment(RequestConfiguration.TAG_FOR_CHILD_DIRECTED_TREATMENT_TRUE)
              .setTagForUnderAgeOfConsent(RequestConfiguration.TAG_FOR_UNDER_AGE_OF_CONSENT_TRUE)
              .setMaxAdContentRating(RequestConfiguration.MAX_AD_CONTENT_RATING_G);
        } else {
            rc.setMaxAdContentRating(RequestConfiguration.MAX_AD_CONTENT_RATING_T); // the game is rated 13+
        }
        MobileAds.setRequestConfiguration(rc.build());
        ConsentRequestParameters.Builder params = new ConsentRequestParameters.Builder();
        if (audience == 1) params.setTagForUnderAgeOfConsent(true);
        consent.requestConsentInfoUpdate(activity, params.build(),
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
            Log.i(TAG, "Ads: SDK ready (consent " + consent.getConsentStatus() + ", audience " + (audience == 1 ? "child" : "13+") + ")");
            main.post(() -> { loadRewarded(); loadInterstitial(); });
        }), "ads-init").start();
    }

    // ---------------------------------------------------------------- revenue -> Firebase Analytics (ad_impression)
    private void logPaid(String format, AdValue v, String adUnit) {
        Bundle b = new Bundle();
        b.putString("ad_platform", "admob");
        b.putString("ad_format", format);
        b.putString("ad_unit_name", adUnit);
        b.putDouble("value", v.getValueMicros() / 1_000_000.0);
        b.putString("currency", v.getCurrencyCode());
        firebase.logEventBundle("ad_impression", b);
    }

    // ---------------------------------------------------------------- rewarded
    private void loadRewarded() {
        if (rewarded != null || loadingRewarded || !consent.canRequestAds()) return;
        loadingRewarded = true;
        state.set(LOADING);
        RewardedAd.load(activity, BuildConfig.ADMOB_REWARDED_ID, new AdRequest.Builder().build(), new RewardedAdLoadCallback() {
            @Override
            public void onAdLoaded(@NonNull RewardedAd ad) {
                loadingRewarded = false;
                rewardedFailures = 0;
                ad.setOnPaidEventListener(v -> logPaid("rewarded", v, BuildConfig.ADMOB_REWARDED_ID));
                rewarded = ad;
                state.set(READY);
                Log.i(TAG, "Ads: rewarded loaded");
            }

            @Override
            public void onAdFailedToLoad(@NonNull LoadAdError error) {
                loadingRewarded = false;
                state.set(UNAVAILABLE);
                final long delay = RETRY_MS[Math.min(rewardedFailures++, RETRY_MS.length - 1)];
                Log.w(TAG, "Ads: rewarded failed to load (" + error.getMessage() + "), retry in " + delay / 1000 + " s");
                main.postDelayed(AdsManager.this::loadRewarded, delay);
            }
        });
    }

    int rewardedState() { return state.get(); }

    /** Called from native code. False when no ad is ready. {@code placement} goes to AdMob as custom data. */
    boolean showRewarded(String placement) {
        final RewardedAd ad = rewarded;
        if (ad == null) return false;
        rewarded = null;
        state.set(LOADING);
        showing = true;
        main.post(() -> {
            ad.setServerSideVerificationOptions(new ServerSideVerificationOptions.Builder().setCustomData(placement).build());
            ad.setFullScreenContentCallback(new FullScreenContentCallback() {
                @Override
                public void onAdDismissedFullScreenContent() { showing = false; loadRewarded(); }

                @Override
                public void onAdFailedToShowFullScreenContent(@NonNull AdError error) {
                    Log.w(TAG, "Ads: rewarded failed to show: " + error.getMessage());
                    showing = false;
                    loadRewarded();
                }
            });
            Log.i(TAG, "Ads: showing rewarded (" + placement + ")");
            ad.show(activity, reward -> {
                Log.i(TAG, "Ads: reward earned (" + placement + ")");
                rewardEarned.set(true); // granted by the game on its next frame
            });
        });
        return true;
    }

    /** Called from native code every frame: true once per earned reward. */
    boolean consumeReward() { return rewardEarned.getAndSet(false); }

    // ---------------------------------------------------------------- interstitial
    private void loadInterstitial() {
        if (interstitial != null || loadingInterstitial || !consent.canRequestAds()) return;
        loadingInterstitial = true;
        InterstitialAd.load(activity, BuildConfig.ADMOB_INTERSTITIAL_ID, new AdRequest.Builder().build(), new InterstitialAdLoadCallback() {
            @Override
            public void onAdLoaded(@NonNull InterstitialAd ad) {
                loadingInterstitial = false;
                interstitialFailures = 0;
                ad.setOnPaidEventListener(v -> logPaid("interstitial", v, BuildConfig.ADMOB_INTERSTITIAL_ID));
                interstitial = ad;
                Log.i(TAG, "Ads: interstitial loaded");
            }

            @Override
            public void onAdFailedToLoad(@NonNull LoadAdError error) {
                loadingInterstitial = false;
                final long delay = RETRY_MS[Math.min(interstitialFailures++, RETRY_MS.length - 1)];
                Log.w(TAG, "Ads: interstitial failed to load (" + error.getMessage() + "), retry in " + delay / 1000 + " s");
                main.postDelayed(AdsManager.this::loadInterstitial, delay);
            }
        });
    }

    boolean interstitialReady() { return interstitial != null; }

    boolean showInterstitial() {
        final InterstitialAd ad = interstitial;
        if (ad == null) return false;
        interstitial = null;
        showing = true;
        main.post(() -> {
            ad.setFullScreenContentCallback(new FullScreenContentCallback() {
                @Override
                public void onAdDismissedFullScreenContent() { showing = false; loadInterstitial(); }

                @Override
                public void onAdFailedToShowFullScreenContent(@NonNull AdError error) {
                    Log.w(TAG, "Ads: interstitial failed to show: " + error.getMessage());
                    showing = false;
                    loadInterstitial();
                }
            });
            Log.i(TAG, "Ads: showing interstitial");
            ad.show(activity);
        });
        return true;
    }

    /** True from show until the ad is dismissed (the game holds the next run meanwhile). */
    boolean adShowing() { return showing; }

    // ---------------------------------------------------------------- privacy
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
