package com.cuckoostack.aerospheregames;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;

/** Learns which app the player picked in the share sheet (for replay_shared analytics). */
public final class ShareTargetReceiver extends BroadcastReceiver {
    static volatile String chosen = "";

    @Override
    public void onReceive(Context context, Intent intent) {
        ComponentName c = intent.getParcelableExtra(Intent.EXTRA_CHOSEN_COMPONENT);
        chosen = c != null ? c.getPackageName() : "unknown";
    }
}
