// The level designer's checker (desktop builds; the level editor runs it): a level file (assets/levels, LevelData.h)
// judged as the tests judge the campaign's levels, and played by the bot in the real game; or a level baked by the
// generator (how the levels were first laid, and how a new one starts).
//   level_check --bake N [--gauntlet NAME|none]   level N's course as the generator lays it (its seed, its length, its
//                                                  saws, its gauntlet at 40% of the way), as a level file
//   level_check --file PATH|- --level N [--gauntlet-file PATH ...] [--json] [--game]
//       its course: walls, barriers, saws, disco balls, gauntlets; the egg budget (a careful player's meter, stacking
//       just enough: where a climb or a blade asks for more than it holds); notes where it reads unfairly (a saw on a
//       slope or under a barrier, no landing run after a drop, a wall no one can climb under the barrier before it, a
//       gauntlet's door off its forecourt's height); --game: the real game, the bot playing it from its start (headless,
//       as GameDev_test does), the layouts in the --gauntlet-files standing in for those gauntlets. --json: all of it
//       as one object (the level editor's)
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/Gauntlet.h"
#include "core/Level.h"
#include "core/LevelData.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;

namespace {

const char* zoneName(int level) { const Zone z = zoneOf(level); return z == Zone::Egypt ? "Neon Egypt" : z == Zone::Japan ? "Neon Japan" : "the neon city"; }

std::vector<std::string> readLines(std::istream& in) {
    std::vector<std::string> out;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// ---- the bake: the course the generator lays for level n
LevelData bake(int n, const Gauntlet* g) {
    const double length = 300.0 + 10.0 * (n - 1);
    Level l;
    l.reset(Campaign::seedOf(n));
    l.setDifficulty(35.0 * (n - 1), 0.35);
    l.setFinish(length);
    l.setSaws(Campaign::sawsOf(n), Campaign::sawRowOf(n));
    l.setGauntlet(g, 0.4 * length);
    l.generateUntil(length + 20);
    LevelData d;
    d.number = n;
    d.length = length;
    d.diffOffset = 35.0 * (n - 1);
    d.diffScale = 0.35;
    d.notes = {"Level " + std::to_string(n) + ", " + zoneName(n) + (g ? ": the " + g->name() + " at 40% of the way" : std::string()) + "."};
    double reach = 0;
    for (const Segment& s : l.segs) {
        if (s.x0 >= length - 1e-6) break;
        LevelData::Wall w;
        w.cells = int(std::lround((s.x1 - s.x0) / U));
        if (s.gauntlet) {
            for (const PlacedGauntlet& p : l.gauntlets) if (std::abs(p.x0 - s.x0) < 1e-6) w.gauntlet = p.g->name();
        } else {
            w.h = s.h; w.kind = s.kind; w.ceil = s.ceil;
        }
        d.course.push_back(w);
        reach = s.x1;
    }
    for (const Saw& w : l.saws) if (!l.gauntletAt(w.x) && w.x < reach) d.saws.push_back(w.x);
    for (const Corn& c : l.corns) if (c.x < reach) d.discos.push_back({c.x, c.y});
    return d;
}

// ---- the checks
std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); return b; }
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

struct Note { double x = 0, y = 0; std::string text; };

