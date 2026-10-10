// Cuckoo Stack game logic, ported from the frame()/press()/lay()/knock()/die() loop of cuckoo-stack.html.
// Platform-free: input arrives as press()/pressAt(), output leaves as a RenderList.
#pragma once

#include "BeatClock.h"
#include "Camera.h"
#include "Campaign.h"
#include "Daily.h"
#include "Difficulty.h"
#include "Economy.h"
#include "Effects.h"
#include "Font.h"
#include "HudIcons.h"
#include "HudImage.h"
#include "Hen.h"
#include "Level.h"
#include "Monetization.h"
#include "RenderList.h"
#include "Services.h"
#include "Surge.h"
#include "Tuning.h"
#include "World.h"

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cs {

class Game {
public:
    enum class State { Title, Play, Dead };

    explicit Game(const GameServices& services);

    // Viewport in logical points (HUD units); safe-area insets in points.
    void resize(float widthPts, float heightPts);
    void setSafeInsets(float top, float bottom) { safeTop_ = top / uiScale_; safeBottom_ = bottom / uiScale_; }

    // Tap / click / space. pressAt also hit-tests the mute button (HUD points).
    void press();
    void pressAt(float xPts, float yPts);
    void toggleMute();

    // rawDt: real seconds since the previous frame
    void update(double rawDt);
    const RenderList& renderList() const { return list_; }

    State state() const { return state_; }
    int score() const { return score_; }
    bool muted() const { return muted_; }

    // A store purchase finished (platform IStore -> here). Credits coins / unlocks; see kProducts in Economy.h.
    void onPurchase(const PurchaseEvent& e);
    // App lifecycle: the platform reports foreground/background so sessions and reminders stay correct.
    void onForeground();
    void onBackground();
    // A link opened the app (challenge links: <site>/c?d=YYYY-MM-DD&m=412&n=Sam, or cuckoostack://c?...).
    void openLink(const std::string& url);

    // testing hooks (desktop --surge flag, tests/surge_test.cpp)
    struct SurgeStatus { int chain; bool surging; float surgeT, graceT, partyK; int smashed; float speed; };
    SurgeStatus surgeStatus() const { return {chain_, surging_, surgeT_, graceT_, partyK_, smashed_, speed_}; }
    void debugStartSurge() { if (state_ == State::Play) { chain_ = 0; startSurge(); } }
    void debugSetChain(int chain, float grace = 0.f) { chain_ = chain; graceT_ = grace; }
    void setDebugAutoSurge(bool on) { autoSurge_ = on; } // debug builds: surge 4 m into every run
    enum class Screen : uint8_t {
        None, AgeGate, Continue, Shop, Locker, Missions, DailyDrop, Settings, LevelUp, StreakSave, ParentalGate,
        NotifPrimer, ReplayPrimer, Starter, Dev
    };
    Screen screen() const { return screen_; }
    const Wallet& wallet() const { return wallet_; }
    const AdPolicy& adPolicy() const { return adPolicy_; }
    const Profile& profile() const { return profile_; }
    const Missions& missions() const { return missions_; }
    const Streak& streak() const { return dayStreak_; }
    const Progression& progression() const { return prog_; }
    // desktop screenshots (src/main.cpp --screen / --coins)
    void debugOpenScreen(const std::string& name);
    void debugAddCoins(int n) { wallet_.earn(n, "debug"); coinShown_ = float(wallet_.coins()); }
    void debugWear(const std::string& id) { wallet_.grant(id); wallet_.equip(id); applyCosmetics(); }
    // the Share Replay end card (EndCard.h), RGBA8
    std::vector<uint8_t> endCard(int width, int height, const ReplayMeta& meta) const;
    // tests: tap the first visible button whose label starts with `label` (returns false if none)
    bool tapButton(const std::string& label);
    int challengeMeters() const { return challenge_ ? challenge_->meters : 0; }
    // levels (tests, developer menu)
    int courseLevel() const { return campaign_.level(); }
    int levelAttempts() const { return campaign_.attempts(); }
    bool lastRunCleared() const { return runCleared_; }
    int coins() const { return wallet_.coins(); }
    void debugNearFinish() { debugNearFinish_ = true; }
    int liveSmashDebris() const { int n = 0; for (const auto& b : smashPool_) n += b.life > 0; return n; }

private:
    static constexpr int MAXE = 8;

    struct Egg { glm::vec3 pos{0.f}, rot{0.f}, scale{0.001f}; float age = 0, sway = 0, jig = 0, jd = 0; int ring = 0; float ringRot = 0; };
    enum class DebrisKind { Egg, Shard, Spark, Feather };
    struct Debris { DebrisKind kind; glm::vec3 pos, rot, scale, v, w; float life, ph; int ring; };
    struct Splat { glm::vec3 pos; float sx, sy, scale, age, k; };
    enum class PopKind { Groove, Sector, Perfect, Surge, Smashed };
    struct Popup { glm::vec3 world; std::string text; PopKind kind; float age, dur; };
    struct DayRecord { std::string date; int best = 0; double bestDist = 0; int attempts = 0; };

    // game flow (names match the web build)
    void reset();
    void start();
    void lay();
    void knock(int k);
    void finishRun(bool cleared = false); // the run is over for good (no continue / or the level was cleared)
    void completeLevel();                 // the finish-line celebration is over: the run ends as a clear
    void revive();             // Continue: back into the run where the hen fell
    void perfect(float top);
    void closeCall(float top);
    void die(bool ceiling);
    void updatePlay(float dt, float rdt);
    void updateHen(float dt);
    void updateDebris(float dt);
    void updateCamera(float rdt);
    void updateAmbient(float dt);

    // surge mode (tuning in Surge.h)
    void startSurge();
    void endSurge();
    void resetSurge();
    bool smashing() const { return surging_ || graceT_ > 0.f; }
    void smashAhead();
    int collapseSegment(Segment& sg, int level);
    void removeBarrier(Segment& sg);
    void smashFeedback(glm::vec3 at, int removed);
    void spawnSmashBlock(glm::vec3 pos, bool hay, bool flipped, float tint);
    void updateSmashDebris(float dt);
    void updateParty(float rdt);
    void emitParty(RenderList& out, float pz);
    void queueHaptic(float delay, int ms);

    // effects helpers
    void burst(glm::vec3 p, int n, std::initializer_list<glm::vec3> cols, float smin, float smax, float life, float size,
               float up = 0, float vx = 0, bool flat = false, float drag = 2.5f, float grav = 7.f);
    void puff(glm::vec3 p, int n, glm::vec3 col, float spread = 1, float alpha = 0.5f);
    void spawnShards(glm::vec3 p, int n, float vx);
    void spawnFeathers(glm::vec3 p, int n, float spread = 1);
    void spawnSplat(float x, float y);
    void popup(std::string text, glm::vec3 world, PopKind kind);
    void flashScreen(glm::vec3 color, float alpha);
    void buzz(int ms) { if (svc_.haptics) svc_.haptics->pulse(ms); }
    void sfx(Sfx s, int variant = 0) { if (svc_.audio) svc_.audio->play(s, variant); }

    // persistence
    void loadRecords();
    void levelGained(int levels);
    void queueSessionScreens();   // daily drop, streak save, starter offer: shown over the title one after another
    void primerAccepted(bool notifications);
    void primerDeclined(bool notifications);
    void onNotificationsAccepted();
    void rescheduleReminders();
    void saveDay();
    void rollDay();
    void placeGate();

    // output
    void buildRenderList();
    void buildHud();
    void hudRect(glm::vec2 center, glm::vec2 size, glm::vec3 color, float alpha, Shape shape = Shape::Solid, float param = 0, float rot = 0);
    void hudText(std::string_view text, const TextStyle& style, float x, float top, TextAlign align = TextAlign::Center);
    void hudTitleOverlay();
    void hudOverOverlay();
    void hudPill(std::string_view label, float bottom, float alpha);
    // the screens and panels of the meta-game (GameUi.cpp)
    void hudTopBar();
    void hudTitleMenu(float& bottom);
    void hudTitleHeader();
    void hudTitleNav(float rowY);
    glm::vec3 iconTint(Icon icon) const;
    void switchScreen(Screen s);
    float titleNavY_ = 0;
    void hudOverMenu(float bottom);
    void hudOverRewards(float& bottom);
    void hudShareReplayButton(glm::vec2 c);
    void hudScreen();
    void hudAgeGate(); void hudContinue(); void hudShop(); void hudLocker(); void hudMissions(); void hudDailyDrop();
    void hudDev();     // developer menu (staging builds only, from Settings)
    void hudSettings(); void hudLevelUp(); void hudStreakSave(); void hudParentalGate(); void hudPrimer(); void hudStarter();
    void hudToasts();
    void hudCoinFx();
    struct Panel { glm::vec2 c, size; float k; float top; };  // k: entrance progress 0..1; top: content start y
    Panel hudPanel(std::string_view tag, std::string_view title, float width, float contentH, glm::vec3 accent, bool closable);
    glm::vec2 hudCoinAmount(int amount, glm::vec2 left, float size, float alpha, TextAlign align = TextAlign::Left, bool plus = false);
    float coinAmountWidth(int amount, float size, bool plus = false) const;
    void hudIconButton(Icon icon, glm::vec2 c, float size, float alpha, std::function<void()> fn, int badge = 0, bool pulse = false);
    glm::vec2 hudTile(glm::vec2 c, glm::vec2 size, glm::vec3 accent, float alpha, bool selected, std::function<void()> fn);
    glm::vec2 hudActionButton(std::string_view label, const Icon* icon, int coinCost, glm::vec2 c, glm::vec3 color, float alpha, bool glow,
                              std::function<void()> fn, float scale = 1.f);
    void hudTextButton(std::string_view label, glm::vec2 c, float alpha, std::function<void()> fn);
    void hudCoinPill(bool interactive);
    void worldGateLabel();
    void worldMarkerLabel(double x, float opacity, std::string_view top, std::string_view bottom, glm::vec3 topColor);
    std::string dayLabel() const;

    GameServices svc_;      // every pointer set (null services replaced by no-op ones)
    FastRandom rng_;
    Dda dda_;
    Level level_;
    World world_;
    Hen hen_;
    BeatClock beat_;
    ParticleSystem fxAdd_{5600}, fxSmoke_{700}; // fxAdd: room for the outfit auras plus surge fireworks
    Shockwaves rings_;
    Camera camera_;
    RenderList list_;
    FontAtlas fonts_;
    HudImage hudIcons_;
    std::unique_ptr<TextLayout> text_;

    State state_ = State::Title;
    double x_ = 0, baseY_ = 0, vy_ = 0;
    float speed_ = 0, meter_ = MAXE;
    int bonus_ = 0, cornCount_ = 0, score_ = 0, best_ = 0, streak_ = 0, gradeIdx_ = 0, sector_ = 1, prevFull_ = MAXE, lastBonus_ = 0;
    float hop_ = 0, hopV_ = 0, flap_ = 0, squash_ = 0, deadT_ = 0, runPhase_ = 0, lean_ = 0;
    float camX_ = 1.6f, camY_ = 1.2f, shake_ = 0, intro_ = 0, camKick_ = 0;
    float timeScale_ = 1, freeze_ = 0, chromaKick_ = 0, danger_ = 0;
    bool needEggs_ = false, muted_ = false;
    glm::vec3 henV_{0.f}, henW_{0.f};
    float time_ = 0, rainAcc_ = 0, moteAcc_ = 0;
    int discoStep_ = 0; float discoLastPulse_ = 0, discoTimer_ = 0;
    double gateX_ = -1; bool gateVisible_ = false, gatePassed_ = false; float gateOpacity_ = 1;
    float padOpacity_ = 0.85f, rdt_ = 0.016f;
    DayRecord day_;

    std::vector<Egg> eggs_;
    std::vector<Debris> debris_;
    std::vector<Splat> splats_;
    std::vector<Popup> popups_;

    // surge mode state
    struct SmashBlock { glm::vec3 pos{0.f}, v{0.f}, rot{0.f}, w{0.f}; float life = 0, tint = 1; bool hay = false, flipped = false; };
    std::array<SmashBlock, surge::DEBRIS_POOL> smashPool_{};   // fixed pool: no allocation per hit
    int smashNext_ = 0;
    int chain_ = 0, smashed_ = 0;
    bool autoSurge_ = false, autoSurgeFired_ = false;
    bool surging_ = false;
    float surgeT_ = 0, graceT_ = 0, surgeSpeedMul_ = 1;
    float partyK_ = 0, partyTime_ = 0, confettiAcc_ = 0, raveTimer_ = 0;
    // surge fireworks: rockets launched from around the hen that trail up and burst (updateFireworks)
    struct Firework { glm::vec3 p, v; float fuse, hue; int kind; };
    std::vector<Firework> fireworks_;
    float fireworkAcc_ = 0, sparkleAcc_ = 0;
    void launchFirework(float delay = 0.f);
    void updateFireworks(float rdt);
    struct PendingHaptic { float delay; int ms; };
    std::array<PendingHaptic, 6> haptics_{};

    // grade-driven scene colours
    glm::vec3 fog_, skyMid_, skyBot_, skyHaze_, pinkLight_;

    // HUD
    // The HUD is laid out in points of a virtual screen at least kUiDesignHeight tall: on shorter phones (iPhone SE,
    // 667 pt) everything is drawn smaller by uiScale_ (< 1) so the layouts keep their spacing instead of colliding.
    // viewW_ / viewH_ / safe insets are in those virtual points; the platform's points are divided by uiScale_.
    static constexpr float kUiDesignHeight = 780.f, kUiMinScale = 0.8f;
    float uiScale_ = 1;
    float viewW_ = 1, viewH_ = 1, safeTop_ = 0, safeBottom_ = 0;
    glm::vec3 flashColor_{0.f}; float flashAlpha_ = 0, flashT_ = 1;
    float meterShake_ = 0, scoreBump_ = 0, overDelay_ = -1, overT_ = 0;
    float popT_[MAXE] = {};
    int finalScore_ = 0, finalDist_ = 0;
    float titleT_ = 0;
    std::string overTag_, overNote_;
    float overButtonRowBottom_ = 0; // game-over: bottom edge (HUD points) of the free row below 'Tap to reboot'
    float overCoinsY_ = 0;          // game-over: where the run's coins row sits (coins fly from here)

    // ---- meta-game: coins, cosmetics, ads, level, streak, missions, daily drop (Economy/Monetization/Daily.h)
    Tuning tune_;
    Profile profile_;
    Wallet wallet_;
    AdPolicy adPolicy_;
    Progression prog_;
    Streak dayStreak_;
    Missions missions_;
    DailyDrop drop_;
    Campaign campaign_;
    float clearT_ = -1;                   // seconds since crossing the finish line (-1: not crossed this run)
    bool runCleared_ = false;
    bool debugNearFinish_ = false;
    bool titleMusic_ = false;             // the soundtrack has been started on the title screen        // developer menu: the next run starts 30 m before the finish line             // the last run ended at the finish line (Level Cleared screen)
    int clearCoins_ = 0;                  // coins for that clear
    void applyCosmetics();
    void track(const char* event, AnalyticsParams params = {});

    // run statistics (missions, coins, analytics)
    struct RunStats { int perfects = 0, closeCalls = 0, eggs = 0, surges = 0, continues = 0; float seconds = 0; bool boosted = false; };
    RunStats run_;
    int runCoins_ = 0, runXp_ = 0;            // earned by the last finished run
    bool coinsDoubled_ = false, newBestRun_ = false;
    enum class Boost : uint8_t { None, Surge, Overclock };
    Boost boost_ = Boost::None;               // armed for the next run (title screen)
    Boost runBoost_ = Boost::None;            // the current run's boost
    Trail trail_ = Trail::None;
    float auraAcc_[4] = {};          // per-layer emission accumulators (emitAura)
    glm::vec3 auraPrev_{0.f};        // hen position last frame, so aura particles ride along with it
    CrashFx crash_ = CrashFx::Feathers;
    float trailAcc_ = 0;
    void emitTrail(float dt);
    void emitAura(float dt);
    void crashFx(glm::vec3 p);
    void missionProgress(int completedMask);

    // ---- rewarded ads and the interstitial
    struct PendingReward { Placement placement; std::function<void()> grant; };
    std::optional<PendingReward> pendingReward_;
    float pendingRewardT_ = 0;
    bool pendingStart_ = false;               // the next run starts once the interstitial is dismissed
    bool continuePending_ = false;            // the Continue panel opens once the crash has played
    float deadRealT_ = 0;                     // real seconds since the crash (deadT_ runs in slow motion)
    bool replaySaved_ = false;                // this crash's replay clip was requested
    // a friend's challenge on today's course (challenge links): their distance shows as a pink marker
    struct Challenge { std::string day, name, id; int meters = 0; bool beaten = false; float opacity = 1; };
    std::optional<Challenge> challenge_;
    void saveChallenge();
    std::string challengeLink(int meters) const;
    // share flow: with nudges available the link waits (briefly) for a server challenge id
    struct PendingShare { std::string caption; int meters; float wait; };
    std::optional<PendingShare> pendingShare_;
    std::string sharedChallengeId_;
    bool beatenPending_ = false;
    void doShare(const std::string& caption, int meters, const std::string& id);
    void shareReplay();
    bool requestReward(Placement p, std::function<void()> grant); // false: no ad ready (caller shows a message)
    bool rewardReady(Placement p) const;
    void pollAds();

    // ---- screens (modal panels); hits are rebuilt every frame by the HUD, topmost last
    Screen screen_ = Screen::None;
    float screenT_ = 0;
    std::vector<Screen> screenStack_;
    std::deque<Screen> screenQueue_;          // shown one after another once the game-over / title overlay is up
    void openScreen(Screen s);
    void closeScreen();
    void queueScreen(Screen s);
    struct HitRect { glm::vec2 c{0.f}, half{0.f}; bool hit(float x, float y) const { return std::abs(x - c.x) <= half.x && std::abs(y - c.y) <= half.y; } };
    struct UiHit { HitRect r; std::function<void()> fn; std::string label; };
    std::vector<UiHit> hits_;
    std::function<void()> primary_;           // keyboard / space while a screen is open
    std::string nextHitLabel_;
    void uiHit(glm::vec2 c, glm::vec2 size, std::function<void()> fn);
    Slot lockerTab_ = Slot::Outfit;
    int devTab_ = 0;   // developer menu: 0 coins, 1 outfits, 2 trails, 3 crash fx, 4 game
    std::string lockerPreview_;               // outfit shown on the hen while browsing
    int gateA_ = 0, gateB_ = 0, gateAnswer_ = -1; std::array<int, 4> gateChoices_{}; std::function<void()> gateThen_;
    int ageYear_ = 0;                         // age gate wheel (0 = nothing picked yet)
    float continueT_ = 0;
    bool continueOffered_ = false;
    int levelUps_ = 0, chestCoins_ = 0, levelFrom_ = 1;
    float xpFrom_ = 0, chestT_ = 0, dropT_ = 0, lockerK_ = 0;
    // Locker showroom: the camera swings to a close 3/4 view of the hen, which jogs in place and turns slowly wearing
    // the previewed outfit (aura) and trail. Works from the title and from game over (the hen is reassembled).
    bool showroom() const { return screen_ == Screen::Locker || lockerK_ > 0.02f; }
    // after a crash the hen is stood on a clear, flat patch of ground behind the wall it hit (showroomSpot)
    bool showPlaced_ = false;
    glm::vec2 showSpot_{0.f};
    glm::vec2 showroomSpot() const;
    bool chestOpened_ = false, chestDoubled_ = false;
    int dropClaimed_ = -1, dropCoins_ = 0;
    float rewardMsgT_ = 0; std::string rewardMsg_;   // "Ad not ready" etc.
    void showMessage(std::string msg) { rewardMsg_ = std::move(msg); rewardMsgT_ = 2.2f; }
    void startPurchase(const std::string& productId);
    void parentalGate(std::function<void()> then);
    std::vector<Product> products_;
    float productsRefreshT_ = 0;

    // ---- HUD feedback: flying coins, the count-up, toasts, sparks
    float coinShown_ = 0;                      // animated coin counter
    float coinPulse_ = 0;
    glm::vec2 coinCounterPos_{0.f};
    struct FlyCoin { glm::vec2 from, ctrl; float t, delay; int value; bool live; };
    std::array<FlyCoin, 24> flyCoins_{};
    void flyCoins(glm::vec2 from, int amount, int pieces);
    struct Toast { std::string text; Icon icon; int coins; float t; };
    std::deque<Toast> toasts_;
    void toast(std::string text, Icon icon, int coins = 0);
    struct HudSpark { glm::vec2 p{0.f}, v{0.f}; float rot = 0, spin = 0, life = 0, maxLife = 1, size = 4; glm::vec3 color{1.f}; bool coin = false; };
    std::array<HudSpark, 64> sparks_{};
    void hudBurst(glm::vec2 at, int n, std::initializer_list<glm::vec3> cols, float speed, bool coins = false);
    void updateHudFx(float rdt);
    void hudSprite(Icon icon, glm::vec2 center, float size, float rot = 0, glm::vec3 tint = glm::vec3(1.f), float alpha = 1, int frame = 0);
    void hudCoin(glm::vec2 center, float size, float alpha, float spin = -1); // spin < 0: animated by time
    void hudAtlasQuad(glm::vec4 uv, glm::vec2 center, glm::vec2 size, float rot, glm::vec3 tint, float alpha);
    void hudFrost(glm::vec2 c, glm::vec2 size, float radius, float amount);
    void hudNineSlice(Icon frame, glm::vec2 center, glm::vec2 size, float corner, glm::vec3 tint, float alpha, float slice = 0.25f);
    // pill button; `fn` (if any) runs when it's tapped. Returns its size.
    glm::vec2 hudButton(std::string_view label, glm::vec2 center, glm::vec3 color, float alpha, bool glow, std::function<void()> fn = {}, float scale = 1.f);
    bool privacyButton_ = false; // title-screen 'Privacy settings' (UMP), refreshed each frame on the title
};

} // namespace cs
