// Maze gauntlets (Gauntlet.h): the ASCII layouts parse (and bad ones don't, with the reason); the cells past the grid
// (the shut door, the street, the porch); the hen's moves cell by cell, by the game's rules (bumpers turn her back, gates
// let her through one way and bounce her the other, stone and cubes at her body crash her, steps rising from her floor
// are climbed with the eggs below their top knocked off, overhangs and ceilings crash her, holes drop her, saws take an
// egg or her, an egg at most every few cells); the solver (reachable by stacking alone at a human tap rate, one way
// through, no dead end that traps her, the fewest eggs); the near misses on the bot's way, counted as the game shows
// them; the bot's policy getting her out, with a real egg meter too; every gauntlet in the game: valid, solvable one
// way, unlike every other one (mirror images included), out with its level's egg meter at its hardest, and full of near
// misses; and each design's signature (the Grand Gallery's climb, the wells, the labyrinth's forks, the ascent's).
#include "TestUtil.h"

#include <algorithm>
#include "core/Campaign.h"
#include "core/Gauntlet.h"
#include "core/GauntletDesigns.h"
#include "core/LevelData.h"
#include "core/Level.h"
#include "core/LevelCatalog.h"

#include <deque>
#include <map>
#include <set>
#include <tuple>

using namespace cs;
using namespace test;

