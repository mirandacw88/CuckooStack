// Fakes shared by the headless tests: in-memory storage, a clock the test moves by hand, scriptable ads and store,
// and an analytics sink that records every event.
#pragma once

#include "core/Game.h"
#include "core/Meta.h"

#include <cstdio>
#include <map>
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

inline void step(cs::Game& g, float seconds) { for (int i = 0; i < int(seconds * 60); ++i) g.update(1.0 / 60.0); }

// start a run and let the hen hit the first wall without laying eggs
inline void dieQuickly(cs::Game& g) {
    g.press();
    for (int i = 0; i < 60 * 30 && g.state() == cs::Game::State::Play; ++i) g.update(1.0 / 60.0);
}

} // namespace test
