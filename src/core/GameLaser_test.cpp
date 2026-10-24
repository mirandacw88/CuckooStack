// Wall lasers (GameLaser.cpp), in the real Game at a fixed 60 Hz step. A laser in a wall's side charges as she comes
// near (its pointer drawn, a rising whine), then fires as its row meets her: at an egg in her stack it cuts exactly
// that egg out (two halves fly apart, red-hot faces, a yolk) and the eggs over it drop; the egg right under her is a
// CLOSE SHAVE. At her body it cuts her in half. Over her head nothing happens, and just over it it fires past her: a
// CLOSE CALL. It hits her once a pass, however long she's in its line. The bot takes a low one with an egg to spare
// and passes a high one short.
#include "TestUtil.h"
#include "core/Game.h"
#include "core/Level.h"
#include "core/BotRun.h"
#include "core/LevelCatalog.h"

#include <cmath>
#include <cstdio>

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

float henX(const Game& g) { return g.hen().nodes[size_t(g.hen().root)].pos.x; }

// a run on level 1 (its first 9.6 m are flat street), `eggs` stacked straight away
void startWith(Game& g, int eggs) {
    g.press();
    for (int i = 0; i < eggs; ++i) { g.update(1.0 / 60.0); g.press(); }
    step(g, 0.3f);
}

void runTo(Game& g, double x) {
    for (int i = 0; i < 60 * 10 && g.state() == Game::State::Play && henX(g) < x; ++i) g.update(1.0 / 60.0);
}

void underHerFeet() {
    Env env;
    Game g(env.svc());
    startWith(g, 3);
    check(g.eggCount() == 3, "three eggs stacked");
    check(g.debugLaser(5.5, 1, -1, 3.0) && g.lasers().size() == 1 && g.lasers()[0].row == 1, "a laser in a wall's side ahead, its row one egg up, firing back at her");
    const float h0 = g.henHeight();
    const Laser& z = g.lasers()[0];
    bool charging = false;
    for (int i = 0; i < 60 * 3 && z.state != Laser::State::Firing; ++i) { g.update(1.0 / 60.0); charging = charging || z.state == Laser::State::Charging; }
    check(charging && env.audio.count(Sfx::LaserHum) == 1, "it charges as she comes near (its whine), before it fires");
    runTo(g, z.x0 + 0.3);
    check(g.state() == Game::State::Play && g.eggCount() == 2 && g.laserZapped(), "it fires as she comes into its line: her second egg cut out, and she runs on");
    check(g.eggHalves().size() == 2 && g.yolks() == 1, "...cut in two halves, the yolk spilling");
    check(g.closeCalls() == 0, "...not the egg under her: no near miss");
    check(env.audio.count(Sfx::LaserCut, 2) == 1 && env.audio.count(Sfx::LaserCut, 0) == 1, "...its blast, and the egg's zip");
    runTo(g, z.x1 - 0.3);
    check(g.eggCount() == 2 && g.eggHalves().size() >= 2 && env.audio.count(Sfx::LaserCut, 0) == 1, "...once a pass: the egg dropped into its row isn't cut again");
    step(g, 0.5f);
    check(std::abs(g.henHeight() - (h0 - Uf)) < 0.08f, "...and the eggs over it, and the hen, drop onto the one under it");
}

void rightUnderHer() {
    Env env;
    Game g(env.svc());
    startWith(g, 2);
    g.debugLaser(5.5, 1, -1, 3.0);
    runTo(g, g.lasers()[0].x0 + 0.4);
    check(g.state() == Game::State::Play && g.eggCount() == 1 && g.closeCalls() == 1, "its row at the egg right under her: cut out, a CLOSE SHAVE");
}

void atHerBody() {
    Env env;
    Game g(env.svc());
    startWith(g, 1);
    g.debugLaser(5.5, 1, -1, 3.0);
    runTo(g, g.lasers()[0].x0 + 0.4);
    check(g.state() == Game::State::Dead && g.death() == Game::Death::Sliced, "its row at her body: she's cut in half");
    check(env.audio.count(Sfx::LaserCut, 1) == 1, "...with its searing zap");
    if (g.hen().canShatter() && g.state() == Game::State::Dead) {
        check(g.hen().pieces() == 2, "...the hen in two pieces");
        const float topY = g.hen().pieceCentre(0).y, botY = g.hen().pieceCentre(1).y;
        step(g, 0.5f);
        check(g.hen().pieceCentre(0).y > topY - 0.6f && g.hen().pieceCentre(0).x != g.hen().pieceCentre(1).x && topY > botY,
              "...the top half above the cut, thrown off, the bottom half under it");
    }
}

void overHerHead() {
    for (int height : {3, 2}) {
        Env env;
        Game g(env.svc());
        startWith(g, 0);
        g.debugLaser(5.5, height, -1, 3.0);
        runTo(g, g.lasers()[0].x0 + 0.6);
        if (height == 3) check(g.state() == Game::State::Play && g.closeCalls() == 0 && g.eggHalves().empty() && env.audio.count(Sfx::LaserCut, 2) == 0, "its row well over her head: it holds its fire");
        else check(g.state() == Game::State::Play && g.closeCalls() == 1 && env.audio.count(Sfx::LaserCut, 2) == 1, "...just over it: it fires past her, a CLOSE CALL");
    }
}

