// The rhythm-beat level generator (LevelBeats.h). Rhythm groups: beats in order within their length, a rest between
// groups; a level's arc: the star taught first, the finale last, the star's verb turning level to level. Every level
// it lays is one the checker passes (no notes, the egg budget, its bounce zones solved) and the game's bot clears
// (levels 31 to 100); the same seed lays the same level; the hazard switches hold. The critics: the interest curve, the
// mix, the edit distance; the biases the papers warn of kept in check (staircases, the long pieces never chosen); and
// the expressive range's traits.
#include "TestUtil.h"
#include "core/BotRun.h"
#include "core/Chunk.h"
#include "core/Campaign.h"
#include "core/CourseRules.h"
#include "core/Hazards.h"
#include "core/LevelBeats.h"
#include "core/LevelJudge.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

using namespace cs;
using namespace cs::beats;
using test::check;

namespace {

// BEATS_QUICK=1: the slow sweeps over fewer levels (every fifth), for iterating
const bool kQuick = std::getenv("BEATS_QUICK") != nullptr;
int step() { return kQuick ? 5 : 1; }

Report judged(const LevelData& d) {
    Level l;
    l.load(d);
    l.generateUntil(d.length + 40);
    return judge(l);
}

void rhythms() {
    const Plan p = compose(31, Campaign::seedOf(31));
    bool ordered = true, inside = true;
    for (const Group& g : p.groups) {
        for (size_t i = 0; i < g.rhythm.beats.size(); ++i) {
            ordered = ordered && (i == 0 || g.rhythm.beats[i].t > g.rhythm.beats[i - 1].t);
            inside = inside && g.rhythm.beats[i].t < g.rhythm.length;
        }
    }
    check(p.groups.size() >= 4 && ordered && inside, "rhythm groups: their beats in order, within their length");
    bool rests = true;
    for (size_t i = 1; i < p.groups.size(); ++i) rests = rests && p.groups[i].x0 - p.groups[i - 1].x1 >= 9 * U;
    check(rests, "...a rest between each group and the next");
    check(p.groups.front().role == Role::Teach && p.groups.back().role == Role::Finale, "the arc: teach first, the finale last");
    std::set<Role> roles;
    for (const Group& g : p.groups) roles.insert(g.role);
    check(roles.count(Role::Test) && roles.count(Role::Twist), "...a test and a twist between");
    check(p.passed > 0 && p.tried >= p.passed, "candidates laid and judged");
}

void stars() {
    int same = 0;
    for (int n = 31; n < 45; ++n) same += compose(n, Campaign::seedOf(n)).star == compose(n + 1, Campaign::seedOf(n + 1)).star;
    check(same == 0, "the star's verb is never the level before's");
}

void fairAndCleared() {
    int clean = 0, cleared = 0, total = 0;
    for (int n = 31; n <= 100; n += step()) {
        const LevelData d = bakeLevel(n, nullptr);
        const Report r = judged(d);
        clean += r.notes.empty() && r.short_.empty() && r.doors.empty();
        LevelCatalog cat;
        cleared += cat.setLevel(n, levelText(d)) && playLevel(n, &cat).cleared;
        ++total;
    }
    check(clean == total, "levels 31 to 100: every one the checker passes");
    check(cleared == total, "...and the game's bot clears");
}

void deterministic() {
    check(levelText(bakeLevel(37, nullptr)) == levelText(bakeLevel(37, nullptr)), "the same seed: the same level");
    check(levelText(bakeLevel(37, nullptr, 11u)) != levelText(bakeLevel(37, nullptr, 12u)), "another seed: another level");
}

void switches() {
    GenOptions none;
    none.only = {"none"};
    const LevelData d = bakeLevel(40, nullptr, 3u, none);
    int hazards = 0;
    for (const Placed& p : d.items) hazards += p.type != nullptr;
    check(hazards == 0, "every hazard switched off: none laid (walls, barriers and bounce boxes only)");
}

void critics() {
    check(editDistance("kitten", "sitting") == 3 && editDistance("", "abc") == 3 && editDistance("cdx", "cdx") == 0, "the edit distance: Levenshtein's");
    std::vector<Group> rising(6), falling(6);
    for (int i = 0; i < 6; ++i) { rising[size_t(i)].intensity = i == 0 ? 0.55 : 0.4 + i * 0.12; falling[size_t(i)].intensity = 1.0 - i * 0.15; }
    check(curveFit(rising) > curveFit(falling), "the interest curve: a build to a climax beats a fade");
    std::vector<Group> mixed(1), climbs(1);
    mixed[0].pieces = "ccccddbbsslljjxxzz";
    climbs[0].pieces = "cccccccccccccccccc";
    check(mixChiSquare(mixed, 50) < mixChiSquare(climbs, 50), "the mix: every piece beats climbs alone");
}

void biases() {
    int springs = 0, boxes = 0, lasers = 0, longest = 0;
    for (int n = 31; n <= 60; n += step()) {
        const Plan p = compose(n, Campaign::seedOf(n));
        springs += p.data.countOf(kSpringHazard);
        lasers += p.data.countOf(kLaserHazard);
        int run = 0;
        for (const Group& g : p.groups)
            for (char c : g.pieces) {
                boxes += c == 'x';
                run = c == 'c' || c == 't' ? run + (c == 't' ? 3 : 1) : 0;
                longest = std::max(longest, run);
            }
    }
    if (!kQuick) check(springs >= 10 && boxes >= 10 && lasers >= 10, "the long pieces get chosen: springs, bounce boxes, lasers all over levels 31-60");
    check(longest <= 9, "no endless staircases (the linearity bias)");
}

// the density slider: 0 as it was; denser up the slider (moments closer together); the densest still fair (the
// checker passes it, the bot clears it), else laid a notch down
void density() {
    check(beatSpacing(1, 0) == 1.7 && beatSpacing(2, 10) < beatSpacing(2, 5) && beatSpacing(2, 5) < beatSpacing(2, 0), "beats closer as the density rises");
    Options zero;
    check(levelText(compose(45, Campaign::seedOf(45), zero).data) == levelText(bakeLevel(45, nullptr)), "density 0: the generator as it was");
    double was = 1e9, at5 = 1e9;
    bool denser = true, fair = true, hardLine = true;
    for (int d : {0, 5, 6, 7, 8, 9, 10}) {
        Options o;
        o.density = d;
        const Plan p = compose(45, Campaign::seedOf(45), o);
        const double pace = traits(p.data).pacing;
        denser = denser && (d > 5 ? pace < at5 : pace < was); // (6 to 10: each denser than 5; a notch down still is)
        if (d <= 5) was = pace;
        if (d == 5) at5 = pace;
        hardLine = hardLine && p.data.hard == (p.density >= 6 ? p.density : 0);
        LevelCatalog cat;
        const Report r = judged(p.data);
        fair = fair && r.notes.empty() && r.short_.empty() && cat.setLevel(45, levelText(p.data)) && playLevel(45, &cat).cleared;
    }
    check(denser, "...each notch up the slider: moments closer together (level 45: 0, 5, then 6 to 10 each denser than 5)");
    check(hardLine, "...and from 6 the hard line is the density it was laid at (a notch down says so)");
    check(fair, "...every one fair: the checker passes it, the bot clears it");
    GenOptions g;
    g.density = 10;
    check(levelText(bakeLevel(45, nullptr, std::nullopt, g)) != levelText(bakeLevel(45, nullptr)), "the editors' Roll takes the slider (GenOptions::density)");
}

// super hard (density 6 to 10): the level says so (its hard line), the checker's human margins shrink with it, half at
// 10, and the generator lays to them; still cleared by the bot
void superHard() {
    check(rules::marginScale(0) == 1 && rules::marginScale(5) == 1 && std::abs(rules::marginScale(6) - 0.9) < 1e-9 &&
              std::abs(rules::marginScale(8) - 0.7) < 1e-9 && rules::marginScale(10) == 0.5 && rules::marginScale(9) < rules::marginScale(8),
          "the margins: all of them up to 5, 0.9 at 6, half at 10");
    Options o;
    o.density = 10;
    const Plan p = compose(31, Campaign::seedOf(31), o);
    check(p.density >= 6 && p.data.hard == p.density && levelText(p.data).find("\nhard " + std::to_string(p.density) + "\n") != std::string::npos,
          "density 10: the level says it's super hard (its hard line: the density it was laid at, 10 or a notch down)");
    const std::string tight = "length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n10 6 crate\n8 2 crate\n4 5 crate\n60 0 crate\nsaws\ndiscos\n";
    std::string hardText = tight;
    hardText.insert(hardText.find("course"), "hard 10\n");
    const auto easy = parseLevel(1, tight), hard = parseLevel(1, hardText);
    check(easy && hard && hard->hard == 10 && levelText(*hard) == hardText, "the hard line reads and writes back");
    if (easy && hard) {
        auto noted = [](const Report& r) { for (const auto& n : r.notes) if (n.text.find("no time to land") != std::string::npos) return true; return false; };
        check(noted(judged(*easy)) && !noted(judged(*hard)), "a landing too short for a careful player: fine for a super hard one");
    }
    LevelCatalog cat;
    check(cat.setLevel(31, levelText(p.data)) && playLevel(31, &cat).cleared, "...and the bot clears a density-10 course");
}

// the set pieces (Compton and Mateas's complex and composite patterns): each laid somewhere in levels 31-60 at density
// 5, bigger and more of them up the slider; every course they're in passes the checker and the bot clears it
void setPieces() {
    std::map<char, int> seen;
    int blocks0 = 0, blocks10 = 0;
    for (int n = 31; n <= 60; n += step()) {
        Options o;
        o.density = 5;
        for (const Group& g : compose(n, Campaign::seedOf(n), o).groups) for (char c : g.pieces) ++seen[c];
    }
    for (int n = 31; n <= 40; ++n)
        for (int d : {0, 10}) {
            Options o;
            o.density = d;
            for (const auto& w : compose(n, Campaign::seedOf(n), o).data.course) (d ? blocks10 : blocks0) += w.top > 0;
        }
    std::string missing;
    for (char c : std::string("KBWPXSLZJ")) if (!seen[c]) missing += c;
    if (!kQuick) check(missing.empty(), ("every set piece laid (block stairs K, barrier stairs B, weaves W, pockets P, mixed clusters X, saw rows S, "
                            "lava pools L, laser stairs Z, springs onto blocks J); missing: " + missing).c_str());
    check(blocks10 > blocks0, "more floating blocks up the slider");
}

// storeys and floating floors (levels 31-60 at density 0): she climbs up and stays up, springs onto floating floors,
// goes up and down by a linear ramp or a random walk, and the level finishes on whatever floor she's on
void storeys() {
    int tall = 0, floats = 0, higher = 0, linear = 0, walk = 0, finishHigh = 0, total = 0, flatOnes = 0, drops = 0, asc = 0;
    double above = 0;
    for (int n = 31; n <= 60; n += step()) {
        const Plan p = compose(n, Campaign::seedOf(n));
        const Traits t = traits(p.data);
        ++total;
        above += t.above;
        flatOnes += t.above < 1.0 / 3;
        tall += t.tallest >= 8;
        (p.linearFloors ? linear : walk) += 1;
        for (const Group& g : p.groups) for (char c : g.pieces) { floats += c == 'F'; higher += c == 'G'; drops += c == 'D'; asc += c == 'A'; }
        const LevelData::Wall& last = p.data.course.back();
        if (std::max(last.h, last.top) > 8) ++finishHigh; // (up past the old 8-row cap: on a storey)
    }
    check(flatOnes == 0, "storeys: every level (31-60) runs over the street for at least a third of its length");
    check(tall * 2 > total, "...most reach 8 rows or more");
    check(floats > 0 && higher > 0, "...springs onto floating floors, some on to a higher one");
    check(asc > 0 && drops > 0, "...ascents and descents");
    check(linear > 0 && walk > 0, "...both floor paths: a linear ramp and a random walk");
    check(finishHigh > 0, "...some levels finish up on a storey, above the old 8-row cap");
    // springs switched off: ascents only, no floating floors
    GenOptions noSprings;
    noSprings.only = {"saw", "lava", "laser"};
    int floatsOff = 0, up = 0;
    for (int n = 31; n <= 40; ++n) {
        const Plan p = compose(n, Campaign::seedOf(n), [&] { Options o; o.hazards = noSprings; return o; }());
        for (const Group& g : p.groups) for (char c : g.pieces) { floatsOff += c == 'F' || c == 'G'; up += c == 'A'; }
    }
    check(floatsOff == 0 && up > 0, "springs off: ascents only, no floating floors");
}

// a level whose finish line is on a high floor: she finishes, the celebration runs on at that height, it clears
void highFinish() {
    const std::string text = "length 60\ndifficulty 350 0.35\nraids none\ncourse\n20 0 crate\n12 2 crate\n12 4 crate\n16 4 crate\n12 7 crate\n200 7 crate\ndiscos\n";
    const auto d = parseLevel(31, text);
    check(d.has_value(), "a level finishing on a storey 7 rows up parses");
    if (!d) return;
    check(judged(*d).notes.empty() && judged(*d).short_.empty(), "...and the checker passes it");
    LevelCatalog cat;
    const GameRun r = cat.setLevel(31, levelText(*d)) ? playLevel(31, &cat) : GameRun{};
    check(r.cleared, "...and the game's bot clears it, the finish line and the celebration up there");
}

void expressiveRange() {
    LevelData flat;
    flat.length = 60;
    flat.course = {{20, 0, WallKind::Crate}, {20, 2, WallKind::Crate}, {20, 4, WallKind::Crate}, {20, 6, WallKind::Crate}};
    check(traits(flat).linearity > 0.95, "linearity: a staircase is a line");
    const Traits t = traits(bakeLevel(45, nullptr));
    check(t.linearity < 0.9 && t.leniency < 0 && t.pacing > 0 && t.tallest > 0, "a generated level: not a line, its hazards, its pacing");
}


// ---- the designer's chunks (Chunk.h): the 'Q' piece

// a 3-wall chunk (run-in, a hay wall with a saw set into it, run-out), and a stepped one with a climb and a drop
Chunk hurdle(const char* ranges, int weight, const char* uses, const char* mirror, const char* id = "hurdle") {
    std::string why;
    const auto c = parseChunk(id, std::string("chunk course\nranges ") + ranges + "\nweight " + std::to_string(weight) + "\nuses " + uses + "\nmirror " + mirror +
                                      "\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n16.8\n", &why);
    if (!c) std::printf("  hurdle: %s\n", why.c_str());
    return c ? *c : Chunk{};
}
Chunk steps(const char* ranges, int weight, const char* uses, const char* mirror) {
    std::string why;
    const auto c = parseChunk("steps", std::string("chunk course\nranges ") + ranges + "\nweight " + std::to_string(weight) + "\nuses " + uses + "\nmirror " + mirror +
                                      "\n---\nlength 55\ncourse\n13 0 crate\n18 2 crate\n18 4 hay\n30 1 crate\n13 1 crate\nsaws\n37.8\n", &why);
    if (!c) std::printf("  steps: %s\n", why.c_str());
    return c ? *c : Chunk{};
}
Plan rolled(int n, uint32_t seed, std::vector<Chunk> chunks, int density = 0) {
    Options o;
    o.density = density;
    o.hazards.chunks = std::move(chunks);
    return compose(n, seed, o);
}
Plan plain(int n, uint32_t seed, int density = 0) { return rolled(n, seed, {}, density); }

bool fair(int n, const LevelData& d) {
    const Report r = judged(d);
    LevelCatalog cat;
    return r.notes.empty() && r.short_.empty() && r.doors.empty() && cat.setLevel(n, levelText(d)) && playLevel(n, &cat).cleared;
}

void chunksPlaced() {
    const int seeds = kQuick ? 3 : 6;
    int placed = 0, outside = 0, mixed = 0, fairOnes = 0, total = 0;
    bool sawInside = true, ordered = true;
    for (int k = 1; k <= seeds; ++k) {
        const Plan p = rolled(33, uint32_t(k), {hurdle("31-35", 10, "any", "yes")});
        placed += int(p.chunks.size());
        total += !p.chunks.empty();
        for (const ChunkUse& u : p.chunks) {
            int saws = 0;
            for (const Placed& it : p.data.items) saws += it.type == &kSawHazard && it.x() > u.x0 && it.x() < u.x1;
            sawInside = sawInside && saws >= 1;
            ordered = ordered && u.id == "hurdle" && u.x1 > u.x0;
        }
        mixed += p.groups.empty() ? 0 : std::count_if(p.groups.begin(), p.groups.end(), [](const Group& g) { return g.pieces.find('Q') != std::string::npos; }) > 0;
        fairOnes += fair(33, p.data);
        const Plan q = rolled(36, uint32_t(k), {hurdle("31-35", 10, "any", "yes")});
        outside += int(q.chunks.size()) + (levelText(q.data) != levelText(plain(36, uint32_t(k)).data));
    }
    check(placed > 0 && total >= seeds / 2, "a chunk with range 31-35 is placed in rolls of level 33 (it is, in at least half of the seeds)");
    check(ordered && sawInside, "...named, with where it lies, and its saw is inside that stretch");
    check(mixed == total, "...the piece in the groups' pieces is 'Q'");
    check(outside == 0, "...and never in level 36: no chunks, the course exactly as without them");
    check(fairOnes == seeds, "every course with a chunk passes the checker and is cleared by the bot");
}

void chunksUses() {
    const int seeds = kQuick ? 3 : 6;
    int most1 = 0, mostAny = 0, most2 = 0;
    for (int k = 1; k <= seeds; ++k) {
        most1 = std::max(most1, int(rolled(34, uint32_t(k), {hurdle("31-40", 10, "1", "no")}).chunks.size()));
        most2 = std::max(most2, int(rolled(34, uint32_t(k), {hurdle("31-40", 10, "2", "no")}).chunks.size()));
        mostAny = std::max(mostAny, int(rolled(34, uint32_t(k), {hurdle("31-40", 10, "any", "no")}).chunks.size()));
    }
    check(most1 == 1, "uses 1: the chunk is placed, and never twice in a course");
    check(most2 == 2, "uses 2: placed twice at most");
    check(mostAny >= 3, "uses any, at a high weight: it repeats");
    // two chunks: each its own count
    int both = 0;
    for (int k = 1; k <= seeds; ++k) {
        const Plan p = rolled(35, uint32_t(k), {hurdle("31-40", 10, "1", "no"), steps("31-40", 10, "1", "no")});
        int h = 0, s = 0;
        for (const ChunkUse& u : p.chunks) { h += u.id == "hurdle"; s += u.id == "steps"; }
        check(h <= 1 && s <= 1, "two chunks of uses 1: each at most once");
        both += h && s;
    }
    check(both > 0, "...and both appear in some course");
}

void chunksMirrored() {
    int flipped = 0, straight = 0, noMirror = 0;
    for (int k = 1; k <= (kQuick ? 4 : 10); ++k) {
        for (const ChunkUse& u : rolled(37, uint32_t(k), {steps("31-40", 10, "any", "yes")}).chunks) (u.mirrored ? flipped : straight) += 1;
        for (const ChunkUse& u : rolled(37, uint32_t(k), {steps("31-40", 10, "any", "no")}).chunks) noMirror += u.mirrored;
    }
    check(flipped > 0 && straight > 0, "mirror yes: the chunk is placed flipped some of the time and straight the rest");
    check(noMirror == 0, "mirror no: never flipped");
    // a flipped one is laid with its heights reversed: the climbs are drops
    bool seenFlipped = false, reversed = false;
    for (uint32_t k = 1; k <= 12 && !reversed; ++k) {
        const Plan p = rolled(37, k, {steps("31-40", 10, "any", "yes")});
        for (const ChunkUse& u : p.chunks) {
            if (!u.mirrored) continue;
            seenFlipped = true;
            // (the stepped chunk's flip: a rise of 1 and a drop of 3 and 2: the stretch has a wall four over its entry's neighbour)
            double x = 0;
            int prev = 0, drops = 0, rises = 0;
            for (const auto& w : p.data.course) {
                if (x >= u.x0 - 1e-6 && x < u.x1 - 1e-6) { drops += w.h < prev; rises += w.h > prev; }
                prev = w.h; x += w.cells * U;
            }
            reversed = reversed || (drops >= 2 && rises >= 1);
        }
    }
    check(seenFlipped && reversed, "a flipped chunk lies reversed: its stairs run down, with a rise at the start");
}

void chunksIdentical() {
    Chunk maze = steps("31-40", 10, "any", "yes");
    maze.maze = true; // (a maze chunk: never a course's)
    const std::vector<Chunk> far = {hurdle("31-35", 10, "any", "yes"), steps("50-60", 10, "any", "yes"), maze};
    int same = 0, total = 0;
    for (int n : {45, 40, 38}) {
        for (uint32_t k : {1u, 2u}) {
            ++total;
            const Plan with = rolled(n, k, far), without = plain(n, k);
            same += levelText(with.data) == levelText(without.data) && with.chunks.empty();
        }
    }
    check(same == total, "chunks none of whose ranges cover the level (or maze ones): byte-identical to a course without them");
    check(levelText(rolled(45, Campaign::seedOf(45), {}).data) == levelText(bakeLevel(45, nullptr)), "no chunks at all: the campaign's own level");
    check(levelText(rolled(45, Campaign::seedOf(45), far).data) == levelText(bakeLevel(45, nullptr)), "...and with far-off ones too");
    // the baker hands back where they went
    GenOptions g;
    g.chunks = {hurdle("31-40", 10, "any", "yes")};
    std::vector<ChunkUse> used;
    const LevelData d = bakeLevel(33, nullptr, 3u, g, &used);
    check(!used.empty() && used.size() == rolled(33, 3u, g.chunks).chunks.size() && levelText(d) == levelText(rolled(33, 3u, g.chunks).data), "bakeLevel: the chunks it placed, with the course");
    std::vector<ChunkUse> none{{"x", 0, 1, false}};
    bakeLevel(33, nullptr, 3u, {}, &none);
    check(none.empty(), "...none, none");
}

void chunksDense() {
    int fairOnes = 0, total = 0;
    for (int d : {5, 8}) {
        for (uint32_t k = 1; k <= (kQuick ? 1u : 3u); ++k) {
            const Plan p = rolled(40 + int(k), k, {hurdle("31-60", 6, "any", "yes"), steps("31-60", 6, "2", "yes")}, d);
            ++total;
            fairOnes += fair(40 + int(k), p.data);
        }
    }
    check(fairOnes == total, "chunks at densities 5 and 8: every course passes the checker and the bot clears it");
}

// BEATS_STATS=1: how often a 3-wall chunk is placed over levels 31-40, 10 seeds each, at weights 1, 5 and 10
void chunkStats() {
    for (int density : {0, 5}) {
        double base = 0;
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int n = 31; n <= 40; ++n) for (uint32_t k = 1; k <= 10; ++k) plain(n, k, density);
            base = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        std::printf("density %d: without chunks, 100 courses in %.1f s (%.2f s each)\n", density, base, base / 100);
        for (int weight : {1, 5, 10}) {
            int placements = 0, withOne = 0, flipped = 0, fallback = 0, notched = 0, total = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int n = 31; n <= 40; ++n)
                for (uint32_t k = 1; k <= 10; ++k) {
                    const Plan p = rolled(n, k, {hurdle("31-40", weight, "any", "yes")}, density);
                    ++total;
                    placements += int(p.chunks.size());
                    withOne += !p.chunks.empty();
                    for (const ChunkUse& u : p.chunks) flipped += u.mirrored;
                    fallback += p.passed == 0;
                    notched += p.density != density;
                }
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("density %d weight %2d: %.2f placements a level, %d%% of levels with one or more, %d%% flipped, %d fallbacks, %d notched down, %.1f s (%.2f s each, %.2fx)\n",
                        density, weight, double(placements) / total, 100 * withOne / total, placements ? 100 * flipped / placements : 0, fallback, notched, secs, secs / total,
                        secs / base);
        }
    }
}

} // namespace

