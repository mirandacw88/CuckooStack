#include "Daily.h"

#include <algorithm>
#include <sstream>

namespace cs {

// ---------------------------------------------------------------- level
Progression::Progression(IStorage* storage, const Tuning& t) : t_(t), rec_(storage, "cluckstack-level") {
    level_ = std::max(1, rec_.i("level", 1));
    xp_ = std::max(0, rec_.i("xp"));
}

void Progression::debugSetLevel(int level) {
    level_ = std::max(1, level);
    xp_ = 0;
    rec_.set("level", level_);
    rec_.set("xp", xp_);
    rec_.save();
}

int Progression::xpToNext() const { return t_.levelBaseXp + t_.levelStepXp * (level_ - 1); }

int Progression::add(int xp) {
    if (xp <= 0) return 0;
    xp_ += xp;
    int gained = 0;
    while (xp_ >= xpToNext()) { xp_ -= xpToNext(); ++level_; ++gained; }
    rec_.set("level", level_);
    rec_.set("xp", xp_);
    rec_.save();
    return gained;
}

// ---------------------------------------------------------------- streak
Streak::Streak(IStorage* storage, IClock* clock) : clock_(clock), rec_(storage, "cluckstack-streak") {
    count_ = rec_.i("count");
    best_ = rec_.i("best");
    last_ = rec_.has("last") ? rec_.l("last") : kNoDay;
}

void Streak::save() {
    rec_.set("count", count_);
    rec_.set("best", best_);
    if (last_ != kNoDay) rec_.set("last", last_);
    rec_.save();
}

Streak::Status Streak::status() const {
    if (last_ == kNoDay) return Status::Fresh;
    const int64_t gap = civilDay(clock_->today()) - last_;
    if (gap <= 1) return Status::Alive;
    if (gap == 2) return Status::Saveable;
    return Status::Broken;
}

int Streak::count() const {
    const Status s = status();
    return s == Status::Alive || s == Status::Saveable ? count_ : 0;
}

bool Streak::playedToday() const { return last_ != kNoDay && last_ == civilDay(clock_->today()); }

void Streak::played() {
    const int64_t today = civilDay(clock_->today());
    if (last_ == today) return;
    count_ = last_ != kNoDay && today - last_ == 1 ? count_ + 1 : 1;
    last_ = today;
    best_ = std::max(best_, count_);
    save();
}

void Streak::debugSet(int count, int daysSinceLastRun) {
    count_ = std::max(0, count);
    best_ = std::max(best_, count_);
    last_ = count_ > 0 ? civilDay(clock_->today()) - daysSinceLastRun : kNoDay;
    if (last_ == kNoDay) rec_.erase("last");
    save();
}

void Streak::keep() {
    if (status() != Status::Saveable) return;
    last_ = civilDay(clock_->today()) - 1; // the missed day counts; playing today continues the streak
    save();
}

// ---------------------------------------------------------------- daily missions
namespace {
struct Spec { MissionKind kind; int targets[3]; }; // easy / medium / hard
const Spec kSpecs[] = {
    {MissionKind::Distance, {120, 250, 420}},
    {MissionKind::Runs, {3, 5, 8}},
    {MissionKind::Perfects, {4, 10, 20}},
    {MissionKind::Surges, {1, 2, 4}},
    {MissionKind::CloseCalls, {2, 4, 8}},
    {MissionKind::Eggs, {60, 140, 260}},
    {MissionKind::BeatBest, {1, 1, 1}},
};
constexpr int kSpecCount = int(sizeof kSpecs / sizeof kSpecs[0]);
} // namespace

Missions::Missions(IStorage* storage, IClock* clock) : clock_(clock), rec_(storage, "cluckstack-missions") {
    day_ = rec_.s("day");
    rerolls_ = rec_.i("rerolls");
    bonusClaimed_ = rec_.i("bonus") != 0;
    for (int i = 0; i < 3; ++i) {
        const std::string k = std::to_string(i);
        m_[size_t(i)].kind = MissionKind(std::clamp(rec_.i("k" + k), 0, int(MissionKind::Count) - 1));
        m_[size_t(i)].target = std::max(1, rec_.i("t" + k, 1));
        m_[size_t(i)].progress = rec_.i("p" + k);
        m_[size_t(i)].done = rec_.i("d" + k) != 0;
    }
    refresh();
}

Mission Missions::pick(int slot, uint64_t salt) const {
    // slot 0 easy, 1 medium, 2 hard; the three never repeat a kind
    const uint64_t h = hashString("missions:" + day_ + ":" + std::to_string(slot) + ":" + std::to_string(salt));
    for (int tries = 0; tries < kSpecCount; ++tries) {
        const Spec& s = kSpecs[(h + uint64_t(tries)) % uint64_t(kSpecCount)];
        bool clash = false;
        for (int j = 0; j < 3; ++j)
            if (j != slot && m_[size_t(j)].kind == s.kind && (j < slot || salt > 0)) clash = true;
        if (clash) continue;
        Mission m;
        m.kind = s.kind;
        m.target = s.targets[slot];
        return m;
    }
    return Mission{};
}

void Missions::refresh() {
    const std::string today = clock_->today();
    if (day_ == today) return;
    day_ = today;
    rerolls_ = 0;
    bonusClaimed_ = false;
    for (auto& m : m_) m = Mission{MissionKind::Count};
    for (int i = 0; i < 3; ++i) m_[size_t(i)] = pick(i, 0);
    save();
}

void Missions::save() {
    rec_.set("day", day_);
    rec_.set("rerolls", rerolls_);
    rec_.set("bonus", bonusClaimed_ ? 1 : 0);
    for (int i = 0; i < 3; ++i) {
        const std::string k = std::to_string(i);
        rec_.set("k" + k, int(m_[size_t(i)].kind));
        rec_.set("t" + k, m_[size_t(i)].target);
        rec_.set("p" + k, m_[size_t(i)].progress);
        rec_.set("d" + k, m_[size_t(i)].done ? 1 : 0);
    }
    rec_.save();
}

int Missions::record(MissionKind kind, int amount) {
    refresh();
    int completed = 0;
    for (int i = 0; i < 3; ++i) {
        Mission& m = m_[size_t(i)];
        if (m.done || m.kind != kind || kind == MissionKind::Distance) continue;
        m.progress = std::min(m.target, m.progress + amount);
        if (m.progress >= m.target) { m.done = true; completed |= 1 << i; }
    }
    if (amount) save();
    return completed;
}

int Missions::recordDistance(int meters) {
    refresh();
    int completed = 0;
    for (int i = 0; i < 3; ++i) {
        Mission& m = m_[size_t(i)];
        if (m.done || m.kind != MissionKind::Distance) continue;
        m.progress = std::min(m.target, std::max(m.progress, meters));
        if (m.progress >= m.target) { m.done = true; completed |= 1 << i; }
    }
    save();
    return completed;
}

bool Missions::allDone() const { return m_[0].done && m_[1].done && m_[2].done; }

void Missions::reroll(int index) {
    if (index < 0 || index > 2 || m_[size_t(index)].done) return;
    ++rerolls_;
    const MissionKind old = m_[size_t(index)].kind;
    for (uint64_t salt = uint64_t(rerolls_); salt < uint64_t(rerolls_) + 16; ++salt) {
        Mission m = pick(index, salt);
        if (m.kind != old) { m_[size_t(index)] = m; break; }
    }
    save();
}

std::string Missions::describe(const Mission& m) {
    const std::string n = std::to_string(m.target);
    switch (m.kind) {
    case MissionKind::Distance: return "Reach " + n + " m in one run";
    case MissionKind::Runs: return "Play " + n + " runs";
    case MissionKind::Perfects: return "Land " + n + " perfects";
    case MissionKind::Surges: return m.target == 1 ? "Trigger a surge" : "Trigger " + n + " surges";
    case MissionKind::CloseCalls: return "Survive " + n + " close calls";
    case MissionKind::Eggs: return "Lay " + n + " eggs";
    case MissionKind::BeatBest: return "Beat today’s best";
    default: return "";
    }
}

// ---------------------------------------------------------------- daily drop
DailyDrop::DailyDrop(IStorage* storage, IClock* clock) : clock_(clock), rec_(storage, "cluckstack-drop") {
    claims_ = rec_.i("claims");
    last_ = rec_.s("last");
}

bool DailyDrop::claimable() const { return last_ != clock_->today(); }

void DailyDrop::debugSet(int claims, bool claimable) {
    claims_ = std::max(0, claims);
    last_ = claimable ? std::string() : clock_->today();
    rec_.set("claims", claims_);
    rec_.set("last", last_);
    rec_.save();
}

int DailyDrop::claim() {
    if (!claimable()) return -1;
    const int step = dayIndex();
    ++claims_;
    last_ = clock_->today();
    rec_.set("claims", claims_);
    rec_.set("last", last_);
    rec_.save();
    return step;
}

// ---------------------------------------------------------------- profile
Profile::Profile(IStorage* storage, IClock* clock) : clock_(clock), rec_(storage, "cluckstack-profile") {
    const int a = rec_.i("aud");
    audience_ = a == 1 ? Audience::Child : a == 2 ? Audience::Teen : Audience::Unknown;
    sessions_ = rec_.i("sessions");
    installDay_ = rec_.has("install") ? rec_.l("install") : kNoDay;
    std::istringstream in(rec_.s("hours"));
    int i = 0;
    for (std::string v; std::getline(in, v, ',') && i < 24; ++i) hours_[size_t(i)] = std::atoi(v.c_str());
}

void Profile::setAudience(Audience a) {
    audience_ = a;
    rec_.set("aud", a == Audience::Child ? 1 : a == Audience::Teen ? 2 : 0);
    rec_.save();
}

void Profile::sessionStarted() {
    ++sessions_;
    const int64_t now = clock_->now();
    if (installDay_ == kNoDay) installDay_ = civilDay(clock_->localDate(now));
    ++hours_[size_t(std::clamp(clock_->localHour(now), 0, 23))];
    std::string h;
    for (size_t i = 0; i < 24; ++i) { if (i) h += ','; h += std::to_string(hours_[i]); }
    rec_.set("sessions", sessions_);
    rec_.set("install", installDay_);
    rec_.set("hours", h);
    rec_.save();
}

int Profile::daysSinceInstall() const {
    return installDay_ == kNoDay ? 0 : int(civilDay(clock_->today()) - installDay_);
}

int Profile::usualHour() const {
    int best = -1, n = 0;
    for (int i = 0; i < 24; ++i)
        if (hours_[size_t(i)] > n) { n = hours_[size_t(i)]; best = i; }
    return best < 0 ? 19 : best;
}

} // namespace cs
