// The course generator (Level.cpp): its fairness rules over many seeds (the egg budget: every climb affordable), sector breathers, the finish runway, determinism, lookups, and the blocks,
// dance-floor tiles and barriers built from the segments.
#include "TestUtil.h"
#include "core/Difficulty.h"
#include "core/Level.h"

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

} // namespace

// The egg budget: a careful player (a full meter at the start, refilling per metre at the curve's pace, stacking just
// enough, so every climb is paid from the meter) can afford every step up, on every course: the endless daily one and
// every campaign level (each starting 35 m further along the curve, at 0.35x)
void eggBudget() {
    auto affordable = [](const Level& l, double& worst) {
        double meter = Level::kBudgetCap;
        for (size_t i = 0; i < l.segs.size(); ++i) {
            const Segment& s = l.segs[i];
            const int prev = i ? l.segs[i - 1].h : 0;
            if (s.h > prev) {
                worst = std::min(worst, meter - (s.h - prev));
                meter -= s.h - prev;
            }
            const Curve c = l.curveAt(s.x0);
            meter = std::min(Level::kBudgetCap, meter + (s.x1 - s.x0) / (c.speed * c.regen * 1.1));
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
    fairness();
    eggBudget();
    sectors();
    finishLine();
    determinism();
    difficultyScale();
    lookups();
    blocks();
    return finish("Level");
}
