// Cuckoo Stack game logic, ported from the frame()/press()/lay()/knock()/die() loop of cuckoo-stack.html.
// Platform-free: input arrives as press()/pressAt(), output leaves as a RenderList.
#pragma once

#include "Lives.h"
#include "BeatClock.h"
#include "Camera.h"
#include "Difficulty.h"
#include "Effects.h"
#include "Font.h"
#include "HudImage.h"
#include "Hen.h"
#include "Level.h"
#include "RenderList.h"
#include "Services.h"
#include "Surge.h"
#include "World.h"

#include <array>
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
    void setSafeInsets(float top, float bottom) { safeTop_ = top; safeBottom_ = bottom; }

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

    // testing hooks (desktop --surge flag, tests/surge_test.cpp)
    struct SurgeStatus { int chain; bool surging; float surgeT, graceT, partyK; int smashed; float speed; };
    SurgeStatus surgeStatus() const { return {chain_, surging_, surgeT_, graceT_, partyK_, smashed_, speed_}; }
    void debugStartSurge() { if (state_ == State::Play) { chain_ = 0; startSurge(); } }
    void debugSetChain(int chain, float grace = 0.f) { chain_ = chain; graceT_ = grace; }
    void setDebugAutoSurge(bool on) { autoSurge_ = on; } // debug builds: surge 4 m into every run
    bool offerOpen() const { return offerOpen_; }
    int liveSmashDebris() const { int n = 0; for (const auto& b : smashPool_) n += b.life > 0; return n; }

private:
    static constexpr int MAXE = 8;

    struct Egg { glm::vec3 pos{0.f}, rot{0.f}, scale{0.001f}; float age = 0, sway = 0, jig = 0, jd = 0; int ring = 0; float ringRot = 0; };
    enum class DebrisKind { Egg, Shard, Spark, Feather };
    struct Debris { DebrisKind kind; glm::vec3 pos, rot, scale, v, w; float life, ph; int ring; };
    struct Splat { glm::vec3 pos; float sx, sy, scale, age, k; };
    enum class PopKind { Groove, Sector, Perfect, Surge, Smashed, ChainLost };
    struct Popup { glm::vec3 world; std::string text; PopKind kind; float age, dur; };
    struct DayRecord { std::string date; int best = 0; double bestDist = 0; int attempts = 0; };

    // game flow (names match the web build)
    void reset();
    void start();
    void lay();
    void knock(int k);
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
    void worldGateLabel();
    std::string dayLabel() const;

    GameServices svc_;
    NullAudio nullAudio_;
    NullAds nullAds_;
    FastRandom rng_;
    Dda dda_;
    Level level_;
    World world_;
    Hen hen_;
    BeatClock beat_;
    ParticleSystem fxAdd_{3200}, fxSmoke_{700};
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
    struct PendingHaptic { float delay; int ms; };
    std::array<PendingHaptic, 6> haptics_{};

    // grade-driven scene colours
    glm::vec3 fog_, skyMid_, skyBot_, skyHaze_, pinkLight_;

    // HUD
    float viewW_ = 1, viewH_ = 1, safeTop_ = 0, safeBottom_ = 0;
    glm::vec3 flashColor_{0.f}; float flashAlpha_ = 0, flashT_ = 1;
    float meterShake_ = 0, scoreBump_ = 0, overDelay_ = -1, overT_ = 0;
    float popT_[MAXE] = {};
    int finalScore_ = 0, finalDist_ = 0;
    float titleT_ = 0;
    std::string overTag_, overNote_;
    float overButtonRowBottom_ = 0; // game-over: bottom edge (HUD points) of the free row below 'Tap to reboot'

    // lives + rewarded-ad offer (Lives.h)
    int lives_ = lives::START;
    bool offerOpen_ = false, offerPending_ = false;
    float offerT_ = 0;                          // seconds the offer has been open
    struct HitRect { glm::vec2 c{0.f}, half{0.f}; bool hit(float x, float y) const { return std::abs(x - c.x) <= half.x && std::abs(y - c.y) <= half.y; } };
    HitRect offerButton_, offerClose_;
    void saveLives();
    void openOffer();
    void watchAd();
    void grantLives();
    bool offerPlayAnyway() const;
    // lives badge: one heart sprite + the count, with the life-lost / lives-gained animations
    enum class LivesFrame { Full = 0, LeftHalf = 1, RightHalf = 2, Crack = 3, Empty = 4, Close = 5 }; // sprite sheet frames
    void hudSprite(LivesFrame frame, glm::vec2 center, float size, float rot, glm::vec3 tint, float alpha);
    void hudLivesBadge(glm::vec2 left, float iconSize, float alpha); // `left`: icon's left edge, vertical centre
    float livesBadgeWidth(float iconSize) const;
    void updateLivesAnim(float rdt);
    struct HudShard { glm::vec2 p{0.f}, v{0.f}; float rot = 0, spin = 0, life = 0, maxLife = 1, size = 4; glm::vec3 color{1.f}; bool heart = false; };
    std::array<HudShard, 18> shards_{};
    float lifeAnimT_ = -1.f, gainAnimT_ = -1.f; // seconds since a life was lost / lives were granted; <0 = idle
    int lifeFrom_ = 0, lifeTo_ = 0;
    bool lifeSplitDone_ = false;
    glm::vec2 badgeIconCenter_{0.f};
    void hudOffer();
    glm::vec2 hudButton(std::string_view label, glm::vec2 center, glm::vec3 color, float alpha, bool glow);
    bool privacyButton_ = false; // title-screen 'Privacy settings' (UMP), refreshed each frame on the title
};

} // namespace cs
