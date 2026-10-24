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
#include "HudImg.h"
#include "Touch.h"
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
    // The level editor's play-test: every run on `level` (cleared, it's played again, not the next), from `x` metres
    // along its course (her meter full, no eggs), or from a cell of its maze gauntlet (`col`, `row`: the cell her feet
    // are in; `dir`: the way she runs, 1 right, -1 left). Just `level`: every run on it, from its start (or, with
    // debugNearGauntlet, its gauntlet's door)
    struct PlayTest { int level = 0; double x = -1; int col = -1, row = -1, dir = 1; };
    // How long the end of a run plays before any panel (and before the replay clip ends): the crash's slow-motion
    // shatter, and the finish line's runway, fireworks and applause. Real seconds.
    static constexpr float kCrashTail = 2.5f;
    static constexpr float kFinishCelebration = 5.5f;
    static constexpr float kMinReplaySeconds = 3.f; // shorter runs make no replay clip
    enum class State { Title, Play, Dead };
    enum class Death { Crash, Sliced, Burned }; // how the hen's run ended (die)

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

    // testing hooks (desktop --surge flag, GameSurge_test.cpp)
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
    // tests: the buttons on screen after the last update (label, centre, half size, in HUD points)
    struct Button { std::string label; glm::vec2 c, half; };
    glm::vec2 hudSize() const { return {viewW_, viewH_}; } // the HUD's own points (small screens are scaled to fit)
    std::vector<Button> buttons() const { std::vector<Button> b; for (const auto& h : hits_) b.push_back({h.label, h.r.c, h.r.half}); return b; }
    // tests: every HUD string drawn by the last update (off unless switched on)
    struct HudString { std::string text; TextStyle style; float x, top, width; TextAlign align; };
    void debugRecordHud(bool on) { recordHud_ = on; hudStrings_.clear(); }
    const std::vector<HudString>& hudStrings() const { return hudStrings_; }
    int challengeMeters() const { return challenge_ ? challenge_->meters : 0; }
    // levels (tests, developer menu)
    int courseLevel() const { return campaign_.level(); }
    int levelAttempts() const { return campaign_.attempts(); }
    bool lastRunCleared() const { return runCleared_; }
    // low-end device (the platform's FramePacer): no street reflection, rain or wind streaks; the cyber cubes without
    // animation
    void setLite(bool on); // (remembered: the next launch starts lite on this device)
    bool lite() const { return lite_; }
    float runSeconds() const { return run_.seconds; } // play time this run (revives included)
    // tests: the hen's height above the street, and whether the street (not a wall) is right below her
    const Hen& hen() const { return hen_; }
    glm::vec4 levelCompleteBanner() const { return completeBox_; } // centre x, y, width, height; zero when hidden
    static constexpr float kHenTop = 1.4f; // the top of the hen's comb above her feet (world m)
    bool henTopOnScreen(glm::vec2& px) const { return camera_.project(hen_.nodes[hen_.root].pos + glm::vec3(0.f, kHenTop, 0.f), px); }
    bool worldOnScreen(const glm::vec3& p, glm::vec2& px) const { return camera_.project(p, px); } // (tests: HUD points)
    float progressBarBottom() const { return barBottom_; }
    float henHeight() const { return hen_.nodes[hen_.root].pos.y; }
    // what's under her feet (either side of her middle): a floating block's top when she's up on it
    double supportY() const { return std::max(level_.floorAt(x_ - 0.22, baseY_), level_.floorAt(x_ + 0.22, baseY_)) * U; }
    bool streetBelowHen() const { return level_.heightAt(state_ == State::Dead ? double(hen_.nodes[hen_.root].pos.x) : x_) <= 0; }
    bool replayTooShort() const { return replayTooShort_; }
    int coins() const { return wallet_.coins(); }
    void debugNearFinish() { debugNearFinish_ = true; }
    // profiling (debug builds log it for slow frames): what this frame draws and what's happening
    std::string frameSummary() const;
    int liveSmashDebris() const { int n = 0; for (const auto& b : smashPool_) n += b.life > 0; return n; }

