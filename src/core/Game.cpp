#include "Game.h"
#include "Log.h"
#include "Materials.h"

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

std::string todayStr() {
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    return buf;
}

const glm::vec3 kCapOk = glow("#ffbe3a", 1.5f), kCapDanger = glow("#ff2a3a", 2.2f), kTileWhite = glow("#ffffff", 1.25f);
} // namespace

Game::Game(const GameServices& services)
    : svc_(services), rng_(static_cast<uint64_t>(std::time(nullptr)) * 2654435761ull + 1), dda_(services.storage), world_(rng_) {
    if (!svc_.audio) svc_.audio = &nullAudio_;
    if (!svc_.ads) svc_.ads = &nullAds_;
    const Grade& g = grade(0);
    fog_ = g.fog; skyMid_ = g.mid; skyBot_ = g.bot; skyHaze_ = g.haze; pinkLight_ = g.light;
    loadRecords();
    svc_.audio->setMuted(muted_);
    if (!fonts_.build()) CS_LOGE("Font atlas could not be built; text disabled");
    text_ = std::make_unique<TextLayout>(fonts_);
    if (!hudIcons_.buildLivesIcons()) CS_LOGE("HUD sprites could not be decoded");
    resize(390, 844);
    reset();
}

// ---------- persistence ----------
void Game::loadRecords() {
    day_.date = todayStr();
    if (!svc_.storage) return;
    if (auto v = svc_.storage->get("cluckstack-best")) best_ = std::atoi(v->c_str());
    if (auto v = svc_.storage->get("cluckstack-music")) muted_ = *v == "off";
    if (auto v = svc_.storage->get(lives::KEY)) lives_ = std::max(0, std::atoi(v->c_str()));
    // the app was closed during a run: that run still costs its life
    if (auto v = svc_.storage->get(lives::RUN_KEY); v && *v == "1") {
        lives_ = std::max(0, lives_ - 1);
        svc_.storage->set(lives::RUN_KEY, "0");
        saveLives();
    }
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
    const std::string t = todayStr();
    if (day_.date != t) day_ = {t, 0, 0, 0};
}

// ---------- flow ----------
void Game::resize(float w, float h) {
    viewW_ = std::max(1.f, w); viewH_ = std::max(1.f, h);
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
    hen_.nodes[hen_.root].pos = glm::vec3(0.f);
    resetSurge();
}

void Game::saveLives() {
    if (svc_.storage) svc_.storage->set(lives::KEY, std::to_string(lives_));
}

void Game::start() {
    if (lives_ <= 0) { openOffer(); return; }
    offerOpen_ = offerPending_ = false;
    if (svc_.storage) svc_.storage->set(lives::RUN_KEY, "1");
    reset();
    state_ = State::Play;
    beat_.start(); svc_.audio->musicStart();
    day_.attempts++; saveDay(); placeGate();
    overDelay_ = -1;
    for (float& p : popT_) p = 0;
    intro_ = 1;
    rings_.spawn({0, 0.02f, 0}, C().cyan, 2.2f, 0.6f, false);
    burst({0, 0.1f, 0}, 30, {C().cyan, C().pink}, 2, 5, 0.6f, 0.1f, 0.3f, 0, true);
}

void Game::placeGate() {
    gatePassed_ = false; gateX_ = day_.bestDist; gateOpacity_ = 1;
    gateVisible_ = gateX_ > 15;
}

void Game::press() {
    if (offerOpen_) { watchAd(); return; } // keyboard / generic tap on the offer = its main button
    if (state_ == State::Title) { start(); return; } // start() opens the offer when out of lives
    if (state_ == State::Play) { lay(); return; }
    if (state_ == State::Dead && deadT_ > 0.8f && overDelay_ <= 0) start();
}

// ---------- lives + rewarded ads ----------
void Game::openOffer() {
    if (offerOpen_) return;
    offerOpen_ = true;
    offerT_ = 0;
}

bool Game::offerPlayAnyway() const {
    return lives::GRANT_WHEN_AD_UNAVAILABLE && offerT_ >= lives::AD_WAIT_SECONDS && svc_.ads->rewardedState() != RewardedState::Ready;
}

void Game::watchAd() {
    if (svc_.ads->rewardedState() == RewardedState::Ready) { svc_.ads->showRewarded(); return; } // reward arrives via consumeReward()
    if (offerPlayAnyway()) grantLives();
    // still loading: the button shows "Loading ad..." and the platform keeps trying
}

void Game::grantLives() {
    lives_ += lives::REWARD;
    saveLives();
    offerOpen_ = false;
    gainAnimT_ = 0.f;
    const glm::vec3 hp = hen_.nodes[hen_.root].pos;
    popup("+" + std::to_string(lives::REWARD) + " LIVES", hp + glm::vec3(0.5f, 1.6f, 0), PopKind::Sector);
    sfx(Sfx::Corn); buzz(20);
    flashScreen(srgbColor("#2bff9a"), 0.2f);
}

