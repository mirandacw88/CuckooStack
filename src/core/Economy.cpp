#include "Economy.h"

#include <algorithm>
#include <sstream>

namespace cs {

namespace {

HenSkin skin(const char* body, const char* plate, const char* accent, const char* visor, float metal = 0.f, float rough = 0.62f,
             float glowLvl = 0.07f, const char* chrome = "#dfe5f0", const char* gold = "#ffb347") {
    HenSkin s;
    s.body = hexColor(body); s.plate = hexColor(plate); s.accent = hexColor(accent); s.visor = hexColor(visor);
    s.bodyMetal = metal; s.bodyRough = rough; s.bodyGlow = glowLvl; s.chrome = hexColor(chrome); s.gold = hexColor(gold);
    return s;
}

Cosmetic outfit(const char* id, const char* name, Unlock u, int price, int level, HenSkin s) {
    Cosmetic c{id, name, Slot::Outfit, u, price, level};
    c.skin = s;
    return c;
}
Cosmetic trail(const char* id, const char* name, Unlock u, int price, int level, Trail t, const char* a, const char* b) {
    Cosmetic c{id, name, Slot::Trail, u, price, level};
    c.trail = t; c.swatch[0] = srgbColor(a); c.swatch[1] = srgbColor(b);
    return c;
}
Cosmetic crash(const char* id, const char* name, Unlock u, int price, CrashFx f, const char* a, const char* b) {
    Cosmetic c{id, name, Slot::Crash, u, price, 0};
    c.crash = f; c.swatch[0] = srgbColor(a); c.swatch[1] = srgbColor(b);
    return c;
}

std::vector<Cosmetic> build() {
    std::vector<Cosmetic> v;
    // outfits
    v.push_back(outfit("hen_classic", "Classic", Unlock::Free, 0, 0, HenSkin{}));
    v.push_back(outfit("hen_midnight", "Midnight", Unlock::Level, 0, 3, skin("#1d1f2e", "#0c0d14", "#9a5bff", "#29e7ff", 0.2f, 0.45f, 0.02f)));
    v.push_back(outfit("hen_vapor", "Vaporwave", Unlock::Coins, 600, 0, skin("#ffc2ec", "#2a6f86", "#9a5bff", "#f4ff5a", 0.f, 0.5f, 0.09f)));
    v.push_back(outfit("hen_toxic", "Toxic", Unlock::Coins, 900, 0, skin("#c6ff4a", "#163a1c", "#ff2bd6", "#f4ff5a", 0.f, 0.5f, 0.12f)));
    v.push_back(outfit("hen_ice", "Cryo", Unlock::Level, 0, 8, skin("#cfeaff", "#5c7fa8", "#ffffff", "#29e7ff", 0.6f, 0.2f, 0.1f, "#ffffff")));
    v.push_back(outfit("hen_lava", "Magma", Unlock::Coins, 1500, 0, skin("#3a0d0a", "#120404", "#ff6a2b", "#ff8a1a", 0.3f, 0.5f, 0.02f, "#ff8a1a", "#ff6a2b")));
    v.push_back(outfit("hen_gold", "Gold Rush", Unlock::Coins, 2500, 0, skin("#ffcc4d", "#1a1408", "#f4ff5a", "#29e7ff", 0.95f, 0.18f, 0.02f, "#ffe9a8", "#fff2b0")));
    v.push_back(outfit("hen_chrome", "Liquid Chrome", Unlock::Featured, 3000, 0, skin("#e9eef7", "#7d8597", "#29e7ff", "#ff2bd6", 1.f, 0.12f, 0.f, "#ffffff")));
    v.push_back(outfit("hen_tiger", "Neon Tiger", Unlock::Featured, 2000, 0, skin("#ff8a1a", "#14090a", "#ff2bd6", "#29e7ff", 0.f, 0.5f, 0.06f)));
    v.push_back(outfit("hen_holo", "Hologram", Unlock::Featured, 2200, 0, skin("#9ff6ff", "#2a3a66", "#29e7ff", "#ffffff", 0.f, 0.3f, 0.35f, "#c9fbff")));
    v.push_back(outfit("hen_sakura", "Sakura", Unlock::Level, 0, 14, skin("#ffe3f2", "#4a1f3d", "#ff7ae6", "#2bff9a", 0.f, 0.55f, 0.08f)));
    {
        HenSkin g = skin("#101018", "#05050a", "#ff2bd6", "#29e7ff", 0.3f, 0.35f, 0.f);
        g.rainbow = true;
        v.push_back(outfit("hen_glitch", "Glitch Hen", Unlock::Starter, 0, 0, g));
    }
    // trails
    v.push_back(trail("trail_none", "No trail", Unlock::Free, 0, 0, Trail::None, "#3a3150", "#2a2440"));
    v.push_back(trail("trail_sparks", "Neon Sparks", Unlock::Level, 0, 2, Trail::Sparks, "#29e7ff", "#ff2bd6"));
    v.push_back(trail("trail_fire", "Afterburner", Unlock::Coins, 500, 0, Trail::Fire, "#ffd23a", "#ff6a2b"));
    v.push_back(trail("trail_gold", "Coin Dust", Unlock::Coins, 800, 0, Trail::Gold, "#fff2b0", "#ffb21a"));
    v.push_back(trail("trail_rainbow", "Prism", Unlock::Coins, 1200, 0, Trail::Rainbow, "#ff2bd6", "#29e7ff"));
    v.push_back(trail("trail_glitch", "Glitch Wake", Unlock::Level, 0, 11, Trail::Glitch, "#ff2b4a", "#29e7ff"));
    // crash effects
    v.push_back(crash("crash_feathers", "Feather Burst", Unlock::Free, 0, CrashFx::Feathers, "#f4f7ff", "#ff2bd6"));
    v.push_back(crash("crash_pixels", "Pixel Shatter", Unlock::Coins, 400, CrashFx::Pixels, "#29e7ff", "#9a5bff"));
    v.push_back(crash("crash_confetti", "Party Popper", Unlock::Coins, 400, CrashFx::Confetti, "#f4ff5a", "#ff2bd6"));
    v.push_back(crash("crash_coins", "Jackpot", Unlock::Coins, 900, CrashFx::CoinShower, "#ffd23a", "#fff2b0"));
    return v;
}

} // namespace

const ProductDef* findProduct(const std::string& id) {
    for (const ProductDef& p : kProducts)
        if (id == p.id) return &p;
    return nullptr;
}

const std::vector<Cosmetic>& catalog() {
    static const std::vector<Cosmetic> c = build();
    return c;
}

const Cosmetic* findCosmetic(const std::string& id) {
    for (const Cosmetic& c : catalog())
        if (id == c.id) return &c;
    return nullptr;
}

const Cosmetic* featuredOutfit(int64_t day) {
    std::vector<const Cosmetic*> f;
    for (const Cosmetic& c : catalog())
        if (c.unlock == Unlock::Featured) f.push_back(&c);
    if (f.empty()) return nullptr;
    const int64_t week = (day + 3) / 7; // weeks start on Monday (1970-01-01 was a Thursday)
    return f[size_t(((week % int64_t(f.size())) + int64_t(f.size())) % int64_t(f.size()))];
}

Wallet::Wallet(IStorage* storage, IAnalytics* analytics) : analytics_(analytics), rec_(storage, "cluckstack-wallet") {
    coins_ = std::max(0, rec_.i("coins"));
    earned_ = rec_.l("earned");
    std::istringstream in(rec_.s("owned"));
    for (std::string id; std::getline(in, id, ',');)
        if (!id.empty() && findCosmetic(id)) owned_.push_back(id);
    for (const Cosmetic& c : catalog())
        if (c.unlock == Unlock::Free && !owns(c.id)) owned_.push_back(c.id);
    const char* keys[3] = {"eq_outfit", "eq_trail", "eq_crash"};
    const char* defs[3] = {"hen_classic", "trail_none", "crash_feathers"};
    for (int i = 0; i < 3; ++i) {
        equipped_[i] = rec_.s(keys[i], defs[i]);
        if (!owns(equipped_[i])) equipped_[i] = defs[i];
    }
    tryOn_ = rec_.s("tryon");
    tryOnRuns_ = rec_.i("tryon_runs");
    if (!findCosmetic(tryOn_) || tryOnRuns_ <= 0) { tryOn_.clear(); tryOnRuns_ = 0; }
    featuredId_ = rec_.s("feat");
    featuredAds_ = rec_.i("feat_ads");
}

void Wallet::save() const {
    Record& r = const_cast<Record&>(rec_);
    r.set("coins", coins_);
    r.set("earned", earned_);
    std::string owned;
    for (const std::string& id : owned_) { if (!owned.empty()) owned += ','; owned += id; }
    r.set("owned", owned);
    r.set("eq_outfit", equipped_[0]); r.set("eq_trail", equipped_[1]); r.set("eq_crash", equipped_[2]);
    r.set("tryon", tryOn_); r.set("tryon_runs", tryOnRuns_);
    r.set("feat", featuredId_); r.set("feat_ads", featuredAds_);
    r.save();
}

void Wallet::earn(int amount, const char* source) {
    if (amount <= 0) return;
    coins_ += amount;
    earned_ += amount;
    save();
    if (analytics_) analytics_->event("coins_earned", {{"amount", std::to_string(amount)}, {"source", source}, {"balance", std::to_string(coins_)}});
}

bool Wallet::spend(int amount, const char* sink) {
    if (amount < 0 || amount > coins_) return false;
    coins_ -= amount;
    save();
    if (analytics_) analytics_->event("coins_spent", {{"amount", std::to_string(amount)}, {"sink", sink}, {"balance", std::to_string(coins_)}});
    return true;
}

bool Wallet::owns(const std::string& id) const { return std::find(owned_.begin(), owned_.end(), id) != owned_.end(); }

void Wallet::grant(const std::string& id) {
    if (owns(id) || !findCosmetic(id)) return;
    owned_.push_back(id);
    if (tryOn_ == id) { tryOn_.clear(); tryOnRuns_ = 0; }
    save();
}

bool Wallet::buy(const Cosmetic& c) {
    if (owns(c.id)) return true;
    if (c.unlock != Unlock::Coins && c.unlock != Unlock::Featured) return false;
    if (!spend(c.price, c.id)) return false;
    grant(c.id);
    return true;
}

void Wallet::equip(const std::string& id) {
    const Cosmetic* c = findCosmetic(id);
    if (!c || !owns(id)) return;
    equipped_[int(c->slot)] = id;
    if (!tryOn_.empty() && findCosmetic(tryOn_)->slot == c->slot) { tryOn_.clear(); tryOnRuns_ = 0; }
    save();
}

const Cosmetic& Wallet::active(Slot s) const {
    if (!tryOn_.empty()) {
        const Cosmetic* t = findCosmetic(tryOn_);
        if (t && t->slot == s) return *t;
    }
    const Cosmetic* c = findCosmetic(equipped_[int(s)]);
    return c ? *c : catalog().front();
}

void Wallet::tryOn(const std::string& id, int runs) {
    if (!findCosmetic(id) || owns(id)) return;
    tryOn_ = id;
    tryOnRuns_ = runs;
    save();
}

void Wallet::runFinished() {
    if (tryOn_.empty()) return;
    if (--tryOnRuns_ <= 0) { tryOn_.clear(); tryOnRuns_ = 0; }
    save();
}

int Wallet::addFeaturedProgress(const std::string& id) {
    if (featuredId_ != id) { featuredId_ = id; featuredAds_ = 0; }
    ++featuredAds_;
    save();
    return featuredAds_;
}

} // namespace cs
