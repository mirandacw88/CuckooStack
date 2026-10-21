// The course generator (Level.cpp): its fairness rules over many seeds (the egg budget: every climb and saw
// affordable), the saw blades of levels 11 on, sector breathers, the finish runway, determinism, lookups, and the
// blocks, dance-floor tiles and barriers built from the segments.
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/Difficulty.h"
#include "core/Level.h"

#include <cstdio>
#include <set>

using namespace cs;
using namespace test;

namespace {

int cols(const Segment& s) { return jsRound((s.x1 - s.x0) / U); }

void fairness() {
    bool contiguous = true, capped = true, barrierAfterRise = true, barrierLong = true, underBarrier = true, afterDrop = true, start = true;
    int barriers = 0, drops = 0;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        Level l;
        l.reset(seedFor("cluckstack:fairness:" + std::to_string(seed)));
        l.generateUntil(1500);
        start = start && l.segs.front().x0 == 0 && l.segs.front().h == 0 && cols(l.segs.front()) >= 16;
        for (size_t i = 1; i < l.segs.size(); ++i) {
            const Segment& p = l.segs[i - 1];
            const Segment& s = l.segs[i];
            contiguous = contiguous && std::abs(s.x0 - p.x1) < 1e-9;
            capped = capped && s.h >= 0 && s.h <= l.curveAt(s.x0).cap;
            if (s.ceil) {
                barriers++;
                barrierAfterRise = barrierAfterRise && s.h > p.h && s.x0 > 30 - (s.x1 - s.x0);
                barrierLong = barrierLong && cols(s) >= int(std::ceil(l.curveAt(s.x0).speed * 0.5 / U)) - 1;
                if (i + 1 < l.segs.size())
                    underBarrier = underBarrier && l.segs[i + 1].h <= s.ceil - int(std::ceil(HEN_H / U));
            }
            if (s.h < p.h) { // (sector breathers too: after a tall wall they get the full landing run)
                drops++;
                // the flat run after a drop: long enough to land and react (the segment that dropped)
                const double fall = std::sqrt(2.0 * (p.h - s.h) * U / 36.0);
                afterDrop = afterDrop && cols(s) >= int(std::ceil(l.curveAt(s.x0).speed * (fall + 0.45) / U)) - 1;
            }
        }
    }
    check(start, "every course starts with a flat 16-crate run-up from x = 0");
    check(contiguous, "segments are contiguous (no gaps, no overlaps)");
    check(capped, "no wall taller than the curve allows at its distance");
    check(barriers > 100 && drops > 100, "60 seeds x 1.5 km: plenty of barriers and drops to judge");
    check(barrierAfterRise, "fair: barriers only over rising walls");
    check(barrierLong, "fair: a barrier lasts long enough to react and lay under it");
    check(underBarrier, "fair: the wall after a barrier is climbable while still under it");
    check(afterDrop, "fair: after a drop, enough flat run to land and react");
}

void sectors() {
    Level l;
    l.reset(1234);
    l.generateUntil(800);
    bool breathers = true;
    for (int sec = 1; sec * SECTOR < 700; ++sec) {
        bool found = false;
        for (const Segment& s : l.segs)
            if (s.x0 >= sec * SECTOR && s.x0 < sec * SECTOR + 20 && s.h == 0 && cols(s) >= 12) found = true; // (longer: the egg budget)
        int corn = 0;
        for (const Corn& c : l.corns) corn += c.x >= sec * SECTOR && c.x < sec * SECTOR + 12 * U + 20;
        breathers = breathers && found && corn >= 3;
    }
    check(breathers, "every 150 m sector opens with a flat breather and a rising line of 3 corn");
}

void finishLine() {
    Level l;
    l.reset(99);
    l.setFinish(300);
    l.generateUntil(400);
    bool flat = true, clear = true;
    for (const Segment& s : l.segs)
        if (s.x0 >= 300 - 2 * U) flat = flat && s.h == 0 && s.ceil == 0;
    for (const Corn& c : l.corns) clear = clear && c.x < 300;
    check(l.finish() == 300 && flat, "past the finish line: flat runway, no walls or barriers");
    check(clear, "...and no corn to chase");
    Level endless;
    endless.reset(99);
    endless.generateUntil(400);
    bool walls = false;
    for (const Segment& s : endless.segs) walls = walls || (s.x0 > 310 && s.h > 0);
    check(endless.finish() == 0 && walls, "no finish (endless): walls carry on");
}

