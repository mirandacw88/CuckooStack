#include "Game.h"
#include "EndCard.h"
#include "Links.h"
#include "Log.h"
#include "Materials.h"
#include "Reminders.h"

#include <cctype>
#include <ctime>
#include <sstream>

namespace cs {

namespace {
struct Palette {
    glm::vec3 cyan = glow("#29e7ff", 3), pink = glow("#ff2bd6", 3), white = glow("#ffffff", 2.4f), volt = glow("#f4ff5a", 3),
              green = glow("#2bff9a", 2.6f), red = glow("#ff2b4a", 3), water = glow("#a8dcff", 1.1f), steam = hexColor("#8a78b0"),
              smoke = hexColor("#3a3150"), ember = glow("#ff6a2b", 3);
};
const Palette& C() { static const Palette p; return p; }

struct Grade { glm::vec3 fog, mid, bot, haze, light; };
const Grade& grade(int i) {
    static const Grade g[] = {
        {hexColor("#1d0b36"), hexColor("#150a35"), hexColor("#4a1468"), hexColor("#ff2e88"), hexColor("#ff2bd6")}, // neon magenta
        {hexColor("#061b2c"), hexColor("#06203a"), hexColor("#0d3f60"), hexColor("#22c4ff"), hexColor("#29e7ff")}, // ice blue
        {hexColor("#26081a"), hexColor("#2a0a22"), hexColor("#62162c"), hexColor("#ff5a3d"), hexColor("#ff6a3d")}, // red dusk
        {hexColor("#120a3a"), hexColor("#140a40"), hexColor("#2c1a7c"), hexColor("#9a5bff"), hexColor("#9a5bff")}, // violet
    };
    return g[i % 4];
}

const glm::vec3& discoPalette(int i) {
    static const glm::vec3 d[] = {glow("#ff2bd6", 1.05f), glow("#29e7ff", 1.05f), glow("#f4ff5a", 0.95f),
                                  glow("#8a5bff", 1.25f), glow("#ff8a1a", 1.f), glow("#2bff9a", 1.f)};
    return d[((i % 6) + 6) % 6];
}
glm::vec3 discoColor(int gx, int gz, int st) {
    const int mode = (st / 8) % 3;
    if (mode == 0) { const int odd = (gx + gz + st) & 1; return discoPalette((st >> 1) + (odd ? 0 : 3)); } // swapping checkerboard
    if (mode == 1) return discoPalette(gx + gz + st);                                                     // rainbow diagonal wave
    const float h = hash(gx * 13.1 + gz * 7.3 + st * 1.7);                                                // sparkle
    const glm::vec3 c = discoPalette(static_cast<int>(h * 60));
    return h < 0.4f ? c * 0.45f : c;
}

float easeOutBack(float t) { const float c1 = 2.2f, c3 = c1 + 1; return 1 + c3 * std::pow(t - 1, 3.f) + c1 * std::pow(t - 1, 2.f); }

GameServices withDefaults(GameServices s) {
    static NullAudio audio; static NullAds ads; static SystemClock clock; static NullAnalytics analytics;
    static NullRemoteConfig rc; static NullStore store; static NullNotifications notifications; static NullReplay replay;
    static NullLeaderboards leaderboards; static NullBackend backend;
    if (!s.audio) s.audio = &audio;
    if (!s.ads) s.ads = &ads;
    if (!s.clock) s.clock = &clock;
    if (!s.analytics) s.analytics = &analytics;
    if (!s.remoteConfig) s.remoteConfig = &rc;
    if (!s.store) s.store = &store;
    if (!s.notifications) s.notifications = &notifications;
    if (!s.replay) s.replay = &replay;
    if (!s.leaderboards) s.leaderboards = &leaderboards;
    if (!s.backend) s.backend = &backend;
    return s;
}

const glm::vec3 kCapOk = glow("#ffbe3a", 1.5f), kCapDanger = glow("#ff2a3a", 2.2f), kTileWhite = glow("#ffffff", 1.25f);
} // namespace

Game::Game(const GameServices& services)
    : svc_(withDefaults(services)), rng_(static_cast<uint64_t>(std::time(nullptr)) * 2654435761ull + 1), dda_(svc_.storage), world_(rng_),
      profile_(svc_.storage, svc_.clock), wallet_(svc_.storage, svc_.analytics), adPolicy_(svc_.storage, svc_.clock, tune_),
      prog_(svc_.storage, tune_), dayStreak_(svc_.storage, svc_.clock), missions_(svc_.storage, svc_.clock), drop_(svc_.storage, svc_.clock) {
    tune_.load(svc_.remoteConfig);
    const Grade& g = grade(0);
    fog_ = g.fog; skyMid_ = g.mid; skyBot_ = g.bot; skyHaze_ = g.haze; pinkLight_ = g.light;
    loadRecords();
    svc_.audio->setMuted(muted_);
    if (!fonts_.build()) CS_LOGE("Font atlas could not be built; text disabled");
    text_ = std::make_unique<TextLayout>(fonts_);
    if (!hudIcons_.buildIcons()) CS_LOGE("HUD sprites could not be decoded");
    resize(390, 844);
    coinShown_ = float(wallet_.coins());
    applyCosmetics();
    reset();
    svc_.replay->setEndCardRenderer([this](int w, int h, const ReplayMeta& m) { return endCard(w, h, m); });
    {   // a challenge accepted earlier today is still on
        Record r(svc_.storage, "cluckstack-challenge");
        if (r.s("day") == svc_.clock->today() && r.i("m") > 0)
            challenge_ = Challenge{r.s("day"), r.s("n"), r.s("c"), r.i("m"), r.i("beaten") != 0, 1.f};
    }
    onForeground();
}

std::vector<uint8_t> Game::endCard(int width, int height, const ReplayMeta& meta) const { return renderEndCard(fonts_, hudIcons_, width, height, meta); }

// A new session (launch, or back from the background after a while): audience, analytics, the title's queue.
void Game::onForeground() {
    profile_.sessionStarted();
    if (profile_.audience() == Audience::Unknown) {
        if (screen_ != Screen::AgeGate) openScreen(Screen::AgeGate);
    } else {
        svc_.ads->setAudience(profile_.child());
        svc_.analytics->userProperty("audience", profile_.child() ? "child" : "teen_adult");
        svc_.replay->setEnabled(!profile_.child() && profile_.flag("replays"));
    }
    track("session_start", {{"session", std::to_string(profile_.sessions())}, {"days_since_install", std::to_string(profile_.daysSinceInstall())},
                            {"level", std::to_string(prog_.level())}, {"coins", std::to_string(wallet_.coins())}});
    missions_.refresh();
    // reminders: did the last one bring the player back? (3 ignored in a row -> back off to every third day)
    {
        int opened = -1;
        if (svc_.notifications->consumeOpened(opened)) {
            profile_.setCounter("notif_ignored", 0);
            track("notif_open", {{"id", std::to_string(opened)}});
        } else {
            const int next = profile_.counter("notif_next");
            if (next > 0 && svc_.clock->now() > next) profile_.setCounter("notif_ignored", profile_.counter("notif_ignored") + 1);
        }
        profile_.setCounter("notif_next", 0);
    }
    queueSessionScreens();
}

void Game::rescheduleReminders() {
    ReminderState st;
    st.enabled = !profile_.child() && profile_.flag("notif") && svc_.notifications->permission() == NotifPermission::Granted;
    st.usualHour = profile_.usualHour();
    st.streak = dayStreak_.count();
    st.playedToday = dayStreak_.playedToday();
    st.dropStep = drop_.dayIndex();
    st.ignored = profile_.counter("notif_ignored");
    const std::vector<Reminder> list = planReminders(*svc_.clock, tune_, st);
    svc_.notifications->replaceAll(list);
    int64_t next = 0;
    for (const Reminder& r : list) if (!next || r.at < next) next = r.at;
    profile_.setCounter("notif_next", int(next));
}

void Game::onBackground() {
    track("session_end", {{"runs_today", std::to_string(day_.attempts)}});
    rescheduleReminders();
}

void Game::track(const char* event, AnalyticsParams params) { svc_.analytics->event(event, params); }

// ---------- challenge links ----------
namespace {
std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) { out += char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else out += s[i];
    }
    return out;
}
std::string queryParam(const std::string& url, const std::string& key) {
    const size_t q = url.find('?');
    if (q == std::string::npos) return {};
    std::istringstream in(url.substr(q + 1));
    for (std::string kv; std::getline(in, kv, '&');) {
        const size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return urlDecode(kv.substr(eq + 1));
    }
    return {};
}
} // namespace

void Game::openLink(const std::string& url) {
    if (url.find("/c") == std::string::npos) return;   // only challenge links for now
    if (profile_.child()) return;                       // social features are 13+
    const std::string day = queryParam(url, "d");
    const int meters = std::atoi(queryParam(url, "m").c_str());
    std::string name = queryParam(url, "n").substr(0, 16);
    for (char& c : name) if (static_cast<unsigned char>(c) < 32) c = ' ';
    const bool expired = day != svc_.clock->today();
    track("challenge_open", {{"expired", expired ? "1" : "0"}, {"meters", std::to_string(meters)}});
    if (meters <= 0 || meters > 100000) return;
    if (expired) { toast("That course expired. Today\u2019s is live!", Icon::Missions); return; }
    std::string id = queryParam(url, "c").substr(0, 40);
    for (char c : id) if (!std::isalnum(static_cast<unsigned char>(c))) { id.clear(); break; }
    challenge_ = Challenge{day, name, id, meters, false, 1.f};
    saveChallenge();
    toast("Beat " + (name.empty() ? std::string("your friend") : name) + ": " + std::to_string(meters) + " m", Icon::Trophy);
    if (state_ == State::Play) return;                 // the marker shows from the next run
}

void Game::saveChallenge() {
    Record r(svc_.storage, "cluckstack-challenge");
    r.clear();
    if (challenge_) {
        r.set("day", challenge_->day); r.set("n", challenge_->name); r.set("c", challenge_->id); r.set("m", challenge_->meters);
        r.set("beaten", challenge_->beaten ? 1 : 0);
    }
    r.save();
}

std::string Game::challengeLink(int meters) const {
    std::string url = std::string(links::kSite) + "/c?d=" + svc_.clock->today() + "&m=" + std::to_string(meters);
    if (!sharedChallengeId_.empty()) url += "&c=" + sharedChallengeId_;
    return url + "&utm_source=share&utm_medium=challenge";
}

// Share: with friend nudges available, register the challenge first (waits at most 1.5 s for the id), so the
// sharer hears about it when a friend beats them.
void Game::doShare(const std::string& caption, int meters, const std::string& id) {
    sharedChallengeId_ = id;
    svc_.replay->share(caption, challengeLink(meters));
}

// ---------- persistence ----------
void Game::loadRecords() {
    day_.date = svc_.clock->today();
    if (!svc_.storage) return;
    if (auto v = svc_.storage->get("cluckstack-best")) best_ = std::atoi(v->c_str());
    if (auto v = svc_.storage->get("cluckstack-music")) muted_ = *v == "off";
    if (auto v = svc_.storage->get("cluckstack-daily")) {
        std::istringstream in(*v);
        DayRecord r;
        in >> r.date >> r.best >> r.bestDist >> r.attempts;
        if (r.date == day_.date) day_ = r;
    }
}
void Game::saveDay() {
    if (!svc_.storage) return;
    std::ostringstream out;
    out << day_.date << ' ' << day_.best << ' ' << day_.bestDist << ' ' << day_.attempts;
    svc_.storage->set("cluckstack-daily", out.str());
}
void Game::rollDay() {
    const std::string t = svc_.clock->today();
    if (day_.date != t) day_ = {t, 0, 0, 0};
}

// ---------- flow ----------
void Game::resize(float w, float h) {
    const float sh = std::max(1.f, h);
    const float prev = uiScale_;
    uiScale_ = std::clamp(sh / kUiDesignHeight, kUiMinScale, 1.f);
    viewW_ = std::max(1.f, w) / uiScale_; viewH_ = sh / uiScale_;
    safeTop_ *= prev / uiScale_; safeBottom_ *= prev / uiScale_; // insets were set in the previous scale
    camera_.setViewport(viewW_, viewH_);
}

void Game::reset() {
    eggs_.clear(); debris_.clear(); splats_.clear(); popups_.clear();
    fxAdd_.clear(); fxSmoke_.clear();
    rollDay(); dda_.newDay(day_.date);
    level_.reset(seedFor("cluckstack:" + day_.date));
    level_.generateUntil(60); level_.rebuildBlocks();
    x_ = 0; speed_ = 0; baseY_ = 0; vy_ = 0; meter_ = MAXE; bonus_ = 0; lastBonus_ = 0; cornCount_ = 0; score_ = 0; hop_ = 0; hopV_ = 0;
    timeScale_ = 1; freeze_ = 0; danger_ = 0; prevFull_ = MAXE; sector_ = 1; streak_ = 0; gradeIdx_ = 0; camKick_ = 0;
    hen_.nodes[hen_.root].rot = glm::vec3(0.f);
    hen_.unshatter();
    hen_.nodes[hen_.root].pos = glm::vec3(0.f);
    resetSurge();
}

void Game::start() {
    if (profile_.audience() == Audience::Unknown) { openScreen(Screen::AgeGate); return; }
    // between runs: the interstitial (if due) plays first, then the run starts once it's dismissed
    if (state_ == State::Dead && !pendingStart_ && adPolicy_.interstitialDue() && svc_.ads->interstitialReady() && svc_.ads->showInterstitial()) {
        adPolicy_.interstitialShown();
        track("ad_interstitial_shown", {{"lifetime_runs", std::to_string(adPolicy_.lifetimeRuns())}});
        pendingStart_ = true;
        return;
    }
    pendingStart_ = false;
    lockerPreview_.clear();
    applyCosmetics();
    if (dayStreak_.status() == Streak::Status::Saveable && !profile_.child()) track("streak_lost", {{"count", std::to_string(dayStreak_.count())}});
    const int streakBefore = dayStreak_.count();
    dayStreak_.played();
    if (dayStreak_.count() != streakBefore) track("streak_update", {{"count", std::to_string(dayStreak_.count())}});
    reset();
    state_ = State::Play;
    beat_.start(); svc_.audio->musicStart();
    day_.attempts++; saveDay(); placeGate();
    overDelay_ = -1;
    for (float& p : popT_) p = 0;
    intro_ = 1;
    run_ = RunStats{};
    // new players: extra (invisible) help on the first runs, fading out, so the first session ends on a high
    {
        const int runs = adPolicy_.lifetimeRuns();
        dda_.setBoost(runs < tune_.newbieRuns ? tune_.newbieBoost * (1.f - float(runs) / float(std::max(1, tune_.newbieRuns))) : 0.f);
    }
    runBoost_ = boost_; boost_ = Boost::None;
    run_.boosted = runBoost_ != Boost::None;
    continuePending_ = continueOffered_ = false;
    rings_.spawn({0, 0.02f, 0}, C().cyan, 2.2f, 0.6f, false);
    burst({0, 0.1f, 0}, 30, {C().cyan, C().pink}, 2, 5, 0.6f, 0.1f, 0.3f, 0, true);
    svc_.replay->runStarted();
    replaySaved_ = false;
    if (challenge_ && challenge_->day != svc_.clock->today()) { challenge_.reset(); saveChallenge(); }
    if (challenge_) challenge_->opacity = 1.f;
    track("run_start", {{"attempt", std::to_string(day_.attempts)}, {"boost", runBoost_ == Boost::Surge ? "surge" : runBoost_ == Boost::Overclock ? "overclock" : "none"},
                        {"outfit", wallet_.active(Slot::Outfit).id}});
}