// What the generator promises a careful player (Level.h, Level_test), looked for in a course as authored
struct Report {
    std::vector<Note> notes;
    std::vector<Note> short_;          // the egg budget: where it runs short
    std::vector<Note> doors;           // a gauntlet's door off its forecourt's height, its porch off its exit's (the tests fail)
    std::vector<std::array<double, 2>> walk; // the careful player's meter along the course: (x, eggs) at each climb and blade
    double lowest = 8;                 // the least eggs a careful player holds at a climb or a blade
    int walls = 0, barriers = 0, tallest = 0;
};
Report judge(const Level& l) {
    Report r;
    const double epuSlack = 1.1;
    auto epu = [&](double x) { const Curve c = l.curveAt(x); return U / (c.speed * c.regen * epuSlack); };
    // walls, barriers, drops, the walls under barriers
    const Segment* prev = nullptr;
    for (const Segment& s : l.segs) {
        if (s.x0 >= l.finish()) break;
        if (s.gauntlet) { prev = &s; continue; }
        r.walls += s.h > 0;
        r.barriers += s.ceil > 0;
        r.tallest = std::max(r.tallest, s.h);
        const Curve c = l.curveAt(s.x0);
        if (prev && !prev->gauntlet) {
            if (prev->ceil && s.h > prev->ceil - 2)
                r.notes.push_back({s.x0, double(s.h), "A wall " + std::to_string(s.h) + " high right after a barrier at " + std::to_string(prev->ceil) +
                                                        ": no one can stack for it under the barrier (" + std::to_string(prev->ceil - 2) + " at most)"});
            if (s.h < prev->h && s.x1 < l.finish()) { // (one that runs on past the finish line: nothing to react to)
                const double fall = std::sqrt(2.0 * (prev->h - s.h) * U / 36.0), need = c.speed * (fall + 0.45);
                if (s.x1 - s.x0 + 1e-6 < need)
                    r.notes.push_back({s.x0, double(s.h), "A drop of " + std::to_string(prev->h - s.h) + " onto " + num((s.x1 - s.x0) / U) +
                                                            " cells: no time to land and react (" + std::to_string(int(std::ceil(need / U))) + " wanted)"});
            }
        }
        prev = &s;
    }
    // saws: on the flat, clear of barriers, a reaction after a drop or a barrier, a run after
    double lastSaw = -1e9;
    for (const Saw& w : l.saws) {
        if (w.x >= l.finish() || l.gauntletAt(w.x)) continue;
        const Segment* s = nullptr;
        for (const Segment& sg : l.segs) if (w.x >= sg.x0 && w.x < sg.x1) { s = &sg; break; }
        if (!s) continue;
        const Curve c = l.curveAt(s->x0);
        const double react = c.speed * 0.6;
        auto note = [&](const std::string& t) { r.notes.push_back({w.x, double(w.y), t}); };
        if (l.heightAt(w.x - Level::kSawReach - 0.3) != s->h || l.heightAt(w.x + Level::kSawReach + 0.3) != s->h)
            note("A saw on the edge of a wall: the stack has to meet it on the flat");
        if (s->ceil) note("A saw under a barrier: no room to stack for it");
        const Segment* before = s > l.segs.data() ? s - 1 : nullptr;
        if (before && !before->gauntlet && before->ceil && w.x - Level::kSawReach - s->x0 < react - 1e-6) note("A saw too soon after a barrier: no time to stack again");
        if (before && !before->gauntlet && before->h > s->h) {
            const double fall = std::sqrt(2.0 * (before->h - s->h) * U / 36.0);
            if (w.x - Level::kSawReach - s->x0 < std::max(react, c.speed * fall + 0.3) - 1e-6) note("A saw too soon after a drop: she's still landing");
        }
        if (s->x1 - w.x < std::max(2.5 * U, c.speed * 0.45 + Level::kSawR) - 1e-6) note("A saw too close to the next wall: no time to stack for it");
        const bool inRow = std::abs(w.x - lastSaw - Level::kSawRowGap) < 0.05;
        if (!inRow && w.x - lastSaw < c.speed * c.regen * 1.1 - 1e-6) note("A saw too soon after the last one: the meter can't have refilled");
        lastSaw = w.x;
    }
    // the egg budget: a careful player's meter (full at the start, refilling per metre at the curve's speed and
    // refill, a little slack for the adaptive difficulty), paying for each climb and each blade, with eggs to spare
    double budget = Level::kBudgetCap, at = 0;
    int lastH = 0;
    size_t sawI = 0;
    std::vector<const Saw*> runSaws;
    for (const Saw& w : l.saws) if (!l.gauntletAt(w.x) && w.x < l.finish()) runSaws.push_back(&w);
    auto credit = [&](double to) { if (to > at) { budget = std::min(Level::kBudgetCap, budget + (to - at) / U * epu(at)); at = to; } r.walk.push_back({at, budget}); };
    for (const Segment& s : l.segs) {
        if (s.x0 >= l.finish()) break;
        if (s.gauntlet) {
            const PlacedGauntlet* p = l.gauntletAt(s.x0 + 0.01);
            credit(s.x0);
            if (budget < 2 - 1e-6) r.notes.push_back({s.x0, double(lastH), "At the " + (p ? p->g->name() : std::string("gauntlet")) + "'s door a careful player holds " + num(budget) +
                                                                        " eggs: its checks count on 2 (a longer forecourt)"});
            budget = std::min(Level::kBudgetCap, 1.0 + 8 * epu(s.x1)); // (after the maze: a careful guess, low)
            at = s.x1;
            r.walk.push_back({at, budget});
            lastH = p ? p->g->exitRow() : 0;
            continue;
        }
        credit(s.x0);
        if (s.h > lastH) {
            const double need = (s.h - lastH) + Level::kBudgetMargin;
            r.lowest = std::min(r.lowest, budget - (s.h - lastH));
            if (budget + 1e-6 < need) r.short_.push_back({s.x0, double(s.h), "A climb of " + std::to_string(s.h - lastH) + " needs " + num(need) +
                                                                                  " eggs; a careful player holds " + num(budget)});
            budget = std::max(0.0, budget - (s.h - lastH));
            r.walk.push_back({at, budget});
        }
        for (; sawI < runSaws.size() && runSaws[sawI]->x < s.x1; ++sawI) {
            credit(runSaws[sawI]->x);
            r.lowest = std::min(r.lowest, budget - 1);
            if (budget + 1e-6 < 1 + Level::kBudgetMargin) r.short_.push_back({runSaws[sawI]->x, double(s.h), "A blade needs an egg; a careful player holds " + num(budget)});
            budget = std::max(0.0, budget - 1);
            r.walk.push_back({at, budget});
        }
        lastH = s.h;
    }
    credit(l.finish());
    // a gauntlet's door: its forecourt at its entry's height (under a lintel as low as the door), its porch at its exit's
    for (size_t i = 0; i < l.segs.size(); ++i) {
        if (!l.segs[i].gauntlet) continue;
        const PlacedGauntlet* p = l.gauntletAt(l.segs[i].x0 + 0.01);
        if (!p) continue;
        const Segment* before = i ? &l.segs[i - 1] : nullptr;
        const Segment* after = i + 1 < l.segs.size() ? &l.segs[i + 1] : nullptr;
        if (!before || before->h != p->g->entryRow())
            r.doors.push_back({p->x0, double(p->g->entryRow()), "The " + p->g->name() + "'s door is at row " + std::to_string(p->g->entryRow()) +
                                                                     ": the course has to come to it at that height"});
        else if (before->ceil && before->ceil != p->g->entryTop())
            r.doors.push_back({p->x0, double(p->g->entryRow()), "The barrier before the " + p->g->name() + "'s door should be its lintel (at " +
                                                                     std::to_string(p->g->entryTop()) + ")"});
        else if (!before->ceil && p->g->entryTop() - p->g->entryRow() - 2 < 8) // (Level::placeGauntlet's low door)
            r.notes.push_back({p->x0, double(p->g->entryRow()), "No lintel before the " + p->g->name() + "'s door: a stack of more than " +
                                                                     std::to_string(std::max(0, p->g->entryTop() - p->g->entryRow() - 2)) +
                                                                     " eggs crashes into its wall over the door"});
        if (after && after->x0 < l.finish() && after->h != p->g->exitRow())
            r.doors.push_back({p->x1(), double(p->g->exitRow()), "The " + p->g->name() + " comes out at row " + std::to_string(p->g->exitRow()) +
                                                                      ": its porch has to be at that height"});
    }
    return r;
}

