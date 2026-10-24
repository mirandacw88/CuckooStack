// A maze made at random (MazeGen.h): every one the generator hands back reads as a gauntlet and passes what the tests
// want of one (GauntletReport.h's checklist), at each size; the same seed makes the same maze; a new seed, another.
#include "TestUtil.h"
#include "core/Gauntlet.h"
#include "core/GauntletReport.h"
#include "core/LevelCatalog.h"
#include "core/BotRun.h"
#include "core/LevelData.h"
#include "core/Level.h"
#include "editor/EditorDoc.h"
#include "editor/MazeGen.h"
#include "core/LevelBake.h"
#include "core/Campaign.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

using namespace cs;
using namespace cs::ed;
using test::check;

namespace {

// (the bulk statistics make mazes without the game's own near-miss gate, `game` false: nearMissesAgree checks that one)
// MAZE_QUICK=1: about a sixth of the seeds in each loop, to iterate on (the checks' bars scale with them); the full run is for the end
bool quick() { static const bool q = std::getenv("MAZE_QUICK") != nullptr; return q; }
uint32_t seedsOf(uint32_t full) { return quick() ? std::max(3u, full / 6) : full; }

void sizes() {
    const int level = 12;
    for (const int floors : {3, 5, 7}) {
        for (const int width : {120, 170, 240}) {
            int made = 0, tries = 0;
            double slowest = 0;
            for (uint32_t seed = 1; seed <= seedsOf(6); ++seed) {
                const auto t0 = std::chrono::steady_clock::now();
                const MazeGenResult r = generateMaze(level, width, floors, seed * 1000, &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, 0, true, false});
                slowest = std::max(slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
                tries += r.tries;
                if (!r.ok) { std::printf("      (%d floors, %d long, seed %u: %s)\n", floors, width, seed * 1000, r.why.c_str()); continue; }
                ++made;
                const std::optional<Gauntlet> g = Gauntlet::parse("generated", r.rows);
                check(g && gauntletReady(*g, level, &LevelCatalog::builtin()).ok(), "a maze made: it reads, and passes the checklist");
                check(g && (r.widened ? g->cols() > width : g->cols() == width), "...as wide as asked, or, if none was ready so, wider");
            }
            std::printf("      %d floors, %d long: %d of %u made, %.1f tries each, the slowest %.2f s\n", floors, width, made, seedsOf(6), tries / double(seedsOf(6)), slowest);
            check(made == int(seedsOf(6)), "every press makes one");
        }
    }
}

// what a maze's hazards look like: the mean distance between hazard columns, and the biggest cluster (hazard cells
// within 3 columns of each other, in a row of them)
struct Pack { double gap = 0; int biggest = 0, ledges = 0, hazards = 0; };
Pack packOf(const MazeGenResult& r) {
    Pack p;
    std::vector<int> count;
    for (const std::string& row : r.rows) {
        if (count.size() < row.size()) count.resize(row.size(), 0);
        for (size_t c = 0; c < row.size(); ++c) count[c] += row[c] == 'S' || row[c] == 'C' || row[c] == '~' || row[c] == '(' || row[c] == ')';
    }
    int last = -1, run = 0, cols = 0, span = 0;
    for (size_t c = 0; c < count.size(); ++c) {
        if (!count[c]) continue;
        p.hazards += count[c];
        if (last >= 0) { span += int(c) - last; ++cols; }
        run = last >= 0 && int(c) - last <= 3 ? run + count[c] : count[c];
        p.biggest = std::max(p.biggest, run);
        last = int(c);
    }
    p.gap = cols ? double(span) / cols : 0;
    for (const std::string& d : r.description) {
        const size_t at = d.find(" ledges,");
        if (at != std::string::npos) { size_t b = d.rfind(' ', at - 1); p.ledges = std::atoi(d.substr(b + 1, at - b - 1).c_str()); }
    }
    return p;
}

// density 0 to 10 (level 20, 170 wide): every density makes ready mazes (the fallback to a wider one counted), and
// at 10 the hazards sit closer and in bigger clusters than at 0; a maze at 6 or on carries its "hard N" line, and is judged by it
void densities() {
    const int level = 20;
    double meanGap[11] = {}, meanBiggest[11] = {};
    for (const int density : {0, 5, 6, 8, 10}) {
        int made = 0, widened = 0, total = 0, ledgeMazes = 0;
        double tries = 0, gap = 0, biggest = 0, ledges = 0, width = 0, seconds = 0;
        for (const int floors : {3, 5, 7}) {
            for (uint32_t seed = 1; seed <= seedsOf(20); ++seed) {
                const auto t0 = std::chrono::steady_clock::now();
                const MazeGenResult r = generateMaze(level, 170, floors, seed * 7717u + uint32_t(density), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density, true, false});
                seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                ++total;
                tries += r.tries;
                if (!r.ok) continue;
                ++made;
                widened += r.widened;
                std::vector<std::string> lines;
                for (const std::string& d : r.description) if (d.rfind("hard ", 0) == 0) lines.push_back("; " + d);
                check(lines.size() == (density >= 6 ? 1u : 0u), "a maze from density 6 carries its hard line, a milder one none");
                lines.insert(lines.end(), r.rows.begin(), r.rows.end());
                const std::optional<Gauntlet> g = Gauntlet::parse("generated", lines);
                check(g && g->hard() == (density >= 6 ? density : 0) && gauntletReady(*g, level, &LevelCatalog::builtin()).ok(), "a maze made at every density: ready, by its own density's rules");
                width += g ? g->cols() : 0;
                const Pack p = packOf(r);
                gap += p.gap; biggest += p.biggest; ledges += p.ledges; ledgeMazes += p.ledges > 0;
            }
        }
        const double n = std::max(1, made);
        meanGap[density] = gap / n;
        meanBiggest[density] = biggest / n;
        std::printf("      density %2d: %d of %d ready, %d widened, width %.0f, gap %.1f, biggest cluster %.1f, ledges %.2f a maze (%d with), %.1f tries, %.2f s each\n",
                    density, made, total, widened, width / n, gap / n, biggest / n, ledges / n, ledgeMazes, tries / total, seconds / total);
        check(made >= total * 9 / 10, "nearly every press makes a ready maze, at any density");
        if (density == 0) check(ledgeMazes > 0, "ledges appear in the tall floors");
    }
    check(meanGap[10] < meanGap[0], "at 10 the hazards are closer than at 0");
    check(meanBiggest[10] > meanBiggest[0], "...and the clusters bigger");
}

// the checklist bends from "hard 6": the same maze is judged on a lesser meter and by more near misses
void bent() {
    check(GauntletReady::slowdownAt(0) == 1.25 && GauntletReady::slowdownAt(5) == 1.25 && GauntletReady::slowdownAt(10) == 1.0 && GauntletReady::slowdownAt(8) < GauntletReady::slowdownAt(6), "the meter's spare: 1.25 to density 5, 1.0 at 10");
    check(GauntletReady::wantMissesAt(0) == 15 && GauntletReady::wantMissesAt(5) == 15 && GauntletReady::wantMissesAt(10) == 20, "...and the near misses wanted: 15 to density 5, 20 at 10");
    // the same rows, with and without the line
    const MazeGenResult r = generateMaze(20, 170, 3, 4242, &LevelCatalog::builtin());
    check(r.ok, "a maze for the bent rules");
    std::vector<std::string> hard{"; hard 10"};
    hard.insert(hard.end(), r.rows.begin(), r.rows.end());
    const auto plain = Gauntlet::parse("a", r.rows), tough = Gauntlet::parse("a", hard);
    check(plain && tough && plain->hard() == 0 && tough->hard() == 10, "a \"hard 10\" line is read; none: 0");
    check(plain && tough && plain->rowsTopFirst() == tough->rowsTopFirst(), "...and is no row");
    const GauntletReady a = gauntletReady(*plain, 20, &LevelCatalog::builtin()), b = gauntletReady(*tough, 20, &LevelCatalog::builtin());
    check(a.wanted == 15 && a.slowdown == 1.25 && b.wanted == 20 && b.slowdown == 1.0, "judged as the line says (15 near misses and 1.25x; 20 and 1.0x)");
    check(Gauntlet::hardOf("; hard 7") == 7 && Gauntlet::hardOf(";hard 10") == 10 && Gauntlet::hardOf("; hard x") == -1 && Gauntlet::hardOf("; hard 11") == 10 && Gauntlet::hardOf("hard 3") == -1 && Gauntlet::hardOf("; a hard one") == -1, "the line reads as 'hard N' and nothing else");
    // every authored gauntlet, no line: judged as before (hard 0)
    for (const std::string& name : LevelCatalog::builtin().gauntletNames()) check(LevelCatalog::builtin().gauntlet(name)->hard() == 0, "an authored gauntlet has no hard line: 0");
}

