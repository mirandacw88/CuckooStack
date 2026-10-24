#include "Level.h"

#include "CourseRules.h"
#include "Difficulty.h"
#include "Hieroglyphs.h"
#include "LevelCatalog.h"
#include "LevelData.h"

#include <algorithm>

namespace cs {

void Level::reset(uint32_t seed) {
    rng_ = Mulberry32(seed);
    sawRng_ = Mulberry32(seed ^ 0x5A3D17C9u);
    hazRng_ = Mulberry32(seed ^ 0x1B6E44A3u);
    segs.clear(); corns.clear(); saws.clear(); lasers.clear(); lavas.clear(); springs.clear(); gsprings.clear();
    lasersPlaced_ = lavaPlaced_ = 0; lastHazX_ = -1e9; nextHazX_ = -1;
    genX_ = 0; lastH_ = 0; segN_ = 0; genSector_ = 0; budget_ = kBudgetCap;
    sawsPlaced_ = 0; nextSawX_ = -1; lastSawX_ = -1e9; nextSawRow_ = 1;
    gauntlets.clear(); gauntletPlaced_ = false; gauntletEnd_ = 0;
    zones.clear(); zoneGrids_.clear();
    authored_ = false;
    hard_ = 0;
}

void Level::load(const LevelData& d, const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    reset(0);
    authored_ = true;
    gauntletPlaced_ = true;
    setDifficulty(d.diffOffset, d.diffScale);
    setFinish(d.length);
    setHard(d.hard);
    for (const LevelData::Wall& w : d.course) {
        if (!w.gauntlet.empty()) { // its span (its saws below)
            const Gauntlet* g = cat->gauntlet(w.gauntlet);
            if (!g) continue; // (parseLevel turns that away)
            gauntlets.push_back({g, genX_});
            pushSeg(g->cols(), 0, WallKind::Crate);
            segs.back().gauntlet = true;
            continue;
        }
        pushSeg(w.cells, w.h, w.kind);
        segs.back().ceil = w.ceil;
        segs.back().top = w.top;
    }
    for (const Placed& p : d.items) p.type->spawn(*this, p); // (its hazards, each as its row lays it: Hazards.h)
    std::stable_sort(saws.begin(), saws.end(), [](const Saw& a, const Saw& b) { return a.x < b.x; });
    std::stable_sort(lasers.begin(), lasers.end(), [](const Laser& a, const Laser& b) { return a.x < b.x; });
    std::stable_sort(lavas.begin(), lavas.end(), [](const Lava& a, const Lava& b) { return a.x0 < b.x0; });
    std::stable_sort(springs.begin(), springs.end(), [](const Spring& a, const Spring& b) { return a.x < b.x; });
    for (const PlacedGauntlet& p : gauntlets) gauntletHazards(p); // (a gauntlet's own, from its grid, as placeGauntlet lays them)
    buildZones();
}

void Level::buildZones() {
    zones.clear(); zoneGrids_.clear();
    std::vector<std::pair<double, double>> groups; // (from the first bouncer's x0 to the last's x1)
    for (const Segment& s : segs) {
        if (s.gauntlet || !bouncer(s.kind)) continue;
        if (!groups.empty() && s.x0 - groups.back().second <= kZoneGap * U + 1e-6) groups.back().second = s.x1;
        else groups.push_back({s.x0, s.x1});
    }
    for (const auto& [b0, b1] : groups) {
        const double x0 = std::max(0.0, b0 - U), x1 = b1; // (from the column she comes to it in)
        const int cols = static_cast<int>(std::lround((x1 - x0) / U));
        int high = 0;
        for (const Segment& s : segs) if (s.x1 > x0 + 1e-6 && s.x0 < x1 - 1e-6) high = std::max({high, s.h, s.top, s.ceil});
        const int rows = high + Gauntlet::kBodyRows + 1; // (over its walls the sky: Gauntlet::at's air, as high as she stacks)
        std::vector<Gauntlet::Cell> cells(size_t(cols * rows), Gauntlet::Cell::Air);
        auto set = [&](int c, int r, Gauntlet::Cell k) { if (c >= 0 && c < cols && r >= 0 && r < rows) cells[size_t(r * cols + c)] = k; };
        for (int c = 0; c < cols; ++c) {
            const double x = x0 + (c + 0.5) * U;
            const Segment* s = nullptr;
            for (const Segment& sg : segs) if (x >= sg.x0 && x < sg.x1) { s = &sg; break; }
            if (!s) continue;
            const Gauntlet::Cell wall = s->kind == WallKind::Bumper ? Gauntlet::Cell::Bumper : s->kind == WallKind::GateR ? Gauntlet::Cell::GateR
                                      : s->kind == WallKind::GateL ? Gauntlet::Cell::GateL : Gauntlet::Cell::Stone;
            for (int r = 0; r < s->h; ++r) set(c, r, wall);
            if (s->ceil) for (int r = s->ceil; r < (s->top ? s->top : rows); ++r) set(c, r, Gauntlet::Cell::Stone);
        }
        for (const Saw& w : saws) // (a saw on a wall's top: the maze's; one on the street, the checker notes)
            if (w.x >= x0 && w.x < x1 && w.y > 0) set(static_cast<int>(std::floor((w.x - x0) / U)), w.y - 1, w.cube ? Gauntlet::Cell::CubeSaw : Gauntlet::Cell::Saw);
        for (const Lava& f : lavas) // (lava on a wall's top: the maze's lava cells; on the street, the checker notes)
            if (f.floor > 0 && f.x1 > x0 + 1e-6 && f.x0 < x1 - 1e-6)
                for (int c = static_cast<int>(std::lround((f.x0 - x0) / U)); c < static_cast<int>(std::lround((f.x1 - x0) / U)); ++c) set(c, f.floor - 1, Gauntlet::Cell::Lava);
        for (const Spring& sp : springs) // (a spring on a wall's top: the maze's spring cell, flown by the maze's rules)
            if (sp.x >= x0 && sp.x < x1 && sp.floor > 0) set(static_cast<int>(std::floor((sp.x - x0) / U)), sp.floor - 1, Gauntlet::Cell::Spring);
        for (const Laser& z : lasers) { // (a laser in a wall's face: the maze's, in the cell it's set into)
            const int edge = static_cast<int>(std::lround((z.x - x0) / U)), in = z.dir > 0 ? edge - 1 : edge;
            if (in >= 0 && in < cols && z.x > x0 - 1e-6 && z.x < x1 + 1e-6) set(in, z.row, z.dir > 0 ? Gauntlet::Cell::LaserR : Gauntlet::Cell::LaserL);
        }
        auto g = std::make_shared<const Gauntlet>(Gauntlet::zone("bounce zone at " + std::to_string(static_cast<int>(std::lround(x0))) + " m", cols, rows,
                                                                 std::move(cells), heightAt(x0 - 0.01 > 0 ? x0 + 0.01 : 0.01), heightAt(x1 + 0.01)));
        zoneGrids_.push_back(g);
        zones.push_back({g.get(), x0});
    }
}

// A gauntlet's grid's hazards on the course: a saw in each saw cell (its hub over the cell, set in its top), a laser in
// each laser cell (in its face, its reach the grid's beam along its row), and lava over each run of lava cells along a
// row (she runs on their top)
void Level::gauntletHazards(const PlacedGauntlet& p) {
    const Gauntlet& g = *p.g;
    for (int col = 0; col < g.cols(); ++col)
        for (int r = 0; r < g.rows(); ++r) {
            const double x = p.x0 + (col + 0.5) * U;
            if (g.sawAt(col, r)) {
                Saw saw;
                saw.x = x; saw.y = r + 1; saw.cube = g.at(col, r) == Gauntlet::Cell::CubeSaw;
                saws.insert(std::lower_bound(saws.begin(), saws.end(), saw.x, [](const Saw& a, double v) { return a.x < v; }), saw);
            } else if (g.springAt(col, r)) { // a pad on the cell's top (she's flown by the maze's rules: Gauntlet::flightFrom)
                Spring sp;
                sp.x = x; sp.floor = r + 1; sp.power = Gauntlet::kSpringPower;
                gsprings.insert(std::lower_bound(gsprings.begin(), gsprings.end(), sp.x, [](const Spring& a, double v) { return a.x < v; }), sp);
            } else if (Gauntlet::laser(g.at(col, r))) {
                Laser z;
                z.dir = g.at(col, r) == Gauntlet::Cell::LaserL ? -1 : 1;
                z.row = r;
                z.x = p.x0 + (z.dir < 0 ? col : col + 1) * U;
                int end = col + z.dir; // (the last cell its beam reaches)
                while (g.beamAt(end + z.dir, r)) end += z.dir;
                z.x0 = z.dir < 0 ? p.x0 + end * U : z.x;
                z.x1 = z.dir < 0 ? z.x : p.x0 + (end + 1) * U;
                if (!g.beamAt(col + z.dir, r)) z.x1 = z.x0; // (its face against a wall: no reach)
                lasers.insert(std::lower_bound(lasers.begin(), lasers.end(), z.x, [](const Laser& a, double v) { return a.x < v; }), z);
            }
        }
    for (int r = 0; r < g.rows(); ++r)
        for (int c = 0; c < g.cols();) {
            if (g.at(c, r) != Gauntlet::Cell::Lava) { ++c; continue; }
            int e = c;
            while (e + 1 < g.cols() && g.at(e + 1, r) == Gauntlet::Cell::Lava) ++e;
            Lava lava{p.x0 + c * U, p.x0 + (e + 1) * U, r + 1};
            lavas.insert(std::lower_bound(lavas.begin(), lavas.end(), lava.x0, [](const Lava& a, double v) { return a.x0 < v; }), lava);
            c = e + 1;
        }
}

namespace {

// the surface she runs on at x (eggs): a floating block's top, else the wall's
int surfaceAt(const std::vector<Segment>& segs, double x) {
    for (const Segment& s : segs)
        if (x >= s.x0 && x < s.x1) return s.top ? s.top : s.h;
    return 0;
}

} // namespace

void Level::addLaser(double x, int row, int dir) {
    Laser z;
    z.x = x; z.row = row; z.dir = dir < 0 ? -1 : 1;
    lasers.push_back(z);
}

bool Level::solidAt(double x, int row) const {
    for (const Segment& s : segs) {
        if (x < s.x0 || x >= s.x1) continue;
        if (s.gauntlet) return true; // (a maze's span: its own grid)
        if (row < s.h) return true;
        if (s.top) return row >= s.ceil && row < s.top;
        return s.ceil && row >= s.ceil;
    }
    return false;
}

bool Level::laserTakesAnEgg(Laser z) const {
    // (the walls only: a floating block she can duck under or climb, her choice)
    if (!traceLaser(z) || z.x1 - z.x0 < U * 0.5) return false;
    int prev = heightAt(z.x0 - U * 0.5);
    for (double x = z.x0 + U * 0.5; x < z.x1; x += U) {
        const int f = heightAt(x);
        if (f <= z.row && (z.row < f + Gauntlet::kBodyRows || prev > z.row)) return true;
        prev = f;
    }
    const int next = heightAt(z.x1 + U * 0.5); // (the wall that stops it, climbed with a stack up through its row)
    return next > z.row && prev <= z.row;
}

bool Level::traceLaser(Laser& z) const {
    if (z.traced()) return true;
    const double far = z.dir < 0 ? z.x - kLaserRange : z.x + kLaserRange;
    if (z.dir > 0 && genX_ < std::min(far, finish_ > 0 ? finish_ : far) && (finish_ <= 0 || genX_ < finish_)) return false; // (not laid that far)
    double e = z.x;
    for (int k = 0; k < int(kLaserRange / U); ++k) {
        const double mid = e + z.dir * U * 0.5;
        if (mid < 0 || solidAt(mid, z.row)) break;
        e += z.dir * U;
    }
    z.x0 = std::min(z.x, e);
    z.x1 = std::max(z.x, e);
    return true;
}

void Level::addLava(double x, int cells) { lavas.push_back({x, x + cells * U, surfaceAt(segs, x)}); }

void Level::addSpring(double x, int power) {
    Spring s;
    s.x = x; s.floor = surfaceAt(segs, x); s.power = std::clamp(power, 2, 12);
    springs.push_back(s);
}

void Level::addSaw(double x) { // (set into the wall it's on: a hay or mix wall's is a cube saw)
    Saw saw;
    saw.x = x;
    for (const Segment& s : segs)
        if (x >= s.x0 && x < s.x1) { // (a floating block's: on its top)
            saw.y = s.top ? s.top : s.h;
            saw.cube = saw.y > 0 && (s.kind == WallKind::Hay || s.kind == WallKind::Mix);
            break;
        }
    saws.push_back(saw);
}

// (refill: 1 / (speed x seconds per egg) eggs per metre, with 10% slack for an adaptive-difficulty slowdown)
double Level::eggsPerUnit(double x) const { const Curve c = curveAt(x); return U / (c.speed * c.regen * 1.1); }

int Level::forecourtAt(double x) const { return rules::forecourtCells(curveAt(x).speed, eggsPerUnit(x)); }

void Level::pushSeg(int len, int h, WallKind kind) {
    Segment s;
    s.x0 = genX_; s.x1 = genX_ + len * U; s.h = h; s.kind = kind;
    segs.push_back(s);
    if (eggBudget_) creditRun(len, genX_); // (the run along it refills the meter)
    genX_ += len * U; lastH_ = h; segN_++;
}

// The web build uses Math.random() for the bob phase; a position hash keeps it cosmetic and deterministic.
void Level::spawnCorn(double x, double y) { if (cornOn_) corns.push_back({x, y, hash(x * 1.7) * 6.f}); }

int Level::heightAt(double x) const {
    for (size_t i = segs.size(); i-- > 0;) {
        const Segment& s = segs[i];
        if (x >= s.x0 && x < s.x1) return s.h;
        if (x >= s.x1) break;
    }
    return 0;
}

int Level::floorAt(double x, double feet) const {
    for (size_t i = segs.size(); i-- > 0;) {
        const Segment& s = segs[i];
        if (x >= s.x0 && x < s.x1) return s.top && feet >= s.top * U - 0.05 ? s.top : s.h;
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
                len = std::max(len, rules::landingCells(curveAt(genX_).speed, lastH_));
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
        if (h < lastH_) len = std::max(len, rules::landingCells(c.speed, lastH_ - h));
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
        if (laserCount_ + lavaCount_ > 0) placeLaserOrLava(prevH, lastCeil, budgetAtStart);
    }
    return changed;
}

// The k-th laser or lava (counted together) is due here: spread evenly over the open course from 30 m to 30 m short of
// the finish line (endless: one per 70 m), each nudged by up to a sixth of the spacing either way
double Level::hazardTarget(int k) {
    constexpr double kStart = 30.0;
    const int count = laserCount_ + lavaCount_;
    const double end = finish_ > 0 ? finish_ - 30.0 : kStart + 70.0 * count;
    const double gap = (end - kStart) / count;
    return kStart + (k + 0.5 + (hazRng_.next() - 0.5) * 0.33) * gap;
}

// A laser or lava on the segment just laid, if one is due by its end (see setLasers): never under a barrier, in a
// gauntlet or on a saw's stretch; past a climb's edge (time to set the stack), the landing from a drop, a barrier's
// end; a reaction's run after it before the next wall; the eggs it takes in the budget
void Level::placeLaserOrLava(int prevH, int prevCeil, double budgetAtStart) {
    const int count = laserCount_ + lavaCount_, done = lasersPlaced_ + lavaPlaced_;
    if (done >= count || segs.size() < 2) return;
    Segment& s = segs.back();
    if (s.ceil || s.gauntlet || s.top) return;
    if (!saws.empty() && saws.back().x >= s.x0 && !gauntletAt(saws.back().x)) return; // (the saw's stretch)
    if (nextHazX_ < 0) nextHazX_ = hazardTarget(done);
    if (s.x1 + 6.0 < nextHazX_) return;
    bool laser = lasersPlaced_ < laserCount_ &&
                 (lavaPlaced_ >= lavaCount_ || hazRng_.next() < double(laserCount_ - lasersPlaced_) / (count - done));
    const Curve c = curveAt(s.x0);
    const double react = c.speed * 0.6, epu = eggsPerUnit(s.x0);
    double lo = s.x0 + 2.5 * U;
    if (s.h < prevH) lo = std::max(lo, s.x0 + rules::sawAfterDrop(c.speed, react, prevH - s.h) + U);
    if (s.h > prevH) lo = std::max(lo, s.x0 + react);
    if (prevCeil) lo = std::max(lo, s.x0 + react + U);
    lo = std::max({lo, lastHazX_ + react + 2 * U / epu, lastSawX_ + react + U / epu});
    double x = std::max(lo, nextHazX_);
    x = s.x0 + std::ceil((x - s.x0) / U - 1e-9) * U; // (on a cell's edge: lava is whole cells)
    // what it takes: a laser in the drop's face (its row between the stretch's floor and the drop's top), firing along
    // the stretch: she falls down through it, so it cuts an egg out (she needs one at the edge); lava 1 to 5 cells, an
    // egg a cell
    // (a laser: the drop's face, at the stretch's start, with an egg in hand at the edge and a reaction's run since the
    // last; no drop here: lava instead if any's due, else it waits for one)
    if (laser && (s.h >= prevH || prevCeil || s.x0 < lastHazX_ + react + 2 * U / epu || s.x0 < lastSawX_ + react + U / epu ||
                  (eggBudget_ && budgetAtStart < 1 + kBudgetMargin))) {
        if (lavaPlaced_ >= lavaCount_) return;
        laser = false;
    }
    if (laser) x = s.x0;
    int row = 0, cells = 0, need = 0, cost = 0;
    if (laser) {
        row = s.h + static_cast<int>(std::floor(hazRng_.next() * std::min(2, prevH - s.h)));
        need = cost = 1;
    } else {
        cells = 1 + static_cast<int>(std::floor(hazRng_.next() * 5));
        need = cost = cells;
    }
    if (eggBudget_ && need > 0 && !laser) {
        const double shortBy = need + kBudgetMargin - std::min(kBudgetCap, budgetAtStart + (x - s.x0) / U * epu);
        if (shortBy > 0) x += std::ceil(shortBy / epu - 1e-9) * U;
    }
    const double past = laser ? x : x + cells * U;
    const double end = past + std::max(2.5 * U, c.speed * (laser ? 0.6 : 0.45));
    if (finish_ > 0 && end > finish_ - 8) return;
    if (end > s.x1) { // its flat stretch, lengthened to fit
        s.x1 += std::ceil((end - s.x1) / U - 1e-9) * U;
        genX_ = s.x1;
    }
    if (eggBudget_) {
        const double atIt = std::min(kBudgetCap, budgetAtStart + (x - s.x0) / U * epu);
        budget_ = std::min(kBudgetCap, atIt - cost + (s.x1 - past) / U * epu);
    }
    if (laser) {
        addLaser(s.x0, row, 1);
        ++lasersPlaced_;
    } else {
        lavas.push_back({x, x + cells * U, s.h});
        ++lavaPlaced_;
    }
    lastHazX_ = past;
    nextHazX_ = -1;
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
    if (s.h < prevH) lo = std::max(lo, s.x0 + rules::sawAfterDrop(c.speed, react, prevH - s.h) + kSawReach);
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
        if (h < lastH_) len = std::max(len, rules::landingCells(c.speed, lastH_ - h));
        affordClimb(h);
        pushSeg(len, h, WallKind::Crate);
    }
    // the forecourt: a reaction's run at the door's height, long enough to refill two and a half eggs at the budget's
    // careful rate (a maze's first saws count on two at the slowest refill); a door too low for a full stack ends it
    // under its lintel: a barrier as low as the door, telegraphed as any barrier (a stack too tall for it crashes)
    const int fore = forecourtAt(genX_);
    const bool lintel = rules::doorNeedsLintel(g);
    pushSeg(lintel ? fore - kLintel : fore, g.entryRow(), WallKind::Crate);
    if (lintel) { pushSeg(kLintel, g.entryRow(), WallKind::Crate); segs.back().ceil = g.entryTop(); }
    const double x0 = genX_;
    pushSeg(g.cols(), 0, WallKind::Crate);
    segs.back().gauntlet = true;
    gauntlets.push_back({&g, x0});
    gauntletHazards(gauntlets.back());
    lastSawX_ = std::max(lastSawX_, genX_);
    lastH_ = 0; // (pushSeg: the span's placeholder)
    pushSeg(kPorch, g.exitRow(), WallKind::Crate); // the porch
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
    return floorAt(x, y) * U;
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
                if (Gauntlet::open(k)) { gback.push_back(pos); continue; } // (a beam's cell is air: its laser draws it)
                gcells.push_back({pos, k, uint8_t((p.g->solid(c - 1, r) ? 0 : 1) | (p.g->solid(c + 1, r) ? 0 : 2))});
            }
    for (const PlacedGauntlet& p : gauntlets) inscribe(p);
    for (const PlacedGauntlet& p : zones) // (a zone's bumpers and gates, as a maze's: its walls are the course's)
        for (int c = 0; c < p.g->cols(); ++c)
            for (int r = 0; r < p.g->rows(); ++r) {
                const Gauntlet::Cell k = p.g->at(c, r);
                if (k != Gauntlet::Cell::Bumper && !Gauntlet::gate(k)) continue;
                gcells.push_back({glm::vec3(float(p.x0 + (c + 0.5) * U), float((r + 0.5) * U), 0.f), k,
                                  uint8_t((p.g->solid(c - 1, r) ? 0 : 1) | (p.g->solid(c + 1, r) ? 0 : 2))});
            }
    for (const Segment& s : segs) {
        if (bouncer(s.kind)) continue; // (its cells: the zone's)
        const int cols = jsRound((s.x1 - s.x0) / U);
        for (int i = 0; i < cols; ++i)
            for (int j = 0; j < s.h; ++j) {
                const double x = s.x0 + (i + 0.5) * U, y = (j + 0.5) * U;
                const bool hay = s.kind == WallKind::Hay || (s.kind == WallKind::Mix && j == s.h - 1);
                blocks.push_back({glm::vec3(float(x), float(y), 0.f), hash(x * 3.1 + j * 7.7) >= 0.5f, hay,
                                  0.82f + 0.18f * hash(x * 9.3 + j * 1.9)});
            }
        if (s.top) // a floating block: its own courses, from its underside to its top (a mix block hay on top)
            for (int i = 0; i < cols; ++i)
                for (int j = s.ceil; j < s.top; ++j) {
                    const double x = s.x0 + (i + 0.5) * U, y = (j + 0.5) * U;
                    const bool hay = s.kind == WallKind::Hay || (s.kind == WallKind::Mix && j == s.top - 1);
                    blocks.push_back({glm::vec3(float(x), float(y), 0.f), hash(x * 3.1 + j * 7.7) >= 0.5f, hay,
                                      0.82f + 0.18f * hash(x * 9.3 + j * 1.9)});
                }
    }
    // disco dance floor on every wall top: 2 x 4 light tiles per column
    for (Segment& s : segs) {
        s.capStart = static_cast<int>(tiles.size()); s.capCount = 0;
        const int cols = jsRound((s.x1 - s.x0) / U);
        for (const int h : {s.h, s.top}) { // (a floating block's top is a dance floor too)
            if (h <= 0 || (h == s.h && (s.kind == WallKind::GateR || s.kind == WallKind::GateL))) continue; // (a gate's no floor)
            for (int i = 0; i < cols; ++i)
                for (int a = 0; a < TILE_X; ++a)
                    for (int b = 0; b < TILE_Z; ++b) {
                        const float tx = float(s.x0 + i * U) + (a + 0.5f) * TW, tz = -DEPTH / 2 + (b + 0.5f) * TD;
                        tiles.push_back({{tx, float(h * U) + 0.025f, tz}, jsRound((s.x0 + i * U) / U) * TILE_X + a, b});
                        s.capCount++;
                    }
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
        if (s.top) continue; // (a floating block: its underside's edge, and nothing over it)
        curtains.push_back({{float((s.x0 + s.x1) / 2), cy + 0.7f, 0.f}, float(s.x1 - s.x0)});
    }
}

} // namespace cs
