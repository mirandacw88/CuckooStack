// Lava (GameLava.cpp), in the real Game at a fixed 60 Hz step. Each cell she crosses on it burns the egg under her (an
// egg burning in the lava, charring and sinking: drawn, then gone), so lava `len` cells long takes `len` eggs, and the
// last one right under her is a CLOSE SHAVE; the egg under her glows as she crosses a cell. Too few eggs: she burns up
// (the hen charring, crumbling). The bot stacks enough to cross.
#include "TestUtil.h"
#include "core/Game.h"
#include "core/Level.h"

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

void startWith(Game& g, int eggs) {
    g.press();
    for (int i = 0; i < eggs; ++i) { g.update(1.0 / 60.0); g.press(); }
    step(g, 0.3f);
}

void crossing() {
    Env env;
    Game g(env.svc());
    startWith(g, 4);
    check(g.debugLava(3.0, 3) && g.lavas().size() == 1 && g.lavas()[0].cells() == 3, "lava three cells long on the street ahead");
    const Lava f = g.lavas()[0];
    int most = 0;
    float glow = 0;
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && henX(g) < f.x1 + 0.3; ++i) {
        g.update(1.0 / 60.0);
        most = std::max(most, g.burntEggs());
        glow = std::max(glow, g.eggBurn());
    }
    check(g.state() == Game::State::Play && g.eggCount() == 1, "four eggs over three cells: three burn, and she runs on with one");
    check(most >= 1 && env.audio.count(Sfx::LavaBurn, 0) == 3, "...each burning in the lava as she crosses its cell");
    check(glow > 0.3f, "...the egg under her glowing as she crosses a cell");
    check(env.audio.count(Sfx::LavaSizzle) == 1, "...its sizzle as it came up");
    check(g.closeCalls() == 0, "...an egg to spare: no near miss");
    step(g, 1.2f);
    check(g.burntEggs() == 0, "...the burnt eggs crumble away");
}

void exactly() {
    Env env;
    Game g(env.svc());
    startWith(g, 2);
    g.debugLava(3.0, 2);
    const Lava f = g.lavas()[0];
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && henX(g) < f.x1 + 0.3; ++i) g.update(1.0 / 60.0);
    check(g.state() == Game::State::Play && g.eggCount() == 0 && g.closeCalls() == 1, "two eggs over two cells: the last right under her, a CLOSE SHAVE");
}

void tooFew() {
    Env env;
    Game g(env.svc());
    startWith(g, 1);
    g.debugLava(3.0, 3);
    const Lava f = g.lavas()[0];
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && henX(g) < f.x1 + 0.3; ++i) g.update(1.0 / 60.0);
    check(g.state() == Game::State::Dead && g.death() == Game::Death::Burned, "one egg over three cells: she burns up");
    check(henX(g) > f.x0 && henX(g) < f.x0 + 2 * U, "...on its second cell");
    check(env.audio.count(Sfx::LavaBurn, 1) == 1, "...with the roar of it");
    const glm::vec3 flash = g.debugFlashColor();
    check(flash.r > 3.f * flash.b && flash.g > 0.15f * flash.r, "...the screen flashing fire orange (not the crash's pink)");
    if (g.hen().canShatter()) {
        check(g.hen().breakup() == Hen::Breakup::Char && g.hen().pieces() > 2, "...the hen charring, then crumbling to ash");
        const glm::vec3 c = g.hen().pieceCentre(0);
        step(g, 0.3f);
        check(std::abs(g.hen().pieceCentre(0).y - c.y) < 0.05f, "...holding together a beat as she burns");
        step(g, 1.2f);
        check(g.hen().pieceCentre(0).y < c.y, "...then falling apart");
    }
}

void drawn() {
    Env env;
    Game g(env.svc());
    startWith(g, 0);
    g.debugLava(4.0, 4);
    step(g, 0.1f);
    int molten = 0;
    for (const Instance& i : g.renderList().buckets[size_t(Pass::Lit)][size_t(MeshId::Box)])
        molten += i.params.x < 0 && i.emissive.r > 1.5f && i.emissive.r > 2.f * i.emissive.b;
    check(molten >= 8, "the lava drawn: its molten surface, glowing orange");
}

void bot() {
    Env env;
    Game g(env.svc());
    g.debugSetBot({true, false, false, 0});
    startWith(g, 0);
    g.debugLava(3.5, 4);
    const Lava f = g.lavas()[0];
    for (int i = 0; i < 60 * 3 && g.state() == Game::State::Play && henX(g) < f.x1 + 0.5; ++i) g.update(1.0 / 60.0);
    check(g.state() == Game::State::Play && henX(g) > f.x1, "the bot: stacks enough to cross lava four cells long");
}

} // namespace

int main() {
    crossing();
    exactly();
    tooFew();
    drawn();
    bot();
    return test::finish("GameLava");
}
