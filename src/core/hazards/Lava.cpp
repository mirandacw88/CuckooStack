// Lava (Level.h's Lava; the game's GameLava.cpp): a stretch of the street or a wall's top that burns the egg under her
// stack, one each cell she crosses on it (an egg every 0.6 m: fast); with no egg under her, it burns her.
#include "../CourseRules.h"
#include "../Hazards.h"
#include "../LevelData.h"
#include "../LevelJudge.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

std::string validate(const Placed& p, const LevelData& d) {
    const double x1 = p.x() + p.at(1) * U;
    double x = 0;
    for (const LevelData::Wall& w : d.course) { // (none in a gauntlet: its own is its grid's)
        const double e = x + w.cells * U;
        if (!w.gauntlet.empty() && p.x() < e && x1 > x) return "lava at " + levelNum(p.x()) + " m runs into the " + w.gauntlet + " (its lava is its grid's)";
        x = e;
    }
    if (x1 > x + 1e-6) return "lava at " + levelNum(p.x()) + " m runs past the course's end";
    return "";
}

void spawn(Level& l, const Placed& p) { l.addLava(p.x(), int(std::lround(p.at(1)))); }

// on one flat surface, with room over it for the eggs it takes, a landing and a reaction before it, a reaction's run
// after it, and the meter refilled since the last
void check(const JudgeView& v, Report& r) {
    const Level& l = v.l;
    double lastEnd = -1e9;
    for (const Lava& f : l.lavas) {
        if (f.x0 >= l.finish() || l.gauntletAt(f.x0) || l.zoneAt(f.x0)) continue; // (a maze's or a bounce zone's: its solver's)
        const Segment* s = nullptr;
        for (const Segment& sg : l.segs) if (f.x0 >= sg.x0 && f.x0 < sg.x1) { s = &sg; break; }
        if (!s) continue;
        const Curve c = l.curveAt(s->x0);
        const double m = rules::marginScale(l.hard()), react = c.speed * 0.6 * m;
        auto note = [&](const std::string& t) { r.notes.push_back({f.x0, double(f.floor), t}); };
        const double feet = f.floor * U;
        if (l.floorAt(f.x0 + 0.05, feet) != f.floor || l.floorAt(f.x1 - 0.05, feet) != f.floor || f.x1 > s->x1 + 1e-6)
            note("Lava off the flat: it must lie on one surface");
        if (s->ceil && !s->top && s->ceil - f.floor - Gauntlet::kBodyRows < f.cells()) note("Lava under a barrier too low for the eggs it burns: no one crosses it");
        const Segment* before = s > l.segs.data() ? s - 1 : nullptr;
        if (before && !before->gauntlet && v.eh[v.ix(before)] > f.floor &&
            f.x0 - s->x0 < rules::sawAfterDrop(c.speed, react, v.eh[v.ix(before)] - f.floor) - 1e-6)
            note("Lava too soon after a drop: she's still landing");
        if (s->x1 - f.x1 < std::max(2.5 * U * m, c.speed * 0.45 * m) - 1e-6) note("Lava too close to the next wall: no time to stack for it");
        if (f.x0 - lastEnd < c.speed * c.regen * (0.5 + 0.6 * m) * 0.5 - 1e-6) note("Lava too soon after the last: the meter can't have refilled");
        lastEnd = f.x1;
    }
}

// an egg a cell
void costs(const Level& l, std::vector<EggCost>& out) {
    for (const Lava& f : l.lavas)
        if (!l.gauntletAt(f.x0) && !l.zoneAt(f.x0) && f.x0 < l.finish()) out.push_back({f.x0, double(f.floor), double(f.cells()), "Lava"});
}

} // namespace

const HazardType kLavaHazard = {
    "lava", "lava", "lava: where along (metres), how long (cells), one a line",
    {{"x", "Along", "m", 0, 5000, 0.1, 0}, {"len", "Length", "cells", 1, 8, 1, 3}},
    1, false,
    "Lava", "lava", "a",
    "A stretch of floor that burns the egg under her stack, one each cell she crosses (an egg every 0.6 m); with no egg "
    "under her, it burns her. Stack as many eggs as it's long.",
    "flame.fill", "lava", 0xff6a1a, Anchor::Surface, 0, 0.5, true,
    validate, spawn, check, costs,
    1, true,
};

} // namespace cs
