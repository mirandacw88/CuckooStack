#include "MazeGen.h"

#include "core/Gauntlet.h"
#include "core/GauntletReport.h"
#include "core/LevelCatalog.h"
#include "core/LevelData.h"
#include "EditorDoc.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace cs::ed {

namespace {

constexpr int kBody = Gauntlet::kBodyRows;

// a grid written bottom up (row 0 at the street), as scripts/gauntlets/kit.py writes one
struct Grid {
    int cols, rows;
    std::vector<std::string> cells;
    Grid(int c, int r) : cols(c), rows(r), cells(size_t(r), std::string(size_t(c), '#')) {}
    void put(int r0, int r1, int c0, int c1, char ch) {
        for (int r = std::max(0, std::min(r0, r1)); r <= std::min(rows - 1, std::max(r0, r1)); ++r)
            for (int c = std::max(0, std::min(c0, c1)); c <= std::min(cols - 1, std::max(c0, c1)); ++c) cells[size_t(r)][size_t(c)] = ch;
    }
    void set(int c, int r, char ch) { if (r >= 0 && r < rows && c >= 0 && c < cols) cells[size_t(r)][size_t(c)] = ch; }
    std::vector<std::string> topFirst() const { return {cells.rbegin(), cells.rend()}; }
};

// the floor path: one storey move a room (up 1, down 1), from storey 0: linear (a ramp up and down, a peak, a climb that
// ends high) or a random walk (up or down at random: never below the street or above the third storey, the same way at
// most four times running)
struct Path {
    std::vector<int> lv; // the storey of each room
    bool linear = false;
    const char* name = "";
};

Path makePath(int moves, std::mt19937& rng) {
    auto roll = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    Path p;
    p.lv.push_back(0);
    const int m = std::max(1, moves);
    if (m <= 6 && roll(0, 99) < 40) { // linear: climbs up then (but for a climb) down, never below the street
        p.linear = true;
        const int pick = m > 5 ? roll(0, 1) : roll(0, 2), half = (m + 1) / 2;
        int ups = half;
        if (pick == 0) p.name = "a ramp";
        else if (pick == 1) { p.name = "a peak"; ups = std::min(3, std::max(half, m - 1)); }
        else { p.name = "a climb"; ups = m; }
        for (int i = 0; i < m; ++i) p.lv.push_back(p.lv.back() + (i < ups ? 1 : -1));
        return p;
    }
    p.name = "a random walk";
    int run = 0, last = 0;
    for (int i = 0; i < m; ++i) {
        int mv = roll(0, 1) ? 1 : -1;
        const int at = p.lv.back();
        if (at == 0 || i == 0) mv = 1;
        else if (at >= 3) mv = -1;
        if (mv == last && run >= 4) mv = -mv;
        if (at + mv < 0 || at + mv > 3) mv = -mv;
        run = mv == last ? run + 1 : 1;
        last = mv;
        p.lv.push_back(at + mv);
    }
    return p;
}
// a stretch of a floor: its floor `off` rows over the floor's base, columns s to e (the floor drawn running right; a floor
// that runs left is this, mirrored)
struct Sec { int off = 0, s = 0, e = 0; };

// a floor of the tower: h high (air rows base to base + h - 1) from row b, in stretches at their own heights
struct Flr { int h = 6, b = 1; std::vector<Sec> sec; Path path; int unit = 0; };

// a chunk's cells set on a floor grid: its bottom row at row g0, from column c0; the stone in its top row reaches up to the roof
void putChunk(Grid& L, const Chunk& ch, int c0, int g0, int roof) {
    stampMazeChunk(ch, c0, g0, roof, [&](int c, int r, char cell) { L.set(c, r, cell); });
}

} // namespace