void determinism() {
    Level a, b, c;
    a.reset(42); b.reset(42); c.reset(43);
    a.generateUntil(600); b.generateUntil(600); c.generateUntil(600);
    bool same = a.segs.size() == b.segs.size();
    for (size_t i = 0; same && i < a.segs.size(); ++i)
        same = a.segs[i].x0 == b.segs[i].x0 && a.segs[i].h == b.segs[i].h && a.segs[i].ceil == b.segs[i].ceil && a.segs[i].kind == b.segs[i].kind;
    bool differ = a.segs.size() != c.segs.size();
    for (size_t i = 0; !differ && i < a.segs.size(); ++i) differ = a.segs[i].h != c.segs[i].h;
    check(same, "same seed, same course");
    check(differ, "another seed, another course");
    // generating in small steps gives the same course as one big step
    Level s;
    s.reset(42);
    for (double x = 10; x <= 600; x += 7) s.generateUntil(x);
    bool stepped = true;
    for (size_t i = 0; i < std::min(s.segs.size(), a.segs.size()); ++i) stepped = stepped && s.segs[i].h == a.segs[i].h && s.segs[i].x0 == a.segs[i].x0;
    check(stepped, "generating as the hen runs matches generating all at once");
    check(!a.generateUntil(500), "generateUntil: nothing new when already generated that far");
    a.reset(42);
    check(a.segs.empty() && a.corns.empty(), "reset clears the course");
}

void difficultyScale() {
    Level easy, hard;
    easy.setDifficulty(0, 0.5);
    hard.setDifficulty(200, 1);
    check(easy.curveAt(200).D == curve(100).D && hard.curveAt(0).D == curve(200).D, "setDifficulty: read at offset + x * scale");
    check(easy.curveAt(-50).D == curve(0).D, "behind the start: the start of the curve");
}

void lookups() {
    Level l;
    l.reset(7);
    l.generateUntil(300);
    bool edges = true;
    for (size_t i = 1; i < l.segs.size(); ++i) {
        const Segment& s = l.segs[i];
        edges = edges && l.heightAt(s.x0) == s.h && l.heightAt((s.x0 + s.x1) / 2) == s.h && l.heightAt(s.x0 - 1e-6) == l.segs[i - 1].h;
    }
    check(edges, "heightAt: a segment starts at x0 (inclusive) and ends before x1");
    check(l.heightAt(-5) == 0 && l.heightAt(1e6) == 0, "heightAt: the street before the course and past the generated part");
    const size_t before = l.segs.size();
    l.dropBehind(200);
    bool kept = true;
    for (const Segment& s : l.segs) kept = kept && s.x1 >= 200 - 14;
    check(l.segs.size() < before && kept && l.heightAt(195) == Level(l).heightAt(195), "dropBehind: forgets walls well behind, keeps the last 14 m");
}

void blocks() {
    Level l;
    l.reset(5);
    l.generateUntil(400);
    l.rebuildBlocks();
    size_t blocksWant = 0, tilesWant = 0, girdersWant = 0, curtainsWant = 0;
    bool mixTop = true, ranges = true;
    for (const Segment& s : l.segs) {
        blocksWant += size_t(cols(s) * s.h);
        if (s.h > 0) tilesWant += size_t(cols(s) * Level::TILE_X * Level::TILE_Z);
        if (s.ceil) { girdersWant += size_t(cols(s)); curtainsWant++; }
        ranges = ranges && s.capCount == (s.h > 0 ? cols(s) * Level::TILE_X * Level::TILE_Z : 0) && s.ceilCount == (s.ceil ? cols(s) : 0);
    }
    for (const Level::Block& b : l.blocks) {
        const int row = int(std::floor(b.pos.y / Uf));
        const int h = l.heightAt(b.pos.x);
        const WallKind k = [&] { for (const Segment& s : l.segs) if (b.pos.x >= s.x0 && b.pos.x < s.x1) return s.kind; return WallKind::Crate; }();
        if (k == WallKind::Mix) mixTop = mixTop && b.hay == (row == h - 1);
        if (k == WallKind::Hay) mixTop = mixTop && b.hay;
        if (k == WallKind::Crate) mixTop = mixTop && !b.hay;
    }
    check(l.blocks.size() == blocksWant, "a block for every crate of every wall");
    check(l.tiles.size() == tilesWant, "2 x 4 dance-floor tiles on every wall top column");
    check(l.girders.size() == girdersWant && l.curtains.size() == curtainsWant, "a girder per barrier column, a laser curtain per barrier");
    check(ranges, "each segment knows its range of tiles and girders");
    check(mixTop, "crate walls are crates, hay walls hay, mixed walls hay on top only");
    bool tints = true;
    for (const Level::Block& b : l.blocks) tints = tints && b.tint >= 0.82f && b.tint <= 1.f;
    check(tints, "block tints stay in 0.82 .. 1");
}