void Game::pressAt(float px, float py) {
    // mute button: 44x44 pt, top-right, 16 pt margin
    const float bx = viewW_ - 16 - 22, by = safeTop_ + 16 + 22;
    if (std::abs(px - bx) < 26 && std::abs(py - by) < 26) { toggleMute(); return; }
    if (offerOpen_) { // modal: only its own controls react
        if (offerClose_.hit(px, py)) offerOpen_ = false;
        else if (offerButton_.hit(px, py)) watchAd();
        return;
    }
    // title screen 'Privacy settings' (shown only when UMP requires it): top-left, mirrors the mute button
    if (privacyButton_ && state_ == State::Title && px < 16 + 150 && std::abs(py - by) < 26) { svc_.ads->showPrivacyOptions(); return; }
    press();
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
    spawnFeathers(p, 34, 1.3f);
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
    const bool isBest = score_ > best_, isDayBest = score_ > day_.best;
    if (isDayBest) day_.best = score_;
    const double pbBefore = day_.bestDist;
    dda_.record(float(x_), float(pbBefore));
    if (x_ > day_.bestDist) day_.bestDist = x_;
    saveDay();
    if (isBest) { best_ = score_; if (svc_.storage) svc_.storage->set("cluckstack-best", std::to_string(best_)); }
    finalScore_ = score_; finalDist_ = static_cast<int>(std::floor(x_));
    {
        const double gap = std::ceil(pbBefore - x_);
        std::string hook;
        if (pbBefore <= 0) hook = "First run of today\u2019s course";
        else if (x_ > pbBefore) hook = "New furthest today \u00b7 +" + std::to_string(static_cast<int>(std::floor(x_ - pbBefore))) + "m";
        else if (gap <= 25) hook = "So close \u00b7 " + std::to_string(static_cast<int>(gap)) + "m short of your best";
        else hook = std::to_string(static_cast<int>(std::floor(day_.bestDist))) + "m furthest today";
        overNote_ = "Attempt " + std::to_string(day_.attempts) + " \u00b7 " + hook;
        static const char* kLines[] = {"Flatlined", "Signal lost", "Scrambled", "Fried circuits"};
        overTag_ = isBest && score_ > 0 ? "New all-time best" : isDayBest && score_ > 0 ? "New daily best" : kLines[rng_.index(4)];
    }
    overDelay_ = 0.9f; overT_ = 0;
    lives_ = std::max(0, lives_ - 1);
    saveLives();
    if (svc_.storage) svc_.storage->set(lives::RUN_KEY, "0");
    offerPending_ = lives_ == 0; // out of lives: the offer opens over the game-over screen
    lifeFrom_ = lives_ + 1; lifeTo_ = lives_; lifeAnimT_ = 0.f; lifeSplitDone_ = false; // life-lost animation (HUD badge)
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
    if (state_ != State::Dead) {
        hopV_ -= 38 * dt; hop_ = std::max(0.f, hop_ + hopV_ * dt); if (hop_ == 0 && hopV_ < 0) hopV_ = 0;
        hen_.nodes[hen_.root].pos = {float(x_), cursor + hop_, 0};
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
        sfx(Sfx::Perfect); flashScreen(srgbColor("#eafaff"), 0.22f); chromaKick_ = std::max(chromaKick_, 0.6f);
        rings_.spawn({float(gateX_), top + 0.6f, 0.4f}, C().white, 3.2f, 0.7f, true);
        burst({float(gateX_), top + 0.8f, 0}, 40, {C().white, C().cyan, C().volt}, 2, 7, 0.8f, 0.1f, 0.8f, 0, false, 2.5f, 4);
    }
    if (gatePassed_) gateOpacity_ = std::max(0.f, gateOpacity_ - rdt * 1.5f);

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
    if (offerOpen_) offerT_ += rdt;
    updateLivesAnim(rdt);
    // out of lives: present the rewarded-ad offer once the game-over overlay is up and the heart has broken
    if (offerPending_ && state_ == State::Dead && overDelay_ <= 0 && (lifeAnimT_ < 0 || lifeAnimT_ > 1.1f)) {
        offerPending_ = false;
        openOffer();
    }
    if (svc_.ads->consumeReward()) grantLives(); // the player watched a rewarded ad to the end
    meterShake_ = std::max(0.f, meterShake_ - rdt);
    scoreBump_ = std::max(0.f, scoreBump_ - rdt);
    for (size_t i = popups_.size(); i-- > 0;) { popups_[i].age += rdt; if (popups_[i].age >= popups_[i].dur) popups_.erase(popups_.begin() + static_cast<long>(i)); }
    if (overDelay_ > 0) {
        overDelay_ -= rdt;
        if (overDelay_ <= 0) {
            overT_ = 0;
            // offer: opened below, once the life-lost animation has played
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
    meter_ = std::min(float(MAXE), meter_ + dt / (float(cv.regen) * dda_.regenMul(float(x_))));
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

    if (autoSurge_ && !autoSurgeFired_ && x_ > 4) { autoSurgeFired_ = true; chain_ = 0; startSurge(); }
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
            if (surging_) popup("+5", p + glm::vec3(0, 0.5f, 0), PopKind::Groove); // surging: no chain progress
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
            corns[i].missed = true; // counted once
            if (!surging_ && chain_ > 0) {
                chain_ = 0;
                popup("CHAIN LOST", {float(x_) + 0.4f, float(topNow) + 1.4f, 0}, PopKind::ChainLost);
            }
        }
    }
    const int s = static_cast<int>(std::floor(x_)) + bonus_;
    score_ = s;
    if (bonus_ != lastBonus_) { lastBonus_ = bonus_; scoreBump_ = 0.32f; }
}

