package com.cuckoostack.aerospheregames;

import android.app.Activity;
import android.util.Log;

import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.PlayGamesSdk;
import com.google.android.gms.games.leaderboard.LeaderboardVariant;

/**
 * Daily leaderboard on Play Games Services v2 (config/store/leaderboards.env -> per-flavour string resources).
 * Until the Play Games project exists the IDs are empty and the game hides its leaderboard button.
 * Never used for under-13 players (the game doesn't call it for them).
 */
final class LeaderboardsManager {
    private static final String TAG = "CuckooStack";
    private final Activity activity;
    private final String leaderboard;
    private final boolean enabled;

    LeaderboardsManager(Activity activity) {
        this.activity = activity;
        final String appId = activity.getString(R.string.game_services_project_id);
        leaderboard = activity.getString(R.string.play_leaderboard_id);
        enabled = !appId.isEmpty() && !leaderboard.isEmpty();
        if (enabled) PlayGamesSdk.initialize(activity);
    }

    boolean available() { return enabled; }

    /** Quietly: only if the player is already signed in to Play Games (automatic on most devices). */
    void submit(long meters) {
        if (!enabled) return;
        activity.runOnUiThread(() -> PlayGames.getGamesSignInClient(activity).isAuthenticated().addOnSuccessListener(r -> {
            if (r.isAuthenticated()) PlayGames.getLeaderboardsClient(activity).submitScore(leaderboard, meters);
        }));
    }

    /** Today's board; signs in first if needed. */
    void show() {
        if (!enabled) return;
        activity.runOnUiThread(() -> PlayGames.getGamesSignInClient(activity).isAuthenticated().addOnSuccessListener(r -> {
            if (r.isAuthenticated()) open();
            else PlayGames.getGamesSignInClient(activity).signIn().addOnSuccessListener(s -> { if (s.isAuthenticated()) open(); });
        }));
    }

    private void open() {
        PlayGames.getLeaderboardsClient(activity)
                .getLeaderboardIntent(leaderboard, LeaderboardVariant.TIME_SPAN_DAILY, LeaderboardVariant.COLLECTION_PUBLIC)
                .addOnSuccessListener(intent -> activity.startActivityForResult(intent, 9004))
                .addOnFailureListener(e -> Log.w(TAG, "Leaderboards: " + e.getMessage()));
    }
}
