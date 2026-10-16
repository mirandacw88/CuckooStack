// The developer menu's scrolling list and its bot (GameBot.cpp). The list: a drag scrolls it (and doesn't press the
// button under the finger), a tap still presses, a fling coasts on, an end pulls back like a rubber band, the wheel
// scrolls, and items past the edges can't be pressed. The bot: it plays a run to the finish on its own, keeps two spare
// eggs when asked, and with "keeps going" declines the continue and starts the next run.
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/Game.h"

#include <algorithm>
#include <optional>

using namespace cs;
using namespace test;

namespace {

struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; return s; }
};

// a small phone: the HUD is drawn at 667/780 of its points
constexpr float kW = 375, kH = 667, kScale = kH / 780.f;

std::optional<Game::Button> button(const Game& g, const std::string& label) {
    for (const auto& b : g.buttons()) if (b.label.rfind("dev:" + label, 0) == 0) return b;
    return std::nullopt;
}
bool drew(const Game& g, const std::string& text) {
    for (const auto& h : g.hudStrings()) if (h.text.rfind(text, 0) == 0) return true;
    return false;
}

void openGameTab(Game& g) {
    g.resize(kW, kH);
    g.debugRecordHud(true);
    step(g, 0.2f);
    g.debugOpenScreen("dev");
    step(g, 0.5f);
    g.tapButton("dev:Game");
    step(g, 0.1f);
}

// a finger: down, moved in steps (one frame each), up
void drag(Game& g, glm::vec2 from, float dy, int steps = 10) {
    g.pressAt(from.x * kScale, from.y * kScale);
    for (int i = 1; i <= steps; ++i) { g.dragTo(from.x * kScale, (from.y + dy * float(i) / steps) * kScale); g.update(1.0 / 60.0); }
    g.release(from.x * kScale, (from.y + dy) * kScale);
}

void scrolls() {
    Env env;
    Game g(env.svc());
    openGameTab(g);
    check(g.screen() == Game::Screen::Dev && button(g, "Bot plays") && button(g, "Rank +1"), "the developer menu's Game tab: the bot options first");
    check(!button(g, "Show age gate").has_value(), "...its last buttons are below the fold (not pressable)");
    check(drew(g, "Bot plays: off") && drew(g, "Bot keeps going") && drew(g, "Bot spare eggs") && drew(g, "Bot sloppy"), "...all four bot options");

    // a drag over a button scrolls the list and doesn't press it
    const auto rank = button(g, "Bot plays"); // (a drag that starts on a button)
    const glm::vec2 at = rank->c;
    drag(g, at, -300);
    check(g.devScroll() > 200.f, "dragging up scrolls the list");
    check(!g.botOptions().on, "...without pressing the button the finger started on");
    step(g, 1.5f);
    const float settled = g.devScroll();
    check(button(g, "Show age gate").has_value(), "...and brings the last buttons up, pressable");

    // a fling coasts on; past the end it pulls back
    drag(g, {kW / 2, 500}, -260, 4);
    const float atRelease = g.devScroll();
    g.update(1.0 / 60.0);
    g.update(1.0 / 60.0);
    const float coasting = g.devScroll();
    step(g, 1.5f);
    check(coasting != atRelease, "a quick flick coasts on after the finger lifts");
    check(g.devScroll() >= settled - 1.f && g.devScroll() < settled + 1.f, "...and past the end, it springs back to the end");
    g.scrollBy(-5000); // (at the top)
    drag(g, {kW / 2, 300}, 240);
    check(g.devScroll() < -40.f && g.devScroll() > -120.f, "pulled past the top: it gives, a third as far as the finger moved");
    step(g, 1.5f);
    check(std::abs(g.devScroll()) < 1.f, "...and springs back to the top");

    // a tap still presses (on release, the finger still)
    const auto bot = button(g, "Bot plays");
    g.pressAt(bot->c.x * kScale, bot->c.y * kScale);
    check(!g.botOptions().on, "pressing a button in the list waits...");
    g.release(bot->c.x * kScale, bot->c.y * kScale);
    check(g.botOptions().on, "...and a tap (no drag) presses it on release");
    step(g, 0.1f);
    check(drew(g, "Bot plays: on"), "...the button says so");

    // a tap on the list while it's still moving only stops it
    const auto bot2 = button(g, "Bot plays");
    drag(g, {kW / 2, 300}, 200, 3); // (pulled past the top: it springs back)
    g.pressAt(bot2->c.x * kScale, bot2->c.y * kScale);
    g.release(bot2->c.x * kScale, bot2->c.y * kScale);
    check(g.botOptions().on, "tapping the list while it springs back only stops it (no press)");
    step(g, 1.f);
    check(std::abs(g.devScroll()) < 1.f, "...and it settles back");

    // the mouse wheel
    g.scrollBy(120);
    step(g, 0.1f);
    check(std::abs(g.devScroll() - 120.f) < 1.f, "the mouse wheel scrolls it");
    g.scrollBy(-5000);
    check(g.devScroll() == 0.f, "...but not past the top");

    // another tab starts at the top
    g.scrollBy(120);
    g.tapButton("dev:Coins");
    step(g, 0.1f);
    g.tapButton("dev:Game");
    step(g, 0.1f);
    check(g.screen() == Game::Screen::Dev, "(still in the menu)");
    g.debugOpenScreen("settings");
    step(g, 0.1f);
    check(g.devScroll() == 0.f, "a new screen starts unscrolled");
}

float henX(const Game& g) { return g.hen().nodes[size_t(g.hen().root)].pos.x; }

void botPlays() {
    Env env;
    env.ads.state = RewardedState::Unavailable;
    Game g(env.svc());
    g.debugSetBot({true, false, false, 0});
    g.press(); // a run; the bot does the rest
    for (int i = 0; i < 60 * 90 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
    check(g.state() == Game::State::Dead && g.lastRunCleared(), "the bot plays level 1 to the finish on its own");
    check(Campaign(&env.st).cleared() == 1, "...and the clear counts");
}

void botSpareEggs() {
    Env env;
    env.ads.state = RewardedState::Unavailable;
    Game g(env.svc());
    g.debugSetBot({true, false, false, 2});
    g.press();
    int most = 0, raids = 0;
    bool was = false;
    for (int i = 0; i < 60 * 40 && g.state() == Game::State::Play; ++i) {
        g.update(1.0 / 60.0);
        most = std::max(most, g.eggCount());
        raids += g.droidRaiding() && !was;
        was = g.droidRaiding();
    }
    check(most >= 3 && g.state() == Game::State::Play, "with spare eggs it carries more (and stays alive)");
}

void botKeepsGoing() {
    Env env;
    env.ads.state = RewardedState::Unavailable;
    Game g(env.svc());
    g.debugSetBot({true, true, true, 0}); // sloppy: it'll crash sooner or later
    g.press();
    int runs = 1;
    Game::State prev = g.state();
    for (int i = 0; i < 60 * 240 && runs < 3; ++i) {
        g.update(1.0 / 60.0);
        if (g.state() == Game::State::Play && prev != Game::State::Play) ++runs;
        prev = g.state();
    }
    check(runs >= 3, "keeps going: after a crash (or a clear) it starts the next run by itself");
}

} // namespace

int main() {
    scrolls();
    botPlays();
    botSpareEggs();
    botKeepsGoing();
    return finish("GameDev");
}