// the numbers in a maze's description: "N saws, ... M ledges, K springs (U up a step in the floor, L onto a ledge)."
struct Springs { int springs = 0, up = 0, onto = 0; };
Springs springsOf(const MazeGenResult& r) {
    Springs out;
    for (const std::string& d : r.description) {
        const size_t at = d.find(" springs (");
        if (at == std::string::npos) continue;
        out.springs = std::atoi(d.substr(d.rfind(' ', at - 1) + 1).c_str());
        out.up = std::atoi(d.c_str() + d.find('(', at) + 1);
        const std::string key = " up a step in the floor, ";
        const size_t u = d.find(key);
        if (u != std::string::npos) out.onto = std::atoi(d.c_str() + u + key.size());
    }
    return out;
}

// springs in mazes (a '^': the solver's flight): over level 20 at 170 wide, in 3 and 5 floors (the tall ones: a spring wants a stretch 8 high) at densities 0, 5 and 10,
// springs appear (up a step in the floor, onto a ledge), every maze made is ready (by its density's rules), and the
// spring cells in it are there in the rows
void springMazes() {
    const int level = 20;
    int totalMade = 0, totalAll = 0, anyUp = 0, anyOnto = 0, cells = 0, described = 0;
    for (const int density : {0, 5, 10})
        for (const int floors : {3, 5}) {
            int made = 0, springs = 0, ledges = 0, onto = 0, up = 0, withSprings = 0;
            double tries = 0, seconds = 0;
            for (uint32_t seed = 1; seed <= seedsOf(20); ++seed) {
                const auto t0 = std::chrono::steady_clock::now();
                const MazeGenResult r = generateMaze(level, 170, floors, seed * 6007u + uint32_t(density * 31 + floors), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density, true, false});
                seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                ++totalAll;
                tries += r.tries;
                if (!r.ok) continue;
                ++made;
                std::vector<std::string> lines;
                for (const std::string& d : r.description) if (d.rfind("hard ", 0) == 0) lines.push_back("; " + d);
                lines.insert(lines.end(), r.rows.begin(), r.rows.end());
                const std::optional<Gauntlet> g = Gauntlet::parse("generated", lines);
                check(g && gauntletReady(*g, level, &LevelCatalog::builtin()).ok(), "a maze with springs: ready");
                const Springs sp = springsOf(r);
                int n = 0;
                for (const std::string& row : r.rows) n += int(std::count(row.begin(), row.end(), '^'));
                cells += n;
                described += n == sp.springs;
                springs += sp.springs; up += sp.up; onto += sp.onto; withSprings += sp.springs > 0;
                ledges += packOf(r).ledges;
            }
            totalMade += made;
            anyUp += up; anyOnto += onto;
            std::printf("      density %2d, %d floors: %2d of 20 ready, springs %.2f a maze (%d up, %d onto ledges; %d mazes with), ledges %.2f a maze, %.1f tries, %.2f s each\n",
                        density, floors, made, springs / std::max(1.0, double(made)), up, onto, withSprings, ledges / std::max(1.0, double(made)), tries / seedsOf(20), seconds / seedsOf(20));
        }
    check(totalMade >= totalAll * 9 / 10, "springs or not, nearly every press makes a ready maze");
    check(anyUp > 0 && anyOnto > 0, "springs fly her up into a higher room, and onto a ledge");
    check(cells > 0 && described == totalMade, "...and the description counts the spring cells in the rows");
}

// the floors' sections: the description's "[0 3 1] [0 2]" groups (rows over each floor's base, along its run)
std::vector<std::vector<int>> sectionsOf(const MazeGenResult& r) {
    std::vector<std::vector<int>> out;
    for (const std::string& d : r.description) {
        if (d.rfind("Floor path within", 0) != 0) continue;
        for (size_t at = d.find('['); at != std::string::npos; at = d.find('[', at + 1)) {
            std::vector<int> floor;
            for (const char* c = d.c_str() + at + 1; *c && *c != ']';) { floor.push_back(std::atoi(c)); while (*c && *c != ' ' && *c != ']') ++c; if (*c == ' ') ++c; }
            out.push_back(floor);
        }
    }
    return out;
}

// the tower (level 20, 170 wide): `floors` 3, 5 and 7 make that many floors stacked one over another (a gate column between
// each two, the exit at the top), every one made is ready, a floor's corridor is cut into stretches at different heights
// in most mazes (up a wall, down a drop), and ledges and springs appear
void towers() {
    const int level = 20;
    int made = 0, total = 0, widened = 0, lower = 0, tries = 0, ledgeMazes = 0, springMazes = 0, flatMazes = 0, floorsTotal = 0, floorsFlat = 0;
    double seconds = 0;
    for (const int floors : {3, 5, 7}) {
        int madeHere = 0;
        for (uint32_t seed = 1; seed <= seedsOf(30); ++seed) {
            const auto t0 = std::chrono::steady_clock::now();
            const MazeGenResult r = generateMaze(level, 170, floors, seed * 104729u, &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, 0, true, false});
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ++total;
            tries += r.tries;
            if (!r.ok) continue;
            ++made;
            ++madeHere;
            widened += r.widened;
            const std::optional<Gauntlet> g = Gauntlet::parse("generated", r.rows);
            check(g && gauntletReady(*g, level, &LevelCatalog::builtin()).ok(), "a maze made: ready");
            // (stacked: n floors, a gate column between each two, the entry at the bottom and the exit on the right edge)
            int gateCols = 0; // (gate runs: a column's R or L cells in a row, one a shaft)
            for (size_t c = 0; c < r.rows[0].size(); ++c)
                for (size_t y = 0; y < r.rows.size(); ++y) {
                    const char ch = r.rows[y][c];
                    const bool gate = ch == 'R' || ch == 'L', above = y > 0 && (r.rows[y - 1][c] == 'R' || r.rows[y - 1][c] == 'L');
                    gateCols += gate && !above;
                }
            check(gateCols == floors - 1, "a gate between each two of the floors");
            const std::string all = r.description.size() > 2 ? r.description[2] : "";
            lower += all.find("below the top floor") != std::string::npos;
            const auto secs = sectionsOf(r);
            check(int(secs.size()) == floors, "the description lists a floor path for each floor");
            bool anyUneven = false;
            for (const auto& f : secs) {
                ++floorsTotal;
                const bool flat = std::all_of(f.begin(), f.end(), [&](int v) { return v == f[0]; });
                floorsFlat += flat;
                anyUneven = anyUneven || !flat;
                check(!f.empty() && f[0] == 0, "a floor starts at its base");
            }
            flatMazes += !anyUneven;
            ledgeMazes += packOf(r).ledges > 0;
            springMazes += springsOf(r).springs > 0;
        }
        std::printf("      %d floors: %d of %u made\n", floors, madeHere, seedsOf(30));
    }
    std::printf("      %d of %d made (%d widened); %d with the exit below the top floor's highest stretch, %d with ledges, %d with springs, %d flat; %d of %d floors flat; %.1f tries each; %.2f s each\n",
                made, total, widened, lower, ledgeMazes, springMazes, flatMazes, floorsFlat, floorsTotal, double(tries) / total, seconds / total);
    check(made >= total * 9 / 10, "nearly every press makes a ready maze");
    check(floorsFlat < floorsTotal / 3, "most floors rise and fall: stretches at different heights (a tower of 7 is 6 to 11 high a floor, like 3 and 5: the exit's porch is a course wall, 60 rows at the most)");
    check(flatMazes == 0, "...in every maze");
    check(ledgeMazes > 0 && springMazes > 0, "ledges and springs appear");
}

