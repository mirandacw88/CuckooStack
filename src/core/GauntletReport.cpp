#include "GauntletReport.h"

#include "Campaign.h"
#include "Game.h"
#include "GauntletDesigns.h"
#include "Level.h"
#include "LevelCatalog.h"
#include "LevelData.h"
#include "MemoryServices.h"
#include "ReportJson.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <set>
#include <tuple>
#include <vector>

namespace cs {

namespace {

constexpr double kAtDoor = 2;                       // eggs in the meter at the door (the forecourt's refill)
constexpr double kRates[] = {1.0, 1.25, 1.4};       // the meter refilling this many times slower than the level's

void say(std::string& o, const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    o += b;
}

// the level's refill in cells per egg (Campaign::applyTo's difficulty, at the gauntlet's place on the course)
double cellsPerEgg(int level, double* speed, const LevelCatalog* cat) {
    Level l;
    l.setDifficulty(35.0 * (level - 1), 0.35);
    if (const LevelData* d = cat->level(level)) l.setDifficulty(d->diffOffset, d->diffScale);
    const Curve c = l.curveAt(Campaign::gauntletAt(level, cat));
    if (speed) *speed = c.speed;
    return c.speed * c.regen / U;
}

// the bot with a real egg meter (Gauntlet::botTraceWithMeter's rules), kept even when it crashes: each move, and the
// eggs in the meter as it made it
struct MeterRun {
    bool out = false;
    int waiting = 0;
    std::vector<Gauntlet::TraceStep> steps;
    std::vector<double> meter;
};
MeterRun runWithMeter(const Gauntlet& g, double perEgg, double atDoor) {
    MeterRun r;
    Gauntlet::State s = g.start();
    double meter = atDoor;
    for (int moves = 1; moves < 50 * g.cols() * g.rows(); ++moves) {
        meter = std::min(8.0, meter + 1.0 / perEgg);
        const bool want = g.shouldLay(s);
        Gauntlet::TraceStep t{s, want && meter >= 1 ? 1 : 0, Gauntlet::Move::Ok, {}};
        r.waiting += want && !t.lay;
        meter -= t.lay;
        Gauntlet::State o;
        t.move = g.step(s, t.lay, o, &t.info);
        r.steps.push_back(t);
        r.meter.push_back(meter);
        if (t.move == Gauntlet::Move::Crash) return r;
        if (t.move == Gauntlet::Move::Exit) { r.out = true; return r; }
        s = o;
    }
    return r;
}

// every cell her feet can get to, stacking any way she can
std::set<std::pair<int, int>> reachable(const Gauntlet& g) {
    std::set<std::pair<int, int>> cells;
    std::set<std::tuple<int, int, int, int, int>> seen;
    std::deque<Gauntlet::State> q{g.start()};
    while (!q.empty()) {
        const Gauntlet::State s = q.front();
        q.pop_front();
        if (!seen.insert({s.c, s.b, s.n, s.d, s.k}).second) continue;
        cells.insert({s.c, s.b});
        for (int lay = 0; lay <= 1; ++lay) {
            Gauntlet::State o;
            const Gauntlet::Move m = g.step(s, lay, o);
            if (m == Gauntlet::Move::Ok || m == Gauntlet::Move::Bounce) q.push_back(o);
        }
    }
    return cells;
}

// where the gauntlet is on its level's course: as the level is authored (assets/levels: placed if the level has it),
// or past the authored levels where the generator lays it (Level::placeGauntlet), and whether the run keeps its saws
struct CoursePlace { bool placed = false, authored = false; double x0 = 0, x1 = 0, finish = 0; int saws = 0, sawsWanted = 0; };
CoursePlace place(const Gauntlet& g, int level, const LevelCatalog* cat) {
    CoursePlace p;
    Level l;
    if (const LevelData* d = cat->level(level)) {
        p.authored = true;
        l.load(*d, cat);
        l.generateUntil(d->length + 20);
    } else {
        l.reset(Campaign::seedOf(level));
        l.setDifficulty(35.0 * (level - 1), 0.35);
        l.setFinish(Campaign::lengthOf(level, cat));
        l.setSaws(Campaign::sawsOf(level), Campaign::sawRowOf(level));
        l.setGauntlet(&g, Campaign::gauntletAt(level, cat));
        l.generateUntil(l.finish() + 20);
        p.sawsWanted = Campaign::sawsOf(level);
    }
    p.finish = l.finish();
    for (const Saw& w : l.saws) p.saws += l.gauntletAt(w.x) == nullptr && w.x < p.finish;
    if (p.authored) p.sawsWanted = p.saws; // (an authored level's saws are its own)
    for (const PlacedGauntlet& pg : l.gauntlets)
        if (pg.g->name() == g.name()) { p.placed = true; p.x0 = pg.x0; p.x1 = pg.x1(); }
    return p;
}

// the level played in the real game, headless, the bot playing from just before the gauntlet (as GameGauntlet_test)
struct GauntletRun {
    bool entered = false, out = false, cleared = false;
    double seconds = 0, inside = 0;
    int perfects = 0, closeCalls = 0;
    double crashCol = -1, crashRow = -1;
};
GauntletRun playInGame(int level, const LevelCatalog* cat) {
    GauntletRun r;
    MemoryStorage st = MemoryStorage::ready();
    FixedClock clock;
    Campaign(&st, cat).debugSetLevel(level);
    GameServices svc;
    svc.storage = &st; svc.clock = &clock; svc.levels = cat; // (no ads: none ready)
    Game g(svc);
    g.debugSeed(1);
    g.setHeadless(true);
    g.debugSetBot({true, false, false, 0});
    g.debugNearGauntlet();
    g.press();
    int p0 = 0, c0 = 0, frames = 0;
    double door = 0, t0 = 0;
    for (; frames < 60 * 180 && g.state() == Game::State::Play; ++frames) {
        g.update(1.0 / 60.0);
        if (g.inGauntlet() && !r.entered) {
            r.entered = true;
            door = g.course().gauntlets.front().x0;
            p0 = g.perfects(); c0 = g.closeCalls(); t0 = frames / 60.0;
        }
        if (r.entered && !g.inGauntlet() && !r.out) {
            r.out = true;
            r.inside = frames / 60.0 - t0;
            r.perfects = g.perfects() - p0; r.closeCalls = g.closeCalls() - c0;
        }
    }
    r.seconds = frames / 60.0;
    r.cleared = g.lastRunCleared();
    if (r.entered && !r.out) {
        r.perfects = g.perfects() - p0; r.closeCalls = g.closeCalls() - c0; r.inside = frames / 60.0 - t0;
        if (g.state() != Game::State::Play) {
            r.crashCol = (g.hen().nodes[size_t(g.hen().root)].pos.x - door) / U;
            r.crashRow = g.henBase() / U;
        }
    }
    return r;
}

const char* yes(bool b) { return b ? "true" : "false"; }

// a move as [c, b, n, d, lay, move (0 on, 1 a bounce, 2 a crash, 3 out), climbed, fell, flags (1 sawn, 2 its last
// egg, 4 exact, 8 a squeeze, 16 flown off a spring: her next move starts where she lands)]
std::string stepJson(const Gauntlet::TraceStep& t, double meter = -1) {
    const int flags = (t.info.sawn ? 1 : 0) | (t.info.lastEgg ? 2 : 0) | (t.info.exact ? 4 : 0) | (t.info.squeeze ? 8 : 0) | (t.info.flew ? 16 : 0);
    char b[128];
    std::snprintf(b, sizeof b, "[%d,%d,%d,%d,%d,%d,%d,%d,%d", t.s.c, t.s.b, t.s.n, t.s.d, t.lay, int(t.move), t.info.climbed, t.info.fell, flags);
    std::string o = b;
    if (meter >= 0) o += "," + std::to_string(int(std::lround(meter * 100))); // (+ the eggs in the meter, in hundredths)
    return o + "]";
}
std::string nearMissesJson(const std::vector<Gauntlet::TraceStep>& way) {
    std::string o = "[";
    for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(way))
        o += (o.size() > 1 ? "," : "") + std::string("[") + std::to_string(m.c) + "," + std::to_string(m.b) + "," + std::to_string(int(m.kind)) + "," +
             std::to_string(m.step) + "]"; // [c, b, kind (0 PERFECT, 1 CLOSE SHAVE, 2 CLOSE CALL), the move it came in]
    return o + "]";
}

} // namespace

