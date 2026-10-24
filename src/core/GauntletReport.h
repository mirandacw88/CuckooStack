// A gauntlet's report as text or as one JSON object (the level editors' protocol, gauntlet_check --json): its way
// through, its near misses and the egg meter, judged as Gauntlet_test judges them, where it lands on its level's
// course, its design's signature (GauntletDesigns.h) and, if asked, the bot playing it in the real game.
// The meter runs: the bot with a real egg meter, 2 eggs in it at the door (what the forecourt refills), refilling at
// the level's rate (an egg every regen seconds at its speed), then 1.25 and 1.4 times slower; it gets out, or crashes
// where it ran short. A design is ready when the way is one (routes 1, no traps), the bot gets out at 1.25x, and it has
// its near misses (docs/LEVEL_DESIGN.md).
#pragma once

#include "Gauntlet.h"

#include <algorithm>
#include <string>

namespace cs {

class LevelCatalog;

// (the levels as cat has them: null, LevelCatalog::dev())
// the level a registered gauntlet plays in (0: none)
int gauntletLevelOf(const std::string& name, const LevelCatalog* cat = nullptr);

// the bot through the level `cat` holds that has the gauntlet in it, in the real game (headless, from just before its door): the near
// misses the game shows inside it (perfects + close calls: the editors' "The game shows its near misses"), and whether it got out
struct GauntletGame { bool entered = false, out = false, cleared = false; int perfects = 0, closeCalls = 0; double inside = 0; };
GauntletGame gauntletInGame(int level, const LevelCatalog* cat);
// `game`: play it too (cat must have this layout for `name`: a LevelCatalog::setGauntlet, or a debugOverrideGauntlet)
std::string gauntletReportJson(const Gauntlet& g, const std::string& name, int level, bool game, const LevelCatalog* cat = nullptr);
// what the tests want of a gauntlet (the editors' checklist, short of where it lands on its course and the game's run):
// the exit reached by stacking alone, one way through, no traps, out with the meter refilling 1.25x slower (less from "hard 6"), its near
// misses (wantMisses), its design's signature; why not, in a line
struct GauntletReady {
    bool reachable = false, meterOut = false, designs = true;
    int routes = 0, softLocks = 0, nearMisses = 0;
    int hard = 0, wanted = 15;  // its "hard N" (the generator's density: Gauntlet::hard) and the near misses that asks
    double slowdown = 1.25;     // the meter refilling this many times slower
    std::string why;
    static constexpr int wantMisses = 15;
    // super hard (density 6 to 10, linear from 5): the spare in the meter shrinks, 1.25x to 1.0x at 10; the near misses
    // it must have go up, 15 to 20 at 10
    static double slowdownAt(int hard) { return hard <= 5 ? 1.25 : 1.25 - 0.05 * (std::min(hard, 10) - 5); }
    static int wantMissesAt(int hard) { return wantMisses + (hard <= 5 ? 0 : std::min(hard, 10) - 5); }
    bool ok() const { return reachable && routes == 1 && softLocks == 0 && meterOut && nearMisses >= wanted && designs; }
};
GauntletReady gauntletReady(const Gauntlet& g, int level, const LevelCatalog* cat = nullptr);
// the level's egg meter at its gauntlet: cells crossed per egg back
double gauntletCellsPerEgg(int level, const LevelCatalog* cat = nullptr);

// --map draws the bot's way; --reach marks everywhere she can get to ('+', her feet); --events lists its near misses
std::string gauntletReportText(const Gauntlet& g, int level, bool map, bool events, bool reach, const LevelCatalog* cat = nullptr);

} // namespace cs
