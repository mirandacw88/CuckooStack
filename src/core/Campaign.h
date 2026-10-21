// The level campaign: numbered levels played in order, no menu. Each level is a seeded course (the same for every
// player), a fixed length ending at a finish line, and a difficulty band along the course curve (Difficulty.h).
// Clearing a level moves on to the next; a crash retries the same one. Persisted under cluckstack-campaign.
#pragma once

#include "Level.h"
#include "Meta.h"
#include "Zone.h"

#include <algorithm>
#include <cstdint>

namespace cs {

class Campaign {
public:
    explicit Campaign(IStorage* storage);

    int level() const { return level_; }              // current level, from 1
    Zone zone() const { return zoneOf(level_); }      // its scenery (Zone.h)
    int cleared() const { return cleared_; }          // levels cleared in total
    double best() const { return best_; }             // best distance on the current level, metres
    int attempts() const { return attempts_; }        // attempts on the current level

    // the level's course: as authored (LevelData.h, assets/levels), or past the last authored level, the generator's
    // (its seed, a length growing 10 m a level from 300, its saws)
    static double lengthOf(int level);
    static uint32_t seedOf(int level);
    // saw blades on its course (Level::setSaws): none before level 11, then 2 rising to 8 by level 20, 8 after that;
    // alone on levels 11 and 12, in rows of up to 2 from 13, up to 3 from 16
    static int sawsOf(int level) { return level < 11 ? 0 : std::min(8, 2 + (level - 11) * 2 / 3); }
    static int sawRowOf(int level) { return level < 13 ? 1 : level < 16 ? 2 : 3; }
    // its maze gauntlet (its data's first, Gauntlets.cpp), if it has one, and where its grid starts (metres along)
    static const Gauntlet* gauntletOf(int level);
    static double gauntletAt(int level);
    static int gauntletLevel();                       // the first level with one (0: none)
    // the droid's raids on it: -1 the game's own rule (Game::planRaids), else how many
    static int raidsOf(int level);
    double length() const { return lengthOf(level_); }
    void applyTo(Level& course) const;                // its course: authored, or the generator's (seed, band, finish, saws)
    void applyGenerated(Level& course) const;         // ...the generator's, authored or not (the developer menu's saws)

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