// the level played in the real game, the bot playing from its start (GameDev_test's refillKeepsUp)
struct GameRun {
    bool cleared = false;
    double seconds = 0, x = 0, y = 0;
    int perfects = 0, closeCalls = 0;
    std::vector<std::array<float, 3>> trace; // (x, the floor her stack's on, eggs) every few frames
    std::vector<std::array<float, 3>> events; // (x, height, kind: 0 a PERFECT, 1 a close call or shave)
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
    g.press();
    int frames = 0, perfects = 0, calls = 0;
    for (; frames < 60 * 180 && g.state() == Game::State::Play; ++frames) {
        g.update(1.0 / 60.0);
        const float x = g.hen().nodes[size_t(g.hen().root)].pos.x, y = g.henBase();
        if (frames % 5 == 0) r.trace.push_back({x, y, float(g.eggCount())});
        if (g.perfects() > perfects) r.events.push_back({x, y + g.eggCount() * float(U), 0});
        if (g.closeCalls() > calls) r.events.push_back({x, y + g.eggCount() * float(U), 1});
        perfects = g.perfects(); calls = g.closeCalls();
    }
    r.cleared = g.lastRunCleared();
    r.seconds = frames / 60.0;
    r.perfects = perfects; r.closeCalls = calls;
    r.x = g.hen().nodes[size_t(g.hen().root)].pos.x;
    r.y = g.henBase();
    return r;
}