int gauntletLevelOf(const std::string& name, const LevelCatalog* cat) {
    for (int level = 1; level <= 100; ++level)
        if (const Gauntlet* g = Campaign::gauntletOf(level, cat); g && g->name() == name) return level;
    return 0;
}

GauntletGame gauntletInGame(int level, const LevelCatalog* cat) {
    const GauntletRun r = playInGame(level, cat ? cat : &LevelCatalog::dev());
    return {r.entered, r.out, r.cleared, r.perfects, r.closeCalls, r.inside};
}

double gauntletCellsPerEgg(int level, const LevelCatalog* cat) { return cellsPerEgg(level, nullptr, cat ? cat : &LevelCatalog::dev()); }

GauntletReady gauntletReady(const Gauntlet& g, int level, const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    GauntletReady r;
    const Gauntlet::Solution s = g.solve();
    r.reachable = s.reachable;
    r.routes = s.routes;
    r.softLocks = s.softLocks;
    r.nearMisses = s.nearMisses;
    if (!s.reachable) { r.why = "no way to the exit"; return r; }
    r.hard = g.hard();
    r.wanted = GauntletReady::wantMissesAt(r.hard);
    r.slowdown = GauntletReady::slowdownAt(r.hard);
    const MeterRun m = runWithMeter(g, cellsPerEgg(level, nullptr, cat) * r.slowdown, kAtDoor);
    r.meterOut = m.out;
    for (const DesignCheck& d : designChecks(g)) r.designs = r.designs && d.ok;
    r.why = s.routes != 1 ? "more than one way through" : s.softLocks ? "a trap" : !m.out ? "out of eggs with the slow meter"
          : s.nearMisses < r.wanted ? std::to_string(s.nearMisses) + " near misses" : !r.designs ? "its design's signature" : "";
    return r;
}