namespace {

Gauntlet must(const std::vector<std::string>& rows) {
    std::string err;
    auto g = Gauntlet::parse("test", rows, &err);
    if (!g) std::printf("  parse: %s\n", err.c_str());
    return g ? *g : *Gauntlet::parse("fallback", {"####", "....", ">..X", "####"});
}

void parsing() {
    const std::vector<std::string> ok = {
        "########",
        "#......#",
        "........",
        ">..B#..X",
        "########",
    };
    std::string err;
    auto g = Gauntlet::parse("ok", ok, &err);
    check(g.has_value() && g->cols() == 8 && g->rows() == 5 && g->entryRow() == 1 && g->exitRow() == 1, "a layout parses: its size, its doors' rows");
    check(g && g->at(3, 1) == Gauntlet::Cell::Bumper && g->at(4, 1) == Gauntlet::Cell::Stone && g->at(1, 1) == Gauntlet::Cell::Air,
          "...its cells (row 0 at the bottom)");
    check(g && g->rowsTopFirst() == ok, "...and writes back as it was written");
    check(g && g->entryTop() == 3, "...its entry's lintel: the first row above the door's opening");
    const std::vector<std::string> kinds = {
        "########",
        "#......#",
        "........",
        ">.HRLSCX",
        "########",
    };
    auto k = Gauntlet::parse("kinds", kinds, &err);
    check(k && k->at(2, 1) == Gauntlet::Cell::Cube && k->at(3, 1) == Gauntlet::Cell::GateR && k->at(4, 1) == Gauntlet::Cell::GateL &&
              k->at(5, 1) == Gauntlet::Cell::Saw && k->at(6, 1) == Gauntlet::Cell::CubeSaw && k->rowsTopFirst() == kinds,
          "...cubes, gates both ways, saws in stone and in cubes");
    check(k && k->solid(2, 1) && !k->solid(3, 1) && k->blocks(3, 1, -1) && !k->blocks(3, 1, 1) && k->bounces(4, 1, 1) && !k->bounces(2, 1, 1),
          "...a gate is never solid (no floor, no roof); it's in her way only running against it, and bounces her");
    check(g && g->at(-1, 2) == Gauntlet::Cell::Bumper && g->at(3, -1) == Gauntlet::Cell::Stone && g->at(8, 0) == Gauntlet::Cell::Stone &&
              g->at(8, 1) == Gauntlet::Cell::Air && g->at(2, 9) == Gauntlet::Cell::Stone,
          "past the grid: the shut door (a bumper), the street below, the porch up to the exit's floor, the roof");
    auto bad = [&](std::vector<std::string> rows, const char* why) {
        std::string e;
        const bool parsed = Gauntlet::parse("bad", rows, &e).has_value();
        check(!parsed && e.find(why) != std::string::npos, (std::string("rejected: ") + why).c_str());
    };
    bad({"########", "#......#", "........", ">..Z#..X", "########"}, "unknown cell");
    bad({"########", "#......#", ".......", ">..B#..X", "########"}, "wide");
    bad({"########", "#......#", "........", "#..B#..X", "########"}, "exactly one entry");
    bad({"########", "#......#", "#.......", ">..B#..X", "########"}, "too low");
    bad({"########", "#......#", "........", ">..B#..X", ".#######"}, "no floor");
    bad({"####.###", "#......#", "........", ">..B#..X", "########"}, "top row");
    bad({"########", ".......#", "#......#", "........", ">..B#..X", "########"}, "open at row");
}

using M = Gauntlet::Move;
using S = Gauntlet::State;

void moves() {
    // a corridor (her floor row 1's top: b = 2) with, along it: a 1-high bumper, a 1-high stone wall, a floor saw, a
    // hole, and a floating block; a roof over her head
    const Gauntlet g = must({
        "####################",
        "#..................#",
        "#..................#",
        "................#...",
        ">..B...#...........X",
        "#########S##..######",
        "####################",
    });
    S o;
    check(g.step({1, 2, 0, 1}, 0, o) == M::Ok && o.c == 2 && o.b == 2 && o.n == 0, "a free cell: she moves on");
    check(g.step({2, 2, 0, 1}, 0, o) == M::Bounce && o.c == 2 && o.d == -1, "a bumper at her body: she turns back where she is");
    check(g.step({2, 2, 1, 1}, 0, o) == M::Ok && o.c == 3 && o.b == 3 && o.n == 0, "...stacked over it, she climbs it like a step (its egg knocked off)");
    check(g.step({6, 2, 0, 1}, 0, o) == M::Crash, "stone at her body: a crash");
    check(g.step({6, 2, 0, 1}, 1, o) == M::Ok && o.c == 7 && o.b == 3 && o.n == 0, "...an egg laid in time climbs it");
    check(g.step({8, 2, 0, 1}, 0, o) == M::Crash, "a saw in the floor with no egg under her: it gets her");
    check(g.step({8, 2, 1, 1}, 0, o) == M::Ok && o.c == 9 && o.n == 0, "...with one, it takes the egg");
    check(g.step({11, 2, 0, 1}, 0, o) == M::Ok && o.c == 12 && o.b == 1, "a hole: she drops straight down it to the floor below");
    check(g.step({15, 2, 2, 1}, 0, o) == M::Crash, "a floating block in the way of her stack: a crash");
    check(g.step({1, 2, 2, 1}, 0, o) == M::Ok && g.step({1, 2, 2, 1}, 1, o) == M::Crash, "stacking into the roof: a crash");
    check(g.step({18, 2, 0, 1}, 0, o) == M::Ok && g.step({19, 2, 0, 1}, 0, o) == M::Exit, "out through the exit");
    check(g.step({0, 2, 0, -1}, 0, o) == M::Bounce && o.d == 1, "back at the entry: its door is shut (it turns her back)");
    // an egg at most every kLayGap cells (a human's tap rate at full speed)
    S laid, on;
    check(g.step({1, 2, 0, 1}, 1, laid) == M::Ok && laid.n == 1 && laid.k == Gauntlet::kLayGap - 1 && g.step(laid, 1, on) == M::Crash,
          "an egg laid: another too soon after it isn't allowed (the solver counts it as a crash)");
    check(g.step({4, 2, 1, 1, 1}, 0, on) == M::Ok && on.k == 0 && g.step(on, 1, on) == M::Ok && on.n == 2, "...one every kLayGap cells is");
    // a gate each way and a cube, along a corridor
    const Gauntlet h = must({
        "##############",
        "#............#",
        "..............",
        ">..R..L..H...X",
        "##############",
    });
    check(h.step({2, 1, 0, 1}, 0, o) == M::Ok && o.c == 3 && o.b == 1, "a gate her way: she runs through it (it's no floor, she stays on hers)");
    check(h.step({4, 1, 0, -1}, 0, o) == M::Bounce && o.c == 4 && o.d == 1, "...back the other way it bounces her, as a bumper");
    check(h.step({5, 1, 0, 1}, 0, o) == M::Bounce && o.d == -1, "a gate set against her: a bumper");
    check(h.step({8, 1, 0, 1}, 0, o) == M::Crash, "a cube at her body: a crash, as stone");
    check(h.step({8, 1, 1, 1}, 0, o) == M::Ok && o.c == 9 && o.b == 2 && o.n == 0, "...stacked, she climbs it, as stone");
}

void solver() {
    // a 2-high step up to the exit's floor (a run-up long enough to lay two eggs at the tap rate, past the door's low
    // frame): reachable by stacking alone, one way through, two eggs
    const Gauntlet step = must({
        "#############",
        "#...........#",
        "#............",
        "#...........X",
        ".......######",
        ">......######",
        "#############",
    });
    const Gauntlet::Solution a = step.solve();
    check(a.reachable && a.routes == 1 && a.softLocks == 0 && a.minEggs == 2, "solver: a step up to the exit: reachable, one way, 2 eggs");
    // ...but not with too short a run-up to lay them both
    const Gauntlet rushed = must({
        "#########",
        "#.......#",
        "#........",
        "#.......X",
        "...######",
        ">..######",
        "#########",
    });
    check(!rushed.solve().reachable, "...nor with a run-up too short to lay two (a tap rate no human has)");
    // a wall as tall as the room: no way through
    const Gauntlet wall = must({
        "########",
        "#..#...#",
        "...#....",
        ">..#...X",
        "########",
    });
    check(!wall.solve().reachable, "solver: a wall as tall as the room: no way through");
    // a hole into a covered pit with bumpers at its ends: she can't stack out under its lid, and bounces in it
    // forever (a trap)
    const Gauntlet pit = must({
        "##########",
        "#........#",
        "..........",
        ">........X",
        "####.#####",
        "#B.....B##",
        "#B.....B##",
        "#B.....B##",
        "##########",
    });
    const Gauntlet::Solution p = pit.solve();
    check(!p.reachable && p.softLocks > 0, "solver: a pit she'd bounce in forever is found (a trap; every gauntlet must have none)");
}

using NM = Gauntlet::NearMiss;

std::vector<NM> kinds(const Gauntlet& g) {
    std::vector<NM> v;
    for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(g.botTrace())) v.push_back(m.kind);
    return v;
}