// a campaign level's course, as Campaign::applyTo lays it
Level levelCourse(int level, uint32_t seed = 0) {
    Level l;
    l.reset(seed ? seed : Campaign::seedOf(level));
    l.setDifficulty(35.0 * (level - 1), 0.35);
    l.setFinish(Campaign::lengthOf(level));
    l.setSaws(Campaign::sawsOf(level), Campaign::sawRowOf(level));
    l.setGauntlet(Campaign::gauntletOf(level), Campaign::gauntletAt(level));
    l.generateUntil(l.finish() + 20);
    return l;
}

const Segment* segmentAt(const Level& l, double x) {
    for (const Segment& s : l.segs) if (x >= s.x0 && x < s.x1) return &s;
    return nullptr;
}

// Saws (levels 11 on): their numbers per level, and every one fair: on a flat stretch of its own (never under a
// barrier; the stack meets it on level ground), past the landing from a drop and a reaction after it, a reaction
// after leaving a barrier, a reaction's run before the next wall, one egg's refill after the last saw. On the 30
// campaign levels and on other seeds of levels 11 to 40.
void saws() {
    check(Campaign::sawsOf(1) == 0 && Campaign::sawsOf(10) == 0 && Campaign::sawsOf(11) == 2 && Campaign::sawsOf(20) == 8 &&
          Campaign::sawsOf(30) == 8 && Campaign::sawsOf(100) == 8, "saws: none before level 11, then 2 rising to 8 by level 20");
    bool none = true, counts = true, flat = true, barriers = true, drops = true, after = true, spacing = true, kinds = true, inside = true;
    int cube = 0, floor = 0, wall = 0, street = 0, firstOnStreet = 0, firsts = 0, doubles = 0, triples = 0;
    bool rowsOk = true;
    std::vector<int> longest(41, 1); // the longest row of blades on each level (any seed)
    bool teach = true; // Neon Egypt's levels (11-20), where saws are new: the first is on the street
    std::string missing;
    for (int level = 1; level <= 40; ++level)
        for (uint32_t seed : {0u, 11u, 12u, 13u}) {
            const Level l = levelCourse(level, seed ? seed * 7919u + uint32_t(level) : 0u);
            if (level < 11) { none = none && l.saws.empty(); continue; }
            std::vector<Saw> run; // (the course's own saws: a gauntlet's are its design's)
            for (const Saw& w : l.saws) if (!l.gauntletAt(w.x)) run.push_back(w);
            if (int(run.size()) != Campaign::sawsOf(level)) {
                counts = false;
                missing += " " + std::to_string(level) + (seed ? "/" + std::to_string(seed) : "") + ":" + std::to_string(run.size());
            }
            int row = 1;
            for (size_t i = 0; i < run.size(); ++i) {
                const Saw& w = run[i];
                const bool inRow = i && std::abs(w.x - run[i - 1].x - Level::kSawRowGap) < 1e-6 && w.y == run[i - 1].y;
                row = inRow ? row + 1 : 1;
                rowsOk = rowsOk && row <= Campaign::sawRowOf(level);
                if (row == 2) (Campaign::sawRowOf(level) == 2 ? doubles : triples) += 0; // (counted below)
                if (row >= 2) longest[size_t(level)] = std::max(longest[size_t(level)], row);
                const Segment* s = segmentAt(l, w.x);
                if (!s) { inside = false; continue; }
                const Curve c = l.curveAt(s->x0);
                const int prevH = s == &l.segs.front() ? 0 : (s - 1)->h, prevCeil = s == &l.segs.front() ? 0 : (s - 1)->ceil;
                flat = flat && w.y == s->h && l.heightAt(w.x - Level::kSawReach - 0.3) == s->h && l.heightAt(w.x + Level::kSawReach + 0.3) == s->h;
                barriers = barriers && !s->ceil && (!prevCeil || w.x - Level::kSawReach - s->x0 >= c.speed * 0.6 - 1e-6);
                if (s->h < prevH) {
                    const double fall = std::sqrt(2.0 * (prevH - s->h) * U / 36.0);
                    drops = drops && w.x - Level::kSawReach - s->x0 >= std::max(c.speed * 0.6, c.speed * fall + 0.3) - 1e-6;
                }
                after = after && s->x1 - w.x >= std::max(2.5 * U, c.speed * 0.45 + Level::kSawR) - 1e-6;
                if (i && !inRow) spacing = spacing && w.x - run[i - 1].x >= c.speed * c.regen * 1.1 + c.speed * 0.6 - 1e-6;
                kinds = kinds && w.cube == (s->h > 0 && s->kind != WallKind::Crate);
                (w.cube ? cube : w.y > 0 ? wall : street)++;
                floor += !w.cube;
                if (i == 0) { ++firsts; firstOnStreet += w.y == 0; if (!seed && level <= 20) teach = teach && w.y == 0; }
            }
        }
    if (!missing.empty()) std::printf("  short of saws (level/seed:placed):%s\n", missing.c_str());
    std::printf("      saws: %d on the street, %d on stone, %d in cyber cubes; first on the street %d of %d\n", street, wall, cube, firstOnStreet, firsts);
    check(none, "...no saw on levels 1 to 10");
    check(counts, "...every level from 11 gets all of its saws, before the finish");
    check(inside && flat, "...each sits in the surface of its own flat stretch (the stack meets it on level ground)");
    check(barriers, "...never under a barrier, and a reaction after leaving one");
    check(drops, "...past the landing from a drop, and a reaction after it");
    check(after, "...a reaction's run after it before the next wall");
    check(spacing, "...an egg's refill and a reaction from the last saw");
    check(kinds && cube > 20 && street > 20, "...floor saws in the street and stone, cube saws in cyber cubes (plenty of each)");
    check(teach && firstOnStreet == firsts, "...a course's first saw is the gentle one: alone, on the street (every level, every seed)");
    bool rowsByLevel = true;
    for (int level = 11; level <= 40; ++level) rowsByLevel = rowsByLevel && longest[size_t(level)] <= Campaign::sawRowOf(level);
    int withDoubles = 0, withTriples = 0;
    for (int level = 13; level <= 40; ++level) { withDoubles += longest[size_t(level)] >= 2; withTriples += longest[size_t(level)] >= 3; }
    (void)doubles; (void)triples;
    check(rowsOk && rowsByLevel && longest[11] == 1 && longest[12] == 1, "...rows of blades back to back: none on 11 and 12, up to 2 from 13, up to 3 from 16");
    check(withDoubles >= 15 && withTriples >= 10, "...and rows do come (most levels from 13 have one)");
    // no two levels alike: the layout (walls, barriers, saws, gauntlets) of every campaign level, 1 to 40, is its own
    std::set<std::string> layouts;
    for (int level = 1; level <= 40; ++level) {
        const Level l = levelCourse(level);
        std::string print;
        for (const Segment& s : l.segs) print += std::to_string(cols(s)) + ":" + std::to_string(s.h) + ":" + std::to_string(s.ceil) + (s.gauntlet ? "G" : "") + ",";
        for (const Saw& w : l.saws) print += "s" + std::to_string(int(w.x * 10)) + ",";
        layouts.insert(print);
    }
    check(layouts.size() == 40, "...no two levels alike: every level's layout is its own");
    // they're dropped well behind the hen, like the walls
    Level d = levelCourse(20);
    const size_t had = d.saws.size();
    const double fourth = d.saws[3].x;
    d.dropBehind(d.saws[2].x + 14.01);
    check(d.saws.size() == had - 3 && d.saws.front().x == fourth, "...dropBehind forgets the saws well behind");
    // deterministic, however the course is generated: in one go or as the hen runs
    Level e;
    e.reset(Campaign::seedOf(20)); e.setDifficulty(35.0 * 19, 0.35); e.setFinish(Campaign::lengthOf(20)); e.setSaws(Campaign::sawsOf(20), Campaign::sawRowOf(20));
    for (double x = 10; x <= e.finish() + 20; x += 7) e.generateUntil(x);
    const Level f = levelCourse(20);
    bool same = e.saws.size() == f.saws.size();
    for (size_t i = 0; same && i < e.saws.size(); ++i) same = e.saws[i].x == f.saws[i].x && e.saws[i].y == f.saws[i].y && e.saws[i].cube == f.saws[i].cube;
    check(same, "...the same saws however the course is generated (all at once, or as the hen runs)");
}

} // namespace

