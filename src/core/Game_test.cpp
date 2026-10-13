// The game (Game.cpp) run by run, headless at a fixed 60 Hz step:
//  - Share Replay, the game's side (Game + IReplay): capture only during play, the crash tail and the finish
//    celebration, when the clip is requested, revived crashes cut, the 3 s minimum, the end card label, Share Replay
//  - the wet street's reflection: on the street, mid-air, crash falls, never over walls
//  - the wall materials it emits (cyber cubes, neon crates: cool cyan edges, not amber)
//  - lite mode (low-end devices): no reflection, rain or wind streaks; remembered for the next launch
//  - the profiling frame summary, and no glow pad under the hen
#include "TestUtil.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

struct Env {
    FakeStorage st = settled();
    // a player who has already answered the replay / notification offers, so no panel covers the results
    static FakeStorage settled() {
        FakeStorage s = FakeStorage::ready();
        s.kv["cluckstack-profile"] = "aud=2;f_replay_asked=1;f_notif_asked=1;";
        return s;
    }
    FakeClock clock;
    FakeAds ads;
    FakeReplay replay;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.replay = &replay; return s; }
};

// a run long enough for a clip: the surge makes the hen invulnerable for its first seconds
void longRun(Game& g) {
    g.press();
    step(g, 0.1f);
    g.debugStartSurge();
    for (int i = 0; i < 60 * 60 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
}
// after a crash: let the tail play, then turn down any Continue
void endRun(Game& g) {
    step(g, Game::kCrashTail + 0.2f);
    if (g.screen() == Game::Screen::Continue) { g.tapButton("No thanks"); step(g, 0.1f); }
}

// ---------------------------------------------------------------- game side

void gameSide() {
    {   // ---- a run and its crash
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        step(g, 0.5f);
        check(!env.replay.capturing, "title: not recording");
        longRun(g);
        check(g.state() == Game::State::Dead && g.runSeconds() > Game::kMinReplaySeconds, "a long run, then a crash");
        check(env.replay.runStarts == 1 && env.replay.crashes == 1, "the run's start and its crash reach the recorder");
        check(env.replay.capturing, "the crash itself is recorded");
        step(g, Game::kCrashTail - 0.3f);
        check(env.replay.capturing && env.replay.saves == 0, "...for its whole tail, before any clip is requested");
        check(g.screen() != Game::Screen::Continue, "no panel during the crash's tail");
        endRun(g);
        check(!env.replay.capturing, "capture stops once the tail has played (no panel gets recorded)");
        check(env.replay.saves == 1, "one clip, requested once the run is over");
        check(env.replay.last.label == "LEVEL 1" && env.replay.last.distance > 0, "the end card: the level and the distance");
        check(env.replay.last.newBest, "a first run is a new best");
        check(!g.replayTooShort(), "a long run isn't too short");

        // the results: Share Replay only with a ready video, and it shares the video only
        step(g, 1.f);
        check(!g.tapButton("Share replay"), "no button while the clip isn't ready");
        env.replay.st = ReplayState::Exporting;
        step(g, 0.1f);
        check(!g.tapButton("Share replay"), "\"Saving\" can't be tapped");
        env.replay.st = ReplayState::Ready;
        step(g, 0.1f);
        check(g.tapButton("Share replay") && env.replay.shares == 1, "a ready clip: Share Replay shares it");
        env.replay.st = ReplayState::Failed;
        step(g, 0.1f);
        check(!g.tapButton("Share replay"), "a failed clip: no button");

        // the next run starts a new clip
        env.replay.st = ReplayState::Recording;
        g.press();
        step(g, 0.2f);
        check(env.replay.runStarts == 2 && env.replay.capturing, "the next run: a fresh clip, recording again");
    }
    {   // ---- too short for a replay
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        test::dieQuickly(g);
        check(g.runSeconds() < Game::kMinReplaySeconds, "a run under the minimum");
        endRun(g);
        check(env.replay.saves == 0 && g.replayTooShort(), "no clip for it; the results say it was too quick");
        step(g, 1.f);
        check(!g.tapButton("Share replay"), "...and there's no Share Replay");
        g.press();
        step(g, 0.1f);
        check(!g.replayTooShort(), "the next run starts fresh");
    }
    {   // ---- a revive cuts the crash out, and the clip is the whole run's
        Env env; // rewarded ads available: Continue offered
        Game g(env.svc());
        longRun(g);
        step(g, Game::kCrashTail + 0.2f);
        check(g.screen() == Game::Screen::Continue && !env.replay.capturing, "Continue panel: not recorded");
        check(env.replay.saves == 0, "a crash that can still be revived gets no clip of its own");
        g.tapButton("Watch ad");
        step(g, 0.2f);
        check(g.state() == Game::State::Play && env.replay.revives == 1, "revived: the crash is cut from the clip");
        check(env.replay.capturing && env.replay.runStarts == 1, "recording carries on in the same run");
        for (int i = 0; i < 60 * 60 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
        check(env.replay.crashes == 2, "the second crash is marked too");
        endRun(g);
        if (g.screen() == Game::Screen::Continue) { g.tapButton("No thanks"); step(g, 0.1f); }
        check(env.replay.saves == 1 && env.replay.revives == 1, "one clip for the run, ending on its final crash");
        check(!env.replay.capturing, "capture off on the results");
    }
    {   // ---- a level clear: the celebration is recorded, then the clip
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        g.debugNearFinish();
        g.press();
        g.debugStartSurge();
        int frames = 0;
        bool celebrating = false;
        for (; frames < 60 * 20 && g.state() == Game::State::Play; ++frames) {
            g.update(1.0 / 60.0);
            celebrating = celebrating || (env.replay.capturing && g.state() == Game::State::Play);
        }
        check(g.state() == Game::State::Dead && g.lastRunCleared(), "the finish line clears the level");
        check(celebrating && env.replay.crashes == 0, "the celebration is recorded (no crash)");
        g.update(1.0 / 60.0);
        check(env.replay.saves == 1 && env.replay.last.label == "LEVEL 1 CLEARED", "the clip right after it: \"LEVEL 1 CLEARED\"");
        check(!env.replay.capturing, "the Level Cleared screen isn't recorded");
    }
}

// ---------------------------------------------------------------- the wet street's reflection

size_t reflected(const Game& g) {
    size_t n = 0;
    for (const auto& v : g.renderList().buckets[size_t(Pass::Reflect)]) n += v.size();
    return n;
}

void streetReflection() {
    Env env;
    env.ads.state = RewardedState::Unavailable;
    Game g(env.svc());
    step(g, 0.5f);
    check(reflected(g) > 0, "reflection: the hen standing on the street (title screen)");
    g.press();
    step(g, 0.1f);
    g.debugStartSurge(); // invulnerable: she runs over walls, drops off them, lays eggs
    int onStreet = 0, airborne = 0, overWall = 0, missing = 0, extra = 0;
    for (int i = 0; i < 60 * 8 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        if (i % 5 == 0) g.press();
        const bool street = g.streetBelowHen(), shown = reflected(g) > 0;
        if (street) { ++onStreet; if (g.henHeight() > 1.f) ++airborne; if (!shown) ++missing; }
        else { ++overWall; if (shown) ++extra; }
    }
    check(onStreet > 0 && missing == 0, "reflection: always there while the street is below her");
    check(airborne > 0, "...including high above it (dropping off a wall, on a stack of eggs)");
    check(overWall == 0 || extra == 0, "reflection: none while she's over a wall (blocks aren't wet)");
    test::dieQuickly(g);
    step(g, 0.2f);
    check(g.state() == Game::State::Dead && (!g.streetBelowHen() || reflected(g) > 0), "reflection: her crash and fall, over the street");
}

// ---------------------------------------------------------------- wall materials

void wallMaterials() {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    ads.state = RewardedState::Unavailable;
    GameServices s;
    s.storage = &st; s.clock = &clock; s.ads = &ads;
    Game g(s);
    g.press();
    step(g, 0.1f);
    g.debugStartSurge(); // invulnerable: runs past walls of both kinds
    int cubes = 0, crates = 0;
    bool coolEdges = true;
    for (int i = 0; i < 60 * 6 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        for (const Instance& in : g.renderList().buckets[size_t(Pass::Lit)][size_t(MeshId::Box)]) {
            const int pattern = int(in.params.z + 0.5f);
            if (pattern != int(Pattern::Hazard) && pattern != int(Pattern::Container)) continue;
            (pattern == int(Pattern::Hazard) ? cubes : crates)++;
            coolEdges = coolEdges && in.rim.b > in.rim.r && in.rim.w <= 0.3f; // cyan, soft (was a strong amber)
        }
    }
    check(cubes > 0 && crates > 0, "walls: cyber cubes and neon crates are drawn");
    check(coolEdges, "walls: soft cyan edge glow on both kinds (no amber)");
}

// ---------------------------------------------------------------- lite mode (low-end devices): the game side

void liteEffects() {
    // the game side: lite turns the street reflection off and flags the frame for the shader
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    GameServices sv;
    sv.storage = &st; sv.clock = &clock; sv.ads = &ads;
    Game g(sv);
    step(g, 0.3f);
    size_t refl = 0;
    for (const auto& v : g.renderList().buckets[size_t(Pass::Reflect)]) refl += v.size();
    check(refl > 0 && !g.renderList().frame.lite, "lite off: the hen is reflected, full effects");
    g.setLite(true);
    step(g, 0.1f);
    refl = 0;
    for (const auto& v : g.renderList().buckets[size_t(Pass::Reflect)]) refl += v.size();
    check(refl == 0 && g.renderList().frame.lite, "lite on: no reflection, the shader is told to skip the animation");
    {   // the result is remembered: the next launch on this device starts lite, without a visible switch
        Game again(sv);
        check(again.lite() && again.renderList().frame.lite == false, "remembered: the next launch starts lite (before its first frame)");
        step(again, 0.1f);
        check(again.renderList().frame.lite, "...and draws lite from its first frame");
    }
    auto streaks = [&] { return g.renderList().buckets[size_t(Pass::UnlitAlpha)][size_t(MeshId::Plane)].size(); };
    const size_t liteStreaks = streaks();
    g.setLite(false);
    step(g, 0.1f);
    check(streaks() >= liteStreaks + 800, "lite on: no rain (900 streaks come back with full effects)");
    // wind streaks (speed lines) show once the run is fast: compare lite and full at speed
    auto windStreaks = [&] { return g.renderList().buckets[size_t(Pass::UnlitAdd)][size_t(MeshId::Plane)].size(); };
    g.press();
    g.debugStartSurge();
    step(g, 3.f);
    const size_t fullWind = windStreaks();
    g.setLite(true);
    step(g, 0.2f);
    check(fullWind >= windStreaks() + 40, "lite on: no wind streaks (42 at speed with full effects)");
}

// ---------------------------------------------------------------- frame summary, no glow pad

void frameSummaryAndPad() {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    ads.state = RewardedState::Unavailable;
    GameServices sv;
    sv.storage = &st; sv.clock = &clock; sv.ads = &ads;
    Game g(sv);
    step(g, 0.3f);
    const std::string title = g.frameSummary();
    check(title.rfind("title", 0) == 0 && title.find("cubes ") != std::string::npos && title.find("sparks ") != std::string::npos,
          "summary: the state and what's drawn (title screen)");
    // no glow pad: no ground-flat radial glow under the hen
    auto pads = [&] {
        int n = 0;
        for (const Instance& in : g.renderList().buckets[size_t(Pass::UnlitAdd)][size_t(MeshId::Plane)]) {
            const glm::vec4 normal = in.model * glm::vec4(0, 0, 1, 0), pos = in.model * glm::vec4(0, 0, 0, 1);
            const bool flat = std::abs(normal.y) > 0.9f * glm::length(glm::vec3(normal));
            n += int(in.params.z + 0.5f) == int(Shape::RadialGlow) && flat && pos.y < 1.5f;
        }
        return n;
    };
    check(pads() == 0, "no glow pad under the hen (title)");
    g.press();
    g.debugStartSurge();
    step(g, 1.f);
    const std::string play = g.frameSummary();
    check(play.rfind("play SURGE", 0) == 0, "summary: a run, with its surge flagged");
    check(pads() == 0, "no glow pad under the hen (in a run)");
    g.setLite(true);
    step(g, 0.1f);
    check(g.frameSummary().find(" lite") != std::string::npos, "summary: lite mode flagged");
}

} // namespace

int main() {
    gameSide();
    streetReflection();
    wallMaterials();
    liteEffects();
    frameSummaryAndPad();
    return finish("Game");
}
