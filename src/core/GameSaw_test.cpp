// Saw blades (GameSaw.cpp), in the real Game at a fixed 60 Hz step. With no egg under her the blade kills the hen;
// with eggs it saws the bottom one off (exactly one per saw): a bite (sparks, a hit-pause), then the egg falls apart in
// two halves that fly apart (one toward the camera, one away) and fade, with a yolk, while the stack settles one egg
// down. The last egg sawn off right under her is a CLOSE SHAVE; a row of blades takes one each. A cube saw on a wall top after a climb: a perfect climb
// onto it leaves no egg, and the blade gets her. OVERDRIVE smashes blades off their slots. Blades glow red when she'd
// reach one bare, whine as they come up, and are drawn spinning. A droid raid never zaps an egg a saw ahead needs. The
// bot stacks an egg over each and clears level 11.
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/Game.h"
#include "core/Gauntlet.h"
#include "core/Level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace cs;
using namespace test;

namespace {

struct RecordingAudio : IAudio {
    std::vector<std::pair<Sfx, int>> played;
    void play(Sfx s, int v) override { played.push_back({s, v}); }
    void musicStart(bool) override {}
    void musicStop(bool) override {}
    void musicTempo(float) override {}
    void musicSurge(bool) override {}
    void setMuted(bool) override {}
    int count(Sfx s, int v = -1) const { int n = 0; for (auto& p : played) n += p.first == s && (v < 0 || p.second == v); return n; }
};

struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    RecordingAudio audio;
    Env() { ads.state = RewardedState::Unavailable; } // (no Continue offers, unless a check wants one)
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.audio = &audio; return s; }
};

const std::vector<Instance>& bucket(const Game& g, Pass p, MeshId m) { return g.renderList().buckets[size_t(p)][size_t(m)]; }
float henX(const Game& g) { return g.hen().nodes[size_t(g.hen().root)].pos.x; }
constexpr double kReach = Level::kSawReach;

// a run on level 1 (its first 9.6 m are flat street), `eggs` stacked straight away and grown
void startWith(Game& g, int eggs) {
    g.press();
    for (int i = 0; i < eggs; ++i) { g.update(1.0 / 60.0); g.press(); }
    step(g, 0.3f);
}

// runs until the hen is `x` along (or the run is over); what happened on the way
struct Seen { bool bite = false, split = false; int halves = 0, yolks = 0, eggsAfter = -1; float lastX = 0; };
Seen runTo(Game& g, double x) {
    Seen s;
    for (int i = 0; i < 60 * 10 && g.state() == Game::State::Play && henX(g) < x; ++i) {
        g.update(1.0 / 60.0);
        s.bite = s.bite || g.sawing();
        if (g.eggHalves().size() >= 2 && !s.split) { s.split = true; s.halves = int(g.eggHalves().size()); s.yolks = g.yolks(); s.eggsAfter = g.eggCount(); }
        if (g.state() == Game::State::Play) s.lastX = henX(g);
    }
    return s;
}

void bare() {
    Env env;
    Game g(env.svc());
    startWith(g, 0);
    check(g.debugSaw(4.0) && g.saws().size() == 1 && g.saws()[0].y == 0, "a saw set into the street ahead");
    const double sx = g.saws()[0].x;
    const Seen s = runTo(g, sx + 2);
    check(g.state() == Game::State::Dead && !s.bite, "no egg under her: the blade catches the hen");
    check(s.lastX > sx - kReach - 0.1 && s.lastX < sx, "...as she reaches it (not before)");
    check(env.audio.count(Sfx::SawCut, 2) == 1 && env.audio.count(Sfx::SawWhirr) == 1, "...with its screech (and its whine before)");
}

void lastEgg() {
    Env env;
    Game g(env.svc());
    startWith(g, 1);
    check(g.eggCount() == 1, "one egg stacked");
    g.debugSaw(4.5);
    const double sx = g.saws()[0].x;
    const Seen s = runTo(g, sx + kReach + 1);
    check(g.state() == Game::State::Play && g.eggCount() == 0, "one egg: the blade saws it off, and the hen runs on");
    check(s.bite && s.split && s.halves == 2 && s.yolks == 1 && s.eggsAfter == 0, "...the egg falls apart in two halves, the yolk spilling");
    check(g.closeCalls() == 1, "...the blade took her last egg right under her: a CLOSE SHAVE");
    check(g.saws()[0].state == Saw::State::Cut, "...and the saw is spent");
    check(env.audio.count(Sfx::SawCut, 0) == 1 && env.audio.count(Sfx::SawCut, 1) == 1, "...its bite, then the egg's squelch");
}

