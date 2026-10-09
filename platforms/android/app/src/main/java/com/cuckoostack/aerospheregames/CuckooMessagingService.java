package com.cuckoostack.aerospheregames;

import androidx.annotation.NonNull;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

/** Campaign pushes from the Firebase console. In the background the system shows them; this covers the foreground. */
public final class CuckooMessagingService extends FirebaseMessagingService {
    @Override
    public void onMessageReceived(@NonNull RemoteMessage message) {
        RemoteMessage.Notification n = message.getNotification();
        if (n == null) return;
        ReminderWorker.show(this, 0, n.getTitle(), n.getBody()); // id 0 = a campaign push
    }

    @Override
    public void onNewToken(@NonNull String token) { /* topics are (re)subscribed by NotificationsManager */ }
}
