#include "LevelBeats.h"

#include "BotRun.h"
#include "Campaign.h"
#include "Chunk.h"
#include "CourseRules.h"
#include "Hazards.h"
#include "Level.h"
#include "LevelCatalog.h"
#include "LevelJudge.h"
#include "Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <random>

namespace cs::beats {

const char* verbName(Verb v) {
    static const char* names[] = {"stack", "shed", "duck", "wait", "launch", "bounce"};
    return names[int(v)];
}

const char* roleName(Role r) {
    static const char* names[] = {"teach", "test", "twist", "finale"};
    return names[int(r)];
}

namespace {

constexpr double kSpringSpeeds[] = {0.8, 0.9, 1.0, 1.12, 1.24}; // (hazards/Spring.cpp's: every speed she may run at)

struct Rng {
    Mulberry32 m;
    explicit Rng(uint32_t seed) : m(seed) {}
    double next() { return m.next(); }
    int below(int n) { return std::min(n - 1, static_cast<int>(next() * n)); }
    bool chance(double p) { return next() < p; }
    template <class T> const T& pick(const std::vector<T>& v) { return v[size_t(below(int(v.size())))]; }
};

// ---- tier 1: rhythms

// a rhythm group's beats: spaced by its tempo (regular: even; swing: long-short; random: anything from 0.6 to 1.7 of the
// beat) at its density, each a verb drawn from `verbs` (weighted), the star's weight raised
Rhythm makeRhythm(Rng& r, Tempo tempo, int density, double length, const std::vector<std::pair<Verb, double>>& verbs, int slider) {
    Rhythm out;
    out.tempo = tempo; out.density = density; out.length = length;
    const double beat = beatSpacing(density, slider); // seconds between beats, on average
    double total = 0;
    for (const auto& v : verbs) total += v.second;
    double t = 0.5;
    for (int k = 0; t < length - 0.6; ++k) {
        double w = r.next() * total;
        Verb verb = verbs.front().first;
        for (const auto& v : verbs) { if (w < v.second) { verb = v.first; break; } w -= v.second; }
        out.beats.push_back({t, verb});
        const double step = tempo == Tempo::Regular ? beat : tempo == Tempo::Swing ? beat * (k % 2 ? 0.65 : 1.35) : beat * (0.6 + 1.1 * r.next());
        t += step;
    }
    return out;
}

// the designer's course chunks that cover this level (Chunk.h): each, and flipped if it may be
struct ChunkPool {
    std::vector<const Chunk*> chunks;
    std::vector<Chunk> flipped; // (parallel: a chunk that can't be flipped has an empty one)
    bool empty() const { return chunks.empty(); }
};

// ---- tier 2: geometry

// the course as it's laid: its walls and items, where it's got to, the floor she's on, a careful player's meter (the
// generator's own budget: a climb it can't afford gets a longer run-up), and what's been laid for the critics
struct Builder {
    int n = 0;
    Level probe;          // (its difficulty: speeds, refills, the curve's limits)
    LevelData d;
    double x = 0;
    int h = 0;
    int base = 0;         // the floor of the storey she's on (a wall's height, or a floating floor's top): climbs go up from it
    bool floating = false; // she's on a floating floor: wall() lays blocks (open air and the street under them)
    struct Under { int base; bool floating; };
    std::vector<Under> below; // the storeys under her, a descent lands on the last
    int lastDown = 0;     // the rows the last wall dropped her
    static constexpr int kMaxLaserRow = 14; // (the laser's row field stops at 14)
    static constexpr int kTop = 18; // the highest a floor may be (gauntlet floors go to 15; the camera follows her up)
    int nextCap = 99;     // the tallest the next wall may be (under a barrier: its underside less her body)
    bool afterDrop = false;
    double lastBox = -1e9; // (bounce boxes apart: bouncers within Level::kZoneGap make one zone)
    double budget = Level::kBudgetCap, lastSaw = -1e9, lastLava = -1e9, lastLaser = -1e9; // (the checker spaces each kind from its own last: a mixed cluster packs tighter)
    Rng* r = nullptr;
    double dense = 0;     // the density slider, 0 .. 1
    double m = 1;         // the human margins it keeps (CourseRules.h's marginScale: less from density 6 on)
    const GenOptions* opts = nullptr; // (the hazards switched on)
    bool allows(const char* id) const { return !opts || opts->has(id); }
    std::string pieces;
    double eggsMet = 0, hazardsMet = 0;
    const ChunkPool* pool = nullptr; // (the designer's chunks in range of this level)
    std::vector<int> chunkUses;      // (how many of each the course holds: copied with the builder, so a try that's reverted takes its count)
    std::vector<ChunkUse> placed;

    Curve c() const { return probe.curveAt(x); }
    // the highest a floor or a block's top may be (springs and block stairs climb from wherever she is: this storey's cap
    // and a little over it, never over the top)
    int ceiling() const { return std::min(kTop, base + c().cap + 4); }
    // the highest a climb goes (the curve's cap over the storey she's on)
    int cap() const { return std::min(kTop, base + c().cap); }
    double v() const { return c().speed; }
    double epu() const { const Curve k = c(); return U / (k.speed * k.regen * 1.1); } // (Level::eggsPerUnit's)
    WallKind kind() { const double k = r->next(); return k < 0.4 ? WallKind::Crate : k < 0.7 ? WallKind::Hay : WallKind::Mix; }

