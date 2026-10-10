// Every tunable number of the meta-game: coin economy, ad rules, progression, notifications.
// Compiled-in defaults below; Firebase Remote Config can override any of them by the key in quotes (load()).
#pragma once

#include "Services.h"

namespace cs {

struct Tuning {
    // ---- coins earned
    int metersPerCoin = 25;        // "coins_per_m"     1 coin per this many metres in a run
    int coinsPerSurge = 2;         // "coins_surge"
    int missionReward = 50;        // "mission_coins"
    int missionAllBonus = 150;     // "mission_bonus"   all three daily missions done
    int dailyDrop[7] = {20, 30, 40, 60, 80, 100, 250}; // "drop_1".."drop_7"  7-day login ladder, then it repeats
    int levelChestCoins = 100;     // "chest_coins"
    int clearCoins = 50;           // "clear_coins"     coins for clearing a course level ...
    int clearCoinsStep = 10;       // "clear_coins_step" ... plus this many per level number
    int clearXp = 100;             // "clear_xp"        XP for clearing a course level
    int freeCoins = 50;            // "free_coins"      shop tile, rewarded ad
    int removeAdsBonus = 500;      // "noads_bonus"     one-off with the Remove Ads purchase
    int starterCoins = 1500;       // "starter_coins"

    // ---- coins spent
    int continueCost = 100;        // "continue_cost"   first continue in a run without the ad
    int continueCost2 = 300;       // "continue_cost2"  second continue (coins only)
    int boostCost = 150;           // "boost_cost"      Head Start / Overclock
    int streakRepairCost = 200;    // "streak_repair"
    int rerollCost = 50;           // "reroll_cost"

    // ---- ads (Monetization.h)
    int adEveryRuns = 3;           // "ad_every_runs"   interstitial at most every N runs ...
    int adMinSeconds = 90;         // "ad_min_seconds"  ... and N seconds
    int adGraceRuns = 5;           // "ad_grace_runs"   no interstitials before this many lifetime runs
    int rewardedDailyCap = 30;     // "rewarded_cap"
    float continueSeconds = 4.f;   // "continue_secs"   Continue panel countdown
    int boostAdsPerDay = 3;        // "boost_ads"
    int freeCoinsPerDay = 5;       // "free_coin_ads"
    int freeCoinsCooldown = 1800;  // "free_coin_cd"    seconds
    int rerollAdsPerDay = 1;       // "reroll_ads"
    int featuredAdsNeeded = 5;     // "featured_ads"    ads to unlock the featured outfit of the week
    int tryOnRuns = 3;             // "tryon_runs"
    int doubleCoinsMin = 10;       // "double_min"      "x2 coins" offered when a run earned at least this

    // ---- progression
    int xpPerMeter = 1;            // "xp_m"
    int xpPerMission = 50;         // "xp_mission"
    int levelBaseXp = 150;         // "lvl_base"        level n -> n+1 needs base + step * (n - 1)
    int levelStepXp = 75;          // "lvl_step"

    // ---- difficulty
    float newbieBoost = 0.25f;     // "dda_newbie"      extra assist on a player's first runs, fading out
    int newbieRuns = 6;            // "dda_newbie_runs"

    // ---- live events (set from the Firebase console: Remote Config, e.g. with a weekend condition)
    float eventCoinMult = 1.f;     // "event_coin_mult" coins from runs and missions x this (2 = a double-coins event)

    // ---- reminders (13+ only)
    int notifQuietStart = 21;      // "notif_quiet_start" local hour
    int notifQuietEnd = 9;         // "notif_quiet_end"
    int notifStreakHour = 19;      // "notif_streak_hour"

    void load(IRemoteConfig* rc);
};

} // namespace cs