void seeds() {
    const auto a = layMaze(170, 3, 42, 3.0), b = layMaze(170, 3, 42, 3.0), c = layMaze(170, 3, 43, 3.0);
    check(a == b, "the same seed: the same maze");
    check(a != c, "another seed: another maze");
}

// the two generators together, as the editor uses them: a level's course rolled anew, then a maze made for it and put
// in at 40% of the way; the game's bot plays it through
void together() {
    const int level = 12;
    auto doc = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
    check(doc && doc->regenerate(777) && doc->canUndo(), "a level rolled anew: one step in its history");
    bool gauntlet = false;
    for (const Col& c : doc->course().cols) gauntlet = gauntlet || !c.g.empty();
    check(!gauntlet, "...a course with no gauntlet");
    const std::string before = doc->text();
    check(doc->regenerate(778) && doc->text() != before, "another seed: another course");
    int cleared = 0, tries = 0, withBoth = 0, unfair = 0;
    for (uint32_t seed : {99u, 7000u, 12345u, 424242u, 9u}) {
        auto d = EditorDoc::open(level, doc->text());
        LevelCatalog cat;
        cat.setLevel(level, d->text());
        const MazeGenResult m = generateMaze(level, 160, 3, seed, &cat);
        if (!m.ok) continue;
        ++tries;
        std::string all;
        for (const std::string& r : m.rows) all += r;
        withBoth += all.find_first_of("()") != std::string::npos && all.find('~') != std::string::npos;
        if (!d->setGrid("maze_12", m.rows) || !d->placeGauntlet("maze_12", int(0.4 * d->course().length / U))) continue;
        LevelCatalog play;
        play.setGauntlet("maze_12", m.rows);
        if (!play.setLevel(level, d->text())) continue;
        const GameRun run = playLevel(level, &play);
        std::printf("      seed %u: the game's bot %s, %.1f s, %d near misses\n", seed, run.cleared ? "cleared it" : "crashed", run.seconds, run.perfects + run.closeCalls);
        cleared += run.cleared;
        const auto sp = spanOf(d->course(), "maze_12");
        for (const auto& n : d->report().notes)
            if (sp && n.x >= sp->c1 * U && n.x < sp->c1 * U + 30 && n.text.find("no time to land") != std::string::npos) { ++unfair; std::printf("      %s\n", n.text.c_str()); }
        if (!run.cleared) {
            const double x0 = Campaign::gauntletAt(level, &play);
            const int col = int(std::floor((run.x - x0) / U)), r = int(std::lround(run.y / U));
            std::printf("      DIAG x %.2f y %.2f x0 %.2f col %d row %d death %d\n", run.x, run.y, x0, col, r, run.death);
            for (size_t i = 0; i < m.rows.size(); ++i) {
                const int rr = int(m.rows.size()) - 1 - int(i);
                if (col < 0 || col >= int(m.rows[i].size())) continue;
                std::printf("      %3d %s\n", rr, m.rows[i].substr(size_t(std::max(0, col - 12)), 26).c_str());
            }
            if (const auto ld = parseLevel(level, d->text())) for (const Placed& p : ld->items) if (std::abs(p.x() - run.x) < 12) std::printf("      %s at %.2f %.0f %.0f\n", p.type->id, p.x(), p.at(1), p.at(2));
            if (const auto ld = parseLevel(level, d->text(), nullptr, &play)) {
                Level l;
                l.load(*ld, &play);
                l.generateUntil(run.x + 40);
                for (const Segment& sg : l.segs) if (sg.x1 > run.x - 8 && sg.x0 < run.x + 4) std::printf("      seg %.2f..%.2f h %d ceil %d top %d g %d\n", sg.x0, sg.x1, sg.h, sg.ceil, sg.top, int(sg.gauntlet));
                for (Laser z : l.lasers) { l.traceLaser(z); if (z.x1 > run.x - 8 && z.x0 < run.x + 4) std::printf("      laser x %.2f row %d dir %d reach %.2f..%.2f\n", z.x, z.row, z.dir, z.x0, z.x1); }
            }
            if (col >= 0 && col < 160)
                for (const auto& t : Gauntlet::parse("x", m.rows)->botTrace())
                    if (std::abs(t.s.c - col) < 8) std::printf("      solver c %d b %d n %d d %d z %d lay %d move %d cut %d\n", t.s.c, t.s.b, t.s.n, t.s.d, t.s.z, t.lay, int(t.move), t.info.cut);
            for (auto& t : run.trace) if (t[0] > run.x - 6 && t[0] <= run.x + 1) std::printf("      trace x %.2f floor %.2f eggs %.0f\n", t[0], t[1], t[2]);
        }
    }
    std::printf("      %d of %d mazes with wall lasers and lava both\n", withBoth, tries);
    check(tries == 5 && withBoth >= 3, "mazes made for it, with wall lasers and lava in them");
    check(cleared == tries, "the game's bot clears the level, its maze too, every one");
    check(unfair == 0, "...off each maze's porch, a fair way down to the course (no drop with no time to land)");
}

// a course rolled anew is one the game's bot clears (the generator's egg budget), in every zone
void rolledCourses() {
    for (const int level : {4, 15, 26}) {
        for (const uint32_t seed : {11u, 12u}) {
            auto doc = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
            doc->regenerate(seed);
            LevelCatalog cat;
            cat.setLevel(level, doc->text());
            const GameRun run = playLevel(level, &cat);
            std::printf("      level %d rolled (seed %u): %s, %zu notes\n", level, seed, run.cleared ? "cleared" : "crashed", doc->report().notes.size());
            if (!run.cleared) {
                std::printf("      DIAG x %.2f y %.2f death %d\n", run.x, run.y, run.death);
                if (const auto ld = parseLevel(level, doc->text(), nullptr, &cat)) {
                    std::printf("      length %.1f\n", ld->length);
                    double x = 0;
                    for (const auto& w : ld->course) { if (x + w.cells * U > run.x - 15 && x < run.x + 5) std::printf("      wall %.2f..%.2f h %d ceil %d top %d %s\n", x, x + w.cells * U, w.h, w.ceil, w.top, w.gauntlet.c_str()); x += w.cells * U; }
                }
                if (const auto ld = parseLevel(level, doc->text(), nullptr, &cat)) {
                    Level l;
                    l.load(*ld, &cat);
                    l.generateUntil(run.x + 40);
                    for (const Segment& sg : l.segs) if (sg.x1 > run.x - 10 && sg.x0 < run.x + 4) std::printf("      seg %.2f..%.2f h %d ceil %d top %d\n", sg.x0, sg.x1, sg.h, sg.ceil, sg.top);
                    for (Laser z : l.lasers) { l.traceLaser(z); if (z.x1 > run.x - 10 && z.x0 < run.x + 4) std::printf("      laser x %.2f row %d dir %d reach %.2f..%.2f\n", z.x, z.row, z.dir, z.x0, z.x1); }
                    for (const Saw& w : l.saws) if (std::abs(w.x - run.x) < 10) std::printf("      saw %.2f\n", w.x);
                    for (const Lava& f : l.lavas) if (f.x1 > run.x - 10 && f.x0 < run.x + 4) std::printf("      lava %.2f..%.2f\n", f.x0, f.x1);
                }
                for (auto& t : run.trace) if (t[0] > run.x - 8 && t[0] <= run.x + 1) std::printf("      trace x %.2f floor %.2f eggs %.0f\n", t[0], t[1], t[2]);
            }
            check(run.cleared, "a level rolled anew: the game's bot clears it");
        }
    }
}