    void wall(int cells, int height, WallKind k, int ceil = 0) {
        cells = std::max(1, cells);
        LevelData::Wall w;
        w.cells = cells; w.h = height; w.kind = k; w.ceil = ceil;
        if (floating && !ceil) { w.h = 0; w.ceil = height - 2; w.top = height; } // (a slab two rows thick, a floor she's on top of)
        d.course.push_back(w);
        budget = std::min(Level::kBudgetCap, budget + cells * epu());
        afterDrop = height < h;
        lastDown = std::max(0, h - height);
        x += cells * U; h = height;
        nextCap = ceil && !floating ? ceil - Gauntlet::kBodyRows : 99;
    }
    // a floating block over the floor `floor`, its underside `ceil`, its top `top`; she's on its top (a floor she runs
    // on) if it's no room under it (ceil a row over the floor)
    void block(int cells, int floor, int ceil, int top, WallKind k) {
        cells = std::max(1, cells);
        LevelData::Wall w;
        w.cells = cells; w.h = floor; w.kind = k; w.ceil = ceil; w.top = top;
        d.course.push_back(w);
        budget = std::min(Level::kBudgetCap, budget + cells * epu());
        const bool over = ceil < floor + Gauntlet::kBodyRows;
        afterDrop = (over ? top : floor) < h;
        lastDown = std::max(0, h - (over ? top : floor));
        x += cells * U; h = over ? top : floor;
        nextCap = over ? 99 : ceil - Gauntlet::kBodyRows;
    }
    // a longer run before what's next, so a careful player holds `need` eggs (false: more than a full meter or too long)
    bool affords(double need) {
        need += Level::kBudgetMargin * (m - 0.5) * 2; // (super hard: no egg to spare)
        if (budget >= need) return true;
        if (need > Level::kBudgetCap) return false;
        const int extra = static_cast<int>(std::ceil((need - budget) / epu()));
        if (extra > Level::kMaxRunup) return false;
        LevelData::Wall& last = d.course.back();
        if (bouncer(last.kind) || last.ceil || !last.gauntlet.empty()) wall(extra, h, WallKind::Crate);
        else { last.cells += extra; x += extra * U; budget = std::min(Level::kBudgetCap, budget + extra * epu()); }
        return true;
    }
    void item(const HazardType& t, std::vector<double> v) { d.items.push_back({&t, std::move(v)}); }
    // a piece's run to the next beat: all of it at density 0; denser, only what it needs (the slack cut, so the next
    // beat's piece comes sooner)
    int cellsFor(double metres) const { return std::max(c().minLen, static_cast<int>(std::ceil(metres * (1 - 0.75 * dense) / U))); }
};

// each piece lays its course from where the builder is, up to about `gap` metres on (the run to the next beat), at a
// tier (0 gentle, 1, 2 hard); false (nothing laid) if it can't: it doesn't fit the beats, the meter, the curve
struct Piece {
    char code;
    Verb verb;
    bool (*lay)(Builder& b, double gap, int tier);
    const char* hazard = nullptr; // the registry hazard it needs switched on (null: none)
};

bool climb(Builder& b, double gap, int tier) {
    const Curve c = b.c();
    // (on a floating floor a step of 4 or more is no climb: the next slab's underside would be over her head)
    int up = std::min({1 + tier + b.r->below(2), c.maxUp, b.cap() - b.h, b.nextCap - b.h, b.floating ? 3 : 99});
    if (up < 1) return false;
    if (!b.affords(up)) { up = std::max(1, static_cast<int>(std::floor(b.budget - Level::kBudgetMargin))); if (!b.affords(up)) return false; }
    b.budget -= up;
    b.wall(std::clamp(b.cellsFor(gap), c.minLen, c.maxLen + 6), b.h + up, b.kind());
    b.eggsMet += up;
    return true;
}

bool drop(Builder& b, double gap, int tier) {
    if (b.h <= b.base) return false; // (down to this storey's floor, never under it: a descent leaves a storey)
    const int down = std::min(b.h - b.base, 1 + tier + b.r->below(2));
    const int landing = rules::landingCells(b.v(), down, b.m);
    if (landing * U > gap * 1.4 + 2) return false; // (the timing rule: her fall and landing would run into the next beat)
    b.wall(std::max(landing, b.cellsFor(gap)), b.h - down, b.kind());
    return true;
}

// a drop with a wall laser in its face, firing along the floor she drops to: she falls down through its beam, so it
// cuts an egg out of her stack (one in hand at the edge; Level::placeLaserOrLava's)
bool laserDrop(Builder& b, double gap, int tier) {
    if (!b.allows("laser") || b.floating) return false; // (the checker's egg count for a laser reads the walls, not the floating floors she's up on)
    const Curve c = b.c();
    if (b.h <= b.base || b.nextCap < 99 || b.x - b.lastLaser < c.speed * 0.6 * b.m || !b.affords(1)) return false;
    const int down = std::min(b.h - b.base, 1 + tier + b.r->below(2));
    const int landing = std::max(rules::landingCells(c.speed, down, b.m), static_cast<int>(std::ceil((2.5 * U * b.m + c.speed * 0.6 * b.m) / U)) + 1);
    if (landing * U > gap * 1.4 + 2) return false;
    const double face = b.x;
    const int row = b.h - down + b.r->below(std::min(2, down));
    if (row > Builder::kMaxLaserRow) return false;
    b.wall(std::max(landing, b.cellsFor(gap)), b.h - down, b.kind());
    b.item(kLaserHazard, {std::round(face * 10) / 10, double(row), 1});
    b.budget -= 1;
    b.lastLaser = b.x;
    b.hazardsMet += 1.5;
    return true;
}

bool barrier(Builder& b, double gap, int tier) {
    if (b.afterDrop || b.floating) return false; // (still falling under it; a floating floor's slab has no room for a barrier)
    const Curve c = b.c();
    const int ceil = b.h + Gauntlet::kBodyRows + (tier == 0 ? 2 : 1); // (an egg's room at least: she can't shed one she holds)
    const int cells = std::max({static_cast<int>(std::ceil(c.speed * 0.5 * b.m / U)) + 2, b.cellsFor(gap), 6});
    b.wall(cells, b.h, b.kind(), ceil);
    b.hazardsMet += 1;
    return true;
}

// a flat stretch with what's in `lay` set into it `at` cells on, a reaction's run before (a landing's after a drop)
// and after it
int reactCells(Builder& b) {
    const double react = b.v() * 0.6 * b.m;
    const double before = b.afterDrop ? rules::sawAfterDrop(b.v(), react, std::max(1, b.lastDown)) : react;
    return static_cast<int>(std::ceil(before / U)) + 1;
}

bool saw(Builder& b, double gap, int tier) {
    if (!b.allows("saw")) return false;
    const int blades = tier == 2 ? 2 : 1;
    const Curve c = b.c();
    if (b.x - b.lastSaw < c.speed * c.regen * (0.5 + 0.6 * b.m) * 1.5) return false; // (the meter must have refilled since the last)
    if (!b.affords(b.h > 0 ? blades + 1.0 : blades)) return false;
    const int lead = reactCells(b), row = static_cast<int>(std::ceil(Level::kSawRowGap / U)) * (blades - 1);
    const int after = static_cast<int>(std::ceil(std::max(2.5 * U * b.m, c.speed * 0.45 * b.m) / U)) + 2;
    const double x0 = b.x;
    b.wall(std::max(lead + row + after, b.cellsFor(gap)), b.h, b.kind());
    for (int k = 0; k < blades; ++k) b.item(kSawHazard, {std::round((x0 + (lead + 0.5) * U + k * Level::kSawRowGap) * 10) / 10});
    b.budget -= blades;
    b.lastSaw = b.x;
    b.hazardsMet += blades * 1.5;
    return true;
}

bool lava(Builder& b, double gap, int tier) {
    if (!b.allows("lava")) return false;
    const int cells = 2 + tier;
    const Curve c = b.c();
    if (b.x - b.lastLava < c.speed * c.regen * (0.5 + 0.6 * b.m) * 0.5 + cells * U) return false;
    if (b.nextCap - b.h - Gauntlet::kBodyRows < cells && b.nextCap < 99) return false;
    if (!b.affords(cells)) return false;
    const int lead = reactCells(b), after = static_cast<int>(std::ceil(std::max(2.5 * U * b.m, c.speed * 0.45 * b.m) / U)) + 2;
    const double x0 = b.x;
    b.wall(std::max(lead + cells + after, b.cellsFor(gap)), b.h, b.kind());
    b.item(kLavaHazard, {x0 + lead * U, double(cells)});
    b.budget -= cells;
    b.lastLava = b.x;
    b.hazardsMet += 1 + cells * 0.3;
    return true;
}

// a run-up, a spring, and what it flies her onto (the pad where every speed she may run at lands her on it, no eggs
// needed): a tower (`onto` 0), a slab block (1) or a floating floor (2: a long slab with open air under it, which she
// stays up on: the builder floats from there till a descent); `lavaUnder`: a moat of lava she flies over between them;
// `extra`: a stronger spring (a higher floor)
bool springTo(Builder& b, double gap, int tier, bool lavaUnder, int onto = 0, int extra = 0) {
    if (!b.allows("spring") || (lavaUnder && !b.allows("lava"))) return false;
    if (b.floating && onto != 2) return false; // (from a floating floor, only onto another: a tower would stand on the street)
    const int ontoBlock = onto;
    const int power = 6 + 2 * tier + extra + b.r->below(2);
    // (a block's top a row lower under the arc's peak than a tower's: its underside meets her head before its side
    // would meet her feet, so she has to be over it sooner)
    const int up = std::min({power - (ontoBlock ? 3 : 2), b.c().cap + 2, b.nextCap - b.h, b.ceiling() - b.h});
    if (up < (onto == 2 ? 4 : 2) || b.h + up < (onto == 2 ? 5 : 0) || b.afterDrop) return false;
    const int clear = static_cast<int>(std::ceil(b.v() * 0.45 * b.m / U)) + 2; // (lava ends a reaction's run short of the tower)
    const int run = lavaUnder ? 18 + clear : 14;
    // (the stretch alone, from its start: its difficulty where it is; the tower or block long, its length laid to fit
    // where she lands at the fastest and the landing she needs from there)
    const int longest = 80;
    LevelData t;
    t.number = b.n; t.diffOffset = b.d.diffOffset + b.x * b.d.diffScale; t.diffScale = b.d.diffScale;
    // (onto a block: a slab floating a row over the floor, its top where a tower's would be)
    LevelData::Wall from{run, b.h, WallKind::Crate};
    if (b.floating) from = LevelData::Wall{run, 0, WallKind::Crate, b.h - 2, b.h};
    t.course = {from, onto == 2 ? LevelData::Wall{longest, 0, WallKind::Crate, b.h + up - 2, b.h + up}
                      : onto ? LevelData::Wall{longest, b.h, WallKind::Crate, b.h + 1, b.h + up} : LevelData::Wall{longest, b.h + up, WallKind::Crate},
                {60, b.h + up, WallKind::Crate}};
    t.length = (run + longest + 40) * U;
    Level l;
    l.load(t);
    l.generateUntil(t.length + 20);
    for (int padCell = 4; padCell < run - 3; ++padCell) {
        Spring s;
        s.x = (padCell + 0.5) * U; s.floor = b.h; s.power = power;
        bool ok = true;
        double reach = 0; // (from the tower's start: where she lands at each speed, and the landing she needs after it)
        for (double k : kSpringSpeeds) {
            const rules::Flight f = rules::springFlight(l, s, l.curveAt(s.x).speed * k);
            const int drop = std::max(0, static_cast<int>(std::lround((f.apexY - f.y) / U)));
            if (f.end != rules::Flight::End::Landed || f.floor != b.h + up || f.x < run * U) { ok = false; break; }
            reach = std::max(reach, f.x - run * U + rules::landingRun(l.curveAt(s.x).speed * k, drop, b.m));
        }
        if (!ok) continue;
        const int tower = std::max({onto == 2 ? 14 : 10, b.cellsFor(gap * 0.5), static_cast<int>(std::ceil(reach / U)) + 1});
        const double x0 = b.x;
        b.wall(run, b.h, WallKind::Crate);
        b.item(kSpringHazard, {std::round((x0 + s.x) * 10) / 10, double(power)});
        if (lavaUnder) {
            const int from = padCell + 4, to = run - clear; // (well past the pad, short of the tower)
            if (to - from >= 2) b.item(kLavaHazard, {x0 + from * U, double(std::min(5, to - from))});
        }
        if (onto == 2) { // (a new storey: the one she's on is under it)
            b.below.push_back({b.base, b.floating});
            b.floating = true;
            b.wall(tower, b.h + up, b.kind());
            b.base = b.h;
        } else if (ontoBlock) b.block(tower, b.h, b.h + 1, b.h + up, b.kind());
        else b.wall(tower, b.h + up, b.kind());
        b.budget = std::min(Level::kBudgetCap, b.budget); // (no eggs: she flies up)
        b.hazardsMet += 1.2 + (lavaUnder ? 0.8 : 0);
        return true;
    }
    return false;
}
bool spring(Builder& b, double gap, int tier) { return springTo(b, gap, tier, false); }
bool springOverLava(Builder& b, double gap, int tier) { return springTo(b, gap, tier, true); }

// ---- set pieces: clusters, block and barrier staircases, weaves (Compton and Mateas's complex and composite
// patterns, sized by the density slider: k from 2 at 0 to 6 at 10, as many as the meter allows)
int clusterSize(const Builder& b, int tier) { // (denser: 5 at density 6, 9 at 10; the hard tier one more)
    return 2 + static_cast<int>(std::lround(4 * b.dense)) + static_cast<int>(std::lround(6 * std::max(0.0, b.dense - 0.5))) + (tier == 2);
}

// a staircase of floating blocks rising a row each from `top` over the floor `floor`, each a slab a row over it (no room
// under: she climbs from block top to block top), then down to the floor; false if it couldn't lay one
bool blocksUp(Builder& b, int floor, int top, int count, double gap) {
    const Curve c = b.c();
    int laid = 0;
    for (int k = 0; k < count; ++k, ++top) {
        if (top > b.ceiling() || top - b.h > c.maxUp + 1) break;
        const int up = top - b.h;
        if (up > 0 && !b.affords(up)) break;
        if (up > 0) { b.budget -= up; b.eggsMet += up; }
        b.block(std::max(c.minLen + 1, b.cellsFor(gap / std::max(1, count))), floor, floor + 1, top, b.kind());
        ++laid;
    }
    if (!laid) return false;
    b.wall(std::max(rules::landingCells(b.v(), b.h - floor, b.m), b.cellsFor(gap * 0.2)), floor, b.kind());
    b.hazardsMet += laid;
    return true;
}

// a block staircase from the floor she's on
bool blockStairs(Builder& b, double gap, int tier) {
    if (b.nextCap < 99 || b.afterDrop || b.floating) return false;
    return blocksUp(b, b.h, b.h + 2, clusterSize(b, tier), gap);
}

// a spring onto a floating block, and (denser) on up a block staircase from there
bool springBlocks(Builder& b, double gap, int tier) {
    if (b.floating) return false;
    const int floor = b.h;
    if (!springTo(b, gap * 0.5, tier, false, true)) return false;
    const int more = clusterSize(b, tier) - 2;
    if (more > 0) blocksUp(b, floor, b.h + 1, more, gap * 0.5);
    else b.wall(std::max(rules::landingCells(b.v(), b.h - floor, b.m), 4), floor, b.kind());
    return true;
}

// a barrier staircase: the floor stepping up a row under one ceiling (the room under it shrinking: an egg's at the
// last), an egg each step
bool barrierStairs(Builder& b, double gap, int tier) {
    if (b.afterDrop || b.nextCap < 99 || b.floating) return false;
    const Curve c = b.c();
    const int steps = std::min(clusterSize(b, tier), b.cap() - b.h + 1);
    if (steps < 2) return false;
    const int ceil = b.h + Gauntlet::kBodyRows + 1 + (steps - 1), len = std::max(static_cast<int>(std::ceil(c.speed * 0.5 * b.m / U)) + 2, 4);
    const int h0 = b.h;
    for (int k = 0; k < steps; ++k) {
        const int up = (h0 + k) - b.h;
        if (up > 0 && !b.affords(up)) return k >= 2;
        if (up > 0) { b.budget -= up; b.eggsMet += up; }
        b.wall(k + 1 < steps ? len : std::max(len, b.cellsFor(gap * 0.3)), h0 + k, b.kind(), ceil);
        b.hazardsMet += 0.8;
    }
    return true;
}

// a weave: over a block, under a block, over a block (each a climb of 2, the one under it a drop and a duck)
bool weave(Builder& b, double gap, int tier) {
    if (b.afterDrop || b.nextCap < 99 || b.floating) return false;
    const Curve c = b.c();
    const int f = b.h, passes = 3 + (clusterSize(b, tier) > 4 ? 2 : 0);
    if (f + 6 + (tier == 2) > b.ceiling() + 2) return false;
    const int len = std::max(c.minLen + 1, 5), under = std::max(rules::landingCells(c.speed, 2, b.m), static_cast<int>(std::ceil(c.speed * 0.5 * b.m / U)) + 2);
    for (int k = 0; k < passes; ++k) {
        if (k % 2 == 0) { // over: a slab a row over the floor, its top 2 up
            if (!b.affords(2)) return k >= 3;
            b.budget -= 2; b.eggsMet += 2;
            b.block(len, f, f + 1, f + 2, b.kind());
        } else { // under: a block 4 over the floor (her body and 2 eggs under it), down off the last block onto the floor
            b.block(under, f, f + 4, f + 6 + (tier == 2), b.kind());
        }
        b.hazardsMet += 0.8;
    }
    b.wall(std::max(rules::landingCells(c.speed, 2, b.m), b.cellsFor(gap * 0.2)), f, b.kind());
    return true;
}

// a pocket: a block low over the floor (her body and an egg under it), and a saw or lava just past it
bool pocket(Builder& b, double gap, int tier) {
    if (b.afterDrop || b.nextCap < 99 || b.floating) return false;
    const Curve c = b.c();
    b.block(std::max(static_cast<int>(std::ceil(c.speed * 0.5 * b.m / U)) + 2, 5), b.h, b.h + Gauntlet::kBodyRows + 1, b.h + Gauntlet::kBodyRows + 3, b.kind());
    b.hazardsMet += 1;
    return b.r->chance(0.5) ? (saw(b, gap * 0.5, tier) || lava(b, gap * 0.5, 0)) : (lava(b, gap * 0.5, 0) || saw(b, gap * 0.5, tier));
}

// one hazard again and again: a row of saws back to back
bool sawRow(Builder& b, double gap, int tier) {
    if (!b.allows("saw")) return false;
    const Curve c = b.c();
    if (b.x - b.lastSaw < c.speed * c.regen * (0.5 + 0.6 * b.m) * 1.5) return false;
    int blades = std::min(8, clusterSize(b, tier));
    while (blades > 2 && !b.affords(blades + (b.h > 0 ? 1.0 : 0.0))) --blades;
    if (!b.affords(blades + (b.h > 0 ? 1.0 : 0.0))) return false;
    const int lead = reactCells(b), row = static_cast<int>(std::ceil(Level::kSawRowGap / U)) * (blades - 1);
    const int after = static_cast<int>(std::ceil(std::max(2.5 * U * b.m, c.speed * 0.45 * b.m) / U)) + 2;
    const double x0 = b.x;
    b.wall(std::max(lead + row + after, b.cellsFor(gap)), b.h, b.kind());
    for (int k = 0; k < blades; ++k) b.item(kSawHazard, {std::round((x0 + (lead + 0.5) * U + k * Level::kSawRowGap) * 10) / 10});
    b.budget -= blades;
    b.lastSaw = b.x;
    b.hazardsMet += blades * 1.5;
    return true;
}

// ...pools of lava one after another on one floor, a few cells of floor between them
bool lavaPools(Builder& b, double gap, int tier) {
    if (!b.allows("lava")) return false;
    const Curve c = b.c();
    const int cells = 2, between = static_cast<int>(std::ceil(c.speed * c.regen * (0.5 + 0.6 * b.m) * 0.5 / U)) + 1;
    if (b.x - b.lastLava < c.speed * c.regen * (0.5 + 0.6 * b.m) * 0.5 + cells * U) return false;
    if (b.nextCap < 99 && b.nextCap - b.h - Gauntlet::kBodyRows < cells) return false;
    int pools = std::min(6, clusterSize(b, tier));
    while (pools > 2 && !b.affords(pools * cells)) --pools;
    if (!b.affords(pools * cells)) return false;
    const int lead = reactCells(b), after = static_cast<int>(std::ceil(std::max(2.5 * U * b.m, c.speed * 0.45 * b.m) / U)) + 2;
    const double x0 = b.x;
    b.wall(std::max(lead + pools * cells + (pools - 1) * between + after, b.cellsFor(gap)), b.h, b.kind());
    for (int k = 0; k < pools; ++k) b.item(kLavaHazard, {x0 + (lead + k * (cells + between)) * U, double(cells)});
    b.budget -= pools * cells;
    b.lastLava = b.x;
    b.hazardsMet += pools * (1 + cells * 0.3);
    return true;
}

// ...a staircase down with a wall laser in every step's face
bool laserStairs(Builder& b, double gap, int tier) {
    if (!b.allows("laser") || b.nextCap < 99 || b.floating) return false;
    const Curve c = b.c();
    int steps = std::min(clusterSize(b, tier), b.h - b.base);
    if (steps < 2 || b.h - 1 > Builder::kMaxLaserRow) return false;
    while (steps > 2 && !b.affords(steps)) --steps;
    if (!b.affords(steps)) return false;
    const int landing = std::max(rules::landingCells(c.speed, 1, b.m), static_cast<int>(std::ceil((2.5 * U * b.m + c.speed * 0.6 * b.m) / U)) + 1);
    for (int k = 0; k < steps; ++k) {
        const double face = b.x;
        b.wall(k + 1 < steps ? landing : std::max(landing, b.cellsFor(gap * 0.3)), b.h - 1, b.kind());
        b.item(kLaserHazard, {std::round(face * 10) / 10, double(b.h), 1});
    }
    b.budget -= steps;
    b.lastLaser = b.x;
    b.hazardsMet += steps * 1.5;
    return true;
}

// a mixed cluster: hazards of different kinds back to back (the checker spaces each kind from its own last: mixed,
// they pack tighter than one kind repeated)
bool sawOnTop(Builder& b, double gap, int tier);
bool mixed(Builder& b, double, int tier) {
    using Lay = bool (*)(Builder&, double, int);
    std::vector<std::pair<char, Lay>> kinds = {{'s', saw}, {'l', lava}, {'z', laserDrop}, {'k', sawOnTop}, {'p', pocket}};
    const int want = std::min(8, clusterSize(b, tier));
    int laid = 0;
    char last = 0;
    for (int tries = 0; laid < want && tries < want * 4; ++tries) {
        const auto& [code, fn] = kinds[size_t(b.r->below(int(kinds.size())))];
        if (code == last) continue;
        const Builder saved = b;
        if (fn(b, 0, tier)) { ++laid; last = code; } else b = saved;
    }
    return laid >= 2;
}

// a bounce box on the street: a gate she runs through, a run, a bumper she bounces off till she's stacked over it,
// out onto a plateau as high as the bumper
bool bounceBox(Builder& b, double gap, int tier) {
    if (b.x - b.lastBox < (Level::kZoneGap + 4) * U || b.base > 0 || b.floating) return false; // (its zone stands on the street: not up a storey)
    if (b.h > 0) b.wall(rules::landingCells(b.v(), b.h, b.m) + 2, 0, WallKind::Crate); // (down to the street first)
    else if (b.afterDrop) b.wall(4, 0, WallKind::Crate);
    const int high = std::min(3 + tier, b.c().cap), run = 6 + 2 * tier + b.r->below(3);
    if (!b.affords(std::min(high, 4))) return false;
    b.wall(1, 3, WallKind::GateR);
    // deadly (more often, denser): a step inside the box, with a saw or lava on its top or a laser in its face (she
    // bounces back into them), or bare: its far face, met running back off the bumper, crashes her unless she's stacked
    // over it
    const int deadly = b.r->chance(0.35 + 0.6 * b.dense) ? 1 + b.r->below(4) : 0;
    if (deadly) {
        const int lead = 4 + b.r->below(3), step = deadly == 1 || deadly == 2 ? 9 : 5, after = 6 + 2 * tier;
        b.wall(lead, 0, WallKind::Crate);
        const double s0 = b.x;
        b.wall(step, 2, WallKind::Crate);
        if (deadly == 1 && b.allows("saw")) b.item(kSawHazard, {std::round((s0 + 4.5 * U) * 10) / 10});
        if (deadly == 2 && b.allows("lava")) b.item(kLavaHazard, {s0 + 3 * U, 2});
        if (deadly == 3 && b.allows("laser")) b.item(kLaserHazard, {std::round(b.x * 10) / 10, 1, 1}); // (its far face, firing on at the bumper)
        b.wall(after, 0, WallKind::Crate);
        b.hazardsMet += 2;
    } else b.wall(run, 0, WallKind::Crate);
    b.wall(2, high, WallKind::Bumper);
    b.lastBox = b.x;
    b.h = high;
    b.wall(std::max(8, b.cellsFor(gap * 0.5)), high, b.kind());
    b.budget = std::max(0.0, b.budget - high + run * b.epu() * 2); // (she bounces back and forth while the meter refills)
    b.eggsMet += high;
    b.hazardsMet += 2;
    return true;
}

// composite: a climb with a barrier over it (she stacks for the wall with a ceiling over her: the classic generator's
// favourite), an egg's room under it at least
bool climbUnder(Builder& b, double gap, int tier) {
    if (b.afterDrop || b.nextCap < 99 || b.floating) return false;
    const Curve c = b.c();
    const int up = std::min({1 + (tier > 0), c.maxUp, b.cap() - b.h});
    if (up < 1 || !b.affords(up)) return false;
    b.budget -= up;
    const int ceil = b.h + up + Gauntlet::kBodyRows + (tier == 2 ? 1 : 2);
    b.wall(std::max({static_cast<int>(std::ceil(c.speed * 0.5 * b.m / U)) + 2, b.cellsFor(gap), c.minLen}), b.h + up, b.kind(), ceil);
    b.eggsMet += up;
    b.hazardsMet += 1;
    return true;
}

// complex: three climbs of the same height (a staircase)
bool staircase(Builder& b, double gap, int tier) {
    const int up = 1 + (tier > 0);
    for (int k = 0; k < 3; ++k) {
        if (b.h + up > std::min(b.cap(), b.nextCap) || !b.affords(up)) return k > 0;
        b.budget -= up;
        b.wall(std::max(b.c().minLen + 1, b.cellsFor(gap / 3)), b.h + up, b.kind());
        b.eggsMet += up;
    }
    return true;
}

// composite: a climb with a saw on its top (stack for both)
bool sawOnTop(Builder& b, double gap, int tier) {
    if (!climb(b, gap * 0.4, tier)) return false;
    return saw(b, gap * 0.6, 0);
}

// Tanagra's valley (a drop, a hazard, a climb back) and mesa (a climb, a hazard, a drop)
bool valley(Builder& b, double gap, int tier) {
    if (!drop(b, gap / 3, tier)) return false;
    if (!(b.r->chance(0.5) ? lava(b, gap / 3, 0) : saw(b, gap / 3, 0)) && !saw(b, gap / 3, 0) && !lava(b, gap / 3, 0)) b.wall(b.cellsFor(gap / 3), b.h, b.kind());
    climb(b, gap / 3, tier);
    return true;
}
bool mesa(Builder& b, double gap, int tier) {
    if (!climb(b, gap / 3, tier)) return false;
    if (!saw(b, gap / 3, 0)) barrier(b, gap / 3, tier);
    drop(b, gap / 3, tier);
    return true;
}

// ---- storeys: the floor path's pieces (laid when the path asks for a move, not drawn from a beat's verb)

// ascent: a staircase of climbs up 4 to 6 rows, each step with the run-up the meter can afford; the storey she's on
// is the new floor, the one under it kept for the descent
bool ascent(Builder& b, double gap, int tier) {
    (void)tier;
    const Curve c = b.c();
    const int want = 4 + b.r->below(3), stepMax = std::min(c.maxUp, b.floating ? 3 : 99);
    const int steps = std::max(2, (want + stepMax - 1) / stepMax);
    int gained = 0;
    for (int guard = 0; gained < want && guard < 8; ++guard) {
        int up = std::min({stepMax, want - gained, Builder::kTop - b.h, b.nextCap - b.h});
        if (up < 1) break;
        if (!b.affords(up)) { up = std::min(up, static_cast<int>(std::floor(b.budget - Level::kBudgetMargin))); if (up < 1 || !b.affords(up)) break; }
        b.budget -= up;
        b.wall(std::max(c.minLen + 1, b.cellsFor(gap / steps)), b.h + up, b.kind());
        b.eggsMet += up;
        gained += up;
    }
    if (gained < 3) return false;
    b.below.push_back({b.base, b.floating});
    b.base = b.h;
    b.hazardsMet += 1;
    return true;
}

// a spring onto a floating floor, from the street or a storey (F), or from a floating floor or a storey onto a higher
// one (G)
bool floatTo(Builder& b, double gap, int) {
    if (b.floating || b.h > Builder::kTop - 6) return false;
    return springTo(b, gap, 0, false, 2, 1);
}
bool floatUp(Builder& b, double gap, int) {
    if (!b.floating && b.base == 0) return false;
    if (b.h > Builder::kTop - 7) return false;
    return springTo(b, gap, 0, false, 2, 2);
}

// descent: back down a storey, by a staircase of drops (some with a wall laser in a face), or, off a floating floor
// onto a storey under it, in one long fall with the landing the margins need
bool descent(Builder& b, double gap, int tier) {
    if (b.below.empty()) return false;
    const Builder::Under to = b.below.back();
    const Curve c = b.c();
    if (b.h - to.base < 3) return false;
    if (b.floating && !to.floating) { // (off the end: one fall to the street or the storey under it)
        const int down = b.h - to.base;
        b.floating = false;
        b.wall(std::max({rules::landingCells(c.speed, down, b.m), b.cellsFor(gap), 6}), to.base, b.kind());
        b.base = to.base; b.floating = to.floating;
        b.below.pop_back();
        b.hazardsMet += 1;
        return true;
    }
    for (int guard = 0; b.h > to.base && guard < 10; ++guard) {
        const int left = b.h - to.base;
        const int down = std::min(left, 1 + b.r->below(3));
        int landing = rules::landingCells(c.speed, down, b.m);
        const int lasered = std::max(landing, static_cast<int>(std::ceil((2.5 * U * b.m + c.speed * 0.6 * b.m) / U)) + 1);
        // (a laser in the face of the step: it cuts an egg out of her stack as she falls through its beam)
        const bool laser = guard < 9 && !b.floating && b.h - 1 <= Builder::kMaxLaserRow && b.allows("laser") && tier > 0 && b.nextCap == 99 && b.x - b.lastLaser >= c.speed * 0.6 * b.m &&
                           b.r->chance(0.35) && b.affords(1);
        const double face = b.x;
        const int row = b.h - down + b.r->below(std::min(2, down));
        if (laser) landing = lasered;
        if (guard == 9) b.wall(std::max(landing, 6), to.base, b.kind()); // (the rest in one fall)
        else b.wall(std::max(landing, left == down ? b.cellsFor(gap * 0.3) : 3), b.h - down, b.kind());
        if (laser) {
            b.item(kLaserHazard, {std::round(face * 10) / 10, double(row), 1});
            b.budget -= 1;
            b.lastLaser = b.x;
            b.hazardsMet += 1.5;
        }
    }
    b.base = to.base;
    b.below.pop_back();
    b.hazardsMet += 1;
    return b.h == to.base;
}

// ---- the designer's chunks (Chunk.h): a stretch of course stamped whole

// a chunk as it's laid: its walls between the pads, heights from the run-in's floor, its hazards from the stretch's start
struct Cut {
    std::vector<LevelData::Wall> walls;
    std::vector<Placed> items;
    int exit = 0;          // the run-out's height from the entry
    int tail = 0;          // the run-out's cells kept (a landing, the room after its last hazard)
    WallKind tailKind = WallKind::Crate;
    int minH = 0, maxH = 0; // the lowest and highest floor in it (a block's top too), from the entry
    bool bounces = false;  // a bumper or a gate: its zone stands on the street
    double length = 0;     // metres, the tail with it
};

// a wall's floor: the row she runs at on it (a block with no room under it: its top)
int floorOf(const LevelData::Wall& w) { return w.top && w.ceil < w.h + Gauntlet::kBodyRows ? w.top : w.h; }

std::optional<Cut> cutOf(const Builder& b, const Chunk& c) {
    const auto& cs = c.body.course;
    Cut t;
    const int entry = c.entryHeight();
    t.exit = c.exitHeight() - entry;
    t.tailKind = cs.back().kind;
    t.walls.assign(cs.begin() + 1, cs.end() - 1);
    int cells = 0, floor = 0;
    for (LevelData::Wall& w : t.walls) {
        cells += w.cells;
        w.h -= entry;
        if (w.ceil) w.ceil -= entry;
        if (w.top) w.top -= entry;
        t.bounces = t.bounces || bouncer(w.kind);
        t.minH = std::min(t.minH, w.h);
        t.maxH = std::max({t.maxH, w.h, w.top});
        floor = floorOf(w);
    }
    const double xs = c.runIn() * U;
    for (const Placed& p : c.body.items) {
        Placed q = p;
        q.v[0] = std::round((p.x() - xs) * 1000) / 1000;
        if (p.type->anchor == Anchor::Face && q.v.size() > 1) q.v[1] -= entry;
        else if (p.type->anchor == Anchor::Free && q.v.size() > 1) q.v[1] = std::round((q.v[1] - entry * U) * 1000) / 1000;
        t.items.push_back(std::move(q));
    }
    // (the run-out kept as far as a drop to it needs the landing, or a step up to it a cell: a hazard's room after it is
    // its own wall's, the author's, as the checker measures it; the pads were the next wall's)
    const double speed = b.v();
    int tail = 0;
    if (floor != t.exit) tail = floor > t.exit ? rules::landingCells(speed, floor - t.exit, b.m) : 1;
    t.tail = tail;
    t.length = (cells + t.tail) * U;
    return t;
}

// a careful player's meter along the stretch from `start` eggs: false if it runs dry; *end is what's left
bool meterAlong(const Builder& b, const Cut& t, double start, double* end) {
    struct Ev { double x, cost; };
    std::vector<Ev> evs;
    double x = 0;
    int was = 0;
    for (const LevelData::Wall& w : t.walls) {
        if (floorOf(w) > was) evs.push_back({x, double(floorOf(w) - was)});
        was = floorOf(w);
        x += w.cells * U;
    }
    for (const Placed& p : t.items) {
        if (p.type == &kSawHazard || p.type == &kLaserHazard) evs.push_back({p.x(), 1});
        else if (p.type == &kLavaHazard) evs.push_back({p.x(), p.at(1)});
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& c) { return a.x < c.x; });
    double have = start, at = 0;
    const double perMetre = b.epu() / U;
    for (const Ev& e : evs) {
        have = std::min(Level::kBudgetCap, have + (e.x - at) * perMetre);
        at = e.x;
        have -= e.cost;
        if (have < 0) return false;
    }
    have = std::min(Level::kBudgetCap, have + (t.length - at) * perMetre);
    *end = have;
    return true;
}

// stamp chunk `ci` (flipped, if it may be and the dice say so) here; false (the builder as it was, for the caller to
// restore) if it doesn't fit where she is
bool chunkPiece(Builder& b, size_t ci) {
    const ChunkPool& pool = *b.pool;
    const Chunk& orig = *pool.chunks[ci];
    if (b.floating || (orig.uses && b.chunkUses[ci] >= orig.uses)) return false;
    for (const Placed& p : orig.body.items) if (!b.allows(p.type->id)) return false;
    const bool flip = orig.mirrors() && b.r->chance(0.5);
    const Chunk& c = flip ? pool.flipped[ci] : orig;
    const std::optional<Cut> cut = cutOf(b, c);
    if (!cut || cut->walls.empty()) return false;
    const Cut& t = *cut;
    // (the seams: under the storey's cap and the 18-row top, above its floor, under any barrier just before)
    if (b.h + t.minH < b.base || b.h + t.maxH > b.ceiling() || b.h + t.walls.front().h > b.nextCap) return false;
    if (t.bounces && (b.base > 0 || b.x - b.lastBox < (Level::kZoneGap + 4) * U)) return false;
    // (a reaction's run, or the landing of the drop before, ahead of the first thing in it)
    const LevelData::Wall& first = t.walls.front();
    double firstEvent = first.ceil || first.top || first.h != 0 || bouncer(first.kind) ? 0.0 : first.cells * U;
    for (const Placed& p : t.items) firstEvent = std::min(firstEvent, p.x());
    const int lead = std::max(0, reactCells(b) - static_cast<int>(std::floor(firstEvent / U + 1e-6)));
    if (b.x + lead * U + t.length > b.d.length - 20) return false;
    if (lead > 0) b.wall(lead, b.h, WallKind::Crate);
    // (a climb at the start, or the stretch's own climbs and hazards: a run-up she can afford)
    double need = 0, end = 0;
    while (need <= Level::kBudgetCap && !meterAlong(b, t, need, &end)) need += 0.25;
    if (need > Level::kBudgetCap || !b.affords(need)) return false;
    const double budget0 = b.budget;
    if (!meterAlong(b, t, budget0, &end)) return false;
    const int delta = b.h; // (a cut's entry is 0)
    // (every hazard's numbers shifted up with it, in range)
    std::vector<Placed> items;
    const double x0 = b.x;
    for (const Placed& p : t.items) {
        Placed q = p;
        q.v[0] = std::round((x0 + p.x()) * 1000) / 1000;
        if (p.type->anchor == Anchor::Face && q.v.size() > 1) q.v[1] += delta;
        else if (p.type->anchor == Anchor::Free && q.v.size() > 1) q.v[1] = std::round((q.v[1] + delta * U) * 1000) / 1000;
        for (size_t k = 0; k < q.v.size() && k < p.type->params.size(); ++k)
            if (q.v[k] < p.type->params[k].min || q.v[k] > p.type->params[k].max) return false;
        items.push_back(std::move(q));
    }
    double lastSaw = b.lastSaw, lastLava = b.lastLava, lastLaser = b.lastLaser;
    for (const LevelData::Wall& w : t.walls) {
        if (w.top) b.block(w.cells, w.h + delta, w.ceil + delta, w.top + delta, w.kind);
        else b.wall(w.cells, w.h + delta, w.kind, w.ceil ? w.ceil + delta : 0);
    }
    if (t.tail > 0) b.wall(t.tail, t.exit + delta, t.tailKind); // (else the last wall's own height stands: it's the exit)
    for (Placed& q : items) {
        if (q.type == &kSawHazard) { lastSaw = b.x; b.hazardsMet += 1.5; }
        else if (q.type == &kLavaHazard) { lastLava = b.x; b.hazardsMet += 1 + q.at(1) * 0.3; }
        else if (q.type == &kLaserHazard) { lastLaser = b.x; b.hazardsMet += 1.5; }
        else if (q.type == &kSpringHazard) b.hazardsMet += 1.2;
        b.d.items.push_back(std::move(q));
    }
    int was = 0;
    for (const LevelData::Wall& w : t.walls) {
        if (floorOf(w) > was) b.eggsMet += floorOf(w) - was;
        if (w.ceil && !w.top) b.hazardsMet += 1;
        was = floorOf(w);
    }
    b.lastSaw = lastSaw; b.lastLava = lastLava; b.lastLaser = lastLaser;
    if (t.bounces) b.lastBox = b.x;
    b.budget = end;
    ++b.chunkUses[ci];
    b.placed.push_back({orig.id, x0, b.x, flip});
    return true;
}

const std::vector<Piece>& grammar() {
    static const std::vector<Piece> g = {
        {'c', Verb::Stack, climb}, {'t', Verb::Stack, staircase}, {'k', Verb::Stack, sawOnTop, "saw"}, {'u', Verb::Stack, climbUnder},
        {'d', Verb::Shed, drop}, {'v', Verb::Shed, valley}, {'m', Verb::Shed, mesa}, {'z', Verb::Shed, laserDrop, "laser"},
        {'b', Verb::Duck, barrier},
        {'s', Verb::Wait, saw, "saw"}, {'l', Verb::Wait, lava, "lava"},
        {'j', Verb::Launch, spring, "spring"}, {'o', Verb::Launch, springOverLava, "spring"},
        {'x', Verb::Bounce, bounceBox},
        // the set pieces (their weight up with the density slider)
        {'K', Verb::Stack, blockStairs}, {'J', Verb::Launch, springBlocks, "spring"},
        {'B', Verb::Duck, barrierStairs}, {'W', Verb::Duck, weave}, {'P', Verb::Duck, pocket},
        {'X', Verb::Wait, mixed}, {'S', Verb::Wait, sawRow, "saw"}, {'L', Verb::Wait, lavaPools, "lava"},
        {'Z', Verb::Shed, laserStairs, "laser"},
        // the storeys (laid where the floor path asks, not by a beat's verb): ascents, springs onto floating floors,
        // descents
        {'A', Verb::Stack, ascent}, {'F', Verb::Launch, floatTo, "spring"}, {'G', Verb::Launch, floatUp, "spring"}, {'D', Verb::Shed, descent},
    };
    return g;
}
bool storeyPiece(char p) { return p == 'A' || p == 'F' || p == 'G' || p == 'D'; }
bool composite(char p) { return p == 'k' || p == 'v' || p == 'm' || p == 'o' || p == 't' || p == 'u' || (p >= 'A' && p <= 'Z'); }
bool setPiece(char p) { return p >= 'A' && p <= 'Z'; }

// a rest: a flat breather at her height with disco balls rising over it (if they're on)
void rest(Builder& b, int cells, bool discos) {
    const double x0 = b.x;
    b.wall(std::max(cells, b.afterDrop ? rules::landingCells(b.v(), 1, b.m) : 0), b.h, WallKind::Crate);
    for (int k = 0; k < (discos ? 3 : 0); ++k) b.item(kDiscoHazard, {std::round((x0 + (2.5 + k * 3) * U) * 10) / 10, std::round(((b.h + 2 + k) * U + 0.25) * 100) / 100});
}

// ---- the level

struct Section { Role role; double seconds; int density; Tempo tempo; };

std::vector<Section> arc(Rng& r, double length, double speed, double dense) {
    const double seconds = (length - 40) / speed; // (less the start and the rests)
    std::vector<Section> out;
    out.push_back({Role::Teach, 5, 0, Tempo::Regular});
    double used = 5 + 2;
    int k = 0;
    while (used < seconds * 0.82 || (dense > 0 && k < 60)) { // (denser: more than it may need; lay stops at the finale's place)
        const Role role = k % 3 == 2 ? Role::Twist : Role::Test;
        const double len = (r.chance(0.5) ? 10 : 5) + (dense > 0 ? 2.5 * std::round(dense * 2) : 0.0); // (denser: longer groups, by 5 s at 10)
        out.push_back({role, len, std::min(2, 1 + (k >= 3)), Tempo(r.below(3))});
        used += len + 2;
        ++k;
    }
    out.push_back({Role::Finale, 10, 2, r.chance(0.5) ? Tempo::Swing : Tempo::Regular});
    return out;
}

std::vector<Verb> available(const GenOptions& o) {
    std::vector<Verb> v = {Verb::Stack, Verb::Shed, Verb::Duck, Verb::Bounce};
    if (o.has("saw") || o.has("lava")) v.push_back(Verb::Wait);
    if (o.has("spring")) v.push_back(Verb::Launch);
    return v;
}

// the level's floor path: its storey moves (+1 up, -1 down), chosen from its seed before anything is laid. Linear (about
// 40%): a steady ramp up and down, a single peak, or a long climb that ends high. Else a random walk, each move up or
// down at random, never under the street or over the third storey, never the same move more than four times in a
// row; about 6 moves at level 31, up to 13 at 100
struct FloorPath {
    std::vector<int> moves;
    bool linear = false;
};
FloorPath makePath(Rng& r, int n, bool linear) {
    constexpr int kStoreys = 3;
    FloorPath p;
    if (linear) {
        p.linear = true;
        const int shape = r.below(3), k = shape == 0 ? 3 : shape == 1 ? 1 + r.below(2) : 2 + r.below(2);
        const int downs = shape == 0 ? 2 + r.below(2) : shape == 1 ? k : 0;
        p.moves.assign(size_t(k), +1);
        p.moves.insert(p.moves.end(), size_t(downs), -1);
        return p;
    }
    const int count = std::clamp(static_cast<int>(std::lround(6 + (n - 31) * 7.0 / 69.0)), 6, 13);
    int at = 0, run = 0, last = 0;
    for (int k = 0; k < count; ++k) {
        int move = at == 0 ? +1 : at == kStoreys ? -1 : r.chance(0.5) ? +1 : -1;
        if (move == last && run >= 4) move = -move;
        run = move == last ? run + 1 : 1;
        last = move;
        at += move;
        p.moves.push_back(move);
    }
    return p;
}

// the move the floor path asks for, as the piece that makes it (0: it couldn't, the builder as it was)
char layMove(Builder& b, int move, double gap, int tier, const GenOptions& opts) {
    const Builder saved = b;
    auto tryPiece = [&](char code, bool (*fn)(Builder&, double, int)) {
        if (fn(b, gap, tier)) return code;
        b = saved;
        return char(0);
    };
    if (move < 0) return tryPiece('D', descent);
    const bool springs = opts.has("spring");
    // (a floating floor or a storey: a spring up to a higher floating floor about half the time; the street: onto one)
    char got = 0;
    if (springs && b.r->chance(0.5)) got = tryPiece('G', floatUp);
    if (!got && springs && !b.floating && b.r->chance(0.5)) got = tryPiece('F', floatTo);
    if (!got) got = tryPiece('A', ascent);
    if (!got && springs) got = tryPiece('F', floatTo);
    return got;
}

// one candidate: the arc's sections laid from their rhythms, each beat its piece (the star's where it can be)
std::optional<Plan> lay(int n, Rng& rr, Rng& rg, Verb star, const GenOptions& opts, int density, bool linear, const ChunkPool* chunks) {
    Builder b;
    b.n = n; b.r = &rg; b.opts = &opts;
    if (chunks && !chunks->empty()) { b.pool = chunks; b.chunkUses.assign(chunks->chunks.size(), 0); } b.dense = std::clamp(density, 0, 10) / 10.0;
    b.d.hard = density >= 6 ? density : 0; // (super hard: the checker judges it with the margins it's laid with)
    b.m = rules::marginScale(b.d.hard);
    b.probe.setDifficulty(35.0 * (n - 1), 0.35);
    b.d.number = n;
    b.d.length = 300.0 + 10.0 * (n - 1);
    b.d.diffOffset = 35.0 * (n - 1);
    b.d.diffScale = 0.35;
    b.d.raids = -1;
    b.wall(16, 0, WallKind::Crate);
    Plan plan;
    plan.star = star;
    const FloorPath path = makePath(rg, n, linear);
    for (int m : path.moves) plan.floors += m > 0 ? 'u' : 'd';
    plan.linearFloors = path.linear;
    std::vector<double> moveX; // (where each move is due: spread along the level from after the teach group)
    size_t nextMove = 0;
    int fails = 0;
    const std::vector<Verb> verbs = available(opts);
    const std::vector<Section> sections = arc(rr, b.d.length, b.v(), b.dense);
    // (the finale's place: its seconds' run before the end, less as the pieces shorten)
    const double finaleAt = b.d.length - 40 - (b.dense > 0 ? sections.back().seconds * b.v() * (1 - 0.6 * b.dense) : 0.0);
    for (size_t si = 0; si < sections.size(); ++si) {
        const Section& sec = sections[si];
        if (b.x > b.d.length - 40) break;
        if (b.dense > 0 && sec.role != Role::Finale && b.x > finaleAt) { // (on to the finale, after the one rest)
            if (density >= 7) rest(b, static_cast<int>(std::lround(10 * (1 - 0.5 * b.dense))), opts.has("disco"));
            si = sections.size() - 2;
            continue;
        }
        std::vector<std::pair<Verb, double>> weights;
        for (Verb v : verbs) {
            // (denser: more of what costs no eggs, barriers and drops, the meter the limit at full speed)
            double w = v == Verb::Stack ? 1.2 : v == Verb::Shed ? 0.9 + 0.6 * b.dense : v == Verb::Duck ? 0.6 + 1.4 * b.dense : 0.6;
            if (v == star) w = sec.role == Role::Teach ? 50 : 2.4;
            if (sec.role == Role::Teach && v != star && v != Verb::Stack) w = 0;
            weights.push_back({v, w});
        }
        const Rhythm rhythm = makeRhythm(rr, sec.tempo, sec.density, sec.seconds, weights, int(std::lround(b.dense * 10)));
        Group g;
        g.x0 = b.x; g.role = sec.role; g.rhythm = rhythm;
        const double eggs0 = b.eggsMet, haz0 = b.hazardsMet;
        // (its beats till the course has run its seconds: pieces longer than the beats allow would run on past them)
        for (size_t i = 0; i < rhythm.beats.size() && b.x < b.d.length - 40 && b.x - g.x0 < sec.seconds * b.v() * 1.1; ++i) {
            const double dt = (i + 1 < rhythm.beats.size() ? rhythm.beats[i + 1].t : rhythm.length) - rhythm.beats[i].t;
            const double gap = dt * b.v();
            // the floor path: the next storey move, once it's due (the teach group stays on the street)
            if (sec.role != Role::Teach && nextMove < path.moves.size() && nextMove < moveX.size() && b.x >= moveX[nextMove]) {
                // (a move the builder can't make, no room up there or no storey under her, or hasn't managed in a while, is dropped)
                const int move = path.moves[nextMove];
                if ((move > 0 && b.h > Builder::kTop - 7) || (move < 0 && b.below.empty()) || fails >= 8) { ++nextMove; fails = 0; }
                else {
                    const char got = layMove(b, move, gap, sec.role == Role::Finale ? 2 : 1, opts);
                    if (got) { g.pieces += got; ++nextMove; fails = 0; continue; }
                    ++fails;
                }
            }
            const int tier = sec.role == Role::Teach ? 0 : sec.role == Role::Finale ? 2 : rg.next() >= 2.0 / 3 - 0.4 * b.dense ? 2 : 1; // (denser: the hard tier more; at 0 a third, as it was)
            std::vector<const Piece*> fits;
            std::vector<size_t> chunkFits; // (the designer's chunks: any beat but the teach group's, as many as their uses allow)
            if (b.pool && sec.role != Role::Teach)
                for (size_t ci = 0; ci < b.pool->chunks.size(); ++ci)
                    if (!b.pool->chunks[ci]->uses || b.chunkUses[ci] < b.pool->chunks[ci]->uses) chunkFits.push_back(ci);
            for (const Piece& p : grammar()) {
                if (p.verb != rhythm.beats[i].verb || (p.hazard && !opts.has(p.hazard)) || storeyPiece(p.code)) continue; // (the storeys: the floor path's)
                if (p.code == 'u' && b.dense <= 0) continue; // (density 0: the grammar as it was)
                const bool comp = composite(p.code);
                if (sec.role == Role::Teach && comp) continue;
                if (sec.role == Role::Twist && p.verb == star && !comp && rg.chance(0.6)) continue; // (the twist: the star in a composite)
                fits.push_back(&p);
            }
            // (in a weighted random order: the set pieces weigh more as the slider rises; Efraimidis and Spirakis)
            std::vector<std::pair<double, const Piece*>> order;
            for (const Piece* p : fits) order.push_back({std::pow(rg.next(), 1.0 / (setPiece(p->code) ? 0.6 + 2.4 * b.dense : 1.0)), p});
            std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& c) { return a.first > c.first; });
            for (size_t k = 0; k < order.size(); ++k) fits[k] = order[k].second;
            bool laid = false;
            if (chunkFits.empty()) {
                for (const Piece* p : fits) {
                    const Builder saved = b;
                    if (p->lay(b, gap, tier)) { g.pieces += p->code; laid = true; break; }
                    b = saved;
                }
            } else { // (one order for the pieces and the chunks: a chunk weighs what its weight says, a plain piece 1)
                std::vector<std::pair<double, int>> all; // (a piece's index in fits, or minus the chunk's + 1)
                for (size_t k = 0; k < fits.size(); ++k) all.push_back({order[k].first, int(k)});
                for (size_t ci : chunkFits) all.push_back({std::pow(rg.next(), 1.0 / b.pool->chunks[ci]->weight), -int(ci) - 1});
                std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& c) { return a.first > c.first; });
                for (const auto& [key, which] : all) {
                    const Builder saved = b;
                    if (which >= 0 ? fits[size_t(which)]->lay(b, gap, tier) : chunkPiece(b, size_t(-which - 1))) { g.pieces += which >= 0 ? fits[size_t(which)]->code : 'Q'; laid = true; break; }
                    b = saved;
                }
            }
            if (!laid && b.floating) { // (up on a floating floor, the beat's verb may have no piece that fits there: any other, in a random order)
                std::vector<const Piece*> any;
                for (const Piece& p : grammar()) if (!storeyPiece(p.code) && (!p.hazard || opts.has(p.hazard)) && !(sec.role == Role::Teach && composite(p.code))) any.push_back(&p);
                for (size_t k = any.size(); k > 1; --k) std::swap(any[k - 1], any[size_t(rg.below(int(k)))]);
                for (const Piece* p : any) {
                    const Builder saved = b;
                    if (p->lay(b, gap, tier)) { g.pieces += p->code; laid = true; break; }
                    b = saved;
                }
            }
            if (!laid && !(climb(b, gap, 0) && (g.pieces += 'c', true))) b.wall(b.cellsFor(gap), b.h, b.kind());
        }
        g.x1 = b.x;
        if (sec.role == Role::Teach && moveX.empty() && !path.moves.empty()) { // (the moves, evenly from here to the finale's start, a little apart from a beat)
            const double lo = b.x + 5 * U, hi = std::max(lo + 30.0, b.d.length - 70);
            for (size_t k = 0; k < path.moves.size(); ++k) moveX.push_back(lo + (hi - lo) * (double(k) + 0.1 + 0.6 * rg.next()) / double(path.moves.size()));
        }
        g.intensity = ((b.eggsMet - eggs0) + (b.hazardsMet - haz0) * 1.5) / std::max(1.0, (g.x1 - g.x0) / b.v());
        if (!g.pieces.empty()) plan.groups.push_back(g);
        // a rest (denser: fewer, shorter; at 10, one in five, half as long; from 7, none but the one before the finale)
        const bool beforeFinale = si + 1 < sections.size() && sections[si + 1].role == Role::Finale;
        if (sec.role != Role::Finale && (density >= 7 ? beforeFinale : (b.dense <= 0 || rg.chance(1 - 0.8 * b.dense))))
            rest(b, static_cast<int>(std::lround((10 + rg.below(5)) * (1 - 0.5 * b.dense))), opts.has("disco"));
    }
    // on past the finish line at the height she's got to (the level finishes on whatever floor the path ends on: the
    // celebration runs on for seconds, so a high floor carries on well past the line)
    const double end = b.d.length + (b.h > 0 ? 90 : 20);
    if (b.x < end) b.wall(static_cast<int>(std::ceil((end - b.x) / U)), b.h, WallKind::Crate);
    std::stable_sort(b.d.items.begin(), b.d.items.end(), [](const Placed& a, const Placed& c) { return a.x() < c.x(); });
    plan.data = b.d;
    plan.chunks = b.placed;
    return plan;
}

