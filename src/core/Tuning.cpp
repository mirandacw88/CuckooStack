#include "Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace cs {

void Tuning::load(IRemoteConfig* rc) {
    if (!rc) return;
    auto in = [rc](const char* key, int& v) { if (auto x = rc->number(key)) v = int(std::lround(*x)); };
    auto fl = [rc](const char* key, float& v) { if (auto x = rc->number(key)) v = float(*x); };
    in("coins_per_m", metersPerCoin); in("coins_surge", coinsPerSurge); in("mission_coins", missionReward);
    in("mission_bonus", missionAllBonus);
    for (int i = 0; i < 7; ++i) in(("drop_" + std::to_string(i + 1)).c_str(), dailyDrop[i]);
    in("chest_coins", levelChestCoins); in("free_coins", freeCoins); in("noads_bonus", removeAdsBonus);
    in("starter_coins", starterCoins);
    in("continue_cost", continueCost); in("continue_cost2", continueCost2); in("boost_cost", boostCost);
    in("streak_repair", streakRepairCost); in("reroll_cost", rerollCost);
    in("ad_every_runs", adEveryRuns); in("ad_min_seconds", adMinSeconds); in("ad_grace_runs", adGraceRuns);
    in("rewarded_cap", rewardedDailyCap); fl("continue_secs", continueSeconds); in("boost_ads", boostAdsPerDay);
    in("free_coin_ads", freeCoinsPerDay); in("free_coin_cd", freeCoinsCooldown); in("reroll_ads", rerollAdsPerDay);
    in("featured_ads", featuredAdsNeeded); in("tryon_runs", tryOnRuns); in("double_min", doubleCoinsMin);
    in("xp_m", xpPerMeter); in("xp_mission", xpPerMission); in("lvl_base", levelBaseXp); in("lvl_step", levelStepXp);
    fl("dda_newbie", newbieBoost); in("dda_newbie_runs", newbieRuns);
    in("notif_quiet_start", notifQuietStart); in("notif_quiet_end", notifQuietEnd); in("notif_streak_hour", notifStreakHour);
    fl("event_coin_mult", eventCoinMult);
    eventCoinMult = std::clamp(eventCoinMult, 1.f, 5.f);
    if (metersPerCoin < 1) metersPerCoin = 1; // guard against a bad server value dividing by zero
}

} // namespace cs