// the switches: a roll uses only the hazards switched on
void switches() {
    const MazeGenResult none = generateMaze(12, 170, 3, 31, &LevelCatalog::builtin(), 60, MazeOptions{false, false, false});
    std::string all;
    for (const std::string& r : none.rows) all += r;
    check(none.ok && all.find_first_of("SC()~") == std::string::npos, "a maze with saws, lasers and lava off: none of them (and it still passes)");
    // (a few seeds: one with lasers in it, none with saws or lava)
    MazeGenResult lasers;
    all.clear();
    for (uint32_t seed = 31; seed < 41; ++seed) {
        lasers = generateMaze(12, 170, 3, seed, &LevelCatalog::builtin(), 60, MazeOptions{false, true, false});
        all.clear();
        for (const std::string& r : lasers.rows) all += r;
        if (lasers.ok && all.find_first_of("()") != std::string::npos) break;
    }
    check(lasers.ok && all.find_first_of("()") != std::string::npos && all.find_first_of("SC~") == std::string::npos, "...lasers alone: wall lasers, no saws or lava");
    bool aimed = true; // (each laser fires into the corridor: air on the side it fires)
    for (const std::string& r : lasers.rows)
        for (size_t c = 0; c < r.size(); ++c) {
            if (r[c] == '(') aimed = aimed && c > 0 && r[c - 1] == '.';
            if (r[c] == ')') aimed = aimed && c + 1 < r.size() && r[c + 1] == '.';
        }
    check(aimed, "...each set into a stone side facing the corridor (air the way it fires)");
    GenOptions lasersOnly;
    lasersOnly.only = {"laser"};
    // (the rhythm-beat generator lays what its beats call for: a few rolls, between them every hazard switched on)
    auto rolls = [&](const GenOptions& o, const HazardType& t) { int n = 0, others = 0; for (uint32_t seed = 5; seed < 9; ++seed) {
        const LevelData d = bakeLevel(40, nullptr, seed, o);
        n += d.countOf(t);
        for (const HazardType* h : {&kSawHazard, &kLaserHazard, &kLavaHazard, &kDiscoHazard, &kSpringHazard}) if (h != &t && !o.has(h->id)) others += d.countOf(*h);
    } return std::pair{n, others}; };
    const auto beams = rolls(lasersOnly, kLaserHazard);
    check(beams.first > 0 && beams.second == 0, "courses rolled with lasers alone: lasers, no saws, lava, springs or disco balls");
    GenOptions sawsOnly;
    sawsOnly.only = {"saw"};
    const auto blades = rolls(sawsOnly, kSawHazard);
    check(blades.first > 0 && blades.second == 0, "courses rolled with saws alone: no lasers, lava, springs or disco balls");
    check(rolls({}, kLaserHazard).first > 0 && rolls({}, kLavaHazard).first > 0 && rolls({}, kSpringHazard).first > 0, "...with everything on: lasers, lava and springs too");
}

// a maze put into level 20 (at 40% of the way, where the editors put one) and the game's bot through the level: 1 cleared,
// 0 crashed, -1 not placed
int playPlaced(const MazeGenResult& m, uint32_t roll = 0, int level = 20) {
    auto d = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
    if (roll && !d->regenerate(roll)) return -1; // (a course rolled anew, the flat one the editor's maze goes into)
    const std::optional<Gauntlet> pg = Gauntlet::parse("x", m.rows);
    if (!m.ok || !pg || !d->setGrid("maze_20", m.rows)) return -1;
    const int at = int(0.40 * d->course().length / U);
    if (!d->placeGauntlet("maze_20", at)) return -1;
    LevelCatalog play;
    play.setGauntlet("maze_20", m.rows);
    play.setLevel(level, d->text());
    const GameRun run = playLevel(level, &play);
    if (!run.cleared) {
        const double x0 = Campaign::gauntletAt(level, &play);
        std::printf("      (the bot crashed at x %.2f, y %.2f: maze col %d row %d, death %d)\n", run.x, run.y, int(std::floor((run.x - x0) / U)), int(std::lround(run.y / U)), run.death);
    }
    return run.cleared ? 1 : 0;
}

// mazes with springs (up a step in the floor, onto a ledge; at density 0 and 8): the game's bot flies each one, in the real
// game, and clears the level with it
void springsPlay() {
    int found = 0, played = 0, cleared = 0, up = 0, onto = 0;
    for (const bool wantUp : {true, false})
        for (const int density : {0, 8}) {
            int here = 0;
            for (uint32_t seed = 1; seed <= 600 && here < 3; ++seed) {
                const MazeGenResult m = generateMaze(20, 170, 3 + 2 * int(seed % 2), seed * 6101u + uint32_t(density) + (wantUp ? 17u : 0u), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density, true, false});
                const Springs sp = springsOf(m);
                if (!m.ok || (wantUp ? sp.up == 0 : sp.onto == 0)) continue;
                ++found; ++here;
                up += sp.up > 0; onto += sp.onto > 0;
                const int r = playPlaced(m, 313 + seed);
                if (r >= 0) { ++played; cleared += r; }
                if (r == 0) { std::printf("      a spring maze the bot crashed in (seed %u, density %d, %d up, %d onto ledges):\n", m.seed, density, sp.up, sp.onto); for (const std::string& row : m.rows) std::printf("        %s\n", row.c_str()); }
            }
        }
    std::printf("      %d mazes with springs (%d up into a room, %d onto a ledge), %d played in level 20, %d cleared\n", found, up, onto, played, cleared);
    check(found >= 10 && played >= 10 && up > 0 && onto > 0, "mazes with springs (up into a room, onto a ledge) are made at 0 and at 8, and put into level 20");
    check(cleared == played, "...and the game's bot flies each, and clears the level");
}

// a maze with a low exit (a fixture: a bare corridor with its exit on the street's row; the generator's towers end high), put in
// front of a wall far too tall to climb from its porch (9 high; the curve's tallest climb is 4, the meter holds 2 and a bit): the
// editor lays a stair up, a run-up before each climb, and the game's bot clears the level
std::vector<std::string> bareCorridor(int width) {
    const std::string wall(size_t(width), '#'), air(size_t(width), '.');
    return {wall, "#" + std::string(size_t(width - 2), '.') + "#", air, air, ">" + std::string(size_t(width - 2), '.') + "X", wall};
}

void lowExitTallWall() {
    const int level = 20;
    int played = 0, cleared = 0, stairs = 0;
    for (const int width : {80, 110, 140, 170}) {
        const std::vector<std::string> rows = bareCorridor(width);
        const std::optional<Gauntlet> pg = Gauntlet::parse("x", rows);
        check(pg && pg->exitRow() <= 2, "a fixture corridor with a low exit");
        if (!pg) continue;
        auto d = EditorDoc::open(level, "length 330\ndifficulty 350 0.35\nraids none\ncourse\n30 0 crate\n500 9 crate\n80 0 crate\n");
        if (!d || !d->setGrid("maze_low", rows)) continue;
        const int at = 30;
        if (!d->placeGauntlet("maze_low", at)) continue;
        const int z = at + pg->cols() + Level::kPorch;
        int h = pg->exitRow(), steps = 0, tall = 0;
        for (int i = z; i < int(d->course().cols.size()) && h < 9; ++i) {
            const int k = d->course().cols[size_t(i)].h;
            if (k > h) { ++steps; tall = std::max(tall, k - h); h = k; }
        }
        stairs += steps >= 2 && tall <= 4;
        LevelCatalog play;
        play.setGauntlet("maze_low", rows);
        if (!play.setLevel(level, d->text())) continue;
        const GameRun run = playLevel(level, &play);
        ++played;
        cleared += run.cleared;
        if (!run.cleared) std::printf("      (exit row %d, width %d: the bot crashed at %.1f m, y %.2f)\n", pg->exitRow(), width, run.x, run.y);
    }
    std::printf("      %d low-exit corridors before a wall 9 high: %d with a stair up (2 climbs or more, 4 at the most each), %d cleared\n", played, stairs, cleared);
    check(played >= 3 && stairs == played, "a maze with a low exit before a wall too tall to climb: the editor lays a stair up");
    check(cleared == played, "...and the game's bot clears the level with each");
}