// The near misses as the game shows them (Game::perfect, closeCall, endSqueeze): a squeeze (her head brushing what's
// over her, two cells or more) is a CLOSE CALL when she has room again, unless a PERFECT or a CLOSE SHAVE came in it;
// a blade taking her last egg is a CLOSE SHAVE; a climb with no egg to spare a PERFECT
void nearMissCounting() {
    // a block hanging to her head, four cells long: she runs under it with no egg (a CLOSE CALL)
    const Gauntlet duck = must({
        "##########",
        "#...####.#",
        "#...####.#",
        "..........",
        ">........X",
        "##########",
    });
    check(kinds(duck) == std::vector<NM>{NM::CloseCall} && duck.solve().nearMisses == 1, "near misses: a squeeze under a hanging block is a CLOSE CALL");
    // a passage with room for one egg, then a step one high: the egg she carries brushes the roof all the way, and
    // climbs the step exactly (a PERFECT, and the squeeze it ends isn't a CLOSE CALL too)
    const Gauntlet step = must({
        "##########",
        "#####....#",
        "..........",
        ".........X",
        ">....#####",
        "##########",
    });
    check(kinds(step) == std::vector<NM>{NM::Perfect}, "...a squeeze ended by a climb with no egg to spare: a PERFECT, not a CLOSE CALL too");
    // the same passage over a blade: it takes the egg she carries (a CLOSE SHAVE, not a CLOSE CALL too)
    const Gauntlet blade = must({
        "##########",
        "..........",
        "..........",
        ">........X",
        "#####S####",
    });
    check(kinds(blade) == std::vector<NM>{NM::Shave}, "...a squeeze ended by a blade taking her last egg: a CLOSE SHAVE, not a CLOSE CALL too");
    // with room for two over the blade she carries one, and it's still her last (the bot lays no egg it doesn't need)
    const Gauntlet roomy = must({
        "##########",
        "..........",
        "..........",
        "..........",
        ">........X",
        "#####S####",
    });
    check(kinds(roomy) == std::vector<NM>{NM::Shave}, "...a blade under a high roof: a CLOSE SHAVE alone");
}

// Each design's signature, on the bot's way (GauntletDesigns.cpp; docs/LEVEL_DESIGN.md): the Grand Gallery's climb,
// the wells, the labyrinth's forks, the ascent's passage, chamber and shaft
void designs() {
    for (const char* name : {"gallery", "wells", "labyrinth", "ascent"}) {
        const Gauntlet* g = gauntletNamed(name);
        if (!g) { check(false, (std::string("the ") + name + " is in the game").c_str()); continue; }
        for (const DesignCheck& d : designChecks(*g)) check(d.ok, (d.what + " (" + d.why + ")").c_str());
    }
}