void drawn() {
    Env env;
    Game g(env.svc());
    startWith(g, 1);
    g.debugLaser(5.5, 1, -1, 3.0); // (at her body, as she is)
    auto beams = [&](float& widest, float& hottest) {
        int n = 0;
        widest = hottest = 0;
        for (const Instance& i : g.renderList().buckets[size_t(Pass::UnlitAdd)][size_t(MeshId::Plane)])
            if (int(i.params.z) == int(Shape::Laser)) { ++n; widest = std::max(widest, glm::length(glm::vec3(i.model[0]))); hottest = std::max(hottest, i.color.r); }
        return n;
    };
    float w = 0, hot = 0;
    for (int i = 0; i < 60 * 3 && g.lasers()[0].state != Laser::State::Charging; ++i) g.update(1.0 / 60.0);
    check(beams(w, hot) >= 1 && w < 0.05f, "charging: its pointer drawn, a thin line along its line");
    for (int i = 0; i < 60 * 3 && g.lasers()[0].state != Laser::State::Firing; ++i) g.update(1.0 / 60.0);
    g.update(1.0 / 60.0);
    check(beams(w, hot) >= 3 && w > 0.2f && hot > 3.f, "firing: a thick beam, white-hot at its core");
}

void bot() {
    // a low laser (she can't pass it short: an egg in its row, cut out) then a high one (short: no more than two eggs)
    Env env;
    Game g(env.svc());
    g.debugSetBot({true, false, false, 0});
    startWith(g, 0);
    g.debugLaser(3.6, 0, -1, 1.2);
    g.debugLaser(6.6, 4, -1, 1.5);
    int most = 0;
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && henX(g) < g.lasers()[1].x + 0.3; ++i) {
        g.update(1.0 / 60.0);
        if (henX(g) > g.lasers()[1].x0 - 0.2) most = std::max(most, g.eggCount());
    }
    check(g.state() == Game::State::Play && g.eggHalves().size() >= 2, "the bot: an egg into the low one's row (cut out)");
    check(most <= 2, "...and short under the high one");
}

// her run over (cut in half), the beam that fired finishes and the laser cools: it isn't drawn firing on forever
void cools() {
    Env env;
    Game g(env.svc());
    startWith(g, 0);
    g.debugLaser(5.5, 0, -1, 3.0);
    runTo(g, g.lasers()[0].x0 + 0.4);
    check(g.state() == Game::State::Dead && g.lasers()[0].state == Laser::State::Firing, "cut in half: its beam firing");
    check(glm::length(g.debugFlashColor()) > 0 && g.debugFlashColor().r > 2.f * g.debugFlashColor().b, "...the screen flashing red (not the crash's pink)");
    step(g, 1.5f);
    check(g.lasers()[0].state == Laser::State::Idle || g.lasers()[0].state == Laser::State::Resting, "...then it ends and the laser cools");
}

// one hit a pass: out of the first's reach, the next one ahead hits her again
void again() {
    Env env;
    Game g(env.svc());
    startWith(g, 3);
    g.debugLaser(4.0, 0, -1, 1.2);
    g.debugLaser(7.0, 0, -1, 1.2);
    runTo(g, g.lasers()[1].x0 - 0.3);
    check(g.eggCount() == 2 && !g.laserZapped(), "the first laser's egg cut; out of its reach, she can be hit again");
    runTo(g, g.lasers()[1].x0 + 0.4);
    check(g.state() == Game::State::Play && g.eggCount() == 1 && g.eggHalves().size() >= 4, "...and the next takes its egg too");
}

// OVERDRIVE: it can't touch her (it fires, she runs through); coming out of it she keeps the egg a laser ahead takes
void overdrive() {
    Env env;
    Game g(env.svc());
    startWith(g, 0);
    g.debugSetChain(0, 3.f); // (its grace: smashing)
    g.debugLaser(5.5, 0, -1, 3.0);
    runTo(g, g.lasers()[0].x0 + 0.4);
    check(g.state() == Game::State::Play && g.eggHalves().empty(), "in OVERDRIVE a laser at her body can't touch her");
    for (const bool laser : {false, true}) {
        Env e2;
        Game h(e2.svc());
        startWith(h, 6);
        h.debugSetChain(0, 0.05f); // (OVERDRIVE ending now, a tall stack: she sheds what's not needed)
        if (laser) h.debugLaser(5.5, 1, -1, 3.0);
        step(h, 0.2f);
        if (!laser) check(h.eggCount() < 2, "...coming out of it with nothing ahead to need them, she sheds her eggs");
        else check(h.eggCount() >= 2, "...but keeps an egg in the row of a laser ahead she can't pass short");
    }
}

// a bot's run (BotRun) records how she died
void botRunDeath() {
    LevelCatalog cat;
    std::string why;
    check(cat.setLevel(1, "length 40\ndifficulty 0 0.35\nraids none\ncourse\n10 0 crate\n2 3 crate\n80 0 crate\nsaws\ndiscos\nlasers\n6 1 0\n", &why), why.c_str());
    const GameRun run = playLevel(1, &cat);
    check(!run.cleared && run.death == int(Game::Death::Sliced), "a run the bot can't clear: how she died (a laser's beam at her body from the start)");
    check(playLevel(1, &LevelCatalog::builtin()).death == -1, "...a cleared run: no death");
}

} // namespace

int main() {
    underHerFeet();
    rightUnderHer();
    atHerBody();
    overHerHead();
    drawn();
    bot();
    cools();
    again();
    overdrive();
    botRunDeath();
    return test::finish("GameLaser");
}
