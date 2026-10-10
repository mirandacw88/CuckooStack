// The level campaign: numbered levels played in order, no menu. Each level is a seeded course (the same for every
// player), a fixed length ending at a finish line, and a difficulty band along the course curve (Difficulty.h).
// Clearing a level moves on to the next; a crash retries the same one. Persisted under cluckstack-campaign.
#pragma once

#include "Level.h"
#include "Meta.h"

#include <cstdint>

namespace cs {

class Campaign {
public:
    explicit Campaign(IStorage* storage);

    int level() const { return level_; }              // current level, from 1
    int cleared() const { return cleared_; }          // levels cleared in total
    double best() const { return best_; }             // best distance on the current level, metres
    int attempts() const { return attempts_; }        // attempts on the current level

    // the level's course
    static double lengthOf(int level) { return 300.0 + 10.0 * (level - 1); }
    static uint32_t seedOf(int level);
    double length() const { return lengthOf(level_); }
    void applyTo(Level& course) const;                // seed, difficulty band, finish line

    void attempt();                                   // a run on the current level started
    bool recordRun(double meters);                    // a run ended there; true if it beat the level best
    void clear();                                     // finish line crossed: next level

    void debugSetLevel(int level);                    // developer menu
    void debugReset();

private:
    void save();
    Record rec_;
    int level_ = 1, cleared_ = 0, attempts_ = 0;
    double best_ = 0;
};

} // namespace cs