constexpr double kMinAbove = 0.34; // (a third of the course over the street, at least)

bool passes(const LevelData& d) {
    std::string why;
    const std::string text = levelText(d);
    const auto back = parseLevel(d.number, text, &why);
    if (!back) return false;
    Level l;
    l.load(*back);
    l.generateUntil(back->length + 40);
    const Report r = judge(l);
    return r.notes.empty() && r.short_.empty() && r.doors.empty();
}

} // namespace

double beatSpacing(int rhythmDensity, int density) {
    const double base = rhythmDensity == 0 ? 2.4 : rhythmDensity == 1 ? 1.7 : 1.25;
    const double d = std::clamp(density, 0, 10) / 10.0;
    return base * (1 - 0.65 * d - 0.1 * std::max(0.0, (d - 0.5) / 0.5)); // (and 10% less across 5 to 10: 25% of the spacing at 10)
}

double targetMoments(int density) { // (0: ~1.2 s a moment, as it was; the aim rises faster from 5)
    const int d = std::clamp(density, 0, 10);
    return 0.85 + 0.12 * d + 0.6 * std::max(0, d - 5) / 5.0;
}

double curveFit(const std::vector<Group>& groups) {
    if (groups.size() < 2) return 0;
    double hi = 1e-9;
    for (const Group& g : groups) hi = std::max(hi, g.intensity);
    double err = 0;
    for (size_t i = 0; i < groups.size(); ++i) {
        const double p = double(i) / double(groups.size() - 1);
        // a hook early, a dip, a varied rise, the climax last (Schell's interest curve)
        const double want = p < 0.12 ? 0.55 : p < 0.25 ? 0.4 : p < 0.9 ? 0.45 + (p - 0.25) * 0.55 : 1.0;
        err += std::abs(groups[i].intensity / hi - want);
    }
    return std::max(0.0, 1.0 - err / double(groups.size()));
}

