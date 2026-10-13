// Tuning (Tuning.cpp): the compiled-in numbers of the meta-game and Firebase Remote Config overriding them by key,
// with guards against bad server values.
#include "TestUtil.h"
#include "core/Tuning.h"

#include <map>

using namespace cs;
using namespace test;

namespace {

struct FakeRemote : IRemoteConfig {
    std::map<std::string, double> v;
    std::optional<double> number(const std::string& key) override {
        const auto it = v.find(key);
        if (it == v.end()) return std::nullopt;
        return it->second;
    }
};

// the int fields and their keys, as Tuning.h documents them
struct IntKey { const char* key; int Tuning::*field; };
const IntKey kInts[] = {
    {"coins_per_m", &Tuning::metersPerCoin}, {"coins_surge", &Tuning::coinsPerSurge}, {"mission_coins", &Tuning::missionReward},
    {"mission_bonus", &Tuning::missionAllBonus}, {"chest_coins", &Tuning::levelChestCoins}, {"clear_coins", &Tuning::clearCoins},
    {"clear_coins_step", &Tuning::clearCoinsStep}, {"clear_xp", &Tuning::clearXp}, {"free_coins", &Tuning::freeCoins},
    {"noads_bonus", &Tuning::removeAdsBonus}, {"starter_coins", &Tuning::starterCoins}, {"continue_cost", &Tuning::continueCost},
    {"continue_cost2", &Tuning::continueCost2}, {"boost_cost", &Tuning::boostCost}, {"streak_repair", &Tuning::streakRepairCost},
    {"reroll_cost", &Tuning::rerollCost}, {"ad_every_runs", &Tuning::adEveryRuns}, {"ad_min_seconds", &Tuning::adMinSeconds},
    {"ad_grace_runs", &Tuning::adGraceRuns}, {"rewarded_cap", &Tuning::rewardedDailyCap}, {"boost_ads", &Tuning::boostAdsPerDay},
    {"free_coin_ads", &Tuning::freeCoinsPerDay}, {"free_coin_cd", &Tuning::freeCoinsCooldown}, {"reroll_ads", &Tuning::rerollAdsPerDay},
    {"featured_ads", &Tuning::featuredAdsNeeded}, {"tryon_runs", &Tuning::tryOnRuns}, {"double_min", &Tuning::doubleCoinsMin},
    {"xp_m", &Tuning::xpPerMeter}, {"xp_mission", &Tuning::xpPerMission}, {"lvl_base", &Tuning::levelBaseXp},
    {"lvl_step", &Tuning::levelStepXp}, {"dda_newbie_runs", &Tuning::newbieRuns}, {"notif_quiet_start", &Tuning::notifQuietStart},
    {"notif_quiet_end", &Tuning::notifQuietEnd}, {"notif_streak_hour", &Tuning::notifStreakHour},
};

void defaults() {
    Tuning t;
    t.load(nullptr);
    Tuning d;
    bool same = true;
    for (const IntKey& k : kInts) same = same && t.*(k.field) == d.*(k.field);
    check(same && t.continueSeconds == 4.f && t.eventCoinMult == 1.f, "no remote config: the compiled-in defaults");
    FakeRemote empty;
    t.load(&empty);
    same = true;
    for (const IntKey& k : kInts) same = same && t.*(k.field) == d.*(k.field);
    check(same, "remote config without our keys: defaults kept");
    check(d.dailyDrop[0] == 20 && d.dailyDrop[6] == 250 && d.metersPerCoin == 25 && d.boostCost == 150, "defaults as documented");
}

void overrides() {
    FakeRemote rc;
    int n = 1000;
    for (const IntKey& k : kInts) rc.v[k.key] = ++n;
    for (int i = 0; i < 7; ++i) rc.v["drop_" + std::to_string(i + 1)] = 500 + i;
    rc.v["continue_secs"] = 6.5;
    rc.v["dda_newbie"] = 0.4;
    rc.v["event_coin_mult"] = 2;
    Tuning t;
    t.load(&rc);
    bool all = true;
    for (const IntKey& k : kInts) all = all && t.*(k.field) == int(rc.v[k.key]);
    check(all, "every documented int key overrides its field");
    bool drops = true;
    for (int i = 0; i < 7; ++i) drops = drops && t.dailyDrop[i] == 500 + i;
    check(drops, "drop_1..drop_7: the daily-drop ladder");
    check(t.continueSeconds == 6.5f && std::abs(t.newbieBoost - 0.4f) < 1e-6f && t.eventCoinMult == 2.f, "the float keys");
}

void guards() {
    FakeRemote rc;
    rc.v["coins_per_m"] = 0;
    rc.v["boost_cost"] = 149.6;
    rc.v["event_coin_mult"] = 50;
    Tuning t;
    t.load(&rc);
    check(t.metersPerCoin == 1, "coins_per_m 0 (a divisor): clamped to 1");
    check(t.boostCost == 150, "ints are rounded, not truncated");
    check(t.eventCoinMult == 5.f, "event_coin_mult: at most 5x");
    rc.v["event_coin_mult"] = 0.2;
    t.load(&rc);
    check(t.eventCoinMult == 1.f, "event_coin_mult: never below 1x (an event can't cut coins)");
    rc.v["coins_per_m"] = -3;
    t.load(&rc);
    check(t.metersPerCoin == 1, "a negative coins_per_m: clamped to 1");
}

} // namespace

int main() {
    defaults();
    overrides();
    guards();
    return finish("Tuning");
}