void Game::placeGate() {
    gatePassed_ = false; gateX_ = day_.bestDist; gateOpacity_ = 1;
    gateVisible_ = gateX_ > 15;
}

void Game::press() {
    if (screen_ != Screen::None) { if (primary_) primary_(); return; } // keyboard / generic tap = the screen's main action
    if (pendingStart_) return;
    if (state_ == State::Title) { start(); return; }
    if (state_ == State::Play) { lay(); return; }
    if (state_ == State::Dead && deadT_ > 0.8f && overDelay_ <= 0 && !continuePending_) start();
}

void Game::pressAt(float px, float py) {
    px /= uiScale_; py /= uiScale_; // platform points -> the HUD's virtual points
    // mute button: 44x44 pt, top-right, 16 pt margin
    const float bx = viewW_ - 16 - 22, by = safeTop_ + 16 + 22;
    if (screen_ == Screen::None && std::abs(px - bx) < 26 && std::abs(py - by) < 26) { toggleMute(); return; }
    for (size_t i = hits_.size(); i-- > 0;)
        if (hits_[i].r.hit(px, py)) { auto fn = hits_[i].fn; fn(); return; }
    if (screen_ != Screen::None) return; // modal: taps outside its controls do nothing
    press();
}

bool Game::tapButton(const std::string& label) {
    for (size_t i = hits_.size(); i-- > 0;)
        if (hits_[i].label.rfind(label, 0) == 0) { auto fn = hits_[i].fn; fn(); return true; }
    return false;
}

void Game::uiHit(glm::vec2 c, glm::vec2 size, std::function<void()> fn) {
    hits_.push_back({{c, size * 0.5f}, std::move(fn), nextHitLabel_});
    nextHitLabel_.clear();
}

// ---------- screens ----------
void Game::openScreen(Screen s) {
    if (screen_ == s) return;
    if (screen_ != Screen::None) screenStack_.push_back(screen_);
    screen_ = s;
    screenT_ = 0;
    if (s == Screen::Shop) track("iap_view", {});
}

void Game::closeScreen() {
    if (screen_ == Screen::Locker) { lockerPreview_.clear(); applyCosmetics(); }
    screen_ = screenStack_.empty() ? Screen::None : screenStack_.back();
    if (!screenStack_.empty()) screenStack_.pop_back();
    screenT_ = 0.35f; // the screen underneath doesn't replay its entrance
    primary_ = nullptr;
}

void Game::queueScreen(Screen s) {
    for (Screen q : screenQueue_) if (q == s) return;
    screenQueue_.push_back(s);
}

// ---------- rewarded ads ----------
bool Game::rewardReady(Placement p) const {
    return adPolicy_.canOffer(p) && svc_.ads->rewardedState() == RewardedState::Ready;
}

bool Game::requestReward(Placement p, std::function<void()> grant) {
    track("ad_offer_accepted", {{"placement", placementName(p)}});
    if (!adPolicy_.canOffer(p)) { showMessage("Come back later for more"); return false; }
    if (svc_.ads->rewardedState() != RewardedState::Ready || !svc_.ads->showRewarded(placementName(p))) {
        showMessage(svc_.ads->rewardedState() == RewardedState::Loading ? "Loading the video\u2026 try again in a moment" : "No video available right now");
        return false;
    }
    pendingReward_ = PendingReward{p, std::move(grant)};
    pendingRewardT_ = 0;
    return true;
}

void Game::pollAds() {
    if (svc_.ads->consumeReward() && pendingReward_) {
        const Placement p = pendingReward_->placement;
        auto grant = std::move(pendingReward_->grant);
        pendingReward_.reset();
        adPolicy_.rewardedWatched(p);
        if (state_ == State::Dead) adPolicy_.rewardedAtThisGameOver();
        track("ad_rewarded_complete", {{"placement", placementName(p)}});
        grant();
        return;
    }
    // closed early: no reward. The callback can arrive just after the dismiss, so allow a moment.
    if (pendingReward_ && !svc_.ads->adShowing()) {
        pendingRewardT_ += rdt_;
        if (pendingRewardT_ > 1.5f) { track("ad_rewarded_skipped", {{"placement", placementName(pendingReward_->placement)}}); pendingReward_.reset(); }
    }
}

// ---------- purchases ----------
void Game::startPurchase(const std::string& productId) {
    auto go = [this, productId] {
        track("iap_start", {{"product", productId}});
        if (!svc_.store->purchase(productId)) showMessage("The store isn\u2019t available right now");
    };
    if (profile_.child()) parentalGate(go); else go();
}

void Game::onPurchase(const PurchaseEvent& e) {
    const ProductDef* p = findProduct(e.productId);
    if (!p) return;
    track("iap_result", {{"product", e.productId}, {"result", e.result == PurchaseResult::Success ? "success" : e.result == PurchaseResult::Cancelled ? "cancelled"
                                                                     : e.result == PurchaseResult::Pending ? "pending" : "failed"},
                         {"restored", e.restored ? "1" : "0"}});
    if (e.result == PurchaseResult::Pending) { showMessage("Purchase pending approval"); return; }
    if (e.result != PurchaseResult::Success) { if (e.result == PurchaseResult::Failed) showMessage("Purchase didn\u2019t go through"); return; }
    adPolicy_.purchaseMade();
    const glm::vec2 mid{viewW_ / 2, viewH_ * 0.45f};
    switch (p->kind) {
    case ProductKind::Coins:
        wallet_.earn(p->coins, "iap");
        flyCoins(mid, p->coins, 18);
        toast("Thanks for your support!", Icon::Shop, p->coins);
        break;
    case ProductKind::RemoveAds:
        adPolicy_.setRemoveAds(true);
        if (!profile_.flag("noads_bonus")) {
            profile_.setFlag("noads_bonus", true);
            wallet_.earn(tune_.removeAdsBonus, "iap_noads_bonus");
            flyCoins(mid, tune_.removeAdsBonus, 12);
        }
        if (!e.restored) toast("Ads removed. Enjoy!", Icon::NoAds);
        break;
    case ProductKind::Starter:
        wallet_.grant("hen_glitch");
        if (!profile_.flag("starter_coins")) {
            profile_.setFlag("starter_coins", true);
            wallet_.earn(tune_.starterCoins, "iap_starter");
            flyCoins(mid, tune_.starterCoins, 18);
        }
        if (!e.restored) { wallet_.equip("hen_glitch"); applyCosmetics(); toast("Glitch Hen unlocked!", Icon::Star); }
        if (screen_ == Screen::Starter) closeScreen();
        break;
    }
    if (!e.restored) {
        hudBurst(mid, 40, {srgbColor("#ffd23a"), srgbColor("#ff2bd6"), srgbColor("#29e7ff"), srgbColor("#f4ff5a")}, 520.f);
        sfx(Sfx::SurgeStart); buzz(30);
        flashScreen(srgbColor("#ffd23a"), 0.25f);
    }
}

void Game::parentalGate(std::function<void()> then) {
    gateA_ = 6 + rng_.index(4); gateB_ = 7 + rng_.index(3);
    const int ans = gateA_ * gateB_;
    gateAnswer_ = rng_.index(4);
    int k = 0;
    for (int i = 0; i < 4; ++i) gateChoices_[size_t(i)] = i == gateAnswer_ ? ans : ans + (++k) * (rng_.next01() < 0.5f ? -1 : 1) * (3 + rng_.index(5));
    gateThen_ = std::move(then);
    openScreen(Screen::ParentalGate);
}

// ---------- cosmetics ----------
void Game::applyCosmetics() {
    const Cosmetic* preview = lockerPreview_.empty() ? nullptr : findCosmetic(lockerPreview_);
    hen_.skin = preview && preview->slot == Slot::Outfit ? preview->skin : wallet_.active(Slot::Outfit).skin;
    trail_ = preview && preview->slot == Slot::Trail ? preview->trail : wallet_.active(Slot::Trail).trail;
    crash_ = preview && preview->slot == Slot::Crash ? preview->crash : wallet_.active(Slot::Crash).crash;
}

void Game::toggleMute() {
    muted_ = !muted_;
    if (svc_.storage) svc_.storage->set("cluckstack-music", muted_ ? "off" : "on");
    svc_.audio->setMuted(muted_);
}

void Game::lay() {
    if (meter_ < 1) { sfx(Sfx::Empty); meterShake_ = 0.3f; return; }
    meter_ -= 1; prevFull_ = static_cast<int>(std::floor(meter_));
    const float ty = float(baseY_) + eggs_.size() * Uf;
    Egg e;
    e.pos = {float(x_), ty + Uf * 0.5f, 0};
    e.rot = {rng_.range(-0.08f, 0.08f), rng_.range(0, 6.28f), rng_.range(-0.08f, 0.08f)};
    e.sway = rng_.next01() * 6; e.ring = static_cast<int>(eggs_.size()) % 2;
    eggs_.push_back(e);
    hopV_ = 2.6f; flap_ = 1; squash_ = -0.2f;
    sfx(Sfx::Lay, static_cast<int>(eggs_.size()) - 1); buzz(8);
    run_.eggs++;
    missionProgress(missions_.record(MissionKind::Eggs));
    const glm::vec3 col = eggs_.size() % 2 ? C().cyan : C().pink;
    rings_.spawn({float(x_), ty + 0.03f, 0}, col, 0.85f, 0.32f, false);
    for (int i = 0; i < 16; ++i) {
        const float a = i / 16.f * 2 * kPi, s = rng_.range(2.2f, 3.6f);
        fxAdd_.emit({float(x_), ty + 0.05f, 0}, {std::cos(a) * s, rng_.range(0.2f, 1.2f), std::sin(a) * s}, rng_.range(0.22f, 0.38f), 0.09f, 0,
                    i % 3 ? col : C().white, 1, 4, 2);
    }
    chromaKick_ = std::max(chromaKick_, 0.22f);
    if (rng_.next01() < 0.6f)
        spawnFeathers({float(x_) - 0.1f, hen_.nodes[hen_.root].pos.y + 0.5f, 0}, 1 + static_cast<int>(rng_.next01() * 2), 0.5f);
}

void Game::knock(int k) {
    for (int i = 0; i < k && !eggs_.empty(); ++i) {
        const Egg e = eggs_.front();
        eggs_.erase(eggs_.begin());
        const glm::vec3 p = e.pos;
        debris_.push_back({DebrisKind::Egg, p, e.rot, e.scale, {-speed_ * 0.5f - rng_.range(0.8f, 2.4f), rng_.range(2.5f, 5.5f), rng_.range(-1.6f, 1.6f)},
                           {rng_.range(-10, 10), rng_.range(-4, 4), rng_.range(-10, 10)}, 3, 0, e.ring});
        spawnShards(p, 4, -speed_ * 0.4f);
        burst(p + glm::vec3(0.2f, 0, 0), 26, {C().cyan, C().white, C().pink}, 2.5f, 7, 0.5f, 0.08f, 0, -speed_ * 0.3f);
        puff({p.x + 0.2f, p.y - 0.2f, 0}, 3, C().smoke, 1, 0.45f);
        rings_.spawn({p.x + 0.25f, p.y, 0.7f}, C().pink, 0.7f, 0.28f, true);
    }
    sfx(Sfx::Crack); buzz(18);
    shake_ = std::min(0.32f, 0.1f + k * 0.05f); camKick_ = 0.16f + k * 0.06f;
    freeze_ = 0.055f; chromaKick_ = std::max(chromaKick_, 0.75f);
}

void Game::perfect(float top) {
    streak_++;
    run_.perfects++;
    missionProgress(missions_.record(MissionKind::Perfects));
    const int pts = 3 * streak_;
    bonus_ += pts;
    popup((streak_ > 1 ? "PERFECT \u00d7" + std::to_string(streak_) : std::string("PERFECT")) + " +" + std::to_string(pts), {float(x_), top + 1.2f, 0}, PopKind::Perfect);
    sfx(Sfx::Perfect, std::min(streak_ - 1, 6) * 2);
    rings_.spawn({float(x_), top + 0.02f, 0}, C().pink, 1.8f + std::min(streak_, 5) * 0.35f, 0.5f, false);
    if (streak_ >= 2) rings_.spawn({float(x_), top + 0.6f, 0.3f}, C().volt, 1.2f + streak_ * 0.3f, 0.45f, true);
    burst({float(x_), top + 0.2f, 0}, 34 + std::min(streak_, 6) * 10, {C().pink, C().volt, C().white}, 2, 6.f + streak_, 0.7f, 0.1f, 1.2f, 0, false, 2.5f, 5);
    if (streak_ >= 3) { flashScreen(srgbColor("#ff2bd6"), 0.16f); chromaKick_ = std::max(chromaKick_, 0.6f); }
}

void Game::closeCall(float top) {
    bonus_ += 2;
    run_.closeCalls++;
    missionProgress(missions_.record(MissionKind::CloseCalls));
    popup("CLOSE CALL +2", {float(x_), top + 0.9f, 0}, PopKind::Perfect);
    sfx(Sfx::Perfect, 7); timeScale_ = std::min(timeScale_, 0.4f);
    rings_.spawn({float(x_), top, 0.3f}, C().white, 1.4f, 0.4f, true);
    burst({float(x_), top, 0}, 22, {C().white, C().cyan}, 2, 5, 0.4f, 0.07f, 0, 0, false, 2.5f, 3);
}

