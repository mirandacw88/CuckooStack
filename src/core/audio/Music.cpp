#include "Music.h"
#include "Mp3.h"
#include "../Log.h"

#include <algorithm>
#include <cmath>

namespace cs::assets {
#define CS_MUSIC_TRACK(sym, bpm, first, loud, len) extern const unsigned char sym[]; extern const size_t sym##_size;
#include "MusicTracks.inc"
#undef CS_MUSIC_TRACK
} // namespace cs::assets

namespace cs::audio {

namespace {
struct Embedded { const unsigned char* data; size_t size; TrackInfo info; };
const Embedded kTracks[] = {
#define CS_MUSIC_TRACK(sym, bpm, first, loud, len) {assets::sym, assets::sym##_size, {bpm, first, loud, len}},
#include "MusicTracks.inc"
#undef CS_MUSIC_TRACK
};
constexpr int kTrackCount = int(sizeof kTracks / sizeof kTracks[0]);
constexpr float kLevel = 0.5f;        // music under the synth's sound effects
constexpr float kFade = 0.35f;        // crossfade / start fade, seconds
constexpr float kTapeStop = 0.8f;     // a crash: the tape winds down over this long
constexpr int kChunk = 1152 * 2;      // frames decoded at a time
} // namespace

const TrackInfo* musicTrackInfo(int track) { return track >= 0 && track < kTrackCount ? &kTracks[track].info : nullptr; }

struct MusicPlayer::Track {
    Mp3 dec;
    bool ok = false;
    double srcRate = 44100, pos = 0;          // read position in source frames (fractional, for resampling)
    uint64_t frames = 0;
    std::vector<float> buf;                   // decoded stereo, frames [bufStart, bufStart + bufLen)
    uint64_t bufStart = 0;
    int bufLen = 0;
    float gain = 0, target = 0;
    TrackInfo info{};
    ~Track() { mp3Close(dec); }