void reportJson(const LevelData& d, const std::string& text, bool game) {
    (void)text;
    Level l;
    l.load(d);
    l.generateUntil(d.length + 20);
    const Report rep = judge(l);
    const Curve c0 = l.curveAt(0), c1 = l.curveAt(d.length);
    int sawsOnRun = 0;
    for (const Saw& w : l.saws) sawsOnRun += !l.gauntletAt(w.x) && w.x < d.length;
    std::string o = "{\"ok\":true,\"level\":" + std::to_string(d.number) + ",\"zone\":\"" + zoneName(d.number) + "\"";
    o += ",\"length\":" + num(d.length) + ",\"courseLength\":" + num(d.courseLength()) + ",\"raids\":" + std::to_string(d.raids);
    o += ",\"speed\":[" + num(c0.speed) + "," + num(c1.speed) + "],\"regen\":[" + num(c0.regen) + "," + num(c1.regen) + "]";
    o += ",\"walls\":" + std::to_string(rep.walls) + ",\"barriers\":" + std::to_string(rep.barriers) + ",\"tallest\":" + std::to_string(rep.tallest) +
         ",\"saws\":" + std::to_string(sawsOnRun) + ",\"discos\":" + std::to_string(l.corns.size()) + ",\"lowest\":" + num(rep.lowest);
    o += ",\"gauntlets\":[";
    for (size_t i = 0; i < l.gauntlets.size(); ++i) {
        const PlacedGauntlet& p = l.gauntlets[i];
        o += std::string(i ? "," : "") + "{\"name\":\"" + esc(p.g->name()) + "\",\"x0\":" + num(p.x0) + ",\"cols\":" + std::to_string(p.g->cols()) +
             ",\"rows\":" + std::to_string(p.g->rows()) + ",\"entryRow\":" + std::to_string(p.g->entryRow()) + ",\"entryTop\":" + std::to_string(p.g->entryTop()) +
             ",\"exitRow\":" + std::to_string(p.g->exitRow()) + "}";
    }
    o += "]";
    auto notes = [&](const std::vector<Note>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) s += std::string(i ? "," : "") + "{\"x\":" + num(v[i].x) + ",\"y\":" + num(v[i].y) + ",\"text\":\"" + esc(v[i].text) + "\"}";
        return s + "]";
    };
    o += ",\"notes\":" + notes(rep.notes) + ",\"short\":" + notes(rep.short_) + ",\"doors\":" + notes(rep.doors) + ",\"walk\":[";
    for (size_t i = 0; i < rep.walk.size(); ++i) o += std::string(i ? "," : "") + "[" + num(rep.walk[i][0]) + "," + num(rep.walk[i][1]) + "]";
    o += "]";
    if (game) {
        const GameRun r = playInGame(d.number);
        o += ",\"game\":{\"cleared\":" + std::string(r.cleared ? "true" : "false") + ",\"seconds\":" + num(r.seconds) + ",\"x\":" + num(r.x) + ",\"y\":" + num(r.y) +
             ",\"perfects\":" + std::to_string(r.perfects) + ",\"closeCalls\":" + std::to_string(r.closeCalls) + ",\"trace\":[";
        for (size_t i = 0; i < r.trace.size(); ++i) o += std::string(i ? "," : "") + "[" + num(r.trace[i][0]) + "," + num(r.trace[i][1]) + "," + num(r.trace[i][2]) + "]";
        o += "],\"events\":[";
        for (size_t i = 0; i < r.events.size(); ++i) o += std::string(i ? "," : "") + "[" + num(r.events[i][0]) + "," + num(r.events[i][1]) + "," + num(r.events[i][2]) + "]";
        o += "]}";
    }
    o += "}";
    std::printf("%s\n", o.c_str());
}

