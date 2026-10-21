// The gauntlet designer's checker (desktop builds; the level editor runs it after every edit): a gauntlet's way
// through, its near misses and the egg meter, judged as Gauntlet_test judges them, and the bot's way drawn on the grid.
//   gauntlet_check NAME [--level N] [--map] [--events] [--reach]   a registered gauntlet (as its level plays it)
// --map draws the bot's way; --reach marks everywhere she can get to ('+', her feet), the way through or not (where a
// design that doesn't get out stops); --events lists its near misses (as the game shows them, played with the meter)
//   gauntlet_check --file PATH|- [--name NAME] [--level N] [...]   a layout, rows top first (';' lines are notes;
//                                                                   '-': stdin), as NAME's level plays it (or N's)
//   ... --level-file PATH   that level as in PATH (LevelData.h), its course laying the layout (the level editor's level,
//                           unsaved edits and all)
//   gauntlet_check --all                                            every registered gauntlet
//   ... --json [--game]   all of it as one JSON object (the level editor's): the solution and where her feet can get,
//                         the bot's way, the meter runs step by step, the near misses, where it lands on its level's
//                         course, its design's signature (GauntletDesigns.h); --game also plays it in the real game,
//                         the bot playing (headless, as GameGauntlet_test does), the layout standing in for the named one
// The meter runs: the bot with a real egg meter, 2 eggs in it at the door (what the forecourt refills), refilling at
// the level's rate (an egg every regen seconds at its speed), then 1.25 and 1.4 times slower; it gets out, or crashes
// where it ran short. A design is ready when the way is one (routes 1, no traps), the bot gets out at 1.25x, and it has
// its near misses (docs/LEVEL_DESIGN.md).
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/Gauntlet.h"
#include "core/GauntletDesigns.h"
#include "core/Level.h"
#include "core/LevelData.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace cs;

