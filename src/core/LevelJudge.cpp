#include "LevelJudge.h"

#include "CourseRules.h"
#include "Hazards.h"
#include "Zone.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <tuple>

namespace cs {

namespace {
std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); return b; }

// a bounce zone's solution and its run with a careful player's meter, kept by its layout (a generator's candidates lay
// the same zones again and again; the editors judge on other threads)
std::string zoneKey(const Gauntlet& g) {
    std::string k = std::to_string(g.entryRow()) + "/" + std::to_string(g.exitRow()) + "^" + std::to_string(g.start().n);
    for (const std::string& r : g.rowsTopFirst()) k += "|" + r;
    return k;
}
std::mutex zoneMutex;
Gauntlet::Solution zoneSolution(const Gauntlet& g) {
    static std::map<std::string, Gauntlet::Solution> kept;
    const std::string k = zoneKey(g);
    { std::lock_guard<std::mutex> lock(zoneMutex); if (auto it = kept.find(k); it != kept.end()) return it->second; }
    Gauntlet::Solution s = g.solve();
    s.cellMap.clear();
    std::lock_guard<std::mutex> lock(zoneMutex);
    if (kept.size() > 4000) kept.clear();
    return kept[k] = s;
}
// (the cells it takes her to get out, 0: she doesn't; the eggs she lays on the way)
std::pair<int, int> zoneRun(const Gauntlet& g, double cellsPerEgg, double atDoor) {
    static std::map<std::string, std::pair<int, int>> kept;
    const std::string k = zoneKey(g) + "@" + num(cellsPerEgg) + "," + num(atDoor);
    { std::lock_guard<std::mutex> lock(zoneMutex); if (auto it = kept.find(k); it != kept.end()) return it->second; }
    const auto way = g.botTraceWithMeter(cellsPerEgg, atDoor);
    int laid = 0;
    for (const auto& t : way) laid += t.lay;
    std::lock_guard<std::mutex> lock(zoneMutex);
    if (kept.size() > 4000) kept.clear();
    return kept[k] = {int(way.size()), laid};
}
} // namespace

const char* zoneName(int level) { const Zone z = zoneOf(level); return z == Zone::Egypt ? "Neon Egypt" : z == Zone::Japan ? "Neon Japan" : "the neon city"; }