// a chunk no course passes with at density 5 (three saws in a hay wall's way, too close for the checker's margins)
Chunk poison(const char* id = "poison") {
    std::string why;
    const auto c = parseChunk(id, "chunk course\nranges 31-40\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n15.6\n16.8\n18\n", &why);
    if (!c) std::printf("  poison: %s\n", why.c_str());
    return c ? *c : Chunk{};
}

// a chunk is never stamped while she is on a floating floor: the wall before it is not a slab (open air under it, `top` set)
void chunksNotFloating() {
    int placed = 0, afterSlab = 0, seen = 0;
    for (int n : {36, 38, 40})
        for (uint32_t k = 1; k <= (kQuick ? 2u : 5u); ++k) {
            const Plan p = rolled(n, k, {hurdle("31-45", 10, "any", "no"), steps("31-45", 10, "any", "no")}, 6);
            bool slabs = false;
            for (const LevelData::Wall& w : p.data.course) slabs = slabs || (w.top && w.h == 0);
            seen += slabs;
            for (const ChunkUse& u : p.chunks) {
                ++placed;
                double x = 0;
                const LevelData::Wall* before = nullptr;
                for (const LevelData::Wall& w : p.data.course) {
                    if (x >= u.x0 - 1e-6) break;
                    before = &w;
                    x += w.cells * U;
                }
                afterSlab += before && before->top && before->h == 0;
            }
        }
    std::printf("      %d chunks placed in courses with floating floors (%d of 15 had some)\n", placed, seen);
    check(placed > 0 && afterSlab == 0, "a chunk is never laid while she is up on a floating floor");
}