namespace {

constexpr double kAtDoor = 2;                       // eggs in the meter at the door (the forecourt's refill)
constexpr double kRates[] = {1.0, 1.25, 1.4};       // the meter refilling this many times slower than the level's

// the level's refill in cells per egg (Campaign::applyTo's difficulty, at the gauntlet's place on the course)
double cellsPerEgg(int level, double* speed) {
    Level l;
    l.setDifficulty(35.0 * (level - 1), 0.35);
    if (const LevelData* d = levelData(level)) l.setDifficulty(d->diffOffset, d->diffScale);
    const Curve c = l.curveAt(Campaign::gauntletAt(level));
    if (speed) *speed = c.speed;
    return c.speed * c.regen / U;
}

// a level standing in for the built-in one (the level editor's, unsaved edits and all): why it doesn't, if it doesn't
std::string& levelError() { static std::string why; return why; }
bool overrideLevel(int level, const std::string& path, FILE* say) {
    std::ifstream in(path);
    std::string text, line;
    while (std::getline(in, line)) text += line + "\n";
    if (!in.eof() || !debugOverrideLevel(level, text, &levelError())) {
        if (levelError().empty()) levelError() = "can't read " + path;
        if (say) std::fprintf(say, "--level-file %s: %s\n", path.c_str(), levelError().c_str());
        return false;
    }
    return true;
}

int levelOf(const std::string& name) {
    for (int level = 1; level <= 100; ++level)
        if (const Gauntlet* g = Campaign::gauntletOf(level); g && g->name() == name) return level;
    return 0;
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
struct Placement { bool placed = false, authored = false; double x0 = 0, x1 = 0, finish = 0; int saws = 0, sawsWanted = 0; };
Placement place(const Gauntlet& g, int level) {
    Placement p;
    Level l;
    if (const LevelData* d = levelData(level)) {
        p.authored = true;
        l.load(*d);
        l.generateUntil(d->length + 20);
    } else {
        l.reset(Campaign::seedOf(level));
        l.setDifficulty(35.0 * (level - 1), 0.35);
        l.setFinish(Campaign::lengthOf(level));
        l.setSaws(Campaign::sawsOf(level), Campaign::sawRowOf(level));
        l.setGauntlet(&g, Campaign::gauntletAt(level));
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
struct GameRun {
    bool entered = false, out = false, cleared = false;
    double seconds = 0, inside = 0;
    int perfects = 0, closeCalls = 0;
    double crashCol = -1, crashRow = -1;
};
GameRun playInGame(int level) {
    GameRun r;
    test::FakeStorage st = test::FakeStorage::ready();
    test::FakeClock clock;
    test::FakeAds ads;
    ads.state = RewardedState::Unavailable;
    Campaign(&st).debugSetLevel(level);
    GameServices svc;
    svc.storage = &st; svc.clock = &clock; svc.ads = &ads;
    Game g(svc);
    g.debugSeed(1);
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

// ---- JSON
std::string esc(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if (ch == '\n') o += "\\n";
        else if (static_cast<unsigned char>(ch) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += ch;
    }
    return o;
}
std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); return b; }
const char* yes(bool b) { return b ? "true" : "false"; }

// a move as [c, b, n, d, lay, move (0 on, 1 a bounce, 2 a crash, 3 out), climbed, fell, flags (1 sawn, 2 its last
// egg, 4 exact, 8 a squeeze)]
std::string stepJson(const Gauntlet::TraceStep& t, double meter = -1) {
    const int flags = (t.info.sawn ? 1 : 0) | (t.info.lastEgg ? 2 : 0) | (t.info.exact ? 4 : 0) | (t.info.squeeze ? 8 : 0);
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

void reportJson(const Gauntlet& g, const std::string& name, int level, bool game) {
    double speed = 12.4;
    const double perEgg = cellsPerEgg(level, &speed);
    const Gauntlet::Solution s = g.solve();
    std::string o = "{\"ok\":true,\"name\":\"" + esc(name) + "\",\"level\":" + std::to_string(level);
    o += ",\"cols\":" + std::to_string(g.cols()) + ",\"rows\":" + std::to_string(g.rows()) + ",\"entryRow\":" + std::to_string(g.entryRow()) +
         ",\"entryTop\":" + std::to_string(g.entryTop()) + ",\"exitRow\":" + std::to_string(g.exitRow());
    o += ",\"speed\":" + num(speed) + ",\"cellsPerEgg\":" + num(perEgg) + ",\"secondsPerCell\":" + num(U / speed) + ",\"atDoor\":" + num(kAtDoor);
    o += ",\"solution\":{\"reachable\":" + std::string(yes(s.reachable)) + ",\"routes\":" + std::to_string(s.routes) +
         ",\"routeCells\":" + std::to_string(s.routeCells) + ",\"deadEndCells\":" + std::to_string(s.deadEndCells) +
         ",\"softLocks\":" + std::to_string(s.softLocks) + ",\"minEggs\":" + std::to_string(s.minEggs) + ",\"moves\":" + std::to_string(s.moves) +
         ",\"perfects\":" + std::to_string(s.perfects) + ",\"shaves\":" + std::to_string(s.shaves) + ",\"squeezes\":" + std::to_string(s.squeezes) +
         ",\"nearMisses\":" + std::to_string(s.nearMisses) + "}";
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
        const MeterRun r = runWithMeter(g, perEgg * kRates[k], kAtDoor);
        o += std::string(k ? "," : "") + "{\"rate\":" + num(kRates[k]) + ",\"out\":" + yes(r.out) + ",\"cells\":" + std::to_string(r.steps.size()) +
             ",\"waiting\":" + std::to_string(r.waiting) + ",\"seconds\":" + num(double(r.steps.size()) * U / speed);
        if (!r.out && !r.steps.empty()) {
            const Gauntlet::State& at = r.steps.back().s;
            o += ",\"crash\":[" + std::to_string(at.c) + "," + std::to_string(at.b) + "," + std::to_string(at.n) + "]";
        } else o += ",\"crash\":null";
        o += ",\"nearMisses\":" + nearMissesJson(r.out ? r.steps : std::vector<Gauntlet::TraceStep>{}) + ",\"steps\":[";
        for (size_t i = 0; i < r.steps.size(); ++i) o += (i ? "," : "") + stepJson(r.steps[i], r.meter[i]);
        o += "]}";
    }
    o += "]";
    const Placement p = place(g, level);
    o += ",\"course\":{\"placed\":" + std::string(yes(p.placed)) + ",\"authored\":" + yes(p.authored) + ",\"x0\":" + num(p.x0) + ",\"x1\":" + num(p.x1) +
         ",\"finish\":" + num(p.finish) + ",\"saws\":" + std::to_string(p.saws) + ",\"sawsWanted\":" + std::to_string(p.sawsWanted) + "}";
    o += ",\"designs\":[";
    const std::vector<DesignCheck> designs = designChecks(g);
    for (size_t i = 0; i < designs.size(); ++i)
        o += std::string(i ? "," : "") + "{\"what\":\"" + esc(designs[i].what) + "\",\"ok\":" + yes(designs[i].ok) + ",\"why\":\"" + esc(designs[i].why) + "\"}";
    o += "]";
    if (game) {
        const GameRun r = playInGame(level);
        o += ",\"game\":{\"entered\":" + std::string(yes(r.entered)) + ",\"out\":" + yes(r.out) + ",\"cleared\":" + yes(r.cleared) +
             ",\"seconds\":" + num(r.seconds) + ",\"inside\":" + num(r.inside) + ",\"perfects\":" + std::to_string(r.perfects) +
             ",\"closeCalls\":" + std::to_string(r.closeCalls) +
             ",\"crash\":" + (r.crashCol >= 0 ? "[" + num(r.crashCol) + "," + num(r.crashRow) + "]" : std::string("null")) + "}";
    }
    o += "}";
    std::printf("%s\n", o.c_str());
}

void report(const Gauntlet& g, int level, bool map, bool events, bool reach) {
    double speed = 12.4;
    const double perEgg = cellsPerEgg(level, &speed);
    const Gauntlet::Solution s = g.solve();
    std::printf("%s (level %d): %d x %d, entry row %d, exit row %d\n", g.name().c_str(), level, g.cols(), g.rows(), g.entryRow(), g.exitRow());
    std::printf("  way through: %s, %d route%s, %d trap states, %d dead-end cells, %d eggs at the least, %d cells\n",
                s.reachable ? "reachable" : "NOT REACHABLE", s.routes, s.routes == 1 ? "" : "s", s.softLocks, s.deadEndCells, s.minEggs, s.moves);
    std::printf("  near misses, the bot's way: %d (%d PERFECT, %d CLOSE SHAVE, %d CLOSE CALL)\n", s.nearMisses, s.perfects, s.shaves, s.squeezes);
    std::printf("  meter, 2 eggs at the door (an egg every %.2f cells), near misses as the game shows them:\n", perEgg);
    std::vector<Gauntlet::TraceStep> asPlayed;
    for (double k : kRates) {
        const MeterRun r = runWithMeter(g, perEgg * k, kAtDoor);
        if (!r.out) {
            const Gauntlet::State at = r.steps.empty() ? g.start() : r.steps.back().s;
            std::printf("    %.2fx: CRASH after %zu cells at col %d, row %d, %d eggs\n", k, r.steps.size(), at.c, at.b, at.n);
            continue;
        }
        if (k == 1.0) asPlayed = r.steps;
        int nm[3] = {};
        for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(r.steps)) ++nm[int(m.kind)];
        std::printf("    %.2fx: out in %zu cells (%d waiting), %.1f s inside; %d near misses (%d PERFECT, %d CLOSE SHAVE, %d CLOSE CALL)\n", k, r.steps.size(),
                    r.waiting, double(r.steps.size()) * U / speed, nm[0] + nm[1] + nm[2], nm[0], nm[1], nm[2]);
    }
    if (events)
        for (const Gauntlet::NearMissAt& m : Gauntlet::nearMisses(asPlayed.empty() ? g.botTrace() : asPlayed))
            std::printf("    col %3d row %2d: %s\n", m.c, m.b,
                        m.kind == Gauntlet::NearMiss::Perfect ? "PERFECT" : m.kind == Gauntlet::NearMiss::Shave ? "CLOSE SHAVE" : "CLOSE CALL (its squeeze ends)");
    if (!map && !reach) return;
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
    for (int i = 0; i < int(rows.size()); ++i) std::printf("  %2d %s\n", g.rows() - 1 - i, rows[size_t(i)].c_str());
}

} // namespace

int main(int argc, char** argv) {
    std::string name, file, levelFile;
    int level = 0;
    bool map = false, events = false, all = false, reach = false, json = false, game = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!std::strcmp(argv[i], "--level") && i + 1 < argc) level = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--level-file") && i + 1 < argc) levelFile = argv[++i];
        else if (!std::strcmp(argv[i], "--map")) map = true;
        else if (!std::strcmp(argv[i], "--events")) events = true;
        else if (!std::strcmp(argv[i], "--reach")) reach = true;
        else if (!std::strcmp(argv[i], "--all")) all = true;
        else if (!std::strcmp(argv[i], "--json")) json = true;
        else if (!std::strcmp(argv[i], "--game")) game = true;
        else name = argv[i];
    }
    if (all) {
        for (const std::string& n : gauntletNames())
            if (const Gauntlet* g = gauntletNamed(n)) report(*g, level ? level : levelOf(n), false, false, false);
        return 0;
    }
    if (!file.empty()) {
        std::vector<std::string> rows;
        std::ifstream f;
        std::istream* in = &std::cin;
        if (file != "-") { f.open(file); in = &f; }
        for (std::string line; std::getline(*in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line[0] != ';') rows.push_back(line);
        }
        if (name.empty()) {
            name = file == "-" ? "design" : file.substr(file.find_last_of('/') + 1);
            name = name.substr(0, name.find('.'));
        }
        if (!level) level = levelOf(name);
        if (!level) level = 13;
        std::string err;
        const std::optional<Gauntlet> g = Gauntlet::parse(name, rows, &err);
        if (!g) {
            if (json) std::printf("{\"ok\":false,\"name\":\"%s\",\"level\":%d,\"error\":\"%s\"}\n", esc(name).c_str(), level, esc(err).c_str());
            else std::printf("doesn't parse: %s\n", err.c_str());
            return 1;
        }
        debugOverrideGauntlet(name, rows); // (the level's course lays this layout)
        if (!levelFile.empty() && !overrideLevel(level, levelFile, json ? nullptr : stdout)) {
            if (json) std::printf("{\"ok\":false,\"name\":\"%s\",\"level\":%d,\"error\":\"level %d: %s\"}\n", esc(name).c_str(), level, level, esc(levelError()).c_str());
            return 1;
        }
        if (json) reportJson(*g, name, level, game);
        else report(*g, level, map, events, reach);
        return 0;
    }
    const Gauntlet* g = gauntletNamed(name);
    if (!g) { std::printf("no gauntlet named '%s' (try --all)\n", name.c_str()); return 1; }
    if (json) reportJson(*g, name, level ? level : levelOf(name), game);
    else report(*g, level ? level : levelOf(name), map, events, reach);
    return 0;
}
