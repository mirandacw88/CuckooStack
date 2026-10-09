package com.cuckoostack.aerospheregames;

import android.app.AlertDialog;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;

import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.google.androidgamesdk.GameActivity;

/**
 * Thin Java shell around the native game (src/platform/android/AndroidMain.cpp).
 * Native code calls {@link #haptic(int)} and {@link #showFatalError(String)} through JNI.
 */
public class CuckooActivity extends GameActivity {

    /** Logo hold after launch before fading to the title screen (ms). */
    private static final long SPLASH_HOLD_MS = 1900;
    private static final int SPLASH_FADE_MS = 400;
    private static final int SPLASH_MAX_WIDTH_DP = 520;

    private View splash;
    private AdsManager ads;
    private FirebaseBridge firebase;
    private StoreManager store;
    private NotificationsManager notifications;
    private LeaderboardsManager leaderboards;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // created before super.onCreate: the native thread starts there and may call into them right away
        firebase = new FirebaseBridge(this);
        ads = new AdsManager(this, firebase);
        store = new StoreManager(this, firebase);
        notifications = new NotificationsManager(this);
        leaderboards = new LeaderboardsManager(this);
        readReminderTap(getIntent());
        super.onCreate(savedInstanceState);
        hideSystemBars();
        if (savedInstanceState == null) showSplash(); // not again on configuration changes
        else ads.start();
    }

    /** Opening splash: the Aerosphere Games logo centred on its background colour, over the game's surface while it starts. */
    private void showSplash() {
        FrameLayout layer = new FrameLayout(this);
        layer.setBackgroundColor(getResources().getColor(R.color.splash_bg, getTheme())); // = logo background
        layer.setClickable(true); // swallow touches so a tap skips the splash instead of starting a run
        layer.setOnClickListener(v -> dismissSplash());

        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.splash_logo);
        logo.setAdjustViewBounds(true);
        logo.setScaleType(ImageView.ScaleType.FIT_CENTER);
        DisplayMetrics dm = getResources().getDisplayMetrics();
        int w = Math.min(Math.round(Math.min(dm.widthPixels, dm.heightPixels) * 0.8f), Math.round(SPLASH_MAX_WIDTH_DP * dm.density));
        layer.addView(logo, new FrameLayout.LayoutParams(w, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER));

        addContentView(layer, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        splash = layer;
        layer.postDelayed(this::dismissSplash, SPLASH_HOLD_MS);
    }

    private void dismissSplash() {
        final View s = splash;
        if (s == null) return;
        splash = null;
        ads.start(); // consent form (where required) only after the splash, never over it
        s.setClickable(false);
        s.animate().alpha(0f).setDuration(SPLASH_FADE_MS).withEndAction(() -> {
            ViewGroup parent = (ViewGroup) s.getParent();
            if (parent != null) parent.removeView(s);
        });
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        readReminderTap(intent);
    }

    /** Opened by tapping a reminder (ReminderWorker) or a campaign push: the game logs it as notif_open. */
    private static volatile String pendingLink = "";

    private void readReminderTap(android.content.Intent intent) {
        if (intent != null && android.content.Intent.ACTION_VIEW.equals(intent.getAction()) && intent.getData() != null)
            pendingLink = intent.getData().toString(); // a challenge link; the game reads it on its next frame
        if (intent == null || notifications == null) return;
        if (intent.hasExtra(NotificationsManager.EXTRA_ID)) notifications.setOpened(intent.getIntExtra(NotificationsManager.EXTRA_ID, 0));
        else if (intent.getExtras() != null && intent.getExtras().containsKey("google.message_id")) notifications.setOpened(0);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @androidx.annotation.NonNull String[] permissions, @androidx.annotation.NonNull int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (notifications != null && results.length > 0) notifications.onPermissionResult(results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        WindowInsetsControllerCompat c = WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
        c.hide(WindowInsetsCompat.Type.systemBars());
        c.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }

    /** navigator.vibrate() replacement. Called from the native game thread; Vibrator is thread-safe. */
    @SuppressWarnings("deprecation")
    public void haptic(int milliseconds) {
        Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (v == null || !v.hasVibrator()) return;
        if (Build.VERSION.SDK_INT >= 26) v.vibrate(VibrationEffect.createOneShot(milliseconds, VibrationEffect.DEFAULT_AMPLITUDE));
        else v.vibrate(milliseconds);
    }

    /** Thermal status for the quality governor (src/graphics/Quality.h): 0 none ... 6 shutdown; 0 before Android 10. */
    public int thermalStatus() {
        if (Build.VERSION.SDK_INT < 29) return 0;
        android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
        return pm == null ? 0 : pm.getCurrentThermalStatus();
    }

    /** Screen density (pixels per dp) for the internal render resolution. */
    public float displayDensity() { return getResources().getDisplayMetrics().density; }

    // ---- ads, called from native code (src/platform/android/AndroidMain.cpp)
    public int adsRewardedState() { return ads != null ? ads.rewardedState() : AdsManager.UNAVAILABLE; }
    public boolean adsShowRewarded(String placement) { return ads != null && ads.showRewarded(placement); }
    public boolean adsConsumeReward() { return ads != null && ads.consumeReward(); }
    public boolean adsPrivacyOptionsRequired() { return ads != null && ads.privacyOptionsRequired(); }
    public void adsShowPrivacyOptions() { if (ads != null) ads.showPrivacyOptions(); }
    public boolean adsInterstitialReady() { return ads != null && ads.interstitialReady(); }
    public boolean adsShowInterstitial() { return ads != null && ads.showInterstitial(); }
    public boolean adsShowing() { return ads != null && ads.adShowing(); }
    public void adsSetAudience(boolean child) {
        if (ads != null) ads.setAudience(child);
        if (firebase != null) firebase.setChild(child);
    }

    // ---- analytics + remote config
    public void fbEvent(String name, String[] kv) { if (firebase != null) firebase.logEvent(name, kv); }
    public void fbUserProperty(String name, String value) { if (firebase != null) firebase.setUserProperty(name, value); }
    public double fbRemoteNumber(String key) { return firebase != null ? firebase.remoteNumber(key) : Double.NaN; }

    // ---- leaderboards
    public boolean boardsAvailable() { return leaderboards != null && leaderboards.available(); }
    public void boardsSubmit(int meters) { if (leaderboards != null) leaderboards.submit(meters); }
    public void boardsShow() { if (leaderboards != null) leaderboards.show(); }

    // ---- friend nudges
    public boolean nudgesAvailable() { return firebase != null && notifications != null && firebase.nudgesAvailable(notifications.permission() == 1); }
    public void nudgeCreate(String day, int meters) { if (firebase != null) firebase.createChallenge(day, meters); }
    public String nudgePollId() { return firebase != null ? firebase.pollChallengeId() : null; }
    public void nudgeBeaten(String id, int meters) { if (firebase != null) firebase.challengeBeaten(id, meters); }

    // ---- links
    public String linkPoll() { final String l = pendingLink; pendingLink = ""; return l; }

    // ---- reminders
    public int notifPermission() { return notifications != null ? notifications.permission() : 0; }
    public void notifRequestPermission() { if (notifications != null) notifications.requestPermission(); }
    public void notifReplaceAll(int[] ids, long[] at, String[] titles, String[] bodies) { if (notifications != null) notifications.replaceAll(ids, at, titles, bodies); }
    public int notifConsumeOpened() { return notifications != null ? notifications.consumeOpened() : -1; }

    // ---- Share Replay (AndroidReplay.cpp): the clip (or, without one, the text) through the system share sheet
    public void replayShare(String path, String text) {
        runOnUiThread(() -> {
            android.content.Intent send = new android.content.Intent(android.content.Intent.ACTION_SEND);
            send.putExtra(android.content.Intent.EXTRA_TEXT, text);
            if (path != null && !path.isEmpty()) {
                android.net.Uri uri = androidx.core.content.FileProvider.getUriForFile(this, getPackageName() + ".replays", new java.io.File(path));
                send.setType("video/mp4");
                send.putExtra(android.content.Intent.EXTRA_STREAM, uri);
                send.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } else {
                send.setType("text/plain");
            }
            android.app.PendingIntent chosen = android.app.PendingIntent.getBroadcast(this, 0, new android.content.Intent(this, ShareTargetReceiver.class),
                    android.app.PendingIntent.FLAG_UPDATE_CURRENT | android.app.PendingIntent.FLAG_MUTABLE);
            startActivity(android.content.Intent.createChooser(send, "Share your run", chosen.getIntentSender()));
        });
    }
    public String replayPollShared() { final String c = ShareTargetReceiver.chosen; ShareTargetReceiver.chosen = ""; return c; }

    // ---- in-app purchases
    public String storeProducts() { return store != null ? store.products() : ""; }
    public boolean storePurchase(String id) { return store != null && store.purchase(id); }
    public void storeRestore() { if (store != null) store.restore(); }
    public String storePollEvent() { return store != null ? store.pollEvent() : null; }

    /** Explicit diagnostic exit when Vulkan cannot start (no loader, no compatible GPU, device lost). */
    public void showFatalError(final String message) {
        runOnUiThread(() -> new AlertDialog.Builder(this)
                .setTitle("Graphics unavailable")
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton("Close", (dialog, which) -> finish())
                .show());
    }
}
