// Android Share Replay. No MediaProjection (no system consent dialog, no casting icon): the renderer blits the
// finished frame into a MediaCodec H.264 encoder's input surface on the GPU (Renderer::startRecording), the synth's
// output feeds an AAC encoder, and both encoded streams sit in a rolling ~20 s window in memory. At the crash the
// recorder shows the end card for 1.8 s, then muxes the last ~15 s + the card into an MP4 in the cache folder.
#pragma once

#include "AndroidWindow.h"
#include "../../core/Services.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AMediaCodec;
struct AMediaFormat;

namespace cs {

class Renderer;

class AndroidReplay final : public IReplay {
public:
    struct Platform {
        std::function<void(const std::string& videoPath, const std::string& text)> share; // empty path: text only
        std::function<std::string()> pollSharedTarget;                                      // "" when nothing new
    };
    AndroidReplay(Renderer& renderer, std::string cacheDir, int audioRate, Platform platform);
    ~AndroidReplay() override;

    // IReplay
    ReplayState state() override;
    void setEnabled(bool on) override;
    void runStarted() override;
    void saveClip(const ReplayMeta& meta) override;
    void share(const std::string& caption, const std::string& url) override;
    bool consumeShared(std::string& target) override;
    void setEndCardRenderer(EndCardRenderer r) override { endCard_ = std::move(r); }

    // main loop: once per frame (drives the session and the end card); hot = thermal Serious or worse
    void frame(bool hot);
    void onSurfaceLost();                                     // the window went away: the session must restart
    // audio thread (Oboe callback): the synth's output, interleaved float
    void pushAudio(const float* samples, int frames, int channels);
    void setAudioRate(int rate) { if (!running_ && rate > 0) audioRate_ = rate; } // the output stream's rate

private:
    struct Sample { std::vector<uint8_t> data; int64_t pts; uint32_t flags; };
    struct Track { std::mutex m; std::deque<Sample> samples; AMediaFormat* format = nullptr; };

    bool startSession();
    void stopSession();
    void drainLoop(AMediaCodec* codec, Track& track, bool video);
    void audioLoop();
    void prune(Track& t, int64_t newest, bool video);
    void exportClip(int64_t fromUs, int64_t toUs, std::string path);
    static int64_t nowUs();

    Renderer& renderer_;
    std::string cacheDir_;
    int audioRate_;
    Platform platform_;
    EndCardRenderer endCard_;
    bool enabled_ = false, available_ = true;
    std::atomic<ReplayState> state_{ReplayState::Off};

    // session
    AMediaCodec* video_ = nullptr;
    AMediaCodec* audio_ = nullptr;
    struct ANativeWindow* input_ = nullptr;
    AndroidWindow inputSurface_;
    std::atomic<bool> running_{false};
    std::thread videoThread_, audioThread_;
    Track videoTrack_, audioTrack_;
    uint32_t width_ = 0, height_ = 0;

    // audio: SPSC ring filled by the Oboe callback, drained by audioLoop
    std::vector<float> ring_;
    std::atomic<uint64_t> ringWrite_{0}, ringRead_{0};
    int channels_ = 2;
    std::atomic<bool> audioTap_{false};

    // the end card + export
    std::atomic<bool> cardReady_{false};
    std::vector<uint8_t> cardPixels_;
    std::thread cardThread_, exportThread_;
    int64_t crashUs_ = 0, cardShownUs_ = 0, sessionStartUs_ = 0;
    bool showingCard_ = false;
    std::string clipPath_;
    std::mutex clipMutex_;
};

} // namespace cs
