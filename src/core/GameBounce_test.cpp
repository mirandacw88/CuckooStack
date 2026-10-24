// Bouncers on the course (Level.h's WallKind::Bumper, GateR, GateL; Level::buildZones). Bumper and gate walls read and
// write as wall kinds; each group of them is a bounce zone, an open-air Gauntlet of the course's cells there that she
// plays by the maze's rules: a bumper at her body turns her back, a gate lets her through its way only, and she stacks
// while she bounces to climb out. The checker solves each zone (out by stacking, no bouncing forever); the bot plays one
// through; one she can't get out of is noted.
#include "TestUtil.h"
#include "core/BotRun.h"
#include "core/Campaign.h"
#include "core/Game.h"
#include "core/Hazards.h"
#include "core/LevelCatalog.h"
#include "core/LevelData.h"
#include "core/LevelJudge.h"

#include <string>

using namespace cs;
using namespace test;

namespace {

// 20 cells of street, a gate letting her through right, a run of street, a bumper wall `h` high, then street
std::string pinball(int h, int run = 10, const std::string& left = "gate>") {
    return "length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n1 3 " + left + "\n" + std::to_string(run) + " 0 crate\n2 " + std::to_string(h) +
           " bumper\n80 0 crate\nsaws\ndiscos\n";
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

void file() {
    std::string why;
    const auto d = parseLevel(1, pinball(3), &why);
    check(d.has_value(), why.c_str());
    check(d && d->course[1].kind == WallKind::GateR && d->course[3].kind == WallKind::Bumper, "bumper and gate walls: wall kinds");
    check(d && levelText(*d).find("\n1 3 gate>\n10 0 crate\n2 3 bumper\n") != std::string::npos, "...written back as they were");
    check(!parseLevel(1, "length 60\ncourse\n20 0 crate\n2 3 bumper 6\n40 0 crate\n", &why), "no barrier over a bumper");
    check(!parseLevel(1, "length 60\ncourse\n20 0 crate\n2 0 bumper\n40 0 crate\n", &why), "...and at least an egg high");
    check(wallKindNamed("gate<") && wallKindNamed("gate<")->bouncer && !wallKindNamed("crate")->bouncer, "the registry's kinds say which bounce");
}

void zones() {
    const Level l = laid(pinball(3));
    check(l.zones.size() == 1, "a gate and a bumper: one bounce zone");
    if (l.zones.empty()) return;
    const PlacedGauntlet& z = l.zones[0];
    check(z.g->isZone() && std::abs(z.x0 - 19 * U) < 1e-9 && z.g->cols() == 14, "...from the column before the gate to the bumper's end");
    check(z.g->at(1, 0) == Gauntlet::Cell::GateR && z.g->at(12, 2) == Gauntlet::Cell::Bumper && z.g->at(12, 3) == Gauntlet::Cell::Air, "...its gate and bumper the maze's cells");
    check(z.g->at(5, z.g->rows() + 3) == Gauntlet::Cell::Air, "...open to the sky");
    check(z.g->solve().reachable && z.g->solve().softLocks == 0, "...out by stacking over the bumper, no bouncing forever");
    check(!l.gauntletAt(z.x0 + 1) && l.zoneAt(z.x0 + 1) == &z, "a zone isn't a maze (gauntletAt), it's a zone (zoneAt)");
    check(laid("length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n2 3 bumper\n80 0 crate\n2 3 bumper\n20 0 crate\n").zones.size() == 2,
          "bouncers far apart: a zone each");
}

void checker() {
    check(!noted(judge(laid(pinball(3))), "bounce zone"), "a zone she can stack out of: no notes");
    check(noted(judge(laid(pinball(30, 1))), "can't stack out"), "one with a bumper too tall to stack over in the room it has: noted");
}

void bot() {
    LevelCatalog cat;
    std::string why;
    check(cat.setLevel(1, pinball(3), &why), why.c_str());
    const GameRun run = playLevel(1, &cat);
    check(run.cleared, "the bot plays a bounce zone through and clears the level");
}

struct NoAudio : IAudio {
    void play(Sfx, int) override {}
    void musicStart(bool) override {}
    void musicStop(bool) override {}
    void musicTempo(float) override {}
    void musicSurge(bool) override {}
    void setMuted(bool) override {}
};

// no taps: off the bumper she runs back left, off the gate (set against her that way) right again, and so on
void bouncing() {
    std::string why;
    check(debugOverrideLevel(1, pinball(3), &why), why.c_str());
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    ads.state = RewardedState::Unavailable;
    NoAudio audio;
    GameServices svc; svc.storage = &st; svc.clock = &clock; svc.ads = &ads; svc.audio = &audio;
    Game g(svc);
    g.press();
    int turns = 0;
    float last = 0, dir = 1, lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 60 * 8 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        const float x = g.hen().nodes[size_t(g.hen().root)].pos.x;
        if (i > 0 && (x - last) * dir < -1e-4f) { dir = -dir; ++turns; }
        if (i > 60 * 4) { lo = std::min(lo, x); hi = std::max(hi, x); }
        last = x;
    }
    check(g.state() == Game::State::Play && turns >= 3, "no taps: turned back again and again, never crashing");
    check(lo > 19 * U && hi < 33 * U, "...kept between the gate and the bumper");
}

} // namespace