// a chunk that spoils every candidate is left out, the density kept (it was a notch, or five, less dense; or the old generator's course)
void chunksDropped() {
    int dropped = 0, kept = 0, fairOnes = 0, same = 0, total = 0;
    for (uint32_t seed : {2u, 3u}) {
        if (kQuick && seed == 3) break;
        const Plan p = rolled(35, seed, {poison()}, 5);
        ++total;
        dropped += p.chunksDropped == std::vector<std::string>{"poison"} && p.chunks.empty();
        kept += p.density == 5 && p.passed > 0;
        fairOnes += fair(35, p.data);
        same += levelText(p.data) == levelText(plain(35, seed, 5).data);
    }
    check(dropped == total, "a chunk that spoils every candidate: left out, named in Plan::chunksDropped, none placed");
    check(kept == total, "...at the density asked (not a notch less), a candidate passed");
    check(fairOnes == total, "...and the course passes the checker and is cleared by the bot");
    check(same == total, "...exactly the course the same seed lays without the chunk");

    // with a good chunk beside it, only the one that spoils it goes; the good one is still placed
    int onlyPoison = 0, goodPlaced = 0, tried = 0;
    for (uint32_t seed : {2u, 3u}) {
        if (kQuick && seed == 3) break;
        const Plan p = rolled(32, seed, {poison(), hurdle("31-40", 10, "any", "no")}, 5);
        ++tried;
        onlyPoison += p.chunksDropped == std::vector<std::string>{"poison"};
        for (const ChunkUse& u : p.chunks) goodPlaced += u.id == "hurdle";
        check(fair(32, p.data), "a course with a good chunk and a spoiler passes and is cleared");
    }
    check(onlyPoison == tried && goodPlaced > 0, "...with a good chunk beside it, only the spoiler is left out and the good one is placed");

    // a chunk that is fine is never in the list; none in range, none
    check(rolled(33, 1, {hurdle("31-35", 10, "any", "yes")}).chunksDropped.empty(), "a chunk that places: nothing left out");
    check(rolled(41, 2, {poison()}, 5).chunksDropped.empty(), "a chunk out of range of the level: nothing left out");
    std::vector<ChunkUse> used{{"x", 0, 1, false}};
    std::vector<std::string> ids{"stale"};
    bakeLevel(36, nullptr, 3u, {}, &used, &ids);
    check(ids.empty(), "bakeLevel clears *dropped when nothing was left out");
    GenOptions o;
    o.chunks = {poison()};
    o.density = 5;
    bakeLevel(35, nullptr, 2u, o, &used, &ids);
    check(ids == std::vector<std::string>{"poison"} && used.empty(), "bakeLevel hands back the ids it left out");
}

int main() {
    if (std::getenv("BEATS_STATS")) { chunkStats(); return 0; }
    if (std::getenv("BEATS_CHUNKS")) { // (just the chunks' checks, for iterating)
        chunksPlaced();
        chunksUses();
        chunksMirrored();
        chunksIdentical();
        chunksDense();
        chunksDropped();
        chunksNotFloating();
        return test::finish("LevelBeats chunks");
    }
    rhythms();
    stars();
    deterministic();
    switches();
    critics();
    biases();
    expressiveRange();
    storeys();
    highFinish();
    density();
    superHard();
    setPieces();
    chunksPlaced();
    chunksUses();
    chunksMirrored();
    chunksIdentical();
    chunksDense();
    chunksDropped();
    chunksNotFloating();
    fairAndCleared();
    return test::finish("LevelBeats");
}
