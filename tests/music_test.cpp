// The recorded soundtrack (audio/Music.h): run track, surge crossfade, resume, tape stop; beat grid for the visuals.
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
    const audio::TrackInfo* hype = audio::musicTrackInfo(1);
    check(run && hype, "two tracks are built in");
    if (!run || !hype) return test::finish("music");
    check(std::abs(run->bpm - 155) < 0.5 && std::abs(hype->bpm - 168) < 0.5, "beat grids: 155 BPM run track, 168 BPM surge track");

    audio::Synth s(48000);
    double bpm = 0, first = 0, pos = 0;
    check(!s.musicBeat(bpm, first, pos), "silent until the music starts");
    s.musicStart();
    std::vector<float> out;
    play(s, 3, &out);
    check(rms(out) > 0.02, "the run track is audible");
    check(s.musicBeat(bpm, first, pos) && std::abs(bpm - run->bpm) < 0.01 && std::abs(pos - 3) < 0.05, "it plays from the start at its own tempo");
    s.musicSurge(true);
    play(s, 2);
    check(s.musicBeat(bpm, first, pos) && std::abs(bpm - hype->bpm) < 0.01, "a surge crossfades to the surge track");
    check(pos > hype->loudest && pos < hype->loudest + 3, "straight into its loudest section");
    s.musicSurge(false);
    play(s, 2);
    check(s.musicBeat(bpm, first, pos) && std::abs(bpm - run->bpm) < 0.01 && pos > 3 && pos < 6, "after the surge the run track resumes where it paused");
    s.musicStop(true);
    play(s, 1.5);
    check(!s.musicBeat(bpm, first, pos), "a crash winds the tape down to silence");
    s.musicStart();
    play(s, 1);
    check(s.musicBeat(bpm, first, pos) && pos > 5, "the next run carries on from there");
    s.setMuted(true);
    out.clear();
    play(s, 1, &out);
    check(rms(out) < 1e-4, "muted: no music");
    return test::finish("music");
}
