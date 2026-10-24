// Springs (Level.h's Spring, hazards/Spring.cpp, GameSpring.cpp, CourseRules.h's springFlight). In the real Game at a
// fixed 60 Hz step: she runs onto one and is launched `power` rows up (the top of her arc that high over the pad), her
// speed kept, and comes down on what's ahead; the pad fires once, with its boing. The game flies her as springFlight
// traces it. The checker: a spring onto a tower is fine; one into a wall's face, under a barrier, or onto a landing too
// short is noted; what a spring flies her over costs no eggs. The level file reads and writes springs; the bot plays a
// spring level through, laying nothing for the tower it's flown onto.
#include "TestUtil.h"
#include "core/BotRun.h"
#include "core/CourseRules.h"
#include "core/Game.h"
#include "core/Hazards.h"
#include "core/LevelCatalog.h"
#include "core/LevelData.h"
#include "core/LevelJudge.h"

#include <algorithm>
#include <cmath>
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
    Env() { ads.state = RewardedState::Unavailable; }
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.audio = &audio; return s; }
};

// a level: 20 cells of street, a tower `h` high and 30 long, then street; a spring at x of `power` (and a barrier
// over the street before the tower, `ceil` high, if any)
std::string course(double x, int power, int h = 6, int ceil = 0) {
    return "length 60\ndifficulty 0 0.35\nraids none\ncourse\n" + std::string(ceil ? "10 0 crate\n10 0 crate " + std::to_string(ceil) + "\n" : "20 0 crate\n") +
           "30 " + std::to_string(h) + " crate\n60 0 crate\nsaws\ndiscos\nsprings\n" + levelNum(x) + " " + std::to_string(power) + "\n";
}

Level laid(const std::string& text) {
    std::string why;
    Level l;
    const auto d = parseLevel(1, text, &why);
    if (!d) { check(false, ("the course reads: " + why).c_str()); return l; }
    l.load(*d);
    l.generateUntil(d->length + 40);
    return l;
}

bool noted(const Report& r, const std::string& what) {
    for (const auto& n : r.notes) if (n.text.find(what) != std::string::npos) return true;
    return false;
}

// where along the street a spring of `power` flies her onto the tower at every speed she may run (the checker's say)
double ontoTheTower(int power, int h = 6) {
    for (double x = 2.0; x < 11.5; x += 0.1) {
        const Level l = laid(course(x, power, h));
        const Report r = judge(l);
        if (r.notes.empty()) return x;
    }
    return -1;
}

void flight() {
    const Level l = laid(course(4.0, 8));
    if (l.springs.size() != 1) { check(false, "a spring laid"); return; }
    const Spring& s = l.springs[0];
    check(s.floor == 0 && s.power == 8, "a spring on the street, 8 rows of lift");
    check(std::abs(rules::springLaunch(8) - std::sqrt(2 * rules::kGravity * 8 * U)) < 1e-9, "its launch: up as fast as a fall of 8 rows lands");
    const rules::Flight open = rules::springFlight(laid(course(4.0, 8, 0)), s, 8.0);
    check(open.end == rules::Flight::End::Landed && open.floor == 0 && std::abs(open.apexY - 8 * U) < 0.05,
          "on the flat: the top of her arc 8 rows up, and down on the street again");
    const double air = 2 * rules::springLaunch(8) / rules::kGravity;
    check(std::abs((open.x - s.x) - 8.0 * air) < 0.1, "...as far on as she runs in the air");
    const double at = ontoTheTower(8);
    check(at > 0, "a spring before a tower 6 high: a place it flies her onto its top at every speed");
    const rules::Flight up = rules::springFlight(laid(course(at, 8)), laid(course(at, 8)).springs[0], laid(course(at, 8)).curveAt(at).speed);
    check(up.end == rules::Flight::End::Landed && up.floor == 6, "...landing on the tower");
    const Level close = laid(course(11.0, 8));
    check(rules::springFlight(close, close.springs[0], close.curveAt(11.0).speed).end == rules::Flight::End::Wall, "too close to it: into its face");
    check(rules::springFlight(close, close.springs[0], close.curveAt(11.0).speed, 7).end == rules::Flight::End::Landed,
          "...but with eggs enough under her she steps onto it, as onto any wall");
}

