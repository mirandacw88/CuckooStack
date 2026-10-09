// When ads may show. Every ad the game can trigger goes through AdPolicy; the numbers live in Tuning.h.
//
//   Interstitial  game-over screen appearing, if ALL hold: >= adEveryRuns runs and >= adMinSeconds since the last ad
//                 (of any kind), >= adGraceRuns lifetime runs, no rewarded ad watched at this game over, no purchase
//                 this session, Remove Ads not owned. Never during a run.
//   Rewarded      only when the player taps an offer; per-placement limits below, plus rewardedDailyCap in total.
#pragma once

#include "Meta.h"
#include "Tuning.h"

namespace cs {

enum class Placement : uint8_t {
    Continue,        // hen died: revive (once per run)
    DoubleCoins,     // game over: x2 the run's coins
    Boost,           // title: free Head Start / Overclock
    FreeCoins,       // shop tile
    DropDouble,      // daily drop claim x2
    MissionReroll,   // swap one daily mission
    MissionBonus,    // x2 the all-missions bonus
    StreakSave,      // missed exactly one day (13+ only)
    Featured,        // progress toward the featured outfit of the week
    TryOn,           // wear a locked outfit for a few runs
    ChestDouble,     // level-up chest x2
    Count
};
const char* placementName(Placement p); // analytics / AdMob custom data

class AdPolicy {
public:
    AdPolicy(IStorage* storage, IClock* clock, const Tuning& tuning);

    // ---- interstitials
    void runFinished();                       // a run ended (counts toward the interstitial rule)
    bool interstitialDue() const;             // ask at the moment the game-over screen appears
    void interstitialShown();
    void rewardedAtThisGameOver() { rewardedThisOver_ = true; }
    void newGameOver() { rewardedThisOver_ = false; }
    void purchaseMade() { purchasedThisSession_ = true; }
    void setRemoveAds(bool owned);
    bool removeAds() const { return removeAds_; }

    // ---- rewarded
    bool canOffer(Placement p) const;         // under its daily cap / cooldown and the global cap
    int64_t cooldownLeft(Placement p) const;  // seconds until it can be offered again (0 = now)
    int usedToday(Placement p) const;
    void rewardedWatched(Placement p);        // a reward was earned for this placement

    int lifetimeRuns() const { return lifetimeRuns_; }

    // developer menu: clear every cap and cooldown; `adReady` also makes the next game over show an interstitial
    void debugReset(bool adReady);

private:
    void rollDay() const;
    void save() const;
    int dailyCap(Placement p) const;
    int64_t cooldown(Placement p) const;

    IClock* clock_;
    const Tuning& t_;
    mutable Record rec_;
    int runsSinceAd_ = 0, lifetimeRuns_ = 0;
    int64_t lastAdAt_ = 0;
    bool removeAds_ = false, rewardedThisOver_ = false, purchasedThisSession_ = false;
    mutable std::string day_;
    mutable int used_[int(Placement::Count)] = {};
    int64_t lastAt_[int(Placement::Count)] = {};
};

} // namespace cs