void Game::die(bool ceiling) {
    state_ = State::Dead; deadT_ = 0;
    surging_ = false; graceT_ = 0; surgeSpeedMul_ = 1; beat_.setParty(false);
    glm::vec3 p = hen_.nodes[hen_.root].pos; p.y += 0.55f;
    if (ceiling) { henV_ = {-1.5f - speed_ * 0.2f, -3, rng_.range(1, 2.5f)}; henW_ = {rng_.range(-3, 3), rng_.range(-4, 4), -6}; }
    else { henV_ = {-2.5f - speed_ * 0.3f, 6.5f, rng_.range(1, 2.5f)}; henW_ = {rng_.range(-3, 3), rng_.range(-4, 4), 7}; }
    // the base crash: the hen shatters like glass (the procedural hen, without the model, bursts into feathers);
    // the equipped crash effect (crashFx) plays on top
    if (hen_.canShatter()) {
        hen_.shatter(p, henV_ * 0.6f, level_.heightAt(p.x) * Uf, uint32_t(time_ * 1000.f));
        burst(p, 160, {glow("#e6fbff", 3.5f), glow("#9ff6ff", 3.f), C().white}, 3, 12, 1.2f, 0.05f, 0.8f, 0, false, 1.4f, 7); // glass dust
        burst(p, 24, {glow("#ffffff", 6.f)}, 1, 4, 0.25f, 0.35f, 0.2f, 0, false, 2.f, 0);                                     // glints
        rings_.spawn(p, glow("#9ff6ff", 2.5f), 3.f, 0.5f, true);
    } else if (crash_ != CrashFx::Feathers) {
        spawnFeathers(p, 34, 1.3f);
    }
    burst(p, 110, {C().cyan, C().pink, C().white, C().volt}, 3, 11, 0.9f, 0.11f, 0, 0, false, 1.8f, 6);
    puff({p.x, p.y - 0.3f, 0}, 18, C().smoke, 2, 0.6f);
    rings_.spawn({p.x, p.y, 0.6f}, C().pink, 3.4f, 0.6f, true); rings_.spawn({p.x, p.y, 0.6f}, C().cyan, 2.2f, 0.45f, true);
    flashScreen(srgbColor("#ff2bd6"), 0.5f);
    while (!eggs_.empty()) {
        const Egg e = eggs_.back(); eggs_.pop_back();
        debris_.push_back({DebrisKind::Egg, e.pos, e.rot, e.scale, {rng_.range(-3, 1), rng_.range(2, 6), rng_.range(-2, 2)},
                           {rng_.range(-10, 10), rng_.range(-4, 4), rng_.range(-10, 10)}, 3, 0, e.ring});
    }
    sfx(Sfx::Squawk); buzz(60); shake_ = 0.55f;
    beat_.stop(); svc_.audio->musicStop(true);
    timeScale_ = 0.2f; chromaKick_ = 1.5f;
    crashFx(p);
    run_.seconds = std::max(run_.seconds, 0.f);
    // Continue? (once with an ad or coins, a second time with more coins); otherwise the run is over now
    const int cost = run_.continues == 0 ? tune_.continueCost : tune_.continueCost2;
    const bool adOk = run_.continues == 0 && rewardReady(Placement::Continue);
    continuePending_ = run_.continues < 2 && (adOk || wallet_.canAfford(cost));
    continueOffered_ = false;
    overDelay_ = 1e6f; // results stay hidden until the run is really over (finishRun sets the real delay)
    if (continuePending_) track("ad_offer_shown", {{"placement", "continue"}, {"ad", adOk ? "1" : "0"}});
    else finishRun();
}

void Game::revive() {
    if (screen_ == Screen::Continue) closeScreen();
    continuePending_ = continueOffered_ = false;
    run_.continues++;
    state_ = State::Play; deadT_ = 0; overDelay_ = -1;
    eggs_.clear();
    meter_ = MAXE; prevFull_ = MAXE;
    baseY_ = std::max(level_.heightAt(x_ - 0.22), level_.heightAt(x_ + 0.22)) * U; vy_ = 0; hop_ = 0; hopV_ = 0;
    hen_.nodes[hen_.root].pos = {float(x_), float(baseY_), 0};
    hen_.nodes[hen_.root].rot = glm::vec3(0.f);
    hen_.unshatter();
    graceT_ = 2.2f;   // surge grace: smashes the wall that killed her, so no instant second death
    timeScale_ = 1; freeze_ = 0; danger_ = 0; shake_ = 0.3f;
    beat_.start(); svc_.audio->musicStart();
    const glm::vec3 p{float(x_), float(baseY_) + 0.6f, 0};
    rings_.spawn(p, C().cyan, 3.2f, 0.6f, true); rings_.spawn(p, C().pink, 2.2f, 0.5f, true);
    burst(p, 90, {C().cyan, C().white, C().volt}, 3, 10, 0.8f, 0.1f, 0.6f, 0, false, 2, 3);
    flashScreen(srgbColor("#29e7ff"), 0.35f); chromaKick_ = 1.2f;
    popup("REBOOTED", p + glm::vec3(2.2f, 1.6f, 0), PopKind::Surge);
    sfx(Sfx::SurgeStart); buzz(40);
}

void Game::finishRun() {
    continuePending_ = false;
    const bool isBest = score_ > best_, isDayBest = score_ > day_.best;
    if (isDayBest) day_.best = score_;
    const double pbBefore = day_.bestDist;
    dda_.record(float(x_), float(pbBefore));
    if (x_ > day_.bestDist) day_.bestDist = x_;
    saveDay();
    if (isBest) { best_ = score_; if (svc_.storage) svc_.storage->set("cluckstack-best", std::to_string(best_)); }
    finalScore_ = score_; finalDist_ = static_cast<int>(std::floor(x_));
    newBestRun_ = (isBest || isDayBest) && score_ > 0;
    {
        const double gap = std::ceil(pbBefore - x_);
        std::string hook;
        if (pbBefore <= 0) hook = "First run of today\u2019s course";
        else if (x_ > pbBefore) hook = x_ - pbBefore < 1 ? std::string("New furthest today \u00b7 by a hair")
                                                         : "New furthest today \u00b7 +" + std::to_string(static_cast<int>(std::floor(x_ - pbBefore))) + "m";
        else if (gap <= 25) hook = "So close \u00b7 " + std::to_string(static_cast<int>(gap)) + "m short of your best";
        else hook = std::to_string(static_cast<int>(std::floor(day_.bestDist))) + "m furthest today";
        overNote_ = "Attempt " + std::to_string(day_.attempts) + " \u00b7 " + hook;
        static const char* kLines[] = {"Flatlined", "Signal lost", "Scrambled", "Fried circuits"};
        overTag_ = isBest && score_ > 0 ? "New all-time best" : isDayBest && score_ > 0 ? "New daily best" : kLines[rng_.index(4)];
    }
    // rewards: coins and XP for the distance, mission progress
    runCoins_ = std::max(1, finalDist_ / tune_.metersPerCoin + run_.surges * tune_.coinsPerSurge); // every run pays something
    runCoins_ = int(std::lround(runCoins_ * tune_.eventCoinMult));                                   // live event
    coinsDoubled_ = false;
    if (runCoins_ > 0) wallet_.earn(runCoins_, "run");
    runXp_ = finalDist_ * tune_.xpPerMeter;
    levelFrom_ = prog_.level();
    xpFrom_ = prog_.fraction();
    missionProgress(missions_.record(MissionKind::Runs));
    missionProgress(missions_.recordDistance(finalDist_));
    levelGained(prog_.add(runXp_));
    if (!profile_.child() && finalDist_ > 0) svc_.leaderboards->submit(finalDist_);
    if (beatenPending_ && challenge_ && !challenge_->id.empty()) svc_.backend->challengeBeaten(challenge_->id, finalDist_);
    beatenPending_ = false;
    adPolicy_.runFinished();
    adPolicy_.newGameOver();
    wallet_.runFinished();
    track("run_end", {{"distance", std::to_string(finalDist_)}, {"score", std::to_string(score_)}, {"seconds", std::to_string(int(run_.seconds))},
                      {"attempt", std::to_string(day_.attempts)}, {"new_best", newBestRun_ ? "1" : "0"}, {"surges", std::to_string(run_.surges)},
                      {"continues", std::to_string(run_.continues)}, {"coins", std::to_string(runCoins_)}, {"boosted", run_.boosted ? "1" : "0"}});
    overDelay_ = continueOffered_ ? 0.2f : 0.9f; overT_ = 0;
    continueOffered_ = false;
    if (newBestRun_ && !profile_.child() && !profile_.flag("replay_asked") && svc_.replay->state() != ReplayState::Unavailable) {
        profile_.setFlag("replay_asked", true);
        queueScreen(Screen::ReplayPrimer);
        track("replay_primer_shown", {});
    } else if (newBestRun_ && !profile_.child() && profile_.sessions() >= 2 && !profile_.flag("notif_asked") &&
        svc_.notifications->permission() != NotifPermission::Denied) {
        profile_.setFlag("notif_asked", true);
        queueScreen(Screen::NotifPrimer);
        track("notif_primer_shown", {});
    }
    rescheduleReminders();
}

void Game::levelGained(int levels) {
    if (levels <= 0) return;
    levelUps_ += levels;
    chestCoins_ = tune_.levelChestCoins + 10 * (prog_.level() - 1);
    chestOpened_ = chestDoubled_ = false;
    for (const Cosmetic& c : catalog())
        if (c.unlock == Unlock::Level && c.level <= prog_.level() && !wallet_.owns(c.id)) {
            wallet_.grant(c.id);
            toast(std::string("Unlocked: ") + c.name, Icon::Star);
        }
    track("level_up", {{"level", std::to_string(prog_.level())}});
    queueScreen(Screen::LevelUp);
}

void Game::onNotificationsAccepted() {
    profile_.setFlag("notif", true);
    svc_.notifications->requestPermission(); // the OS prompt; reminders are planned once it's granted
    rescheduleReminders();
}

void Game::missionProgress(int mask) {
    for (int i = 0; i < 3; ++i) {
        if (!(mask & (1 << i))) continue;
        const Mission& m = missions_.list()[size_t(i)];
        const int coins = int(std::lround(tune_.missionReward * tune_.eventCoinMult));
        wallet_.earn(coins, "mission");
        toast(Missions::describe(m), Icon::Check, coins);
        track("mission_complete", {{"kind", std::to_string(int(m.kind))}, {"target", std::to_string(m.target)}});
        levelGained(prog_.add(tune_.xpPerMission));
    }
}

// ---------- cosmetic effects ----------
void Game::emitTrail(float dt) {
    if (trail_ == Trail::None) return;
    const Hen::Node& root = hen_.nodes[hen_.root];
    const glm::vec3 at{root.pos.x - 0.32f, root.pos.y + 0.55f, 0.f};
    // showroom (not running): the trail is blown back at run speed so it streams off the tail as in a run, and it is
    // denser, bigger and longer-lived so it can be judged at a glance
    const bool show = state_ != State::Play;
    const glm::vec3 wind = show ? glm::vec3(-6.f, 0.f, 0.f) : glm::vec3(0.f);
    const float big = show ? 1.8f : 1.f, longer = show ? 1.6f : 1.f;
    trailAcc_ += dt * (trail_ == Trail::Rainbow ? 70.f : 50.f) * (show ? 2.5f : 1.f);
    auto emit = [&](glm::vec3 p, glm::vec3 v, float life, float s0, float s1, glm::vec3 col, float a, float drag, float grav) {
        fxAdd_.emit(p, v + wind, life * longer, s0 * big, s1 * big, col, a, drag, grav);
    };
    while (trailAcc_ >= 1.f) {
        trailAcc_ -= 1.f;
        const glm::vec3 j{rng_.range(-0.08f, 0.08f), rng_.range(-0.12f, 0.12f), rng_.range(-0.1f, 0.1f)};
        switch (trail_) {
        case Trail::Sparks:
            emit(at + j, {rng_.range(-2.4f, -1.f), rng_.range(-0.3f, 0.8f), rng_.range(-0.3f, 0.3f)}, rng_.range(0.25f, 0.45f), 0.06f, 0,
                        rng_.next01() < 0.5f ? C().cyan : C().pink, 1, 2, 3);
            break;
        case Trail::Rainbow:
            emit(at + j, {rng_.range(-1.6f, -0.8f), rng_.range(-0.1f, 0.2f), 0}, rng_.range(0.5f, 0.7f), 0.12f, 0.02f,
                        hueColor(time_ * 0.8f + j.y * 3.f) * 2.6f, 0.9f, 1, 0);
            break;
        case Trail::Fire:
            emit(at + j, {rng_.range(-1.8f, -0.6f), rng_.range(0.6f, 1.6f), rng_.range(-0.2f, 0.2f)}, rng_.range(0.3f, 0.5f), 0.1f, 0.0f,
                        rng_.next01() < 0.6f ? C().ember : C().volt, 1, 2, -2);
            break;
        case Trail::Gold:
            emit(at + j, {rng_.range(-1.2f, -0.4f), rng_.range(-0.2f, 0.5f), rng_.range(-0.3f, 0.3f)}, rng_.range(0.5f, 0.9f), 0.05f, 0.02f,
                        glow("#ffd23a", 2.8f), 1, 1, 1);
            break;
        case Trail::Glitch: {
            const bool red = rng_.next01() < 0.5f;
            emit(at + j + glm::vec3(red ? -0.04f : 0.04f, 0, 0), {rng_.range(-2.f, -1.2f), 0, 0}, rng_.range(0.15f, 0.3f), 0.09f, 0,
                        red ? C().red : C().cyan, 1, 0, 0);
            break;
        }
        default: break;
        }
    }
}

