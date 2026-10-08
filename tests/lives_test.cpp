// Headless checks for lives + the rewarded-ad offer (src/core/Lives.h), driving the real Game.
#include "core/Game.h"

#include <cstdio>
#include <map>

namespace {
int failures = 0;
void check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) failures++; }

struct FakeStorage : cs::IStorage {
    std::map<std::string, std::string> kv;
    std::optional<std::string> get(const std::string& k) override { auto it = kv.find(k); if (it == kv.end()) return std::nullopt; return it->second; }
    void set(const std::string& k, const std::string& v) override { kv[k] = v; }
};
struct FakeAds : cs::IAds {
    cs::RewardedState state = cs::RewardedState::Ready;
    int shows = 0; bool pendingReward = false, rewardOnShow = true;
    cs::RewardedState rewardedState() override { return state; }
    bool showRewarded() override { if (state != cs::RewardedState::Ready) return false; shows++; pendingReward = rewardOnShow; return true; }
    bool consumeReward() override { const bool r = pendingReward; pendingReward = false; return r; }
    bool privacyOptionsRequired() override { return false; }
    void showPrivacyOptions() override {}
};

void step(cs::Game& g, float s) { for (int i = 0; i < int(s * 60); ++i) g.update(1.0 / 60); }
// start a run, lay nothing, let the first wall end it, then wait for the game-over overlay
void playOneRun(cs::Game& g) {
    g.press();
    step(g, 0.1f);
    for (int i = 0; i < 60 * 30 && g.state() == cs::Game::State::Play; ++i) g.update(1.0 / 60);
    step(g, 1.5f);
}
int lives(FakeStorage& s) { return std::atoi(s.kv["cluckstack-lives"].c_str()); }
} // namespace

int main() {
    FakeStorage store;
    FakeAds ads;
    {
        cs::Game g({nullptr, &store, nullptr, &ads});
        check(g.state() == cs::Game::State::Title, "title on launch");
        playOneRun(g);
        check(g.state() == cs::Game::State::Dead && lives(store) == 2, "a finished run costs one life (3 -> 2)");
        playOneRun(g); playOneRun(g);
        check(lives(store) == 0, "three runs use all three lives");
        step(g, 1.5f); // offer opens once the heart-break animation has played
        check(g.offerOpen(), "out of lives: the offer opens over the game-over screen");
        // the close button sits on the panel's top-right corner (default 390 x 844 pt viewport)
        g.pressAt(195.f + 170.f - 16.f, 422.f - 175.f + 16.f);
        check(!g.offerOpen() && ads.shows == 0, "tapping the close button closes the offer without playing an ad");
        g.press(); // tap on the game-over screen with no lives: no new run, the offer reopens
        step(g, 0.2f);
        check(g.state() == cs::Game::State::Dead && g.offerOpen(), "out of lives: tapping reopens the offer, no new run");
        g.press(); // the offer's main button
        check(ads.shows == 1, "the offer's main button plays the rewarded ad");
        step(g, 0.1f);
        check(lives(store) == 3, "watching the ad grants 3 lives");
        playOneRun(g);
        check(g.state() == cs::Game::State::Dead && lives(store) == 2, "can play again after the reward");
    }
    {   // closing the app mid-run still costs that life
        cs::Game g({nullptr, &store, nullptr, &ads});
        g.press();
        step(g, 0.5f);
        check(g.state() == cs::Game::State::Play, "run in progress");
    }
    {
        cs::Game g({nullptr, &store, nullptr, &ads}); // relaunch
        check(lives(store) == 1, "relaunch after quitting mid-run: that run's life is gone (2 -> 1)");
    }
    {   // ad not available: no free lives until the wait, then "Play anyway" (GRANT_WHEN_AD_UNAVAILABLE)
        FakeStorage s2; FakeAds noAds; noAds.state = cs::RewardedState::Unavailable;
        s2.kv["cluckstack-lives"] = "1";
        cs::Game g({nullptr, &s2, nullptr, &noAds});
        playOneRun(g);
        g.press(); step(g, 0.2f);
        check(lives(s2) == 0, "unavailable ad: no lives granted right away");
        step(g, cs::lives::AD_WAIT_SECONDS);
        g.press(); step(g, 0.1f);
        check(lives(s2) == (cs::lives::GRANT_WHEN_AD_UNAVAILABLE ? cs::lives::REWARD : 0), "after the wait, 'Play anyway' grants the lives");
    }
    std::printf("%s\n", failures ? "LIVES TESTS FAILED" : "all lives checks passed");
    return failures ? 1 : 0;
}