Report judge(const Level& l) {
    Report r;
    const double m = rules::marginScale(l.hard()); // (super hard: the margins less)
    const double epuSlack = 1.0 + 0.1 * (m - 0.5) * 2, spare = Level::kBudgetMargin * (m - 0.5) * 2;
    auto epu = [&](double x) { const Curve c = l.curveAt(x); return U / (c.speed * c.regen * epuSlack); };
    auto ix = [&](const Segment* s) { return size_t(s - l.segs.data()); };
    // the springs' flights at the curve's speed (their own checks try the other speeds): what she flies over is free,
    // a wall she lands on climbed with no eggs
    struct Flown { double x0, x1; };
    std::vector<Flown> flights;
    for (const Spring& s : l.springs) {
        if (s.x >= l.finish() || l.gauntletAt(s.x) || l.zoneAt(s.x)) continue; // (a zone's spring is flown by its grid's rules: the zone's solver)
        const rules::Flight f = rules::springFlight(l, s, l.curveAt(s.x).speed);
        if (f.end == rules::Flight::End::Landed) flights.push_back({s.x, f.x});
    }
    auto flown = [&](double x) { for (const Flown& f : flights) if (x > f.x0 && x <= f.x1) return true; return false; };
    // a floating block, as the game's bot takes it (GameBot.cpp): over it where there's no room under it (from the
    // street or from the height she comes at, a lower block in a stair of them), where she comes to it as high as its
    // top, or where it bridges onto a wall as high as its top; else under it. `eh`: the height she runs at over each
    // segment that way
    JudgeView v{l, std::vector<int>(l.segs.size()), std::vector<bool>(l.segs.size())};
    std::vector<int>& eh = v.eh;
    std::vector<bool>& over = v.over;
    for (size_t i = 0; i < l.segs.size(); ++i) {
        const Segment& s = l.segs[i];
        const int before = i ? eh[i - 1] : 0;
        // (or a spring flew her up onto it: a floating floor she lands on from below)
        over[i] = rules::takesBlockOver(s, i + 1 < l.segs.size() ? &l.segs[i + 1] : nullptr, before * U) || (s.top && flown(s.x0));
        eh[i] = over[i] ? s.top : s.h;
    }
    // a bounce zone's walls are its grid's: judged by its solver (below), not as the run's walls
    auto zoned = [&](double x) { const PlacedGauntlet* z = l.zoneAt(x + 0.01); return z && x > z->x0 + 1e-6 ? z : nullptr; };
    // walls, barriers, drops, the walls under barriers
    const Segment* prev = nullptr;
    for (const Segment& s : l.segs) {
        if (s.x0 >= l.finish()) break;
        if (s.gauntlet) { prev = &s; continue; }
        r.walls += s.h > 0;
        r.barriers += s.ceil > 0 && !s.top;
        r.blocks += s.top > 0;
        r.tallest = std::max({r.tallest, s.h, s.top});
        const Curve c = l.curveAt(s.x0);
        const int sh = eh[ix(&s)];
        if (zoned(s.x0) || (prev && zoned(prev->x0))) { prev = &s; continue; }
        if (prev && !prev->gauntlet && !flown(s.x0)) {
            const int ph = eh[ix(prev)];
            if (prev->ceil && !over[ix(prev)] && sh > prev->ceil - 2)
                r.notes.push_back({s.x0, double(sh), "A wall " + std::to_string(sh) + " high right after a barrier at " + std::to_string(prev->ceil) +
                                                        ": no one can stack for it under the barrier (" + std::to_string(prev->ceil - 2) + " at most)"});
            if (sh < ph && s.x1 < l.finish()) { // (one that runs on past the finish line: nothing to react to)
                const double need = rules::landingRun(c.speed, ph - sh, m);
                if (s.x1 - s.x0 + 1e-6 < need)
                    r.notes.push_back({s.x0, double(sh), "A drop of " + std::to_string(ph - sh) + " onto " + num((s.x1 - s.x0) / U) +
                                                            " cells: no time to land and react (" + std::to_string(int(std::ceil(need / U))) + " wanted)"});
            }
        }
        prev = &s;
    }
    // the bounce zones, by the maze's solver: out by stacking, and no bouncing forever; none of the hazards a zone's
    // grid has no cells for (street lava and saws: its row 0 is the street's top)
    for (const PlacedGauntlet& z : l.zones) {
        if (z.x0 >= l.finish()) continue;
        // (she may come into it stacked: the fewest eggs stacked at its door that get her out)
        Gauntlet::Solution sol;
        (void)z.g->shouldLay(z.g->start()); // (its cost to the exit worked out once: every arrival's copy shares it)
        for (int eggs = 0; eggs <= int(Level::kBudgetCap) && !sol.reachable; ++eggs) sol = zoneSolution(z.g->arriving(eggs));
        const double y = z.g->entryRow();
        if (!sol.reachable) r.notes.push_back({z.x0, y, "A bounce zone she can't stack out of: no way over its last bumper or gate"});
        else if (sol.softLocks > 0) r.notes.push_back({z.x0, y, "A bumper with no way out: off the way through, she'd bounce there forever"});
        auto in = [&](double x) { return x >= z.x0 && x < z.x1(); };
        for (const Saw& w : l.saws) if (in(w.x) && w.y == 0) r.notes.push_back({w.x, 0, "A saw on the street in a bounce zone: set it in a wall's top"});
        for (const Lava& f : l.lavas) if ((in(f.x0) || in(f.x1 - 0.01)) && f.floor == 0) r.notes.push_back({f.x0, 0, "Lava on the street in a bounce zone: set it on a wall's top"});
        for (const Spring& s : l.springs) if (in(s.x) && s.floor == 0) r.notes.push_back({s.x, 0, "A spring on the street in a bounce zone: set it on a wall's top"});
    }
    // each hazard's own (Hazards.h: a saw on the flat, clear of barriers, a reaction after a drop...)
    for (const HazardType* t : hazardTypes()) if (t->check) t->check(v, r);
    // the egg budget: a careful player's meter (full at the start, refilling per metre at the curve's speed and
    // refill, a little slack for the adaptive difficulty), paying for each climb and each hazard's cost (a blade's egg),
    // with eggs to spare
    double budget = Level::kBudgetCap, at = 0;
    int lastH = 0;
    size_t costI = 0;
    std::vector<EggCost> costs;
    for (const HazardType* t : hazardTypes()) if (t->costs) t->costs(l, costs);
    std::stable_sort(costs.begin(), costs.end(), [](const EggCost& a, const EggCost& b) { return a.x < b.x; });
    auto credit = [&](double to) { if (to > at) { budget = std::min(Level::kBudgetCap, budget + (to - at) / U * epu(at)); at = to; } r.walk.push_back({at, budget}); };
    for (const Segment& s : l.segs) {
        if (s.x0 >= l.finish()) break;
        if (s.gauntlet) {
            const PlacedGauntlet* p = l.gauntletAt(s.x0 + 0.01);
            credit(s.x0);
            if (budget < 2 - 1e-6) r.notes.push_back({s.x0, double(lastH), "At the " + (p ? p->g->name() : std::string("gauntlet")) + "'s door a careful player holds " + num(budget) +
                                                                        " eggs: its checks count on 2 (a longer forecourt)"});
            budget = std::min(Level::kBudgetCap, 1.0 + 8 * epu(s.x1)); // (after the maze: a careful guess, low)
            at = s.x1;
            r.walk.push_back({at, budget});
            lastH = p ? p->g->exitRow() : 0;
            continue;
        }
        credit(s.x0);
        const int sh = eh[ix(&s)];
        if (const PlacedGauntlet* z = zoned(s.x0)) { // (a bounce zone: played with a careful player's meter, refilling as she bounces)
            if (s.x0 < z->x0 + U + 1e-6) {
                // (stacked at its door from the meter, the rest of the meter with her: the fewest stacked that get her out)
                int cells = 0, laid = 0;
                for (int eggs = 0; eggs <= int(std::floor(budget + 1e-6)) && cells == 0; ++eggs) {
                    std::tie(cells, laid) = zoneRun(z->g->arriving(eggs), 1.0 / epu(s.x0), std::floor(budget + 1e-6) - eggs);
                    laid += eggs;
                }
                if (cells == 0) r.short_.push_back({s.x0, double(lastH), "A bounce zone a careful player runs out of eggs in (holding " + num(budget) + " as she comes to it)"});
                budget = std::max(0.0, std::min(Level::kBudgetCap, budget - laid + cells * epu(s.x0)));
                r.lowest = std::min(r.lowest, budget);
                r.walk.push_back({at, budget});
            }
            lastH = z->g->exitRow();
            continue;
        }
        if (sh > lastH && flown(s.x0)) lastH = sh; // (a spring flew her up onto it)
        if (sh > lastH) {
            const double need = (sh - lastH) + spare;
            r.lowest = std::min(r.lowest, budget - (sh - lastH));
            if (budget + 1e-6 < need) r.short_.push_back({s.x0, double(sh), "A climb of " + std::to_string(sh - lastH) + " needs " + num(need) +
                                                                                 " eggs; a careful player holds " + num(budget)});
            budget = std::max(0.0, budget - (sh - lastH));
            r.walk.push_back({at, budget});
        }
        for (; costI < costs.size() && costs[costI].x < s.x1; ++costI) {
            const EggCost& k = costs[costI];
            if (flown(k.x)) continue; // (she flies over it)
            credit(k.x);
            r.lowest = std::min(r.lowest, budget - k.eggs);
            if (budget + 1e-6 < k.eggs + spare)
                r.short_.push_back({k.x, double(sh), std::string(k.what) + (k.eggs == 1 ? std::string(" needs an egg") : " needs " + num(k.eggs) + " eggs") +
                                                         "; a careful player holds " + num(budget)});
            budget = std::max(0.0, budget - k.eggs);
            r.walk.push_back({at, budget});
        }
        lastH = sh;
    }
    credit(l.finish());
    // a gauntlet's door: its forecourt at its entry's height (under a lintel as low as the door), its porch at its exit's
    for (size_t i = 0; i < l.segs.size(); ++i) {
        if (!l.segs[i].gauntlet) continue;
        const PlacedGauntlet* p = l.gauntletAt(l.segs[i].x0 + 0.01);
        if (!p) continue;
        const Segment* before = i ? &l.segs[i - 1] : nullptr;
        const Segment* after = i + 1 < l.segs.size() ? &l.segs[i + 1] : nullptr;
        if (!before || before->h != p->g->entryRow())
            r.doors.push_back({p->x0, double(p->g->entryRow()), "The " + p->g->name() + "'s door is at row " + std::to_string(p->g->entryRow()) +
                                                                     ": the course has to come to it at that height"});
        else if (before->ceil && before->ceil != p->g->entryTop())
            r.doors.push_back({p->x0, double(p->g->entryRow()), "The barrier before the " + p->g->name() + "'s door should be its lintel (at " +
                                                                     std::to_string(p->g->entryTop()) + ")"});
        else if (!before->ceil && rules::doorNeedsLintel(*p->g)) // (Level::placeGauntlet's low door)
            r.notes.push_back({p->x0, double(p->g->entryRow()), "No lintel before the " + p->g->name() + "'s door: a stack of more than " +
                                                                     std::to_string(std::max(0, p->g->entryTop() - p->g->entryRow() - Gauntlet::kBodyRows)) +
                                                                     " eggs crashes into its wall over the door"});
        if (after && after->x0 < l.finish() && after->h != p->g->exitRow())
            r.doors.push_back({p->x1(), double(p->g->exitRow()), "The " + p->g->name() + " comes out at row " + std::to_string(p->g->exitRow()) +
                                                                      ": its porch has to be at that height"});
    }
    return r;
}

} // namespace cs