std::string gauntletReportJson(const Gauntlet& g, const std::string& name, int level, bool game, const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    double speed = 12.4;
    const double perEgg = cellsPerEgg(level, &speed, cat);
    const Gauntlet::Solution s = g.solve();
    std::string o = "{\"ok\":true,\"name\":\"" + jsonEsc(name) + "\",\"level\":" + std::to_string(level);
    o += ",\"cols\":" + std::to_string(g.cols()) + ",\"rows\":" + std::to_string(g.rows()) + ",\"entryRow\":" + std::to_string(g.entryRow()) +
         ",\"entryTop\":" + std::to_string(g.entryTop()) + ",\"exitRow\":" + std::to_string(g.exitRow());
    o += ",\"speed\":" + jsonNum(speed) + ",\"cellsPerEgg\":" + jsonNum(perEgg) + ",\"secondsPerCell\":" + jsonNum(U / speed) + ",\"atDoor\":" + jsonNum(kAtDoor);
    o += ",\"solution\":{\"reachable\":" + std::string(yes(s.reachable)) + ",\"routes\":" + std::to_string(s.routes) +
         ",\"routeCells\":" + std::to_string(s.routeCells) + ",\"deadEndCells\":" + std::to_string(s.deadEndCells) +
         ",\"softLocks\":" + std::to_string(s.softLocks) + ",\"minEggs\":" + std::to_string(s.minEggs) + ",\"moves\":" + std::to_string(s.moves) +
         ",\"perfects\":" + std::to_string(s.perfects) + ",\"shaves\":" + std::to_string(s.shaves) + ",\"squeezes\":" + std::to_string(s.squeezes) +
         ",\"nearMisses\":" + std::to_string(s.nearMisses) + "}";
    o += ",\"hard\":" + std::to_string(g.hard()) + ",\"wantMisses\":" + std::to_string(GauntletReady::wantMissesAt(g.hard()));
    // where her feet can get, rows top first: '.' nowhere, 'd' a dead end, 'w' on a way through
    o += ",\"cellMap\":[";
    for (int r = g.rows() - 1; r >= 0; --r) {
        std::string row;
        for (int c = 0; c < g.cols(); ++c) {
            const uint8_t k = s.cellMap.empty() ? 0 : s.cellMap[size_t(r * g.cols() + c)];
            row += k == 2 ? 'w' : k == 1 ? 'd' : '.';
        }
        o += std::string(r == g.rows() - 1 ? "" : ",") + "\"" + row + "\"";
    }
    o += "]";
    const std::vector<Gauntlet::TraceStep> way = g.botTrace();
    o += ",\"way\":[";
    for (size_t i = 0; i < way.size(); ++i) o += (i ? "," : "") + stepJson(way[i]);
    o += "],\"wayNearMisses\":" + nearMissesJson(way);
    o += ",\"meter\":[";
    for (size_t k = 0; k < std::size(kRates); ++k) {
        const double rate = k == 1 ? GauntletReady::slowdownAt(g.hard()) : kRates[k]; // (the hardest the checklist asks: less from "hard 6")
        const MeterRun r = runWithMeter(g, perEgg * rate, kAtDoor);
        o += std::string(k ? "," : "") + "{\"rate\":" + jsonNum(rate) + ",\"out\":" + yes(r.out) + ",\"cells\":" + std::to_string(r.steps.size()) +
             ",\"waiting\":" + std::to_string(r.waiting) + ",\"seconds\":" + jsonNum(double(r.steps.size()) * U / speed);
        if (!r.out && !r.steps.empty()) {
            const Gauntlet::State& at = r.steps.back().s;
            o += ",\"crash\":[" + std::to_string(at.c) + "," + std::to_string(at.b) + "," + std::to_string(at.n) + "]";
        } else o += ",\"crash\":null";
        o += ",\"nearMisses\":" + nearMissesJson(r.out ? r.steps : std::vector<Gauntlet::TraceStep>{}) + ",\"steps\":[";
        for (size_t i = 0; i < r.steps.size(); ++i) o += (i ? "," : "") + stepJson(r.steps[i], r.meter[i]);
        o += "]}";
    }
    o += "]";
    const CoursePlace p = place(g, level, cat);
    o += ",\"course\":{\"placed\":" + std::string(yes(p.placed)) + ",\"authored\":" + yes(p.authored) + ",\"x0\":" + jsonNum(p.x0) + ",\"x1\":" + jsonNum(p.x1) +
         ",\"finish\":" + jsonNum(p.finish) + ",\"saws\":" + std::to_string(p.saws) + ",\"sawsWanted\":" + std::to_string(p.sawsWanted) + "}";
    o += ",\"designs\":[";
    const std::vector<DesignCheck> designs = designChecks(g);
    for (size_t i = 0; i < designs.size(); ++i)
        o += std::string(i ? "," : "") + "{\"what\":\"" + jsonEsc(designs[i].what) + "\",\"ok\":" + yes(designs[i].ok) + ",\"why\":\"" + jsonEsc(designs[i].why) + "\"}";
    o += "]";
    if (game) {
        const GauntletRun r = playInGame(level, cat);
        o += ",\"game\":{\"entered\":" + std::string(yes(r.entered)) + ",\"out\":" + yes(r.out) + ",\"cleared\":" + yes(r.cleared) +
             ",\"seconds\":" + jsonNum(r.seconds) + ",\"inside\":" + jsonNum(r.inside) + ",\"perfects\":" + std::to_string(r.perfects) +
             ",\"closeCalls\":" + std::to_string(r.closeCalls) +
             ",\"crash\":" + (r.crashCol >= 0 ? "[" + jsonNum(r.crashCol) + "," + jsonNum(r.crashRow) + "]" : std::string("null")) + "}";
    }
    return o + "}";
}

