#include "AndroidReplay.h"
#include "../../core/Log.h"
#include "../../graphics/Renderer.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace cs {

namespace {
constexpr int kVideoWidth = 720, kFps = 30, kVideoBitrate = 4'000'000, kAudioBitrate = 128'000;
constexpr int64_t kWindowUs = 22'000'000;     // rolling window kept in memory
constexpr int64_t kClipUs = 15'000'000;       // shared clip length (before the end card)
constexpr int64_t kCardUs = 1'800'000;        // end card on screen
constexpr int kAudioChunk = 1024;             // AAC frame size
constexpr uint32_t kKeyFrame = 1;             // MediaCodec BUFFER_FLAG_KEY_FRAME
constexpr int32_t kColorFormatSurface = 0x7F000789;

// API 26+ entry points, looked up at runtime (minSdk is 24)
using CreateInputSurfaceFn = media_status_t (*)(AMediaCodec*, ANativeWindow**);
using SetParametersFn = media_status_t (*)(AMediaCodec*, const AMediaFormat*);
CreateInputSurfaceFn createInputSurface() {
    static auto fn = reinterpret_cast<CreateInputSurfaceFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_createInputSurface"));
    return fn;
}
SetParametersFn setParameters() {
    static auto fn = reinterpret_cast<SetParametersFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_setParameters"));
    return fn;
}
} // namespace

int64_t AndroidReplay::nowUs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts); // the clock the encoder stamps surface frames with
    return int64_t(ts.tv_sec) * 1'000'000 + ts.tv_nsec / 1000;
}

AndroidReplay::AndroidReplay(Renderer& renderer, std::string cacheDir, int audioRate, Platform platform)
    : renderer_(renderer), cacheDir_(std::move(cacheDir)), audioRate_(audioRate > 0 ? audioRate : 48000), platform_(std::move(platform)) {
    available_ = createInputSurface() != nullptr && setParameters() != nullptr;
    ring_.assign(size_t(96000) * 2 * 2, 0.f); // >= 2 s of stereo at any output rate; never reallocated (the audio thread writes it)
    if (!available_) CS_LOGI("Replay: needs Android 8.0+ (MediaCodec input surfaces); sharing falls back to text");
}

AndroidReplay::~AndroidReplay() {
    stopSession();
    if (cardThread_.joinable()) cardThread_.join();
    if (exportThread_.joinable()) exportThread_.join();
}

ReplayState AndroidReplay::state() {
    if (!available_) return ReplayState::Unavailable;
    if (!enabled_) return ReplayState::Off;
    return state_.load();
}

void AndroidReplay::setEnabled(bool on) {
    enabled_ = on;
    if (!on) { stopSession(); state_ = ReplayState::Off; }
}

void AndroidReplay::runStarted() {
    if (enabled_ && running_ && state_ != ReplayState::Exporting) state_ = ReplayState::Recording;
    runStartUs_ = nowUs(); // this run's clip starts here at the earliest
    if (running_ && video_) { // ... on a fresh keyframe
        AMediaFormat* p = AMediaFormat_new();
        AMediaFormat_setInt32(p, "request-sync", 0);
        setParameters()(video_, p);
        AMediaFormat_delete(p);
    }
}

void AndroidReplay::crashed() { crashMarkUs_ = nowUs(); }

// A revive (capture is paused: the Continue panel is up): the crash it came back from is cut like a pause, from the
// crash mark to the resume, and the revived run starts on a fresh keyframe.
void AndroidReplay::revived() {
    if (crashMarkUs_ < 0) return;
    if (pauseStartUs_ >= 0) pauseStartUs_ = std::min(pauseStartUs_, crashMarkUs_);
    crashMarkUs_ = -1;
    if (running_ && video_) {
        AMediaFormat* p = AMediaFormat_new();
        AMediaFormat_setInt32(p, "request-sync", 0);
        setParameters()(video_, p);
        AMediaFormat_delete(p);
    }
}

// Only gameplay goes into the clip: while paused the renderer sends no frames (except the end card) and the synth's
// audio isn't fed; the export cuts the paused stretches out of the timeline.
void AndroidReplay::setCapturing(bool on) {
    if (on == capturing_.load()) return;
    capturing_ = on;
    renderer_.setCapturePaused(!on);
    const int64_t now = nowUs();
    if (!on) { pauseStartUs_ = now; return; }
    if (pauseStartUs_ >= 0) {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        pauses_.emplace_back(pauseStartUs_, now);
        while (pauses_.size() > 64) pauses_.pop_front();
    }
    pauseStartUs_ = -1;
    if (sessionStartUs_) sessionStartUs_ = now; // the watchdog only counts time spent capturing
}

