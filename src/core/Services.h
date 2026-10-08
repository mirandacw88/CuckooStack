// Platform services the game depends on, injected by each platform entry point.
// Replaces Web Audio, localStorage and navigator.vibrate from the web build.
#pragma once

#include <optional>
#include <string>

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
    virtual bool showRewarded() = 0;           // false when no ad is ready
    virtual bool consumeReward() = 0;          // true once for each reward the player earned (watched to the end)
    virtual bool privacyOptionsRequired() = 0; // UMP: the user must be able to change consent from inside the app
    virtual void showPrivacyOptions() = 0;
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
    bool showRewarded() override { return false; }
    bool consumeReward() override { return false; }
    bool privacyOptionsRequired() override { return false; }
    void showPrivacyOptions() override {}
};
class NullHaptics final : public IHaptics {
public:
    void pulse(int) override {}
};

struct GameServices {
    IAudio* audio = nullptr;
    IStorage* storage = nullptr;
    IHaptics* haptics = nullptr;
    IAds* ads = nullptr;
};

} // namespace cs
