// Platform services the game depends on, injected by each platform entry point.
// Replaces Web Audio, localStorage and navigator.vibrate from the web build.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cs {

enum class Sfx { Lay, Crack, Perfect, Corn, Land, Squawk, Empty, Smash, SurgeStart, SurgeEnd, LifeLost };

// Web Audio replacement. The web build synthesizes every sound live (see docs/PORTING.md, "Audio");
// a native backend (AAudio/Oboe, AVAudioEngine) implements this with the same synth graph.
class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void play(Sfx sfx, int variant = 0) = 0;    // variant: egg index for Lay, semitone step for Perfect
    virtual void musicStart() = 0;
    virtual void musicStop(bool powerDown) = 0;
    virtual void musicTempo(float bpm) = 0;
    virtual void musicSurge(bool on) = 0;            // party pattern during a surge; the song resumes where it left off
    virtual void setMuted(bool muted) = 0;
};

// localStorage replacement: small string key/value pairs, persisted by the platform.
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;
};

class IHaptics {
public:
    virtual ~IHaptics() = default;
    virtual void pulse(int milliseconds) = 0;
};

// Rewarded ads (AdMob rewarded video + Google UMP consent). Called from the game thread; implementations hop to
// the UI thread themselves and report rewards back through consumeReward(), which the game polls every frame.
enum class RewardedState { Loading, Ready, Unavailable };

class IAds {
public:
    virtual ~IAds() = default;
    virtual RewardedState rewardedState() = 0;
    // false when no ad is ready. `placement` names the offer (Monetization.h) for analytics and AdMob reporting.
    virtual bool showRewarded(const char* placement) = 0;
    virtual bool consumeReward() = 0;          // true once for each reward the player earned (watched to the end)
    virtual bool privacyOptionsRequired() = 0; // UMP: the user must be able to change consent from inside the app
    virtual void showPrivacyOptions() = 0;
    // Full-screen interstitial between runs (the rules for when live in Monetization.h).
    virtual bool interstitialReady() { return false; }
    virtual bool showInterstitial() { return false; }
    // true while any full-screen ad covers the game (the game pauses its clocks and music meanwhile)
    virtual bool adShowing() { return false; }
    // Under-13 players: child-directed treatment, G-rated ads only, no tracking prompt. Called before ads start.
    virtual void setAudience(bool child) { (void)child; }
};

// Wall clock + local calendar. Injected so streaks, missions and reminders can be tested with a fake clock.
class IClock {
public:
    virtual ~IClock() = default;
    virtual int64_t now() = 0;                      // seconds since the Unix epoch
    virtual std::string localDate(int64_t t) = 0;   // "YYYY-MM-DD" in the device's time zone
    virtual int localHour(int64_t t) = 0;           // 0..23
    virtual int64_t utcOffset(int64_t t) { (void)t; return 0; } // seconds east of UTC (local = UTC + offset)
    std::string today() { return localDate(now()); }
    // the timestamp of local midnight starting the day that contains t
    int64_t localMidnight(int64_t t) { const int64_t o = utcOffset(t); const int64_t l = t + o; return (l >= 0 ? l / 86400 : (l - 86399) / 86400) * 86400 - o; }
};

// Product analytics (Firebase Analytics on device). Event and parameter names: src/core/Analytics.h.
using AnalyticsParams = std::vector<std::pair<std::string, std::string>>;
class IAnalytics {
public:
    virtual ~IAnalytics() = default;
    virtual void event(const std::string& name, const AnalyticsParams& params) = 0;
    virtual void userProperty(const std::string& name, const std::string& value) = 0;
};

// Server-tunable values (Firebase Remote Config). Keys: the `rc` names in Economy.h / Monetization.h.
// Returns nullopt for keys the server doesn't set; the game then keeps its compiled-in default.
class IRemoteConfig {
public:
    virtual ~IRemoteConfig() = default;
    virtual std::optional<double> number(const std::string& key) = 0;
};

// In-app purchases (StoreKit 2 / Play Billing). Product ids and contents: kProducts in Economy.h.
// Consumables are verified server-side (Cloud Function verifyPurchase) BEFORE the platform reports Success, so the
// game can credit what it's told. Events are polled every frame, like rewards.
struct Product { std::string id, price; };               // price: localised display string ("$2.99"), empty if unknown
enum class PurchaseResult : uint8_t { Success, Cancelled, Failed, Pending };
struct PurchaseEvent { std::string productId; PurchaseResult result = PurchaseResult::Failed; bool restored = false; };
class IStore {
public:
    virtual ~IStore() = default;
    virtual std::vector<Product> products() = 0;          // loaded products (empty until the store answers)
    virtual bool purchase(const std::string& productId) = 0; // false: store unavailable
    virtual void restore() = 0;                            // re-sends Success (restored = true) for owned non-consumables
    virtual bool pollEvent(PurchaseEvent& out) = 0;
};

// Reminder notifications (UNUserNotificationCenter / Android notifications). The game plans them (Reminders.h) and
// replaces the whole set whenever it changes; the platform just delivers. Never used for under-13 players.
enum class NotifPermission : uint8_t { Unknown, Granted, Denied };
struct Reminder { int id; int64_t at; std::string title, body; };
class INotifications {
public:
    virtual ~INotifications() = default;
    virtual NotifPermission permission() = 0;
    virtual void requestPermission() = 0;                 // shows the OS prompt (once; later calls just re-check)
    virtual void replaceAll(const std::vector<Reminder>& reminders) = 0;
    virtual bool consumeOpened(int& reminderId) = 0;      // true once if the app was opened from a reminder
};

