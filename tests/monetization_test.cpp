// Ad rules (Monetization.h): every interstitial condition, rewarded caps and cooldowns, and the Continue flow
// driven through the real Game.
#include "TestUtil.h"

using namespace cs;
using namespace test;

int main() {
    {   // ---- AdPolicy: interstitial rules
        FakeStorage st;
        FakeClock clock;
        Tuning t;
        AdPolicy p(&st, &clock, t);
        for (int i = 0; i < 4; ++i) { p.runFinished(); clock.advance(60); }
        check(!p.interstitialDue(), "no interstitial during a new player's first runs (grace)");
        p.runFinished(); clock.advance(60);
        check(p.interstitialDue(), "due once past the grace runs, 3+ runs and 90+ s since the last ad");
        p.interstitialShown();
        p.runFinished(); p.runFinished(); clock.advance(200);
        check(!p.interstitialDue(), "not due again until 3 more runs");
        p.runFinished();
        check(p.interstitialDue(), "due after the 3rd run");
        p.interstitialShown();
        p.runFinished(); p.runFinished(); p.runFinished(); clock.advance(30);
        check(!p.interstitialDue(), "3 runs but only 30 s since the last ad: not due");
        clock.advance(60);
        check(p.interstitialDue(), "...due once 90 s have passed");
        p.rewardedAtThisGameOver();
        check(!p.interstitialDue(), "a rewarded ad at this game over skips the interstitial");
        p.newGameOver();
        p.purchaseMade();
        check(!p.interstitialDue(), "no interstitials after a purchase this session");
        AdPolicy p2(&st, &clock, t);
        check(p2.interstitialDue(), "counters survive a relaunch (new session: purchase flag cleared)");
        p2.setRemoveAds(true);
        check(!p2.interstitialDue(), "Remove Ads: never");
        AdPolicy p3(&st, &clock, t);
        check(p3.removeAds(), "Remove Ads is remembered");
    }
    {   // ---- AdPolicy: rewarded caps and cooldowns
        FakeStorage st;
        FakeClock clock;
        Tuning t;
        AdPolicy p(&st, &clock, t);
        for (int i = 0; i < t.boostAdsPerDay; ++i) { check(p.canOffer(Placement::Boost), "boost ad offered"); p.rewardedWatched(Placement::Boost); }
        check(!p.canOffer(Placement::Boost), "boost ads capped per day");
        p.rewardedWatched(Placement::FreeCoins);
        check(!p.canOffer(Placement::FreeCoins) && p.cooldownLeft(Placement::FreeCoins) > 0, "free coins: cooldown after each");
        clock.advance(t.freeCoinsCooldown);
        check(p.canOffer(Placement::FreeCoins), "free coins: available again after the cooldown");
        p.rewardedWatched(Placement::StreakSave);
        check(!p.canOffer(Placement::StreakSave), "streak save: once a day");
        clock.nextDay();
        check(p.canOffer(Placement::Boost) && p.canOffer(Placement::StreakSave), "caps reset the next day");
        for (int i = 0; i < t.rewardedDailyCap; ++i) p.rewardedWatched(Placement::Continue);
        check(!p.canOffer(Placement::DoubleCoins), "global daily rewarded cap");
    }
    {   // ---- Game: Continue (ad), then a second crash with no continue left -> results, coins credited after
        FakeStorage st = FakeStorage::ready();
        FakeClock clock;
        FakeAds ads;
        FakeAnalytics an;
        GameServices s;
        s.storage = &st; s.clock = &clock; s.ads = &ads; s.analytics = &an;
        Game g(s);
        const int coins0 = g.wallet().coins();
        dieQuickly(g);
        check(g.state() == Game::State::Dead, "crash");
        step(g, 0.9f);
        check(g.screen() == Game::Screen::Continue, "Continue panel opens after the crash");
        check(g.wallet().coins() == coins0, "no results yet while Continue is up");
        check(!g.tapButton("Tap to reboot") && g.screen() == Game::Screen::Continue, "results screen hidden while Continue is up");
        check(g.tapButton("Watch ad") && ads.rewardedShows == 1 && ads.lastPlacement == "continue", "Watch ad plays a rewarded ad (placement continue)");
        step(g, 0.1f);
        check(g.state() == Game::State::Play && g.screen() == Game::Screen::None, "reward: the hen is back in the run");
        for (int i = 0; i < 60 * 40 && g.state() == Game::State::Play; ++i) g.update(1.0 / 60.0);
        step(g, 0.9f);
        // coins 0 -> no second continue possible (300 coins, no ad for the second)
        const bool second = g.screen() == Game::Screen::Continue;
        if (second) { g.tapButton("No thanks"); step(g, 0.1f); }
        check(g.screen() != Game::Screen::Continue, "no free second continue");
        step(g, 2.f);
        check(an.count("run_end") == 1, "exactly one run_end for the run (the continue doesn't end it)");
        check(g.wallet().coins() >= coins0, "run coins credited at the end");
        check(ads.interstitials == 0, "no interstitial in the first runs");
    }
    {   // ---- Game: Continue timeout -> results; interstitial between runs once due, never mid-run
        FakeStorage st = FakeStorage::teen();
        st.kv["cluckstack-ads"] = "runs=10;since=5;last=0;";
        st.kv["cluckstack-drop"] = "claims=1;last=2026-10-08;";
        FakeClock clock;
        FakeAds ads;
        ads.state = RewardedState::Unavailable; // no rewarded video: Continue only for coins (none) -> straight to results
        GameServices s;
        s.storage = &st; s.clock = &clock; s.ads = &ads;
        Game g(s);
        dieQuickly(g);
        step(g, 1.5f);
        check(g.screen() == Game::Screen::None, "no ad and no coins: no Continue panel");
        check(ads.interstitials == 0, "interstitial never shown during the crash / results");
        step(g, 1.f);
        g.press(); // next run
        check(ads.interstitials == 1, "interstitial plays when the player starts the next run");
        ads.showing = true;
        step(g, 0.5f);
        check(g.state() == Game::State::Dead, "the run waits while the ad is up");
        ads.showing = false;
        step(g, 0.1f);
        check(g.state() == Game::State::Play, "...and starts once it's dismissed");
        dieQuickly(g); step(g, 2.5f); g.press();
        check(ads.interstitials == 1, "not again right after (3 runs / 90 s spacing)");
    }
    return finish("monetization");
}