std::vector<std::string> layMaze(int width, int floors, uint32_t seed, double cellsPerEgg, std::vector<std::string>* description, const MazeOptions& options, std::vector<MazeChunkUse>* used) {
    std::mt19937 rng(seed);
    auto roll = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    const int W = std::clamp(width, 60, 400), n = std::clamp(floors | 1, 3, 7); // (odd: the last floor runs right, to the exit; one alone is a corridor, too few near misses)
    const int density = std::clamp(options.density, 0, 10);
    const double dd = density / 10.0;
    // the floors, stacked, the tomb's way: right along the first, up a shaft through a one-way gate (stacked over its wall),
    // left along the next, ... Each is 6 to 11 high or so, and its own floor rises and falls along it: stretches at different
    // heights (a stone wall she stacks over going up, a drop going down) in a floor path (makePath: linear, or a random
    // walk); the last stretch of a floor she climbs out of is high (the gate's climb is 8 eggs at the most), the first of
    // the next at its base (the shaft's top)
    std::vector<Flr> fl(static_cast<size_t>(n));
    // how high each floor is: 6 to 11, whatever the tower. A course wall is 60 rows at the most (LevelData), and the exit's porch is
    // one, so the exit stays within 58 rows and the grid within 64 (the editors'): the floors under the last add up to 51 rows
    // with their floors (a tower of 7: about 8 a floor), the tallest cut back till they fit (and with them the rises inside)
    const int hmin = 6;
    std::vector<int> heights(static_cast<size_t>(n));
    for (int& v : heights) v = std::min(11, roll(0, 99) < 65 ? roll(hmin, hmin + 2) : roll(hmin + 2, hmin + 5));
    for (;;) {
        int sum = 0, best = -1;
        for (int k = 0; k + 1 < n; ++k) { sum += heights[size_t(k)] + 1; if (heights[size_t(k)] > hmin && (best < 0 || heights[size_t(k)] > heights[size_t(best)] || (heights[size_t(k)] == heights[size_t(best)] && roll(0, 1)))) best = k; }
        if (sum <= 51 || best < 0) break;
        --heights[size_t(best)];
    }
    heights.back() = std::min(heights.back(), 62 - (1 + [&] { int t = 0; for (int k = 0; k + 1 < n; ++k) t += heights[size_t(k)] + 1; return t; }()));
    int base = 1;
    for (int k = 0; k < n; ++k) {
        Flr& F = fl[size_t(k)];
        F.b = base;
        base += heights[size_t(k)] + 1;
        const bool last = k == n - 1;
        const int sA = k == 0 ? 0 : 1, sB = last ? W - 2 : W - 6, L = sB - sA + 1;
        int h = heights[size_t(k)];
        const int cap = last ? std::min(h - 4, 58 - F.b) : h - 4; // (rows the floor may rise: 4 of head room left at the highest stretch; the exit no higher than 58)
        const int need = last ? 0 : std::max(0, h - 7);  // (the climb through the gate: h + 1 - the last stretch's rise, 8 eggs at the most)
        int m = roll(2, std::max(2, std::min(5, L / 32)));
        std::vector<int> off;
        Path path;
        bool ok = false;
        for (int attempt = 0; attempt < 16 && !ok && cap >= 1; ++attempt) {
            path = makePath(m - 1, rng);
            const int top = *std::max_element(path.lv.begin(), path.lv.end());
            if (top == 0) continue;
            const int q = std::min(roll(2, 4), cap / top); // (rows a move of the path is)
            if (q < 1 || (q < 2 && attempt < 12) || path.lv.back() * q < need) continue;
            for (int v : path.lv) off.push_back(v * q);
            F.unit = q;
            ok = true;
        }
        if (!ok && cap >= std::max(2, need)) { // (one step up, as high as the gate's climb needs)
            m = 2;
            path = Path{{0, 1}, false, "a step"};
            F.unit = std::max(2, need);
            off = {0, F.unit};
            ok = true;
        }
        if (!ok) { // (no head room to rise in: flat)
            m = 1;
            path = Path{{0}, false, "flat"};
            off = {0};
        }
        F.h = h;
        F.path = path;
        std::vector<double> share;
        double all = 0;
        for (int i = 0; i < m; ++i) { share.push_back(0.85 + 0.3 * roll(0, 100) / 100.0); all += share.back(); }
        double cum = 0;
        for (int i = 0; i < m; ++i) {
            const int s0 = sA + int(std::lround(L * cum / all));
            cum += share[size_t(i)];
            const int s1 = i + 1 == m ? sB + 1 : sA + int(std::lround(L * cum / all));
            F.sec.push_back({off[size_t(i)], s0, s1 - 1});
        }
    }
    auto topOf = [&](int k) { return fl[size_t(k)].b + fl[size_t(k)].h - 1; };
    Grid g(W, topOf(n - 1) + 2);
    const int gRows = g.rows;
    Grid* cur = &g; // (the floor being laid: a grid of its own, floor row 1 and roof row h, as if it ran right)
    int springs = 0, padsUp = 0, padsLedge = 0;
    int secS = 0, secE = 0; // (the stretch being laid: its columns; a beam is let loose only where it stops inside them, at the wall of the next)
    bool gateEnd = false;   // ...and whether it ends at the gate: the beam stops before it
    // along each room, her way: what the tomb has, spaced so the meter refills between what costs eggs
    int saws = 0, dips = 0, steps = 0, beams = 0, lava = 0, nledges = 0;
    // what may come (the options: the hazards switched on), and how often
    const bool sawsOn = options.saws;
    struct Kind { int id, weight; };
    std::vector<Kind> kinds;
    // (the denser, the more of what packs near misses small: saws, saws over which the roof dips, fewer long steps)
    const auto up = [&](int w, double by) { return w + int(std::lround(by * dd)); };
    if (sawsOn) kinds.insert(kinds.end(), {{0, up(28, 0)}, {1, up(14, 6)}, {3, up(10, 70)}});
    kinds.insert(kinds.end(), {{2, 14}, {4, up(34, -26)}});
    if (options.lasers) kinds.push_back({5, 24});
    if (options.lava) kinds.push_back({6, 30});
    int total = 0;
    for (const Kind& c : kinds) total += c.weight;
    bool noBeam = false; // (a room with a spring: no wall laser in it, its beam would cross the arc: Gauntlet::flightFrom)
    auto pick = [&] {
        for (int again = 0;; ++again) {
            int r = roll(0, total - 1);
            int id = kinds.back().id;
            for (const Kind& c : kinds) { if (r < c.weight) { id = c.id; break; } r -= c.weight; }
            if (id != 5 || !noBeam || again > 20) return id == 5 && noBeam ? 4 : id;
        }
    };
    // the cells between hazards: the meter's refill (cells per egg) times 1.4 and 1 to 6 more, down to times 0.7 and 0 to 2 at
    // density 10 (but never fewer than she needs to lay the eggs: kLayGap a cell, or the maze has no way out)
    auto gapFor = [&](int eggs) {
        const double per = density > 0 ? std::max(double(Gauntlet::kLayGap), cellsPerEgg * (1.4 - 0.7 * dd)) : cellsPerEgg * 1.4;
        return std::max(4, int(std::ceil(std::max(eggs, 1) * per)));
    };
    auto extra = [&] { return roll(density >= 5 ? 0 : 1, 6 - int(std::lround(4 * dd))); };
    int lead = 0; // (steps forced right after the first hazard: a landing's saw, then a step up she has just the egg for)
    // hazards along one room (floor row f, roof row t, hh high), between columns lo and hi, running d
    auto decorate = [&](int f, int t, int hh, int lo, int hi, int d, bool first) {
        Grid& g = *cur;
        int rejected = 0;
        // (a wall laser's beam runs along its row through the air to the first stone: only where that is inside this stretch, or
        // its next wall, so it never crosses into the stretch beside it, or the shaft)
        auto beamStops = [&](int x, int row, int dir) {
            if (row < 0 || row >= g.rows) return false;
            while (x >= 0 && x < g.cols && g.cells[size_t(row)][size_t(x)] == '.') x += dir;
            return x >= secS - 1 && x <= (gateEnd ? secE : secE + 1) && x >= 0 && x < g.cols;
        };
        if (hi - lo < 2) return;
        // a cluster: hazards back to back that each take eggs (saws, lava, a step), more of them and bigger up the slider; the
        // gap before it is long enough to stack all they take (under a roof that holds them), and no dip or laser among them
        // (room2: that many cells on from `last` still inside the run)
        auto room2 = [&](int last, int cells) { return d > 0 ? last + cells <= hi : last - cells >= lo; };
        // (denser than 0: the next kind is picked early, and the gap before it stacks what it will take)
        int left = 0, bank = 0, nextKind = -1;
        static constexpr int kTakes[] = {1, 3, 1, 1, 3, 3, 3, 1, 1};
        bool clustered = false;
        auto after = [&](int last, int eggs) {
            if (clustered) bank -= eggs;
            if (left > 0 && bank > 0) { --left; clustered = true; return last + d * roll(2, 3); }
            left = 0;
            clustered = false;
            if (density > 0 && roll(0, 99) < 6 * density && hh - kBody >= 2 && room2(last, gapFor(std::min(hh - kBody, 5)) + 14)) {
                bank = std::min(hh - kBody, 5);
                left = std::min(bank, roll(2, 2 + int(std::lround(3 * dd)))); // (members: this many in all)
                bank = std::min(bank, left + roll(0, 1));
                clustered = true;
                --left;
                return last + d * (gapFor(std::max(eggs, bank)) + extra());
            }
            if (density > 0) {
                nextKind = pick();
                return last + d * (gapFor(kTakes[nextKind]) + extra());
            }
            return last + d * (gapFor(eggs) + extra());
        };
        int at = d > 0 ? lo + roll(0, 4) : hi - roll(0, 4);
        auto room = [&](int span) { return d > 0 ? at + span - 1 <= hi : at - span + 1 >= lo; };
        auto col = [&](int i) { return at + d * i; };
        while (d > 0 ? at <= hi : at >= lo) {
            int kind = first ? (sawsOn ? 0 : options.lava ? 7 : 8) : lead > 0 ? 8 : nextKind >= 0 ? nextKind : pick(); // (lead: a step right after the first, a PERFECT)
            if (!first && lead > 0) { --lead; clustered = false; left = 0; }
            nextKind = -1;
            if (density > 0 && sawsOn && !first && !clustered && kind != 0 && kind != 7 && !room(10) && room(1)) kind = 0; // (no room for a long one: a saw)
            if (clustered && !first) { // (a member of a cluster: what takes eggs, one that fits what's left to stack)
                std::vector<int> m{8};
                if (sawsOn) m.insert(m.end(), {0, 0, 1});
                if (options.lava) m.push_back(6);
                kind = m[size_t(roll(0, int(m.size()) - 1))];
                if (kind == 1 && bank < 2) kind = 0;
            }
            first = false; // (the first thing in from the door takes the egg she may have brought in: a saw, or a cell of lava, or a step)
            if (kind == 7) { // (one cell of lava)
                if (!room(1)) break;
                g.set(col(0), f - 1, '~');
                ++lava;
                at = after(col(0), 1);
                continue;
            }
            if (kind == 8) { // (a step one high)
                const int span = roll(4, 8);
                if (!room(span)) break;
                g.put(f, f, col(0), col(span - 1), '#');
                ++steps;
                at = after(col(span - 1), 1);
                continue;
            }
            int span = 1, eggs = 0, holdOff = 0; // (holdOff: cells more before the next, so she isn't asked to lay an egg for it while the beam is over her)
            if (kind == 0) { // a saw
                if (!room(1)) break;
                g.set(col(0), f - 1, 'S');
                eggs = 1; ++saws;
            } else if (kind == 1) { // two saws back to back (a cell apart: each reads, and cuts, on its own); a row of them, up the slider
                int blades = 2 + (density > 0 && roll(0, 99) < 10 * density ? roll(1, 1 + int(std::lround(2 * dd))) : 0);
                if (clustered) blades = std::min(blades, bank);
                span = 2 * blades - 1;
                if (!room(span)) break;
                for (int i = 0; i < blades; ++i) g.set(col(2 * i), f - 1, 'S');
                eggs = blades; saws += blades;
            } else if (kind == 5) { // a wall laser: in the face of a step she drops off (firing on along the floor: she falls
                                    // down through it, an egg cut out), or in a dip of the roof's face, firing back at her
                                    // just over her head as she ducks under it
                if (roll(0, 1) == 0) {
                    const int high = roll(1, std::min(2, hh - kBody - 1));
                    span = roll(4, 8);
                    if (high < 1 || !room(span)) break;
                    if (!beamStops(col(span - 1) + d, f, d)) { if (++rejected > 30) break; continue; }
                    g.put(f, f + high - 1, col(0), col(span - 1), '#');
                    g.set(col(span - 1), f, d > 0 ? ')' : '(');
                    eggs = high + 1; ++steps;
                } else {
                    const int under = std::min(roll(0, 1), hh - kBody - 1);
                    // (a stub of the roof 5 to 9 cells back from the dip stops the beam there: it never sweeps the floor she
                    // must lay her eggs on, and no egg she carries is cut but in the few cells between)
                    const int back = roll(5, 9), dip = roll(3, 6);
                    span = 1 + back + dip;
                    if (!room(span) || f + kBody + under > t) break;
                    g.put(f + kBody + under, t, col(0), col(0), '#');
                    g.put(f + kBody + under, t, col(back + 1), col(span - 1), '#');
                    g.set(col(back + 1), f + kBody + under, d > 0 ? '(' : ')');
                    holdOff = 12;
                    ++dips;
                }
                ++beams;
            } else if (kind == 6) { // lava under the corridor: an egg a cell (no more than fit under its roof)
                span = roll(1, std::max(1, std::min({4 + int(std::lround(2 * dd)), hh - kBody, clustered ? bank : 9}))); // (a run of lava, longer up the slider)
                if (!room(span)) break;
                for (int i = 0; i < span; ++i) g.set(col(i), f - 1, '~');
                eggs = span; lava += span;
            } else if (kind == 2 || kind == 3) { // the roof dips: no egg under it, or one or two (none over a saw: one, exactly)
                const bool saw = kind == 3;
                const int under = saw ? 1 : std::min(roll(0, 2), hh - kBody - 1);
                span = roll(3, 8 - int(std::lround(3 * dd)));
                if (!room(span) || f + kBody + under > t) break;
                g.put(f + kBody + under, t, col(0), col(span - 1), '#');
                if (saw) { g.set(col(span / 2), f - 1, 'S'); eggs = 1; ++saws; }
                ++dips;
            } else { // a step up (stone or cyber cubes), a saw in its top at times; she drops off its far end
                const bool cube = roll(0, 1) == 1, saw = sawsOn && roll(0, 1) == 1 && hh - kBody >= 3;
                const int high = roll(1, std::min(3, hh - kBody - (saw ? 2 : 1)));
                span = roll(4, 10 - int(std::lround(4 * dd)));
                if (high < 1 || !room(span)) break;
                g.put(f, f + high - 1, col(0), col(span - 1), cube ? 'H' : '#');
                if (saw) { g.set(col(1 + roll(0, span - 3)), f + high - 1, cube ? 'C' : 'S'); ++saws; }
                eggs = high + (saw ? 1 : 0); ++steps;
            }
            at = after(col(span - 1), eggs) + d * holdOff;
        }
    };
    // the chunks: the maze chunks asked for that are big enough to hold (3 x 3 at the least), and how many of each is laid
    std::vector<const Chunk*> chunks;
    for (const Chunk& c : options.chunks)
        if (c.maze && c.rows.size() >= 3 && c.rows.front().size() >= 3 && chunkBodyProblem(c).empty()) chunks.push_back(&c);
    std::vector<int> chunkUses(chunks.size(), 0);
    std::vector<MazeChunkUse> placed;
    std::vector<ChunkUse> label; // (for the description)
    // a chunk into the stretch being laid (floor row f0, roof row t, hh air rows, laid between columns lo and hi), or false: one
    // weighs in as the sum of its weight against 3 (a 3 is a coin toss), is picked by its weight among those that fit (the
    // head room, the length, its uses left, a floor that runs left only if it may be mirrored), stamped at a column along the
    // stretch (flipped half the time if it may be), kept only if every wall laser of its keeps its beam within the stretch; the
    // hazards go either side of it, a gap the meter refills in away
    auto stamp = [&](Grid& L, int k, int f0, int t, int hh, int lo, int hi, bool opening, bool wasLanded) {
        std::vector<size_t> fit;
        int sum = 0;
        for (size_t c = 0; c < chunks.size(); ++c) {
            const Chunk& ch = *chunks[c];
            const int H = int(ch.rows.size()), Wc = int(ch.rows.front().size());
            const bool lasers = std::any_of(ch.rows.begin(), ch.rows.end(), [](const std::string& r) { return r.find_first_of("()") != std::string::npos; });
            if ((ch.uses && chunkUses[c] >= ch.uses) || (k % 2 == 1 && !ch.mirrors()) || H > hh + 2 || Wc > hi - lo + 1 || (lasers && noBeam)) continue;
            fit.push_back(c);
            sum += ch.weight;
        }
        if (fit.empty() || roll(0, 99) >= 100 * sum / (sum + 3)) return false;
        int pickAt = roll(0, sum - 1);
        size_t c = fit.back();
        for (size_t f : fit) { if (pickAt < chunks[f]->weight) { c = f; break; } pickAt -= chunks[f]->weight; }
        const Chunk& ch = *chunks[c];
        const bool flip = ch.mirrors() && roll(0, 1) == 1;
        const Chunk shown = flip ? mirrored(ch) : ch;
        const int H = int(shown.rows.size()), Wc = int(shown.rows.front().size());
        const int c0 = lo + roll(0, hi - lo + 1 - Wc), c1 = c0 + Wc - 1;
        const Grid before = L;
        putChunk(L, shown, c0, f0 - 1, t + 1);
        // (a wall laser fires along its row to the first stone: inside this stretch, or its next wall, as the generator's own do)
        bool springs = false;
        for (int j = 0; j < H; ++j)
            for (int x = 0; x < Wc; ++x) {
                const char ch2 = shown.rows[size_t(j)][size_t(x)];
                springs = springs || ch2 == '^';
                if (ch2 != '(' && ch2 != ')') continue;
                const int dir = ch2 == ')' ? 1 : -1, row = f0 - 1 + (H - 1 - j);
                int bx = c0 + x + dir;
                while (bx >= 0 && bx < L.cols && L.cells[size_t(row)][size_t(bx)] == '.') bx += dir;
                if (!(bx >= secS - 1 && bx <= (gateEnd ? secE : secE + 1) && bx >= 0 && bx < L.cols)) { L = before; return false; }
            }
        ++chunkUses[c];
        noBeam = noBeam || springs;
        // the eggs it takes: a cell of saw, lava or cube saw, and the tallest step she stacks over
        int cost = 0, tall = 0;
        for (int j = 0; j < H; ++j) for (char z : shown.rows[size_t(j)]) cost += z == 'S' || z == 'C' || z == '~';
        for (int x = 0; x < Wc; ++x) {
            int up = 0;
            for (int j = H - 2; j >= 0 && (shown.rows[size_t(j)][size_t(x)] == '#' || shown.rows[size_t(j)][size_t(x)] == 'H' || shown.rows[size_t(j)][size_t(x)] == 'B'); --j) ++up;
            tall = std::max(tall, up);
        }
        const int gap = gapFor(std::clamp(cost + tall, 1, std::max(1, std::min(5, hh - kBody))));
        const int rowBottom = gRows - 1 - (fl[size_t(k)].b - 1 + f0 - 1), rowTop = rowBottom - (H - 1);
        placed.push_back({ch.id, k, k % 2 == 0 ? c0 : W - 1 - c1, k % 2 == 0 ? c1 : W - 1 - c0, rowTop, rowBottom, (k % 2 == 1) != flip});
        label.push_back({ch.id, double(c0), double(c1), (k % 2 == 1) != flip});
        decorate(f0, t, hh, lo, c0 - gap, 1, opening || wasLanded);
        lead = 0;
        decorate(f0, t, hh, c1 + gap, hi, 1, false);
        return true;
    };
    // each floor, laid on a grid of its own (running right: floor row 1, roof row h), then set in the tower, mirrored if it runs left
    int exitLow = 0;
    std::vector<std::vector<int>> offsets;
    for (int k = 0; k < n; ++k) {
        Flr& F = fl[size_t(k)];
        const int h = F.h, t = h, sm = int(F.sec.size());
        Grid L(W, h + 3);
        cur = &L;
        auto fr = [&](int i) { return 1 + F.sec[size_t(i)].off; }; // (a stretch's floor row)
        for (int i = 0; i < sm; ++i) L.put(fr(i), t, F.sec[size_t(i)].s, F.sec[size_t(i)].e, '.');
        if (k == 0) L.put(1 + kBody + 1, t, 0, 0, '#'); // (the door 3 high, whatever the corridor's height)
        // ledges: a stretch she drops into (4 rows or more down) that is 8 high or more has stone hung in mid-air under the
        // drop, one or two, 2 thick, each a fall or so lower than the last and the last well over the floor; she lands on the
        // first, runs off its end onto the next, and off that onto the floor. Never one she'd have to stack up into the side of
        struct Ledge { int sec, c0, c1, top; bool beam; int spring = -1, land = -1; }; // (spring: the column of the pad that flies her onto it, and where she lands)
        std::vector<Ledge> ledges;
        std::vector<char> dropLedge(static_cast<size_t>(sm), 0);
        for (int i = 1; i < sm; ++i) {
            const Sec& a = F.sec[size_t(i - 1)];
            const Sec& r = F.sec[size_t(i)];
            if (a.off - r.off < 4 || h - r.off < 8) continue;
            const int rb = fr(i), cap = r.e - 16; // (room kept past them for the floor's hazards and the climb)
            int top = fr(i - 1) - roll(1, 2), c0 = r.s;
            for (int j = 0; j < 2; ++j) {
                const int len = roll(14, 24), c1 = std::min(cap, c0 + len - 1);
                if (top < rb + 3 || top + 2 + kBody > t || c1 - c0 < 11) break;
                L.put(top - 2, top - 1, c0, c1, '#');
                ledges.push_back({i, c0, c1, top, false});
                dropLedge[size_t(i)] = 1;
                const int next = top - roll(3, 4);
                if (j == 1 || next < rb + 3 || roll(0, 99) < 45) break;
                c0 = c1 + 1;
                top = next;
            }
            if (!ledges.empty() && ledges.back().sec == i && options.lasers && roll(0, 2) == 0) { // (a laser in the last one's face, firing on over the drop)
                Ledge& l = ledges.back();
                if (l.c1 + 2 < W && L.cells[size_t(l.top - 2)][size_t(l.c1 + 1)] == '.') {
                    int x = l.c1 + 1;
                    while (x < W && L.cells[size_t(l.top - 2)][size_t(x)] == '.') ++x;
                    if (x <= r.e + 1 && !(i + 1 == sm && k + 1 < n && x > r.e)) { L.set(l.c1, l.top - 2, ')'); l.beam = true; } // (it stops inside the stretch, or at its next wall)
                }
            }
        }
        // springs (a '^': she flies off it; the solver's arc, Gauntlet::flightFrom): a pad on the floor that flies her up over a
        // step into the stretch above (rises of 2 to 4; this one 8 high at least, that one 6), or onto a ledge hung in the
        // air of a stretch 8 high (2 thick, 3 or 4 rows over its floor, long enough to land on and run along: she runs off its
        // end). Each is found by trying columns on the grid as it is, and kept only where the flight lands her where it
        // should; the corridor past a pad is never walked (the pad launches her), so the stacked wall behind it is no second
        // way. Where nothing fits, the floor is as it was (the wall she stacks over; no ledge)
        struct Pad { int sec, col, land; bool up; };
        std::vector<Pad> pads;
        auto probe = [&](const Grid& grid) {
            Grid c = grid;
            c.put(1, 3, 0, 0, '.');
            c.put(4, c.rows - 1, 0, 0, '#');
            c.set(0, 1, '>');
            c.put(1, 3, W - 1, W - 1, '.');
            c.put(4, c.rows - 1, W - 1, W - 1, '#');
            c.set(W - 1, 1, 'X');
            return Gauntlet::parse("probe", c.topFirst());
        };
        if (options.springs && roll(0, 99) < 55) {
            std::vector<int> ups, onto;
            for (int i = 0; i < sm; ++i) {
                const Sec& r = F.sec[size_t(i)];
                if (i + 1 < sm && F.sec[size_t(i + 1)].off - r.off >= 2 && F.sec[size_t(i + 1)].off - r.off <= 4 && h - r.off >= 8 && h - F.sec[size_t(i + 1)].off >= 6 && !dropLedge[size_t(i)]) ups.push_back(i);
                if (h - r.off >= 8 && !dropLedge[size_t(i)] && r.e - r.s >= 36) onto.push_back(i);
            }
            const bool wantUp = !ups.empty() && (onto.empty() || roll(0, 99) < 55);
            std::vector<int>& from = wantUp ? ups : onto;
            while (!from.empty() && pads.empty()) {
                const size_t at = size_t(roll(0, int(from.size()) - 1));
                const int i = from[at];
                from.erase(from.begin() + long(at));
                const Sec& r = F.sec[size_t(i)];
                if (wantUp) {
                    const Sec& z = F.sec[size_t(i + 1)];
                    const std::optional<Gauntlet> pr = probe(L);
                    if (!pr) continue;
                    for (int sc = r.e - 3; sc >= std::max(r.s + 10, r.e - 26); --sc) {
                        const Gauntlet::Flight f = pr->flightFrom(sc, fr(i), 0, 1);
                        if (f.end != Gauntlet::Flight::End::Landed || f.floor != fr(i + 1) || f.col < r.e + 3 || f.col > z.e - 14) continue;
                        L.set(sc, fr(i) - 1, '^');
                        pads.push_back({i, sc, f.col, true});
                        ++springs; ++padsUp;
                        break;
                    }
                } else {
                    for (int attempt = 0; attempt < 14 && pads.empty(); ++attempt) {
                        const int top = fr(i) + roll(3, 4), len = roll(12, 20), sc = roll(r.s + 8, std::max(r.s + 8, r.e - 36)), c0 = sc + roll(3, 9), c1 = std::min(r.e - 12, c0 + len - 1);
                        if (top + kBody > t || c1 - c0 < 9 || c0 <= r.s + 8) continue;
                        Grid with = L;
                        with.put(top - 2, top - 1, c0, c1, '#');
                        const std::optional<Gauntlet> pr = probe(with);
                        if (!pr) continue;
                        const Gauntlet::Flight f = pr->flightFrom(sc, fr(i), 0, 1);
                        if (f.end != Gauntlet::Flight::End::Landed || f.floor != top || f.col < c0 + 1 || f.col > c1 - 6) continue;
                        L = with;
                        L.set(sc, fr(i) - 1, '^');
                        ledges.push_back({i, c0, c1, top, false, sc, f.col});
                        pads.push_back({i, sc, f.col, false});
                        ++springs; ++padsLedge;
                    }
                }
            }
        }
        // along each stretch, her way: what the tomb has, spaced so the meter refills between what costs eggs
        for (int i = 0; i < sm; ++i) {
            const Sec& r = F.sec[size_t(i)];
            const int f0 = fr(i), hh = h - r.off;
            const int riseIn = i > 0 ? r.off - F.sec[size_t(i - 1)].off : 0, riseOut = i + 1 < sm ? F.sec[size_t(i + 1)].off - r.off : 0;
            // in: the door (the first thing takes the egg she may bring), the shaft's top, a wall she climbed (the meter refills
            // what it cost), a drop; out: the wall she climbs next (its eggs stacked in time), a drop, the gate, the exit
            int lo, hi = i + 1 == sm ? W - 11 : riseOut > 0 ? r.e - std::max(4 * riseOut + 2, gapFor(riseOut)) : r.e - 3;
            if (i == 0) lo = k == 0 ? 7 : 10;
            else if (riseIn > 0) lo = r.s + gapFor(riseIn);
            else lo = r.s + 6;
            secS = r.s; secE = r.e; gateEnd = i + 1 == sm && k + 1 < n;
            noBeam = false;
            for (const Pad& pd : pads) noBeam = noBeam || pd.sec == i || (pd.up && pd.sec == i - 1);
            int sprung = -1, after = -1; // (a spring ledge: the pad's column, and where the floor's hazards start again past the ledge)
            for (const Pad& pd : pads) {
                if (pd.sec == i && pd.up) hi = std::min(hi, pd.col - 4);        // (the pad that flies her up out of the stretch)
                if (pd.sec == i - 1 && pd.up) lo = std::max(lo, pd.land + 5);   // ...and where it lands her
            }
            for (const Ledge& l : ledges) {
                if (l.sec != i) continue;
                ++nledges;
                beams += l.beam;
                lead = l.land >= 0 ? 1 : 0;
                decorate(l.top, t, t - l.top + 1, l.land >= 0 ? std::max(l.c0 + gapFor(1), l.land + 4) : l.c0 + (l.c0 == r.s ? 3 + gapFor(1) : gapFor(1)), l.c1 - 3, 1, l.land >= 0);
                if (l.land >= 0) { sprung = l.spring; after = l.c1 + 6; }
                else lo = std::max(lo, l.c1 + 6);
            }
            const bool opening = i == 0; // (the first thing in from the door, or the shaft's top: she has no egg to spare)
            if (sprung >= 0) { // (the floor before the pad, and past the ledge's end)
                decorate(f0, t, hh, lo, sprung - 4, 1, opening);
                decorate(f0, t, hh, std::max(lo, after), hi, 1, true);
            } else {
                const bool landed = i > 0 && lo > r.s + 6 && std::any_of(pads.begin(), pads.end(), [&](const Pad& pd) { return pd.up && pd.sec == i - 1; }); // (first: a spring has just landed her with no egg to spare)
                lead = landed ? 2 : 0; // (a saw, then two steps she has just the egg for: the near misses the stacked wall gave)
                const bool ledgeHere = std::any_of(ledges.begin(), ledges.end(), [&](const Ledge& l) { return l.sec == i; });
                const bool padHere = std::any_of(pads.begin(), pads.end(), [&](const Pad& pd) { return pd.sec == i; });
                if (chunks.empty() || ledgeHere || padHere || !stamp(L, k, f0, t, hh, lo, hi, opening, landed)) decorate(f0, t, hh, lo, hi, 1, opening || landed);
            }
        }
        // set in the tower: the rows from the one under the floor to the roof
        for (int r = 0; r <= h; ++r)
            for (int u = 0; u < W; ++u) {
                const char c = L.cells[size_t(r)][size_t(u)];
                g.set(k % 2 == 0 ? u : W - 1 - u, F.b - 1 + r, k % 2 == 0 ? c : mirroredCell(c));
            }
        std::vector<int> hs;
        for (const Sec& s : F.sec) hs.push_back(s.off);
        offsets.push_back(hs);
    }
    // the shafts between the floors (the tomb's: through a gate, stacked over its wall onto the floor above)
    for (int k = 0; k + 1 < n; ++k) {
        const Flr& F = fl[size_t(k)];
        const int fe = F.b + F.sec.back().off, tk = topOf(k), tn = topOf(k + 1);
        auto col = [&](int u) { return k % 2 == 0 ? u : W - 1 - u; };
        g.put(fe, tk, col(W - 5), col(W - 5), k % 2 == 0 ? 'R' : 'L');
        g.set(col(W - 5), tk + 1, 'B');                                                     // its wall, stacked over onto the floor above
        g.put(fe, tn, std::min(col(W - 4), col(W - 2)), std::max(col(W - 4), col(W - 2)), '.'); // the shaft
        g.put(fe, tn, col(W - 1), col(W - 1), 'B');                                         // its far wall
    }
    g.set(0, 1, '>');
    // the door 3 high (its lintel on the course: she comes in with an egg at most), whatever the corridor's height
    g.put(1 + kBody + 1, topOf(0), 0, 0, '#');
    const Flr& lastF = fl.back();
    const int fx = lastF.b + lastF.sec.back().off, peak = lastF.b + (*std::max_element(offsets.back().begin(), offsets.back().end()));
    g.put(fx, topOf(n - 1), W - 1, W - 1, '#');
    g.set(W - 1, fx, 'X');
    g.set(W - 1, fx + 1, '.');
    exitLow = fx < peak;
    if (description) {
        std::string hs, ps;
        for (int k = 0; k < n; ++k) {
            hs += (k ? ", " : "") + std::to_string(fl[size_t(k)].h);
            ps += (k ? " " : "") + std::string("[");
            for (size_t i = 0; i < offsets[size_t(k)].size(); ++i) ps += (i ? " " : "") + std::to_string(offsets[size_t(k)][i]);
            ps += "]";
        }
        *description = {"A generated maze (seed " + std::to_string(seed) + "): " + std::to_string(n) + " floors (" + hs + " high), " + std::to_string(W) + " long, joined by shafts.",
                        std::to_string(saws) + " saws, " + std::to_string(beams) + " wall lasers, " + std::to_string(lava) + " cells of lava, " +
                            std::to_string(dips) + " dips in the roof, " + std::to_string(steps) + " steps, " + std::to_string(nledges) + " ledges, " +
                            std::to_string(springs) + " springs (" + std::to_string(padsUp) + " up a step in the floor, " + std::to_string(padsLedge) + " onto a ledge).",
                        "Floor path within the floors (rows over each floor's base, along its run): " + ps + "; the exit is " +
                            (exitLow ? "below the top floor's highest stretch." : "on the top floor's highest stretch.")};
        if (!label.empty()) description->push_back("chunks: " + chunkUsesLabel(label));
        if (density >= 6) description->push_back("hard " + std::to_string(density)); // (the checklist bends by it: Gauntlet::hardOf)
    }
    if (used) *used = placed;
    return g.topFirst();
}