void reportText(const LevelData& d) {
    Level l;
    l.load(d);
    l.generateUntil(d.length + 20);
    const Report rep = judge(l);
    std::printf("level %d (%s): %.0f m, %d walls (the tallest %d), %d barriers, %zu saws and blades, %zu disco balls, %zu gauntlet(s)\n", d.number,
                zoneName(d.number), d.length, rep.walls, rep.tallest, rep.barriers, l.saws.size(), l.corns.size(), l.gauntlets.size());
    std::printf("  the egg budget: %s (the least a careful player holds: %.1f)\n", rep.short_.empty() ? "every climb and blade affordable" : "SHORT", rep.lowest);
    for (const Note& n : rep.short_) std::printf("    %6.1f m: %s\n", n.x, n.text.c_str());
    for (const Note& n : rep.doors) std::printf("  DOOR, %6.1f m: %s\n", n.x, n.text.c_str());
    for (const Note& n : rep.notes) std::printf("  note, %6.1f m: %s\n", n.x, n.text.c_str());
}

} // namespace

int main(int argc, char** argv) {
    std::string file, gauntletName;
    std::vector<std::string> gauntletFiles;
    int level = 0, bakeN = 0;
    bool json = false, game = false, hasGauntlet = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bake") && i + 1 < argc) bakeN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--gauntlet") && i + 1 < argc) { gauntletName = argv[++i]; hasGauntlet = true; }
        else if (!std::strcmp(argv[i], "--gauntlet-file") && i + 1 < argc) gauntletFiles.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!std::strcmp(argv[i], "--level") && i + 1 < argc) level = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--json")) json = true;
        else if (!std::strcmp(argv[i], "--game")) game = true;
    }
    // layouts standing in for gauntlets (the editor's unsaved grids): before anything lays a course with them
    for (const std::string& path : gauntletFiles) {
        std::ifstream in(path);
        std::vector<std::string> rows;
        for (const std::string& line : readLines(in)) if (!line.empty() && line[0] != ';') rows.push_back(line);
        std::string name = path.substr(path.find_last_of('/') + 1), why;
        name = name.substr(0, name.find('.'));
        if (!debugOverrideGauntlet(name, rows, &why)) {
            if (json) std::printf("{\"ok\":false,\"error\":\"%s\"}\n", esc(why).c_str());
            else std::printf("%s\n", why.c_str());
            return 1;
        }
    }
    if (bakeN > 0) {
        const Gauntlet* g = hasGauntlet ? (gauntletName == "none" ? nullptr : gauntletNamed(gauntletName)) : Campaign::gauntletOf(bakeN);
        if (hasGauntlet && gauntletName != "none" && !g) { std::fprintf(stderr, "no gauntlet called %s\n", gauntletName.c_str()); return 1; }
        std::printf("%s", levelText(bake(bakeN, g)).c_str());
        return 0;
    }
    if (file.empty() || level <= 0) {
        std::fprintf(stderr, "level_check --file PATH|- --level N [--gauntlet-file PATH ...] [--json] [--game] | --bake N [--gauntlet NAME|none]\n");
        return 2;
    }
    std::ifstream f;
    std::istream* in = &std::cin;
    if (file != "-") { f.open(file); in = &f; }
    std::string text;
    for (const std::string& line : readLines(*in)) text += line + "\n";
    std::string why;
    const std::optional<LevelData> d = parseLevel(level, text, &why);
    if (!d) {
        if (json) std::printf("{\"ok\":false,\"level\":%d,\"error\":\"%s\"}\n", level, esc(why).c_str());
        else std::printf("doesn't parse: %s\n", why.c_str());
        return 1;
    }
    if (game) debugOverrideLevel(level, text); // (the campaign lays this course)
    if (json) reportJson(*d, text, game);
    else reportText(*d);
    return 0;
}
