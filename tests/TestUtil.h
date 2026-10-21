// Fakes shared by the headless tests: in-memory storage, a clock the test moves by hand, scriptable ads and store,
// and an analytics sink that records every event. And the course content the game's mechanics tests are written round,
// frozen (useFixtures).
#pragma once

#include "core/Game.h"
#include "core/Gauntlet.h"
#include "core/LevelData.h"
#include "core/Meta.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace test {

inline int failures = 0;
inline void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}
inline int finish(const char* name) {
    std::printf("%s\n", failures ? (std::string(name) + " TESTS FAILED").c_str() : (std::string("all ") + name + " checks passed").c_str());
    return failures ? 1 : 0;
}

#ifdef CS_SOURCE_DIR
// The levels and gauntlets the game's mechanics tests are written round, frozen: tests/fixtures holds them as they
// were when those tests were written (the tomb on level 11, the wells on 15, the ascent on 19, every level's walls and
// saws), and they stand in for the game's own (assets/levels, assets/gauntlets) from here on. Tuning a level or a
// gauntlet in the level editor then never breaks a test of how the game plays. The tests of what every level and
// gauntlet must be (each cleared by the bot, each gauntlet one way through ...) run on the game's own: before this.
inline void useFixtures() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(CS_SOURCE_DIR) / "tests" / "fixtures";
    auto read = [](const fs::path& p) { std::ifstream in(p); std::stringstream s; s << in.rdbuf(); return s.str(); };
    bool ok = fs::is_directory(root / "gauntlets") && fs::is_directory(root / "levels");
    std::string why;
    for (const auto& e : fs::directory_iterator(root / "gauntlets")) { // (first: the levels name them)
        if (e.path().extension() != ".txt") continue;
        std::vector<std::string> rows;
        std::istringstream in(read(e.path()));
        for (std::string line; std::getline(in, line);) if (!line.empty() && line[0] != ';') rows.push_back(line);
        if (!cs::debugOverrideGauntlet(e.path().stem().string(), rows, &why)) { ok = false; std::printf("  fixture %s: %s\n", e.path().c_str(), why.c_str()); }
    }
    for (const auto& e : fs::directory_iterator(root / "levels")) {
        const std::string name = e.path().filename().string(); // level_NN.txt
        if (name.rfind("level_", 0) != 0 || e.path().extension() != ".txt") continue;
        if (!cs::debugOverrideLevel(std::atoi(name.c_str() + 6), read(e.path()), &why)) { ok = false; std::printf("  fixture %s: %s\n", name.c_str(), why.c_str()); }
    }
    if (!ok) check(false, "the frozen levels and gauntlets (tests/fixtures) stand in for the game's");
}
#endif

struct FakeStorage : cs::IStorage {
    std::map<std::string, std::string> kv;
    std::optional<std::string> get(const std::string& k) override { auto it = kv.find(k); if (it == kv.end()) return std::nullopt; return it->second; }
    void set(const std::string& k, const std::string& v) override { kv[k] = v; }
    // a 13+ player who already answered the age screen
    static FakeStorage teen() { FakeStorage s; s.kv["cluckstack-profile"] = "aud=2;"; return s; }
    // ...who has also claimed today's drop (FakeClock's day), so no screen pops up over the title
    static FakeStorage ready() { FakeStorage s = teen(); s.kv["cluckstack-drop"] = "claims=1;last=2026-10-08;"; return s; }
};

// Starts at 2026-10-08 10:00 local (UTC here: the fake treats the epoch as local time).
struct FakeClock : cs::IClock {
    int64_t t = cs::civilDay("2026-10-08") * 86400 + 10 * 3600;
    int64_t now() override { return t; }
    std::string localDate(int64_t s) override { return cs::civilDate(s >= 0 ? s / 86400 : (s - 86399) / 86400); }
    int localHour(int64_t s) override { return int(((s % 86400) + 86400) % 86400 / 3600); }
    void advance(int64_t seconds) { t += seconds; }
    void nextDay() { t += 86400; }
};

struct FakeAds : cs::IAds {
    cs::RewardedState state = cs::RewardedState::Ready;
    int rewardedShows = 0, interstitials = 0;
    bool interstitialAvailable = true, rewardOnShow = true, pending = false, showing = false;
    std::string lastPlacement;
    bool child = false;
    cs::RewardedState rewardedState() override { return state; }
    bool showRewarded(const char* placement) override {
        if (state != cs::RewardedState::Ready) return false;
        rewardedShows++; lastPlacement = placement; pending = rewardOnShow; return true;
    }
    bool consumeReward() override { const bool r = pending; pending = false; return r; }
    bool privacyOptionsRequired() override { return false; }
    void showPrivacyOptions() override {}
    bool interstitialReady() override { return interstitialAvailable; }
    bool showInterstitial() override { if (!interstitialAvailable) return false; interstitials++; return true; }
    bool adShowing() override { return showing; }
    void setAudience(bool c) override { child = c; }
};

struct FakeAnalytics : cs::IAnalytics {
    std::vector<std::string> events;
    void event(const std::string& name, const cs::AnalyticsParams&) override { events.push_back(name); }
    void userProperty(const std::string&, const std::string&) override {}
    int count(const std::string& name) const { int n = 0; for (auto& e : events) n += e == name; return n; }
};

struct FakeStore : cs::IStore {
    std::vector<cs::PurchaseEvent> queue;
    std::vector<std::string> bought;
    std::vector<cs::Product> products() override { return {{"coins_m", "$2.99"}}; }
    bool purchase(const std::string& id) override { bought.push_back(id); return true; }
    void restore() override {}
    bool pollEvent(cs::PurchaseEvent& e) override { if (queue.empty()) return false; e = queue.front(); queue.erase(queue.begin()); return true; }
};

// Share Replay as the game drives it: records every call (Game_test.cpp, Monetization_test.cpp)
struct FakeReplay final : cs::IReplay {
    cs::ReplayState st = cs::ReplayState::Recording;
    int saves = 0, crashes = 0, revives = 0, runStarts = 0, shares = 0, enables = 0;
    bool enabled = false, capturing = false;
    std::vector<bool> captureLog; // every setCapturing change
    cs::ReplayMeta last;
    cs::ReplayState state() override { return st; }
    void setEnabled(bool on) override { enabled = on; ++enables; }
    void runStarted() override { ++runStarts; }
    void setCapturing(bool on) override { if (on != capturing) captureLog.push_back(on); capturing = on; }
    void crashed() override { ++crashes; }
    void revived() override { ++revives; }
    void saveClip(const cs::ReplayMeta& m) override { ++saves; last = m; }
    void shareVideo() override { ++shares; }
    bool consumeShared(std::string&) override { return false; }
};

inline void step(cs::Game& g, float seconds) { for (int i = 0; i < int(seconds * 60); ++i) g.update(1.0 / 60.0); }

// start a run and let the hen hit the first wall without laying eggs
inline void dieQuickly(cs::Game& g) {
    g.press();
    for (int i = 0; i < 60 * 30 && g.state() == cs::Game::State::Play; ++i) g.update(1.0 / 60.0);
}

} // namespace test
