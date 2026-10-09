#include "Monetization.h"

#include <algorithm>

namespace cs {

const char* placementName(Placement p) {
    static const char* n[] = {"continue", "double_coins", "boost", "free_coins", "drop_double", "mission_reroll",
                              "mission_bonus", "streak_save", "featured_outfit", "try_on", "chest_double"};
    return n[int(p)];
}

AdPolicy::AdPolicy(IStorage* storage, IClock* clock, const Tuning& tuning) : clock_(clock), t_(tuning), rec_(storage, "cluckstack-ads") {
    runsSinceAd_ = rec_.i("since");
    lifetimeRuns_ = rec_.i("runs");
    lastAdAt_ = rec_.l("last");
    removeAds_ = rec_.i("noads") != 0;
    day_ = rec_.s("day");
    for (int i = 0; i < int(Placement::Count); ++i) {
        used_[i] = rec_.i("u" + std::to_string(i));
        lastAt_[i] = rec_.l("t" + std::to_string(i));
    }
    rollDay();
}

void AdPolicy::rollDay() const {
    const std::string today = clock_->today();
    if (day_ == today) return;
    day_ = today;
    for (int& u : used_) u = 0;
}

void AdPolicy::debugReset(bool adReady) {
    for (int& u : used_) u = 0;
    for (int64_t& t : lastAt_) t = 0;
    lastAdAt_ = 0;
    if (adReady) { runsSinceAd_ = t_.adEveryRuns; lifetimeRuns_ = std::max(lifetimeRuns_, t_.adGraceRuns); }
    save();
}

void AdPolicy::save() const {
    rec_.set("since", runsSinceAd_);
    rec_.set("runs", lifetimeRuns_);
    rec_.set("last", lastAdAt_);
    rec_.set("noads", removeAds_ ? 1 : 0);
    rec_.set("day", day_);
    for (int i = 0; i < int(Placement::Count); ++i) {
        rec_.set("u" + std::to_string(i), used_[i]);
        rec_.set("t" + std::to_string(i), lastAt_[i]);
    }
    rec_.save();
}

void AdPolicy::runFinished() {
    ++runsSinceAd_;
    ++lifetimeRuns_;
    save();
}

bool AdPolicy::interstitialDue() const {
    if (removeAds_ || rewardedThisOver_ || purchasedThisSession_) return false;
    if (lifetimeRuns_ < t_.adGraceRuns) return false;
    if (runsSinceAd_ < t_.adEveryRuns) return false;
    return clock_->now() - lastAdAt_ >= t_.adMinSeconds;
}

void AdPolicy::interstitialShown() {
    runsSinceAd_ = 0;
    lastAdAt_ = clock_->now();
    save();
}

void AdPolicy::setRemoveAds(bool owned) {
    if (removeAds_ == owned) return;
    removeAds_ = owned;
    save();
}

int AdPolicy::dailyCap(Placement p) const {
    switch (p) {
    case Placement::Boost: return t_.boostAdsPerDay;
    case Placement::FreeCoins: return t_.freeCoinsPerDay;
    case Placement::MissionReroll: return t_.rerollAdsPerDay;
    case Placement::DropDouble: case Placement::MissionBonus: case Placement::StreakSave: return 1;
    default: return t_.rewardedDailyCap;
    }
}

int64_t AdPolicy::cooldown(Placement p) const { return p == Placement::FreeCoins ? t_.freeCoinsCooldown : 0; }

int AdPolicy::usedToday(Placement p) const { rollDay(); return used_[int(p)]; }

int64_t AdPolicy::cooldownLeft(Placement p) const {
    const int64_t cd = cooldown(p);
    if (cd <= 0 || lastAt_[int(p)] == 0) return 0;
    return std::max<int64_t>(0, lastAt_[int(p)] + cd - clock_->now());
}

bool AdPolicy::canOffer(Placement p) const {
    rollDay();
    int total = 0;
    for (int u : used_) total += u;
    if (total >= t_.rewardedDailyCap) return false;
    if (used_[int(p)] >= dailyCap(p)) return false;
    return cooldownLeft(p) == 0;
}

void AdPolicy::rewardedWatched(Placement p) {
    rollDay();
    ++used_[int(p)];
    lastAt_[int(p)] = clock_->now();
    // any full-screen ad resets the interstitial spacing, so players aren't hit with two ads back to back
    runsSinceAd_ = 0;
    lastAdAt_ = clock_->now();
    save();
}

} // namespace cs