// mazes with ledges (and at density 8, and 10 too): the game's bot clears the level with each
void ledgesPlay() {
    int found = 0, played = 0, cleared = 0;
    for (const int density : {0, 8}) {
        int here = 0;
        for (uint32_t seed = 1; seed <= 120 && here < 4; ++seed) {
            const MazeGenResult m = generateMaze(20, 170, 3 + 2 * int(seed % 2), seed * 9973u + uint32_t(density), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density, true, false});
            if (!m.ok || packOf(m).ledges == 0) continue;
            ++found; ++here;
            const int r = playPlaced(m, 777 + seed);
            if (r >= 0) { ++played; cleared += r; }
            else std::printf("      (a ledge maze, seed %u, density %d: not placed)\n", m.seed, density);
            if (r == 0) { std::printf("      a ledge maze the bot crashed in (seed %u, density %d):\n", m.seed, density); for (const std::string& row : m.rows) std::printf("        %s\n", row.c_str()); }
        }
    }
    std::printf("      %d mazes with ledges, %d played in level 20, %d cleared\n", found, played, cleared);
    check(found >= 6 && played >= 4, "mazes with ledges are made (at 0 and at 8), and put into level 20");
    check(cleared == played, "...and the game's bot clears the level with each");
}

} // namespace

// many mazes made for one level (the shipped level 20), each put in where the editors put one (40% of the way): a fair
// way onto it and off it (no drop the checker finds no time to land from), and the game's bot through the level
void manyOnOneLevel() {
    const int level = 20;
    int unfair = 0, crashed = 0, crashedAfter = 0, made = 0;
    const uint32_t count = std::getenv("MAZE_MANY") && std::atoi(std::getenv("MAZE_MANY")) > 1 ? uint32_t(std::atoi(std::getenv("MAZE_MANY"))) : seedsOf(18); // (MAZE_MANY=100: a hundred)
    for (uint32_t seed = 1; seed <= count; ++seed) {
        auto d = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
        const MazeGenResult m = generateMaze(level, 170, 3 + 2 * int(seed % 3), seed * 7919u, &LevelCatalog::builtin()); // (3, 5 and 7 floors: exits at any height)
        const std::optional<Gauntlet> pg = Gauntlet::parse("x", m.rows);
        if (!m.ok || !pg || !d->setGrid("maze_20", m.rows)) continue;
        // (at 40% of the way, where the editors put one: a low exit before a tall wall gets a stair up, a high one a stair down)
        const int at = int(0.40 * d->course().length / U);
        if (!d->placeGauntlet("maze_20", at)) continue;
        ++made;
        const auto sp = spanOf(d->course(), "maze_20");
        bool bad = false;
        for (const auto& n : d->report().notes)
            if (n.x >= sp->c0 * U - 40 && n.x < sp->c1 * U + 120 && (n.text.find("no time to land") != std::string::npos || n.text.find("saw") != std::string::npos || n.text.find("Saw") != std::string::npos || n.text.find("Lava") != std::string::npos || n.text.find("spring") != std::string::npos)) { bad = true; std::printf("      seed %u: %.1f %s\n", seed * 7919u, n.x, n.text.c_str()); }
        LevelCatalog play;
        play.setGauntlet("maze_20", m.rows);
        play.setLevel(level, d->text());
        const GameRun run = playLevel(level, &play);
        if (!run.cleared) {
            std::printf("      seed %u: the bot crashed at %.1f m, y %.2f, death %d (the maze %.1f .. %.1f)\n", seed * 7919u, run.x, run.y, run.death, sp->c0 * U, sp->c1 * U);
            if (run.x < sp->c1 * U) for (const std::string& r : m.rows) std::printf("        %s\n", r.substr(0, 14).c_str());
            const auto g = Gauntlet::parse("x", m.rows);
            for (const auto& t : g->botTrace()) if (t.s.c < 6) std::printf("        solver c %d b %d n %d d %d k %d z %d lay %d move %d again %d\n", t.s.c, t.s.b, t.s.n, t.s.d, t.s.k, t.s.z, t.lay, int(t.move), int(g->shouldLay(t.s)));
            for (int c = 1; c <= 3; ++c) for (int n = 1; n <= 5; ++n) for (int dd : {-1, 1}) for (int k = 0; k < 4; ++k) {
                const Gauntlet::State st{c, 7, n, dd, k, 0};
                Gauntlet::State o1, o0;
                std::printf("        state c %d n %d d %d k %d: lay %d (lay-> %d, no-> %d)\n", c, n, dd, k, int(g->shouldLay(st)), int(g->step(st, 1, o1)), int(g->step(st, 0, o0)));
            }
        }
        if (bad || !run.cleared) {
            double x = 0;
            for (const Placed& p : d->course().items) if (std::abs(p.x() - run.x) < 14) std::printf("        item %s at %.2f %.2f %.2f\n", p.type->id, p.x(), p.at(1), p.at(2));
            for (const auto& w : toData(d->course()).course) {
                if ((x + w.cells * U > sp->c1 * U - 2 && x < sp->c1 * U + 40) || (!run.cleared && x + w.cells * U > run.x - 14 && x < run.x + 6)) std::printf("        wall %.1f..%.1f h %d ceil %d top %d %s\n", x, x + w.cells * U, w.h, w.ceil, w.top, w.gauntlet.c_str());
                x += w.cells * U;
            }
        }
        // (a crash past the maze's porch is the stair down and the level's own saws, not the maze: level 20's pair at 376 m)
        const bool inMaze = !run.cleared && run.x < sp->c1 * U + Level::kPorch * U;
        unfair += bad;
        crashed += inMaze;
        crashedAfter += !run.cleared && !inMaze;
    }
    std::printf("      %d of %u placed, %d unfair, %d crashed in the maze, %d past it\n", made, count, unfair, crashed, crashedAfter);
    check(made >= int(count) * 10 / 18 && unfair == 0, "mazes made for level 20, put in: a fair way onto each and off it");
    check(crashed == 0, "...and the game's bot clears the maze in each");
    check(crashedAfter == 0, "...and the game's bot clears the whole level, off the porch and past level 20's own saws, every time");
}

bool gameVsSolver(const MazeGenResult& m, int level, int& solver, int& shown, int& wanted);

// the numbers (MAZE_REPORT=1): level 20, 170 wide, each density and floor count, 10 seeds (MAZE_SEEDS=n: n)
void report() {
    const uint32_t seeds = std::getenv("MAZE_SEEDS") ? uint32_t(std::atoi(std::getenv("MAZE_SEEDS"))) : 10;
    std::printf("density floors  ready  rows  flat floors  ledges springs  game-solver max diff  tries  secs\n");
    for (const int density : {0, 10}) for (const int floors : {3, 5, 7}) {
        int made = 0, flat = 0, floorsAll = 0, maxDiff = 0, played = 0;
        double rows = 0, ledges = 0, springs = 0, tries = 0, seconds = 0;
        for (uint32_t seed = 1; seed <= seeds; ++seed) {
            const auto t0 = std::chrono::steady_clock::now();
            const MazeGenResult r = generateMaze(20, 170, floors, seed * 7717u + uint32_t(density), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density});
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            tries += r.tries;
            if (!r.ok) continue;
            ++made;
            rows += double(r.rows.size());
            for (const auto& f : sectionsOf(r)) { ++floorsAll; flat += std::all_of(f.begin(), f.end(), [&](int v) { return v == f[0]; }); }
            ledges += packOf(r).ledges; springs += springsOf(r).springs;
            int solver = 0, shown = 0, wanted = 0;
            if (gameVsSolver(r, 20, solver, shown, wanted)) { ++played; maxDiff = std::max(maxDiff, std::abs(shown - solver)); }
        }
        const double n = std::max(1, made);
        std::printf("%7d %6d %2d/%u %5.1f %6d/%d %9.2f %7.2f %12d (%d played) %8.1f %5.2f\n", density, floors, made, seeds, rows / n, flat, floorsAll, ledges / n, springs / n, maxDiff, played, tries / seeds, seconds / seeds);
    }
}

// the near misses the game shows against the solver's (MAZE_MISSES=1): level 20, 170 wide, density 10 and 0, 10 seeds each, 3/5/7 floors
int countIn(const std::string& json, const char* key) { // (the entries of an array of arrays: [[..],[..]])
    const size_t at = json.find(key);
    if (at == std::string::npos) return -1;
    int n = 0, depth = 0;
    for (size_t i = json.find('[', at); i < json.size(); ++i) {
        if (json[i] == '[') { if (++depth == 2) ++n; }
        else if (json[i] == ']' && --depth == 0) break;
    }
    return n;
}
int numAfter(const std::string& json, const char* key) { const size_t at = json.find(key); return at == std::string::npos ? -1 : std::atoi(json.c_str() + at + std::strlen(key)); }