void AndroidReplay::onSurfaceLost() { stopSession(); }

// ---------------------------------------------------------------- session

bool AndroidReplay::startSession() {
    const VkExtent2D screen = renderer_.logicalExtent();
    if (!screen.width || screen.height <= screen.width) return false; // portrait phones only
    width_ = kVideoWidth;
    height_ = uint32_t(std::lround(double(kVideoWidth) * screen.height / screen.width / 16.0)) * 16;

    video_ = AMediaCodec_createEncoderByType("video/avc");
    if (!video_) { available_ = false; return false; }
    AMediaFormat* vf = AMediaFormat_new();
    AMediaFormat_setString(vf, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_WIDTH, int32_t(width_));
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_HEIGHT, int32_t(height_));
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatSurface);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_BIT_RATE, kVideoBitrate);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_FRAME_RATE, kFps);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1); // cut points every second
    AMediaFormat_setInt32(vf, "max-bframes", 0);                     // presentation order == decode order
    media_status_t st = AMediaCodec_configure(video_, vf, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(vf);
    if (st != AMEDIA_OK || createInputSurface()(video_, &input_) != AMEDIA_OK || AMediaCodec_start(video_) != AMEDIA_OK) {
        CS_LOGW("Replay: video encoder unavailable (%d)", int(st));
        stopSession();
        available_ = false;
        return false;
    }
    inputSurface_.setWindow(input_);
    if (!renderer_.startRecording(inputSurface_, {width_, height_})) {
        stopSession();
        available_ = false;
        return false;
    }
    renderer_.setCapturePaused(!capturing_);

    audio_ = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (audio_) {
        AMediaFormat* af = AMediaFormat_new();
        AMediaFormat_setString(af, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_SAMPLE_RATE, audioRate_);
        AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
        AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_BIT_RATE, kAudioBitrate);
        AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_AAC_PROFILE, 2); // AAC-LC
        AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);
        if (AMediaCodec_configure(audio_, af, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK || AMediaCodec_start(audio_) != AMEDIA_OK) {
            AMediaCodec_delete(audio_);
            audio_ = nullptr; // video-only replays are still worth sharing
        }
        AMediaFormat_delete(af);
    }
    ringRead_ = ringWrite_.load();
    running_ = true;
    audioTap_ = audio_ != nullptr;
    videoThread_ = std::thread([this] { drainLoop(video_, videoTrack_, true); });
    if (audio_) audioThread_ = std::thread([this] { audioLoop(); });
    state_ = ReplayState::Recording;
    sessionStartUs_ = nowUs();
    CS_LOGI("Replay: recording %ux%u%s", width_, height_, audio_ ? " + audio" : "");
    return true;
}

void AndroidReplay::stopSession() {
    const bool was = running_.exchange(false);
    audioTap_ = false;
    if (videoThread_.joinable()) videoThread_.join();
    if (audioThread_.joinable()) audioThread_.join();
    if (showingCard_) { renderer_.clearRecordOverlay(); showingCard_ = false; }
    renderer_.stopRecording();
    for (AMediaCodec** c : {&video_, &audio_})
        if (*c) { AMediaCodec_stop(*c); AMediaCodec_delete(*c); *c = nullptr; }
    if (input_) { ANativeWindow_release(input_); input_ = nullptr; }
    inputSurface_.setWindow(nullptr);
    for (Track* t : {&videoTrack_, &audioTrack_}) {
        std::lock_guard<std::mutex> lock(t->m);
        t->samples.clear();
        if (t->format) { AMediaFormat_delete(t->format); t->format = nullptr; }
    }
    if (was && state_ != ReplayState::Ready && state_ != ReplayState::Exporting) state_ = enabled_ ? ReplayState::Failed : ReplayState::Off;
}

void AndroidReplay::prune(Track& t, int64_t newest, bool video) {
    while (!t.samples.empty() && newest - t.samples.front().pts > kWindowUs) t.samples.pop_front();
    if (video) // the window must start on a keyframe to decode
        while (t.samples.size() > 1 && !(t.samples.front().flags & kKeyFrame)) t.samples.pop_front();
}