// The outfit's signature effect, worn on the hen: emitted from points on its body and head, carrying most of the
// hen's own velocity so it stays wrapped around the hen at full run speed (and streams off behind it).
void Game::emitAura(float dt) {
    const Hen::Node& root = hen_.nodes[hen_.root];
    glm::vec3 hv = dt > 0.f ? (root.pos - auraPrev_) / dt : glm::vec3(0.f);
    auraPrev_ = root.pos;
    if (glm::length(hv) > 40.f) hv = glm::vec3(0.f); // a reset / teleport, not motion
    const Aura aura = hen_.skin.aura;
    if (aura == Aura::None || dt <= 0.f) return;

    constexpr float S = 1.1f; // hen.root scale
    const glm::vec3 body = root.pos + glm::vec3(0.f, 0.6f, 0.f) * S, head = root.pos + glm::vec3(0.33f, 1.05f, 0.f) * S;
    struct Pt { glm::vec3 p, n; };
    // a random point on the hen (body ellipsoid, sometimes the head) and its outward normal; `out` pushes it off the skin
    auto surface = [&](float out = 1.f, float headShare = 0.28f) {
        const float u = rng_.range(0.f, 2 * kPi), v = rng_.range(-1.f, 1.f), r = std::sqrt(1.f - v * v);
        const glm::vec3 n{r * std::cos(u), v, r * std::sin(u)};
        if (rng_.next01() < headShare) return Pt{head + n * glm::vec3(0.22f, 0.22f, 0.2f) * out, n};
        return Pt{body + n * glm::vec3(0.46f, 0.5f, 0.38f) * out, n};
    };
    auto ride = [&](glm::vec3 v, float k = 0.9f) { return v + hv * k; };
    auto count = [&](int layer, float perSecond) {
        auraAcc_[layer] += perSecond * dt;
        const int n = int(auraAcc_[layer]);
        auraAcc_[layer] -= float(n);
        return n;
    };
    auto pick = [&](std::initializer_list<glm::vec3> cs) { return cs.begin()[int(rng_.next01() * cs.size()) % int(cs.size())]; };
    auto R = [&](float a, float b) { return rng_.range(a, b); };
    // signature glow: one big soft light in the outfit's colour wrapped around the hen, flickering
    auto halo = [&](glm::vec3 col, float alpha) {
        fxAdd_.emit(body + glm::vec3(0, 0.15f, 0), ride({0, 0, 0}, 1.f), 0.06f, R(1.9f, 2.3f), 2.1f, col, alpha * R(0.8f, 1.15f), 0.f, 0.f);
    };

    switch (aura) {
    case Aura::Magma: { // on fire: licking flames, rising embers, a smoke plume and the odd flare
        static const glm::vec3 core = glow("#ffd86a", 2.6f), flame = glow("#ff7a1a", 2.2f), deep = glow("#ff3b1a", 1.8f), spark = glow("#ffb347", 4.5f);
        // heat glow: one big soft orange light wrapped around the hen, flickering
        halo(glow("#ff5a1a", 1.f), 0.15f);
        // flames: big overlapping tongues that start hot (yellow) at the skin and redden and shrink as they rise
        for (int i = count(0, 460); i-- > 0;) {
            Pt s = surface(0.85f, 0.3f);
            if (s.n.y < -0.3f) s.n.y = -s.n.y; // flames climb the hen, they don't hang under it
            const float r = rng_.next01();
            const glm::vec3 c = r < 0.3f ? core : r < 0.75f ? flame : deep;
            fxAdd_.emit(s.p, ride({s.n.x * 0.4f + R(-0.25f, 0.25f), R(1.5f, 3.f), s.n.z * 0.4f}), R(0.3f, 0.55f), R(0.24f, 0.36f), 0.04f,
                        c, R(0.35f, 0.55f), 1.5f, -3.5f); // many soft, dim blobs blend into one fire
        }
        for (int i = count(1, 45); i-- > 0;) {
            const Pt s = surface(1.f);
            fxAdd_.emit(s.p, ride({R(-1.f, 1.f), R(1.6f, 3.4f), R(-0.8f, 0.8f)}, 0.6f), R(0.7f, 1.4f), R(0.035f, 0.055f), 0.01f, spark, 1, 1.2f, -0.8f);
        }
        for (int i = count(2, 14); i-- > 0;)
            fxSmoke_.emit(head + glm::vec3(R(-0.15f, 0.15f), 0.25f, R(-0.1f, 0.1f)), ride({R(-0.3f, 0.3f), R(1.f, 1.6f), R(-0.2f, 0.2f)}, 0.5f),
                          R(1.f, 1.6f), 0.18f, 0.65f, hexColor("#24120e"), 0.5f, 1.f, -0.4f);
        for (int i = count(3, 2.5f); i-- > 0;) { // flare: a quick burst of fire off one spot
            const Pt s = surface(1.f);
            for (int k = 0; k < 18; ++k)
                fxAdd_.emit(s.p, ride({s.n.x * R(1.f, 2.5f), R(1.5f, 3.5f), s.n.z * R(1.f, 2.5f)}), R(0.25f, 0.45f), R(0.1f, 0.16f), 0.f,
                            pick({core, flame, spark}), 1, 3.f, -2.f);
        }
        break;
    }
    case Aura::Cryo: { // frozen: frost mist rolling off, ice glitter clinging, snow falling away
        static const glm::vec3 ice = glow("#cfeaff", 3.f), cyan = glow("#29e7ff", 3.2f), white = glow("#ffffff", 3.5f);
        halo(glow("#7fd6ff", 1.f), 0.14f);
        for (int i = count(0, 22); i-- > 0;) {
            const Pt s = surface(1.f, 0.15f);
            fxSmoke_.emit(s.p, ride({s.n.x * 0.5f, R(-0.5f, -0.1f), s.n.z * 0.5f}, 0.7f), R(0.9f, 1.4f), 0.25f, 0.8f, hexColor("#bfe4ff"), 0.5f, 1.5f, 0.3f);
        }
        for (int i = count(1, 170); i-- > 0;) {
            const Pt s = surface(1.04f);
            fxAdd_.emit(s.p, ride({R(-0.15f, 0.15f), R(-0.1f, 0.25f), R(-0.15f, 0.15f)}, 0.97f), R(0.35f, 0.8f), R(0.07f, 0.12f), 0.f, pick({ice, cyan, white}), 1, 2.f, 0.f);
        }
        for (int i = count(2, 30); i-- > 0;) {
            const Pt s = surface(1.1f);
            fxAdd_.emit(s.p, ride({R(-0.4f, 0.4f), R(-0.2f, 0.4f), R(-0.4f, 0.4f)}, 0.5f), R(0.8f, 1.3f), 0.085f, 0.05f, ice, 0.9f, 1.f, 2.5f);
        }
        break;
    }
    case Aura::Toxic: { // radioactive: bubbling ooze, dripping slime, green gas
        static const glm::vec3 lime = glow("#c6ff4a", 3.f), green = glow("#2bff9a", 3.f), pink = glow("#ff2bd6", 2.6f);
        halo(glow("#6aff3a", 1.f), 0.14f);
        for (int i = count(0, 120); i-- > 0;) {
            const Pt s = surface(1.f);
            fxAdd_.emit(s.p, ride({s.n.x * 0.3f, R(0.6f, 1.4f), s.n.z * 0.3f}), R(0.5f, 0.9f), R(0.08f, 0.12f), R(0.16f, 0.24f), pick({lime, green, green}), 0.9f, 2.f, -0.5f);
        }
        for (int i = count(1, 30); i-- > 0;) {
            Pt s = surface(0.95f, 0.f);
            s.p.y = body.y - 0.45f;
            fxAdd_.emit(s.p, ride({0, R(-0.3f, 0.f), 0}, 0.8f), R(0.5f, 0.8f), 0.11f, 0.06f, lime, 1, 0.5f, 9.f);
        }
        for (int i = count(2, 12); i-- > 0;)
            fxSmoke_.emit(surface(1.f).p, ride({R(-0.4f, 0.4f), R(0.3f, 0.8f), R(-0.4f, 0.4f)}, 0.6f), R(1.f, 1.5f), 0.15f, 0.6f, hexColor("#3f8a2a"), 0.35f, 1.f, -0.2f);
        for (int i = count(3, 10); i-- > 0;) // the odd magenta spark (the outfit's accent)
            fxAdd_.emit(surface(1.05f).p, ride({R(-1.5f, 1.5f), R(0.5f, 2.f), R(-1.f, 1.f)}), 0.3f, 0.06f, 0.f, pink, 1, 3.f, 2.f);
        break;
    }
    case Aura::Gold: { // rich: a cloud of gold glitter, star twinkles, gold dust raining off
        static const glm::vec3 gold = glow("#ffd23a", 3.6f), pale = glow("#fff2b0", 3.6f), star = glow("#ffffff", 5.f);
        halo(glow("#ffc21a", 1.f), 0.14f);
        for (int i = count(0, 220); i-- > 0;) {
            const Pt s = surface(R(1.f, 1.25f));
            fxAdd_.emit(s.p, ride({R(-0.2f, 0.2f), R(-0.1f, 0.3f), R(-0.2f, 0.2f)}, 0.95f), R(0.3f, 0.7f), R(0.05f, 0.08f), 0.f, pick({gold, gold, pale}), 1, 2.f, 0.f);
        }
        for (int i = count(1, 14); i-- > 0;) // twinkles: big, very bright, gone in a blink
            fxAdd_.emit(surface(1.08f).p, ride({0, 0, 0}, 1.f), 0.22f, 0.42f, 0.f, star, 1, 0.f, 0.f);
        for (int i = count(2, 40); i-- > 0;)
            fxAdd_.emit(surface(1.f, 0.1f).p, ride({R(-0.5f, 0.5f), R(0.f, 0.6f), R(-0.5f, 0.5f)}, 0.5f), R(0.8f, 1.2f), 0.075f, 0.04f, gold, 1, 1.f, 5.f);
        break;
    }
    case Aura::Chrome: { // liquid metal: mirror glints, mercury drips, cyan / magenta reflections
        static const glm::vec3 glint = glow("#ffffff", 5.f), silver = glow("#dfe5f0", 2.f), cyan = glow("#29e7ff", 3.f), pink = glow("#ff2bd6", 3.f);
        halo(rng_.next01() < 0.5f ? glow("#29e7ff", 1.f) : glow("#ff2bd6", 1.f), 0.1f);
        for (int i = count(0, 34); i-- > 0;)
            fxAdd_.emit(surface(1.02f).p, ride({0, 0, 0}, 1.f), R(0.12f, 0.22f), R(0.3f, 0.4f), 0.f, glint, 1, 0.f, 0.f);
        for (int i = count(1, 26); i-- > 0;) {
            Pt s = surface(0.95f, 0.f);
            s.p.y = body.y - R(0.35f, 0.5f);
            fxAdd_.emit(s.p, ride({0, R(-0.4f, 0.f), 0}, 0.85f), R(0.45f, 0.7f), 0.11f, 0.07f, silver, 1, 0.5f, 10.f);
        }
        for (int i = count(2, 100); i-- > 0;) {
            const Pt s = surface(1.03f);
            fxAdd_.emit(s.p, ride({s.n.x * 0.3f, s.n.y * 0.3f, s.n.z * 0.3f}, 0.95f), R(0.25f, 0.45f), 0.08f, 0.f, rng_.next01() < 0.5f ? cyan : pink, 0.9f, 2.f, 0.f);
        }
        break;
    }
    case Aura::Tiger: { // electric: crackling sparks off the skin and arc bursts
        static const glm::vec3 orange = glow("#ff8a1a", 3.6f), pink = glow("#ff2bd6", 3.6f), white = glow("#ffffff", 4.f);
        halo(std::fmod(time_, 0.5f) < 0.25f ? glow("#ff6a1a", 1.f) : glow("#ff2bd6", 1.f), 0.13f);
        for (int i = count(0, 240); i-- > 0;) {
            const Pt s = surface(1.f);
            const float sp = R(2.5f, 5.f);
            fxAdd_.emit(s.p, ride(s.n * sp + glm::vec3(R(-1, 1), R(-1, 1), R(-1, 1))), R(0.1f, 0.22f), R(0.07f, 0.1f), 0.01f, pick({orange, pink, orange, white}), 1, 7.f, 0.f);
        }
        for (int i = count(1, 6.f); i-- > 0;) { // an arc: a jagged line of sparks between two points on the hen
            const glm::vec3 a = surface(1.05f).p, b = surface(1.05f).p;
            for (int k = 0; k <= 14; ++k) {
                const float t = k / 14.f;
                const glm::vec3 j{R(-0.06f, 0.06f), R(-0.06f, 0.06f), R(-0.06f, 0.06f)};
                fxAdd_.emit(glm::mix(a, b, t) + j * std::sin(t * kPi) * 2.f, ride({0, 0, 0}, 1.f), R(0.08f, 0.14f), 0.11f, 0.03f, k % 3 ? white : pink, 1, 0.f, 0.f);
            }
        }
        break;
    }
    case Aura::Holo: { // hologram: pixels drifting off, a scan line sweeping up through the hen
        static const glm::vec3 cyan = glow("#9ff6ff", 3.f), deep = glow("#29e7ff", 3.2f), white = glow("#ffffff", 3.6f);
        halo(glow("#29e7ff", 1.f), 0.13f);
        for (int i = count(0, 140); i-- > 0;) {
            const Pt s = surface(1.02f);
            fxAdd_.emit(s.p, ride({R(-0.1f, 0.1f), R(0.7f, 1.6f), R(-0.1f, 0.1f)}), R(0.4f, 0.8f), 0.09f, 0.09f, pick({cyan, deep}), 0.85f, 0.f, 0.f);
        }
        {   // scan line: a ring of light rising through the body every 1.4 s
            const float k = std::fmod(time_ / 1.4f, 1.f), y = body.y - 0.6f + k * 1.55f;
            for (int i = count(1, 260); i-- > 0;) {
                const float u = R(0.f, 2 * kPi), rr = std::sqrt(std::max(0.f, 1.f - std::pow((y - body.y) / 0.62f, 2.f))) * 0.5f + 0.08f;
                fxAdd_.emit({body.x + std::cos(u) * rr, y, body.z + std::sin(u) * rr * 0.8f}, ride({0, 0, 0}, 1.f), 0.12f, 0.08f, 0.05f, white, 0.9f, 0.f, 0.f);
            }
        }
        for (int i = count(2, 8); i-- > 0;) { // glitch tear: a short horizontal smear
            const Pt s = surface(1.05f);
            for (int k = 0; k < 8; ++k) fxAdd_.emit(s.p + glm::vec3(k * 0.05f - 0.2f, 0, 0), ride({0, 0, 0}, 1.f), 0.1f, 0.05f, 0.05f, deep, 1, 0.f, 0.f);
        }
        break;
    }
    case Aura::Sakura: { // spring: petals peeling off and drifting away, pink sparkles
        static const glm::vec3 petal = glow("#ff9fe0", 2.2f), blush = glow("#ffd6ef", 2.2f), spark = glow("#ff7ae6", 3.4f);
        halo(glow("#ff7ae6", 1.f), 0.13f);
        for (int i = count(0, 60); i-- > 0;) {
            const Pt s = surface(1.05f, 0.35f);
            fxAdd_.emit(s.p, ride({R(-1.4f, -0.3f), R(0.2f, 1.f), R(-0.6f, 0.6f)}, 0.6f), R(1.4f, 2.2f), R(0.13f, 0.18f), 0.1f, rng_.next01() < 0.6f ? petal : blush, 0.95f, 1.6f, 0.9f);
        }
        for (int i = count(1, 50); i-- > 0;)
            fxAdd_.emit(surface(1.08f).p, ride({R(-0.2f, 0.2f), R(0.f, 0.4f), R(-0.2f, 0.2f)}, 0.95f), R(0.3f, 0.6f), 0.07f, 0.f, spark, 1, 2.f, 0.f);
        break;
    }
    case Aura::Vapor: { // vaporwave: pastel orbs floating up, a soft pink / cyan haze
        static const glm::vec3 pink = glow("#ff9ce6", 2.6f), cyan = glow("#7ff0ff", 2.6f), yellow = glow("#f4ff5a", 2.6f), violet = glow("#9a5bff", 3.f);
        halo(std::sin(time_ * 1.5f) > 0.f ? glow("#ff9ce6", 1.f) : glow("#7ff0ff", 1.f), 0.13f);
        for (int i = count(0, 80); i-- > 0;) {
            const Pt s = surface(R(1.f, 1.2f));
            fxAdd_.emit(s.p, ride({R(-0.3f, 0.3f), R(0.4f, 1.f), R(-0.3f, 0.3f)}, 0.8f), R(0.9f, 1.5f), R(0.14f, 0.22f), 0.03f, pick({pink, cyan, yellow, violet}), 0.85f, 1.2f, -0.2f);
        }
        for (int i = count(1, 10); i-- > 0;)
            fxSmoke_.emit(surface(1.f).p, ride({R(-0.3f, 0.3f), R(0.2f, 0.5f), R(-0.3f, 0.3f)}, 0.7f), R(1.f, 1.5f), 0.2f, 0.6f,
                          rng_.next01() < 0.5f ? hexColor("#ff9ce6") : hexColor("#7ff0ff"), 0.25f, 1.f, 0.f);
        break;
    }
    case Aura::Midnight: { // the night sky: stardust swirling around the hen and shooting stars
        static const glm::vec3 violet = glow("#9a5bff", 3.4f), blue = glow("#5b8cff", 3.2f), white = glow("#ffffff", 4.f);
        halo(glow("#6a3bff", 1.f), 0.15f);
        for (int i = count(0, 180); i-- > 0;) {
            const Pt s = surface(R(1.05f, 1.35f));
            const glm::vec3 swirl = glm::normalize(glm::cross(s.n, glm::vec3(0, 1, 0)) + glm::vec3(0.001f)) * R(0.6f, 1.2f);
            fxAdd_.emit(s.p, ride(swirl, 0.95f), R(0.5f, 1.f), R(0.06f, 0.09f), 0.f, pick({violet, blue, violet, white}), 1, 1.f, 0.f);
        }
        for (int i = count(1, 5.f); i-- > 0;) { // shooting star: a bright streak arcing off the hen
            const Pt s = surface(1.1f);
            const glm::vec3 v{R(-3.f, -1.5f), R(1.f, 2.5f), R(-0.5f, 0.5f)};
            for (int k = 0; k < 10; ++k)
                fxAdd_.emit(s.p - v * (k * 0.012f), ride(v, 0.7f), 0.45f - k * 0.025f, 0.15f - k * 0.01f, 0.f, k ? violet : white, 1, 0.5f, 1.f);
        }
        break;
    }
    case Aura::Glitch: { // glitch: hue-cycling shards that jump and tear, with an RGB split
        halo(hueColor(time_ * 0.9f), 0.13f);
        for (int i = count(0, 200); i-- > 0;) {
            const Pt s = surface(R(1.f, 1.2f));
            const glm::vec3 jump{std::round(R(-2.f, 2.f)) * 0.12f, std::round(R(-1.f, 1.f)) * 0.06f, 0.f};
            const glm::vec3 c = hueColor(time_ * 0.9f + s.p.y * 0.8f) * 3.2f;
            fxAdd_.emit(s.p + jump, ride({R(-2.5f, 2.5f), 0, 0}, 1.f), R(0.06f, 0.16f), R(0.1f, 0.16f), R(0.1f, 0.16f), c, 1, 12.f, 0.f);
        }
        for (int i = count(1, 14); i-- > 0;) { // RGB split: the same tear in red and cyan, offset
            const Pt s = surface(1.05f);
            for (int k = 0; k < 6; ++k) {
                const glm::vec3 p = s.p + glm::vec3(k * 0.06f - 0.15f, 0, 0);
                fxAdd_.emit(p + glm::vec3(-0.04f, 0, 0), ride({0, 0, 0}, 1.f), 0.1f, 0.09f, 0.09f, C().red, 1, 0.f, 0.f);
                fxAdd_.emit(p + glm::vec3(0.04f, 0, 0), ride({0, 0, 0}, 1.f), 0.1f, 0.09f, 0.09f, C().cyan, 1, 0.f, 0.f);
            }
        }
        break;
    }
    default: break;
    }
}

