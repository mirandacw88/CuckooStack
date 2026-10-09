// Coins and cosmetics (Economy.h) and purchases through the real Game.
#include "TestUtil.h"

using namespace cs;
using namespace test;

// config/store/products.json must match kProducts (ids + coins; non-consumable coins are the Tuning bonuses)
void productsMatchConfig() {
    std::FILE* f = std::fopen(CS_SOURCE_DIR "/config/store/products.json", "rb");
    check(f != nullptr, "config/store/products.json exists");
    if (!f) return;
    std::string json;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) json.append(buf, n);
    std::fclose(f);
    Tuning t;
    bool ok = true;
    for (const ProductDef& p : kProducts) {
        const size_t at = json.find(std::string("\"id\": \"") + p.id + "\"");
        if (at == std::string::npos) { std::printf("      missing %s\n", p.id); ok = false; continue; }
        const size_t c = json.find("\"coins\":", at);
        const int coins = c == std::string::npos ? -1 : std::atoi(json.c_str() + c + 8);
        const int expect = p.kind == ProductKind::Coins ? p.coins : p.kind == ProductKind::Starter ? t.starterCoins : t.removeAdsBonus;
        if (coins != expect) { std::printf("      %s: json %d, game %d\n", p.id, coins, expect); ok = false; }
    }
    check(ok, "products.json matches Economy.h kProducts");
}

int main() {
    productsMatchConfig();
    {   // ---- wallet
        FakeStorage st;
        FakeAnalytics an;
        Wallet w(&st, &an);
        check(w.coins() == 0 && w.owns("hen_classic") && w.equipped(Slot::Outfit) == "hen_classic", "new wallet: 0 coins, classic outfit");
        w.earn(1000, "test");
        check(w.coins() == 1000 && an.count("coins_earned") == 1, "earn logs coins_earned");
        check(!w.spend(1100, "x") && w.coins() == 1000, "can't overspend");
        const Cosmetic* vapor = findCosmetic("hen_vapor");
        check(vapor && w.buy(*vapor) && w.owns("hen_vapor") && w.coins() == 1000 - vapor->price, "buy an outfit");
        w.equip("hen_toxic");
        check(w.equipped(Slot::Outfit) == "hen_classic", "can't equip what you don't own");
        w.equip("hen_vapor");
        check(w.active(Slot::Outfit).id == std::string("hen_vapor"), "equip");
        w.tryOn("hen_lava", 2);
        check(w.active(Slot::Outfit).id == std::string("hen_lava"), "try-on wins while it lasts");
        w.runFinished(); w.runFinished();
        check(w.active(Slot::Outfit).id == std::string("hen_vapor"), "try-on ends after its runs");
        Wallet w2(&st, nullptr);
        check(w2.coins() == w.coins() && w2.owns("hen_vapor") && w2.equipped(Slot::Outfit) == "hen_vapor", "wallet persists");
        check(w2.addFeaturedProgress("hen_chrome") == 1 && w2.addFeaturedProgress("hen_chrome") == 2, "featured progress counts");
        check(w2.addFeaturedProgress("hen_tiger") == 1, "featured progress resets for a new featured outfit");
        check(featuredOutfit(civilDay("2026-10-08")) != nullptr, "there is a featured outfit every week");
        check(featuredOutfit(civilDay("2026-10-05")) == featuredOutfit(civilDay("2026-10-11")), "same featured outfit Monday..Sunday");
    }
    {   // ---- purchases
        FakeStorage st = FakeStorage::teen();
        st.kv["cluckstack-drop"] = "claims=1;last=2026-10-08;";
        FakeClock clock;
        FakeStore store;
        FakeAds ads;
        GameServices s;
        s.storage = &st; s.clock = &clock; s.store = &store; s.ads = &ads;
        Game g(s);
        g.onPurchase({"coins_m", PurchaseResult::Success, false});
        check(g.wallet().coins() == 1000, "coin pack credits its coins");
        g.onPurchase({"coins_m", PurchaseResult::Cancelled, false});
        check(g.wallet().coins() == 1000, "cancelled purchase credits nothing");
        g.onPurchase({"remove_ads", PurchaseResult::Success, false});
        check(g.adPolicy().removeAds() && g.wallet().coins() == 1500, "Remove Ads: flag + 500 bonus coins");
        g.onPurchase({"remove_ads", PurchaseResult::Success, true});
        check(g.wallet().coins() == 1500, "restoring Remove Ads doesn't pay the bonus twice");
        g.onPurchase({"starter_pack", PurchaseResult::Success, false});
        check(g.wallet().owns("hen_glitch") && g.wallet().coins() == 3000 && g.wallet().equipped(Slot::Outfit) == "hen_glitch",
              "starter pack: Glitch Hen (equipped) + 1,500 coins");
        // the shop's buttons start purchases
        step(g, 0.1f);
        g.tapButton("Coins");  // top bar coin counter opens the shop
        step(g, 0.5f);
        check(g.screen() == Game::Screen::Shop, "coin counter opens the shop");
        check(g.tapButton("coins_xl") && !store.bought.empty() && store.bought.back() == "coins_xl", "tapping a pack starts that purchase");
    }
    {   // ---- children: purchases go through the parental gate
        FakeStorage st;
        st.kv["cluckstack-profile"] = "aud=1;";
        st.kv["cluckstack-drop"] = "claims=1;last=2026-10-08;";
        FakeClock clock;
        FakeStore store;
        GameServices s;
        s.storage = &st; s.clock = &clock; s.store = &store;
        Game g(s);
        step(g, 0.1f);
        g.tapButton("Coins");
        step(g, 0.5f);
        g.tapButton("coins_s");
        step(g, 0.1f);
        check(store.bought.empty() && g.screen() == Game::Screen::ParentalGate, "child: a purchase opens the parental gate first");
    }
    {   // ---- run coins
        FakeStorage st = FakeStorage::teen();
        st.kv["cluckstack-drop"] = "claims=1;last=2026-10-08;";
        FakeClock clock;
        FakeAds ads;
        ads.state = RewardedState::Unavailable;
        GameServices s;
        s.storage = &st; s.clock = &clock; s.ads = &ads;
        Game g(s);
        dieQuickly(g);
        step(g, 1.5f);
        check(g.state() == Game::State::Dead, "run over");
        Tuning t;
        check(g.wallet().coins() >= 0 && g.wallet().lifetimeEarned() >= 0, "run coins never negative");
        (void)t;
    }
    return finish("economy");
}