// Every level with a gauntlet (its data says which: assets/levels): the bot gets out with that level's egg meter at its
// hardest (only the 2 eggs the forecourt refills at the door, refilling 1.25 times slower: the adaptive difficulty at
// its hardest slows it about 1.2 times), and there are near misses all the way (15 or more)
void levels() {
    std::string slow, quiet;
    int gauntlets = 0;
    for (int level = 1; level <= 30; ++level) {
        const Gauntlet* g = Campaign::gauntletOf(level);
        if (!g) continue;
        ++gauntlets;
        Level l;
        l.setDifficulty(35.0 * (level - 1), 0.35);
        if (const LevelData* d = levelData(level)) l.setDifficulty(d->diffOffset, d->diffScale);
        const Curve cv = l.curveAt(Campaign::gauntletAt(level));
        if (g->botRunWithMeter(cv.speed * cv.regen / U * 1.25, 2) == 0) slow += " " + std::to_string(level);
        const int near = g->solve().nearMisses;
        std::printf("      level %d, the %s: %d near misses, %.0f s inside\n", level, g->name().c_str(), near, g->solve().moves * U / cv.speed);
        if (near < 15) quiet += " " + std::to_string(level);
    }
    if (!slow.empty()) std::printf("  short of eggs (level):%s\n", slow.c_str());
    if (!quiet.empty()) std::printf("  too few near misses (level):%s\n", quiet.c_str());
    check(gauntlets > 0, "the levels have their gauntlets (assets/levels)");
    check(slow.empty(), "...each one out with its level's egg meter at its hardest, from the forecourt's 2 eggs");
    check(quiet.empty(), "...and full of near misses: 15 or more on the bot's way through each");
}

void tomb() {
    const Gauntlet* g = gauntletNamed("tomb");
    check(g != nullptr && g->cols() == 168 && g->rows() == 30, "the tomb (level 11): in the game, 168 x 30");
    if (!g) return;
    const Gauntlet::Solution s = g->solve();
    std::printf("      tomb: %d route cells, %d dead-end cells, %d eggs at the least, %d cells crossed\n", s.routeCells, s.deadEndCells, s.minEggs, s.moves);
    check(s.reachable && s.routes == 1 && s.softLocks == 0, "...one way through, by stacking alone, nowhere she's trapped");
    // the tunnel's 4 saws; the shaft's 5; the middle floor's 2 saws; the second shaft's 8; the hall's 2 saws, cube wall
    // and its saw (5), the bay's 9, stairs (3) and 2 saws; the lower floor's cube step and its saw (3), 2 saws, stone
    // step and its saw (3); the bottom hall's 7 saws and cube step (2)
    check(s.minEggs == 57, "...57 eggs at the least");
    // the bot's policy takes her out
    Gauntlet::State st = g->start();
    int laid = 0, moves = 0;
    bool out = false;
    for (; moves < 2000 && !out; ++moves) {
        const int lay = g->shouldLay(st) ? 1 : 0;
        laid += lay;
        Gauntlet::State o;
        const Gauntlet::Move m = g->step(st, lay, o);
        if (m == Gauntlet::Move::Crash) break;
        out = m == Gauntlet::Move::Exit;
        st = o;
    }
    check(out && laid == s.minEggs, "...the bot's policy takes her out, laying no egg she doesn't need");
    // ...and with a real egg meter: only the eggs the forecourt refills at the door, and the meter refilling 1.25 times
    // slower than level 11's (the adaptive difficulty at its hardest slows it about 1.2 times): never short of an egg on
    // a floor (she can wait for one only in a shaft or a bay, bouncing)
    FakeStorage storage = FakeStorage::ready();
    Campaign c(&storage);
    c.debugSetLevel(11);
    Level l;
    c.applyTo(l);
    const Curve cv = l.curveAt(Campaign::gauntletAt(11));
    const double cellsPerEgg = cv.speed * cv.regen / U; // (the meter: an egg back every `regen` seconds)
    check(g->botRunWithMeter(cellsPerEgg * 1.25, 2) > 0, "...and with level 11's egg meter, 2 eggs at the door, refilling 1.25 times slower");
}