void Game::crashFx(glm::vec3 p) {
    switch (crash_) {
    case CrashFx::Pixels:
        burst(p, 120, {C().cyan, glow("#9a5bff", 3), C().white}, 2, 9, 1.1f, 0.14f, 0.5f, 0, false, 1.5f, 8);
        break;
    case CrashFx::Confetti:
        burst(p, 140, {C().volt, C().pink, C().green, C().cyan}, 3, 9, 1.6f, 0.1f, 1.5f, 0, false, 2.2f, 4);
        break;
    case CrashFx::Feathers:
        spawnFeathers(p, 34, 1.3f);
        break;
    case CrashFx::CoinShower:
        burst(p, 110, {glow("#ffd23a", 3), glow("#fff2b0", 2.4f), C().volt}, 3, 10, 1.4f, 0.12f, 2.f, 0, false, 1.6f, 9);
        rings_.spawn(p, glow("#ffd23a", 2.5f), 3.8f, 0.7f, true);
        break;
    default: break;
    }
}

// ---------- effects helpers ----------
void Game::burst(glm::vec3 p, int n, std::initializer_list<glm::vec3> cols, float smin, float smax, float life, float size,
                 float up, float vx, bool flat, float drag, float grav) {
    const glm::vec3* c = cols.begin();
    const int nc = static_cast<int>(cols.size());
    for (int i = 0; i < n; ++i) {
        const float dx = rng_.range(-1, 1), dy = rng_.range(-1, 1) + up, dz = rng_.range(-1, 1) * (flat ? 0.3f : 1.f);
        float l = std::sqrt(dx * dx + dy * dy + dz * dz); if (l == 0) l = 1;
        const float s = rng_.range(smin, smax);
        fxAdd_.emit(p, {dx / l * s + vx, dy / l * s, dz / l * s}, life * rng_.range(0.6f, 1.2f), size * rng_.range(0.7f, 1.3f), 0, c[i % nc], 1, drag, grav);
    }
}

void Game::puff(glm::vec3 p, int n, glm::vec3 col, float spread, float alpha) {
    for (int i = 0; i < n; ++i)
        fxSmoke_.emit({p.x + rng_.range(-0.2f, 0.2f) * spread, p.y + rng_.range(0, 0.15f), p.z + rng_.range(-0.2f, 0.2f) * spread},
                      {rng_.range(-0.7f, 0.7f) * spread, rng_.range(0.3f, 1.1f), rng_.range(-0.7f, 0.7f) * spread}, rng_.range(0.8f, 1.5f),
                      rng_.range(0.25f, 0.45f), rng_.range(1.1f, 2), col, alpha * rng_.range(0.7f, 1), 1.6f, -0.5f);
}

void Game::spawnShards(glm::vec3 p, int n, float vx) {
    for (int i = 0; i < n; ++i)
        debris_.push_back({i % 2 ? DebrisKind::Spark : DebrisKind::Shard, p, glm::vec3(0.f), glm::vec3(1.f),
                           {vx + rng_.range(-1.8f, 1.8f), rng_.range(1.5f, 4.5f), rng_.range(-1.8f, 1.8f)},
                           {rng_.range(-12, 12), rng_.range(-12, 12), rng_.range(-12, 12)}, rng_.range(0.6f, 1.3f), 0, 0});
}

void Game::spawnFeathers(glm::vec3 p, int n, float spread) {
    for (int i = 0; i < n; ++i)
        debris_.push_back({DebrisKind::Feather, {p.x + rng_.range(-0.2f, 0.2f), p.y + rng_.range(-0.1f, 0.2f), p.z + rng_.range(-0.2f, 0.2f)},
                           {rng_.range(0, 6), rng_.range(0, 6), rng_.range(0, 6)}, glm::vec3(1.f),
                           {rng_.range(-2, 1) * spread, rng_.range(1, 3.5f) * spread, rng_.range(-1.5f, 1.5f) * spread},
                           {rng_.range(-6, 6), rng_.range(-6, 6), rng_.range(-6, 6)}, rng_.range(1.4f, 2.4f), rng_.next01() * 6, 0});
}

void Game::spawnSplat(float x, float y) {
    splats_.push_back({{x, y + 0.012f, rng_.range(-0.3f, 0.3f)}, rng_.range(0.9f, 1.3f), rng_.range(0.8f, 1.1f), 0.3f, 0, 1});
}

void Game::popup(std::string text, glm::vec3 world, PopKind kind) {
    const float dur = kind == PopKind::Surge ? 1.3f : kind == PopKind::Sector || kind == PopKind::Smashed ? 1.4f : kind == PopKind::Perfect ? 1.1f : 0.9f;
    popups_.push_back({world, std::move(text), kind, 0, dur});
}

void Game::flashScreen(glm::vec3 color, float alpha) { flashColor_ = color; flashAlpha_ = alpha; flashT_ = 0; }

// ---------- frame ----------
void Game::update(double rawDt) {
    const float raw = static_cast<float>(rawDt), rdt = std::min(0.033f, raw);
    rdt_ = rdt;
    beat_.advance(std::min(0.25f, raw));
    if (freeze_ > 0) freeze_ -= rdt;
    timeScale_ += (1 - timeScale_) * std::min(1.f, rdt * (state_ == State::Dead ? 1.2f : 4.f));
    const float dt = rdt * (freeze_ > 0 ? 0.04f : timeScale_);
    time_ += dt;

    if (state_ == State::Play) updatePlay(dt, rdt);
    else danger_ += (0 - danger_) * std::min(1.f, dt * 6);

    // stack layout: each egg grows from zero so the hen rides it up
    float cursor = float(baseY_);
    for (size_t i = 0; i < eggs_.size(); ++i) {
        Egg& e = eggs_[i];
        e.age += dt;
        const float s = e.age < 0.2f ? std::max(0.001f, easeOutBack(e.age / 0.2f)) : 1.f;
        float jy = 1;
        if (e.jig > 0) { e.jd -= dt; if (e.jd <= 0) { e.jig = std::max(0.f, e.jig - dt * 3.5f); jy = 1 - std::sin(e.jig * kPi) * 0.14f * e.jig; } }
        const float hgt = Uf * std::min(s, 1.08f) * jy;
        const float sway = std::sin(time_ * 2.6f + e.sway) * 0.004f * std::pow(float(i + 1), 1.2f) * (eggs_.size() > 4 ? 1.f : 0.3f);
        const float wide = std::min(1.f, s * 1.05f) * (2 - jy);
        e.scale = {wide, s * jy, wide};
        e.pos = {float(x_) + sway, cursor + hgt * 0.5f, 0};
        e.ringRot = time_ * 1.5f + float(i);
        cursor += hgt;
    }
    if (!showroom()) showPlaced_ = false;
    if (state_ != State::Dead || showroom()) {
        hopV_ -= 38 * dt; hop_ = std::max(0.f, hop_ + hopV_ * dt); if (hop_ == 0 && hopV_ < 0) hopV_ = 0;
        if (state_ == State::Dead) { // try-on after a crash: stand on clear ground, not inside the wall she hit
            if (!showPlaced_) { showSpot_ = showroomSpot(); showPlaced_ = true; }
            hen_.nodes[hen_.root].pos = {showSpot_.x, showSpot_.y + hop_, 0};
        } else {
            hen_.nodes[hen_.root].pos = {float(x_), cursor + hop_, 0};
        }
    }
    updateHen(dt);
    updateDebris(dt);
    updateSmashDebris(dt);
    updateParty(rdt);
    if (partyK_ > 0.f) // rainbow outline glow on the hen; it also keeps her readable against the party lights
        hen_.rimColor = glm::mix(hen_.rimColor, glm::mix(hueColor(partyTime_ * 0.7f), glm::vec3(1.f), 0.3f), partyK_);

    for (size_t i = splats_.size(); i-- > 0;) {
        Splat& s = splats_[i];
        s.age += dt;
        s.k = clampf(1 - (s.age - 2.5f) / 1.5f, 0, 1);
        if (s.age < 0.15f) s.scale = 0.3f + s.age / 0.15f * 0.7f; else s.scale = 1;
        if (s.k <= 0 || s.pos.x < x_ - 16) splats_.erase(splats_.begin() + static_cast<long>(i));
    }
    for (const Corn& c : level_.corns)
        if (rng_.next01() < 0.35f) {
            const float a = rng_.range(0, 6.28f);
            const float y = float(c.y) + std::sin(time_ * 3 + c.phase) * 0.06f;
            const glm::vec3 cols[] = {C().pink, C().cyan, C().volt};
            fxAdd_.emit({float(c.x) + std::cos(a) * 0.5f, y + rng_.range(-0.3f, 0.3f), std::sin(a) * 0.5f}, {0, rng_.range(0.3f, 0.7f), 0},
                        rng_.range(0.5f, 0.9f), 0.07f, 0, cols[rng_.index(3)], 0.45f, 0, 0);
        }

    updateCamera(rdt);
    world_.update(dt, time_, camX_, camY_, speed_, state_ == State::Play);
    updateAmbient(dt);
    fxAdd_.update(dt); fxSmoke_.update(dt); rings_.update(dt);

    const float pz = beat_.pulse();
    // disco floor steps on every kick (or a steady tempo when no kicks are coming)
    if (pz > discoLastPulse_ + 0.5f) discoStep_++;
    discoLastPulse_ = pz;
    discoTimer_ += rdt;
    if (discoTimer_ > 0.47f && pz < 0.01f) { discoTimer_ = 0; discoStep_++; }
    if (pz > 0.5f) discoTimer_ = 0;

    if (gateVisible_ && state_ == State::Play && !gatePassed_ && x_ > gateX_) {
        gatePassed_ = true;
        const float top = float(baseY_) + eggs_.size() * Uf;
        popup("NEW DAILY BEST", {float(x_) + 1, top + 2, 0}, PopKind::Sector);
        missionProgress(missions_.record(MissionKind::BeatBest));
        sfx(Sfx::Perfect); flashScreen(srgbColor("#eafaff"), 0.22f); chromaKick_ = std::max(chromaKick_, 0.6f);
        rings_.spawn({float(gateX_), top + 0.6f, 0.4f}, C().white, 3.2f, 0.7f, true);
        burst({float(gateX_), top + 0.8f, 0}, 40, {C().white, C().cyan, C().volt}, 2, 7, 0.8f, 0.1f, 0.8f, 0, false, 2.5f, 4);
    }
    if (gatePassed_) gateOpacity_ = std::max(0.f, gateOpacity_ - rdt * 1.5f);
    if (challenge_ && state_ == State::Play && x_ > challenge_->meters && challenge_->opacity >= 1.f) {
        const bool first = !challenge_->beaten;
        challenge_->beaten = true;
        challenge_->opacity = 0.999f; // fades out below
        saveChallenge();
        const float top = float(baseY_) + eggs_.size() * Uf;
        popup(first ? "CHALLENGE BEATEN!" : "PASSED THEM AGAIN", {float(x_) + 1, top + 2.4f, 0}, PopKind::Surge);
        flashScreen(srgbColor("#ff2bd6"), 0.3f); sfx(Sfx::SurgeStart); buzz(30);
        rings_.spawn({float(challenge_->meters), top + 0.6f, 0.4f}, C().pink, 3.4f, 0.7f, true);
        burst({float(challenge_->meters), top + 0.8f, 0}, 60, {C().pink, C().volt, C().white}, 2, 8, 0.9f, 0.1f, 0.8f, 0, false, 2.5f, 4);
        if (first) {
            track("challenge_beaten", {{"meters", std::to_string(challenge_->meters)}});
            if (!challenge_->id.empty()) beatenPending_ = true; // the sharer is nudged with the final distance (finishRun)
        }
    }
    if (challenge_ && challenge_->opacity < 1.f) challenge_->opacity = std::max(0.f, challenge_->opacity - rdt * 1.2f);

    { // drift the scene's colour mood toward the current sector's grade
        const Grade& g = grade(gradeIdx_);
        const float k = std::min(1.f, rdt * 0.8f);
        fog_ = glm::mix(fog_, g.fog, k); skyMid_ = glm::mix(skyMid_, g.mid, k); skyBot_ = glm::mix(skyBot_, g.bot, k);
        skyHaze_ = glm::mix(skyHaze_, g.haze, k); pinkLight_ = glm::mix(pinkLight_, g.light, k);
    }
    chromaKick_ = std::max(0.f, chromaKick_ - rdt * 2.8f);

    // HUD timers
    flashT_ += rdt;
    if (state_ == State::Title) { titleT_ += rdt; privacyButton_ = svc_.ads->privacyOptionsRequired(); }
    if (state_ == State::Play) run_.seconds += rdt;
    screenT_ += rdt;
    rewardMsgT_ = std::max(0.f, rewardMsgT_ - rdt);
    updateHudFx(rdt);
    pollAds();
    for (PurchaseEvent e; svc_.store->pollEvent(e);) onPurchase(e);
    if ((productsRefreshT_ -= rdt) <= 0) { products_ = svc_.store->products(); productsRefreshT_ = products_.empty() ? 1.f : 30.f; }
    if (pendingStart_ && !svc_.ads->adShowing()) start(); // the interstitial was dismissed
    // the Continue panel, once the crash has played
    if (state_ == State::Dead) deadRealT_ += rdt; else deadRealT_ = 0;
    if (state_ == State::Dead && !replaySaved_ && deadRealT_ > 0.62f) { // the crash has played; panels not up yet
        replaySaved_ = true;
        ReplayMeta m;
        m.distance = int(std::floor(x_)); m.score = score_; m.day = dayLabel();
        m.newBest = x_ > day_.bestDist || score_ > best_;
        svc_.replay->saveClip(m);
    }
    {
        std::string target;
        if (svc_.replay->consumeShared(target)) track("replay_shared", {{"target", target}, {"distance", std::to_string(finalDist_)}});
    }
    if (pendingShare_) {
        std::string id;
        pendingShare_->wait -= rdt;
        if (svc_.backend->pollChallengeId(id) || pendingShare_->wait <= 0) {
            const PendingShare p = *pendingShare_;
            pendingShare_.reset();
            doShare(p.caption, p.meters, id);
        }
    }
    if (continuePending_ && !continueOffered_ && state_ == State::Dead && deadRealT_ > 0.7f) {
        continueOffered_ = true;
        continueT_ = 0;
        openScreen(Screen::Continue);
    }
    if (screen_ == Screen::Continue && !pendingReward_) {
        continueT_ += rdt;
        if (continueT_ >= tune_.continueSeconds) { closeScreen(); finishRun(); }
    }
    // queued screens (daily drop, level up, ...) open one at a time over the title or the results
    const bool overlayUp = state_ == State::Title || (state_ == State::Dead && overDelay_ <= 0 && overT_ > 0.9f);
    if (screen_ == Screen::None && !screenQueue_.empty() && overlayUp && !pendingReward_) {
        const Screen next = screenQueue_.front();
        screenQueue_.pop_front();
        if (next == Screen::Starter) profile_.setCounter("starter_seen", profile_.counter("starter_seen") + 1);
        if (next == Screen::StreakSave) profile_.setCounter("streak_prompt_day", int(civilDay(svc_.clock->today())));
        openScreen(next);
    }
    meterShake_ = std::max(0.f, meterShake_ - rdt);
    scoreBump_ = std::max(0.f, scoreBump_ - rdt);
    for (size_t i = popups_.size(); i-- > 0;) { popups_[i].age += rdt; if (popups_[i].age >= popups_[i].dur) popups_.erase(popups_.begin() + static_cast<long>(i)); }
    if (overDelay_ > 0) {
        overDelay_ -= rdt;
        if (overDelay_ <= 0) {
            overT_ = 0;
            // the run's coins fly from the results up into the counter
            if (runCoins_ > 0) flyCoins({viewW_ / 2, overCoinsY_}, runCoins_, std::clamp(runCoins_ / 2, 4, 14));
            // new best: confetti from both sides of the results, a flash and a fanfare
            if (newBestRun_) {
                const glm::vec3 cols[] = {srgbColor("#ff2bd6"), srgbColor("#29e7ff"), srgbColor("#f4ff5a"), srgbColor("#2bff9a")};
                for (int side = 0; side < 2; ++side)
                    hudBurst({side ? viewW_ - 30.f : 30.f, viewH_ * 0.45f}, 26, {cols[0], cols[1], cols[2], cols[3]}, 620.f);
                flashScreen(srgbColor("#f4ff5a"), 0.22f);
                sfx(Sfx::SurgeStart);
                queueHaptic(0.f, 25); queueHaptic(0.12f, 25); queueHaptic(0.24f, 40);
            }
        }
    }
    else overT_ += rdt;
    for (float& p : popT_) p = std::max(0.f, p - rdt);

    buildRenderList();
}