double mixChiSquare(const std::vector<Group>& groups, int level) {
    // climbs, drops, barriers, saws, lava, springs, bounce boxes, lasers, blocks, storeys (ascents, floating floors,
    // descents): the zone's mix (later, more of the new)
    const std::string cats = "cdbsljxzKA";
    const std::array<double, 10> want = level <= 40 ? std::array<double, 10>{0.2, 0.14, 0.1, 0.11, 0.07, 0.07, 0.07, 0.06, 0.08, 0.1}
                                                    : std::array<double, 10>{0.16, 0.12, 0.09, 0.12, 0.09, 0.09, 0.08, 0.07, 0.09, 0.09};
    std::array<double, 10> got{};
    double total = 0;
    for (const Group& g : groups)
        for (char p : g.pieces) {
            const char k = p == 't' ? 'c' : p == 'k' ? 's' : p == 'v' ? 'd' : p == 'm' ? 'c' : p == 'o' ? 'j' : p == 'S' || p == 'X' ? 's' : p == 'u' || p == 'B' ? 'b'
                         : p == 'L' ? 'l' : p == 'Z' ? 'z' : p == 'J' ? 'j' : p == 'K' || p == 'W' || p == 'P' || p == 'Q' ? 'K' : storeyPiece(p) ? 'A' : p;
            const size_t i = cats.find(k);
            if (i != std::string::npos) { got[i] += 1; total += 1; }
        }
    if (total < 1) return 1e9;
    double chi = 0;
    for (size_t i = 0; i < want.size(); ++i) { const double e = want[i] * total; chi += (got[i] - e) * (got[i] - e) / e; }
    return chi;
}