void oneEach() {
    Env env;
    Game g(env.svc());
    startWith(g, 3);
    g.debugSaw(3.5);
    g.debugSaw(7.0);
    const double second = g.saws()[1].x;
    const float h0 = g.henHeight();
    // the first: one egg off, and the stack settles (falls) one egg down rather than snapping
    int splitFrame = -1;
    float atSplit = 0, settled = 0;
    bool falling = false;
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play; ++i) {
        const int before = g.eggCount();
        g.update(1.0 / 60.0);
        if (before == 3 && g.eggCount() == 2) { splitFrame = i; atSplit = g.henHeight(); }
        if (splitFrame >= 0 && i > splitFrame && i < splitFrame + 30) falling = falling || (g.henHeight() < atSplit - 0.1f && g.henHeight() > h0 - Uf - 0.1f);
        if (splitFrame >= 0 && i == splitFrame + 50) { settled = g.henHeight(); break; } // (after its slow motion, and the jiggle)
    }
    check(splitFrame >= 0 && std::abs(atSplit - h0) < 0.06f, "three eggs: the first saw takes one (the stack's top stays put at the split)");
    check(falling && std::abs(settled - (h0 - Uf)) < 0.06f, "...then the stack drops one egg's height onto what's left");
    runTo(g, second + kReach + 0.5);
    check(g.state() == Game::State::Play && g.eggCount() == 1 && g.closeCalls() == 0, "...the second saw one more: exactly one egg per saw");
}

void rows() {
    // blades back to back: one egg each. Three eggs over a row of three: all sawn off, the last right under her (a
    // CLOSE SHAVE); two eggs: the third blade gets her
    for (int eggs : {3, 2}) {
        Env env;
        Game g(env.svc());
        startWith(g, eggs);
        for (int i = 0; i < 3; ++i) g.debugSaw(1.4 + i * Level::kSawRowGap); // (on the run-up's street, before the first wall)
        const double last = g.saws()[2].x;
        runTo(g, last + kReach + 0.2); // (level 1's first wall is just past it)
        if (eggs == 3)
            check(g.state() == Game::State::Play && g.eggCount() == 0 && g.closeCalls() == 1 && env.audio.count(Sfx::SawCut, 1) == 3,
                  "a row of three blades: three eggs, each sawn off in turn, the last right under her");
        else
            check(g.state() == Game::State::Dead && env.audio.count(Sfx::SawCut, 1) == 2, "...with only two, the third blade gets her");
    }
}

void halves() {
    Env env;
    Game g(env.svc());
    startWith(g, 2);
    g.debugSaw(4.0);
    // the bite: sparks spray out of the cut
    size_t sparksBefore = 0, sparksBite = 0;
    for (int i = 0; i < 60 * 3 && !g.sawing(); ++i) { sparksBefore = g.renderList().particlesAdd.size(); g.update(1.0 / 60.0); }
    sparksBite = g.renderList().particlesAdd.size();
    check(g.sawing() && sparksBite >= sparksBefore + 15, "the bite: sparks spray out of the cut");
    while (g.sawing() && g.state() == Game::State::Play) g.update(1.0 / 60.0);
    std::vector<glm::vec3> h = g.eggHalves();
    check(h.size() == 2 && bucket(g, Pass::Lit, MeshId::EggHalf).size() == 2 && bucket(g, Pass::Lit, MeshId::EggHalfCap).size() == 2,
          "across the blade, the egg falls apart: two shell halves, each with its cut face, drawn");
    const float apart0 = h.size() == 2 ? glm::length(h[0] - h[1]) : 0;
    step(g, 0.3f);
    h = g.eggHalves();
    check(h.size() == 2 && glm::length(h[0] - h[1]) > apart0 + 0.5f, "...they fly apart");
    check(h.size() == 2 && std::max(h[0].z, h[1].z) > 0.25f && std::min(h[0].z, h[1].z) < -0.25f, "...one toward the camera, one away");
    step(g, 1.5f);
    check(g.eggHalves().empty() && g.yolks() == 0, "...and they're gone after a moment (the yolk splatted)");
}