void Game::updatePlay(float dt, float rdt) {
    const Curve cv = curve(x_);
    // surge: x1.35 on top of difficulty + adaptive speed, easing back after
    surgeSpeedMul_ += ((surging_ ? surge::SPEED_MUL : 1.f) - surgeSpeedMul_) * std::min(1.f, dt * 6.f);
    speed_ = float(cv.speed) * dda_.speedMul(float(x_)) * surgeSpeedMul_;
    beat_.setTempoFromSpeed(speed_, surge::MUSIC_BPM_CAP); svc_.audio->musicTempo(float(beat_.bpm()));
    // real time, so hit-pauses don't stretch the 5 s surge
    if (surging_) { surgeT_ -= rdt; if (surgeT_ <= 0) endSurge(); }
    else if (graceT_ > 0) graceT_ = std::max(0.f, graceT_ - rdt);
    x_ += speed_ * dt;
    const float overclock = runBoost_ == Boost::Overclock ? 0.6f : 1.f; // boost: eggs refill faster all run
    meter_ = std::min(float(MAXE), meter_ + dt / (float(cv.regen) * dda_.regenMul(float(x_)) * overclock));
    const int fullNow = std::min(MAXE, static_cast<int>(std::floor(meter_)));
    if (fullNow > prevFull_) for (int i = std::max(0, prevFull_); i < fullNow; ++i) popT_[i] = 0.35f;
    prevFull_ = fullNow;

    const int sectorNow = static_cast<int>(std::floor(x_ / SECTOR)) + 1;
    if (sectorNow > sector_) {
        sector_ = sectorNow; gradeIdx_ = (sector_ - 1) % 4;
        const float top = float(baseY_) + eggs_.size() * Uf;
        popup("SECTOR " + std::to_string(sector_), {float(x_) + 2, top + 2.2f, 0}, PopKind::Sector);
        sfx(Sfx::Perfect); flashScreen(srgbColor("#29e7ff"), 0.18f); chromaKick_ = std::max(chromaKick_, 0.5f);
        rings_.spawn({float(x_), top + 0.5f, 0.5f}, C().cyan, 3, 0.7f, true);
    }
    if (level_.generateUntil(x_ + 45)) { level_.dropBehind(x_); level_.rebuildBlocks(); }

    if ((autoSurge_ || runBoost_ == Boost::Surge) && !autoSurgeFired_ && x_ > 4) { autoSurgeFired_ = true; chain_ = 0; startSurge(); }
    if (smashing()) smashAhead();
    const double hb = 0.22;
    const double hFront = level_.heightAt(x_ + hb) * U;
    const double support = std::max(level_.heightAt(x_ - hb), level_.heightAt(x_ + hb)) * U;
    if (hFront > baseY_ + 0.05) {
        const int k = static_cast<int>(std::ceil((hFront - baseY_) / U - 0.5 - 1e-6));
        const double top = baseY_ + eggs_.size() * U;
        if (smashing()) { baseY_ = hFront; vy_ = 0; } // invulnerable: step through whatever is left
        else if (top < hFront - 0.06 || k > static_cast<int>(eggs_.size())) { die(false); }
        else {
            if (k > 0 && k == static_cast<int>(eggs_.size())) perfect(float(hFront)); else if (k > 0) streak_ = 0;
            if (k > 0) knock(k);
            baseY_ = hFront; vy_ = 0;
            for (int i = 0; i < 12; ++i)
                fxAdd_.emit({float(x_) + 0.3f, float(hFront) + 0.02f, rng_.range(-0.6f, 0.6f)}, {rng_.range(-3, -1), rng_.range(0.5f, 2), rng_.range(-0.5f, 0.5f)},
                            rng_.range(0.2f, 0.4f), 0.07f, 0, i % 2 ? C().cyan : C().ember, 1, 3, 8);
        }
    } else if (support < baseY_ - 1e-4) {
        vy_ -= 36 * dt; baseY_ += vy_ * dt;
        if (baseY_ <= support) {
            if (vy_ < -4) {
                squash_ = 0.24f; sfx(Sfx::Land); shake_ = std::max(shake_, 0.1f);
                puff({float(x_), float(support) + 0.05f, 0}, 7, C().steam, 1.6f, 0.45f);
                if (support < 0.01)
                    for (int i = 0; i < 14; ++i)
                        fxAdd_.emit({float(x_) + rng_.range(-0.3f, 0.3f), 0.03f, rng_.range(-0.3f, 0.3f)}, {rng_.range(-2, 1), rng_.range(1.2f, 2.6f), rng_.range(-1, 1)},
                                    rng_.range(0.3f, 0.5f), 0.05f, 0.02f, C().water, 1, 1, 9);
                rings_.spawn({float(x_), float(support) + 0.02f, 0}, C().cyan, 1.2f, 0.35f, false);
                for (size_t i = 0; i < eggs_.size(); ++i) { eggs_[i].jig = 1; eggs_[i].jd = i * 0.035f; }
            }
            baseY_ = support; vy_ = 0;
        }
    }
    if (state_ != State::Play) return;

    // overhead barrier: hen's head may not rise above its underside
    {
        const double henTop = baseY_ + eggs_.size() * U + HEN_H;
        for (Segment& sg : level_.segs) {
            if (!sg.ceil || x_ + 0.25 < sg.x0 || x_ + 0.22 >= sg.x1) continue; // the barrier ends exactly when the stack steps onto the next wall
            if (henTop > sg.ceil * U + 0.02 && !smashing()) { die(true); return; }
            sg.minHead = sg.hasMinHead ? std::min(sg.minHead, sg.ceil * U - henTop) : sg.ceil * U - henTop;
            sg.hasMinHead = true;
        }
        for (Segment& sg : level_.segs)
            if (sg.ceil && !sg.closeCallChecked && sg.hasMinHead && x_ + 0.22 >= sg.x1) {
                sg.closeCallChecked = true;
                if (sg.minHead < 0.35) closeCall(float(sg.ceil * U));
            }
    }
    // danger: the next wall is taller than the stack (judged by where the stack will land)
    const double landBase = std::min(baseY_, support), topNow = landBase + eggs_.size() * U;
    double wallH = 0;
    for (const Segment& s : level_.segs) {
        if (s.x0 <= x_ + 0.22) continue;
        if (s.x0 > x_ + 1.2 + speed_ * 0.75) break;
        wallH = std::max(wallH, s.h * U);
        if (s.ceil) break; // never look past the first overhead barrier
    }
    for (const Segment& sg : level_.segs)
        if (sg.ceil && x_ + 0.25 >= sg.x0 && x_ + 0.22 < sg.x1)
            wallH = std::min(wallH, std::floor((sg.ceil * U - HEN_H - 0.02) / U + 1e-6) * U); // snapped to whole eggs
    needEggs_ = wallH > topNow + 0.05 && !smashing(); // no 'need more eggs' warning while smashing
    danger_ += ((needEggs_ ? 1.f : 0.f) - danger_) * std::min(1.f, dt * 10);

    // disco ball pickups
    const double hy = topNow + 0.45;
    auto& corns = level_.corns;
    for (size_t i = corns.size(); i-- > 0;) {
        const Corn c = corns[i];
        if (std::abs(c.x - x_) < 0.6 && std::abs(c.y - hy) < 0.65) {
            const glm::vec3 p{float(c.x), float(c.y) + std::sin(time_ * 3 + c.phase) * 0.06f, 0};
            corns.erase(corns.begin() + static_cast<long>(i));
            bonus_ += 5; cornCount_++; meter_ = std::min(float(MAXE), meter_ + 1.5f);
            sfx(Sfx::Corn); buzz(10);
            if (surging_) popup("+5", p + glm::vec3(0, 0.5f, 0), PopKind::Groove); // surging: no progress toward the next surge
            else {
                chain_++;
                popup("+5 \u00b7 " + std::to_string(chain_) + "/" + std::to_string(surge::NEED), p + glm::vec3(0, 0.5f, 0), PopKind::Groove);
                if (chain_ >= surge::NEED) { chain_ = 0; startSurge(); }
            }
            const glm::vec3 hp = hen_.nodes[hen_.root].pos;
            burst({hp.x, hp.y + 0.8f, 0}, 14, {C().pink, C().cyan, C().volt}, 1, 2.5f, 0.5f, 0.06f, 1, 0, false, 2.5f, -1);
            rings_.spawn({p.x, p.y, 0.3f}, C().pink, 1.1f, 0.4f, true); rings_.spawn({p.x, p.y, 0.3f}, C().cyan, 0.8f, 0.35f, true);
            const glm::vec3 cols[] = {C().pink, C().cyan, C().volt, C().green};
            for (int k = 0; k < 28; ++k) {
                const float a = k / 28.f * 2 * kPi, s = rng_.range(1.5f, 3.2f);
                fxAdd_.emit(p, {std::cos(a) * s, std::sin(a) * s + 1, rng_.range(-0.6f, 0.6f)}, rng_.range(0.5f, 0.8f), 0.1f, 0, cols[k % 4], 1, 2.5f, 1.5f);
            }
            chromaKick_ = std::max(chromaKick_, 0.3f);
        } else if (c.x < x_ - 14) {
            corns.erase(corns.begin() + static_cast<long>(i));
        } else if (!c.missed && c.x < x_ - surge::MISS_BEHIND) {
            corns[i].missed = true; // counted once; a miss no longer breaks the count: any 10 balls in a run trigger a surge
        }
    }
    const int s = static_cast<int>(std::floor(x_)) + bonus_;
    score_ = s;
    if (bonus_ != lastBonus_) { lastBonus_ = bonus_; scoreBump_ = 0.32f; }
}

// A flat stretch of ground (the same height 0.8 m either side) at least 1.5 m behind the crash, searching back
glm::vec2 Game::showroomSpot() const {
    for (float x = float(x_) - 1.5f; x > float(x_) - 30.f; x -= 0.25f) {
        const int h = level_.heightAt(x);
        if (level_.heightAt(x - 0.8f) == h && level_.heightAt(x + 0.8f) == h && level_.heightAt(x + 1.4f) <= h)
            return {x, float(h) * Uf};
    }
    return {float(x_) - 1.5f, float(level_.heightAt(x_ - 1.5)) * Uf};
}

