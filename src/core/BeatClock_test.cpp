// The beat clock (BeatClock.h): the kick-drum pulse the visuals breathe with, mirroring the web build's music
// scheduler (126 BPM, tempo follows speed, no kicks in the two-bar build before the drop, the surge party pattern),
// or locked to the recorded soundtrack's beat grid.
#include "TestUtil.h"
#include "core/BeatClock.h"

#include <vector>

using namespace cs;
using namespace test;

namespace {

// times at which the pulse jumps back up (a kick), stepping in small real-time increments
std::vector<double> kicks(BeatClock& c, double seconds, double dt = 1.0 / 960.0) {
    std::vector<double> out;
    double t = 0;
    float last = c.pulse();
    for (; t < seconds; t += dt) {
        c.advance(dt);
        const float p = c.pulse();
        if (p > last + 0.05f) out.push_back(t);
        last = p;
    }
    return out;
}

void steady() {
    BeatClock c;
    check(c.pulse() == 0.f, "before it starts: no pulse");
    c.start();
    const auto k = kicks(c, 8.0);
    const double beat = 60.0 / 126.0;
    bool even = !k.empty();
    for (size_t i = 1; i < k.size(); ++i) even = even && std::abs(k[i] - k[i - 1] - beat) < 0.005;
    check(even && k.size() >= 15, "126 BPM: a kick on every beat");
    check(c.bpm() == 126.0, "starts at 126 BPM");
}

void buildBeforeDrop() {
    BeatClock c;
    c.start();
    const double bar = 4 * 60.0 / 126.0;
    const auto k = kicks(c, bar * 25);
    int inBuild = 0, before = 0;
    for (double t : k) {
        const double b = (t - 0.08) / bar;
        if (b >= 22.0 + 0.01 && b < 24.0 - 0.01) inBuild++;
        if (b >= 20.0 && b < 22.0 - 0.01) before++;
    }
    check(before >= 7 && inBuild == 0, "bars 22-23 (the build before the drop): no kicks");
}

void party() {
    BeatClock c;
    c.start();
    c.setParty(true);
    const auto k = kicks(c, 60.0 / 126.0 * 8);
    const double sixteenth = 60.0 / 126.0 / 4;
    int doubles = 0;
    for (size_t i = 1; i < k.size(); ++i) doubles += std::abs(k[i] - k[i - 1] - sixteenth) < 0.004;
    check(doubles >= 1 && k.size() >= 9, "surge party: every beat plus the last 16th of the bar");
}

void tempo() {
    BeatClock c;
    c.start();
    c.setTempoFromSpeed(8);
    check(c.bpm() == 126.0, "tempo: 126 BPM up to 8 m/s");
    c.setTempoFromSpeed(12);
    check(std::abs(c.bpm() - (126.0 + 4 * 3.4)) < 1e-9, "tempo: +3.4 BPM per m/s above 8");
    c.setTempoFromSpeed(40, 140);
    check(c.bpm() == 140.0, "tempo: capped");
    c.syncToTrack(128, 0.5, 10.0);
    c.setTempoFromSpeed(20);
    check(c.bpm() == 128.0, "tracked: the soundtrack's BPM wins over speed");
    c.syncToTrack(128, 0.5, 10.0, 1.15);
    check(std::abs(c.bpm() - 128 * 1.15) < 1e-9, "tracked: a faster playback rate, faster beats");
}

void tracked() {
    BeatClock c;
    c.start();
    // on a beat exactly: the pulse is at its peak
    c.syncToTrack(120, 0.0, 2.0);
    check(c.pulse() > 0.99f, "tracked: on the beat, full pulse");
    c.syncToTrack(120, 0.0, 2.25); // half a beat later
    const float mid = c.pulse();
    check(mid < 0.3f && mid > 0.f, "tracked: between beats, decayed");
    c.advance(0.5);
    check(c.pulse() < mid, "tracked: advancing alone schedules no synthetic kicks (the pulse only decays)");
    c.untrack();
    c.advance(1.0);
    check(kicks(c, 2.0).size() >= 3, "untracked: back to the synthetic clock");
    c.syncToTrack(120, 5.0, 2.0);
    check(c.pulse() < 0.99f, "tracked: before the first beat, no fresh kick");
}

void stop() {
    BeatClock c;
    c.start();
    c.advance(1.0);
    c.stop();
    c.advance(1.0);
    check(c.pulse() == 0.f, "stopped: the pulse fades to exactly 0");
    BeatClock s;
    s.start();
    s.advance(0.1);
    check(s.pulse() > 0.f, "the first kick comes right after start");
}

} // namespace

int main() {
    steady();
    buildBeforeDrop();
    party();
    tempo();
    tracked();
    stop();
    return finish("BeatClock");
}