// The game's bot through the maze, put into the level where the editors put one (40% of the way along), as the iPad's checklist
// has it play: out, and the near misses it shows as many as asked for and within 2 of the solver's count. A level the catalog has no
// text for (the generator lays it each time) isn't played: the solver's word stands
bool gameShowsItsMisses(const std::vector<std::string>& lines, int level, const LevelCatalog* cat, const GauntletReady& ready) {
    if (!cat) cat = &LevelCatalog::builtin();
    const LevelData* data = cat->level(level);
    if (!data) return true;
    auto d = EditorDoc::open(level, levelText(*data), nullptr);
    if (!d || !d->setGrid("generated_maze", lines) || !d->placeGauntlet("generated_maze", int(0.40 * d->course().length / U))) return true;
    LevelCatalog play(cat);
    if (!play.setGauntlet("generated_maze", lines) || !play.setLevel(level, d->text())) return true;
    const GauntletGame game = gauntletInGame(level, &play);
    const int shown = game.perfects + game.closeCalls;
    return game.out && shown >= ready.wanted && std::abs(shown - ready.nearMisses) <= 2;
}

// (a failure the chunks may have caused: the maze isn't solvable as laid. The ordinary ones of a short tower -- too few near
// misses, the game's count, the design's signature -- come with or without them)
static bool blamesChunk(const std::string& why) {
    return !(why.size() >= 11 && why.compare(why.size() - 11, 11, "near misses") == 0) && why.rfind("the game doesn't", 0) != 0 && why != "its design's signature";
}