void checker() {
    const double at = ontoTheTower(8);
    check(judge(laid(course(at, 8))).notes.empty(), "a spring onto a tower: no notes");
    check(noted(judge(laid(course(11.0, 8))), "into a wall's face"), "one into the tower's face: noted");
    check(noted(judge(laid(course(3.0, 8, 6, 5))), "into a barrier"), "one under a barrier: noted");
    check(noted(judge(laid(course(at, 3))), "too close to the next wall"), "one too weak for the tower (down just short of it): noted");
    // the budget: the tower's climb is free off a spring
    const double tall = ontoTheTower(11, 8);
    const Report with = judge(laid(course(tall, 11, 8))), without = judge(laid("length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n30 8 crate\n60 0 crate\nsaws\ndiscos\n"));
    check(tall > 0 && !without.short_.empty() && with.short_.empty() && with.notes.empty(), "a tower 8 high: more eggs than a careful player holds, but none off a spring");
}

void file() {
    std::string why;
    const auto d = parseLevel(1, course(5.5, 9), &why);
    check(d && d->items.size() == 1 && d->items[0].type == &kSpringHazard && d->items[0].at(1) == 9, "the level file's springs: where along, its lift");
    check(d && levelText(*d).find("springs\n5.5 9\n") != std::string::npos, "...written back as it was");
    check(!parseLevel(1, course(5.5, 13), &why), "a lift past 12: turned away");
    check(!parseLevel(1, "length 60\ncourse\n10 0 crate\n168 0 gauntlet tomb\n40 0 crate\nsaws\ndiscos\nsprings\n20 6\n", &why) && why.find("tomb") != std::string::npos,
          "...in a gauntlet: turned away");
    check(kSpringHazard.group == std::string("bouncers") && kSpringHazard.key == std::string("j"), "its tool: J, in the Bouncers group");
    check(registryJson().find("\"spring\"") != std::string::npos, "...in the editors' registry");
}

void launch() {
    std::string why;
    check(debugOverrideLevel(1, "length 80\ndifficulty 0 0.35\nraids none\ncourse\n140 0 crate\nsaws\ndiscos\n", &why), "(level 1: a flat street)");
    Env env;
    Game g(env.svc());
    g.press();
    step(g, 0.3f);
    check(g.debugSpring(3.0, 6), "a spring 3 m ahead on the street");
    double top = 0, landed = -1;
    bool up = false;
    float squash = 0;
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        top = std::max(top, double(g.henBase()));
        up = up || g.launched();
        squash = std::max(squash, g.springs().empty() ? 0.f : g.springs()[0].squash);
        if (up && !g.launched() && landed < 0) landed = g.henBase();
    }
    check(g.springsFired() == 1 && up, "she runs onto it: launched, once");
    check(std::abs(top - 6 * U) < 0.2, "...the top of her arc 6 rows up");
    check(landed >= 0 && landed < 0.05 && g.state() == Game::State::Play, "...and down on the street again, running on");
    check(env.audio.count(Sfx::Bounce, 2) == 1 && squash > 0.5f, "...its boing, its pad firing");
}

void bot() {
    const double at = ontoTheTower(8);
    LevelCatalog cat;
    std::string why;
    check(cat.setLevel(1, course(at, 8), &why), why.c_str());
    const GameRun run = playLevel(1, &cat);
    check(run.cleared, "the bot clears a spring level");
    int most = 0;
    for (const auto& t : run.trace) if (t[0] < 12.0) most = std::max(most, int(t[2]));
    check(most == 0, "...laying nothing for the tower the spring flies it onto");
    LevelCatalog bad;
    check(bad.setLevel(1, course(11.0, 8), &why), why.c_str());
    const GameRun crash = playLevel(1, &bad);
    check(!crash.cleared && crash.death == int(Game::Death::Crash), "one into the tower's face: it crashes, as the checker said");
}

} // namespace

int main() {
    flight();
    checker();
    file();
    launch();
    bot();
    return test::finish("GameSpring");
}