    void seekTo(uint64_t frame) {
        frame %= std::max<uint64_t>(1, frames);
        mp3Seek(dec, frame);
        bufStart = frame; bufLen = 0; pos = double(frame);
    }
    // the stereo frame `f` (absolute), decoding ahead as needed; wraps at the end (the track loops). Reading carries
    // on sequentially: the last decoded frame is kept, since the resampler still needs it for the next sample.
    void frameAt(uint64_t f, float& l, float& r) {
        if (f >= frames) f %= frames;
        if (f < bufStart || f >= bufStart + uint64_t(bufLen)) {
            float tmp[kChunk * 2];
            int keep = 0;
            if (bufLen > 0 && f == bufStart + uint64_t(bufLen)) { // sequential: keep the last 3 frames (interpolation)
                keep = std::min(3, bufLen);
                for (int k = 0; k < keep; ++k) {
                    buf[size_t(k) * 2] = buf[size_t(bufLen - keep + k) * 2];
                    buf[size_t(k) * 2 + 1] = buf[size_t(bufLen - keep + k) * 2 + 1];
                }
                bufStart = f - uint64_t(keep);
            } else {                                              // a jump (the loop, a section start)
                mp3Seek(dec, f);
                bufStart = f;
            }
            const uint32_t ch = std::max(1u, dec.channels);
            const int got = int(mp3Read(dec, kChunk, tmp));
            for (int i = 0; i < got; ++i) {
                buf[size_t(keep + i) * 2] = tmp[size_t(i) * ch];
                buf[size_t(keep + i) * 2 + 1] = ch > 1 ? tmp[size_t(i) * ch + 1] : tmp[size_t(i) * ch];
            }
            bufLen = keep + got;
            if (f >= bufStart + uint64_t(bufLen)) { l = r = 0; return; } // decoder ran dry
        }
        const size_t i = size_t(f - bufStart) * 2;
        l = buf[i]; r = buf[i + 1];
    }
};

MusicPlayer::MusicPlayer() {
    for (int i = 0; i < kTrackCount; ++i) {
        auto t = std::make_unique<Track>();
        t->info = kTracks[i].info;
        if (mp3Open(t->dec, kTracks[i].data, kTracks[i].size)) {
            t->ok = true;
            t->srcRate = t->dec.sampleRate;
            t->frames = t->dec.frames;
            t->buf.resize(size_t(kChunk + 3) * 2);
        }
        if (!t->ok || t->frames == 0 || t->dec.channels == 0 || t->dec.channels > 2) {
            CS_LOGW("Music: track %d could not be decoded; using the synth soundtrack", i);
            tracks_.clear();
            return;
        }
        tracks_.push_back(std::move(t));
    }
    ok_ = tracks_.size() >= 2;
    if (ok_) CS_LOGI("Music: %d tracks, %.0f / %.0f BPM", int(tracks_.size()), tracks_[0]->info.bpm, tracks_[1]->info.bpm);
}

MusicPlayer::~MusicPlayer() = default;

void MusicPlayer::start() {
    if (!ok_) return;
    playing_ = true; tapeStopping_ = false; rate_ = 1.f;
    masterTarget_ = 1.f;
    Track& run = *tracks_[0];
    if (!surging_) { run.target = 1.f; tracks_[1]->target = 0.f; }
}

void MusicPlayer::stop(bool tapeStop) {
    if (!ok_ || !playing_) return;
    surging_ = false;
    tracks_[1]->target = 0.f;
    if (tapeStop) tapeStopping_ = true; // render() winds the rate down, then stops
    else masterTarget_ = 0.f;
}

void MusicPlayer::surge(bool on) {
    if (!ok_ || on == surging_) return;
    surging_ = on;
    Track& run = *tracks_[0];
    Track& hype = *tracks_[1];
    if (on) {
        hype.seekTo(uint64_t(hype.info.loudest * hype.srcRate)); // straight into its loudest section
        hype.target = 1.f; run.target = 0.f;
    } else {
        hype.target = 0.f; run.target = 1.f;                    // the run track resumes where it paused
    }
}

void MusicPlayer::beat(int& track, double& seconds) const {
    track = beatTrack_.load(std::memory_order_relaxed);
    seconds = beatPos_.load(std::memory_order_relaxed);
}

void MusicPlayer::render(float* out, int frames, int channels, float outRate) {
    if (!ok_ || (!playing_ && master_ <= 0.f)) { beatTrack_.store(-1, std::memory_order_relaxed); return; }
    const float dt = 1.f / std::max(1.f, outRate);
    const float fadeStep = dt / kFade;
    for (int i = 0; i < frames; ++i) {
        // master fade, tape stop
        if (tapeStopping_) {
            rate_ = std::max(0.f, rate_ - dt / kTapeStop);
            if (rate_ <= 0.f) { tapeStopping_ = false; playing_ = false; master_ = masterTarget_ = 0.f; rate_ = 1.f; break; }
        }
        master_ += std::clamp(masterTarget_ - master_, -fadeStep, fadeStep);
        if (master_ <= 0.f && masterTarget_ <= 0.f) { playing_ = false; break; }
        float L = 0, R = 0;
        for (auto& tp : tracks_) {
            Track& t = *tp;
            t.gain += std::clamp(t.target - t.gain, -fadeStep, fadeStep);
            if (t.gain <= 0.f) continue;                // silent tracks hold their place
            float l, r;
            advance(t, rate_ * float(t.srcRate) / outRate, l, r);
            L += l * t.gain; R += r * t.gain;
        }
        const float g = master_ * kLevel * (muted_ ? 0.f : 1.f) * (tapeStopping_ ? std::sqrt(rate_) : 1.f);
        if (channels >= 2) { out[i * channels] += L * g; out[i * channels + 1] += R * g; }
        else out[i] += 0.5f * (L + R) * g;
    }
    // the dominant track's position, for the beat-synced visuals
    const Track& a = *tracks_[0];
    const Track& b = *tracks_[1];
    const Track& lead = b.gain > a.gain ? b : a;
    beatTrack_.store(&lead == &a ? 0 : 1, std::memory_order_relaxed);
    beatPos_.store(lead.pos / lead.srcRate, std::memory_order_relaxed);
}

// one output sample of a track: 4-point cubic (Catmull-Rom) interpolation between source frames, advancing by
// `ratio` source frames. Frames are fetched oldest first, so decoding only ever moves forward.
float MusicPlayer::advance(Track& t, float ratio, float& l, float& r) {
    const uint64_t f0 = uint64_t(t.pos);
    const float x = float(t.pos - double(f0));
    float lm, rm, l0, r0, l1, r1, l2, r2;
    t.frameAt(f0 > 0 ? f0 - 1 : f0, lm, rm);
    t.frameAt(f0, l0, r0);
    t.frameAt(f0 + 1, l1, r1);
    t.frameAt(f0 + 2, l2, r2);
    auto cr = [x](float a, float b, float c, float d) {
        return b + 0.5f * x * (c - a + x * (2.f * a - 5.f * b + 4.f * c - d + x * (3.f * (b - c) + d - a)));
    };
    l = cr(lm, l0, l1, l2);
    r = cr(rm, r0, r1, r2);
    t.pos += ratio;
    if (t.pos >= double(t.frames)) t.pos -= double(t.frames); // loop
    return l;
}

} // namespace cs::audio
