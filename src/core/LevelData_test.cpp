// The campaign's levels as authored (LevelData.h): a level file reads into its data and writes back as it was; what
// isn't a level is turned away, with why; a course laid from its data (Level::load) is its walls, barriers, gauntlets
// (their saws with them), saws and disco balls, then runway; every level the game ships reads, writes back as it was,
// and is the campaign's (its length, its gauntlet, where); and a level can stand in for the built-in one (the editor).
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/LevelData.h"

#include <cmath>
#include <set>

using namespace cs;
using test::check;

namespace {

const char* kSmall = R"(; A test course.
;
; Two lines of notes.
length 30
difficulty 70 0.35
raids 2
course
16 0 crate
5 2 hay 6
4 3 mix
30 0 crate
saws
14.1 22.5
discos
10.5 1.45
)";

void reading() {
    std::string why;
    const auto d = parseLevel(4, kSmall, &why);
    check(d.has_value(), "a level file reads");
    if (!d) { std::printf("  %s\n", why.c_str()); return; }
    check(d->number == 4 && d->length == 30 && d->diffOffset == 70 && d->diffScale == 0.35 && d->raids == 2,
          "...its finish line, its difficulty band, its raids");
    check(d->notes.size() == 3 && d->notes[0] == "A test course." && d->notes[1].empty(), "...the notes at its top: its description");
    check(d->course.size() == 4 && d->course[1].cells == 5 && d->course[1].h == 2 && d->course[1].kind == WallKind::Hay && d->course[1].ceil == 6 &&
              d->course[2].kind == WallKind::Mix && d->course[2].ceil == 0,
          "...its walls: how long, how high, their kind, a barrier over one");
    check(d->saws.size() == 2 && d->saws[1] == 22.5 && d->discos.size() == 1 && d->discos[0].y == 1.45, "...its saws, its disco balls");
    check(levelText(*d) == kSmall, "...and it writes back as it was");
    const auto defaults = parseLevel(9, "length 300\ncourse\n500 0 crate\n", &why);
    check(defaults && defaults->raids == -1 && defaults->diffOffset == 35 * 8 && defaults->saws.empty(),
          "left out: raids by the game's own rule, the difficulty band its level's");
}

void turnedAway() {
    auto why = [](const std::string& text) { std::string w; return parseLevel(1, text, &w) ? std::string("(read)") : w; };
    struct Case { const char* text; const char* says; };
    const Case cases[] = {
        {"course\n10 0 crate\n", "no length"},
        {"length 10\n", "no course"},
        {"length 10\ncourse\n10 0 brick\n", "kind"},
        {"length 10\ncourse\n10 3 crate 4\n", "underside"},
        {"length 10\ncourse\n10 0 gauntlet nope\n", "no gauntlet called nope"},
        {"length 10\ncourse\n10 0 gauntlet tomb\n", "cells wide"},
        {"length 100\ncourse\n10 0 crate\n", "short of its finish line"},
        {"length 5\ncourse\n10 0 crate\nsaws\nx\n", "a saw"},
        {"length 5\ncourse\n10 0 crate\nsaws\n9\n", "past the course"},
        {"length 5\nwhat 3\ncourse\n10 0 crate\n", "what's 'what'"},
        {"length 5\ncourse\n10 0 crate\n3\n", "a wall: how long"},
    };
    bool all = true;
    for (const Case& c : cases) {
        const std::string w = why(c.text);
        if (w.find(c.says) == std::string::npos) { all = false; std::printf("  \"%s\": %s\n", c.text, w.c_str()); }
    }
    check(all, "what isn't a level is turned away, with why (no length, no course, a kind, a barrier on her head, no such "
               "gauntlet, a span its grid isn't, a course short of its finish, a saw that isn't, one past the course)");
    const Gauntlet* tomb = gauntletNamed("tomb");
    if (!tomb) return;
    std::string w;
    const std::string inside = "length 300\ncourse\n10 0 crate\n" + std::to_string(tomb->cols()) + " 0 gauntlet tomb\n400 0 crate\nsaws\n" +
                               std::to_string(10 * U + 5) + "\n";
    check(!parseLevel(1, inside, &w) && w.find("inside the tomb") != std::string::npos, "...a saw inside a gauntlet too (its blades are its grid's)");
}

