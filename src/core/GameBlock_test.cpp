// Floating blocks (Level.h's Segment::top, LevelData.h): a slab in the air over the course, its underside a barrier
// she ducks under, its top a wall she climbs onto with a stack as high as it (the eggs below it knocked off) and rides
// to its end. And knives: blades under a roof that leaves room for so many eggs, so the last egg they take is forced.
// Played in the real game, headless at 60 Hz, on courses written for these checks (debugOverrideLevel).
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/LevelData.h"

#include <cstdio>
#include <string>

namespace {
using namespace cs;
using test::check;

struct Env {
    test::FakeStorage st = test::FakeStorage::ready();
    test::FakeClock clock;
    test::FakeAds ads;
    GameServices svc() {
        ads.state = RewardedState::Unavailable; // (no Continue offers)
        GameServices s;
        s.storage = &st; s.ads = &ads; s.clock = &clock;
        return s;
    }
};

// a run of level 2 laid as `course` (the bot playing it, or nobody): how high her feet got over [x0, x1), whether she
// ever went under its underside there, and how it ended
struct Run { double highest = 0, lowest = 99; bool cleared = false, died = false; int perfects = 0, closeCalls = 0, eggsAt = -1; };
Run play(const std::string& course, const std::string& saws, double x0, double x1, bool bot = true) {
    const std::string text = "length 30\ndifficulty 0 0.35\nraids none\ncourse\n" + course + "saws\n" + saws + "discos\n";
    std::string why;
    if (!debugOverrideLevel(2, text, &why)) { std::printf("      (not a level: %s)\n", why.c_str()); return {}; }
    Env env;
    Campaign(&env.st).debugSetLevel(2);
    Game g(env.svc());
    g.debugSeed(1);
    g.debugSetBot({bot, false, false, 0});
    g.press();
    Run r;
    for (int i = 0; i < 60 * 30 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        const double x = g.hen().nodes[size_t(g.hen().root)].pos.x;
        if (x >= x0 && x < x1) { r.highest = std::max(r.highest, double(g.henBase())); r.lowest = std::min(r.lowest, double(g.henBase())); }
        if (x >= x1 && r.eggsAt < 0) r.eggsAt = g.eggCount();
    }
    r.cleared = g.lastRunCleared();
    r.died = !r.cleared;
    r.perfects = g.perfects();
    r.closeCalls = g.closeCalls();
    return r;
}

std::string fmtSaw(double x) { char b[32]; std::snprintf(b, sizeof b, "%.2f", x); return b; }

void parsing() {
    std::string why;
    const auto d = parseLevel(2, "length 30\ncourse\n10 0 crate\n6 0 hay 3 5\n44 0 crate\nsaws\ndiscos\n", &why);
    check(d && d->course.size() == 3 && d->course[1].ceil == 3 && d->course[1].top == 5 && d->course[1].kind == WallKind::Hay,
          "a course line's fifth number is a floating block's top (its fourth, its underside)");
    check(d && levelText(*d).find("\n6 0 hay 3 5\n") != std::string::npos, "...and it's written back as it was");
    auto bad = [](const std::string& line) { std::string w; return !parseLevel(2, "length 30\ncourse\n10 0 crate\n" + line + "\n50 0 crate\n", &w) && !w.empty(); };
    check(bad("6 0 crate 3 3") && bad("6 0 crate 3 2"), "a block's top over its underside");
    check(bad("6 2 crate 2 5"), "...its underside over its wall");
    check(bad("6 0 crate 3 61") && bad("6 0 crate 3 5 7"), "...its top up to 60, and nothing more on the line");
    check(!bad("6 2 crate 3 5"), "...an underside just 1 over its wall: a block you can only climb onto");
    Level l;
    l.load(*d);
    l.rebuildBlocks();
    const double x = 13 * U; // (over the block)
    check(l.heightAt(x) == 0 && l.floorAt(x, 0) == 0 && l.floorAt(x, 5 * U) == 5, "the floor under a block, or its top once she's up on it");
    size_t blocks = l.blocks.size();
    check(blocks == size_t(6 * 2), "a block is drawn as its own courses: 6 columns, from its underside (3) to its top (5)");
}

void underAndOver() {
    // room under it (1 egg): the bot ducks, never up on it
    const Run under = play("20 0 crate\n12 0 crate 3 6\n40 0 crate\n", "", 20 * U + 0.3, 32 * U);
    check(under.cleared && under.highest < 0.05, "a block with room under it: the bot runs under it, and on");
    // bridging onto a wall as high as its top: the bot stacks 3, climbs onto it and runs on along it
    const Run bridge = play("20 0 crate\n10 0 crate 2 3\n40 3 crate\n", "", 20 * U + 0.4, 30 * U);
    check(bridge.cleared && bridge.highest > 3 * U - 0.05 && bridge.lowest > 3 * U - 0.05, "a block bridging onto a wall as high as its top: up on it all along, and on");
    check(bridge.perfects >= 1, "...climbed onto with no egg to spare: a PERFECT");
    // no room under it: only over, then off its far end
    const Run onlyOver = play("20 0 crate\n12 0 crate 1 3\n40 0 crate\n", "", 20 * U + 0.4, 32 * U);
    check(onlyOver.cleared && onlyOver.highest > 3 * U - 0.05, "a block too low to duck under: over it, and down off its far end");
    // ...whatever frame she meets it on: stacked over its top (here 2 spare for a blade on it), its underside never
    // gets her in the frame before she steps up (the barrier's check reaches 3 cm further ahead than the climb's)
    bool always = true;
    for (int lead = 20; lead < 30; ++lead) {
        const Run r = play(std::to_string(lead) + " 0 crate\n20 0 hay 1 5\n40 0 crate\n", fmtSaw((lead + 6) * U + 0.3) + "\n" + fmtSaw((lead + 9) * U + 0.3) + "\n", lead * U + 0.4, (lead + 20) * U);
        always = always && r.cleared && r.highest > 5 * U - 0.05;
    }
    check(always, "...climbed onto stacked higher than its top, from any approach: never caught by its underside first");
    // a stair of blocks in the sky, each one's underside just over the last one's top: room under it from the street,
    // none from the block she stands on, so up onto each in turn
    const Run stair = play("20 0 crate\n30 0 crate 1 4\n30 0 crate 5 8\n30 0 crate 9 12\n60 0 crate\n", "", 20 * U + 0.4, 110 * U);
    check(stair.cleared && stair.highest > 12 * U - 0.05, "a stair of floating blocks: up each one from the last, to the top, and down");
    // nobody stacking: she runs into its underside
    const Run nobody = play("20 0 crate\n12 0 crate 1 3\n40 0 crate\n", "", 0, 1, false);
    check(nobody.died, "...and with no eggs, into its underside: the run ends");
}

void sawOnTop() {
    // a blade 5 cells into a block's top (hay: a cube saw), the block bridging onto a wall: carry one over it
    const Run r = play("20 0 crate\n16 0 hay 2 3\n40 3 crate\n", "16.5\n", 20 * U, 40 * U);
    check(r.cleared && r.highest > 3 * U - 0.05, "a blade on a block's top: the bot crosses it up there and goes on");
    check(r.closeCalls >= 1, "...and it takes her last egg (a CLOSE SHAVE)");
}

// a knife: a blade under a roof of room 1 (the roof begun a reaction before it): she can carry one egg over it and no
// more, so it takes her last; a row of 3 under a roof of 3 takes all three
void knives() {
    const Run one = play("20 0 crate\n40 0 crate 3\n40 0 crate\n", fmtSaw(20 * U + 20 * U + 0.3) + "\n", 0, 100);
    check(one.cleared && one.closeCalls >= 1, "a knife (a blade under a roof of room 1): crossed with the one egg that fits, a CLOSE SHAVE");
    const Run three = play("20 0 crate\n44 0 crate 5\n40 0 crate\n", fmtSaw(40 * U + 0.3) + "\n" + fmtSaw(43 * U + 0.3) + "\n" + fmtSaw(46 * U + 0.3) + "\n", 0, 100);
    check(three.cleared && three.closeCalls >= 1, "...a row of three under a roof of room 3: the third takes her last egg");
}

} // namespace

int main() {
    parsing();
    underAndOver();
    sawOnTop();
    knives();
    return test::finish("GameBlock");
}
