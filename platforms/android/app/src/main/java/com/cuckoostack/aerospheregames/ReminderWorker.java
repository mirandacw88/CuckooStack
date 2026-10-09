package com.cuckoostack.aerospheregames;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;

import androidx.annotation.NonNull;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

/** Posts one planned reminder (NotificationsManager). Tapping it opens the game, which logs notif_open. */
public final class ReminderWorker extends Worker {
    public ReminderWorker(@NonNull Context context, @NonNull WorkerParameters params) { super(context, params); }

    @NonNull
    @Override
    public Result doWork() {
        final Context c = getApplicationContext();
        final int id = getInputData().getInt("id", 0);
        show(c, id, getInputData().getString("title"), getInputData().getString("body"));
        return Result.success();
    }

    @SuppressWarnings("MissingPermission") // areNotificationsEnabled() is checked first
    static void show(Context c, int id, String title, String body) {
        if (!NotificationManagerCompat.from(c).areNotificationsEnabled()) return;
        NotificationsManager.createChannel(c);
        Intent open = new Intent(c, CuckooActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP).putExtra(NotificationsManager.EXTRA_ID, id);
        PendingIntent pi = PendingIntent.getActivity(c, id, open, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        NotificationCompat.Builder b = new NotificationCompat.Builder(c, NotificationsManager.CHANNEL)
                .setSmallIcon(R.drawable.ic_stat_reminder)
                .setColor(0xFFFF2BD6)
                .setContentTitle(title)
                .setContentText(body)
                .setAutoCancel(true)
                .setContentIntent(pi);
        NotificationManagerCompat.from(c).notify(id, b.build());
    }
}
