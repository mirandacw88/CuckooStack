// Reasons to come back: player level (XP), daily streak, three daily missions, the 7-day daily drop, and the player
// profile (audience, settings, play habits). All persisted; all driven by an IClock so tests can move time.
#pragma once

#include "Meta.h"
#include "Tuning.h"

#include <array>
#include <string>

namespace cs {

// ---------------------------------------------------------------- level
class Progression {
public:
    Progression(IStorage* storage, const Tuning& t);
    int level() const { return level_; }
    int xp() const { return xp_; }                 // into the current level
    int xpToNext() const;                          // needed for the next level
    float fraction() const { return float(xp_) / float(std::max(1, xpToNext())); }
    int add(int xp);                               // returns how many levels were gained
    void debugSetLevel(int level);                 // developer menu: jump to a level with 0 xp

private:
    const Tuning& t_;
    Record rec_;
    int level_ = 1, xp_ = 0;
};

// ---------------------------------------------------------------- streak
class Streak {
public:
    enum class Status { Fresh, Alive, Saveable, Broken }; // Fresh: never played
    Streak(IStorage* storage, IClock* clock);
    int count() const;                 // current streak as the player sees it now (0 when broken)
    int best() const { return best_; }
    bool playedToday() const;
    Status status() const;             // Saveable: missed exactly one day; can be kept with an ad or coins
    void played();                     // a run started today
    void keep();                       // save / repair: the missed day counts as played
    void debugSet(int count, int daysSinceLastRun); // developer menu: e.g. (5, 2) = a 5-day streak, missed yesterday

private:
    void save();
    IClock* clock_;
    Record rec_;
    int count_ = 0, best_ = 0;
    int64_t last_ = kNoDay;            // civil day of the last run
};

// ---------------------------------------------------------------- daily missions
enum class MissionKind : uint8_t { Distance, Runs, Perfects, Surges, CloseCalls, Eggs, BeatBest, LevelsCleared, Count };

struct Mission {
    MissionKind kind = MissionKind::Runs;
    int target = 1, progress = 0;
    bool done = false;
};

class Missions {
public:
    Missions(IStorage* storage, IClock* clock);
    void refresh();                                    // new day: today's three missions (same for every player)
    const std::array<Mission, 3>& list() const { return m_; }
    // progress events; return the indices that just completed (bit mask)
    int record(MissionKind kind, int amount = 1);
    int recordDistance(int meters);                    // best single run today
    bool allDone() const;
    bool bonusClaimed() const { return bonusClaimed_; }
    void claimBonus() { bonusClaimed_ = true; save(); }
    void reroll(int index);                            // swap one unfinished mission for another
    int rerolls() const { return rerolls_; }
    static std::string describe(const Mission& m);     // "Reach 300 m in one run"

private:
    Mission pick(int slot, uint64_t salt) const;
    void save();
    IClock* clock_;
    Record rec_;
    std::string day_;
    std::array<Mission, 3> m_{};
    int rerolls_ = 0;
    bool bonusClaimed_ = false;
};

// ---------------------------------------------------------------- daily drop (7-day login ladder)
class DailyDrop {
public:
    DailyDrop(IStorage* storage, IClock* clock);
    bool claimable() const;
    int dayIndex() const { return claims_ % 7; }       // 0..6: which step of the ladder is next
    int claim();                                       // returns the ladder step just claimed
    void debugSet(int claims, bool claimable);         // developer menu: ladder position, claimable again today
    int claims() const { return claims_; }

private:
    IClock* clock_;
    Record rec_;
    int claims_ = 0;
    std::string last_;
};

// ---------------------------------------------------------------- player profile
enum class Audience : uint8_t { Unknown, Child, Teen }; // Teen = 13 and over

class Profile {
public:
    Profile(IStorage* storage, IClock* clock);
    Audience audience() const { return audience_; }
    void setAudience(Audience a);
    bool child() const { return audience_ == Audience::Child; }

    void sessionStarted();
    int sessions() const { return sessions_; }
    int64_t installDay() const { return installDay_; }
    int daysSinceInstall() const;
    int usualHour() const;                             // most common local hour the player starts sessions (19 if unknown)

    // flags (each saved)
    bool flag(const std::string& name) const { return rec_.i("f_" + name) != 0; }
    void setFlag(const std::string& name, bool on) { rec_.set("f_" + name, on ? 1 : 0); rec_.save(); }
    int counter(const std::string& name) const { return rec_.i("c_" + name); }
    void setCounter(const std::string& name, int v) { rec_.set("c_" + name, v); rec_.save(); }

private:
    IClock* clock_;
    Record rec_;
    Audience audience_ = Audience::Unknown;
    int sessions_ = 0;
    int64_t installDay_ = kNoDay;
    std::array<int, 24> hours_{};
};

} // namespace cs
