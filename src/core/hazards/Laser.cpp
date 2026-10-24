// The wall laser (Level.h's Laser; the game's GameLaser.cpp): set into a wall's or a block's side (a face, at a
// column's edge) at a row, eggs up from the street, it fires along the course, left or right, as far as the first
// solid cell in its row. As she comes near it charges (a pointer's line and dot, a split second) and fires a thick
// beam; it hits her once a pass, the first time its row meets her: an egg there is cut out (the eggs over it drop), her
// body there cuts her in half; over her head it's harmless. Falling down through it, her bottom egg meets it first.
#include "../CourseRules.h"
#include "../Hazards.h"
#include "../LevelData.h"
#include "../LevelJudge.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

// the level's cells as its walls lay them: solid at (column, row)? (a wall, a block, a barrier, a gauntlet's span; -1
// past either end)
struct Cells {
    struct C { int h = 0, ceil = 0, top = 0; bool g = false; };
    std::vector<C> cols;
    explicit Cells(const LevelData& d) {
        for (const LevelData::Wall& w : d.course)
            for (int i = 0; i < w.cells; ++i) cols.push_back({w.h, w.ceil, w.top, !w.gauntlet.empty()});
    }
    int solid(int col, int row) const {
        if (col < 0 || col >= int(cols.size())) return -1;
        const C& c = cols[size_t(col)];
        if (c.g || row < c.h) return 1;
        if (c.top) return row >= c.ceil && row < c.top;
        return c.ceil && row >= c.ceil;
    }
};

std::string validate(const Placed& p, const LevelData& d) {
    const std::string at = "a laser at " + levelNum(p.x()) + " m";
    const double edge = p.x() / U;
    const int col = static_cast<int>(std::lround(edge)), row = static_cast<int>(std::lround(p.at(1)));
    if (std::abs(edge - col) > 1e-6) return at + " isn't on a wall's side (a column's edge)";
    const Cells cells(d);
    if (col > int(cells.cols.size())) return at + " is past the course's end";
    const bool right = p.at(2) > 0.5;
    const int in = right ? col - 1 : col, out = right ? col : col - 1; // (the cell it's set into; the one it fires into)
    if (cells.solid(in, row) != 1) return at + " has no " + (right ? "wall on its left" : "wall on its right") + " at row " + std::to_string(row) + " to be set into";
    if ((in >= 0 && in < int(cells.cols.size()) && cells.cols[size_t(in)].g) || cells.solid(out, row) == 1)
        return at + " fires into a wall (or a maze: its lasers are its grid's)";
    return "";
}

void spawn(Level& l, const Placed& p) { l.addLaser(p.x(), int(std::lround(p.at(1))), p.at(2) > 0.5 ? 1 : -1); }

void check(const JudgeView& v, Report& r) {
    const Level& l = v.l;
    for (Laser z : l.lasers) {
        if (z.x >= l.finish() || l.gauntletAt(z.x - U * 0.5) || l.gauntletAt(z.x + U * 0.5) || !l.traceLaser(z)) continue;
        if (z.x1 - z.x0 < U * 0.5) r.notes.push_back({z.x, double(z.row), "A laser firing straight into a wall: its beam goes nowhere"});
        else if (z.x0 < 2.0 && z.row < Gauntlet::kBodyRows + l.heightAt(0.3)) // (she's in its line, her body at its row, from the off)
            r.notes.push_back({z.x, double(z.row), "A laser whose beam reaches the level's start at her body: it cuts her in half before she can stack"});
    }
}

// one she can't pass short takes an egg (she needs it as she comes into its reach)
void costs(const Level& l, std::vector<EggCost>& out) {
    for (Laser z : l.lasers)
        if (z.x < l.finish() && !l.gauntletAt(z.x - U * 0.5) && !l.gauntletAt(z.x + U * 0.5) && !l.zoneAt(z.x - U * 0.5) && !l.zoneAt(z.x + U * 0.5) && l.laserTakesAnEgg(z) && l.traceLaser(z))
            out.push_back({z.x0, double(z.row), 1, "A laser"}); // (she comes into it running right: at its reach's start)
}

} // namespace

const HazardType kLaserHazard = {
    "laser", "lasers", "a wall laser: its wall's side (metres along), its row (eggs up), the way it fires (0 left, 1 right), one a line",
    {{"x", "Along", "m", 0, 5000, 0.6, 0}, {"row", "Height", "eggs", 0, 14, 1, 1}, {"dir", "Fires right", "", 0, 1, 1, 0}},
    1, false,
    "Wall laser", "wall lasers", "z",
    "Set into a wall's or a block's side, it fires along the course. As she comes near it charges (a pointer's dot), then "
    "shoots a thick beam: an egg in its row is cut out, her body in it is cut in half, over her head it's harmless.",
    "light.beacon.max.fill", "laser", 0xff2b4a, Anchor::Face, 0, 0, true,
    validate, spawn, check, costs,
    -1, true,
};

} // namespace cs