// a maze put into level 20 as the editors put one and played by the game's bot, as the iPad's checklist reads it: the near misses the
// game shows (perfects + close calls), the solver's count, what the maze's density wants; false if it couldn't be placed
bool gameVsSolver(const MazeGenResult& m, int level, int& solver, int& shown, int& wanted) {
    auto d = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
    if (!m.ok || !d->setGrid("maze_20", m.rows) || !d->placeGauntlet("maze_20", int(0.40 * d->course().length / U))) return false;
    LevelCatalog play;
    play.setGauntlet("maze_20", m.rows);
    play.setLevel(level, d->text());
    std::vector<std::string> lines;
    for (const std::string& dd : m.description) if (dd.rfind("hard ", 0) == 0) lines.push_back("; " + dd);
    lines.insert(lines.end(), m.rows.begin(), m.rows.end());
    const std::optional<Gauntlet> g = Gauntlet::parse("maze_20", lines);
    if (!g) return false;
    const GauntletGame game = gauntletInGame(level, &play);
    solver = g->solve().nearMisses;
    shown = game.perfects + game.closeCalls;
    wanted = GauntletReady::wantMissesAt(g->hard());
    return game.out;
}

// a tower of 7 reaches 50 rows or more: its exit's porch is a course wall that high (60 eggs at the most), the camera follows her up
// it, and the game's bot clears the level (the porch, its stair down off that height, the saws after)
void tallTower() {
    int played = 0, cleared = 0, tallest = 0;
    for (uint32_t seed = 1; seed <= seedsOf(40) && played < 3; ++seed) {
        const MazeGenResult m = generateMaze(20, 170, 7, seed * 4421u, &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, 0, true, false});
        const std::optional<Gauntlet> g = Gauntlet::parse("x", m.rows);
        if (!m.ok || !g || g->exitRow() < 48) continue;
        auto d = EditorDoc::open(20, levelText(*LevelCatalog::builtin().level(20)));
        if (!d->setGrid("maze_20", m.rows) || !d->placeGauntlet("maze_20", int(0.40 * d->course().length / U))) continue;
        const auto sp = spanOf(d->course(), "maze_20");
        const int porch = sp ? d->course().cols[size_t(sp->c1)].h : 0;
        tallest = std::max(tallest, porch);
        const int r = playPlaced(m, 0, 20);
        if (r >= 0) { ++played; cleared += r; }
        std::printf("      7 floors, %zu rows, exit at row %d, porch %d high: the bot %s\n", m.rows.size(), g->exitRow(), porch, r == 1 ? "cleared the level" : "crashed");
    }
    check(played >= 2 && tallest > 40 && tallest <= 60, "a tower of 7: its exit and porch above the old 40-row cap (up to 60)");
    check(cleared == played, "...and the game's bot climbs it and clears the level");
}

// the iPad's "The game shows its near misses": a fresh maze (any floors, density 0 and 10) shows as many as it must and within 2 of the
// solver's count; the game's bot counts the lay gap as the solver does (a bounce is one move), so the numbers agree
void nearMissesAgree() {
    int n = 0, bad = 0, worst = 0;
    for (const int density : {0, 10}) for (const int floors : {3, 5, 7}) for (uint32_t seed = 1; seed <= seedsOf(8); ++seed) {
        const MazeGenResult m = generateMaze(20, 170, floors, seed * 3301u + uint32_t(density), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density});
        int solver = 0, shown = 0, wanted = 0;
        if (!gameVsSolver(m, 20, solver, shown, wanted)) { ++bad; continue; }
        ++n;
        worst = std::max(worst, std::abs(shown - solver));
        if (shown < wanted || std::abs(shown - solver) > 2) { ++bad; std::printf("      (%d floors, density %d, seed %u: the game shows %d, the solver counts %d, %d wanted)\n", floors, density, m.seed, shown, solver, wanted); }
    }
    std::printf("      %d mazes played in level 20: the game and the solver differ by %d at the most\n", n, worst);
    check(n >= 6 && bad == 0, "a fresh maze: the game shows its near misses (as many as asked for, within 2 of the solver's count), at every size and density");
}

void missesReport() {
    const int level = 20;
    std::printf("density floors seed  solver  way-list  game(perfects+calls)  diff\n");
    int worst = 0;
    for (const int density : {10, 0}) for (const int floors : {3, 5, 7}) for (uint32_t seed = 1; seed <= 10; ++seed) {
        const auto t0 = std::chrono::steady_clock::now();
        const MazeGenResult m = generateMaze(level, 170, floors, seed * 7717u + uint32_t(density), &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, density});
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        auto d = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
        if (!m.ok || !d->setGrid("maze_20", m.rows) || !d->placeGauntlet("maze_20", int(0.40 * d->course().length / U))) continue;
        LevelCatalog play;
        play.setGauntlet("maze_20", m.rows);
        play.setLevel(level, d->text());
        std::vector<std::string> lines;
        for (const std::string& dd : m.description) if (dd.rfind("hard ", 0) == 0) lines.push_back("; " + dd);
        lines.insert(lines.end(), m.rows.begin(), m.rows.end());
        const std::optional<Gauntlet> g = Gauntlet::parse("maze_20", lines);
        if (!g) continue;
        const std::string js = gauntletReportJson(*g, "maze_20", level, true, &play);
        const int solver = numAfter(js, "\"nearMisses\":"), list = countIn(js, "\"wayNearMisses\":");
        const size_t at = js.find("\"game\":");
        const int shown = numAfter(js.substr(at), "\"perfects\":") + numAfter(js.substr(at), "\"closeCalls\":");
        std::printf("%7d %6d %4u %7d %9d %12d %8d   solver P%d S%d C%d, game P%d C%d  tries %d, %.2f s\n", density, floors, seed, solver, list, shown, shown - solver, numAfter(js, "\"perfects\":"), numAfter(js, "\"shaves\":"), numAfter(js, "\"squeezes\":"), numAfter(js.substr(at), "\"perfects\":"), numAfter(js.substr(at), "\"closeCalls\":"), m.tries, secs);
        worst = std::max(worst, std::abs(shown - solver));
        if (std::getenv("MAZE_MISSES_LIST") && std::string(std::getenv("MAZE_MISSES_LIST")) == std::to_string(density) + "," + std::to_string(floors) + "," + std::to_string(seed)) {
            const size_t w = js.find("\"wayNearMisses\":");
            (void)w;
            for (const std::string& row : m.rows) std::printf("        %s\n", row.c_str());
            const GameRun run = playLevel(level, &play);
            const double x0 = Campaign::gauntletAt(level, &play);
            std::printf("      game events in the maze (col, row, kind):");
            for (const auto& e : run.events) if (e[0] >= x0 && e[0] < x0 + g->cols() * U) std::printf(" [%d,%.0f,%d]", int((e[0] - x0) / U), e[1] / U, int(e[2]));
                        std::printf("\n      solver:");
            for (const Gauntlet::NearMissAt& n : Gauntlet::nearMisses(g->botTrace())) std::printf(" [%d,%d,%d,%d]", n.c, n.b, int(n.kind), n.step);
            std::printf("\n");
        }
    }
    std::printf("worst difference %d\n", worst);
}

void bothRate() {
    for (const int floors : {3, 5, 7}) {
        int n = 0, both = 0, lasers = 0, lava = 0;
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            const MazeGenResult m = generateMaze(12, 160, floors, seed * 977u, &LevelCatalog::builtin(), 60, MazeOptions{true, true, true, 0, true, false});
            if (!m.ok) continue;
            std::string all;
            for (const std::string& r : m.rows) all += r;
            const bool l = all.find_first_of("()") != std::string::npos, v = all.find('~') != std::string::npos;
            ++n; both += l && v; lasers += l; lava += v;
        }
        std::printf("%d floors: %d mazes, %d with lasers, %d with lava, %d both\n", floors, n, lasers, lava, both);
    }
}