void AndroidReplay::drainLoop(AMediaCodec* codec, Track& track, bool video) {
    while (running_) {
        AMediaCodecBufferInfo info{};
        const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec, &info, 20'000);
        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            std::lock_guard<std::mutex> lock(track.m);
            if (track.format) AMediaFormat_delete(track.format);
            track.format = AMediaCodec_getOutputFormat(codec);
            continue;
        }
        if (idx < 0) { if (!video) return; continue; } // the audio loop calls this non-blocking (one pass)
        size_t cap = 0;
        const uint8_t* buf = AMediaCodec_getOutputBuffer(codec, size_t(idx), &cap);
        if (buf && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
            Sample s;
            s.data.assign(buf + info.offset, buf + info.offset + info.size);
            s.pts = info.presentationTimeUs;
            s.flags = info.flags;
            std::lock_guard<std::mutex> lock(track.m);
            track.samples.push_back(std::move(s));
            prune(track, info.presentationTimeUs, video);
        }
        AMediaCodec_releaseOutputBuffer(codec, size_t(idx), false);
        if (!video) return;
    }
}

// ---------------------------------------------------------------- audio

void AndroidReplay::pushAudio(const float* samples, int frames, int channels) {
    if (!audioTap_ || !capturing_.load(std::memory_order_relaxed)) return; // gameplay only
    const uint64_t w = ringWrite_.load(std::memory_order_relaxed), r = ringRead_.load(std::memory_order_acquire);
    const size_t capFrames = ring_.size() / 2;
    if (w - r + uint64_t(frames) > capFrames) return; // encoder behind: drop rather than block the audio thread
    for (int i = 0; i < frames; ++i) {
        const size_t at = size_t((w + uint64_t(i)) % capFrames) * 2;
        ring_[at] = samples[size_t(i) * channels];
        ring_[at + 1] = samples[size_t(i) * channels + (channels > 1 ? 1 : 0)];
    }
    ringWrite_.store(w + uint64_t(frames), std::memory_order_release);
}

