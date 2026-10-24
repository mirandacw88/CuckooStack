// The saw blade (Level.h's Saw; the game's GameSaw.cpp): set into a wall's top or the street, it takes the bottom egg
// as her stack crosses it, and her, with no egg under her.
#include "../CourseRules.h"
#include "../Hazards.h"
#include "../LevelData.h"
#include "../LevelJudge.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

std::string validate(const Placed& p, const LevelData& d) {
    double x = 0;
    for (const LevelData::Wall& w : d.course) { // (no saw in a gauntlet: its own are its grid's)
        const double x1 = x + w.cells * U;
        if (!w.gauntlet.empty() && p.x() >= x && p.x() < x1) return "a saw at " + levelNum(p.x()) + " m is inside the " + w.gauntlet + " (its blades are its grid's)";
        x = x1;
    }
    if (p.x() > x) return "a saw at " + levelNum(p.x()) + " m is past the course's end";
    return "";
}

void spawn(Level& l, const Placed& p) { l.addSaw(p.x()); }

// on the flat, clear of barriers, a reaction after a drop or a barrier, a run after
void check(const JudgeView& v, Report& r) {
    const Level& l = v.l;
    double lastSaw = -1e9;
    for (const Saw& w : l.saws) {
        if (w.x >= l.finish() || l.gauntletAt(w.x) || l.zoneAt(w.x)) continue; // (a maze's or a bounce zone's: its solver's)
        const Segment* s = nullptr;
        for (const Segment& sg : l.segs) if (w.x >= sg.x0 && w.x < sg.x1) { s = &sg; break; }
        if (!s) continue;
        const Curve c = l.curveAt(s->x0);
        const double m = rules::marginScale(l.hard()), react = c.speed * 0.6 * m;
        auto note = [&](const std::string& t) { r.notes.push_back({w.x, double(w.y), t}); };
        const double feet = w.y * U; // (the surface it's set into: a floating block's top, or the wall under it)
        if (l.floorAt(w.x - Level::kSawReach - 0.3, feet) != w.y || l.floorAt(w.x + Level::kSawReach + 0.3, feet) != w.y)
            note("A saw on the edge of a wall: the stack has to meet it on the flat");
        // a roof stepping down over one floor (a flood lowering over her) is one roof: it begins at its first step, and
        // nothing under it needs restacking (each step leaves her no more room than the last)
        auto roofOver = [&](const Segment* g) { return g->ceil && !g->top && !g->gauntlet; };
        const Segment* roof0 = s;
        while (roofOver(roof0) && roof0 > l.segs.data() && roofOver(roof0 - 1) && (roof0 - 1)->h == roof0->h && (roof0 - 1)->ceil >= roof0->ceil) --roof0;
        if (s->ceil && !s->top) { // a knife: a blade under a roof that leaves room for an egg, the roof begun a reaction before it
            if (s->ceil - w.y - 2 < 1) note("A saw under a barrier with no room for an egg over it: no one crosses it");
            else if (w.x - Level::kSawReach - roof0->x0 < react - 1e-6) note("A knife (a saw under a barrier) too soon after its roof begins: no time to read it");
        }
        if (s->top && !v.over[v.ix(s)]) note("A saw on a floating block she goes under: no one meets it");
        const Segment* before = s > l.segs.data() ? s - 1 : nullptr;
        const bool stepDown = roof0 != s; // (the barrier before is this roof's own higher step)
        if (before && !stepDown && !before->gauntlet && before->ceil && !v.over[v.ix(before)] && w.x - Level::kSawReach - s->x0 < react - 1e-6) note("A saw too soon after a barrier: no time to stack again");
        if (before && !before->gauntlet && v.eh[v.ix(before)] > w.y) {
            if (w.x - Level::kSawReach - s->x0 < rules::sawAfterDrop(c.speed, react, v.eh[v.ix(before)] - int(w.y)) - 1e-6) note("A saw too soon after a drop: she's still landing");
        }
        const Segment* next = s + 1 < l.segs.data() + l.segs.size() ? s + 1 : nullptr;
        const bool lowering = roofOver(s) && next && roofOver(next) && next->h == s->h && next->ceil <= s->ceil; // (the flood's next, lower step: nothing to stack for)
        if (!lowering && s->x1 - w.x < std::max(2.5 * U * m, c.speed * 0.45 * m + Level::kSawR) - 1e-6) note("A saw too close to the next wall: no time to stack for it");
        const bool inRow = std::abs(w.x - lastSaw - Level::kSawRowGap) < 0.05;
        if (!inRow && w.x - lastSaw < c.speed * c.regen * (0.5 + 0.6 * m) - 1e-6) note("A saw too soon after the last one: the meter can't have refilled");
        lastSaw = w.x;
    }
}

// each blade on the run (a gauntlet's are its own checks') takes an egg
void costs(const Level& l, std::vector<EggCost>& out) {
    for (const Saw& w : l.saws)
        if (!l.gauntletAt(w.x) && !l.zoneAt(w.x) && w.x < l.finish()) out.push_back({w.x, double(w.y), 1, "A blade"});
}

} // namespace

const HazardType kSawHazard = {
    "saw", "saws", "a saw: where along, metres",
    {{"x", "Along", "m", 0, 5000, 0.1, 0}},
    0, true,
    "Saw", "saws", "s",
    "A blade set into a wall's top or the street: as her stack crosses it, it saws off the bottom egg; with no egg under "
    "her, it gets her. On a hay or hay-topped wall it's a cube saw. Blades a gap (1.8 m) apart cut one after another.",
    "gearshape.fill", "saw", 0xff3d6a, Anchor::Surface, Level::kSawRowGap, 0.5, true,
    validate, spawn, check, costs,
    -1, true,
};

} // namespace cs
