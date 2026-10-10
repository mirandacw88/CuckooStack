// The recorded soundtrack (audio/Music.h): one track, sped up during a surge, tape stop; beat grid for the visuals.
#include "TestUtil.h"
#include "../src/core/audio/Synth.h"

#include <cmath>
#include <vector>

using namespace cs;
using test::check;

namespace {
void play(audio::Synth& s, double secs, std::vector<float>* keep = nullptr) {
    std::vector<float> b(512 * 2);
    for (int i = 0; i < int(secs * 48000 / 512); ++i) {
        s.render(b.data(), 512, 2);
        if (keep) keep->insert(keep->end(), b.begin(), b.end());
    }
}
double rms(const std::vector<float>& v) { double e = 0; for (float x : v) e += double(x) * x; return std::sqrt(e / std::max<size_t>(1, v.size())); }
} // namespace

int main() {
    const audio::TrackInfo* run = audio::musicTrackInfo(0);
    check(run && !audio::musicTrackInfo(1), "one track is built in");
    if (!run) return test::finish("music");
    check(std::abs(run->bpm - 155) < 0.5, "beat grid: 155 BPM");

    audio::Synth s(48000);
    double bpm = 0, first = 0, pos = 0, rate = 0;
    check(!s.musicBeat(bpm, first, pos, rate), "silent until the music starts");
    s.musicStart();
    std::vector<float> out;
    play(s, 3, &out);
    check(rms(out) > 0.02, "the track is audible");
    check(s.musicBeat(bpm, first, pos, rate) && std::abs(bpm - run->bpm) < 0.01 && std::abs(pos - 3) < 0.05 &&
          std::abs(rate - 1) < 1e-6, "it plays from the start at normal speed");
    s.musicSurge(true);
    play(s, 0.5);
    const double before = pos;
    s.musicBeat(bpm, first, pos, rate);
    const double p0 = pos;
    play(s, 2);
    s.musicBeat(bpm, first, pos, rate);
    check(std::abs(rate - 1.15) < 1e-3, "a surge speeds the same track up to 1.15x");
    check(p0 > before && std::abs((pos - p0) - 2 * 1.15) < 0.05, "...so it advances 1.15 s of music per second");
    s.musicSurge(false);
    play(s, 1);
    s.musicBeat(bpm, first, pos, rate);
    const double p1 = pos;
    play(s, 1);
    s.musicBeat(bpm, first, pos, rate);
    check(std::abs(rate - 1) < 1e-6 && std::abs((pos - p1) - 1) < 0.02, "after the surge it eases back to normal speed");
    s.musicStop(true);
    play(s, 1.5);
    check(!s.musicBeat(bpm, first, pos, rate), "a crash winds the tape down to silence");
    s.musicStart();
    play(s, 1);
    check(s.musicBeat(bpm, first, pos, rate) && pos > 8, "the next run carries on from there");
    s.setMuted(true);
    out.clear();
    play(s, 1, &out);
    check(rms(out) < 1e-4, "muted: no music");
    return test::finish("music");
}
