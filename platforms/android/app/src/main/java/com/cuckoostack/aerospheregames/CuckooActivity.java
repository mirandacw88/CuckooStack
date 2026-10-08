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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();
        ads = new AdsManager(this);
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
    public boolean adsShowRewarded() { return ads != null && ads.showRewarded(); }
    public boolean adsConsumeReward() { return ads != null && ads.consumeReward(); }
    public boolean adsPrivacyOptionsRequired() { return ads != null && ads.privacyOptionsRequired(); }
    public void adsShowPrivacyOptions() { if (ads != null) ads.showPrivacyOptions(); }

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