void registry() {
    std::set<std::string> prints;
    bool valid = true, solvable = true;
    for (const std::string& name : gauntletNames()) {
        const Gauntlet* g = gauntletNamed(name);
        valid = valid && g != nullptr;
        if (!g) continue;
        const Gauntlet::Solution s = g->solve();
        solvable = solvable && s.reachable && s.routes == 1 && s.softLocks == 0;
        prints.insert(g->fingerprint());
    }
    check(!gauntletNames().empty() && valid, "every gauntlet in the game parses");
    check(solvable, "...each one way through, by stacking alone, with no trap");
    check(prints.size() == gauntletNames().size(), "...and no two alike (mirror images included)");
    // the fingerprint can't be fooled by a mirror image
    const Gauntlet* g = gauntletNamed("tomb");
    if (!g) return;
    std::vector<std::string> mirrored;
    for (std::string row : g->rowsTopFirst()) {
        std::string r(row.rbegin(), row.rend());
        for (char& ch : r) ch = ch == '>' ? 'X' : ch == 'X' ? '>' : ch == 'R' ? 'L' : ch == 'L' ? 'R' : ch; // (its gates turned round)
        mirrored.push_back(r);
    }
    // (a mirror image has its doors on the wrong sides to parse as a gauntlet; its fingerprint is compared as written)
    std::string a, m;
    for (const std::string& r : g->rowsTopFirst()) a += r + '/';
    for (const std::string& r : mirrored) m += r + '/';
    check(g->fingerprint() == std::min(a, m), "...a mirror image has the same fingerprint as the original");
}

} // namespace