private:
    static constexpr int MAXE = 8;

    // cut: how far a saw has sawn through it (0: not being sawn; GameSaw.cpp)
    // (drop: an egg over one a laser cut out, still falling onto the one below, metres; burn: the egg under her heating
    // on lava, 0 .. 1 as she crosses the cell)
    struct Egg { glm::vec3 pos{0.f}, rot{0.f}, scale{0.001f}; float age = 0, sway = 0, jig = 0, jd = 0; int ring = 0; float ringRot = 0, cut = 0, drop = 0, dropV = 0, burn = 0; };
    // EggHalf: half of a sawn egg (its rot is an axis * angle tumble on top of `basis`, its orientation when sawn; a
    // laser's halves are cut level, their cut faces red-hot); Yolk: the yolk spilling out of it; Blade: a saw blade
    // smashed off its slot by OVERDRIVE; Burnt: an egg burning in lava, charring, sinking, crumbling to ash
    enum class DebrisKind { Egg, Shard, Spark, Feather, EggHalf, Yolk, Blade, Burnt };
    // (zapped: an egg the droid's laser knocked out of the stack: it glows and crackles blue as it flies, at first;
    // heat: a laser's halves' cut faces glowing, 1 .. 0)
    struct Debris { DebrisKind kind; glm::vec3 pos, rot, scale, v, w; float life, ph; int ring; glm::mat3 basis{1.f}; bool zapped = false; float heat = 0; };
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
    void closeCall(float top, const char* label = "CLOSE CALL");
    static constexpr float kNearChain = 1.2f; // near misses this close (game seconds) chain
    // the hen's end: a crash (into a wall, `ceiling`: up into a barrier), cut in half by a laser's beam at `cutY`, or
    // burned up in lava (Death)
    void die(bool ceiling, Death how = Death::Crash, float cutY = 0);
    void updatePlay(float dt, float rdt);
    void updateHen(float dt);
    void titleIdle(float dt);
    // the title screen's jet droid (GameDroid.cpp): circles the hen, boosts away when a run starts
    // pitch: + noses it down; faceCamera: turned partly toward the camera (the title's 3/4 view)
    struct DroidPose { glm::vec3 pos, dir; float bank, k, pitch, scale; bool faceCamera; };
    DroidPose droidPose() const;
    // its raids on the stack in a run (GameDroid.cpp)
    struct DroidRaid {
        bool on = false, charged = false;
        // orbitT: its circling clock (paused to shoot); topY: the stack top it circles over, eased; lock: 0 circling .. 1 at its firing spot
        float t = 0, orbitT = 0, theta0 = 0, leaving = -1, topY = 0, lock = 0;
        int shots = 1, fired = 0;
        float fireAt[2] = {0, 0}, beamT[2] = {-1, -1};
        glm::vec3 spawn{0.f}, leaveFrom{0.f}, leaveDir{1.f, 0.f, 0.f}, beamTo[2]{}; // relative to the hen (beamTo: to her x)
    };
    glm::vec3 raidOrbit(float t) const;
    glm::vec3 raidAnchor() const;
    void planRaids();
    bool raidAllowed(int shots) const;
    void startRaid(int shots);
    void zapEgg(int shot);
    void beginDroidExit();
    void updateDroid(float rdt);
    void emitDroid(RenderList& out, bool onStreet);
    void updateDebris(float dt);
    // saw blades (GameSaw.cpp): the stack crossing one, the cut, the egg falling apart; the blades and the halves drawn
    void updateSaws(float dt);
    void animateSaws(float rdt); // every frame, in play or not: the spin, spent blades sinking, the slash and zoom fading
    void sawEgg(Saw& s);
    void sawHen(Saw& s);
    void smashSaw(Saw& s);
    glm::mat4 stackEggMatrix(size_t i) const; // an egg of the stack as it's drawn (lying on its side, rolling)
    glm::vec3 sawColor() const;               // the zone's colour for the blades' teeth
    void emitSaws(RenderList& out, float pz);
    void emitSawBlade(RenderList& out, const glm::mat4& hub, float spin, glm::vec3 teeth, float glowK);
    void emitSawDebris(RenderList& out, const Debris& d);
    // wall lasers (GameLaser.cpp): charging as she comes near, firing as its row meets her: an egg cut out of her stack
    // level with the beam, or the hen cut in half; the turrets, their pointers and beams drawn
    void updateLasers(float dt);
    int laserMeets(const Laser& z, bool* brush) const;
    void coolLasers(float dt);
    void laserEgg(Laser& z, size_t i);
    void laserHen(Laser& z);
    void emitLasers(RenderList& out, float pz);
    // lava (GameLava.cpp): the egg under her burning a cell at a time, the hen burning up; the molten floor drawn
    void updateLava(float dt);
    void burnEgg(const Lava& f);
    void burnHen(const Lava& f);
    void emitLava(RenderList& out, float pz);
    // springs (GameSpring.cpp): she runs over one and is launched; the pad and its coil drawn
    void collectDiscos(double topNow);
    void updateSprings(float dt);
    void launchSpring(Spring& s);
    void springFx(Spring& s); // (its boing, sparks and rings, the stack's jig: launchSpring's, and a maze's)
    void emitSprings(RenderList& out, float pz);
    void emitBurnt(RenderList& out, const Debris& d);
    void updateCamera(float rdt);
    void updateAmbient(float dt);

    // surge mode (tuning in Surge.h)
    void startSurge();
    void endSurge();
    void shedAfterSurge();
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
    void hudLevelComplete();
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

    GameServices svc_;      // every pointer set (null services replaced by no-op ones)
    FastRandom rng_;
    // the scenery's own stream (rain, signs, traffic): what it draws never shifts the run's (debugSeed: repeatable runs)
    FastRandom sceneRng_;
    Dda dda_;
    Level level_;
    World world_;
    Hen hen_;
    BeatClock beat_;
    ParticleSystem fxAdd_{5600}, fxSmoke_{700}; // fxAdd: room for the outfit auras plus surge fireworks
    Shockwaves rings_;
    Camera camera_;
    RenderList list_;
    // the text's SDF atlas and the HUD's sprites: one each for every game in the process (built by the first game that
    // asks: a second or more of work, once; then the play-test and every bot run read them)
    const FontAtlas& fonts_;
    const HudImage& hudIcons_;
    std::unique_ptr<TextLayout> text_;

    State state_ = State::Title;
    double x_ = 0, baseY_ = 0, vy_ = 0;
    float speed_ = 0, meter_ = MAXE;
    int bonus_ = 0, cornCount_ = 0, score_ = 0, best_ = 0, streak_ = 0, gradeIdx_ = 0, sector_ = 1, prevFull_ = MAXE, lastBonus_ = 0;
    float idleT_ = 0; // time into the title screen's idle loop
    float hop_ = 0, hopV_ = 0, flap_ = 0, squash_ = 0, deadT_ = 0, runPhase_ = 0, lean_ = 0;
    float camX_ = 1.6f, camY_ = 1.2f, shake_ = 0, intro_ = 0, camKick_ = 0;
    float timeScale_ = 1, freeze_ = 0, chromaKick_ = 0, danger_ = 0;
    bool needEggs_ = false, muted_ = false;
    glm::vec3 henV_{0.f}, henW_{0.f};
    float time_ = 0, rainAcc_ = 0, moteAcc_ = 0;
    int discoStep_ = 0; float discoLastPulse_ = 0, discoTimer_ = 0;
    double gateX_ = -1; bool gateVisible_ = false, gatePassed_ = false; float gateOpacity_ = 1;
    float rdt_ = 0.016f;
    DayRecord day_;

    std::vector<Egg> eggs_;
    std::vector<Debris> debris_;
    // maze gauntlets (GameGauntlet.cpp): the one she's in (g null: none) and her way through it (+1 right, -1 left), at
    // the run's full speed;
    // the camera's blend into its follow, and its lead her way (-1..1, eased round on a bounce); which way she faces
    // (radians about y, eased round too); a bumper's flash (where, how much left); the furthest she's got (distance,
    // score, progress: in a maze she turns back)
    PlacedGauntlet gIn_{};
    int gDir_ = 1;
    int gMoves_ = 0, gBotLay_ = -99; // cells her front edge has crossed in it (and bounces); when the bot last laid
    // a spring's flight in a maze (GameGauntlet.cpp): the solver's plan (Gauntlet::flightFrom), the time into it, the bounces
    // played so far. Her x and height follow it exactly, so she lands where the solver says
    bool gFly_ = false;
    Gauntlet::Flight gPlan_;
    double gFlyT_ = 0;
    size_t gFlyBounce_ = 0;
    Spring* mazeSpringAt(int col, int row); // (the pad drawn on a maze's or a zone's spring cell, or null)
    void flyGauntlet(float dt);
    void launchGauntlet(int col, int row, double over); // (a spring cell under her middle: the flight, `over` seconds into it)
    bool gBounced_ = false;          // her last move a bounce: she's still in the column she bounced in, as the solver has her
    float gK_ = 0, gLead_ = 1, henYaw_ = 0, gFlash_ = 0;
    // a squeeze in a maze: her head brushing what's over her since this time (-1: none), over this many columns; it's
    // a CLOSE CALL when it ends, unless a near miss came in it
    float gSqueezeFrom_ = -1;
    int gSqueezeCols_ = 0;
    // near misses in quick succession chain (CLOSE SHAVE x3): the last one's time, the chain so far; the last cut's time
    // (saws cut in quick succession skip the slow motion and the zoom after the first)
    float nearMissT_ = -10, sawCutT_ = -10;
    int nearChain_ = 0;
    glm::vec3 gFlashAt_{0.f};
    double maxX_ = 0;
    bool headless_ = false;          // setHeadless
    bool debugNearGauntlet_ = false; // developer menu: the next run starts just before its level's gauntlet
    bool debugNearGauntletEvery_ = false; // (every run, not just the next: the level editor's play-test)
    PlayTest playTest_{};                 // the level editor's play-test: its level, where every run starts
    void enterGauntlet(const PlacedGauntlet& p);
    void leaveGauntlet();
    void updateGauntlet(float dt);
    void gauntletBounce(int c, int r);
    void emitGauntlets(RenderList& out, float pz);
    void endSqueeze(); // (a maze's squeeze is over: its CLOSE CALL, if it earned one)
    void emitInscriptions(RenderList& out, float lo, float hi); // (a tomb's hieroglyphs, from emitGauntlets)
    void wallMaterials(float pz, LitMaterial& crate, LitMaterial& hay, float& courses) const; // the zone's walls
    // saws (GameSaw.cpp): the one sawing the bottom egg now (its x, -1: none; and its height: in a maze one may sit right
    // over another, on another floor); the blades' spin, in real time so they
    // scream on through a hit-pause; the stack settling one egg down after a cut (cosmetic: metres still to fall)
    static constexpr float kSawSpin = 11.f; // rad/s (the spokes and teeth stay readable at 60 fps)
    double cutSawX_ = -1;
    int cutSawY_ = 0;
    bool cutting(const Saw& s) const { return s.x == cutSawX_ && s.y == cutSawY_; }
    float sawSpin_ = 0, sawDrop_ = 0, sawDropV_ = 0;
    // the slash of light along the cut as a sawn egg falls apart (real seconds since; < 0: none)
    struct SawSlash { glm::vec3 pos{0.f}; float angle = 0, t = -1; } sawSlash_;
    float sawZoom_ = 0; // ...and the camera's punch in on it (1 at the split, easing out)
    // lasers: the hen dropping onto what's left after a beam cut an egg out under her (metres still to fall); lava: the
    // cell of it she's on (its start and the cell's index along it: a new one burns an egg; -1: off it)
    float henDrop_ = 0, henDropV_ = 0;
    bool zapped_ = false, brushed_ = false; // a laser hit her (its beam passed over her head) this pass: none again till she's out of every reach
    double lavaAt_ = -1;
    int lavaCell_ = -1;
    bool launched_ = false; // a spring has her in the air (till she's down on a surface)
    int springsFired_ = 0;  // this run (tests)
    Death death_ = Death::Crash;
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
    float droidT_ = 0, droidExitT_ = -1;      // the title droid's clock; seconds into its exit (-1: not leaving)
    glm::vec3 droidExitFrom_{0.f}, droidExitDir_{1.f, 0.f, 0.f};
    DroidRaid raid_;
    struct Bot { bool on = false, loop = false, sloppy = false, short_ = false; int spare = 0; float cooldown = 0, restartT = 0; double rolledFor = -1; } bot_;
    void updateBot(float rdt);
    static int zoneGrade(Zone z) { return z == Zone::Egypt ? 4 : z == Zone::Japan ? 5 : 0; } // the colour grade per zone (Game.cpp grade())
    std::vector<double> raidPlan_;            // this run's raids: where along the course they fall due
    size_t raidNext_ = 0;
    bool debugRaids_ = false;                 // developer menu: the next run's droid raids come every ~35 m
    bool debugSaws_ = false;                  // developer menu: the next run's course has saws (any level), ~one per 25 m
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
    bool recordHud_ = false;
    std::vector<HudString> hudStrings_;
    float barBottom_ = 0;                 // the HUD's level progress bar (or overdrive timer): its bottom edge
    glm::vec4 completeBox_{0.f};          // the "Level Complete" banner on screen: centre x, y, width, height (0: hidden)
    float clearT_ = -1;                   // seconds since crossing the finish line (-1: not crossed this run)
    bool runCleared_ = false;
    bool debugNearFinish_ = false;
    bool debugCrash_ = false;      // developer menu (Show continue): crash the run just started, to bring up the Continue offer
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
    bool lite_ = false;                       // low-end device: lighter effects (setLite)
    bool runOver_ = false;                    // finishRun has run: no revive any more, the clip can be requested
    bool replayRequested_ = false;            // the run's clip was requested (or it was too short for one)
    bool replayTooShort_ = false;             // ... too short: the results say so
    // a friend's challenge on today's course (challenge links): their distance shows as a pink marker
    struct Challenge { std::string day, name, id; int meters = 0; bool beaten = false; float opacity = 1; };
    std::optional<Challenge> challenge_;
    void saveChallenge();
    bool beatenPending_ = false;
    void shareReplay();
    void hudReplayTooShort(glm::vec2 c);
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
    // a scrolling list (one per screen): its viewport, set while drawing; y = how far it's scrolled (points), v = fling
    // velocity; a press inside it waits: a tap fires on release, a drag scrolls
    struct Scroll {
        HitRect view; bool active = false;
        float y = 0, v = 0, max = 0;
        bool held = false, dragging = false, caught = false;
        glm::vec2 down{0.f}, last{0.f}; float lastT = 0;
    } scroll_;
    void updateScroll(float rdt);
    void hitAt(float px, float py);
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
public:
    bool droidVisible() const; // the droid is on screen: on the title (or leaving it), or raiding the stack
    bool droidRaiding() const { return raid_.on; }
    int eggCount() const { return int(eggs_.size()); }
    int droidShotsFired() const { return raid_.fired; }
    const std::vector<double>& droidRaidPlan() const { return raidPlan_; }
    void debugDroidRaid(int shots); // developer menu / tests: a raid now (in a run)
    void debugSeed(uint64_t seed) { rng_ = FastRandom(seed * 2654435761ull + 1); } // tests: a repeatable run (before press())
    // a game nobody sees (the checkers' and the level editors' bot runs: BotRun.h): no render list built, each update
    // the game's logic alone (on an iPad, most of a frame's work)
    void setHeadless(bool headless) { headless_ = headless; }
    // developer menu: the bot plays (GameBot.cpp)
    struct BotOptions { bool on = false, loop = false, sloppy = false; int spare = 0; };
    BotOptions botOptions() const { return {bot_.on, bot_.loop, bot_.sloppy, bot_.spare}; }
    void debugSetBot(const BotOptions& o) { bot_.on = o.on; bot_.loop = o.loop; bot_.sloppy = o.sloppy; bot_.spare = o.spare; }
    // touch input beyond the tap (a press on a scrolling list becomes a tap on release, or a drag)
    void dragTo(float xPts, float yPts);
    void release(float xPts, float yPts);
    // the platforms' single entry point for a finger (or the mouse button): Began presses, Moved drags, Ended
    // releases; Cancelled releases too, but a tap held on a list doesn't fire (TouchPhase, Touch.h)
    void touch(TouchPhase phase, float xPts, float yPts);
    void scrollBy(float dyPts); // mouse wheel / trackpad (points, + = content moves up)
    float devScroll() const { return scroll_.y; }
    glm::vec3 droidPosition() const { return droidPose().pos; }
    // saws (tests, developer menu): this course's saws; one set into the surface `ahead` metres in front of the hen
    // (cube: on the next cyber-cube wall top ahead instead, 2.5 eggs past its edge; false if there's none in 40 m)
    const std::vector<Saw>& saws() const { return level_.saws; }
    const Level& course() const { return level_; } // tests: the course as laid so far
    // gauntlets (tests, developer menu): whether she's in one, which way she's running, the furthest she's got
    bool inGauntlet() const { return gIn_.g != nullptr; }
    int runDirection() const { return gDir_; }
    double furthest() const { return std::max(x_, maxX_); }
    void debugNearGauntlet(bool everyRun = false) { debugNearGauntlet_ = true; debugNearGauntletEvery_ = everyRun; }
    // the level editor's play-test (PlayTest, above): every run on its level, from where it says; none of the screens
    // between runs (a rank up, the daily drop, a streak, the primers: queueScreen)
    void debugPlayTest(const PlayTest& t) { playTest_ = t; }
    float meterEggs() const { return meter_; } // tests: the eggs in the meter
    float henBase() const { return float(baseY_); } // tests: the floor her stack stands on, metres
    bool debugSaw(double ahead, bool cube = false);
    // tests: a wall laser in a face `ahead` metres on, its beam `height` eggs up from the surface there, firing `dir`,
    // its reach `reach` metres (no wall needed); lava `cells` long from `ahead` metres on
    bool debugLaser(double ahead, int height, int dir = -1, double reach = 6);
    bool laserZapped() const { return zapped_; }
    glm::vec3 debugFlashColor() const { return flashColor_; } // tests: the screen flash's colour (a death's)
    bool debugLava(double ahead, int cells);
    bool debugSpring(double ahead, int power); // tests: a spring `ahead` metres on, on the surface there
    const std::vector<Spring>& springs() const { return level_.springs; }
    int springsFired() const { return springsFired_; }
    bool launched() const { return launched_; }
    double henSpeedY() const { return vy_; }
    const std::vector<Laser>& lasers() const { return level_.lasers; }
    const std::vector<Lava>& lavas() const { return level_.lavas; }
    // tests: the sawn eggs' halves and yolks flying about (their centres), and the close calls this run
    std::vector<glm::vec3> eggHalves() const { std::vector<glm::vec3> v; for (const auto& d : debris_) if (d.kind == DebrisKind::EggHalf) v.push_back(d.pos); return v; }
    std::vector<glm::vec3> zappedEggs() const { std::vector<glm::vec3> v; for (const auto& d : debris_) if (d.zapped) v.push_back(d.pos); return v; }
    float stackDrawnBase() const { return float(baseY_) + sawDrop_; } // (tests: where the stack's drawn from, settling)
    int yolks() const { int n = 0; for (const auto& d : debris_) n += d.kind == DebrisKind::Yolk; return n; }
    int flyingBlades() const { int n = 0; for (const auto& d : debris_) n += d.kind == DebrisKind::Blade; return n; }
    int closeCalls() const { return run_.closeCalls; }
    int perfects() const { return run_.perfects; }
    float timeScale() const { return timeScale_; } // (tests: the slow motion a near miss brings on)
    bool sawing() const { return cutSawX_ >= 0; }
    // tests: eggs burning in lava, how she died, the hen's pieces (a sliced hen's two)
    int burntEggs() const { int n = 0; for (const auto& d : debris_) n += d.kind == DebrisKind::Burnt; return n; }
    Death death() const { return death_; }
    float eggBurn() const { return eggs_.empty() ? 0.f : eggs_.front().burn; }
private:
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
    void hudImage(const HudImg& img, glm::vec2 center, glm::vec2 size, float alpha, glm::vec3 tint = glm::vec3(1.f));
    // stretched sideways through its middle: the end caps (half its height each) keep their shape
    // (leftCap: the left end's width in image pixels, when something there mustn't stretch; 0 = half its height)
    void hudImage3(const HudImg& img, glm::vec2 center, glm::vec2 size, float alpha, glm::vec3 tint = glm::vec3(1.f), float leftCap = 0);
    // 9-sliced: `src` = corner size in image pixels, `dst` = on screen
    void hudImage9(const HudImg& img, glm::vec2 center, glm::vec2 size, glm::vec2 src, glm::vec2 dst, float alpha, glm::vec3 tint = glm::vec3(1.f));
    // pill button; `fn` (if any) runs when it's tapped. Returns its size.
    glm::vec2 hudButton(std::string_view label, glm::vec2 center, glm::vec3 color, float alpha, bool glow, std::function<void()> fn = {}, float scale = 1.f);
    bool privacyButton_ = false; // title-screen 'Privacy settings' (UMP), refreshed each frame on the title
};

} // namespace cs
