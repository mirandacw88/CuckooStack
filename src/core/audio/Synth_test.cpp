// The synthesized audio (Synth.cpp): every sound effect renders real, finite, unclipped sound; the live soundtrack
// plays, powers down, and stays silent while muted; the same commands give the same output.
// (The recorded soundtrack and applause: Music_test.cpp.)
#include "TestUtil.h"
#include "core/audio/Synth.h"

#include <cmath>
#include <vector>

using namespace cs;
using namespace cs::audio;
using namespace test;

namespace {

struct Stats { double rms = 0, peak = 0; bool finite = true, mono = true; };
Stats render(Synth& s, double seconds) {
    Stats st;
    std::vector<float> buf(512 * 2);
    double sum = 0;
    size_t n = 0;
    for (int done = 0; done < int(seconds * s.sampleRate()); done += 512) {
        s.render(buf.data(), 512, 2);
        for (size_t i = 0; i < buf.size(); i += 2) {
            st.finite = st.finite && std::isfinite(buf[i]) && std::isfinite(buf[i + 1]);
            st.mono = st.mono && buf[i] == buf[i + 1];
            st.peak = std::max(st.peak, double(std::abs(buf[i])));
            sum += double(buf[i]) * buf[i];
            ++n;
        }
    }
    st.rms = n ? std::sqrt(sum / double(n)) : 0;
    return st;
}

void effects() {
    const Sfx all[] = {Sfx::Lay, Sfx::Crack, Sfx::Perfect, Sfx::Corn, Sfx::Land, Sfx::Squawk, Sfx::Empty, Sfx::Smash,
                       Sfx::SurgeStart, Sfx::SurgeEnd, Sfx::LifeLost, Sfx::Firework, Sfx::DroidWarp, Sfx::DroidCharge, Sfx::DroidLaser};
    bool sound = true, finite = true, clean = true;
    for (Sfx f : all)
        for (int variant : {0, 2}) {
            Synth s;
            render(s, 0.05);
            s.play(f, variant);
            const Stats st = render(s, 1.5);
            sound = sound && st.rms > 1e-4;
            finite = finite && st.finite && st.mono;
            clean = clean && st.peak <= 1.0;
            if (st.rms <= 1e-4) std::printf("  silent: sfx %d variant %d\n", int(f), variant);
        }
    check(sound, "every sound effect makes a sound");
    check(finite, "...finite, the same on both channels (the synth is mono)");
    check(clean, "...and never clips past full scale");
    Synth quiet;
    check(render(quiet, 0.5).peak == 0, "nothing played: silence");
}

void music() {
    Synth s;
    s.musicStart(true);
    const Stats playing = render(s, 4.0);
    check(playing.rms > 0.01 && playing.finite && playing.peak <= 1.0, "the live soundtrack plays (finite, unclipped)");
    s.musicTempo(150);
    s.musicSurge(true);
    const Stats party = render(s, 2.0);
    check(party.rms > 0.01 && party.peak <= 1.0, "surge party mode at a faster tempo");
    s.musicSurge(false);
    s.musicStop(true);
    render(s, 3.0);
    check(render(s, 1.0).rms < playing.rms * 0.05, "power-down: the music winds down to (near) silence");
    // the mute is the Music toggle (the top-right button, "Music: off" in Settings): sound effects keep playing
    Synth m;
    m.setMuted(true);
    m.musicStart(true);
    check(render(m, 2.0).peak == 0, "music off: the soundtrack is silent");
    m.play(Sfx::Perfect);
    check(render(m, 1.0).rms > 1e-4, "music off: sound effects still play");
    m.setMuted(false);
    render(m, 0.5);
    check(render(m, 1.0).rms > 0.005, "music back on: the soundtrack again");
}

void deterministic() {
    Synth a, b;
    a.musicStart(true); b.musicStart(true);
    a.play(Sfx::Smash); b.play(Sfx::Smash);
    std::vector<float> x(4800 * 2), y(4800 * 2);
    a.render(x.data(), 4800, 2); b.render(y.data(), 4800, 2);
    check(x == y, "the same commands render the same samples");
    Synth r(44100.f);
    r.play(Sfx::Lay);
    check(r.sampleRate() == 44100.f && render(r, 0.5).rms > 1e-4, "another sample rate works too");
}

} // namespace

int main() {
    effects();
    music();
    deterministic();
    return finish("Synth");
}
