// Course levels (Campaign.h): seeded per level, a finish line, Level Cleared, retry on a crash.
#include "TestUtil.h"
#include "core/HudStyle.h"
#include "core/Campaign.h"
#include "core/Chunk.h"
#include "core/LevelBake.h"
#include "core/Gauntlet.h"
#include "core/LevelData.h"

#include <set>

using namespace cs;
using test::check;

namespace {
struct Env {
    test::FakeStorage st = test::FakeStorage::ready();
    test::FakeClock clock;
    test::FakeAds ads;
    GameServices svc() {
        ads.state = RewardedState::Unavailable; // no Continue offers in these checks
        GameServices s;
        s.storage = &st; s.ads = &ads; s.clock = &clock;
        return s;
    }
};
std::vector<Segment> courseOf(int level, double until, const LevelCatalog* cat = nullptr) {
    Campaign c(nullptr, cat);
    c.debugSetLevel(level);
    Level course;
    c.applyTo(course);
    course.generateUntil(until);
    return course.segs;
}
bool same(const std::vector<Segment>& a, const std::vector<Segment>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x0 != b[i].x0 || a[i].x1 != b[i].x1 || a[i].h != b[i].h || a[i].ceil != b[i].ceil) return false;
    return true;
}
} // namespace

int main() {
    {   // ---- gauntlets (the game's own levels): which level plays which is the levels' data (assets/levels: the level
        // editor moves them), every one a gauntlet in the game, none in two levels, each before its level's finish line;
        // past the authored levels, none
        std::set<const Gauntlet*> seen;
        bool named = true, once = true, before = true;
        int with = 0;
        for (int l = 1; l <= 40; ++l) {
            const Gauntlet* g = Campaign::gauntletOf(l);
            std::string want;
            if (const LevelData* d = levelData(l))
                for (const LevelData::Wall& w : d->course) if (!w.gauntlet.empty()) { want = w.gauntlet; break; }
            named = named && (want.empty() ? g == nullptr : g != nullptr && g == gauntletNamed(want));
            if (!g) continue;
            ++with;
            once = once && seen.insert(g).second;
            before = before && Campaign::gauntletAt(l) > 0 && Campaign::gauntletAt(l) + g->cols() * U < Campaign::lengthOf(l);
        }
        check(with > 0 && named, "gauntlets: each level's is the one its data names (assets/levels), and none past the authored levels");
        check(once, "...none plays in two levels");
        check(before, "...each one before its level's finish line");
    }
    test::useFixtures(); // (from here: the levels these tests are written round, tests/fixtures)
    {   // ---- zones: the city, then Neon Egypt on levels 11-20, then the city again
        bool city = true, egypt = true, japan = true, after = true;
        for (int l = 1; l <= 10; ++l) city = city && zoneOf(l) == Zone::City;
        for (int l = 11; l <= 20; ++l) egypt = egypt && zoneOf(l) == Zone::Egypt;
        for (int l = 21; l <= 30; ++l) japan = japan && zoneOf(l) == Zone::Japan;
        for (int l = 31; l <= 50; ++l) after = after && zoneOf(l) == Zone::City;
        check(city && egypt && japan && after, "zones: levels 1-10 the city, 11-20 Neon Egypt, 21-30 Neon Japan, 31+ the city again");
        check(zoneTrack(Zone::City) == 0 && zoneTrack(Zone::Egypt) == 1 && zoneTrack(Zone::Japan) == 2,
              "...each with its own soundtrack");
        Campaign c(nullptr);
        c.debugSetLevel(11);
        check(c.zone() == Zone::Egypt, "...a campaign on level 11 is in Egypt");
    }
    {   // ---- the same course for everyone; every level different
        check(same(courseOf(3, 400), courseOf(3, 400)), "level 3 is the same course every time (and for every player)");
        check(!same(courseOf(1, 400), courseOf(2, 400)), "level 1 and level 2 are different courses");
        check(Campaign::lengthOf(1) == 300 && Campaign::lengthOf(10) == 390, "levels are 300 m, +10 m per level");
    }
    {   // ---- difficulty steps up per level
        Campaign c1(nullptr), c10(nullptr);
        c10.debugSetLevel(10);
        Level l1, l10;
        c1.applyTo(l1); c10.applyTo(l10);
        const Curve a = l1.curveAt(150), b = l10.curveAt(150);
        check(b.speed > a.speed + 2.0, "level 10 runs much faster than level 1");
        check(b.cap > a.cap && b.maxUp > a.maxUp, "level 10 has taller walls and bigger steps");
        check(l1.curveAt(0).speed <= curve(0).speed + 1e-9, "level 1 starts at the easiest point of the curve");
    }
    {   // ---- past the finish line: a flat runway
        Campaign c(nullptr);
        Level course;
        c.applyTo(course);
        course.generateUntil(c.length() + 80);
        bool flat = true;
        for (const Segment& s : course.segs)
            if (s.x0 >= c.length() && (s.h != 0 || s.ceil != 0)) flat = false;
        check(flat, "after the finish the course is flat, with no walls or barriers");
        check(course.corns.empty() || course.corns.back().x < c.length(), "no disco balls past the finish");
    }
    {   // ---- a crash retries the same level
        Env env;
        Game g(env.svc());
        check(g.courseLevel() == 1, "a new player starts on level 1");
        test::dieQuickly(g);
        check(g.state() == Game::State::Dead && !g.lastRunCleared(), "a crash is not a clear");
        check(g.courseLevel() == 1 && g.levelAttempts() == 1, "after a crash: still level 1, 1 attempt");
        test::step(g, Game::kCrashTail + 0.5f); // the crash plays out, then the results
        test::dieQuickly(g);
        check(g.courseLevel() == 1 && g.levelAttempts() == 2, "the retry is level 1 again (2 attempts)");
    }
    {   // ---- reaching the finish clears the level
        Env env;
        Game g(env.svc());
        const int coinsBefore = g.coins();
        g.resize(430, 900);
        g.debugNearFinish();
        g.press();                 // starts 30 m before the finish line
        g.debugStartSurge();       // invulnerable through the last walls
        bool shown = false, centred = true, aboveHen = true, fits = true;
        for (int i = 0; i < 60 * 12 && g.state() == Game::State::Play; ++i) {
            g.update(1.0 / 60.0);
            const glm::vec4 b = g.levelCompleteBanner();
            if (b.w <= 0) continue;
            shown = true;
            glm::vec2 hen;
            centred = centred && std::abs(b.x - 215.f) <= 13.f; // the glitch jolts it a few px
            // halfway between the progress bar and the top of the hen (within the glitch / pop wobble)
            if (g.henTopOnScreen(hen)) {
                const float mid = (g.progressBarBottom() + hen.y) / 2;
                aboveHen = aboveHen && b.y + b.w / 2 < hen.y && b.y - b.w / 2 > g.progressBarBottom() && std::abs(b.y - mid) < 4.f;
            }
            fits = fits && b.z <= 430 - 32 && b.y - b.w / 2 > 0;
        }
        check(shown, "the finish line: \"Level Complete\" shows over the fireworks");
        check(centred, "...centred on screen");
        check(aboveHen, "...centred between the progress bar and the top of the hen");
        check(fits, "...and fits on screen");
        check(g.levelCompleteBanner().w == 0, "...and it's gone on the results");
        check(g.state() == Game::State::Dead && g.lastRunCleared(), "crossing the finish line ends the run as a clear");
        check(g.courseLevel() == 2 && g.levelAttempts() == 0, "the next level is level 2, with no attempts yet");
        check(g.coins() >= coinsBefore + 60, "clearing level 1 pays the clear bonus (50 + 10 per level)");
        Game again(env.svc());
        check(again.courseLevel() == 2, "the level is saved: a relaunch resumes on level 2");
    }
    {   // ---- the banner's animation
        using namespace cs::hud;
        const float T = Game::kFinishCelebration;
        check(completeBanner(0.f, T).alpha == 0.f, "banner: not there the instant the line is crossed");
        float peak = 0;
        for (float t = 0.06f; t < 0.5f; t += 0.01f) peak = std::max(peak, completeBanner(t, T).scale);
        check(peak > 1.02f && std::abs(completeBanner(0.6f, T).scale - 1.f) < 1e-4f, "banner: pops in (overshoots, settles)");
        check(completeBanner(0.2f, T).dx != 0.f && completeBanner(1.f, T).dx == 0.f, "banner: glitches in, then holds still");
        check(completeBanner(2.f, T).alpha == 1.f && completeBanner(T - 0.45f, T).alpha == 1.f, "banner: fully shown through the celebration");
        check(completeBanner(T - 0.1f, T).alpha < 0.3f && completeBanner(T, T).alpha == 0.f, "banner: fades out before the results");
    }
    {   // ---- seeds, bests, reset
        check(Campaign::seedOf(1) == Campaign::seedOf(1) && Campaign::seedOf(1) != Campaign::seedOf(2), "each level has its own fixed seed (the same for every player)");
        check(Campaign::lengthOf(1) == 300 && Campaign::lengthOf(5) == 340, "levels grow 10 m longer each");
        test::FakeStorage st;
        Campaign c(&st);
        check(c.recordRun(120) && !c.recordRun(80) && c.best() == 120 && c.recordRun(150) && c.best() == 150, "recordRun: true only for a new best");
        c.attempt();
        c.clear();
        check(c.level() == 2 && c.best() == 0 && c.attempts() == 0 && c.cleared() == 1, "a clear: next level, fresh best and attempts");
        c.debugReset();
        Campaign again(&st);
        check(again.level() == 1 && again.cleared() == 0 && again.best() == 0, "developer reset: back to level 1 (saved)");
    }
    {   // ---- the designer's chunks (Chunk.h, assets/chunks): a generated level (31 and up, no file of its own) holds the ones whose ranges
        // cover it; none built in: the levels are as they always were; a level with a file never changes
        const char* text = "; Hurdle\nchunk course\nranges 31-40\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n16.8\n";
        const auto chunk = parseChunk("test-chunk", text);
        check(chunk.has_value(), "(the test chunk reads)");
        LevelCatalog plain, mine;
        check(&plain.chunks() == &LevelCatalog::builtin().chunks(), "a catalog over the built-in one has its chunks");
        if (chunk) mine.setChunks({*chunk});
        check(mine.chunks().size() == 1 && mine.chunks()[0].id == "test-chunk", "a catalog given its own chunks has those");
        const double until = 600;
        // (the built-in chunks, if the designer has made some, are in the plain catalog's levels: the claims below are against the generator with the same ones)
        GenOptions none;
        for (const Chunk& c : plain.chunks()) if (!c.maze && c.inRange(35)) none.chunks.push_back(c);
        Level baked35;
        baked35.load(bakeLevel(35, nullptr, std::nullopt, none), &plain);
        baked35.generateUntil(until);
        check(same(courseOf(35, until, &plain), baked35.segs), "level 35 with nothing but the built-in chunks: the generator's course for it");
        if (chunk && plain.chunks().empty()) {
            check(same(courseOf(35, until), courseOf(35, until, &plain)), "no chunks built in: level 35 is the same in the game's own catalog and a plain one over it (the course as it always was)");
        }
        GenOptions with;
        with.chunks.push_back(*chunk);
        std::vector<ChunkUse> used;
        const LevelData bakedWith = bakeLevel(35, nullptr, std::nullopt, with, &used);
        check(!used.empty(), "(the generator does place the test chunk at level 35)");
        Level expect;
        expect.load(bakedWith, &mine);
        expect.generateUntil(until);
        const std::vector<Segment> got = courseOf(35, until, &mine);
        check(same(got, expect.segs), "generated level 35 in a catalog with a 31-40 chunk: the course the generator lays holding it");
        check(!same(got, courseOf(35, until, &plain)), "...not the course without it");
        check(same(courseOf(41, until, &mine), courseOf(41, until, &plain)), "level 41 (past the chunk's ranges): the same as without it");
        check(same(courseOf(30, until, &mine), courseOf(30, until, &plain)), "level 30 (before them, and a saved level): the same as without it");
        // a saved level never changes: the chunk's ranges cover level 12 too
        LevelCatalog wide;
        std::string why;
        Chunk everywhere = *chunk;
        everywhere.ranges = {{1, 100}};
        wide.setChunks({everywhere});
        check(same(courseOf(12, until, &wide), courseOf(12, until, &plain)), "a saved level (12) never changes, whatever a chunk's ranges say");
        check(same(courseOf(35, until, &mine), courseOf(35, until, &mine)), "(and the same chunks lay the same course every time: the cache keeps each catalog's apart)");
    }
    return test::finish("Campaign");
}
