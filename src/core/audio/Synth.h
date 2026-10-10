// Native replacement for the web build's Web Audio code: every sound effect (tone()/noise()/sfx) and the
// live-synthesized progressive-house soundtrack (music), ported onto a small Web-Audio-style engine.
//
// Threading: the game thread calls the IAudio methods; they only push commands into a lock-free queue.
// The platform audio callback calls render(), which drains the queue, runs the music scheduler (the web
// build's 25 ms setInterval + 150 ms lookahead, here done per audio block) and mixes the voices.
#pragma once

#include "../Services.h"
#include "AudioParam.h"
#include "Music.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace cs::audio {

class Synth final : public IAudio {
public:
    explicit Synth(float sampleRate = 48000.f);

    // IAudio (game thread)
    void play(Sfx sfx, int variant = 0) override;
    void musicStart() override;
    void musicStop(bool powerDown) override;
    void musicTempo(float bpm) override;
    void musicSurge(bool on) override;
    void setMuted(bool muted) override;
    bool musicBeat(double& bpm, double& firstBeat, double& position) const override;

    // Audio thread. Interleaved float output; `channels` copies of the mono mix.
    void render(float* out, int frames, int channels);
    // Must be called before rendering starts (or from the audio thread) when the device rate is known.
    void setSampleRate(float sr);
    float sampleRate() const { return sr_; }
    double time() const { return now_; }

private:
    enum class Wave : uint8_t { Sine, Triangle, Square, Sawtooth };
    enum class Bus : uint8_t { Master, Pump, Drums, None };
    struct Osc { Wave wave; Param freq; float detune; double phase; };
    struct Voice {
        double start = 0, stop = 0;
        std::vector<Osc> oscs;
        const std::vector<float>* buffer = nullptr; // noise source
        std::vector<float> ownBuffer;               // per-call noise() buffer
        double bufPos = 0;
        bool loop = false;
        bool filtered = false;
        Biquad::Type filterType = Biquad::Type::Lowpass;
        float q = 1;
        Param filterFreq{350};
        Biquad bq;
        Param gain{1};
        Bus dest = Bus::Master;
        float delaySend = 0;
    };
    struct Command { enum Type : uint8_t { Sfx, MusicStart, MusicStop, Tempo, Mute, Surge } type; int a; float f; };

    // ---- web build ports (audio thread)
    void sfx(Sfx s, int variant);
    void tone(float f0, float f1, float dur, Wave type = Wave::Sine, float vol = 0.15f, float delay = 0);
    void noise(float dur, float vol, float freq);
    void schedule();
    void schedulePartyStep(double t, double sixteenth);
    void woop(double t, float dur);
    void kick(double t);
    void noiseHit(double t, float dur, float vol, Biquad::Type type, float freq, float q = 1, Bus dest = Bus::Drums);
    void clap(double t);
    void snare(double t, float vol, float pitch);
    void saws(int m, double t, float dur, float vol, std::initializer_list<float> detunes, float cutoff, Bus dest, float send);
    void bass(int m, double t, float dur);
    void pluck(int m, double t, float vol);
    void crash(double t);
    void riser(double t, float dur);
    void powerDown();
    void startMusic();
    void stopMusic(bool sad);
    static void env(Param& g, double t, float a, float peak, float dec);

    Voice& newVoice(double start, double stop, Bus dest);
    float random01();
    void processCommands();
    void renderBlock(float* mono, int n);

    float sr_;
    double now_ = 0;
    uint64_t rng_ = 0x2545F4914F6CDD1Dull;
    std::vector<Voice> voices_;
    std::vector<float> noiseBuffer_; // nb: 2 s of white noise

    // music graph (ensure()): pump -> filt, drums -> filt, delay -> dlp -> fb -> delay, dlp -> filt, filt -> out -> comp -> destination
    Param pump_{1}, filtFreq_{18000}, out_{0}, delayTime_{0.3f};
    Biquad filt_, dlp_;
    std::vector<float> delayLine_;
    size_t delayWrite_ = 0;
    float compEnvDb_ = 0.f, compMakeup_ = 1.f;

    // scheduler state (music IIFE)
    bool playing_ = false, muted_ = false, schedulerOn_ = false;
    double nextT_ = 0, schedulerUntil_ = 0;
    int step_ = 0, partyStep_ = 0;
    bool party_ = false;
    double bpm_ = 126;

    // SPSC command queue (game thread -> audio thread)
    static constexpr size_t kQueue = 256;
    std::array<Command, kQueue> queue_{};
    std::atomic<size_t> head_{0}, tail_{0};
    void push(const Command& c);

    MusicPlayer music_; // the recorded soundtrack (when built in): replaces the synthesized songs, keeps the SFX
    std::vector<float> mono_, busPump_, busDrums_, busDelay_, busMaster_;
};

} // namespace cs::audio