// maze chunks (Chunk.h): the generator stamps them into a stretch of a floor
Chunk testWell(const std::string& id, int from, int to, int weight, int uses, bool mirror, std::vector<std::string> rows = {}) {
    Chunk c;
    c.id = id;
    c.maze = true;
    c.ranges = {{from, to}};
    c.weight = weight;
    c.uses = uses;
    c.mirror = mirror;
    // a small sacred well: a step up to a rim, the well itself (a saw in its floor) and the way out
    c.rows = rows.empty() ? std::vector<std::string>{".....", ".....", "##...", "##S##"} : rows;
    return c;
}

MazeOptions withChunks(std::vector<Chunk> chunks, bool game = false) {
    MazeOptions o{true, true, true, 0, true, game};
    o.chunks = std::move(chunks);
    return o;
}

// the chunk's cells at its reported place in the grid: as drawn, or mirrored if the grid shows it mirrored
bool chunkIsThere(const std::vector<std::string>& rows, const MazeChunkUse& u, const Chunk& c) {
    const Chunk shown = u.mirrored ? mirrored(c) : c;
    if (u.r1 - u.r0 + 1 != int(shown.rows.size()) || u.c1 - u.c0 + 1 != int(shown.rows.front().size())) return false;
    for (int j = 0; j < int(shown.rows.size()); ++j)
        if (rows[size_t(u.r0 + j)].substr(size_t(u.c0), size_t(u.c1 - u.c0 + 1)) != shown.rows[size_t(j)]) {
            return false;
        }
    return true;
}

void chunks() {
    const int level = 20;
    const uint32_t seeds = seedsOf(24);
    // in range 20-20: in the mazes made for level 20, never in those for 21, where the maze is the one without chunks
    const Chunk well = testWell("sacred-well", 20, 20, 10, 0, true);
    int with = 0, made = 0, onOdd = 0, flippedOdd = 0, flippedEven = 0, there = 0, placed = 0, ready = 0;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const int floors = 3 + 2 * int(seed % 3);
        const MazeGenResult m = generateMaze(level, 170, floors, seed * 31u, &LevelCatalog::builtin(), 60, withChunks({well}));
        ++made;
        ready += m.ok;
        if (!m.ok) continue;
        with += !m.chunks.empty();
        bool lined = false;
        for (const std::string& d : m.description) lined = lined || d.rfind("chunks: sacred-well", 0) == 0;
        check(lined == !m.chunks.empty(), "the description names the chunks placed (and only then)");
        for (const MazeChunkUse& u : m.chunks) {
            ++placed;
            there += chunkIsThere(m.rows, u, well);
            onOdd += u.floor % 2 == 1;
            (u.floor % 2 == 1 ? flippedOdd : flippedEven) += u.mirrored;
        }
        const MazeGenResult other = generateMaze(level + 1, 170, floors, seed * 31u, &LevelCatalog::builtin(), 60, withChunks({well}));
        const MazeGenResult none = generateMaze(level + 1, 170, floors, seed * 31u, &LevelCatalog::builtin(), 60, withChunks({}));
        check(other.chunks.empty() && other.rows == none.rows && other.description == none.description && other.seed == none.seed, "level 21: no chunk, the maze exactly as without chunks");
    }
    std::printf("      %d of %d made; %d with a chunk, %d placed, %d on left-running floors, %d mirrored there\n", ready, made, with, placed, onOdd, flippedOdd);
    check(ready >= made * 3 / 4, "mazes with a chunk in range are made (ready, and shown in the game)");
    check(with >= ready * 3 / 4 && placed > 0, "a chunk in range 20-20 weighs 10: in most mazes made for level 20");
    check(there == placed, "...each where it says it is, in the grid, as drawn or mirrored");
    check(onOdd > 0 && flippedOdd > 0 && flippedEven > 0, "...with Mirror on, on floors that run either way, and mirrored in the grid on both kinds (the left-running floor draws it flipped, a flip as she runs undoes that)");

    // Mirror off: only the floors that run right, as drawn
    const Chunk fixed = testWell("fixed-well", 20, 20, 10, 0, false);
    int fixedPlaced = 0, fixedBad = 0;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const MazeGenResult m = generateMaze(level, 170, 3 + 2 * int(seed % 3), seed * 17u, &LevelCatalog::builtin(), 60, withChunks({fixed}));
        for (const MazeChunkUse& u : m.chunks) { ++fixedPlaced; fixedBad += u.floor % 2 == 1 || u.mirrored || !chunkIsThere(m.rows, u, fixed); }
    }
    check(fixedPlaced > 0 && fixedBad == 0, "Mirror off: only on floors that run right, and as drawn");

    // uses: at most that many in a maze; no limit: repeats
    int most1 = 0, most2 = 0, mostAny = 0;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const int floors = 3 + 2 * int(seed % 3);
        most1 = std::max(most1, int(generateMaze(level, 170, floors, seed * 5u, &LevelCatalog::builtin(), 60, withChunks({testWell("once", 20, 20, 10, 1, true)})).chunks.size()));
        most2 = std::max(most2, int(generateMaze(level, 170, floors, seed * 5u, &LevelCatalog::builtin(), 60, withChunks({testWell("twice", 20, 20, 10, 2, true)})).chunks.size()));
        mostAny = std::max(mostAny, int(generateMaze(level, 170, floors, seed * 5u, &LevelCatalog::builtin(), 60, withChunks({testWell("any", 20, 20, 10, 0, true)})).chunks.size()));
    }
    check(most1 == 1, "uses 1: never more than one in a maze (and one is placed)");
    check(most2 == 2, "uses 2: never more than two");
    check(mostAny > 2, "no limit: repeats");

    // with no chunk to place, the same seed gives the same maze as ever: none handed over, none in range, or none that fits
    // (too tall for any stretch, a different kind, under 3 x 3)
    const std::vector<std::string> tall(30, std::string(5, '.'));
    Chunk course = well;
    course.maze = false;
    Chunk tiny = well;
    tiny.rows = {"..", "##"};
    for (uint32_t seed : {42u, 43u, 1000u}) {
        const auto plain = layMaze(170, 3, seed, 3.0);
        check(layMaze(170, 3, seed, 3.0, nullptr, MazeOptions{}) == plain, "no chunks: the maze a seed always made");
        check(layMaze(170, 5, seed, 3.0, nullptr, withChunks({testWell("tall", 1, 99, 10, 0, true, tall)})) == layMaze(170, 5, seed, 3.0), "a chunk too tall for every stretch: the same maze");
        check(layMaze(170, 5, seed, 3.0, nullptr, withChunks({course, tiny})) == layMaze(170, 5, seed, 3.0), "a course chunk, a chunk under 3 x 3: the same maze");
        std::vector<MazeChunkUse> used;
        layMaze(170, 3, seed, 3.0, nullptr, withChunks({well}), &used);
        check(!used.empty(), "layMaze alone (it takes every chunk it is handed) lays some");
    }

    // the maze with a chunk in it is played by the game's bot
    int cleared = 0, played = 0;
    for (uint32_t seed = 1; seed <= std::max(3u, seeds / 4); ++seed) {
        auto d = EditorDoc::open(level, levelText(*LevelCatalog::builtin().level(level)));
        const MazeGenResult m = generateMaze(level, 170, 3 + 2 * int(seed % 3), seed * 7919u, &LevelCatalog::builtin(), 60, withChunks({well}, true));
        if (!m.ok || m.chunks.empty() || !d->setGrid("maze_20", m.rows) || !d->placeGauntlet("maze_20", int(0.40 * d->course().length / U))) continue;
        LevelCatalog play;
        play.setGauntlet("maze_20", m.rows);
        play.setLevel(level, d->text());
        ++played;
        cleared += playLevel(level, &play).cleared;
    }
    std::printf("      the game's bot cleared %d of %d mazes with chunks\n", cleared, played);
    check(played > 0 && cleared == played, "every maze with a chunk is cleared by the game's bot");

}