int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> row(b.size() + 1);
    std::iota(row.begin(), row.end(), 0);
    for (size_t i = 1; i <= a.size(); ++i) {
        int diag = row[0];
        row[0] = int(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// compose's work; `fallback` off: a course that nothing passes comes back unfinished (.passed 0) for the caller to try again
static Plan composeWith(int n, uint32_t seed, const Options& options, bool fallback) {
    std::vector<Verb> verbs = available(options.hazards);
    // the star: the campaign's own seed turns through the verbs level by level (no level like the one before); a roll
    // of the editors' picks one at random
    const Verb star = verbs[size_t((seed == Campaign::seedOf(n) ? n : n + int(seed % 97)) % int(verbs.size()))];
    Plan best;
    std::vector<Plan> kept;
    int tried = 0, passed = 0;
    // the floor path's shape, from the level's seed (all its candidates share it): linear about 40% of the time
    const bool linear = Rng(seed ^ 0xF100F5u).chance(0.4);
    ChunkPool pool; // (the designer's course chunks in range of this level, none: the level is exactly as it was)
    for (const Chunk& c : options.hazards.chunks)
        if (!c.maze && c.inRange(n) && c.body.course.size() >= 3) {
            pool.chunks.push_back(&c);
            pool.flipped.push_back(c.mirrors() ? mirrored(c) : Chunk{});
        }
    for (int gi = 0; gi < options.geometries && passed < options.enough; ++gi) // (each rhythm in turn, then another geometry)
        for (int ri = 0; ri < options.rhythms && passed < options.enough; ++ri) {
            Rng rr(seed ^ uint32_t(0x9E3779B9u * uint32_t(ri + 1)));
            Rng rg(seed ^ uint32_t(0x85EBCA6Bu * uint32_t(ri * 131 + gi + 7)));
            std::optional<Plan> p = lay(n, rr, rg, star, options.hazards, options.density, linear, &pool);
            ++tried;
            if (!p || !passes(p->data)) continue;
            const Traits tr = traits(p->data);
            if (tr.above < kMinAbove) continue; // (never flat: a course that stays on the street most of the way is no candidate)
            ++passed;
            // variety: no two groups alike, the staircases in check (a linearity bias)
            double variety = 1;
            for (size_t i = 1; i < p->groups.size(); ++i) if (editDistance(p->groups[i].pieces, p->groups[i - 1].pieces) < 2) variety -= 0.15;
            int run = 0, longest = 0;
            for (const Group& g : p->groups) for (char c : g.pieces) { run = c == 'c' || c == 't' ? run + (c == 't' ? 3 : 1) : 0; longest = std::max(longest, run); }
            if (longest > 6) variety -= 0.3;
            p->score = 0.5 * curveFit(p->groups) + 0.3 / (1 + mixChiSquare(p->groups, n) / 10) + 0.2 * std::max(0.0, variety);
            // the never-flat critic: the time spent above the street, each storey reached and each floating floor
            int rises = 0;
            for (const Group& g : p->groups) for (char c : g.pieces) rises += c == 'A' || c == 'F' || c == 'G';
            p->score += 0.3 * std::min(1.0, tr.meanFloor / 8.0) + 0.03 * std::min(6, rises);
            if (options.density > 0) { // the density critic: as many moments a second as the slider asks for
                const double got = 1.0 / std::max(0.05, tr.pacing);
                p->score += 0.4 * std::max(0.0, 1.0 - std::abs(got - targetMoments(options.density)) / targetMoments(options.density));
            }
            kept.push_back(std::move(*p));
        }
    // the game's bot plays the best of them, best first, till one it clears (the checker can't see everything)
    std::stable_sort(kept.begin(), kept.end(), [](const Plan& a, const Plan& c) { return a.score > c.score; });
    best = Plan{};
    best.score = -1e9;
    for (size_t i = 0; i < kept.size() && i < 16; ++i) {
        LevelCatalog cat;
        if (!cat.setLevel(n, levelText(kept[i].data))) continue;
        if (playLevel(n, &cat).cleared) { best = kept[i]; break; }
    }
    if (best.score <= -1e9) passed = 0;
    std::vector<std::string> inRange; // (the chunks that were in the pool)
    for (const Chunk* c : pool.chunks) inRange.push_back(c->id);
    if (passed == 0 && !fallback) { Plan none; none.density = options.density; none.tried = tried; return none; }
    if (passed == 0 && !inRange.empty()) {
        // nothing passed with the chunks: the density is kept and the chunks that spoil it left out: each alone first if there
        // are several, then all of them (a course never comes back plain or less dense because of a chunk)
        auto without = [&](const std::vector<std::string>& ids) {
            Options o = options;
            o.hazards.chunks.clear();
            for (const Chunk& c : options.hazards.chunks) if (std::find(ids.begin(), ids.end(), c.id) == ids.end()) o.hazards.chunks.push_back(c);
            return o;
        };
        std::vector<std::vector<std::string>> drops;
        if (inRange.size() > 1) for (const std::string& id : inRange) drops.push_back({id});
        drops.push_back(inRange);
        for (const std::vector<std::string>& ids : drops) {
            Plan p = composeWith(n, seed, without(ids), false);
            if (p.passed > 0) { p.chunksDropped = ids; return p; }
        }
    }
    if (passed == 0 && options.density > 0) { // (nothing passed this dense: a notch down)
        Options less = options;
        --less.density;
        return composeWith(n, seed, less, true);
    }
    best.density = options.density;
    if (passed == 0) { // (nothing passed: the old generator's course, as before)
        best = Plan{};
        best.data = bakeClassic(n, nullptr, seed, options.hazards);
        best.chunksDropped = inRange; // (it holds none)
    }
    best.data.notes = {"Level " + std::to_string(n) + ", " + zoneName(n) + ": " + verbName(best.star) + " is its star (the rhythm-beat generator)."};
    best.tried = tried; best.passed = passed;
    return best;
}

Plan compose(int n, uint32_t seed, const Options& options) { return composeWith(n, seed, options, true); }

Traits traits(const LevelData& d) {
    Traits t;
    std::vector<std::pair<double, double>> mid;
    double x = 0;
    int moments = 0;
    double lastH = 0, hazard = 0;
    for (const LevelData::Wall& w : d.course) {
        if (x >= d.length) break;
        // (a floating floor, a slab two rows thick over the open street: she's on its top; no barrier, no moment of its own)
        const bool floor = w.top && w.h == 0 && w.ceil >= 3 && w.top - w.ceil == 2;
        const int h = floor ? w.top : w.h;
        mid.push_back({x + w.cells * U / 2, double(h)});
        if (h != lastH) { ++moments; hazard += h > lastH ? (h - lastH) : 0.5; }
        if (w.ceil && !floor) { ++moments; hazard += 1; }
        lastH = h; t.tallest = std::max(t.tallest, h);
        x += w.cells * U;
    }
    for (const Placed& p : d.items) if (p.type != &kDiscoHazard && p.x() < d.length) { ++moments; hazard += p.type == &kSpringHazard ? 0.5 : 1.5; }
    // R² of the least-squares line through the floor's midpoints
    const double nn = double(mid.size());
    if (nn >= 2) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
        for (auto [a, b] : mid) { sx += a; sy += b; sxx += a * a; sxy += a * b; syy += b * b; }
        const double vx = sxx - sx * sx / nn, vy = syy - sy * sy / nn, cxy = sxy - sx * sy / nn;
        t.linearity = vx > 0 && vy > 0 ? cxy * cxy / (vx * vy) : 1;
    }
    // the share of the course over the street: the floor she runs at on each segment, as the checker takes the blocks
    // (over where there's no room under them, once she's as high as their top)
    if (d.length > 0) {
        Level l;
        l.load(d);
        l.generateUntil(d.length + 5);
        double over = 0, rows = 0;
        int feet = 0;
        std::vector<std::pair<double, double>> flights; // (the springs' flights: she's up on the block they land her on)
        for (const Spring& sp : l.springs) {
            const rules::Flight f = rules::springFlight(l, sp, l.curveAt(sp.x).speed);
            if (f.end == rules::Flight::End::Landed) flights.push_back({sp.x, f.x});
        }
        for (size_t i = 0; i < l.segs.size(); ++i) {
            const Segment& s = l.segs[i];
            if (s.x0 >= d.length) break;
            bool flown = false;
            for (const auto& f : flights) flown = flown || (s.x0 > f.first && s.x0 <= f.second);
            feet = (s.top && flown) || rules::takesBlockOver(s, i + 1 < l.segs.size() ? &l.segs[i + 1] : nullptr, feet * U) ? s.top : s.h;
            if (feet > 0) over += std::min(s.x1, d.length) - s.x0;
            rows += feet * (std::min(s.x1, d.length) - s.x0);
        }
        t.above = over / d.length;
        t.meanFloor = rows / d.length;
    }
    t.leniency = -hazard / std::max(1.0, d.length) * 10;
    const double speed = curve(d.diffOffset + d.length * d.diffScale * 0.5).speed;
    t.pacing = moments ? d.length / speed / moments : d.length / speed;
    return t;
}

} // namespace cs::beats