void overdrive() {
    Env env;
    Game g(env.svc());
    startWith(g, 0);
    g.debugStartSurge();
    g.debugSaw(5.0);
    const double sx = g.saws()[0].x;
    bool blade = false;
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && henX(g) < sx + 2; ++i) { g.update(1.0 / 60.0); blade = blade || g.flyingBlades() > 0; }
    check(g.state() == Game::State::Play && g.saws()[0].state == Saw::State::Smashed, "OVERDRIVE: the blade is smashed off its slot, harmless");
    check(blade, "...and flies off spinning");
}

void cubeSaw() {
    for (int spare : {0, 1}) {
        Env env;
        Game g(env.svc());
        g.press();
        g.update(1.0 / 60.0);
        const bool placed = g.debugSaw(0, true);
        const int k = placed ? g.saws()[0].y : 1; // the wall's height over the street: the climb takes k eggs
        for (int i = 0; i < k + spare; ++i) { g.press(); step(g, 0.1f); }
        const double sx = placed ? g.saws()[0].x : 0;
        runTo(g, sx + kReach + 0.5);
        if (spare == 0) {
            check(placed && g.state() == Game::State::Dead, "a cube saw on a wall top: a perfect climb onto it leaves no egg, and the blade gets her");
        } else {
            check(g.state() == Game::State::Play && g.eggCount() == 0 && g.closeCalls() == 1,
                  "...with one egg to spare, the blade saws that one off right under her (a CLOSE SHAVE)");
        }
    }
}

// the teeth's colour on screen (the blade nearest the hen)
glm::vec3 teeth(const Game& g) {
    const auto& t = bucket(g, Pass::Lit, MeshId::SawTeeth);
    return t.empty() ? glm::vec3(0.f) : glm::vec3(t.front().emissive);
}

void warnings() {
    // the blade's teeth, frame by frame as she comes up to it: with no egg under her they strobe, bright; with one, a
    // steady glow
    float bareMax = 0, bareMin = 1e9f, safeMax = 0, safeMin = 1e9f;
    for (int eggs : {0, 1}) {
        Env env;
        Game g(env.svc());
        startWith(g, eggs);
        g.debugSaw(7.0);
        const double sx = g.saws()[0].x;
        runTo(g, sx - kReach - 3.0);
        check(bucket(g, Pass::Lit, MeshId::SawTeeth).size() == 1, eggs ? "...(with an egg)" : "a blade on screen: its toothed rim drawn");
        for (int i = 0; i < 14; ++i) {
            g.update(1.0 / 60.0);
            const glm::vec3 c = teeth(g);
            const float l = c.r + c.g + c.b;
            (eggs ? safeMax : bareMax) = std::max(eggs ? safeMax : bareMax, l);
            (eggs ? safeMin : bareMin) = std::min(eggs ? safeMin : bareMin, l);
        }
    }
    check(bareMax > 1.5f * safeMax && bareMin < 0.6f * bareMax, "no egg under her: the blade strobes, hot and bright, as she nears it");
    check(safeMin > 0.75f * safeMax, "...with an egg over it, a steady red glow");
    {   // the blade turns: its etched spokes move from frame to frame
        Env env;
        Game g(env.svc());
        startWith(g, 1);
        g.debugSaw(8.0);
        g.update(1.0 / 60.0);
        auto spokes = [&] {
            std::vector<glm::vec3> p;
            for (const Instance& b : bucket(g, Pass::Lit, MeshId::Box))
                if (std::abs(glm::length(glm::vec3(b.model[0])) - Level::kSawR * 0.5f) < 1e-3f) p.push_back(glm::vec3(b.model[3]));
            return p;
        };
        const auto a = spokes();
        g.update(1.0 / 60.0);
        const auto b = spokes();
        float moved = 0;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) moved += glm::length(a[i] - b[i]);
        check(a.size() == 3 && b.size() == 3 && moved > 0.01f, "...and it spins (its etched spokes move)");
    }
}

