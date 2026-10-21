// A gauntlet's signature: what its design is built round (docs/LEVEL_DESIGN.md: the Grand Gallery's climb, the wells,
// the labyrinth's forks, the ascent's passage), found on the bot's way through it. Gauntlet_test holds each gauntlet to
// its own, and the level editor shows them as its grid is tuned (gauntlet_check --json), so a grid it calls ready
// passes the tests. A gauntlet with no signature of its own (the tomb, a new one) has none.
#pragma once

#include "Gauntlet.h"

#include <string>
#include <vector>

namespace cs {

struct DesignCheck {
    std::string what; // the signature, in a line
    bool ok = false;
    std::string why;  // what the bot's way has of it
};
std::vector<DesignCheck> designChecks(const Gauntlet& g);

// The forks on the bot's way: bumper walls 2 high or more that it climbs or bounces off where the other (bouncing off
// with no egg, climbing it stacked to its top) is a legal move into a dead end. `bounced`: those where it bounces
int designForks(const Gauntlet& g, int* bounced);

} // namespace cs