void laying() {
    const Gauntlet* tomb = gauntletNamed("tomb");
    if (!tomb) { check(false, "the tomb, to lay"); return; }
    const std::string text = "length 200\ncourse\n10 0 crate\n4 2 hay\n4 3 crate 6\n" + std::to_string(tomb->cols()) + " 0 gauntlet tomb\n200 0 crate\nsaws\n6.3 9\ndiscos\n3 1.45\n";
    std::string why;
    const auto d = parseLevel(11, text, &why);
    if (!d) { check(false, ("a level with the tomb: " + why).c_str()); return; }
    Level l;
    l.load(*d);
    l.generateUntil(400);
    int tombSaws = 0;
    for (int c = 0; c < tomb->cols(); ++c)
        for (int r = 0; r < tomb->rows(); ++r) tombSaws += tomb->sawAt(c, r);
    check(l.segs.size() >= 5 && l.segs[1].h == 2 && l.segs[1].kind == WallKind::Hay && l.segs[2].ceil == 6 && std::abs(l.segs[3].x0 - 18 * U) < 1e-9 &&
              l.segs[3].gauntlet && l.finish() == 200,
          "a course laid from its data: its walls, a barrier, the gauntlet's span, its finish line");
    check(l.gauntlets.size() == 1 && l.gauntlets[0].g == tomb && std::abs(l.gauntlets[0].x0 - 18 * U) < 1e-9, "...the gauntlet where its span is");
    bool sorted = true, onHay = false, onCrate = false;
    for (size_t i = 1; i < l.saws.size(); ++i) sorted = sorted && l.saws[i - 1].x <= l.saws[i].x;
    for (const Saw& s : l.saws) {
        if (std::abs(s.x - 6.3) < 1e-9) onHay = s.y == 2 && s.cube;  // (a hay wall's top: a cube saw)
        if (std::abs(s.x - 9) < 1e-9) onCrate = s.y == 3 && !s.cube; // (a crate wall's: a floor saw in stone)
    }
    const bool sawsOk = onHay && onCrate;
    check(int(l.saws.size()) == 2 + tombSaws && sorted && sawsOk, "...its saws set into their walls (and the gauntlet's own), in course order");
    check(l.corns.size() == 1 && l.corns[0].x == 3, "...its disco balls");
    const Segment& last = l.segs.back();
    check(last.x1 >= 400 && last.h == 0 && !last.ceil, "...and past its last wall, flat runway only");
}

void shipped() {
    const std::vector<int> levels = levelNumbers();
    bool all = true, roundTrip = true, campaign = true;
    std::set<std::string> used;
    bool once = true;
    for (int n = 1; n <= 30; ++n) all = all && levelData(n) != nullptr;
    for (int n : levels) {
        const LevelData* d = levelData(n);
        if (!d) { all = false; continue; }
        roundTrip = roundTrip && parseLevel(n, levelText(*d)).has_value() && levelText(*parseLevel(n, levelText(*d))) == levelText(*d);
        campaign = campaign && Campaign::lengthOf(n) == d->length;
        for (const LevelData::Wall& w : d->course)
            if (!w.gauntlet.empty()) once = once && used.insert(w.gauntlet).second;
    }
    check(all && !levels.empty(), "levels 1-30 ship authored (assets/levels), every one reading");
    check(roundTrip, "...every one writing back as it reads");
    check(campaign, "...and the campaign's lengths are theirs");
    check(once, "...no gauntlet in two levels");
    bool where = true;
    for (int n : levels) {
        const LevelData* d = levelData(n);
        if (!d) continue;
        int cells = 0;
        double at = -1;
        std::string name;
        for (const LevelData::Wall& w : d->course) { if (!w.gauntlet.empty()) { at = cells * U; name = w.gauntlet; break; } cells += w.cells; }
        where = where && (name.empty() ? Campaign::gauntletOf(n) == nullptr
                                       : Campaign::gauntletOf(n) == gauntletNamed(name) && std::abs(Campaign::gauntletAt(n) - at) < 1e-9);
    }
    check(where, "a level's gauntlet, and where, are its data's");
}

void standIn() {
    std::string why;
    const std::string mine = "; mine\nlength 120\ncourse\n300 0 crate\n";
    check(!debugOverrideLevel(2, "length 10\n", &why) && !why.empty(), "a stand-in that isn't a level is turned away");
    check(debugOverrideLevel(2, mine, &why) && levelData(2) && levelData(2)->length == 120 && Campaign::lengthOf(2) == 120,
          "a level can stand in for the built-in one (the editor's play-test): the campaign plays it");
}

} // namespace

int main() {
    reading();
    turnedAway();
    laying();
    shipped();
    standIn(); // (last: it changes level 2 for the rest of the run)
    return test::finish("LevelData");
}