void AndroidReplay::audioLoop() {
    int64_t base = -1;
    uint64_t sent = 0;
    const size_t capFrames = ring_.size() / 2;
    while (running_) {
        const uint64_t w = ringWrite_.load(std::memory_order_acquire), r = ringRead_.load(std::memory_order_relaxed);
        const uint64_t avail = w - r;
        if (avail >= uint64_t(kAudioChunk)) {
            const ssize_t in = AMediaCodec_dequeueInputBuffer(audio_, 5'000);
            if (in >= 0) {
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(audio_, size_t(in), &cap);
                const int frames = int(std::min<size_t>(kAudioChunk, cap / 4));
                auto* out = reinterpret_cast<int16_t*>(buf);
                for (int i = 0; i < frames * 2; ++i) {
                    const float v = ring_[size_t((r + uint64_t(i / 2)) % capFrames) * 2 + size_t(i % 2)];
                    out[i] = int16_t(std::clamp(v, -1.f, 1.f) * 32767.f);
                }
                ringRead_.store(r + uint64_t(frames), std::memory_order_release);
                // timestamps on the video's clock: the chunk was produced `avail` frames ago
                const int64_t expected = nowUs() - int64_t(avail * 1'000'000 / uint64_t(audioRate_));
                int64_t pts = base + int64_t(sent * 1'000'000 / uint64_t(audioRate_));
                if (base < 0 || std::abs(pts - expected) > 200'000) { base = expected; sent = 0; pts = base; } // start, or after a pause
                sent += uint64_t(frames);
                AMediaCodec_queueInputBuffer(audio_, size_t(in), 0, size_t(frames) * 4, uint64_t(pts), 0);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        drainLoop(audio_, audioTrack_, false);
    }
}

// ---------------------------------------------------------------- the clip

void AndroidReplay::saveClip(const ReplayMeta& meta) {
    if (!running_ || state_ == ReplayState::Exporting || showingCard_) return;
    crashUs_ = nowUs(); // the cut: capture pauses now (the game stops capturing once the clip is requested)
    state_ = ReplayState::Exporting;
    if (cardThread_.joinable()) cardThread_.join();
    cardReady_ = false;
    const uint32_t w = width_, h = height_;
    cardThread_ = std::thread([this, meta, w, h] { // ~0.2-0.5 s of CPU text rendering: off the game thread
        cardPixels_ = endCard_ ? endCard_(int(w), int(h), meta) : std::vector<uint8_t>{};
        cardReady_ = true;
    });
}

void AndroidReplay::frame(bool hot) {
    if (!enabled_ || !available_) return;
    if (hot && running_ && !showingCard_ && state_ != ReplayState::Exporting) { stopSession(); state_ = ReplayState::Failed; return; } // heat
    if (!running_ && !hot && renderer_.hasSurface() && state_ != ReplayState::Exporting) startSession();
    if (!running_) return;
    // watchdog: an encoder that won't take the GPU's frames (seen on emulators) gets no second chance on this device
    if (capturing_ && sessionStartUs_ && nowUs() - sessionStartUs_ > 4'000'000) {
        size_t n = 0;
        { std::lock_guard<std::mutex> lock(videoTrack_.m); n = videoTrack_.samples.size(); }
        if (n <= 2) {
            CS_LOGW("Replay: the video encoder isn't consuming frames; replays off on this device (sharing falls back to text)");
            stopSession();
            available_ = false;
            return;
        }
        sessionStartUs_ = 0; // healthy
    }
    // the end card: switch the recorder to it once it's drawn (a fresh keyframe there makes a clean cut)
    if (cardReady_.exchange(false)) {
        if (!cardPixels_.empty() && renderer_.setRecordOverlay(cardPixels_.data(), width_, height_)) {
            AMediaFormat* p = AMediaFormat_new();
            AMediaFormat_setInt32(p, "request-sync", 0);
            setParameters()(video_, p);
            AMediaFormat_delete(p);
            showingCard_ = true;
            cardShownUs_ = nowUs();
        } else {
            state_ = ReplayState::Failed;
        }
    }
    if (showingCard_ && nowUs() - cardShownUs_ > kCardUs) {
        renderer_.clearRecordOverlay();
        showingCard_ = false;
        if (exportThread_.joinable()) exportThread_.join();
        const std::string path = cacheDir_ + "/replays/cuckoo-stack-replay.mp4";
        const int64_t to = nowUs();
        exportThread_ = std::thread([this, to, path] { exportClip(to, path); });
    }
}

void AndroidReplay::exportClip(int64_t toUs, std::string path) {
    const int64_t cut = crashUs_, card = cardShownUs_, runStart = runStartUs_;
    // the encoder runs a little behind the screen: give it up to 1.5 s to deliver the end card's frames
    for (int i = 0; i < 30; ++i) {
        { std::lock_guard<std::mutex> lock(videoTrack_.m); if (!videoTrack_.samples.empty() && videoTrack_.samples.back().pts >= toUs - 300'000) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // gameplay time: real time minus the capture pauses before it (Continue panel, menus...)
    std::vector<std::pair<int64_t, int64_t>> pauses;
    { std::lock_guard<std::mutex> lock(pauseMutex_); pauses.assign(pauses_.begin(), pauses_.end()); }
    auto paused = [&pauses](int64_t pts) { // inside a pause or a cut (frames still in the encoder when it began)
        for (const auto& [a, b] : pauses) if (pts >= a && pts < b) return true;
        return false;
    };
    auto played = [&pauses](int64_t pts) {
        int64_t t = pts;
        for (const auto& [a, b] : pauses) t -= std::clamp<int64_t>(pts, a, b) - a;
        return t;
    };
    std::vector<Sample> video, audio;
    std::vector<int64_t> videoOut, audioOut; // output timestamps
    AMediaFormat* vfmt = nullptr;
    AMediaFormat* afmt = nullptr;
    // the formats are borrowed for the export (AMediaFormat_copy needs API 28) and handed back afterwards
    int64_t startKey = INT64_MIN, t0 = 0, lastGame = 0;
    {
        std::lock_guard<std::mutex> lock(videoTrack_.m);
        std::swap(vfmt, videoTrack_.format);
        // game frames: this run's last kClipUs of play before the cut, from a keyframe; then the end card from its keyframe
        const int64_t from = played(cut) - kClipUs;
        for (const Sample& s : videoTrack_.samples) {
            if (s.pts < runStart || s.pts >= cut || !(s.flags & kKeyFrame) || paused(s.pts)) continue;
            if (played(s.pts) <= from || startKey == INT64_MIN) startKey = s.pts;
            if (played(s.pts) > from) break;
        }
        t0 = startKey == INT64_MIN ? 0 : played(startKey);
        bool inCard = false, needKey = false;
        for (const Sample& s : videoTrack_.samples) {
            if (s.pts > toUs) break;
            if (startKey == INT64_MIN || s.pts < startKey) continue;
            if (s.pts < cut) {
                if (paused(s.pts)) { needKey = true; continue; } // a cut: carry on from the next keyframe
                if (needKey && !(s.flags & kKeyFrame)) continue;
                needKey = false;
                video.push_back(s);
                videoOut.push_back(lastGame = played(s.pts) - t0);
                continue;
            }
            if (!inCard && !(s.pts >= card && (s.flags & kKeyFrame))) continue;
            if (!inCard) { inCard = true; t0 = s.pts - (lastGame + 1'000'000 / kFps); } // the card follows the last game frame
            video.push_back(s);
            videoOut.push_back(s.pts - t0);
        }
    }
    {
        std::lock_guard<std::mutex> lock(audioTrack_.m);
        std::swap(afmt, audioTrack_.format);
        const int64_t a0 = startKey == INT64_MIN ? 0 : played(startKey);
        for (const Sample& s : audioTrack_.samples)
            if (startKey != INT64_MIN && s.pts >= startKey && s.pts < cut && !paused(s.pts)) { audio.push_back(s); audioOut.push_back(played(s.pts) - a0); }
    }
    bool ok = vfmt && video.size() >= 2 && lastGame >= 1'000'000; // at least a second of play
    if (ok) {
        ::mkdir((cacheDir_ + "/replays").c_str(), 0700);
        const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
        AMediaMuxer* mux = fd >= 0 ? AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4) : nullptr;
        ok = mux != nullptr;
        if (ok) {
            const ssize_t vt = AMediaMuxer_addTrack(mux, vfmt);
            const ssize_t at = afmt && !audio.empty() ? AMediaMuxer_addTrack(mux, afmt) : -1;
            ok = vt >= 0 && AMediaMuxer_start(mux) == AMEDIA_OK;
            size_t ai = 0;
            for (size_t vi = 0; ok && vi < video.size(); ++vi) {
                for (; at >= 0 && ai < audio.size() && audioOut[ai] <= videoOut[vi]; ++ai) { // interleave by time
                    if (audioOut[ai] < 0) continue;
                    AMediaCodecBufferInfo info{0, int32_t(audio[ai].data.size()), audioOut[ai], 0};
                    AMediaMuxer_writeSampleData(mux, size_t(at), audio[ai].data.data(), &info);
                }
                AMediaCodecBufferInfo info{0, int32_t(video[vi].data.size()), videoOut[vi], video[vi].flags & kKeyFrame};
                ok = AMediaMuxer_writeSampleData(mux, size_t(vt), video[vi].data.data(), &info) == AMEDIA_OK;
            }
            if (AMediaMuxer_stop(mux) != AMEDIA_OK) ok = false;
            AMediaMuxer_delete(mux);
        }
        if (fd >= 0) ::close(fd);
    }
    for (auto [t, f] : {std::pair<Track*, AMediaFormat*>{&videoTrack_, vfmt}, {&audioTrack_, afmt}}) {
        std::lock_guard<std::mutex> lock(t->m);
        if (!t->format) t->format = f; // unless the encoder announced a newer one meanwhile
        else if (f) AMediaFormat_delete(f);
    }
    {
        std::lock_guard<std::mutex> lock(clipMutex_);
        clipPath_ = ok ? path : "";
    }
    state_ = ok ? ReplayState::Ready : ReplayState::Failed;
    CS_LOGI("Replay: %s (%zu video, %zu audio samples)", ok ? "clip saved" : "export failed", video.size(), audio.size());
}

void AndroidReplay::shareVideo() {
    if (state_ != ReplayState::Ready || !platform_.share) return;
    std::string path;
    { std::lock_guard<std::mutex> lock(clipMutex_); path = clipPath_; }
    platform_.share(path, ""); // the video only
}

bool AndroidReplay::consumeShared(std::string& target) {
    if (!platform_.pollSharedTarget) return false;
    target = platform_.pollSharedTarget();
    return !target.empty();
}

} // namespace cs