void Game::updateHen(float dt) {
    Hen::Node& root = hen_.nodes[hen_.root];
    if (state_ != State::Dead) {
        Hen::Node& body = hen_.nodes[hen_.body];
        Hen::Node& head = hen_.nodes[hen_.head];
        flap_ = std::max(0.f, flap_ - dt * 3.2f);
        squash_ += (0 - squash_) * std::min(1.f, dt * 12);
        body.scale = {1 - squash_ * 0.5f, 1 + squash_, 1 - squash_ * 0.5f};
        const bool onSurface = state_ == State::Play && eggs_.empty() && vy_ == 0;
        const bool falling = state_ == State::Play && vy_ < -1;
        const bool tall = eggs_.size() > 4;
        runPhase_ += dt * (onSurface ? speed_ * 4.5f : 0);
        body.pos.y = onSurface ? std::abs(std::sin(runPhase_)) * 0.06f : std::sin(time_ * 3) * 0.012f;
        const float leanT = onSurface ? -0.14f : falling ? 0.2f : tall ? std::sin(time_ * 2.6f) * 0.05f - 0.04f : -0.05f;
        lean_ += (leanT - lean_) * std::min(1.f, dt * 8);
        root.rot = {0, 0, lean_};
        for (int k = 0; k < 2; ++k) {
            const float s = k == 0 ? -1.f : 1.f;
            float& rx = hen_.nodes[hen_.wings[k]].rot.x;
            if (falling) rx = s * (-0.7f - std::sin(time_ * 42) * 0.45f);
            else if (flap_ > 0) rx = s * (-(std::sin(flap_ * 18) * flap_ * 1.1f) - 0.02f);
            else if (tall) rx = s * (-0.35f - std::sin(time_ * 2.6f + s) * 0.12f);
            else rx = s * (onSurface ? -0.1f - std::abs(std::sin(runPhase_)) * 0.2f : -0.02f);
            hen_.nodes[hen_.legs[k]].rot.z = onSurface ? std::sin(runPhase_ + k * kPi) * 0.75f
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
        // jet trail from the battery pack
        if (state_ == State::Play) {
            const float px = root.pos.x - 0.26f, py = root.pos.y + 0.82f;
            for (int i = 0; i < 2; ++i)
                fxAdd_.emit({px + rng_.range(-0.03f, 0.03f), py + rng_.range(-0.03f, 0.03f), rng_.range(-0.05f, 0.05f)},
                            {rng_.range(-1.2f, -0.4f), rng_.range(-0.1f, 0.4f), rng_.range(-0.2f, 0.2f)}, rng_.range(0.3f, 0.5f), 0.11f, 0.01f,
                            rng_.next01() < 0.75f ? C().cyan : C().pink, 0.9f, 1.5f, -0.5f);
            if (onSurface && baseY_ < 0.01 && rng_.next01() < 0.5f)
                fxAdd_.emit({float(x_) + rng_.range(-0.1f, 0.1f), 0.03f, rng_.range(-0.15f, 0.15f)}, {rng_.range(-2, -0.5f), rng_.range(0.8f, 1.8f), rng_.range(-0.6f, 0.6f)},
                            rng_.range(0.25f, 0.4f), 0.045f, 0.02f, C().water, 1, 1, 9);
        }
    } else {
        deadT_ += dt;
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
    const glm::vec3 cyan = hexColor("#29e7ff"), red = hexColor("#ff2b4a");
    hen_.rimColor = glm::mix(glm::mix(cyan, red, danger_), glm::vec3(1.f), 0.35f);
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
    camera_.setFovBoost(surge::FOV_ADD * partyK_);
    const float dist = camera_.followDistance();
    camera_.setPose({camX_ + sx + drift * 0.8f - camKick_ - ie * 3, camY_ + 1.9f + sy + drift * 0.3f + ie * 4.5f,
                     dist * (1 - 0.38f * dze) - std::abs(drift) * 0.8f + ie * 6},
                    {camX_ + drift * 0.3f - camKick_ * 0.5f, camY_ + 0.1f + ie * 1.2f + dze * 0.3f, 0});
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
    for (const Egg& e : eggs_) emitEgg(e.pos, e.rot, e.scale, e.ring, e.ringRot);
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

    emitParty(out, pz);
    world_.emit(out, camX_, camY_, time_, pz, grade(gradeIdx_).light, text_.get(), surge::WINDOW_FLASH * pz * partyK_);
    worldGateLabel();
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

    buildHud();
}

} // namespace cs