void Game::updateHen(float dt) {
    Hen::Node& root = hen_.nodes[hen_.root];
    const bool show = showroom();
    if (show && state_ == State::Dead && hen_.shattered()) { hen_.unshatter(); henV_ = henW_ = glm::vec3(0.f); } // reassemble for the try-on
    if (state_ != State::Dead || show) {
        Hen::Node& body = hen_.nodes[hen_.body];
        Hen::Node& head = hen_.nodes[hen_.head];
        flap_ = std::max(0.f, flap_ - dt * 3.2f);
        squash_ += (0 - squash_) * std::min(1.f, dt * 12);
        body.scale = {1 - squash_ * 0.5f, 1 + squash_, 1 - squash_ * 0.5f};
        const bool onSurface = state_ == State::Play && eggs_.empty() && vy_ == 0;
        const bool onEggs = state_ == State::Play && !eggs_.empty() && hop_ == 0; // running on the rolling eggs
        const bool running = onSurface || onEggs || show; // the showroom hen jogs in place
        const bool falling = state_ == State::Play && vy_ < -1;
        const bool tall = eggs_.size() > 4;
        runPhase_ += dt * (running ? std::max(speed_, 2.f) * 4.5f : 0);
        body.pos.y = running ? std::abs(std::sin(runPhase_)) * 0.06f : std::sin(time_ * 3) * 0.012f;
        const float leanT = onSurface ? -0.14f : falling ? 0.2f : tall ? std::sin(time_ * 2.6f) * 0.05f - 0.04f : -0.05f;
        lean_ += (leanT - lean_) * std::min(1.f, dt * 8);
        // showroom: a slow turn left and right so the outfit and the trail streaming off it are seen from both sides
        const float lk = lockerK_ * lockerK_ * (3 - 2 * lockerK_);
        root.rot = {0, std::sin(time_ * 0.55f) * 0.75f * lk, lean_ * (1 - lk)};
        for (int k = 0; k < 2; ++k) {
            const float s = k == 0 ? -1.f : 1.f;
            float& rx = hen_.nodes[hen_.wings[k]].rot.x;
            if (falling) rx = s * (-0.7f - std::sin(time_ * 42) * 0.45f);
            else if (flap_ > 0) rx = s * (-(std::sin(flap_ * 18) * flap_ * 1.1f) - 0.02f);
            else if (tall) rx = s * (-0.35f - std::sin(time_ * 2.6f + s) * 0.12f);
            else rx = s * (onSurface ? -0.1f - std::abs(std::sin(runPhase_)) * 0.2f : -0.02f);
            hen_.nodes[hen_.legs[k]].rot.z = running ? std::sin(runPhase_ + k * kPi) * 0.75f
                                           : falling ? 0.35f + std::sin(time_ * 20 + k) * 0.25f : 0.f;
        }
        if (state_ == State::Title) {
            head.pos.x = 0.3f + std::max(0.f, std::sin(time_ * 1.7f)) * 0.06f;
            head.rot.z = -std::max(0.f, std::sin(time_ * 1.7f - 0.4f)) * 0.45f;
            head.rot.y = std::sin(time_ * 0.6f) * 0.5f;
        } else {
            head.pos.x = 0.3f + std::sin(time_ * 9) * 0.015f;
            head.rot.z += ((danger_ * 0.4f) - head.rot.z) * std::min(1.f, dt * 10);
            head.rot.y *= 0.9f;
        }
        hen_.nodes[hen_.tail].rot.z = std::sin(time_ * (onSurface ? 14.f : 4.f)) * (onSurface ? 0.12f : 0.05f);
        head.pos.y = 0.92f + beat_.pulse() * 0.045f;
        if (hen_.canShatter()) {
            // the 3D hen models are one round body: their "head" bone carries the whole upper half, so the procedural
            // hen's pecks and head turns would shear it back and forth. Keep only a slight tilt and the beat bob.
            head.pos.x = 0.3f;
            head.rot.z *= 0.12f;
            head.rot.y *= 0.15f;
            head.pos.y = 0.92f + beat_.pulse() * 0.012f;
        }
        if (state_ == State::Title || (show && state_ != State::Play)) emitAura(dt); // title and showroom (runs call it below)
        if (show && state_ != State::Play) emitTrail(dt);                            // try the trail on in place
        // jet trail from the battery pack
        if (state_ == State::Play) {
            const float px = root.pos.x - 0.26f, py = root.pos.y + 0.82f;
            for (int i = 0; i < 2; ++i)
                fxAdd_.emit({px + rng_.range(-0.03f, 0.03f), py + rng_.range(-0.03f, 0.03f), rng_.range(-0.05f, 0.05f)},
                            {rng_.range(-1.2f, -0.4f), rng_.range(-0.1f, 0.4f), rng_.range(-0.2f, 0.2f)}, rng_.range(0.3f, 0.5f), 0.11f, 0.01f,
                            rng_.next01() < 0.75f ? C().cyan : C().pink, 0.9f, 1.5f, -0.5f);
            emitTrail(dt);
            emitAura(dt);
            if (onSurface && baseY_ < 0.01 && rng_.next01() < 0.5f)
                fxAdd_.emit({float(x_) + rng_.range(-0.1f, 0.1f), 0.03f, rng_.range(-0.15f, 0.15f)}, {rng_.range(-2, -0.5f), rng_.range(0.8f, 1.8f), rng_.range(-0.6f, 0.6f)},
                            rng_.range(0.25f, 0.4f), 0.045f, 0.02f, C().water, 1, 1, 9);
        }
    } else {
        deadT_ += dt;
        hen_.updateShatter(dt);
        henV_.y -= 22 * dt;
        root.pos += henV_ * dt;
        root.rot += henW_ * dt;
        if (rng_.next01() < 0.6f)
            fxAdd_.emit(root.pos + glm::vec3(0, 0.4f, 0.1f), {rng_.range(-1, 1), rng_.range(0, 1.5f), rng_.range(-1, 1)}, rng_.range(0.2f, 0.4f), 0.08f, 0,
                        rng_.next01() < 0.5f ? C().ember : C().cyan, 1, 2, 4);
        if (rng_.next01() < 0.25f) puff(root.pos + glm::vec3(0, 0.3f, 0), 1, C().smoke, 0.6f, 0.5f);
        const float floorY = level_.heightAt(root.pos.x) * Uf + 0.25f;
        if (root.pos.y < floorY && henV_.y < 0) { root.pos.y = floorY; henV_ *= 0.35f; henV_.y = std::abs(henV_.y) * 0.4f; henW_ *= 0.5f; }
    }
    // visor turns red when the next wall is too tall
    const glm::vec3 cyan = hen_.skin.visor, red = hexColor("#ff2b4a");
    hen_.time = time_;
    hen_.rimColor = glm::mix(glm::mix(hen_.skin.rainbow ? hueColor(time_ * 0.6f) : cyan, red, danger_), glm::vec3(1.f), 0.35f);
    hen_.cyanColor = glm::mix(cyan, red, danger_);
    hen_.cyanIntensity = (3.2f + danger_ * std::max(0.f, std::sin(time_ * 22)) * 2.5f) * (std::fmod(time_, 3.7f) < 0.08f ? 0.12f : 1.f);
    hen_.tipVisible = std::fmod(time_, 1.1f) < 0.18f;
}

void Game::updateDebris(float dt) {
    for (size_t i = debris_.size(); i-- > 0;) {
        Debris& d = debris_[i];
        d.life -= dt;
        if (d.kind == DebrisKind::Feather) {
            d.v.y = std::max(d.v.y - 6 * dt, -0.7f); d.v.x *= 1 - dt * 1.5f; d.v.z *= 1 - dt * 1.5f;
            d.pos += d.v * dt; d.pos.x += std::sin(time_ * 5 + d.ph) * 0.4f * dt;
            d.rot.x += d.w.x * dt * 0.4f; d.rot.z += d.w.z * dt * 0.4f;
        } else {
            d.v.y -= 22 * dt; d.pos += d.v * dt; d.rot += d.w * dt;
            if (d.kind == DebrisKind::Egg) fxAdd_.emit(d.pos, glm::vec3(0.f), 0.3f, 0.12f, 0, C().cyan, 0.7f, 0, 0);
            const float fy = std::abs(d.pos.z) < DEPTH / 2 ? level_.heightAt(d.pos.x) * Uf : 0.f;
            if (d.pos.y < fy + 0.05f && d.v.y < 0) {
                if (d.kind == DebrisKind::Egg) {
                    spawnSplat(d.pos.x, fy);
                    const glm::vec3 p = d.pos;
                    spawnShards({p.x, fy + 0.1f, p.z}, 4, 0);
                    burst({p.x, fy + 0.08f, p.z}, 16, {C().volt, C().cyan}, 1.5f, 4, 0.45f, 0.07f, 1.2f, 0, false, 2.5f, 9);
                    rings_.spawn({p.x, fy + 0.015f, p.z}, C().volt, 0.7f, 0.4f, false);
                    debris_[i].life = 0;
                } else { d.pos.y = fy + 0.05f; d.v *= 0.3f; d.v.y *= -0.5f; }
            }
        }
        Debris& dd = debris_[i];
        if (dd.life < 0.3f && dd.kind != DebrisKind::Egg) dd.scale = glm::vec3(std::max(0.001f, dd.life / 0.3f));
        if (dd.life <= 0) debris_.erase(debris_.begin() + static_cast<long>(i));
    }
}

void Game::updateCamera(float rdt) {
    const glm::vec3 hp = hen_.nodes[hen_.root].pos;
    const float lookAhead = state_ == State::Play ? speed_ * 0.12f : 0;
    const float dz = state_ == State::Dead ? std::min(1.f, deadT_ / 0.9f) : 0, dze = dz * dz * (3 - 2 * dz);
    const float focusX = state_ == State::Dead ? lerp(float(x_) + 1.7f, hp.x + 0.5f, dze) : hp.x + 1.7f + lookAhead;
    camX_ += (focusX - camX_) * std::min(1.f, rdt * 8);
    const float top = state_ == State::Dead ? lerp(float(baseY_), std::max(0.3f, hp.y), dze) : hp.y;
    const float wantY = std::max(1.3f, (float(baseY_) + top) * 0.5f + 1.2f);
    camY_ += (wantY - camY_) * std::min(1.f, rdt * 3.5f);
    shake_ = std::max(0.f, shake_ - rdt * 1.4f);
    const float sx = (rng_.next01() - 0.5f) * shake_ * 0.35f, sy = (rng_.next01() - 0.5f) * shake_ * 0.35f;
    const float drift = state_ == State::Title ? std::sin(time_ * 0.35f) : 0;
    intro_ = std::max(0.f, intro_ - rdt / 1.3f);
    const float ie = intro_ * intro_ * (3 - 2 * intro_);
    camKick_ *= std::exp(-rdt * 9);
    // locker open: frame the hen in the top part of the screen, above the sheet, a little closer
    lockerK_ += ((screen_ == Screen::Locker ? 1.f : 0.f) - lockerK_) * std::min(1.f, rdt * 6.f);
    const float lk = lockerK_ * lockerK_ * (3 - 2 * lockerK_);
    camera_.setFovBoost(surge::FOV_ADD * partyK_);
    const float dist = camera_.followDistance();
    const glm::vec3 eye{camX_ + sx + drift * 0.8f - camKick_ - ie * 3, camY_ + 1.9f + sy + drift * 0.3f + ie * 4.5f,
                        dist * (1 - 0.38f * dze) - std::abs(drift) * 0.8f + ie * 6};
    const glm::vec3 look{camX_ + drift * 0.3f - camKick_ * 0.5f, camY_ + 0.1f + ie * 1.2f + dze * 0.3f, 0};
    // showroom: a close 3/4 front view (the hen faces +x), aimed low so the hen sits in the screen's top part, above
    // the Locker sheet; a gentle drift keeps it alive
    const glm::vec3 hc = hp + glm::vec3(0.f, 0.8f, 0.f);
    const glm::vec3 showEye = hc + glm::vec3(3.6f + std::sin(time_ * 0.3f) * 0.3f, 0.9f, 7.6f);
    const glm::vec3 showLook = hc + glm::vec3(0.1f, -1.95f, 0.f);
    camera_.setPose(glm::mix(eye, showEye, lk), glm::mix(look, showLook, lk));
}

void Game::updateAmbient(float dt) {
    // rain splashes, neon dust, vent steam
    rainAcc_ += dt * 75;
    while (rainAcc_ > 1) {
        rainAcc_--;
        const float rx = camX_ + rng_.range(-8, 12), rz = rng_.range(-1.4f, 6.5f);
        const float ry = std::abs(rz) < DEPTH / 2 ? level_.heightAt(rx) * Uf : 0.f;
        for (int k = 0; k < 2; ++k)
            fxAdd_.emit({rx, ry + 0.02f, rz}, {rng_.range(-0.6f, 0.6f), rng_.range(0.8f, 1.6f), rng_.range(-0.6f, 0.6f)}, rng_.range(0.18f, 0.3f), 0.04f, 0.01f,
                        C().water, 0.8f, 1, 9);
        if (rng_.next01() < 0.3f) rings_.spawn({rx, ry + 0.012f, rz}, C().water, rng_.range(0.18f, 0.3f), 0.45f, false);
    }
    moteAcc_ += dt * 24;
    while (moteAcc_ > 1) {
        moteAcc_--;
        const glm::vec3 cols[] = {C().cyan, C().pink, C().volt};
        fxAdd_.emit({camX_ + rng_.range(-8, 11), camY_ + rng_.range(-3, 7), rng_.range(-4, 6)}, {rng_.range(-0.4f, 0.1f), rng_.range(-0.1f, 0.15f), rng_.range(-0.1f, 0.1f)},
                    rng_.range(3, 6), rng_.range(0.025f, 0.06f), rng_.range(0.025f, 0.06f), cols[rng_.index(3)], 0.19f, 0, 0);
    }
    for (const glm::vec3& v : world_.vents())
        if (rng_.next01() < dt * 8)
            fxSmoke_.emit({v.x + rng_.range(-0.3f, 0.3f), v.y + 0.05f, v.z + rng_.range(-0.2f, 0.2f)}, {rng_.range(-0.5f, -0.1f), rng_.range(0.9f, 1.5f), rng_.range(-0.15f, 0.15f)},
                          rng_.range(1.8f, 2.6f), rng_.range(0.3f, 0.45f), rng_.range(1.4f, 2.2f), C().steam, rng_.range(0.25f, 0.4f), 0.4f, -0.1f);
}

