// Coins (the soft currency: a holographic coin, never named in the UI) and the cosmetics they buy.
// Amounts and prices: Tuning.h. Purchased coins arrive through the store flow (Game::onPurchase), not from here.
#pragma once

#include "Hen.h"
#include "Meta.h"

#include <string>
#include <vector>

namespace cs {

enum class Slot : uint8_t { Outfit, Trail, Crash };
enum class Unlock : uint8_t {
    Free,      // owned from the start
    Coins,     // buy with coins
    Level,     // reaching a player level unlocks it (no cost)
    Starter,   // only in the starter-pack purchase
    Featured,  // coins, or N rewarded ads while it's the featured outfit of the week
};
enum class Trail : uint8_t { None, Sparks, Rainbow, Fire, Gold, Glitch };
enum class CrashFx : uint8_t { Feathers, Pixels, Confetti, CoinShower };

struct Cosmetic {
    const char* id;          // stable: saved, and sent to analytics
    const char* name;        // shown in the Locker
    Slot slot;
    Unlock unlock;
    int price = 0;           // coins (Coins / Featured)
    int level = 0;           // Unlock::Level
    HenSkin skin{};          // Slot::Outfit
    Trail trail = Trail::None;
    CrashFx crash = CrashFx::Feathers;
    glm::vec3 swatch[2]{};   // Locker tile colours for trails / crash effects
};

// In-app products (ids must match App Store Connect / Play Console, config/store/products.json).
enum class ProductKind : uint8_t { Coins, RemoveAds, Starter };
struct ProductDef { const char* id; ProductKind kind; int coins; const char* fallbackPrice; bool bestValue; };
constexpr ProductDef kProducts[] = {
    {"coins_s", ProductKind::Coins, 300, "$0.99", false},
    {"coins_m", ProductKind::Coins, 1000, "$2.99", false},
    {"coins_l", ProductKind::Coins, 1800, "$4.99", false},
    {"coins_xl", ProductKind::Coins, 4000, "$9.99", true},
    {"coins_xxl", ProductKind::Coins, 9000, "$19.99", false},
    {"starter_pack", ProductKind::Starter, 0, "$2.99", false},   // coins: Tuning::starterCoins + the Glitch Hen
    {"remove_ads", ProductKind::RemoveAds, 0, "$3.99", false},   // coins: Tuning::removeAdsBonus
};
const ProductDef* findProduct(const std::string& id);

const std::vector<Cosmetic>& catalog();
const Cosmetic* findCosmetic(const std::string& id);
// The outfit featured this week (ad-unlockable), picked from the Featured items by ISO-ish week number.
const Cosmetic* featuredOutfit(int64_t civilDayNumber);

// Balance, owned and equipped cosmetics, try-ons. Persisted under "cluckstack-wallet".
class Wallet {
public:
    explicit Wallet(IStorage* storage, IAnalytics* analytics);

    int coins() const { return coins_; }
    int64_t lifetimeEarned() const { return earned_; }
    void earn(int amount, const char* source);            // analytics: coins_earned
    bool spend(int amount, const char* sink);              // false (and no change) when it can't afford it
    bool canAfford(int amount) const { return amount <= coins_; }

    bool owns(const std::string& id) const;
    void grant(const std::string& id);                     // own it (purchase, level, starter pack, ads)
    bool buy(const Cosmetic& c);                           // spend + grant
    const std::string& equipped(Slot s) const { return equipped_[int(s)]; }
    void equip(const std::string& id);                     // owned items only (or the current try-on)
    const Cosmetic& active(Slot s) const;                  // what the next run uses (a try-on wins)

    // try an outfit on for a few runs (rewarded ad)
    void tryOn(const std::string& id, int runs);
    const std::string& tryOnId() const { return tryOn_; }
    int tryOnRuns() const { return tryOnRuns_; }
    void runFinished();                                    // counts down try-ons

    // featured outfit of the week: rewarded-ad progress (resets when the featured outfit changes)
    int featuredProgress(const std::string& id) const { return featuredId_ == id ? featuredAds_ : 0; }
    int addFeaturedProgress(const std::string& id);        // returns the new count

private:
    void save() const;
    IAnalytics* analytics_;
    Record rec_;
    int coins_ = 0;
    int64_t earned_ = 0;
    std::vector<std::string> owned_;
    std::string equipped_[3];
    std::string tryOn_, featuredId_;
    int tryOnRuns_ = 0, featuredAds_ = 0;
};

} // namespace cs
