// The game's screens (GameUi.cpp): every panel opens and draws at phone sizes from the smallest iPhone up, keeps its
// buttons on screen, closes, and does what its buttons say. Settings toggles, the parental gate in front of under-13
// purchases (and its maths question), the boost tiles' coins / ad choice, and the staging-only developer menu.
#include "TestUtil.h"
#include "core/HudIcons.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

using namespace cs;
using namespace test;

namespace {

struct Env {
    FakeStorage st = FakeStorage::ready();
    FakeClock clock;
    FakeAds ads;
    FakeStore store;
    GameServices svc() { GameServices s; s.storage = &st; s.clock = &clock; s.ads = &ads; s.store = &store; return s; }
};
bool drew(const Game& g, const std::string& text) {
    for (const auto& h : g.hudStrings()) if (h.text == text) return true;
    return false;
}

const std::pair<const char*, Game::Screen> kScreens[] = {
    {"shop", Game::Screen::Shop}, {"locker", Game::Screen::Locker}, {"missions", Game::Screen::Missions}, {"drop", Game::Screen::DailyDrop},
    {"settings", Game::Screen::Settings}, {"dev", Game::Screen::Dev}, {"levelup", Game::Screen::LevelUp}, {"streak", Game::Screen::StreakSave},
    {"gate", Game::Screen::ParentalGate}, {"notif", Game::Screen::NotifPrimer}, {"replay", Game::Screen::ReplayPrimer},
    {"starter", Game::Screen::Starter}};

// a panel is modal: the buttons of the screen underneath can't be tapped through its dimmed backdrop
void closeAll(Game& g) {
    for (int i = 0; i < 8 && g.screen() != Game::Screen::None; ++i) { if (!g.tapButton("Close")) break; step(g, 0.05f); }
}

void modal() {
    Env env;
    Game g(env.svc());
    g.resize(393, 852);
    step(g, 1.5f); // the title's tiles have risen in
    glm::vec2 tile{-1};
    for (const auto& b : g.buttons()) if (b.label == "Overclock") tile = b.c;
    for (const char* name : {"settings", "missions", "gate", "drop", "levelup", "streak", "notif"}) {
        g.debugOpenScreen(name);
        step(g, 0.5f);
        const Game::Screen open = g.screen();
        bool covered = false;
        for (const auto& b : g.buttons()) covered = covered || (b.label != "Overclock" && std::abs(tile.x - b.c.x) <= b.half.x && std::abs(tile.y - b.c.y) <= b.half.y);
        if (!covered) g.pressAt(tile.x, tile.y);
        step(g, 0.2f);
        check(tile.x >= 0 && env.ads.rewardedShows == 0 && g.screen() == open,
              (std::string(name) + ": tapping the Overclock tile behind the panel does nothing").c_str());
        closeAll(g);
        step(g, 0.3f);
    }
}

void screens() {
    std::map<std::string, std::string> problems;
    int closable = 0;
    for (auto [w, h] : {std::pair{320.f, 568.f}, {375.f, 667.f}, {393.f, 852.f}, {440.f, 956.f}}) {
        Env env;
        Game g(env.svc());
        g.resize(w, h);
        g.debugRecordHud(true);
        step(g, 0.2f);
        for (const auto& [name, screen] : kScreens) {
            g.debugOpenScreen(name);
            step(g, 0.6f); // past the entrance animation
            const std::string at = std::string(name) + " @" + std::to_string(int(w)) + "x" + std::to_string(int(h));
            if (g.screen() != screen) { problems[at] = "didn't open"; closeAll(g); continue; }
            if (g.hudStrings().size() < 2) problems[at] = "drew no text";
            const float W = g.hudSize().x, H = g.hudSize().y; // HUD points
            for (const Game::Button& b : g.buttons()) {
                // full-screen tap areas (tap outside to dismiss) are allowed to fill the screen
                if (b.half.x >= W / 2 - 1 && b.half.y >= H / 2 - 1) continue;
                // the visible button must be on screen (a tap area may overhang the edge by a few points)
                if (b.c.x < 0 || b.c.x > W || b.c.y < 0 || b.c.y > H || b.c.x - b.half.x < -12 || b.c.x + b.half.x > W + 12 ||
                    b.c.y - b.half.y < -12 || b.c.y + b.half.y > H + 12)
                    problems[at] += "'" + b.label + "' c=(" + std::to_string(int(b.c.x)) + "," + std::to_string(int(b.c.y)) + ")  ";
            }
            if (g.tapButton("Close")) {
                step(g, 0.05f);
                if (g.screen() == screen) problems[at] = "Close didn't close it";
                else if (w == 393.f) closable++;
            }
            closeAll(g);
        }
    }
    for (const auto& [at, why] : problems) std::printf("  %s: %s\n", at.c_str(), why.c_str());
    check(problems.empty(), "every screen opens, draws, and keeps its buttons on screen (320x568 to 440x956)");
    check(closable >= 8, "panels close with their Close button");
}

void settings() {
    Env env;
    Game g(env.svc());
    g.resize(393, 852);
    g.debugRecordHud(true);
    step(g, 0.2f);
    check(g.tapButton("Settings"), "the settings button on the title screen");
    step(g, 0.5f);
    check(g.screen() == Game::Screen::Settings && drew(g, "Music: on"), "settings: music on");
    check(g.tapButton("Music"), "tap Music");
    step(g, 0.1f);
    check(g.muted() && drew(g, "Music: off"), "...turns it off (and the row says so)");
    check(!g.buttons().empty() && g.tapButton("Restore purchases"), "settings: Restore purchases");
    step(g, 0.1f);
    bool dev = false;
    for (const auto& b : g.buttons()) dev = dev || b.label.rfind("Developer", 0) == 0 || b.label == "Developer";
    check(drew(g, "Developer"), "staging build: the developer menu is offered");
    check(g.tapButton("Developer") && (step(g, 0.3f), g.screen() == Game::Screen::Dev), "...and opens");
    (void)dev;
}

// the gate's question "A × B = ?" and its four answer buttons (labelled with their numbers)
int gateProduct(const Game& g) {
    for (const auto& h : g.hudStrings())
        if (h.text.find(" × ") != std::string::npos && h.text.find(" = ?") != std::string::npos)
            return std::atoi(h.text.c_str()) * std::atoi(h.text.c_str() + h.text.find(" × ") + std::strlen(" × "));
    return 0;
}
std::vector<int> gateChoices(const Game& g) {
    std::vector<int> out;
    for (const auto& b : g.buttons())
        if (!b.label.empty() && b.label.find_first_not_of("0123456789") == std::string::npos) out.push_back(std::atoi(b.label.c_str()));
    return out;
}

void parentalGate() {
    {   // a child: buying goes through the gate
        Env env;
        env.st.kv["cluckstack-profile"] = "aud=1;";
        Game g(env.svc());
        g.resize(393, 852);
        g.debugRecordHud(true);
        step(g, 0.3f);
        g.debugOpenScreen("shop");
        step(g, 0.5f);
        check(g.tapButton("remove_ads") || g.tapButton("starter_pack"), "child: a product in the shop");
        step(g, 0.5f);
        check(g.screen() == Game::Screen::ParentalGate && env.store.bought.empty(), "child: a purchase opens the parental gate first");
        const int answer = gateProduct(g);
        const std::vector<int> choices = gateChoices(g);
        check(answer >= 42 && choices.size() == 4 && std::count(choices.begin(), choices.end(), answer) == 1,
              "the gate asks a multiplication with its answer among four choices");
        int wrong = 0;
        for (int c : choices) if (c != answer) wrong = c;
        g.tapButton(std::to_string(wrong));
        step(g, 0.2f);
        check(env.store.bought.empty() && g.screen() != Game::Screen::ParentalGate, "a wrong answer: no purchase");
        g.debugOpenScreen("shop");
        step(g, 0.3f);
        g.tapButton("remove_ads") || g.tapButton("starter_pack");
        step(g, 0.5f);
        g.tapButton(std::to_string(gateProduct(g)));
        step(g, 0.2f);
        check(env.store.bought.size() == 1, "the right answer: the purchase goes ahead");
    }
    {   // 13+: no gate
        Env env;
        Game g(env.svc());
        g.resize(393, 852);
        step(g, 0.3f);
        g.debugOpenScreen("shop");
        step(g, 0.5f);
        g.tapButton("remove_ads") || g.tapButton("starter_pack");
        step(g, 0.2f);
        check(env.store.bought.size() == 1 && g.screen() != Game::Screen::ParentalGate, "13+: straight to the store");
    }
    {   // the gate's wrong answers: never the right one, never twice the same
        bool distinct = true, positive = true;
        Env env;
        Game g(env.svc());
        g.resize(393, 852);
        g.debugRecordHud(true);
        step(g, 0.05f);
        for (int i = 0; i < 150; ++i) {
            g.debugOpenScreen("gate"); // a fresh question each time
            g.update(1.0 / 60.0);
            if (i == 0) step(g, 0.5f);
            std::vector<int> nums = gateChoices(g);
            std::sort(nums.begin(), nums.end());
            distinct = distinct && nums.size() == 4 && std::adjacent_find(nums.begin(), nums.end()) == nums.end();
            for (int n : nums) positive = positive && n > 0;
        }
        check(distinct, "the gate's four choices are always four different numbers");
        check(positive, "all positive");
    }
}

void boostTiles() {
    Env env;
    Game g(env.svc());
    g.resize(393, 852);
    g.debugRecordHud(true);
    step(g, 0.5f);
    check(drew(g, "Free"), "with an ad available, the boost tiles are free (watch an ad)");
    const int coins = g.coins();
    check(g.tapButton("Overclock"), "tap the Overclock tile");
    step(g, 0.3f);
    check(env.ads.rewardedShows == 1 && g.coins() == coins && drew(g, "Ready"), "free: an ad, then the boost is armed (no coins spent)");
    Env poor;
    poor.ads.state = RewardedState::Unavailable;
    Game p(poor.svc());
    p.resize(393, 852);
    p.debugAddCoins(500);
    step(p, 0.5f);
    const int before = p.coins();
    check(p.tapButton("Overdrive start"), "no ad: tap the Overdrive start tile");
    step(p, 0.3f);
    check(p.coins() == before - 150, "no ad: the boost costs 150 coins");
}

// the HUD atlas rect a sprite instance samples (its UV rect, in atlas cells)
int spriteCell(const Instance& in) { return int(in.emissive.y * kIconRows + 1e-3f) * kIconCols + int(in.emissive.x * kIconCols + 1e-3f); }
int sprites(const Game& g, Icon icon) { // instances drawn from that sprite's cell(s)
    const int first = int(icon), last = first + iconSpan(icon) - 1;
    int n = 0;
    const RenderList& l = g.renderList();
    for (size_t i = 0; i < l.hud.size(); ++i)
        if (l.hudKind[i] == HudSprite) { const int c = spriteCell(l.hud[i]); n += c >= first && c <= last; }
    return n;
}

void dailyDrop() {
    // the artist's Daily Drop (assets/hud/source/daily_drop_mockup.jpg): its frame, seven day tiles in their states,
    // coins and the day 7 chest, CLAIM and the ad's CLAIM X2
    {
        Env env;
        env.st.kv["cluckstack-drop"] = "claims=0;last=2026-10-07;"; // day 1, claimable
        Game g(env.svc());
        g.resize(430, 932);
        g.debugRecordHud(true);
        step(g, 0.2f);
        g.debugOpenScreen("drop");
        step(g, 1.f);
        check(g.screen() == Game::Screen::DailyDrop && drew(g, "Your reward"), "Daily Drop: opens on \"Your reward\"");
        bool days = true;
        for (int d = 1; d <= 7; ++d) days = days && drew(g, "Day " + std::to_string(d));
        check(days && drew(g, "20") && drew(g, "250"), "...seven days, labelled, with their coins");
        check(sprites(g, Icon::DropFrame) == 9, "...in the artist's tech frame (9-sliced)");
        check(sprites(g, Icon::DropTileGreen) == 1 && sprites(g, Icon::DropTilePurple) == 1 && sprites(g, Icon::DropTileBlue) == 4 &&
                  sprites(g, Icon::DropTilePink) == 1,
              "...today green, tomorrow purple, day 7 pink, the rest blue");
        check(sprites(g, Icon::DropCoin) == 6 && sprites(g, Icon::DropChest) == 1, "...a coin on days 1-6, the chest on day 7");
        check(sprites(g, Icon::DropClaim) == 3 && sprites(g, Icon::DropClaim2) == 3, "...CLAIM and CLAIM X2 (3-sliced) side by side");
        bool onScreen = true;
        for (const auto& b : g.buttons())
            onScreen = onScreen && b.c.x - b.half.x >= 0 && b.c.x + b.half.x <= 430 && b.c.y - b.half.y >= 0 && b.c.y + b.half.y <= 932;
        check(onScreen, "...every button on screen");
        // the tiles breathe and the coins bob: the frame differs a moment later
        const std::vector<Instance> a = g.renderList().hud;
        step(g, 0.3f);
        bool moved = false;
        const std::vector<Instance>& b = g.renderList().hud;
        for (size_t i = 0; i < std::min(a.size(), b.size()) && !moved; ++i) moved = a[i].model != b[i].model;
        check(moved, "...and it's animated");
        const int before = g.coins();
        check(g.tapButton("Claim"), "CLAIM");
        step(g, 0.5f);
        check(g.coins() == before + 20 && drew(g, "Collected!") && drew(g, "Done"), "...collects day 1's 20 coins: \"Collected!\", and Done");
        check(sprites(g, Icon::Check) >= 1, "...its tile stamped with a tick");
        check(g.tapButton("Done") && (step(g, 0.3f), g.screen() == Game::Screen::None), "Done closes it");
    }
    {   // CLAIM X2: through a rewarded ad, double the coins
        Env env;
        env.st.kv["cluckstack-drop"] = "claims=6;last=2026-10-07;"; // day 7
        Game g(env.svc());
        g.resize(430, 932);
        g.debugRecordHud(true);
        step(g, 0.2f);
        g.debugOpenScreen("drop");
        step(g, 1.f);
        check(sprites(g, Icon::DropTileGreen) == 0 && sprites(g, Icon::DropTilePink) == 1, "day 7 is today: its tile stays pink (the chest's)");
        const int before = g.coins();
        check(g.tapButton("x2 drop"), "CLAIM X2");
        step(g, 0.5f);
        check(env.ads.rewardedShows == 1 && g.coins() == before + 500, "...plays the ad, then pays day 7's 250 twice");
    }
    {   // no ad ready: one CLAIM, centred
        Env env;
        env.ads.state = RewardedState::Unavailable;
        env.st.kv["cluckstack-drop"] = "claims=0;last=2026-10-07;";
        Game g(env.svc());
        g.resize(430, 932);
        step(g, 0.2f);
        g.debugOpenScreen("drop");
        step(g, 1.f);
        glm::vec2 c{-1};
        for (const auto& b : g.buttons()) if (b.label == "Claim") c = b.c;
        check(sprites(g, Icon::DropClaim2) == 0 && std::abs(c.x - 215.f) < 1.f, "no ad ready: a single CLAIM, centred");
    }
    {   // the smallest phone: the panel fits
        Env env;
        env.st.kv["cluckstack-drop"] = "claims=0;last=2026-10-07;";
        Game g(env.svc());
        g.resize(320, 568);
        step(g, 0.2f);
        g.debugOpenScreen("drop");
        step(g, 1.f);
        bool onScreen = !g.buttons().empty();
        for (const auto& b : g.buttons())
            onScreen = onScreen && b.c.x + b.half.x <= 320.f / (568.f / 780.f < 0.8f ? 0.8f : 568.f / 780.f) + 1;
        check(onScreen, "an iPhone SE: everything fits");
    }
}

} // namespace

int main() {
    modal();
    screens();
    settings();
    parentalGate();
    boostTiles();
    dailyDrop();
    return finish("GameUi");
}