void droidHolds() {
    // a droid raid's shot falls due just before two saws: zapping one of her two eggs would leave her short for the
    // second, so it holds its fire (and she keeps an egg for each blade). On the first street flat after a drop
    Env env;
    Game g(env.svc());
    startWith(g, 3);
    const Segment* flat = nullptr;
    for (size_t i = 1; i < g.course().segs.size() && !flat; ++i) {
        const Segment& s = g.course().segs[i];
        if (s.h == 0 && g.course().segs[i - 1].h > 0 && s.x1 - s.x0 > 5.5) flat = &s;
    }
    check(flat != nullptr, "a street flat after a drop, early in level 1");
    if (!flat) return;
    const double saw1 = flat->x0 + 2.6, saw2 = saw1 + 1.4, wallH = g.course().segs[size_t(flat - &g.course().segs[0]) - 1].h;
    const float x0 = henX(g);
    g.update(1.0 / 60.0);
    const float v = (henX(g) - x0) * 60;
    // the raid's shot comes 1.2 s after it starts: start it so the shot falls due just before the first saw (both
    // saws within the third of a second it leaves her to stack again)
    runTo(g, saw1 - kReach - 0.35 - v * 1.2);
    g.debugDroidRaid(1);
    g.debugSaw(saw1 - henX(g));
    g.debugSaw(saw2 - henX(g));
    check(g.droidRaiding() && g.saws().size() == 2 && g.saws()[0].y == 0 && g.saws()[1].y == 0 && g.eggCount() == 3 && wallH == 1,
          "...a raid on her, two saws ahead on it");
    runTo(g, saw2 + kReach + 0.5);
    check(g.state() == Game::State::Play && env.audio.count(Sfx::DroidLaser) == 0 && g.eggCount() == 0,
          "...due just before two saws, it holds its fire: she keeps an egg for each blade");
}

void revive() {
    Env env;
    env.ads.state = RewardedState::Ready;
    Game g(env.svc());
    startWith(g, 0);
    g.debugSaw(4.0);
    runTo(g, 10);
    check(g.state() == Game::State::Dead, "caught by a blade...");
    step(g, Game::kCrashTail + 0.2f);
    g.tapButton("Watch ad");
    step(g, 0.2f);
    check(g.state() == Game::State::Play && g.saws()[0].state != Saw::State::Armed, "...revived: that blade is spent (it sinks into its slot)");
    const double sx = g.saws()[0].x;
    runTo(g, sx + 3);
    check(g.state() == Game::State::Play && henX(g) > sx + 2, "...and she runs on over it");
}

void campaign() {
    {   // level 11's run has its saws; level 10's none
        Env env;
        Campaign(&env.st).debugSetLevel(10);
        Game g(env.svc());
        g.press();
        check(g.saws().empty(), "level 10: no saws");
    }
    Env env;
    Campaign(&env.st).debugSetLevel(11);
    Game g(env.svc());
    g.debugSeed(3);
    g.debugSetBot({true, false, false, 0});
    g.press();
    // (the course is laid as she runs: count the saws as they come; in a maze, one may sit over another, on another
    // floor, and each takes its own egg: the tomb has two in one column, the great hall's and the bottom hall's)
    std::vector<std::pair<double, int>> met, cut;
    for (int i = 0; i < 60 * 120 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        for (const Saw& s : g.saws()) {
            const std::pair<double, int> at{s.x, s.y};
            if (std::find(met.begin(), met.end(), at) == met.end()) met.push_back(at);
            if (s.state == Saw::State::Cut && std::find(cut.begin(), cut.end(), at) == cut.end()) cut.push_back(at);
        }
    }
    int tombSaws = 0; // (the tomb's own, in its grid)
    if (const Gauntlet* t = Campaign::gauntletOf(11))
        for (int c = 0; c < t->cols(); ++c)
            for (int r = 0; r < t->rows(); ++r) tombSaws += t->sawAt(c, r) ? 1 : 0;
    check(tombSaws > 0 && int(met.size()) == Campaign::sawsOf(11) + tombSaws, "level 11: its saws come along the course (and the tomb's own)");
    check(g.lastRunCleared(), "the bot clears level 11, saws and all");
    check(cut.size() == met.size() && g.closeCalls() >= 1, "...every saw met with an egg over it (a close shave or more)");
}

} // namespace

int main() {
    test::useFixtures(); // (the levels and gauntlets these tests are written round: tests/fixtures)
    bare();
    lastEgg();
    oneEach();
    rows();
    halves();
    overdrive();
    cubeSaw();
    warnings();
    droidHolds();
    revive();
    campaign();
    return finish("GameSaw");
}
