// The recorded soundtrack (audio/Music.h): one track, sped up during a surge, tape stop, restart vs revive; beat grid for the visuals;
// the finish-line applause and firework bursts.
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
    s.musicStart(true);
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
    s.musicStart(false);
    play(s, 1);
    check(s.musicBeat(bpm, first, pos, rate) && pos > 8, "a revive carries on from there");
    s.musicStop(true);
    play(s, 1.5);
    s.musicStart(true);
    play(s, 1);
    check(s.musicBeat(bpm, first, pos, rate) && std::abs(pos - 1) < 0.05, "a new run starts the song from the top");
    s.musicStart(true);
    play(s, 1);
    check(s.musicBeat(bpm, first, pos, rate) && std::abs(pos - 2) < 0.05, "...but not if it's already playing (title into the first run)");
    s.musicStop(true);
    play(s, 0.3);
    s.musicStart(true);
    play(s, 1);
    check(s.musicBeat(bpm, first, pos, rate) && std::abs(pos - 1) < 0.05, "a quick retry, mid wind-down, also starts over");
    s.setMuted(true);
    out.clear();
    play(s, 1, &out);
    check(rms(out) < 1e-4, "muted: no music");
    s.play(Sfx::Cheer);
    out.clear();
    play(s, 1, &out);
    check(rms(out) < 1e-4, "muted: no applause");

    // the applause: heard on its own, played once, not cut off when the music fades out (a level clear)
    audio::Synth c(48000);
    c.play(Sfx::Cheer);
    out.clear();
    play(c, 2, &out);
    check(rms(out) > 0.02, "the applause is audible without music");
    c.musicStart(true);
    play(c, 1);
    c.musicStop(false);
    out.clear();
    play(c, 4, &out);
    check(rms(std::vector<float>(out.end() - 48000, out.end())) > 0.01, "the music fading out doesn't cut it off");
    play(c, 2);
    out.clear();
    play(c, 1, &out);
    check(rms(out) < 1e-4, "it plays once (about 8 s), then stops");

    // the finish-line fireworks: a synthesized burst per shell, loud but under clipping even in a dense barrage
    audio::Synth f(48000);
    out.clear();
    std::vector<float> b(512 * 2);
    for (int i = 0; i < 48000 * 2 / 512; ++i) {
        if (i % 9 == 0) f.play(Sfx::Firework, i % 3);
        f.render(b.data(), 512, 2);
        out.insert(out.end(), b.begin(), b.end());
    }
    float peak = 0;
    for (float x : out) peak = std::max(peak, std::abs(x));
    check(rms(out) > 0.05 && peak < 1.f, "firework bursts are audible and don't clip");
    return test::finish("music");
}
