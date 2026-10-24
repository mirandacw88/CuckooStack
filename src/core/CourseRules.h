// The course's shared rules: one copy of each formula the generator (Level.cpp), the game's bot (GameBot.cpp), the
// level checker (LevelJudge) and the level editors all use, so none of them can drift from the others.
#pragma once

#include "Level.h"

#include <cmath>
#include <vector>

namespace cs::rules {

constexpr double kGravity = 36.0; // the game's, on the hen (m/s²)
// a fall of `drop` eggs takes this long (gravity as the game's hen falls)
double fallSeconds(int drop);
// super hard: how much of a human's margins (a reaction, the egg budget's spare, the meter's refill between hazards) a
// level at `hard` (LevelData's) keeps: all of them up to 7, then less, half at 10 (and no spare egg)
double marginScale(int hard);
// the flat run (metres) a landing needs after a fall of `drop`: the fall, then a reaction (`m`: marginScale's)
double landingRun(double speed, int drop, double m = 1);
inline int landingCells(double speed, int drop, double m = 1) { return static_cast<int>(std::ceil(landingRun(speed, drop, m) / U)); }
// how far past a drop's edge a saw may first sit (metres, before its reach): she's landed and had a reaction
double sawAfterDrop(double speed, double react, int drop);

// a floating block: does she take it over (onto its top) rather than under it? Over where there's no room under it
// (from the street or from where her feet are, a lower block in a stair of them), once she's as high as its top, or
// where it bridges onto a wall as high as its top (`next`: the segment after it)
bool takesBlockOver(const Segment& s, const Segment* next, double feetY);

// a spring: her speed up off its pad (`power` rows to the top of the arc), and her flight from it as the game flies
// her (Game::updatePlay: running at `speed`, her stack `eggs` high, its walls, blocks and barriers). Where she lands
// (her feet's floor, in eggs), or where she crashes: her stack's front into a wall's face below its top (with eggs
// enough over it, she steps onto it, as any wall), her head into a barrier; Off: nothing to land on before the course
// runs out. `path`: her feet, (x, y) a step, for the editors to draw
struct Flight {
    enum class End { Landed, Wall, Barrier, Off };
    End end = End::Off;
    double x = 0, y = 0;      // where it ended (her centre, her feet)
    int floor = 0;            // Landed: the floor she's on
    double apexX = 0, apexY = 0;
    std::vector<double> path; // x0, y0, x1, y1... (every 0.05 s)
};
double springLaunch(int power);
Flight springFlight(const Level& l, const Spring& s, double speed, int eggs = 0);

// a gauntlet's forecourt (cells at its door's height): a reaction's run, long enough to refill two and a half eggs at
// the budget's careful rate (`epu`: eggs per metre)
int forecourtCells(double speed, double epu);
// a door too low for a full stack gets a lintel (a barrier as low as the door) at the forecourt's end
bool doorNeedsLintel(const Gauntlet& g);

} // namespace cs::rules
