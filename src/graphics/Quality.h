// Battery / heat policy shared by the mobile platforms: internal render resolution and frame-rate cap,
// stepped down as the OS reports thermal pressure.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs::quality {

enum class Thermal { Nominal, Fair, Serious, Critical };

constexpr float PX_PER_POINT = 2.f;   // scene pixels per HUD point: plenty under bloom + fog (3x phones -> 0.67)
constexpr float MIN_SCALE = 0.5f;
constexpr int FPS = 60;               // ProMotion / 120 Hz displays would double the GPU work for little gain
constexpr int FPS_CRITICAL = 30;

inline float thermalFactor(Thermal t) {
    switch (t) {
    case Thermal::Nominal: return 1.f;
    case Thermal::Fair: return 0.9f;
    case Thermal::Serious: return 0.75f;
    case Thermal::Critical: return 0.6f;
    }
    return 1.f;
}

// pixelsPerPoint: the screen's density (iOS contentScaleFactor, Android DisplayMetrics.density)
inline float renderScale(float pixelsPerPoint, Thermal t) {
    const float base = std::clamp(PX_PER_POINT / std::max(1.f, pixelsPerPoint), MIN_SCALE, 1.f);
    return std::max(MIN_SCALE * 0.8f, base * thermalFactor(t));
}

inline int targetFps(Thermal t) { return t == Thermal::Critical ? FPS_CRITICAL : FPS; }

// Frame start times on the display's refresh grid (Android: AChoreographer's vsync). Every frame starts on a refresh,
// every k-th one for the target rate (60 Hz panel: k = 1 at 60 fps, 2 at 30), so each frame is shown for the same
// number of refreshes; timing frames from our own clock instead drifts against the panel (frames on 1, 2, 3
// refreshes: stutter). All times in ns on one monotonic clock.
//  lastFrame: the refresh the previous frame started on (0: none yet); lastVsync: the latest refresh seen;
//  period: the refresh period; now: the current time. Returns the refresh the next frame starts on.
inline int64_t nextFrameVsync(int64_t lastFrame, int64_t lastVsync, double period, int targetFps, int64_t now) {
    const int k = std::max(1, int(std::lround(1e9 / targetFps / period)));
    int64_t target = lastFrame > 0 ? lastFrame + int64_t(k * period) : now;
    if (target < now - int64_t(period)) target = now; // fell behind: don't chase old refreshes
    // snap to the grid of the latest refresh (phase-locked: no drift against the panel)
    const double n = std::ceil((double(target - lastVsync) - period * 0.25) / period);
    return lastVsync + int64_t(std::max(0.0, n) * period);
}

// Low-end devices (the GPU can't keep 60 fps): frames alternating 16.7 / 33 ms read as stutter. Fed the time each
// frame took to produce (update + render, waiting for the GPU included), it decides once whether the device is
// low-end; then for the rest of the session: lite effects (no street reflection, rain or wind streaks; the cyber
// cubes without their animation: the game reads lite()) and an even 30 fps, together.
//  - probe: the first frames are measured while the splash still covers the screen (0.4 s of warm-up skipped, then
//    1.2 s), so the switch is never seen; the platforms keep the splash up until decided().
//  - after that it keeps watching: too slow for 60 for two 2 s windows running -> low-end (a heavier scene in play).
//  - remembered(true): a device already found low-end on an earlier launch starts that way (Game remembers it).
// Thermal Critical also caps at 30 (targetFps).
class FramePacer {
public:
    int fps() const { return lite_ ? FPS_CRITICAL : FPS; }
    bool lite() const { return lite_; }
    bool decided() const { return probed_ || lite_; } // the splash may go
    void remembered(bool lowEnd) { if (lowEnd) lite_ = true; }
    // returns true when the device is found to be low-end (once)
    bool frame(double busyMs, double dtSec) {
        if (lite_) return false;
        if (!probed_ && warmup_ < 0.4) { warmup_ += dtSec; return false; } // first frames: loading, shader setup
        busy_ += busyMs; ++frames_; window_ += dtSec;
        const double need = probed_ ? 2.0 : 1.2;
        if (window_ < need) return false;
        const double avg = busy_ / frames_;
        busy_ = 0; frames_ = 0; window_ = 0;
        const bool slow = avg > 1000.0 / FPS * 1.08; // over ~18 ms on average
        if (!probed_) { // the probe: one window decides (many frames, the scene still)
            probed_ = true;
            if (slow) { lite_ = true; return true; }
            return false;
        }
        slow_ = slow ? slow_ + 1 : 0;
        if (slow_ < 2) return false;
        lite_ = true;
        return true;
    }

private:
    int slow_ = 0, frames_ = 0;
    bool lite_ = false, probed_ = false;
    double busy_ = 0, window_ = 0, warmup_ = 0;
};

} // namespace cs::quality
