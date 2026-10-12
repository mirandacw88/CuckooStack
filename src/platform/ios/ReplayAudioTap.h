// Share Replay's soundtrack: a rolling copy of the last ~20 s the game's audio engine played (16-bit stereo). The
// clip uses it instead of ReplayKit's app-audio capture, which drops buffers (a 15 s clip came back with 10.4 s of
// audio, packed together and drifting ahead of the picture). Written on the audio thread, read at the crash.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cs {

class ReplayAudioTap {
public:
    static ReplayAudioTap& shared() { static ReplayAudioTap t; return t; }

    void setRate(double rate) { rate_.store(rate, std::memory_order_relaxed); }

    // replays turned on (main thread): allocate once; the audio thread starts copying
    void enable() {
        if (ready_.load(std::memory_order_acquire)) return;
        buf_.assign(kFrames * 2, 0);
        ready_.store(true, std::memory_order_release);
    }

    // only gameplay is kept (IReplay::setCapturing): paused stretches are left out, like the video's
    void setCapturing(bool on) { capturing_.store(on, std::memory_order_relaxed); }

    // a crash: rewindToMark() (a revive, while capture is paused) drops what was kept since, like the video's cut
    void mark() { mark_ = write_.load(std::memory_order_acquire); }
    void rewindToMark() { if (mark_ != UINT64_MAX) { write_.store(mark_, std::memory_order_release); mark_ = UINT64_MAX; } }

    // audio thread: interleaved stereo float
    void push(const float* inter, int frames) {
        if (!ready_.load(std::memory_order_acquire) || !capturing_.load(std::memory_order_relaxed)) return;
        const uint64_t w = write_.load(std::memory_order_relaxed);
        for (int i = 0; i < frames; ++i) {
            const size_t at = size_t((w + uint64_t(i)) % kFrames) * 2;
            buf_[at] = toPcm(inter[i * 2]);
            buf_[at + 1] = toPcm(inter[i * 2 + 1]);
        }
        write_.store(w + uint64_t(frames), std::memory_order_release);
    }

    // the last `seconds` (fewer if not recorded yet) as a 16-bit stereo WAV; false if nothing is recorded
    bool writeWav(const std::string& path, double seconds) const {
        if (!ready_.load(std::memory_order_acquire)) return false;
        const double rate = rate_.load(std::memory_order_relaxed);
        const uint64_t w = write_.load(std::memory_order_acquire);
        const uint64_t margin = 8192; // the writer keeps going while this copies: stay clear of it
        const uint64_t n = std::min<uint64_t>({uint64_t(seconds * rate), w, kFrames - margin});
        if (n == 0) return false;
        std::vector<int16_t> pcm(size_t(n) * 2);
        for (uint64_t i = 0; i < n; ++i) {
            const size_t at = size_t((w - n + i) % kFrames) * 2;
            pcm[size_t(i) * 2] = buf_[at];
            pcm[size_t(i) * 2 + 1] = buf_[at + 1];
        }
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        const uint32_t data = uint32_t(pcm.size() * 2), sr = uint32_t(rate), byteRate = sr * 4;
        auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16); u16(1); u16(2); u32(sr); u32(byteRate); u16(4); u16(16);
        std::fwrite("data", 1, 4, f); u32(data);
        std::fwrite(pcm.data(), 2, pcm.size(), f);
        return std::fclose(f) == 0;
    }

private:
    static constexpr uint64_t kFrames = 20 * 48000; // ~20 s at 48 kHz (more at 44.1 kHz)
    static int16_t toPcm(float x) { return int16_t(std::clamp(x, -1.f, 1.f) * 32767.f); }
    std::vector<int16_t> buf_;
    std::atomic<bool> ready_{false}, capturing_{false};
    std::atomic<uint64_t> write_{0};
    uint64_t mark_ = UINT64_MAX;
    std::atomic<double> rate_{48000.0};
};

} // namespace cs
