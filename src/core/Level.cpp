#include "Level.h"
#include "Difficulty.h"
#include "Hieroglyphs.h"
#include "LevelData.h"

#include <algorithm>

namespace cs {

void Level::reset(uint32_t seed) {
    rng_ = Mulberry32(seed);
    sawRng_ = Mulberry32(seed ^ 0x5A3D17C9u);
    segs.clear(); corns.clear(); saws.clear();
    genX_ = 0; lastH_ = 0; segN_ = 0; genSector_ = 0; budget_ = kBudgetCap;
    sawsPlaced_ = 0; nextSawX_ = -1; lastSawX_ = -1e9; nextSawRow_ = 1;
    gauntlets.clear(); gauntletPlaced_ = false; gauntletEnd_ = 0;
    authored_ = false;
}

void Level::load(const LevelData& d) {
    reset(0);
    authored_ = true;
    gauntletPlaced_ = true;
    setDifficulty(d.diffOffset, d.diffScale);
    setFinish(d.length);
    for (const LevelData::Wall& w : d.course) {
        if (!w.gauntlet.empty()) { // its span (its saws below)
            const Gauntlet* g = gauntletNamed(w.gauntlet);
            if (!g) continue; // (parseLevel turns that away)
            gauntlets.push_back({g, genX_});
            pushSeg(g->cols(), 0, WallKind::Crate);
            segs.back().gauntlet = true;
            continue;
        }
        pushSeg(w.cells, w.h, w.kind);
        segs.back().ceil = w.ceil;
    }
    for (const double x : d.saws) { // (set into the wall it's on: a hay or mix wall's is a cube saw)
        Saw saw;
        saw.x = x;
        for (const Segment& s : segs)
            if (x >= s.x0 && x < s.x1) { saw.y = s.h; saw.cube = s.h > 0 && (s.kind == WallKind::Hay || s.kind == WallKind::Mix); break; }
        saws.push_back(saw);
    }
    std::stable_sort(saws.begin(), saws.end(), [](const Saw& a, const Saw& b) { return a.x < b.x; });
    for (const PlacedGauntlet& p : gauntlets) // a gauntlet's own, from its grid, as placeGauntlet lays them
        for (int col = 0; col < p.g->cols(); ++col)
            for (int r = 0; r < p.g->rows(); ++r)
                if (p.g->sawAt(col, r)) {
                    Saw saw;
                    saw.x = p.x0 + (col + 0.5) * U; saw.y = r + 1; saw.cube = p.g->at(col, r) == Gauntlet::Cell::CubeSaw;
                    saws.insert(std::lower_bound(saws.begin(), saws.end(), saw.x, [](const Saw& a, double x) { return a.x < x; }), saw);
                }
    for (const LevelData::Disco& c : d.discos) spawnCorn(c.x, c.y);
}

// (refill: 1 / (speed x seconds per egg) eggs per metre, with 10% slack for an adaptive-difficulty slowdown)
double Level::eggsPerUnit(double x) const { const Curve c = curveAt(x); return U / (c.speed * c.regen * 1.1); }

void Level::pushSeg(int len, int h, WallKind kind) {
    Segment s;
    s.x0 = genX_; s.x1 = genX_ + len * U; s.h = h; s.kind = kind;
    segs.push_back(s);
    if (eggBudget_) creditRun(len, genX_); // (the run along it refills the meter)
    genX_ += len * U; lastH_ = h; segN_++;
}

// The web build uses Math.random() for the bob phase; a position hash keeps it cosmetic and deterministic.
void Level::spawnCorn(double x, double y) { corns.push_back({x, y, hash(x * 1.7) * 6.f}); }

int Level::heightAt(double x) const {
    for (size_t i = segs.size(); i-- > 0;) {
        const Segment& s = segs[i];
        if (x >= s.x0 && x < s.x1) return s.h;
        if (x >= s.x1) break;
    }
    return 0;
}

bool Level::generateUntil(double xmax) {
    bool changed = false;
    while (genX_ < xmax) {
        changed = true;
        if (segN_ == 0) { pushSeg(16, 0, WallKind::Crate); genSector_ = 0; continue; }
        if (authored_) { pushSeg(10, 0, WallKind::Crate); continue; } // (past an authored level's last wall: runway)
        // past the finish line: flat runway for the celebration (no walls, barriers or corn)
        if (finish_ > 0 && genX_ >= finish_ - 2 * U) { pushSeg(10, 0, WallKind::Crate); continue; }
        // a gauntlet due (its approach starts 20 m ahead of it)
        if (gauntletDef_ && !gauntletPlaced_ && genX_ >= gauntletAt_ - 20) { placeGauntlet(); continue; }
        // sector breather: a flat stretch with a rising line of corn
        const int sec = static_cast<int>(std::floor(genX_ / SECTOR));
        if (sec > genSector_) {
            genSector_ = sec;
            const double x0 = genX_;
            int len = 12;
            if (lastH_ > 0) { // (after a tall wall: long enough to land and react, like any drop)
                const double fall = std::sqrt(2.0 * lastH_ * U / 36.0);
                len = std::max(len, static_cast<int>(std::ceil(curveAt(genX_).speed * (fall + 0.45) / U)));
            }
            const double budgetAtStart = budget_;
            const int prevH = lastH_, prevCeil = segs.empty() ? 0 : segs.back().ceil;
            pushSeg(len, 0, WallKind::Crate);
            for (int k = 0; k < 3; ++k) spawnCorn(x0 + (2.5 + k * 3) * U, (2 + k) * U + 0.25);
            if (sawCount_ > 0 && sawsPlaced_ == 0) placeSaw(prevH, prevCeil, budgetAtStart); // (only the gentle first saw: a breather is a rest)
            continue;
        }
        const Curve c = curveAt(genX_);
        const int lastCeil = segs.empty() ? 0 : segs.back().ceil;
        const bool afterDrop = segs.size() >= 2 && segs[segs.size() - 2].h > lastH_;
        int h;
        if (lastH_ > 0 && rng_.next() < c.drop) h = std::max(0, lastH_ - 1 - static_cast<int>(std::floor(rng_.next() * lastH_)));
        else h = std::min(c.cap, lastH_ + 1 + static_cast<int>(std::floor(rng_.next() * c.maxUp)));
        // fairness: after a barrier, the next wall must be climbable while still under that barrier
        if (lastCeil) h = std::min(h, lastCeil - static_cast<int>(std::ceil(HEN_H / U)));
        // a course's first saw is taught on the street: when it's due, down to the street
        if (sawCount_ > 0 && sawsPlaced_ == 0 && nextSawX_ >= 0 && genX_ + 6 >= nextSawX_) h = 0;
        int len = c.minLen + static_cast<int>(std::floor(rng_.next() * (c.maxLen - c.minLen + 1)));
        // fairness: after a drop, leave enough flat run to finish falling and react to the next wall
        if (h < lastH_) {
            const double fall = std::sqrt(2.0 * (lastH_ - h) * U / 36.0);
            len = std::max(len, static_cast<int>(std::ceil(c.speed * (fall + 0.45) / U)));
        }
        const double r = rng_.next();
        const int prevH = lastH_;
        if (eggBudget_ && h > lastH_ && !segs.empty()) { // the egg budget: can a careful player afford this climb?
            const double need = (h - lastH_) + kBudgetMargin;
            if (budget_ < need) {
                const int extra = static_cast<int>(std::ceil((need - budget_) / eggsPerUnit(genX_)));
                if (extra <= kMaxRunup && budget_ + extra * eggsPerUnit(genX_) <= kBudgetCap + 1e-9) {
                    segs.back().x1 += extra * U; // a longer run-up on the segment before the wall
                    creditRun(extra, genX_);
                    genX_ += extra * U;
                } else { // too long a wait (or more than a full meter): a lower wall, after a full refill if needed
                    const int extraMax = std::min(kMaxRunup, static_cast<int>(std::ceil((kBudgetCap - budget_) / eggsPerUnit(genX_))));
                    if (extraMax > 0) { segs.back().x1 += extraMax * U; creditRun(extraMax, genX_); genX_ += extraMax * U; }
                    h = std::max(lastH_ + 1, std::min(h, lastH_ + static_cast<int>(std::floor(budget_ - kBudgetMargin))));
                }
            }
            budget_ = std::max(0.0, budget_ - (h - lastH_)); // the eggs knocked off climbing it
        }
        const double budgetAtStart = budget_; // (the meter where the new segment starts: a saw on it charges from here)
        pushSeg(len, h, r < 0.4 ? WallKind::Crate : r < 0.7 ? WallKind::Hay : WallKind::Mix);
        Segment& seg = segs.back();
        // barriers only over rising walls that don't follow a drop (the hen would still be falling)
        if (genX_ > 30 && h > prevH && !afterDrop && rng_.next() < c.ceil) {
            seg.ceil = h + c.gap + (rng_.next() < 0.35 ? 1 : 0);
            // fairness: you can't pre-stack past a barrier, so it must last long enough to react and lay under it
            const int minUnits = static_cast<int>(std::ceil(c.speed * 0.5 / U));
            const int cur = jsRound((seg.x1 - seg.x0) / U);
            if (cur < minUnits) {
                if (eggBudget_) creditRun(minUnits - cur, genX_);
                seg.x1 = seg.x0 + minUnits * U; genX_ = seg.x1;
            }
        }
        if (rng_.next() < c.corn) {
            const double y = seg.ceil ? h * U + 0.7 : (h + 2 + static_cast<int>(std::floor(rng_.next() * 3))) * U + 0.25;
            spawnCorn(genX_ - len * U * 0.5, y);
        }
        if (sawCount_ > 0) placeSaw(prevH, lastCeil, budgetAtStart);
    }
    return changed;
}

// The k-th blade is due here: spread evenly over the open course from 40 m to 40 m short of the finish line (endless:
// one per 60 m), a gauntlet's stretch left out (from its approach to its porch: guessed till it's laid), each nudged by
// up to an eighth of the spacing either way, and at least 60% of the spacing after the one before (when that one came
// late). Also how many blades come in a row there: a course's first saw alone, then 1 up to the row's most.
double Level::sawTarget(int k) {
    constexpr double kStart = 40.0;
    const double end = finish_ > 0 ? finish_ - 40.0 : kStart + 60.0 * sawCount_;
    double g0 = 0, span = 0; // the gauntlet's stretch
    if (gauntletDef_ && gauntletAt_ - 20 > kStart && gauntletAt_ - 20 < end) {
        g0 = gauntletAt_ - 20;
        const double guess = 20 + 2.5 * gauntletDef_->entryRow() + gauntletDef_->cols() * U + 8 * U; // (approach, steps, span, porch)
        span = std::min(end - g0, gauntletPlaced_ ? gauntletEnd_ - g0 : guess);
    }
    const double gap = (end - kStart - span) / sawCount_;
    double x = kStart + (k + 0.5 + (sawRng_.next() - 0.5) * 0.25) * gap;
    if (span > 0 && x > g0) x += span;
    x = std::max(x, lastSawX_ + 0.6 * gap);
    if (k == 0 && gauntletDef_) x = std::min(x, gauntletAt_ - 60); // (the first saw before a gauntlet: one new thing at a time)
    nextSawRow_ = k == 0 ? 1 : std::min(sawCount_ - k, 1 + static_cast<int>(std::floor(sawRng_.next() * sawRow_)));
    return x;
}

// A saw (or a row of them) on the segment just laid, if one is due by its end (see setSaws). budgetAtStart: the
// careful player's meter where the segment starts (after its climb)
void Level::placeSaw(int prevH, int prevCeil, double budgetAtStart) {
    if (sawsPlaced_ >= sawCount_ || segs.size() < 2) return;
    if (nextSawX_ < 0) nextSawX_ = sawTarget(sawsPlaced_);
    Segment& s = segs.back();
    // due by this segment's end (or nearly: it's lengthened to fit); never under a barrier
    if (s.x1 + 6.0 < nextSawX_ || s.ceil || s.gauntlet) return;
    // the first saw of a course is the gentle one, on the street (a breather's too), if the street comes along within
    // 60 m (and before a gauntlet's approach)
    if (sawsPlaced_ == 0 && s.h > 0 && s.x0 < nextSawX_ + 60 && !(gauntletDef_ && !gauntletPlaced_ && s.x1 > gauntletAt_ - 45)) return;
    const Curve c = curveAt(s.x0);
    const double react = c.speed * 0.6, epu = eggsPerUnit(s.x0);
    const int k = nextSawRow_;
    // its first spot: a beat past a climb's edge (the stack has landed); past the landing from a drop, and a reaction
    // after it; a reaction after leaving a barrier (to re-stack); one egg's refill and a reaction after the last saw
    double lo = s.x0 + 2.5 * U;
    if (s.h < prevH) lo = std::max(lo, s.x0 + std::max(react, c.speed * std::sqrt(2.0 * (prevH - s.h) * U / 36.0) + 0.3) + kSawReach);
    if (prevCeil) lo = std::max(lo, s.x0 + react + kSawReach);
    lo = std::max(lo, lastSawX_ + U / epu + react);
    double sx = std::max(lo, nextSawX_);
    // the egg budget: a careful player has an egg for each blade (with eggs to spare) when they get there
    if (eggBudget_) {
        const double shortBy = k + kBudgetMargin - std::min(kBudgetCap, budgetAtStart + (sx - s.x0) / U * epu);
        if (shortBy > 0) sx += std::ceil(shortBy / epu - 1e-9) * U;
    }
    // and a reaction's run after the last blade (to stack again for the next wall)
    const double last = sx + (k - 1) * kSawRowGap;
    const double end = last + std::max(2.5 * U, c.speed * 0.45 + kSawR);
    if (finish_ > 0 && end > finish_ - 8) return;
    if (end > s.x1) { // its flat stretch, lengthened to fit
        s.x1 += std::ceil((end - s.x1) / U - 1e-9) * U;
        genX_ = s.x1;
    }
    if (eggBudget_) {
        const double atSaw = std::min(kBudgetCap, budgetAtStart + (sx - s.x0) / U * epu);
        budget_ = std::min(kBudgetCap, atSaw - k + (s.x1 - sx) / U * epu);
    }
    for (int i = 0; i < k; ++i) {
        Saw saw;
        saw.x = sx + i * kSawRowGap; saw.y = s.h;
        saw.cube = s.h > 0 && (s.kind == WallKind::Hay || s.kind == WallKind::Mix);
        saws.push_back(saw);
    }
    lastSawX_ = last; sawsPlaced_ += k; nextSawX_ = -1;
}

// The course's step up to h, affordable: as in generateUntil, a longer run-up on the segment before (or, past
// kMaxRunup, a lower step), and the eggs it knocks off charged
void Level::affordClimb(int& h) {
    if (!eggBudget_ || h <= lastH_ || segs.empty()) return;
    const double need = (h - lastH_) + kBudgetMargin;
    if (budget_ < need) {
        const int extra = static_cast<int>(std::ceil((need - budget_) / eggsPerUnit(genX_)));
        if (extra <= kMaxRunup && budget_ + extra * eggsPerUnit(genX_) <= kBudgetCap + 1e-9) {
            segs.back().x1 += extra * U;
            creditRun(extra, genX_);
            genX_ += extra * U;
        } else {
            const int extraMax = std::min(kMaxRunup, static_cast<int>(std::ceil((kBudgetCap - budget_) / eggsPerUnit(genX_))));
            if (extraMax > 0) { segs.back().x1 += extraMax * U; creditRun(extraMax, genX_); genX_ += extraMax * U; }
            h = std::max(lastH_ + 1, std::min(h, lastH_ + static_cast<int>(std::floor(budget_ - kBudgetMargin))));
        }
    }
    budget_ = std::max(0.0, budget_ - (h - lastH_));
}

// The gauntlet (setGauntlet), laid now: steps to its entry's height (at most the curve's step up each, affordable; a
// drop gets its landing run), a forecourt, the gauntlet's span, its saws, and a porch at its exit's height
void Level::placeGauntlet() {
    const Gauntlet& g = *gauntletDef_;
    gauntletPlaced_ = true;
    const Curve c = curveAt(genX_);
    // out from under a barrier first: a step up stacks a full step's eggs, and under it there's no room (its run-up
    // would be its tunnel)
    if (!segs.empty() && segs.back().ceil && g.entryRow() > lastH_) pushSeg(c.minLen + 1, lastH_, WallKind::Crate);
    while (lastH_ != g.entryRow()) {
        int h = lastH_ < g.entryRow() ? std::min(g.entryRow(), lastH_ + std::max(1, c.maxUp)) : g.entryRow();
        int len = c.minLen + 1;
        if (h < lastH_) len = std::max(len, static_cast<int>(std::ceil(c.speed * (std::sqrt(2.0 * (lastH_ - h) * U / 36.0) + 0.45) / U)));
        affordClimb(h);
        pushSeg(len, h, WallKind::Crate);
    }
    // the forecourt: a reaction's run at the door's height, long enough to refill two and a half eggs at the budget's
    // careful rate (a maze's first saws count on two at the slowest refill); a door too low for a full stack ends it
    // under its lintel: a barrier as low as the door, telegraphed as any barrier (a stack too tall for it crashes)
    const int fore = std::max({8, static_cast<int>(std::ceil(c.speed * 0.9 / U)), static_cast<int>(std::ceil(2.5 / eggsPerUnit(genX_)))});
    const bool lintel = g.entryTop() - g.entryRow() - Gauntlet::kBodyRows < static_cast<int>(kBudgetCap);
    pushSeg(lintel ? fore - kLintel : fore, g.entryRow(), WallKind::Crate);
    if (lintel) { pushSeg(kLintel, g.entryRow(), WallKind::Crate); segs.back().ceil = g.entryTop(); }
    const double x0 = genX_;
    pushSeg(g.cols(), 0, WallKind::Crate);
    segs.back().gauntlet = true;
    gauntlets.push_back({&g, x0});
    for (int col = 0; col < g.cols(); ++col)
        for (int r = 0; r < g.rows(); ++r)
            if (g.sawAt(col, r)) {
                Saw saw;
                saw.x = x0 + (col + 0.5) * U; saw.y = r + 1; saw.cube = g.at(col, r) == Gauntlet::Cell::CubeSaw;
                saws.insert(std::lower_bound(saws.begin(), saws.end(), saw.x, [](const Saw& a, double x) { return a.x < x; }), saw);
            }
    lastSawX_ = std::max(lastSawX_, genX_);
    lastH_ = 0; // (pushSeg: the span's placeholder)
    pushSeg(8, g.exitRow(), WallKind::Crate); // the porch
    gauntletEnd_ = genX_;
    budget_ = std::min(kBudgetCap, 1.0 + 8 * eggsPerUnit(genX_)); // (what's left after the maze: a careful guess, low)
    genSector_ = static_cast<int>(std::floor(genX_ / SECTOR));   // (no breather straight after)
}

// A tomb's inscriptions, from its grid, laid out like the Pyramid Texts: in each column, every run of air 3 or more
// cells high (a room, there) is a column of signs, one a cell and read top down, over a blank dado (the cell on its
// floor) and, in a room 7 or more high, under a star along its top. Birds and the egg come up most. A room 12 or more
// high holds a cartouche every 24 columns, "Cuckoo", under its stars. Rules run down between the columns, along the
// dado and under the stars. The doorways stay bare.
void Level::inscribe(const PlacedGauntlet& p) {
    const Gauntlet& g = *p.g;
    const float z = -DEPTH / 2 + 0.01f; // (just in front of the back wall)
    auto at = [&](double c, double r) { return glm::vec3(float(p.x0 + c * U), float(r * U), z); };
    auto run = [&](int c, int r) { // the height of the run of air through (c, r), 0 if it's not air
        if (c < 1 || c >= g.cols() - 1 || g.at(c, r) != Gauntlet::Cell::Air) return 0;
        int a = r, b = r;
        while (a > 0 && g.at(c, a - 1) == Gauntlet::Cell::Air) --a;
        while (b < g.rows() - 1 && g.at(c, b + 1) == Gauntlet::Cell::Air) ++b;
        return b - a + 1;
    };
    const double salt = p.x0 * 0.37;
    for (int c = 1; c < g.cols() - 1; ++c)
        for (int r = 0; r < g.rows();) {
            if (g.at(c, r) != Gauntlet::Cell::Air) { ++r; continue; }
            const int r0 = r;
            while (r < g.rows() && g.at(c, r) == Gauntlet::Cell::Air) ++r;
            const int r1 = r - 1, h = r1 - r0 + 1;
            if (h < 3) continue;
            const bool stars = h >= 7, cartouche = h >= 12 && c % 24 == 12;
            const int top = stars ? r1 - 1 : r1; // (the column's top sign)
            uint32_t above = 0;
            for (int k = top; k > r0; --k) {
                const float seed = hash(salt + c * 12.9898 + k * 78.233);
                if (cartouche && k <= r1 - 2 && k >= r1 - 5) { // (k-w-k-w, read top down)
                    gsigns.push_back({at(c + 0.5, k + 0.5), hieroglyphs::kCuckoo[size_t(r1 - 2 - k)], 2, seed});
                    continue;
                }
                const float pickA = hash(salt + c * 3.17 + k * 5.71), pickB = hash(salt + c * 7.31 + k * 2.13);
                const size_t birds = hieroglyphs::kBirds, rest = hieroglyphs::kColumns.size() - birds;
                size_t i = pickA < 0.4f ? size_t(pickB * birds) % birds : birds + size_t(pickB * rest) % rest;
                if (hieroglyphs::kColumns[i] == above) i = (i + 1) % hieroglyphs::kColumns.size(); // (never twice running)
                above = hieroglyphs::kColumns[i];
                gsigns.push_back({at(c + 0.5, k + 0.5), above, 0, seed});
            }
            if (stars) gsigns.push_back({at(c + 0.5, r1 + 0.5), hieroglyphs::kStar, 1, hash(salt + c * 1.7)});
            if (cartouche) gcartouches.push_back({at(c + 0.5, r1 - 3), {0.86f * Uf, 4.3f * Uf}});
            const float thin = 0.018f;
            grules.push_back({at(c, (r0 + 1 + top + 1) * 0.5), {thin, float(top - r0) * Uf}});        // down its left side
            if (run(c + 1, (r0 + top) / 2 + 1) < 3)                                                   // ...and its right, at the end
                grules.push_back({at(c + 1, (r0 + 1 + top + 1) * 0.5), {thin, float(top - r0) * Uf}});
            grules.push_back({at(c + 0.5, r0 + 1), {Uf, thin}});                                      // along the dado
            if (stars) grules.push_back({at(c + 0.5, r1), {Uf, thin}});                               // under the stars
        }
}

double Level::groundBelow(double x, double y) const {
    if (const PlacedGauntlet* p = gauntletAt(x)) {
        const int c = p->col(x);
        for (int r = std::min(p->g->rows(), static_cast<int>(std::floor(y / U + 1e-6))) - 1; r >= 0; --r)
            if (p->g->solid(c, r)) return (r + 1) * U;
        return 0;
    }
    return heightAt(x) * U;
}

void Level::rebuildBlocks() {
    blocks.clear(); tiles.clear(); girders.clear(); curtains.clear(); gcells.clear(); gback.clear();
    gsigns.clear(); grules.clear(); gcartouches.clear();
    // the gauntlets' cells: solid ones (which sides are open: a bumper's chevrons face them), and the back wall
    for (const PlacedGauntlet& p : gauntlets)
        for (int c = 0; c < p.g->cols(); ++c)
            for (int r = 0; r < p.g->rows(); ++r) {
                const glm::vec3 pos{float(p.x0 + (c + 0.5) * U), float((r + 0.5) * U), 0.f};
                const Gauntlet::Cell k = p.g->at(c, r);
                if (k == Gauntlet::Cell::Air) { gback.push_back(pos); continue; }
                gcells.push_back({pos, k, uint8_t((p.g->solid(c - 1, r) ? 0 : 1) | (p.g->solid(c + 1, r) ? 0 : 2))});
            }
    for (const PlacedGauntlet& p : gauntlets) inscribe(p);
    for (const Segment& s : segs) {
        const int cols = jsRound((s.x1 - s.x0) / U);
        for (int i = 0; i < cols; ++i)
            for (int j = 0; j < s.h; ++j) {
                const double x = s.x0 + (i + 0.5) * U, y = (j + 0.5) * U;
                const bool hay = s.kind == WallKind::Hay || (s.kind == WallKind::Mix && j == s.h - 1);
                blocks.push_back({glm::vec3(float(x), float(y), 0.f), hash(x * 3.1 + j * 7.7) >= 0.5f, hay,
                                  0.82f + 0.18f * hash(x * 9.3 + j * 1.9)});
            }
    }
    // disco dance floor on every wall top: 2 x 4 light tiles per column
    for (Segment& s : segs) {
        s.capStart = static_cast<int>(tiles.size()); s.capCount = 0;
        if (s.h <= 0) continue;
        const int cols = jsRound((s.x1 - s.x0) / U);
        for (int i = 0; i < cols; ++i)
            for (int a = 0; a < TILE_X; ++a)
                for (int b = 0; b < TILE_Z; ++b) {
                    const float tx = float(s.x0 + i * U) + (a + 0.5f) * TW, tz = -DEPTH / 2 + (b + 0.5f) * TD;
                    tiles.push_back({{tx, float(s.h * U) + 0.025f, tz}, jsRound((s.x0 + i * U) / U) * TILE_X + a, b});
                    s.capCount++;
                }
    }
    // overhead barriers: a hazard-striped girder, a bright underside edge, and a red laser curtain above it
    for (Segment& s : segs) {
        s.ceilStart = static_cast<int>(girders.size()); s.ceilCount = 0;
        if (!s.ceil) continue;
        const int cols = jsRound((s.x1 - s.x0) / U);
        const float cy = float(s.ceil * U);
        for (int i = 0; i < cols; ++i) {
            girders.push_back({{float(s.x0 + (i + 0.5) * U), cy, 0.f}});
            s.ceilCount++;
        }
        curtains.push_back({{float((s.x0 + s.x1) / 2), cy + 0.7f, 0.f}, float(s.x1 - s.x0)});
    }
}

} // namespace cs
