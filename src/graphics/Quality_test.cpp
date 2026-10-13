// Frame pacing (Quality.h): FramePacer (an even 30 fps with lite effects on a GPU that can't keep 60, decided under
// the splash, remembered) and nextFrameVsync (frames on the display's refresh grid, Android's pacing).
#include "TestUtil.h"
#include "graphics/Quality.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

// ---------------------------------------------------------------- frame pacing

void pacing() {
    using quality::FramePacer;
    auto run = [](FramePacer& p, double busyMs, double seconds, double fps) {
        int changes = 0;
        for (int i = 0; i < int(seconds * fps); ++i) changes += p.frame(busyMs, 1.0 / fps);
        return changes;
    };
    FramePacer fast;
    check(!fast.decided(), "pacing: undecided at launch (the splash waits)");
    run(fast, 9.0, 1.7, 60);
    check(fast.decided() && fast.fps() == 60 && !fast.lite(), "probe: a GPU that keeps up is cleared within the splash (~1.6 s)");
    run(fast, 9.0, 20, 60);
    check(fast.fps() == 60 && !fast.lite(), "pacing: and stays at 60 with full effects");
    FramePacer slow;
    check(run(slow, 23.0, 1.7, 60) == 1 && slow.decided(), "probe: a low-end GPU is found while the splash still covers the screen");
    check(slow.fps() == 30 && slow.lite(), "probe: lite effects and an even 30 fps, together, before anything is seen");
    check(run(slow, 3.0, 60, 30) == 0 && slow.fps() == 30 && slow.lite(), "pacing: it stays for the session (no flip-flopping)");
    FramePacer loading;
    run(loading, 60.0, 0.35, 60); // the first frames: loading, shader setup
    run(loading, 9.0, 1.4, 60);
    check(loading.decided() && !loading.lite(), "probe: slow first frames (loading) don't count");
    FramePacer later;
    run(later, 9.0, 1.7, 60);
    run(later, 23.0, 2.1, 60);
    check(!later.lite(), "in play: one slow 2 s window isn't enough (a hitch)");
    check(run(later, 23.0, 2.1, 60) == 1 && later.lite() && later.fps() == 30, "in play: too slow for 4 s -> low-end then");
    FramePacer edge;
    run(edge, 17.5, 20, 60);
    check(edge.fps() == 60 && !edge.lite(), "pacing: just under the limit keeps everything");
    FramePacer known;
    known.remembered(true);
    check(known.decided() && known.lite() && known.fps() == 30, "remembered low-end: lite at 30 from the first frame, no probe needed");
}

// ---------------------------------------------------------------- frame timing on the refresh grid

void vsyncPacing() {
    using quality::nextFrameVsync;
    const double p = 1e9 / 60.0; // a 60 Hz panel
    const int64_t v0 = 1'000'000'000;
    auto grid = [&](int64_t t) { const double n = double(t - v0) / p; return std::abs(n - std::round(n)) < 1e-3; };
    // 30 fps on 60 Hz: every frame two refreshes after the last, whatever our own timing does
    int64_t frame = 0, vs = v0;
    bool even = true, onGrid = true;
    int64_t now = v0 + 3'000'000;
    for (int i = 0; i < 300; ++i) {
        const int64_t next = nextFrameVsync(frame, vs, p, 30, now);
        if (frame > 0) even = even && std::llabs(next - frame - int64_t(2 * p)) <= 2;
        onGrid = onGrid && grid(next);
        frame = next;
        vs = frame + int64_t(p) * (i % 2);                          // the latest refresh seen: sometimes one later
        now = frame + 1'000'000 + (i % 7) * 900'000;                // our wake-ups jitter by up to ~6 ms
    }
    check(even, "vsync: at 30 fps every frame starts exactly two refreshes after the last (no 1 / 3 refresh frames)");
    check(onGrid, "vsync: every frame starts on a refresh (phase-locked: no drift)");
    // 60 fps: every refresh
    check(nextFrameVsync(v0, v0, p, 60, v0 + 2'000'000) == v0 + int64_t(p), "vsync: at 60 fps, the next refresh");
    // a 120 Hz panel at 30 fps: every 4th refresh
    const double p120 = 1e9 / 120.0;
    check(std::llabs(nextFrameVsync(v0, v0, p120, 30, v0 + 1'000'000) - v0 - int64_t(4 * p120)) <= 2, "vsync: 120 Hz panel, 30 fps -> every 4th refresh");
    // fell behind (a long stall): start on the coming refresh, don't chase the missed ones
    const int64_t late = v0 + int64_t(10 * p) + 3'000'000;
    const int64_t r = nextFrameVsync(v0, v0 + int64_t(10 * p), p, 30, late);
    check(r >= late - int64_t(p) && r <= late + int64_t(p) && grid(r), "vsync: after a stall, the next refresh on the grid (no catch-up burst)");
    // the first frame: right away, on the grid
    const int64_t f = nextFrameVsync(0, v0, p, 30, v0 + 5'000'000);
    check(grid(f) && f >= v0 && f <= v0 + int64_t(p) + 1, "vsync: the first frame starts on the next refresh");
}

// ---------------------------------------------------------------- heat: resolution and frame rate

void thermal() {
    using namespace quality;
    check(renderScale(2.f, Thermal::Nominal) == 1.f && std::abs(renderScale(3.f, Thermal::Nominal) - 2.f / 3) < 1e-6f,
          "render scale: 2 scene pixels per point (a 3x phone renders at 2/3)");
    check(renderScale(1.f, Thermal::Nominal) == 1.f, "a 1x screen: full resolution, never upscaled past 1");
    check(renderScale(8.f, Thermal::Nominal) == 0.5f, "a very dense screen: no lower than half");
    float last = 2;
    bool down = true;
    for (Thermal t : {Thermal::Nominal, Thermal::Fair, Thermal::Serious, Thermal::Critical}) {
        down = down && renderScale(3.f, t) < last;
        last = renderScale(3.f, t);
    }
    check(down, "hotter: a lower resolution at every step");
    check(renderScale(8.f, Thermal::Critical) == 0.5f * 0.8f, "never below 40% (half, then 0.8 when critical)");
    check(thermalFactor(Thermal::Nominal) == 1.f && thermalFactor(Thermal::Critical) == 0.6f, "thermal factor: 1 -> 0.6");
    check(targetFps(Thermal::Nominal) == 60 && targetFps(Thermal::Serious) == 60 && targetFps(Thermal::Critical) == 30,
          "frame rate: 60, dropping to 30 only when critical");
}

} // namespace

int main() {
    pacing();
    vsyncPacing();
    thermal();
    return finish("Quality");
}