// wall lasers ('(' and ')': stone with a laser in its left or right face, its beam along its row through the air) and
// lava ('~': a floor) in a maze. A laser hits her once a pass (zapped till she's out of every beam's columns): the
// first time its row meets her, an egg there is cut out (the egg right under her: a CLOSE SHAVE), her body there
// crashes her (cut in half); falling down through it, her bottom egg meets it first. Its beam just over her head as she
// comes into its reach brushes it (a CLOSE CALL). Lava burns an egg a cell (none: a crash, her last: a CLOSE SHAVE);
// the solver and the bot take them through
void hazards() {
    // a laser in a wall's right face, at its foot, firing right along the floor she drops onto
    const std::vector<std::string> drop = {
        "##########",
        "#........#",
        "#........#",
        "#........#",
        "...#......",
        ">..).....X",
        "##########",
    };
    std::string err;
    const auto f = Gauntlet::parse("drop", drop, &err);
    check(f.has_value() && f->rowsTopFirst() == drop, ("a laser in a face parses and writes back " + err).c_str());
    if (!f) return;
    check(f->solid(3, 1) && f->beamAt(4, 1) && f->beamAt(9, 1) && !f->beamAt(2, 1) && f->beamColumn(5) && !f->beamColumn(3),
          "its cell is solid; its beam runs along its row from its face, the way it fires");
    S o;
    Gauntlet::StepInfo in;
    check(f->step({3, 3, 0, 1}, 0, o, &in) == M::Crash, "dropping down through its beam with no egg: cut in half");
    check(f->step({3, 3, 1, 1}, 0, o, &in) == M::Ok && o.b == 1 && o.n == 0 && o.z == 1 && in.cut == 1 && in.lastEgg, "...with one: her bottom egg cut out, a CLOSE SHAVE");
    check(f->step({4, 1, 0, 1, 0, 1}, 0, o, &in) == M::Ok && o.z == 1, "...then it's done with her this pass (her body in it)");
    check(f->step({4, 1, 0, 1, 0, 0}, 0, o, &in) == M::Crash, "in it, not hit yet, her body in its row: cut in half");
    check(f->step({4, 1, 2, 1, 0, 0}, 0, o, &in) == M::Ok && o.n == 1 && in.cut == 1 && !in.lastEgg, "...an egg lower down in it: cut out, the rest drop");

    // a laser in a hanging pillar's left face, over her head, firing back along the hall; then lava
    const std::vector<std::string> rows = {
        "######################",
        "#............##......#",
        "#............##......#",
        "#............(.......#",
        "......................",
        ">....................X",
        "###################~~#",
    };
    const auto g = Gauntlet::parse("hazards", rows, &err);
    check(g.has_value() && g->rowsTopFirst() == rows, ("a laser and lava parse and write back " + err).c_str());
    if (!g) return;
    check(g->beamAt(1, 3) && g->beamAt(12, 3) && !g->beamAt(0, 3) && g->lavaAt(19, 0) && g->solid(19, 0), "its beam stops at the first solid cell; lava is a floor");
    check(g->step({0, 1, 0, 1}, 0, o, &in) == M::Ok && in.brush, "coming into its reach with its beam just over her head: a CLOSE CALL");
    check(g->step({3, 1, 2, 1}, 0, o, &in) == M::Crash, "...her body in its row: cut in half");
    check(g->step({3, 1, 3, 1}, 0, o, &in) == M::Ok && o.n == 2 && in.cut == 1 && in.lastEgg, "...the egg right under her in it: cut out, a CLOSE SHAVE");
    check(g->step({12, 1, 0, 1, 0, 1}, 0, o, &in) == M::Ok && o.z == 0, "out of its reach: it can hit her again");
    check(g->step({3, 1, 0, 1, 0, 0}, 1, o, &in) == M::Crash, "an egg laid in its reach lifting her body into its row: cut in half where she stands");
    check(g->step({3, 1, 0, 1, 0, 1}, 1, o, &in) == M::Ok, "...not once it's had her this pass");
    check(!Gauntlet::parse("old", {"####", "#..#", ">=.X", "####"}, &err) && err.find("unknown cell") != std::string::npos, "the old beam cell ('=') is gone");
    check(g->step({18, 1, 2, 1}, 0, o, &in) == M::Ok && o.n == 1 && in.burned && !in.lastEgg, "lava: an egg burned as she comes onto a cell of it");
    check(g->step({19, 1, 1, 1}, 0, o, &in) == M::Ok && o.n == 0 && in.burned && in.lastEgg, "...her last: a CLOSE SHAVE");
    check(g->step({19, 1, 0, 1}, 0, o, &in) == M::Crash, "...with none: she burns up");
    const Gauntlet::Solution sol = g->solve();
    check(sol.reachable && !g->botTrace().empty(), "the solver gets her through (short under the beam), the bot's way too");
    int shaves = 0, calls = 0;
    for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(g->botTrace())) { shaves += m.kind == Gauntlet::NearMiss::Shave; calls += m.kind == Gauntlet::NearMiss::CloseCall; }
    check(shaves >= 1 && calls >= 1, "...its near misses counted");
    auto mirror = [](std::vector<std::string> m) {
        for (std::string& r : m) { std::reverse(r.begin(), r.end()); for (char& c : r) c = c == '>' ? 'X' : c == 'X' ? '>' : c == '(' ? ')' : c == ')' ? '(' : c; }
        return m;
    };
    check(g->fingerprint() == Gauntlet::parse("mirror", mirror(rows))->fingerprint(), "...and its mirror image (its lasers facing the other way) is the same layout");

    // on the course: the maze's lasers join the level's, in their cells' faces, their reach the grid's beams
    LevelCatalog cat;
    std::string why;
    check(cat.setGauntlet("drop", drop, &why) && cat.setLevel(4, "length 30\ncourse\n10 0 crate\n10 0 gauntlet drop\n40 0 crate\n", &why), ("a level laying it " + why).c_str());
    Level l;
    l.load(*cat.level(4), &cat);
    l.generateUntil(40);
    const double x0 = 10 * U; // (the maze's left edge)
    check(l.lasers.size() == 1 && std::abs(l.lasers[0].x - (x0 + 4 * U)) < 1e-9 && l.lasers[0].dir == 1 && l.lasers[0].row == 1 && l.lasers[0].traced() &&
              std::abs(l.lasers[0].x0 - (x0 + 4 * U)) < 1e-9 && std::abs(l.lasers[0].x1 - (x0 + 10 * U)) < 1e-9,
          "the maze's laser on the course: in its cell's right face, firing right along its row to the maze's end");
}

// a room to fly in: W wide, H rows of air over a street of stone (row 0), its door and exit at row 1; `set` puts cells in
// (column, row, char)
std::vector<std::string> room(int W, int H, const std::vector<std::tuple<int, int, char>>& set = {}) {
    std::vector<std::string> bottomUp(size_t(H + 1), std::string(size_t(W), '.'));
    bottomUp[0] = std::string(size_t(W), '#');
    bottomUp[size_t(H)] = std::string(size_t(W), '#');
    for (int r = 1; r < H; ++r) bottomUp[size_t(r)][0] = bottomUp[size_t(r)][size_t(W - 1)] = '#';
    bottomUp[1][0] = '>'; bottomUp[2][0] = '.';
    bottomUp[1][size_t(W - 1)] = 'X'; bottomUp[2][size_t(W - 1)] = '.';
    for (const auto& [c, r, ch] : set) bottomUp[size_t(r)][size_t(c)] = ch;
    return {bottomUp.rbegin(), bottomUp.rend()};
}

