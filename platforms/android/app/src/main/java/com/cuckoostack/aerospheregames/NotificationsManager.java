package com.cuckoostack.aerospheregames;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.content.ContextCompat;
import androidx.work.Data;
import androidx.work.OneTimeWorkRequest;
import androidx.work.WorkManager;

import com.google.firebase.FirebaseApp;
import com.google.firebase.messaging.FirebaseMessaging;

import java.util.concurrent.TimeUnit;

/**
 * Reminders planned by the game (src/core/Reminders.h), delivered by WorkManager (survives reboots, battery-friendly),
 * plus Firebase Cloud Messaging for console campaigns. Nothing here runs until a 13+ player turns reminders on:
 * FCM auto-init is off in the manifest and only switched on after the permission is granted.
 */
final class NotificationsManager {
    static final String CHANNEL = "reminders";
    static final String EXTRA_ID = "cs_reminder_id";
    private static final String TAG = "reminder";
    private static final int REQUEST_CODE = 7701;

    private final Activity activity;
    private final SharedPreferences prefs;
    private volatile int opened = -1;

    NotificationsManager(Activity activity) {
        this.activity = activity;
        this.prefs = activity.getSharedPreferences("cs_notifications", Context.MODE_PRIVATE);
        createChannel(activity);
        if (permission() == 1) enablePush();
    }

    static void createChannel(Context c) {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationChannel ch = new NotificationChannel(CHANNEL, c.getString(R.string.reminders_channel), NotificationManager.IMPORTANCE_DEFAULT);
        ch.setDescription(c.getString(R.string.reminders_channel_desc));
        c.getSystemService(NotificationManager.class).createNotificationChannel(ch);
    }

    /** 0 unknown, 1 granted, 2 denied (cs::NotifPermission) */
    int permission() {
        if (Build.VERSION.SDK_INT >= 33) {
            if (ContextCompat.checkSelfPermission(activity, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) return 1;
            return prefs.getBoolean("asked", false) ? 2 : 0;
        }
        return NotificationManagerCompat.from(activity).areNotificationsEnabled() ? 1 : 2;
    }

    void requestPermission() {
        if (Build.VERSION.SDK_INT >= 33 && permission() != 1) {
            prefs.edit().putBoolean("asked", true).apply();
            activity.runOnUiThread(() -> ActivityCompat.requestPermissions(activity, new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_CODE));
        } else if (permission() == 1) {
            enablePush();
        }
    }

    /** From CuckooActivity.onRequestPermissionsResult. */
    void onPermissionResult(boolean granted) { if (granted) enablePush(); }

    private void enablePush() {
        if (FirebaseApp.getApps(activity).isEmpty()) return;
        FirebaseMessaging fm = FirebaseMessaging.getInstance();
        fm.setAutoInitEnabled(true);
        fm.subscribeToTopic("all");
        fm.subscribeToTopic("android");
    }

    /** Replaces every pending reminder (parallel arrays; at = seconds since the epoch). */
    void replaceAll(int[] ids, long[] at, String[] titles, String[] bodies) {
        WorkManager wm = WorkManager.getInstance(activity);
        wm.cancelAllWorkByTag(TAG);
        final long now = System.currentTimeMillis() / 1000;
        for (int i = 0; i < ids.length; i++) {
            final long wait = at[i] - now;
            if (wait <= 60) continue;
            Data data = new Data.Builder().putInt("id", ids[i]).putString("title", titles[i]).putString("body", bodies[i]).build();
            wm.enqueue(new OneTimeWorkRequest.Builder(ReminderWorker.class).setInitialDelay(wait, TimeUnit.SECONDS).setInputData(data).addTag(TAG).build());
        }
    }

    void setOpened(int id) { opened = id; }

    /** The reminder the app was opened from, once; -1 if none. */
    int consumeOpened() { final int o = opened; opened = -1; return o; }
}
