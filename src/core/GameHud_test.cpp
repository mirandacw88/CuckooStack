// The in-game HUD (GameHud.cpp), read back through the strings it draws: the score and level line in a run, the title
// header (level, logo, live-event banner) and its note, the results overlay after a crash or a clear, and popups.
// (The OVERDRIVE line, the timer and the Level Complete banner: tests/polish_test.cpp and Campaign_test.cpp.)
#include "TestUtil.h"
#include "core/HudStyle.h"

#include <map>

using namespace cs;
using namespace cs::hud;
using namespace test;

namespace {

struct FakeRemote : IRemoteConfig {
    std::map<std::string, double> v;
    std::optional<double> number(const std::string& k) override { auto it = v.find(k); if (it == v.end()) return std::nullopt; return it->second; }
};
struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    FakeRemote rc;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.remoteConfig = &rc; return s; }
};
const Game::HudString* find(const Game& g, const std::string& text) {
    for (const auto& h : g.hudStrings()) if (h.text == text) return &h;
    return nullptr;
}
// (notes are drawn in capitals: compared without case)
bool startsWith(const Game& g, const std::string& prefix) {
    for (const auto& h : g.hudStrings()) if (toUpperAscii(h.text).rfind(toUpperAscii(prefix), 0) == 0) return true;
    return false;
}

void title() {
    Env env;
    Game g(env.svc());
    g.resize(393, 852);
    g.debugRecordHud(true);
    step(g, 1.5f);
    check(find(g, "Level 1") != nullptr, "title: the level tag");
    check(find(g, "Cuckoo") || find(g, "Cuckoo Stack"), "title: the logo");
    check(startsWith(g, "Reach the finish line 300 m away"), "title: a first-timer is told what to do (finish 300 m away)");
    check(!startsWith(g, "Level 1 · disco balls"), "title: no in-run HUD");
    check(!startsWith(g, "2x coins event"), "no live event: no banner");
    check(find(g, "Tap to jack in") != nullptr, "title: the start pill");
}

void liveEvent() {
    Env env;
    env.rc.v["event_coin_mult"] = 2;
    Game g(env.svc());
    g.resize(393, 852);
    g.debugRecordHud(true);
    step(g, 1.5f);
    check(find(g, "2x coins event") != nullptr, "a double-coins live event: its banner under the logo");
}

void run() {
    Env env;
    env.ads.state = RewardedState::Unavailable;
    Game g(env.svc());
    g.resize(393, 852);
    g.debugRecordHud(true);
    g.press();
    bool scoreShown = true, levelLine = false, bar = true;
    for (int i = 0; i < 60 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        const Game::HudString* s = find(g, std::to_string(g.score()));
        scoreShown = scoreShown && s && s->style.font == FontId::Display;
        levelLine = levelLine || find(g, "Level 1 · disco balls 0/10");
        bar = bar && g.progressBarBottom() > 0;
    }
    check(scoreShown, "in a run: the score, big, every frame");
    check(levelLine, "in a run: the level and the disco-ball count toward overdrive");
    check(bar, "in a run: the progress bar toward the finish");
    check(!find(g, "Tap to jack in"), "in a run: no title pill");
    g.debugStartSurge();
    float biggest = 0;
    bool white = true;
    for (int i = 0; i < 40; ++i) {
        g.update(1.0 / 60.0);
        if (const Game::HudString* pop = find(g, "OVERDRIVE!")) { biggest = std::max(biggest, pop->style.size); white = white && pop->style.color == kText; }
    }
    check(biggest > 56 && biggest <= 56 * 1.2f + 0.01f && white, "the overdrive popup: big and white, popping up to 120% then settling");
}

void results() {
    {
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        g.resize(393, 852);
        g.debugRecordHud(true);
        test::dieQuickly(g);
        step(g, Game::kCrashTail + 2.f);
        check(g.state() == Game::State::Dead && find(g, "Tap to reboot") != nullptr, "after a crash: Tap to reboot");
        check(!startsWith(g, "Level 1 · disco balls"), "the run's HUD is gone from the results");
        step(g, 0.5f);
        g.press();
        step(g, 0.2f);
        check(g.state() == Game::State::Play, "tap: the next run");
        test::dieQuickly(g);
        step(g, Game::kCrashTail + 2.f);
        g.debugRecordHud(true);
        Game again(env.svc());
        again.resize(393, 852);
        again.debugRecordHud(true);
        step(again, 1.5f);
        check(startsWith(again, "Your best here"), "back on the title: your best on this level and your attempts");
    }
    {
        Env env;
        env.ads.state = RewardedState::Unavailable;
        Game g(env.svc());
        g.resize(393, 852);
        g.debugRecordHud(true);
        g.debugNearFinish();
        g.press();
        g.debugStartSurge();
        for (int i = 0; i < 60 * 12 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
        step(g, 2.f);
        check(g.lastRunCleared() && find(g, "Tap for level 2") != nullptr, "after a clear: Tap for level 2");
    }
}

} // namespace

int main() {
    test::useFixtures(); // (the levels and gauntlets these tests are written round: tests/fixtures)
    title();
    liveEvent();
    run();
    results();
    return finish("GameHud");
}