// The egg budget: a careful player (a full meter at the start, refilling per metre at the curve's pace, stacking just
// enough, so every climb is paid from the meter) can afford every step up, on every course: the endless daily one and
// every campaign level (each starting 35 m further along the curve, at 0.35x)
void eggBudget() {
    auto affordable = [](const Level& l, double& worst) {
        double meter = Level::kBudgetCap;
        size_t saw = 0;
        for (size_t i = 0; i < l.segs.size(); ++i) {
            const Segment& s = l.segs[i];
            const int prev = i ? l.segs[i - 1].h : 0;
            if (s.h > prev) {
                worst = std::min(worst, meter - (s.h - prev));
                meter -= s.h - prev;
            }
            const Curve c = l.curveAt(s.x0);
            const double perMetre = 1.0 / (c.speed * c.regen * 1.1);
            double at = s.x0;
            for (; saw < l.saws.size() && l.saws[saw].x < s.x1; ++saw) { // each saw on it costs an egg, where it is
                meter = std::min(Level::kBudgetCap, meter + (l.saws[saw].x - at) * perMetre);
                worst = std::min(worst, meter - 1);
                meter -= 1;
                at = l.saws[saw].x;
            }
            meter = std::min(Level::kBudgetCap, meter + (s.x1 - at) * perMetre);
        }
    };
    double worst = 99;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        Level l;
        l.reset(seed * 2654435761u);
        l.generateUntil(3000);
        affordable(l, worst);
    }
    check(worst >= Level::kBudgetMargin - 1e-6, "egg budget: every climb on the endless course is affordable (with eggs to spare)");
    double worstLevels = 99;
    for (int level = 1; level <= 40; ++level)
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            Level l;
            l.reset(seed * 7919u + uint32_t(level));
            l.setDifficulty(35.0 * (level - 1), 0.35);
            l.setFinish(300.0 + 10.0 * (level - 1));
            l.generateUntil(l.finish() + 20);
            affordable(l, worstLevels);
        }
    check(worstLevels >= Level::kBudgetMargin - 1e-6, "...and on every campaign level, 1 to 40");
    double worstSaws = 99;
    for (int level = 11; level <= 40; ++level)
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            Level l;
            l.reset(seed * 7919u + uint32_t(level));
            l.setDifficulty(35.0 * (level - 1), 0.35);
            l.setFinish(300.0 + 10.0 * (level - 1));
            l.setSaws(Campaign::sawsOf(level));
            l.generateUntil(l.finish() + 20);
            affordable(l, worstSaws);
        }
    check(worstSaws >= Level::kBudgetMargin - 1e-6, "...with the saws of levels 11 to 40 too (each costs an egg)");
    // without it, the same courses ask for more than the meter holds (the rule is doing the work)
    double worstOff = 99;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        Level l;
        l.reset(seed * 2654435761u);
        l.setEggBudget(false);
        l.generateUntil(3000);
        affordable(l, worstOff);
    }
    check(worstOff < 0, "...(switched off, the same seeds give climbs no meter can pay for)");
}

int main() {
    test::useFixtures(); // (the levels and gauntlets these tests are written round: tests/fixtures)
    fairness();
    eggBudget();
    saws();
    sectors();
    finishLine();
    determinism();
    difficultyScale();
    lookups();
    blocks();
    return finish("Level");
}