// a deadly bounce box: a step inside it with lava on its top, a wall laser in its face firing back at the gate. The zone
// carries them as the maze's cells (the solver sees them), the game burns and cuts as anywhere, and the bot gets out
void deadly() {
    const std::string box = "length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n1 3 gate>\n6 0 crate\n5 2 crate\n6 0 crate\n2 4 bumper\n80 0 crate\nsaws\ndiscos\n"
                            "lasers\n16.2 1 0\nlava\n16.8 2\n";
    const Level l = laid(box);
    if (l.zones.empty()) { check(false, "a zone"); return; }
    const Gauntlet& g = *l.zones[0].g;
    const int step = static_cast<int>(std::lround(16.2 / U - l.zones[0].x0 / U));
    check(g.at(step, 1) == Gauntlet::Cell::LaserL && g.at(step + 1, 1) == Gauntlet::Cell::Lava && g.at(step + 2, 1) == Gauntlet::Cell::Lava,
          "the box's laser and lava: the maze's cells in its zone");
    check(g.beamAt(step - 1, 1) && g.beamColumn(step - 3), "...the laser's beam traced back along the run");
    int least = -1;
    for (int e = 0; e <= 8 && least < 0; ++e) if (g.arriving(e).solve().reachable) least = e;
    check(least > 0 && g.arriving(least).solve().softLocks == 0 && !g.solve().reachable,
          "...solved coming into it stacked (with none: the beam across its run cuts her in half), no bouncing forever");
    check(!noted(judge(l), "bounce zone"), "...and the checker has nothing to say");
    LevelCatalog cat;
    std::string why;
    check(cat.setLevel(1, box, &why), why.c_str());
    check(playLevel(1, &cat).cleared, "the bot gets through the deadly box");
    check(noted(judge(laid("length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n1 3 gate>\n10 0 crate\n2 4 bumper\n80 0 crate\nsaws\ndiscos\nlava\n14.4 2\n")), "on the street"),
          "lava on the street in a zone: noted (its grid has no row for it)");
}

// a spring inside a bounce box: on a step's top it is a spring cell of the zone (flown by the maze's arc, the same at any
// speed), the checker no longer turns it away, and the bot flies it, bounced off the bumper in the air and back down
void spring() {
    const std::string box = "length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n1 3 gate>\n4 0 crate\n3 1 crate\n16 0 crate\n2 4 bumper\n80 0 crate\nsaws\ndiscos\n"
                            "springs\n15.9 5\n";
    const Level l = laid(box);
    if (l.zones.empty()) { check(false, "a zone"); return; }
    const Gauntlet& g = *l.zones[0].g;
    const int at = static_cast<int>(std::floor((15.9 - l.zones[0].x0) / U));
    check(g.at(at, 0) == Gauntlet::Cell::Spring && g.springAt(at, 0) && g.at(at - 1, 0) == Gauntlet::Cell::Stone, "a spring on a step's top in a box: the maze's spring cell in its zone");
    int least = -1;
    for (int e = 0; e <= 8 && least < 0; ++e) if (g.arriving(e).solve().reachable) least = e;
    check(least >= 0 && g.arriving(least).solve().softLocks == 0, "...the zone solved over it (flown over the street to its landing, stacking for the bumper)");
    const Report r = judge(l);
    check(!noted(r, "spring") && !noted(r, "bounce zone"), "...and the checker has nothing to say (no 'spring in a bounce zone' note)");
    check(noted(judge(laid("length 60\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n1 3 gate>\n10 0 crate\n2 4 bumper\n80 0 crate\nsaws\ndiscos\nsprings\n14.4 5\n")), "on the street"),
          "a spring on the street in a zone: noted (its grid has no row for the pad)");
    LevelCatalog cat;
    std::string why;
    check(cat.setLevel(1, box, &why), why.c_str());
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    ads.state = RewardedState::Unavailable;
    NoAudio audio;
    GameServices svc; svc.storage = &st; svc.clock = &clock; svc.ads = &ads; svc.audio = &audio; svc.levels = &cat;
    Campaign(&st, &cat).debugSetLevel(1);
    Game game(svc);
    game.debugSeed(1);
    game.debugSetBot({true, false, false, 0});
    game.setHeadless(true);
    game.press();
    for (int i = 0; i < 60 * 120 && game.state() == Game::State::Play; ++i) game.update(1.0 / 60.0);
    check(game.lastRunCleared() && game.springsFired() >= 1, "the bot flies the spring in the box and clears the level");
}

int main() {
    file();
    zones();
    checker();
    bot();
    bouncing();
    deadly();
    spring();
    return test::finish("GameBounce");
}
