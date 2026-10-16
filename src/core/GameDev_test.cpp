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
    // grouped in sections, each under its header: every button sits below its own section's header and above the next
    const char* kSections[] = {"Bot", "Next run", "Level & rank", "Streak & daily drop", "Missions", "Ads & audience", "Show a screen"};
    std::vector<float> heads;
    for (const char* h : kSections) { // (1e9: past the fold, not drawn)
        float y = 1e9f;
        for (const auto& t : g.hudStrings()) if (t.text == h) y = t.top;
        heads.push_back(y);
    }
    check(heads[0] < 1e8f && heads[1] < 1e8f && heads[0] < heads[1], "the Game tab is grouped: Bot, then Next run, ...");
    auto under = [&](const std::string& button, int section) {
        for (const auto& t : g.hudStrings())
            if (t.text.rfind(button, 0) == 0)
                return t.top > heads[size_t(section)] && (section + 1 >= int(heads.size()) || heads[size_t(section) + 1] > 1e8f || t.top < heads[size_t(section) + 1]);
        return false;
    };
    check(under("Bot sloppy", 0) && under("Next run: droid raids", 1) && under("Free Overdrive Start", 1), "...each button under its own section");


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

void touchPhases() {
    // Game::touch, the platforms' entry point (iOS touches, Android motion events, the desktop mouse)
    Env env;
    Game g(env.svc());
    openGameTab(g);
    const auto bot = button(g, "Bot plays");
    const glm::vec2 p = bot->c * kScale;
    g.touch(TouchPhase::Began, p.x, p.y);
    g.touch(TouchPhase::Ended, p.x, p.y);
    check(g.botOptions().on, "touch: Began then Ended on a button taps it");
    g.touch(TouchPhase::Began, p.x, p.y);
    for (int i = 1; i <= 10; ++i) { g.touch(TouchPhase::Moved, p.x, p.y - 30.f * i); g.update(1.0 / 60.0); }
    g.touch(TouchPhase::Ended, p.x, p.y - 300.f);
    check(g.devScroll() > 150.f && g.botOptions().on, "...Moved drags the list (and doesn't press the button under the finger)");
    step(g, 1.5f);
    const float settled = g.devScroll();
    g.touch(TouchPhase::Began, p.x, 500.f * kScale);
    for (int i = 1; i <= 4; ++i) { g.touch(TouchPhase::Moved, p.x, (500.f + 40.f * i) * kScale); g.update(1.0 / 60.0); }
    g.touch(TouchPhase::Cancelled, p.x, 660.f * kScale);
    step(g, 1.5f);
    check(g.devScroll() < settled - 50.f, "...Cancelled lets go like Ended (the system took the gesture: no stuck finger)");
    g.scrollBy(-100000);
    step(g, 0.2f);
    const auto again = button(g, "Bot plays");
    g.touch(TouchPhase::Began, again->c.x * kScale, again->c.y * kScale);
    g.touch(TouchPhase::Cancelled, again->c.x * kScale, again->c.y * kScale);
    check(g.botOptions().on, "...and a cancelled touch on a button doesn't press it (an incoming call mid-tap)");
    g.touch(TouchPhase::Began, again->c.x * kScale, again->c.y * kScale);
    g.touch(TouchPhase::Ended, again->c.x * kScale, again->c.y * kScale);
    check(!g.botOptions().on, "...the next tap works normally");
    check(wheelToScroll(1.0) == -40.f && wheelToScroll(-2.0) == 80.f, "the mouse wheel: a notch up scrolls 40 points toward the top");
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

void botSloppy() {
    // sloppy: now and then one egg short for a wall. Same seeds: the careful bot clears every run, the sloppy one
    // crashes in some (but not right away: it's only short sometimes)
    int carefulClears = 0, sloppyClears = 0;
    float sloppyFar = 0;
    for (int seed = 1; seed <= 6; ++seed)
        for (bool sloppy : {false, true}) {
            Env env;
            env.ads.state = RewardedState::Unavailable;
            Game g(env.svc());
            g.debugSeed(uint64_t(seed));
            g.debugSetBot({true, false, sloppy, 0});
            g.press();
            float x = 0;
            for (int i = 0; i < 60 * 90 && g.state() == Game::State::Play; ++i) { g.update(1.0 / 60.0); x = henX(g); }
            (sloppy ? sloppyClears : carefulClears) += g.lastRunCleared();
            if (sloppy) sloppyFar += x / 6;
        }
    check(carefulClears == 6, "the careful bot clears level 1 every time");
    check(sloppyClears < 6, "sloppy: it comes up short and crashes in some runs");
    check(sloppyFar > 40, "...but only now and then (it gets well into the level on average)");
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

void devButtons() {
    {   // "Next run: droid raids": the next run's raids come every ~35 m from 20 m (on any level), once
        Env env;
        Game g(env.svc());
        openGameTab(g);
        check(g.tapButton("dev:Next run: droid raids") && (step(g, 0.1f), drew(g, "Droid raids: armed")), "Next run: droid raids: arms it");
        g.tapButton("Close");
        step(g, 0.3f);
        g.press();
        const std::vector<double> plan = g.droidRaidPlan();
        check(g.state() == Game::State::Play && plan.size() > 3 && std::abs(plan[0] - 20) < 1e-6 && std::abs(plan[1] - plan[0] - 35) < 1e-6,
              "...that run's raids come every 35 m from 20 m (level 1 has none otherwise)");
        test::dieQuickly(g);
        for (int i = 0; i < 60 * 8 && g.state() != Game::State::Play; ++i) {
            if (g.screen() == Game::Screen::Continue) g.tapButton("No thanks"); // (a new run, not a revive)
            else if (i % 30 == 0) g.press();
            g.update(1.0 / 60.0);
        }
        check(g.state() == Game::State::Play && g.droidRaidPlan().empty(), "...for one run only");
    }
    {   // "Daily drop day 1": the drop back to day 1, claimable
        Env env;
        env.st.kv["cluckstack-drop"] = "claims=4;last=2026-10-08;"; // day 5, claimed today
        Game g(env.svc());
        openGameTab(g);
        g.scrollBy(100000);
        step(g, 0.1f);
        check(g.tapButton("dev:Daily drop day 1"), "Daily drop day 1");
        g.debugOpenScreen("drop");
        step(g, 1.f);
        const int before = g.coins();
        check(g.tapButton("Claim") && (step(g, 0.3f), g.coins() == before + 20), "...the drop is claimable again, at day 1 (20 coins)");
    }
}

void showScreens() {
    // every pop-up can be opened from the Game tab's "Show a screen" section (at the bottom of the list)
    const std::pair<const char*, Game::Screen> kShow[] = {
        {"Show rank-up", Game::Screen::LevelUp}, {"Show starter offer", Game::Screen::Starter}, {"Show streak save", Game::Screen::StreakSave},
        {"Show daily drop", Game::Screen::DailyDrop}, {"Show age gate", Game::Screen::AgeGate}, {"Show notif primer", Game::Screen::NotifPrimer},
        {"Show replay primer", Game::Screen::ReplayPrimer}, {"Show shop", Game::Screen::Shop}, {"Show locker", Game::Screen::Locker},
        {"Show missions", Game::Screen::Missions}};
    for (const auto& [label, screen] : kShow) {
        Env env;
        Game g(env.svc());
        openGameTab(g);
        g.scrollBy(100000);
        step(g, 0.1f);
        const bool tapped = g.tapButton(std::string("dev:") + label);
        step(g, 0.3f);
        check(tapped && g.screen() == screen, (std::string(label) + ": opens it").c_str());
    }
    {   // Continue belongs to a crash: the button starts a run and crashes it, and the offer comes up as in play
        Env env;
        Game g(env.svc()); // (an ad is ready, so a continue can be offered)
        openGameTab(g);
        g.scrollBy(100000);
        step(g, 0.1f);
        check(g.tapButton("dev:Show continue"), "Show continue");
        step(g, 0.3f);
        check(g.screen() == Game::Screen::None && g.state() == Game::State::Play, "...starts a run");
        for (int i = 0; i < 60 * 4 && g.screen() != Game::Screen::Continue; ++i) g.update(1.0 / 60.0);
        check(g.state() == Game::State::Dead && g.screen() == Game::Screen::Continue, "...crashes it, and the Continue offer comes up");
        step(g, 0.5f);
        const bool watched = g.tapButton("Watch ad");
        for (int i = 0; i < 60 * 3 && g.state() != Game::State::Play; ++i) g.update(1.0 / 60.0);
        check(watched && env.ads.rewardedShows == 1 && g.state() == Game::State::Play, "...whose buttons work as in play (an ad revives the run)");
    }
}

} // namespace

int main() {
    scrolls();
    botPlays();
    botSpareEggs();
    botSloppy();
    botKeepsGoing();
    showScreens();
    devButtons();
    touchPhases();
    return finish("GameDev");
}