// ---------- output ----------
void Game::buildRenderList() {
    list_.clear();
    RenderList& out = list_;
    const float pz = beat_.pulse();
    const float x = float(x_);

    // walls: crates and hazard bales, tinted per block
    const glm::vec3 wallRim = hexColor("#ffb020");
    LitMaterial crate; crate.color = glm::vec3(1.f); crate.emissive = glm::vec3(1.4f + pz * 0.36f); crate.metalness = 0.45f; crate.roughness = 0.5f;
    crate.rimColor = wallRim; crate.rimStrength = 0.55f; crate.rimPow = 3.f; crate.pattern = Pattern::Container;
    LitMaterial hay = crate; hay.emissive = glm::vec3(1.3f + pz * 0.3f); hay.metalness = 0.2f; hay.roughness = 0.55f; hay.pattern = Pattern::Hazard;
    const glm::vec3 blockSize{Uf * 0.985f, Uf * 0.985f, DEPTH};
    for (const Level::Block& b : level_.blocks) {
        if (b.pos.x < x - 16 || b.pos.x > x + 48) continue;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose(b.pos, {0, b.flipped ? kPi : 0.f, 0}, blockSize), b.hay ? hay : crate, b.pos.x, b.tint);
    }
    // surge debris: pooled blocks flying off smashed walls, shrinking out at the end of their life
    for (const SmashBlock& b : smashPool_) {
        if (b.life <= 0) continue;
        const float s = std::min(1.f, b.life / surge::DEBRIS_SHRINK);
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose(b.pos, b.rot + glm::vec3(0, b.flipped ? kPi : 0.f, 0), blockSize * glm::vec3(1, 1, 0.5f) * s),
                                                  b.hay ? hay : crate, b.pos.x, b.tint);
    }
    // disco floor tiles and barrier underside edges
    const float flashOn = std::sin(time_ * 16) > 0 ? 1.f : 0.45f, flashSoft = 0.6f + 0.4f * std::max(0.f, std::sin(time_ * 16));
    const double topNow = state_ == State::Play
        ? std::min(baseY_, std::max(level_.heightAt(x_ - 0.22), level_.heightAt(x_ + 0.22)) * U) + eggs_.size() * U : -1.0;
    const double henTop = state_ == State::Play ? baseY_ + eggs_.size() * U + HEN_H : -1.0;
    const glm::vec3 tileSize{Level::TW * 0.93f, 0.05f, Level::TD * 0.93f};
    for (const Segment& sg : level_.segs) {
        if (sg.x1 < x_ - 6 || sg.x0 > x_ + 40) continue;
        const bool near = sg.x0 <= x_ + 30;
        const bool bad = near && state_ == State::Play && !smashing() && sg.x1 > x_ && sg.h * U > topNow + 0.05;
        const bool underStack = state_ != State::Dead && std::abs(sg.h * U - baseY_) < 0.02;
        for (int k = 0; k < sg.capCount; ++k) {
            const Level::Tile& t = level_.tiles[sg.capStart + k];
            glm::vec3 c;
            if (bad) c = kCapDanger * flashOn;
            else if (underStack && std::abs(t.pos.x - x) < 0.32f) c = kTileWhite;
            else c = discoColor(t.gx, t.gz, discoStep_) * (1 + pz * 0.35f);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(t.pos, tileSize), c);
        }
        const bool badCeil = state_ == State::Play && !smashing() && sg.x1 > x_ && henTop > sg.ceil * U + 0.02;
        const glm::vec3 ceilCol = badCeil ? kCapDanger * flashSoft : kCapOk * (1 + pz * 0.25f);
        LitMaterial girder = hay; girder.emissive = glm::vec3(1.3f + pz * 0.3f);
        for (int k = 0; k < sg.ceilCount; ++k) {
            const glm::vec3 p = level_.girders[sg.ceilStart + k].pos;
            out.add(Pass::Lit, MeshId::Box) = makeLit(compose(p + glm::vec3(0, 0.385f, 0), {Uf * 0.985f, 0.7f, DEPTH}), girder, p.x);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(p, {Uf, 0.07f, DEPTH + 0.06f}), ceilCol);
        }
    }
    for (const Level::Curtain& c : level_.curtains)
        if (c.pos.x > x - 20 && c.pos.x < x + 50)
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(c.pos + glm::vec3(0, 3.5f, 0), {c.width, 7, 1}), glow("#ff2a3a", 1.1f), 1, Shape::Curtain);

    // disco balls
    {
        LitMaterial ball; ball.color = glm::vec3(1.f); ball.metalness = 1; ball.roughness = 0.14f; ball.pattern = Pattern::DiscoBall;
        ball.emissive = glm::vec3(0.75f + pz * 0.5f);
        LitMaterial chain; chain.color = hexColor("#cfd6e2"); chain.metalness = 1; chain.roughness = 0.3f;
        const char* fleck[] = {"#ff2bd6", "#29e7ff", "#f4ff5a", "#8a5bff", "#2bff9a", "#ff8a1a"};
        for (const Corn& c : level_.corns) {
            if (c.x > x_ + 50) continue;
            const glm::vec3 p{float(c.x), float(c.y) + std::sin(time_ * 3 + c.phase) * 0.06f, 0};
            const glm::mat4 g = compose(p, {0, time_ * 1.6f + c.phase, 0}, glm::vec3(1.65f));
            out.add(Pass::Lit, MeshId::SphereLow) = makeLit(g * compose({}, glm::vec3(0.19f)), ball);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(p, glm::vec3(1.15f * 1.65f)), glow("#ffb8f4", 0.9f), 1, Shape::RadialGlow);
            out.add(Pass::Lit, MeshId::Cylinder) = makeLit(g * compose({0, 0.42f, 0}, {0.008f, 0.45f, 0.008f}), chain);
            out.add(Pass::Lit, MeshId::Cylinder) = makeLit(g * compose({0, 0.2f, 0}, {0.035f, 0.05f, 0.035f}), chain);
            const glm::mat4 hg = g * compose({}, {0, -time_ * 5.2f, 0}, glm::vec3(1.f));
            for (int k = 0; k < 6; ++k) {
                const float a = k / 6.f * 2 * kPi;
                out.add(Pass::Lit, MeshId::Sphere) =
                    makeEmissive(hg * compose({std::cos(a) * 0.36f, (k % 3 - 1) * 0.1f, std::sin(a) * 0.36f}, glm::vec3(0.022f)), glow(fleck[k], 1.12f));
            }
        }
    }

    // eggs (stack + flying)
    LitMaterial egg; egg.color = hexColor("#f6f3ff"); egg.metalness = 0.15f; egg.roughness = 0.28f; egg.emissive = glm::vec3(0.12f);
    egg.rimColor = hexColor("#ffb8f4"); egg.rimStrength = 1.8f; egg.rimPow = 2.2f;
    const glm::vec3 ringCol[2] = {glow("#29e7ff", 4.2f) * (1 + pz * 0.6f), glow("#ff2bd6", 4.2f) * (1 + pz * 0.6f)};
    auto emitEgg = [&](glm::vec3 pos, glm::vec3 rot, glm::vec3 scale, int ring, float ringRot) {
        const glm::mat4 g = compose(pos, rot, scale);
        out.add(Pass::Lit, MeshId::Egg) = makeLit(g, egg);
        out.add(Pass::Lit, MeshId::EggRing) = makeEmissive(g * compose({}, {kPi / 2, 0, ringRot}, glm::vec3(1.f)), ringCol[ring]);
    };
    // the stack: each egg lies on its side and rolls about its long axis as the hen runs on top; neighbours turn
    // opposite ways, like meshed gears. The long axis is turned ~70 degrees toward the screen plane and stretched a
    // little (x1.3) so the oval, pointy-ended egg silhouette reads (alternating which end points forward). The neon
    // ring runs lengthwise around the shell, so the roll reads as a spinning stripe. Sized (x1.12) so the round side
    // fills its U-tall slot.
    for (size_t i = 0; i < eggs_.size(); ++i) {
        const Egg& e = eggs_[i];
        const float sgn = i % 2 ? 1.f : -1.f;
        const float roll = sgn * (float(x_) / 0.3f) + e.sway;
        const glm::vec3 s{e.scale.x * 1.12f, e.scale.z * 1.12f, e.scale.y * 1.3f}; // x, height, long axis
        const glm::mat4 m = compose(e.pos, {0, sgn * 1.22f, 0}, glm::vec3(1.f)) * compose({}, {0, 0, roll}, glm::vec3(1.f)) *
                            compose({}, {kPi / 2, 0, 0}, glm::vec3(1.f)) * compose({}, {s.x, s.z, s.y});
        out.add(Pass::Lit, MeshId::Egg) = makeLit(m, egg);
        out.add(Pass::Lit, MeshId::EggRing) = makeEmissive(m * compose({}, {1.02f, 1.17f, 1.02f}), ringCol[e.ring]);
    }
    LitMaterial shell; shell.color = hexColor("#d4dbe8"); shell.metalness = 1; shell.roughness = 0.24f;
    for (const Debris& d : debris_) {
        switch (d.kind) {
        case DebrisKind::Egg: emitEgg(d.pos, d.rot, d.scale, d.ring, 0); break;
        case DebrisKind::Shard: out.add(Pass::Lit, MeshId::Box) = makeLit(compose(d.pos, d.rot, d.scale * 0.07f), shell); break;
        case DebrisKind::Spark: out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(d.pos, d.rot, d.scale * glm::vec3(0.025f, 0.025f, 0.14f)), glow("#29e7ff", 4)); break;
        case DebrisKind::Feather:
            out.add(Pass::UnlitAlpha, MeshId::Plane) = makeUnlit(compose(d.pos, d.rot, d.scale * glm::vec3(0.1f, 0.2f, 1)), {0.85f, 0.95f, 1.05f}, 1, Shape::Feather);
            break;
        }
    }
    for (const Splat& s : splats_) {
        out.add(Pass::UnlitAlpha, MeshId::Circle) =
            makeUnlit(compose(s.pos, {-kPi / 2, 0, 0}, glm::vec3(0.3f * s.sx, 0.3f * s.sy, 1) * s.scale), glow("#29e7ff", 1.4f), 0.85f * s.k);
        out.add(Pass::UnlitAlpha, MeshId::Sphere) =
            makeUnlit(compose(s.pos + glm::vec3(0, 0.01f * s.scale, 0), glm::vec3(0.12f, 0.048f, 0.12f) * s.scale), glow("#f4ff5a", 3), s.k);
    }

    hen_.emit(out);

    // glow pad under the stack
    {
        const glm::vec3 hp = hen_.nodes[hen_.root].pos;
        const float o = state_ == State::Dead ? (padOpacity_ = std::max(0.f, padOpacity_ - rdt_ * 2)) : (padOpacity_ = 0.85f + std::sin(time_ * 4) * 0.1f);
        out.add(Pass::UnlitAdd, MeshId::Plane) =
            makeUnlit(compose({state_ == State::Dead ? x : hp.x, float(baseY_) + 0.02f, 0}, {-kPi / 2, 0, 0}, {2.2f, 1.6f, 1}), glow("#29e7ff", 1.6f), o, Shape::RadialGlow);
    }
    // today's-best gate
    if (gateVisible_ && gateOpacity_ > 0) {
        const float gx = float(gateX_);
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({gx, 8, -0.85f}, {0.9f, 16, 1}), glow("#eafaff", 0.9f), gateOpacity_, Shape::HBeam);
        out.add(Pass::UnlitAdd, MeshId::Box) = makeUnlit(compose({gx, 0.02f, 1.2f}, {0.06f, 0.02f, 7}), glow("#eafaff", 1.05f), gateOpacity_);
    }

    // a friend's challenge: a pink beam at their distance
    if (challenge_ && challenge_->opacity > 0 && challenge_->meters > 5) {
        const float cx = float(challenge_->meters), o = challenge_->opacity * (0.85f + 0.15f * std::sin(time_ * 5));
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose({cx, 8, -0.85f}, {1.1f, 16, 1}), glow("#ff2bd6", 1.1f), o, Shape::HBeam);
        out.add(Pass::UnlitAdd, MeshId::Box) = makeUnlit(compose({cx, 0.02f, 1.2f}, {0.08f, 0.02f, 7}), glow("#ff2bd6", 1.3f), o);
    }

    emitParty(out, pz);
    world_.emit(out, camX_, camY_, time_, pz, grade(gradeIdx_).light, text_.get(), surge::WINDOW_FLASH * pz * partyK_);
    worldGateLabel();
    if (challenge_ && challenge_->opacity > 0 && challenge_->meters > 5)
        worldMarkerLabel(challenge_->meters, challenge_->opacity, challenge_->name.empty() ? "FRIEND'S RUN" : toUpperAscii(challenge_->name) + "'S RUN",
                         std::to_string(challenge_->meters) + " m", hexColor("#ff2bd6"));
    out.fontAtlas = fonts_.built() ? &fonts_ : nullptr;
    out.hudImage = hudIcons_.built() ? &hudIcons_ : nullptr;
    rings_.emitTo(out);
    fxAdd_.emitTo(out.particlesAdd);
    fxSmoke_.emitTo(out.particlesSmoke);

    // frame constants
    FrameParams& f = out.frame;
    f.view = camera_.view(); f.proj = camera_.proj(); f.cameraPos = camera_.position();
    f.fogColor = fog_; f.fogDensity = 0.016f;
    f.skyTop = hexColor("#03030c"); f.skyMid = skyMid_; f.skyBot = skyBot_; f.skyHaze = skyHaze_;
    f.moonDir = glm::normalize(glm::vec3(0.35f, 0.42f, -0.84f));
    f.hemiSky = hexColor("#5d4cb0"); f.hemiGround = hexColor("#140a1e"); f.hemiIntensity = 0.85f;
    f.sunDir = glm::normalize(glm::vec3(-7, 14, 9)); f.sunColor = hexColor("#a9b8ff") * 1.1f;
    const glm::vec3 hp = hen_.nodes[hen_.root].pos;
    const float stackTop = state_ == State::Dead ? float(baseY_) : hp.y;
    // party mode: scene light and sky haze cycle through the hue wheel
    const glm::vec3 partyHue = hueColor(partyTime_ * surge::HUE_SPEED);
    f.skyHaze = glm::mix(skyHaze_, partyHue * glm::length(skyHaze_), partyK_ * 0.75f);
    f.points[0] = {{camX_ - 4.5f, camY_ + 2.5f, 3.5f}, glm::mix(pinkLight_, partyHue, partyK_ * 0.7f), 14.4f, 16}; // pinkL
    f.points[1] = {{hp.x + 1.3f, stackTop + 1.3f, 2.4f}, hexColor("#fff2e6"), 7, 6.5f};  // heroLight
    f.points[2] = {};                                                                    // cyanL is never added to the scene in the web build
    f.time = time_; f.pulse = pz;
    f.chromaAmount = 0.18f + chromaKick_ + danger_ * 0.12f;
    f.vignette = 0.45f;
    f.bloomStrength = 0.85f + pz * 0.3f + chromaKick_ * 0.15f + surge::BLOOM_ADD * partyK_;
    f.vignetteTint = glm::vec4(hueColor(partyTime_ * surge::HUE_SPEED * 1.6f + 0.5f), surge::VIGNETTE_TINT * partyK_);
    f.viewportW = viewW_; f.viewportH = viewH_;
    f.frostAmount = 0.f; // a glass panel turns it on while the HUD is built (hudFrost)
    f.showcase = screen_ == Screen::Locker;

    buildHud();
}

} // namespace cs