// Share Replay: the last ~15 s of the run as a 9:16 video with an end card, shared through the system sheet.
// iOS: ReplayKit clip buffering. Android: in-engine recorder (Vulkan -> MediaCodec). Never offered to under-13s.
enum class ReplayState : uint8_t { Unavailable, Off, Recording, Exporting, Ready, Failed };
struct ReplayMeta { int distance = 0, score = 0; bool newBest = false; std::string day; };
// draws the end card (RGBA8, opaque) at the requested size; the game provides it (EndCard.h)
using EndCardRenderer = std::function<std::vector<uint8_t>(int width, int height, const ReplayMeta&)>;
class IReplay {
public:
    virtual ~IReplay() = default;
    virtual void setEndCardRenderer(EndCardRenderer r) { (void)r; } // platforms that composite their own card ignore it
    virtual ReplayState state() = 0;
    virtual void setEnabled(bool on) = 0;              // the player opted in (Profile flag "replays")
    virtual void runStarted() = 0;                      // keep a rolling buffer from now on
    virtual void saveClip(const ReplayMeta& meta) = 0;  // the crash: export the last ~15 s + the end card
    virtual void share(const std::string& caption, const std::string& url) = 0;
    virtual bool consumeShared(std::string& target) = 0; // the share sheet finished (target app, for analytics)
};

// Daily leaderboards: Game Center (recurring daily board) / Play Games Services (daily time span). 13+ only.
class ILeaderboards {
public:
    virtual ~ILeaderboards() = default;
    virtual bool available() = 0;            // configured on this platform (the button hides otherwise)
    virtual void submit(int meters) = 0;     // signs in quietly first if needed
    virtual void show() = 0;                 // today's board (signs in if needed)
};

// Friend nudges through Firebase (firebase/functions/src/challenges.ts). Results are polled like purchases.
class IBackend {
public:
    virtual ~IBackend() = default;
    virtual bool nudgesAvailable() = 0;                                  // Firebase on + this player has a push token
    virtual void createChallenge(const std::string& day, int meters) = 0;
    virtual bool pollChallengeId(std::string& id) = 0;                   // the id for the last createChallenge
    virtual void challengeBeaten(const std::string& id, int meters) = 0;
};

class NullAudio final : public IAudio {
public:
    void play(Sfx, int) override {}
    void musicStart() override {}
    void musicStop(bool) override {}
    void musicTempo(float) override {}
    void musicSurge(bool) override {}
    void setMuted(bool) override {}
};
class NullAds final : public IAds {
public:
    RewardedState rewardedState() override { return RewardedState::Unavailable; }
    bool showRewarded(const char*) override { return false; }
    bool consumeReward() override { return false; }
    bool privacyOptionsRequired() override { return false; }
    void showPrivacyOptions() override {}
};
class NullHaptics final : public IHaptics {
public:
    void pulse(int) override {}
};
class NullNotifications final : public INotifications {
public:
    NotifPermission permission() override { return NotifPermission::Unknown; }
    void requestPermission() override {}
    void replaceAll(const std::vector<Reminder>&) override {}
    bool consumeOpened(int&) override { return false; }
};
class NullReplay final : public IReplay {
public:
    ReplayState state() override { return ReplayState::Unavailable; }
    void setEnabled(bool) override {}
    void runStarted() override {}
    void saveClip(const ReplayMeta&) override {}
    void share(const std::string&, const std::string&) override {}
    bool consumeShared(std::string&) override { return false; }
};
class NullLeaderboards final : public ILeaderboards {
public:
    bool available() override { return false; }
    void submit(int) override {}
    void show() override {}
};
class NullBackend final : public IBackend {
public:
    bool nudgesAvailable() override { return false; }
    void createChallenge(const std::string&, int) override {}
    bool pollChallengeId(std::string&) override { return false; }
    void challengeBeaten(const std::string&, int) override {}
};
class NullStore final : public IStore {
public:
    std::vector<Product> products() override { return {}; }
    bool purchase(const std::string&) override { return false; }
    void restore() override {}
    bool pollEvent(PurchaseEvent&) override { return false; }
};
class NullAnalytics final : public IAnalytics {
public:
    void event(const std::string&, const AnalyticsParams&) override {}
    void userProperty(const std::string&, const std::string&) override {}
};
class NullRemoteConfig final : public IRemoteConfig {
public:
    std::optional<double> number(const std::string&) override { return std::nullopt; }
};
// The real clock; platform-free (std::time + localtime), so every platform shares it.
class SystemClock final : public IClock {
public:
    int64_t now() override;
    std::string localDate(int64_t t) override;
    int localHour(int64_t t) override;
    int64_t utcOffset(int64_t t) override;
};

struct GameServices {
    IAudio* audio = nullptr;
    IStorage* storage = nullptr;
    IHaptics* haptics = nullptr;
    IAds* ads = nullptr;
    IClock* clock = nullptr;          // null: SystemClock
    IAnalytics* analytics = nullptr;
    IRemoteConfig* remoteConfig = nullptr;
    IStore* store = nullptr;
    INotifications* notifications = nullptr;
    IReplay* replay = nullptr;
    ILeaderboards* leaderboards = nullptr;
    IBackend* backend = nullptr;
};

} // namespace cs