// a chunk with a spring never goes on a floor that runs left or is flipped (Mirror on or not); one with a wall laser keeps its beam in the stretch
void chunkKinds() {
    const int level = 20;
    const uint32_t seeds = seedsOf(24);
    Chunk spring = testWell("spring-well", 20, 20, 10, 0, true, {".....", ".....", "##...", "##^##"});
    check(!spring.canMirror() && !spring.mirrors(), "a maze chunk with a spring can't be mirrored");
    int placed = 0, bad = 0;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const MazeGenResult m = generateMaze(level, 170, 3 + 2 * int(seed % 3), seed * 13u, &LevelCatalog::builtin(), 60, withChunks({spring}));
        for (const MazeChunkUse& u : m.chunks) { ++placed; bad += u.floor % 2 == 1 || u.mirrored; }
    }
    check(placed > 0 && bad == 0, "...so it goes on floors that run right only, as drawn");
    Chunk laser = testWell("laser-well", 20, 20, 10, 0, true, {"......", "......", "#.....", "#)....", "###S##"});
    int laserPlaced = 0, notThere = 0;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const MazeGenResult m = generateMaze(level, 170, 3 + 2 * int(seed % 3), seed * 29u, &LevelCatalog::builtin(), 60, withChunks({laser}));
        for (const MazeChunkUse& u : m.chunks) {
            ++laserPlaced;
            notThere += !chunkIsThere(m.rows, u, laser);
            // (ready means the checker parsed it: exactly one way through, the beam's cuts counted)
        }
        if (m.ok) check(Gauntlet::parse("x", m.rows).has_value(), "a maze with a laser chunk reads as a gauntlet");
    }
    std::printf("      the laser chunk: %d placed\n", laserPlaced);
    check(laserPlaced > 0 && notThere == 0, "a chunk with a wall laser is placed too, where it says");
}

// a roll never comes back empty because of a chunk: a demanding one (three saws in its five cells) that no maze holds is
// laid in smaller numbers, or left out, and the result says so
void chunksGiveUp() {
    const int level = 20;
    const std::vector<std::string> rows = {".....", "..#..", ".###.", "S#S#S"};
    const Chunk hard = testWell("three-saws", 20, 20, 10, 0, true, rows);
    int made = 0, total = 0, fewer = 0, left = 0, said = 0, worst = 0, badLeft = 0, badFewer = 0;
    double slowest = 0;
    const uint32_t seeds = std::getenv("MAZE_PROBE") ? 12 : seedsOf(4);
    for (uint32_t seed = 1; seed <= seeds; ++seed)
        for (const int floors : {3, 5, 7}) {
            const auto t0 = std::chrono::steady_clock::now();
            const MazeGenResult m = generateMaze(level, 170, floors, seed * 977u, &LevelCatalog::builtin(), 60, withChunks({hard}, true));
            slowest = std::max(slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            ++total;
            made += m.ok;
            fewer += !m.chunksReduced.empty();
            left += !m.chunksDropped.empty();
            worst = std::max(worst, m.tries);
            const bool gaveUp = !m.chunksReduced.empty() || !m.chunksDropped.empty();
            said += gaveUp == !m.chunkNote.empty() && (!gaveUp || m.chunkNote.find("three-saws") != std::string::npos);
            badLeft += !m.chunksDropped.empty() && !(m.chunks.empty() && m.chunksReduced.empty());
            badFewer += !m.chunksReduced.empty() && !(m.chunksDropped.empty() && m.chunks.size() <= 1);
            if (std::getenv("MAZE_PROBE")) std::printf("   %d floors seed %u: ok %d, %d tries, placed %zu, reduced %zu, dropped %zu\n", floors, seed, m.ok, m.tries, m.chunks.size(), m.chunksReduced.size(), m.chunksDropped.size());
        }
    std::printf("      the demanding chunk: %d of %d made a maze (%d with fewer, %d without it), the most tries %d, the slowest %.1f s\n", made, total, fewer, left, worst, slowest);
    check(made == total, "a chunk no maze holds: a maze is made all the same");
    check(said == total, "...the result names what was given up (chunkNote), and nothing when nothing was");
    check(fewer + left > 0, "...a demanding chunk was given up on in some of them");
    int quiet = 0;
    for (uint32_t seed = 1; seed <= 3; ++seed) {
        const MazeGenResult m = generateMaze(level, 170, 5, seed * 31u, &LevelCatalog::builtin(), 60, withChunks({testWell("sacred-well", 20, 20, 10, 0, true)}));
        quiet += m.ok && m.chunkNote.empty() && m.chunksDropped.empty() && m.chunksReduced.empty();
    }
    check(quiet == 3, "a chunk that fits: nothing given up, no note");
    check(badLeft == 0, "...a chunk left out is not in the maze");
    check(badFewer == 0, "...and fewer means one use at most");
    check(worst <= 70, "...in a bounded number of tries (a dozen unsolvable ones with the chunks, six with fewer, then the maze's own), not a hundred and twenty");
    check(slowest < 8, "...and quickly, not the ten seconds of a hundred and twenty tries");
}

// the numbers (MAZE_CHUNK_REPORT=1): a 5 x 4 well in range for level 20 at weight 1, 5 and 10 (and none, and a demanding one: three
// blades in its five cells), 3, 5 and 7 floors, 10 seeds (MAZE_SEEDS=n: n), the game's gate on
void chunkReport() {
    const int level = 20;
    const uint32_t n = std::getenv("MAZE_SEEDS") ? uint32_t(std::atoi(std::getenv("MAZE_SEEDS"))) : 10;
    struct Row { const char* what; int weight; std::vector<std::string> rows; };
    const std::vector<Row> kinds = {{"no chunk  ", 0, {}}, {"well w1    ", 1, {}}, {"well w5    ", 5, {}}, {"well w10   ", 10, {}}, {"demanding w5", 5, {".....", "..#..", ".###.", "S#S#S"}}, {"demanding w10", 10, {".....", "..#..", ".###.", "S#S#S"}}};
    for (const Row& k : kinds)
        for (const int floors : {3, 5, 7}) {
            int ok = 0, tries = 0, placed = 0, mirroredN = 0, onLeft = 0;
            double secs = 0;
            for (uint32_t seed = 1; seed <= n; ++seed) {
                const auto t0 = std::chrono::steady_clock::now();
                const MazeGenResult m = generateMaze(level, 170, floors, seed * 977u, &LevelCatalog::builtin(), 60, withChunks(k.weight ? std::vector<Chunk>{testWell("sacred-well", 20, 20, k.weight, 0, true, k.rows)} : std::vector<Chunk>{}, true));
                secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                tries += m.tries;
                ok += m.ok;
                for (const MazeChunkUse& u : m.chunks) { ++placed; mirroredN += u.mirrored; onLeft += u.floor % 2; }
            }
            std::printf("%-13s %d floors: %.1f placed a maze, ready %d/%u, %.1f tries, %.0f%% mirrored in the grid (%.0f%% on left-running floors), %.2f s a maze\n", k.what, floors, double(placed) / n, ok, n, double(tries) / n, placed ? 100.0 * mirroredN / placed : 0.0, placed ? 100.0 * onLeft / placed : 0.0, secs / n);
        }
}

int main() {
    if (std::getenv("MAZE_CHUNK_REPORT")) { chunkReport(); return 0; }
    if (std::getenv("MAZE_CHUNKS")) { chunks(); chunkKinds(); chunksGiveUp(); return test::finish("MazeGen"); }
    if (std::getenv("MAZE_BOTH")) { bothRate(); return 0; }
    if (std::getenv("MAZE_MISSES")) { missesReport(); return 0; }
    if (std::getenv("MAZE_TALL")) { tallTower(); return test::finish("MazeGen"); }
    if (std::getenv("MAZE_REPORT")) { report(); return 0; }
    if (std::getenv("MAZE_MANY")) { manyOnOneLevel(); return test::finish("MazeGen"); } // (just the mazes put into level 20)
    if (std::getenv("MAZE_SPRINGS")) { springMazes(); return test::finish("MazeGen"); } // (just the springs' numbers)
    if (std::getenv("MAZE_PLAY")) { springsPlay(); lowExitTallWall(); return test::finish("MazeGen"); }
    seeds();
    towers();
    bent();
    densities();
    sizes();
    together();
    rolledCourses();
    switches();
    manyOnOneLevel();
    tallTower();
    nearMissesAgree();
    ledgesPlay();
    lowExitTallWall();
    springsPlay();
    chunks();
    chunkKinds();
    chunksGiveUp();
    return test::finish("MazeGen");
}
