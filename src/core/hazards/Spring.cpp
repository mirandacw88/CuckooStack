// The spring (Level.h's Spring; the game's GameSpring.cpp): a pad on a surface that launches her, stack and all, as
// she runs over it, `power` rows up with her speed kept. She flies over what's ahead with no eggs and lands where the
// arc comes down (CourseRules.h's springFlight, the game's own physics); her front meeting a wall's face below her
// feet on the way crashes her, as any wall does, and so does her head meeting a barrier. It costs no eggs: the climbs
// it flies her over are free (LevelJudge's budget).
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
    for (const LevelData::Wall& w : d.course) { // (none in a gauntlet: its bouncers are its grid's)
        const double e = x + w.cells * U;
        if (!w.gauntlet.empty() && p.x() > x - 0.3 && p.x() < e + 0.3) return "a spring at " + levelNum(p.x()) + " m is in the " + w.gauntlet;
        x = e;
    }
    if (p.x() > x - 0.3) return "a spring at " + levelNum(p.x()) + " m is past the course's end";
    return "";
}

void spawn(Level& l, const Placed& p) { l.addSpring(p.x(), int(std::lround(p.at(1)))); }

// the speeds she may run at over it (the adaptive difficulty's range: 0.8 to 1.24 of the curve's; OVERDRIVE smashes
// through whatever it meets)
constexpr double kSpeeds[] = {0.8, 0.9, 1.0, 1.12, 1.24};

// on the flat, clear of barriers, a reaction after a drop before it; every speed she may run at flies her onto a
// surface (never into a wall's face or a barrier), with a landing long enough to land and react on
void check(const JudgeView& v, Report& r) {
    const Level& l = v.l;
    for (const Spring& s : l.springs) {
        if (s.x >= l.finish() || l.gauntletAt(s.x) || l.zoneAt(s.x)) continue; // (a zone's: its grid's, the zone's solver)
        const Segment* seg = nullptr;
        for (const Segment& sg : l.segs) if (s.x >= sg.x0 && s.x < sg.x1) { seg = &sg; break; }
        if (!seg) continue;
        const Curve c = l.curveAt(s.x);
        auto note = [&](double x, double y, const std::string& t) { r.notes.push_back({x, y, t}); };
        // (a floating floor's top is a surface too: she's up on it)
        const double feet = s.floor * U;
        if (l.floorAt(s.x - 0.3, feet) != s.floor || l.floorAt(s.x + 0.3, feet) != s.floor || (seg->top && seg->top != s.floor)) note(s.x, s.floor, "A spring off the flat: it must sit on one surface");
        const Segment* before = seg > l.segs.data() ? seg - 1 : nullptr;
        if (before && !before->gauntlet && v.eh[v.ix(before)] > s.floor &&
            s.x - seg->x0 < rules::sawAfterDrop(c.speed, c.speed * 0.6 * rules::marginScale(l.hard()), v.eh[v.ix(before)] - s.floor) - 1e-6)
            note(s.x, s.floor, "A spring too soon after a drop: she's still landing");
        for (double k : kSpeeds) {
            const rules::Flight f = rules::springFlight(l, s, c.speed * k);
            const std::string at = " at " + levelNum(std::round(f.x * 10) / 10) + " m";
            if (f.end == rules::Flight::End::Wall) { note(f.x, f.y / U, "A spring that flings her into a wall's face" + at + (k != 1.0 ? " (running " + levelNum(k) + "× as fast)" : "")); break; }
            if (f.end == rules::Flight::End::Barrier) { note(f.x, f.y / U, "A spring that flings her into a barrier" + at); break; }
            if (f.end == rules::Flight::End::Off) { note(s.x, s.floor, "A spring with nothing to land on before the course runs out"); break; }
            const Segment* land = nullptr;
            for (const Segment& sg : l.segs) if (f.x >= sg.x0 && f.x < sg.x1) { land = &sg; break; }
            if (!land || land->gauntlet) continue;
            const Segment* next = land + 1 < l.segs.data() + l.segs.size() ? land + 1 : nullptr;
            const int drop = std::max(0, static_cast<int>(std::lround((f.apexY - f.y) / U)));
            if (next && !next->gauntlet && next->x0 < l.finish() && v.eh[v.ix(next)] > f.floor &&
                land->x1 - f.x + 1e-6 < rules::landingRun(c.speed * k, drop, rules::marginScale(l.hard()))) {
                note(f.x, f.floor, "A spring that lands her" + at + " too close to the next wall: no time to land and react");
                break;
            }
        }
    }
}

} // namespace

const HazardType kSpringHazard = {
    "spring", "springs", "a spring: where along (metres), its lift (rows), one a line",
    {{"x", "Along", "m", 0, 5000, 0.1, 0}, {"power", "Lift", "rows", 2, 12, 1, 6}},
    1, false,
    "Spring", "springs", "j",
    "A pad that launches her, stack and all, as she runs over it: up as many rows as its lift, her speed kept, onto "
    "what's ahead. It costs no eggs; mind the walls' faces and barriers on the way, and land her somewhere long enough.",
    "arrow.up.to.line.compact", "spring", 0x7dff4a, Anchor::Surface, 0, 1.2, true,
    validate, spawn, check, nullptr,
    -1, true, "bouncers",
};

} // namespace cs
