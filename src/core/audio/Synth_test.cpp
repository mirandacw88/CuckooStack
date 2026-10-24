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
                       Sfx::SurgeStart, Sfx::SurgeEnd, Sfx::LifeLost, Sfx::Firework, Sfx::DroidWarp, Sfx::DroidCharge, Sfx::DroidLaser,
                       Sfx::SawWhirr, Sfx::SawCut, Sfx::Bounce, Sfx::LaserHum, Sfx::LaserCut, Sfx::LavaSizzle, Sfx::LavaBurn};
    bool sound = true, finite = true, clean = true;
    for (Sfx f : all)
        for (int variant : {0, 1, 2}) {
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

// zero crossings per second over a stretch of a mono render: rises and falls with the sound's pitch
double crossingRate(Synth& s, double seconds) {
    std::vector<float> buf(size_t(48000 * seconds) * 2);
    s.render(buf.data(), int(buf.size() / 2), 2);
    int crossings = 0;
    for (size_t i = 2; i < buf.size(); i += 2) crossings += (buf[i - 2] < 0) != (buf[i] < 0);
    return crossings / seconds;
}

void droidSounds() {
    // the raid's sounds: as loud as the game's other effects (they have to carry over the music), and the second
    // laser shot of a double sits lower than the first
    auto rms = [](Sfx f, int k) { Synth s; render(s, 0.05); s.play(f, k); return render(s, 0.6).rms; };
    const double crack = rms(Sfx::Crack, 0);
    check(rms(Sfx::DroidWarp, 0) > 0.6 * crack && rms(Sfx::DroidCharge, 0) > 0.3 * crack && rms(Sfx::DroidLaser, 0) > 0.6 * crack,
          "droid: the warp, charge and laser are about as loud as an egg cracking");
    Synth a, b;
    render(a, 0.05); render(b, 0.05);
    a.play(Sfx::DroidLaser, 0); b.play(Sfx::DroidLaser, 1);
    check(crossingRate(b, 0.12) < crossingRate(a, 0.12), "...the second shot of a double is lower-pitched");
}

void sawSounds() {
    // the saw's sounds carry over the music like the droid's; the whine climbs as the blade comes up, and the egg
    // falling apart (a wet squelch) sits well below the blade's bite
    auto rms = [](Sfx f, int k) { Synth s; render(s, 0.05); s.play(f, k); return render(s, 0.6).rms; };
    const double crack = rms(Sfx::Crack, 0);
    check(rms(Sfx::SawWhirr, 0) > 0.3 * crack && rms(Sfx::SawCut, 0) > 0.6 * crack && rms(Sfx::SawCut, 1) > 0.6 * crack && rms(Sfx::SawCut, 2) > 0.6 * crack,
          "saw: the whine, the bite and the split are about as loud as an egg cracking");
    Synth w;
    render(w, 0.05);
    w.play(Sfx::SawWhirr, 0);
    const double early = crossingRate(w, 0.1);
    render(w, 0.2);
    check(crossingRate(w, 0.1) > early * 1.3, "...the whine rises as the blade spins up");
    Synth bite, split;
    render(bite, 0.05); render(split, 0.05);
    bite.play(Sfx::SawCut, 0); split.play(Sfx::SawCut, 1);
    check(crossingRate(split, 0.1) < crossingRate(bite, 0.1) * 0.6, "...the egg falling apart sits well below the bite");
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

// the wall laser's: its charge a split second's warning (over before the shot), and the shot, the egg's cut and the
// hen's each their own sound
void laserSounds() {
    Synth charge;
    charge.play(Sfx::LaserHum, 0);
    const Stats during = render(charge, 0.4);
    render(charge, 0.25); // (its ready blip, and the tails)
    const Stats after = render(charge, 0.5);
    check(during.rms > 1e-3 && after.rms < during.rms * 0.05, "the laser's charge: a whine for a split second, then gone (its shot comes next)");
    std::vector<double> rmsOf;
    for (int k : {0, 1, 2}) {
        Synth s;
        s.play(Sfx::LaserCut, k);
        rmsOf.push_back(render(s, 0.6).rms);
    }
    check(std::abs(rmsOf[0] - rmsOf[1]) > 1e-4 && std::abs(rmsOf[0] - rmsOf[2]) > 1e-4 && std::abs(rmsOf[1] - rmsOf[2]) > 1e-4,
          "...its shot (2), an egg cut (0) and the hen cut (1): three different sounds");
}

int main() {
    effects();
    droidSounds();
    sawSounds();
    laserSounds();
    music();
    deterministic();
    return finish("Synth");
}