MazeGenResult generateMaze(int level, int width, int floors, uint32_t seed, const LevelCatalog* cat, int tries, const MazeOptions& options) {
    MazeGenResult r;
    // (the chunks whose ranges hold the level)
    MazeOptions kept = options;
    kept.chunks.clear();
    for (const Chunk& c : options.chunks) if (c.maze && c.inRange(level)) kept.chunks.push_back(c);
    const double perEgg = gauntletCellsPerEgg(level, cat);
    // at the width asked; if none of its tries is ready (a short tower has too few near misses to find 15), at a wider one
    // (a quarter wider, and 220, 150 or 110 at the least for 3, 5 or 7 floors): the fallback
    const int n = std::clamp(floors | 1, 3, 7);
    const int wider = std::min(400, std::max(std::clamp(width, 60, 400) * 5 / 4, n == 3 ? 220 : n == 5 ? 150 : 110));
    int made = 0; // (every try, over all the stages)
    // a stage: the tries at the width asked and the wider one with these options; ends early once `hardCap` of its tries
    // (0: no cap) have failed for want of a way through, which is what a chunk the maze can't hold does
    auto stage = [&](const MazeOptions& opt, int hardCap) {
        int hard = 0;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1 && wider <= std::clamp(width, 60, 400)) break;
            for (int i = 0; i < tries; ++i) {
                if (hardCap && hard >= hardCap) return false;
                const uint32_t s = seed + uint32_t(pass * tries + i);
                std::vector<std::string> description;
                std::vector<MazeChunkUse> used;
                std::vector<std::string> rows = layMaze(pass == 1 ? wider : width, floors, s, perEgg, &description, opt, &used);
                r.tries = ++made;
                // (judged as the file would be read: its "hard N" line with the rows)
                std::vector<std::string> lines;
                for (const std::string& d : description) if (Gauntlet::hardOf("; " + d) >= 0) lines.push_back("; " + d);
                lines.insert(lines.end(), rows.begin(), rows.end());
                std::string why;
                const std::optional<Gauntlet> g = Gauntlet::parse("generated", lines, &why);
                if (!g) { r.why = why; hard += blamesChunk(why); continue; }
                const GauntletReady ready = gauntletReady(*g, level, cat);
                if (!ready.ok()) { r.why = ready.why; hard += blamesChunk(ready.why); continue; }
                if (options.game && !gameShowsItsMisses(lines, level, cat, ready)) { r.why = "the game doesn't show its near misses as the solver counts them"; continue; }
                r.ok = true;
                r.widened = pass == 1;
                r.rows = std::move(rows);
                r.description = std::move(description);
                r.chunks = std::move(used);
                r.seed = s;
                r.why.clear();
                return true;
            }
        }
        return false;
    };
    if (kept.chunks.empty()) { stage(kept, 0); return r; } // (no chunks: as it always was)
    // with chunks: as they are; if the maze can't be solved with them, fewer (half the weight, one use each); then none. A roll
    // never comes back empty because of a chunk
    constexpr int kHardFull = 12, kHardFewer = 6;
    if (stage(kept, kHardFull)) return r;
    MazeOptions fewer = kept;
    for (Chunk& c : fewer.chunks) { c.weight = std::max(1, c.weight / 2); c.uses = 1; }
    if (stage(fewer, kHardFewer)) {
        for (const Chunk& c : kept.chunks) r.chunksReduced.push_back(c.id);
        r.chunkNote = chunksLeftOutNote(r.chunksReduced, "maze", level, true);
        return r;
    }
    MazeOptions none = kept;
    none.chunks.clear();
    stage(none, 0);
    for (const Chunk& c : kept.chunks) r.chunksDropped.push_back(c.id);
    r.chunkNote = chunksLeftOutNote(r.chunksDropped, "maze", level);
    return r;
}

} // namespace cs::ed