std::string gauntletReportText(const Gauntlet& g, int level, bool map, bool events, bool reach, const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    std::string o;
    double speed = 12.4;
    const double perEgg = cellsPerEgg(level, &speed, cat);
    const Gauntlet::Solution s = g.solve();
    say(o, "%s (level %d): %d x %d, entry row %d, exit row %d\n", g.name().c_str(), level, g.cols(), g.rows(), g.entryRow(), g.exitRow());
    say(o, "  way through: %s, %d route%s, %d trap states, %d dead-end cells, %d eggs at the least, %d cells\n",
        s.reachable ? "reachable" : "NOT REACHABLE", s.routes, s.routes == 1 ? "" : "s", s.softLocks, s.deadEndCells, s.minEggs, s.moves);
    say(o, "  near misses, the bot's way: %d (%d PERFECT, %d CLOSE SHAVE, %d CLOSE CALL)\n", s.nearMisses, s.perfects, s.shaves, s.squeezes);
    say(o, "  meter, 2 eggs at the door (an egg every %.2f cells), near misses as the game shows them:\n", perEgg);
    std::vector<Gauntlet::TraceStep> asPlayed;
    for (double kr : kRates) {
        const double k = kr == 1.25 ? GauntletReady::slowdownAt(g.hard()) : kr;
        const MeterRun r = runWithMeter(g, perEgg * k, kAtDoor);
        if (!r.out) {
            const Gauntlet::State at = r.steps.empty() ? g.start() : r.steps.back().s;
            say(o, "    %.2fx: CRASH after %zu cells at col %d, row %d, %d eggs\n", k, r.steps.size(), at.c, at.b, at.n);
            continue;
        }
        if (k == 1.0) asPlayed = r.steps;
        int nm[3] = {};
        for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(r.steps)) ++nm[int(m.kind)];
        say(o, "    %.2fx: out in %zu cells (%d waiting), %.1f s inside; %d near misses (%d PERFECT, %d CLOSE SHAVE, %d CLOSE CALL)\n", k, r.steps.size(),
            r.waiting, double(r.steps.size()) * U / speed, nm[0] + nm[1] + nm[2], nm[0], nm[1], nm[2]);
    }
    if (events)
        for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(asPlayed.empty() ? g.botTrace() : asPlayed))
            say(o, "    col %3d row %2d: %s\n", m.c, m.b,
                m.kind == Gauntlet::NearMiss::Perfect ? "PERFECT" : m.kind == Gauntlet::NearMiss::Shave ? "CLOSE SHAVE" : "CLOSE CALL (its squeeze ends)");
    if (!map && !reach) return o;
    std::vector<std::string> rows = g.rowsTopFirst();
    if (reach)
        for (const auto& [c, b] : reachable(g)) {
            const int r = g.rows() - 1 - b;
            if (r >= 0 && r < g.rows() && c >= 0 && c < g.cols() && rows[size_t(r)][size_t(c)] == '.') rows[size_t(r)][size_t(c)] = '+';
        }
    for (const Gauntlet::State& st : g.botPath()) {
        const int r = g.rows() - 1 - (st.b + st.n);
        if (r >= 0 && r < g.rows() && st.c >= 0 && st.c < g.cols() && rows[size_t(r)][size_t(st.c)] == '.')
            rows[size_t(r)][size_t(st.c)] = st.d > 0 ? '>' : '<';
    }
    for (int i = 0; i < int(rows.size()); ++i) {
        say(o, "  %2d ", g.rows() - 1 - i);
        o += rows[size_t(i)] + "\n"; // (a row can outgrow say's buffer)
    }
    return o;
}

} // namespace cs