// springs (Gauntlet::flightFrom): a pad she's launched off, up kSpringPower rows and on at kFlightSpeed; the solver flies the
// arc inside one step, so the bot's way jumps from the pad to where she lands
void springs() {
    using F = Gauntlet::Flight;
    check(Gauntlet::kSpringPower == 5 && Gauntlet::kFlightSpeed == 8.0, "a spring: 5 rows up, 8 m/s along");
    const auto open = room(40, 9, {{6, 0, '^'}});
    const Gauntlet g = must(open);
    check(g.at(6, 0) == Gauntlet::Cell::Spring && g.springAt(6, 0) && g.solid(6, 0) && g.rowsTopFirst() == open, "a '^' reads as a spring, a floor, and writes back as it was");
    check(g.fingerprint() == must(room(40, 9, {{6, 0, '^'}})).fingerprint(), "...and a layout with one is as itself");
    F f = g.flightFrom(6, 1, 0, 1);
    check(f.end == F::End::Landed && f.floor == 1 && f.dir == 1 && f.col == 17 && std::abs(f.seconds - 0.8167) < 0.01, "an open room: flown up and over, down on the floor 11 cells on (0.8 s)");
    check(f.bounces.empty() && std::abs(f.xAt(0) - 6.5) < 1e-9 && f.rowAt(0) == 1 && std::abs(f.rowAt(f.seconds / 2) - 6.0) < 0.05, "...from the pad's middle, to 5 rows above it at the top of the arc");
    check(std::abs(f.xAt(f.seconds) - (6.5 + Gauntlet::kFlightSpeed / 0.6 * f.seconds)) < 1e-9, "...running at 8 m/s the whole way");
    Gauntlet::State o;
    Gauntlet::StepInfo in;
    check(g.step({5, 1, 0, 1, 0, 0}, 0, o, &in) == Gauntlet::Move::Ok && in.flew && o.c == 17 && o.b == 1 && o.n == 0 && o.d == 1 && o.k == 0,
          "a step onto the pad flies her in that step: where she lands, no egg laid in the air, the cooldown over");
    check(g.step({5, 1, 1, 1, 0, 0}, 0, o, &in) == Gauntlet::Move::Ok && o.n == 1 && o.c == 17, "...her stack along with her");
    check(g.step({5, 1, 7, 1, 0, 0}, 0, o) == Gauntlet::Move::Crash, "...a stack so tall her head meets the roof in the arc crashes her");
    // a wall in the way: the arc ends at its face
    const Gauntlet wall = must(room(40, 9, {{6, 0, '^'}, {12, 1, '#'}, {12, 2, '#'}, {12, 3, '#'}, {12, 4, '#'}, {12, 5, '#'}, {12, 6, '#'}, {12, 7, '#'}}));
    f = wall.flightFrom(6, 1, 0, 1);
    check(f.end == F::End::Wall && wall.step({5, 1, 0, 1, 0, 0}, 0, o) == Gauntlet::Move::Crash, "a wall as high as the room in the arc's way: she crashes into its face");
    // a low wall: over it
    const Gauntlet low = must(room(40, 9, {{6, 0, '^'}, {12, 1, '#'}, {12, 2, '#'}}));
    f = low.flightFrom(6, 1, 0, 1);
    check(f.end == F::End::Landed && f.col == 17, "a wall 2 high: flown over");
    // a ledge: 3 rows up, 12 cells long: she comes down on it, and the bot runs along it and off its end
    std::vector<std::tuple<int, int, char>> set = {{5, 0, '^'}};
    for (int c = 12; c <= 23; ++c) { set.push_back({c, 2, '#'}); set.push_back({c, 3, '#'}); }
    const Gauntlet ledge = must(room(34, 9, set));
    f = ledge.flightFrom(5, 1, 0, 1);
    check(f.end == F::End::Landed && f.floor == 4 && f.col == 14, "a ledge 3 rows up: landed on its top, 9 cells on");
    check(ledge.step({4, 1, 0, 1, 0, 0}, 0, o, &in) == Gauntlet::Move::Ok && in.flew && o.c == 14 && o.b == 4, "...in one step of the solver's");
    const Gauntlet::Solution sol = ledge.solve();
    bool flew = false;
    int atLedge = 0;
    for (const Gauntlet::TraceStep& t : ledge.botTrace()) { flew = flew || t.info.flew; atLedge += t.s.b == 4; }
    check(sol.reachable && sol.routes == 1 && sol.softLocks == 0 && flew && atLedge >= 8, "...the maze is solved over it (one way through, no traps): the bot flies, runs the ledge, drops off its end");
    // a ledge just too high (the arc's top is 5 rows over the floor: the ledge's top 6)
    set = {{5, 0, '^'}};
    for (int c = 12; c <= 23; ++c) { set.push_back({c, 3, '#'}); set.push_back({c, 4, '#'}); set.push_back({c, 5, '#'}); }
    check(must(room(34, 10, set)).flightFrom(5, 1, 0, 1).end != F::End::Landed, "a ledge 5 rows up (or more): her stack meets its side, no landing");
    // a bumper in the way: she's turned back in the air, and comes down behind it
    const Gauntlet bump = must(room(40, 9, {{6, 0, '^'}, {14, 3, 'B'}, {14, 4, 'B'}, {14, 5, 'B'}, {14, 6, 'B'}, {14, 7, 'B'}}));
    f = bump.flightFrom(6, 1, 0, 1);
    check(f.end == F::End::Landed && f.dir == -1 && f.bounces.size() == 1 && f.bounces[0].c == 14 && f.col >= 8 && f.col <= 11 && f.floor == 1,
          "a bumper in the arc's way: bounced back in the air, landing the other way, between the pad and the bumper");
    check(f.xAt(f.seconds) < f.xAt(f.bounces[0].t) && std::abs(f.xAt(f.bounces[0].t) - (14 - 0.22 / 0.6)) < 0.1, "...her x turning round at the bumper's face");
    check(bump.step({5, 1, 0, 1, 0, 0}, 0, o, &in) == Gauntlet::Move::Ok && in.flew && o.d == -1 && o.c == f.col, "...the solver has her running left after it");
    // out of the question: past the exit, on a blade, lava or another pad, a blade in the arc, a beam across it
    check(must(room(14, 9, {{6, 0, '^'}})).flightFrom(6, 1, 0, 1).end == F::End::Wall, "a spring too near the exit: the arc never comes down inside (not out by air)");
    check(must(room(40, 9, {{6, 0, '^'}, {17, 0, 'S'}})).flightFrom(6, 1, 0, 1).end == F::End::Wall, "...a blade where she'd land: no");
    check(must(room(40, 9, {{6, 0, '^'}, {17, 0, '~'}})).flightFrom(6, 1, 0, 1).end == F::End::Wall, "...lava where she'd land: no");
    check(must(room(40, 9, {{6, 0, '^'}, {17, 0, '^'}})).flightFrom(6, 1, 0, 1).end == F::End::Wall, "...another spring where she'd land: no");
    check(must(room(40, 9, {{6, 0, '^'}, {7, 0, 'S'}})).flightFrom(6, 1, 0, 1).end == F::End::Wall, "...a blade right by the pad: no");
    check(must(room(40, 9, {{6, 0, '^'}, {14, 3, ')'}})).flightFrom(6, 1, 0, 1).end == F::End::Wall, "...a laser's beam across the landing: no");
    check(must(room(40, 9, {{6, 0, '^'}, {20, 0, 'S'}})).flightFrom(6, 1, 0, 1).end == F::End::Landed, "a blade past where she lands is a blade like any other: no bother to the arc");
    // a pad on the other side of the bot's way, running left, flies her left
    const Gauntlet leftward = must(room(40, 9, {{30, 0, '^'}}));
    f = leftward.flightFrom(30, 1, 0, -1);
    check(f.end == F::End::Landed && f.dir == -1 && f.col == 19, "running left: flown left, down 11 cells back");
}

int main() {
    designs();  // (the game's own gauntlets: what each must be)
    levels();
    registry();
    test::useFixtures(); // (from here: the gauntlets these tests are written round, tests/fixtures)
    parsing();
    moves();
    solver();
    nearMissCounting();
    tomb();
    hazards();
    springs();
    return finish("Gauntlet");
}
