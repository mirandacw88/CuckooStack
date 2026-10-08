#include "Synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cs::audio {

namespace {
constexpr int kSub = 16;              // automation is evaluated every 16 samples (~0.33 ms) and interpolated
constexpr int kBlock = 256;           // internal render block
constexpr double kLookahead = 0.15;   // schedule() runs notes up to 150 ms ahead, like the web build
constexpr float kTwoPi = 6.28318530718f;

float hz(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }

// PolyBLEP residual: removes the aliasing of naive saw/square edges
float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

// DynamicsCompressorNode static curve, as defined by the Web Audio spec: identity below the threshold, an
// exponential knee  y = T + (1 - e^(-k (x - T))) / k  (linear units) whose k is solved so the slope in dB at the
// knee end equals 1/ratio, then a straight 1/ratio line in dB.
class CompressorCurve {
public:
    CompressorCurve(float thresholdDb, float kneeDb, float ratio) : ratio_(ratio) {
        t_ = dbToLin(thresholdDb);
        kneeEndDb_ = thresholdDb + kneeDb;
        kneeEnd_ = dbToLin(kneeEndDb_);
        double lo = 0.1, hi = 10000.0; // slope(k) decreases with k: bisect in log space
        for (int i = 0; i < 64; ++i) {
            const double mid = std::sqrt(lo * hi);
            (slopeDb(mid) > 1.0 / ratio ? lo : hi) = mid;
        }
        k_ = std::sqrt(lo * hi);
        kneeEndOutDb_ = linToDb(knee(kneeEnd_));
    }
    // gain (linear) the curve applies to an input of linear amplitude x
    double gainFor(double x) const {
        if (x <= t_) return 1.0;
        if (x < kneeEnd_) return knee(x) / x;
        const double yDb = kneeEndOutDb_ + (linToDb(x) - kneeEndDb_) / ratio_;
        return dbToLin(yDb) / x;
    }

private:
    static double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
    static double linToDb(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }
    double knee(double x) const { return t_ + (1.0 - std::exp(-k_ * (x - t_))) / k_; }
    double slopeDb(double k) const { // d(y_dB)/d(x_dB) at the knee end for a given k
        const double y = t_ + (1.0 - std::exp(-k * (kneeEnd_ - t_))) / k;
        return std::exp(-k * (kneeEnd_ - t_)) * kneeEnd_ / y;
    }
    double ratio_, t_, kneeEnd_, kneeEndDb_, kneeEndOutDb_, k_ = 1;
};
const CompressorCurve& musicCompressor() { static const CompressorCurve c(-14.f, 30.f, 4.f); return c; }

// A minor - F - C - G, voiced for smooth movement
struct Chord { int bass; int stab[3]; int arp[4]; };
const Chord CH[4] = {{45, {69, 72, 76}, {69, 72, 76, 81}},
                     {41, {65, 69, 72}, {65, 69, 72, 77}},
                     {48, {67, 72, 76}, {67, 72, 76, 79}},
                     {43, {67, 71, 74}, {67, 71, 74, 79}}};
const int ARP[16] = {0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 2, 1, 3, 2};
// drop hook in 16ths: -1 holds, 0 rests
const int LEAD[4][16] = {{81, -1, 0, 79, -1, 0, 76, -1, 0, 76, 79, 0, 81, 0, 84, -1},
                         {84, -1, 0, 81, -1, 0, 77, -1, 0, 77, 81, 0, 84, 0, 86, -1},
                         {84, -1, 0, 79, -1, 0, 76, -1, 0, 76, 79, 0, 83, 0, 84, -1},
                         {83, -1, 0, 79, -1, 0, 74, -1, 0, 79, 0, 81, -1, 0, 83, -1}};
} // namespace

Synth::Synth(float sampleRate) : sr_(sampleRate) { setSampleRate(sampleRate); }

void Synth::setSampleRate(float sr) {
    sr_ = sr;
    // ensure(): nb = 2 s of white noise; delay line up to 1 s (createDelay(1))
    noiseBuffer_.resize(size_t(sr * 2));
    for (float& v : noiseBuffer_) v = random01() * 2.f - 1.f;
    delayLine_.assign(size_t(sr) + kBlock + 4, 0.f);
    delayWrite_ = 0;
    // makeup gain = (1 / curve(1.0))^0.6 (Web Audio spec)
    compMakeup_ = float(std::pow(1.0 / musicCompressor().gainFor(1.0), 0.6));
    for (auto* b : {&mono_, &busPump_, &busDrums_, &busDelay_, &busMaster_}) b->assign(kBlock, 0.f);
}

float Synth::random01() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
    return float((rng_ >> 40) & 0xFFFFFF) / 16777216.f;
}

// ---------------------------------------------------------------- command queue (game thread side)

void Synth::push(const Command& c) {
    const size_t h = head_.load(std::memory_order_relaxed), next = (h + 1) % kQueue;
    if (next == tail_.load(std::memory_order_acquire)) return; // full: drop (never block the game)
    queue_[h] = c;
    head_.store(next, std::memory_order_release);
}
void Synth::play(Sfx s, int variant) { push({Command::Sfx, int(s) | (variant << 8), 0}); }
void Synth::musicStart() { push({Command::MusicStart, 0, 0}); }
void Synth::musicStop(bool sad) { push({Command::MusicStop, sad ? 1 : 0, 0}); }
void Synth::musicTempo(float bpm) { push({Command::Tempo, 0, bpm}); }
void Synth::musicSurge(bool on) { push({Command::Surge, on ? 1 : 0, 0}); }
void Synth::setMuted(bool m) { push({Command::Mute, m ? 1 : 0, 0}); }

void Synth::processCommands() {
    size_t t = tail_.load(std::memory_order_relaxed);
    while (t != head_.load(std::memory_order_acquire)) {
        const Command c = queue_[t];
        switch (c.type) {
        case Command::Sfx: sfx(Sfx(c.a & 0xFF), c.a >> 8); break;
        case Command::MusicStart: startMusic(); break;
        case Command::MusicStop: stopMusic(c.a != 0); break;
        case Command::Tempo: bpm_ = c.f; break;
        case Command::Surge:
            if (c.a && playing_ && !muted_ && !party_) { // party music only while the music is audible
                party_ = true; partyStep_ = 0;
                filtFreq_.cancelScheduledValues(now_);   // open any intro / build filter fully
                filtFreq_.setValueAtTime(18000, now_);
            } else if (!c.a) {
                party_ = false;                          // song resumes at step_, where it left off
            }
            break;
        case Command::Mute:
            muted_ = c.a != 0;
            out_.setTargetAtTime(muted_ || !playing_ ? 0.f : 0.55f, now_, 0.05f); // music.toggle()
            break;
        }
        t = (t + 1) % kQueue;
        tail_.store(t, std::memory_order_release);
    }
}

// ---------------------------------------------------------------- sfx (tone / noise / sfx object)

Synth::Voice& Synth::newVoice(double start, double stop, Bus dest) {
    Voice& v = voices_.emplace_back();
    v.start = start; v.stop = stop; v.dest = dest;
    return v;
}

void Synth::tone(float f0, float f1, float dur, Wave type, float vol, float delay) {
    const double t = now_ + delay;
    Voice& v = newVoice(t, t + dur + 0.02, Bus::Master);
    Osc& o = v.oscs.emplace_back(Osc{type, Param(f0), 0.f, type == Wave::Sawtooth ? 0.5 : 0.0});
    o.freq.setValueAtTime(f0, t);
    o.freq.exponentialRampToValueAtTime(std::max(20.f, f1), t + dur);
    v.gain.setValueAtTime(0.0001f, t);
    v.gain.exponentialRampToValueAtTime(vol, t + 0.01);
    v.gain.exponentialRampToValueAtTime(0.0001f, t + dur);
}

void Synth::noise(float dur, float vol, float freq) {
    const double t = now_;
    const size_t n = size_t(sr_ * dur);
    Voice& v = newVoice(t, t + dur, Bus::Master);
    v.ownBuffer.resize(n);
    for (size_t i = 0; i < n; ++i) v.ownBuffer[i] = (random01() * 2.f - 1.f) * std::pow(1.f - float(i) / float(n), 2.5f);
    v.filtered = true; v.filterType = Biquad::Type::Bandpass; v.q = 0.9f; v.filterFreq.setValue(freq);
    v.gain.setValue(vol);
}

void Synth::sfx(Sfx s, int k) {
    switch (s) {
    case Sfx::Lay: {
        static const int scale[] = {69, 72, 74, 76, 79, 81, 84, 86, 88, 91};
        const float f = hz(float(scale[std::min(std::max(k, 0), 9)]));
        tone(f, f, 0.16f, Wave::Triangle, 0.2f);
        tone(f * 2, f * 2, 0.07f, Wave::Square, 0.03f);
        tone(f * 0.5f, f * 0.5f, 0.05f, Wave::Sine, 0.15f);
        break;
    }
    case Sfx::Crack: noise(0.14f, 0.5f, 2600); tone(240, 90, 0.12f, Wave::Triangle, 0.12f); break;
    case Sfx::Perfect: {
        const float m = std::pow(2.f, float(k) / 12.f);
        const float notes[] = {784, 988, 1175, 1568};
        for (int i = 0; i < 4; ++i) tone(notes[i] * m, notes[i] * m, 0.14f, Wave::Square, 0.05f, i * 0.05f);
        break;
    }
    case Sfx::Corn: {
        const float notes[] = {1046.5f, 1318.5f, 1568, 2093, 2637};
        for (int i = 0; i < 5; ++i) tone(notes[i], notes[i] * 1.01f, 0.1f, Wave::Triangle, 0.07f, i * 0.035f);
        tone(523, 1046, 0.18f, Wave::Sine, 0.06f);
        break;
    }
    case Sfx::Land: noise(0.06f, 0.18f, 600); break;
    case Sfx::Squawk:
        tone(950, 320, 0.28f, Wave::Sawtooth, 0.09f);
        tone(1200, 500, 0.22f, Wave::Square, 0.04f, 0.12f);
        noise(0.25f, 0.35f, 1500);
        break;
    case Sfx::Empty: tone(220, 180, 0.08f, Wave::Square, 0.05f); break;
    case Sfx::Smash: // crunchy noise burst + low pitch-dropping thump
        noise(0.22f, 0.55f, 1900);
        noise(0.12f, 0.4f, 650);
        tone(170, 38, 0.3f, Wave::Sine, 0.55f);
        tone(320, 90, 0.08f, Wave::Square, 0.06f);
        break;
    case Sfx::SurgeStart: { // rising power-up sweep + quick ascending arpeggio
        tone(180, 1800, 0.65f, Wave::Sawtooth, 0.07f);
        tone(90, 900, 0.65f, Wave::Square, 0.04f);
        const float notes[] = {880, 1046.5f, 1318.5f, 1760, 2093};
        for (int i = 0; i < 5; ++i) tone(notes[i], notes[i], 0.12f, Wave::Square, 0.05f, 0.08f + i * 0.055f);
        break;
    }
    case Sfx::LifeLost: // glassy crack + a two-step falling chime
        noise(0.1f, 0.35f, 3200);
        tone(988, 740, 0.12f, Wave::Triangle, 0.10f);
        tone(740, 370, 0.26f, Wave::Triangle, 0.11f, 0.11f);
        tone(185, 92, 0.3f, Wave::Sine, 0.18f, 0.05f);
        break;
    case Sfx::SurgeEnd: tone(1500, 160, 0.5f, Wave::Sawtooth, 0.06f); tone(750, 80, 0.5f, Wave::Triangle, 0.08f); break;
    }
}

// ---------------------------------------------------------------- music: original progressive house, synthesized live

void Synth::env(Param& g, double t, float a, float peak, float dec) {
    g.setValueAtTime(0.0001f, t);
    g.exponentialRampToValueAtTime(peak, t + a);
    g.exponentialRampToValueAtTime(0.0001f, t + a + dec);
}

void Synth::noiseHit(double t, float dur, float vol, Biquad::Type type, float freq, float q, Bus dest) {
    Voice& v = newVoice(t, t + dur + 0.05, dest);
    v.buffer = &noiseBuffer_;
    v.bufPos = double(random01()) * 1.5 * sr_; // s.start(t, Math.random() * 1.5)
    v.filtered = true; v.filterType = type; v.q = q; v.filterFreq.setValue(freq);
    env(v.gain, t, 0.002f, vol, dur);
}

void Synth::kick(double t) {
    Voice& v = newVoice(t, t + 0.45, Bus::Drums);
    Osc& o = v.oscs.emplace_back(Osc{Wave::Sine, Param(160), 0.f, 0.0});
    o.freq.setValueAtTime(160, t);
    o.freq.exponentialRampToValueAtTime(42, t + 0.11);
    v.gain.setValueAtTime(1.1f, t);
    v.gain.exponentialRampToValueAtTime(0.0001f, t + 0.42);
    noiseHit(t, 0.012f, 0.3f, Biquad::Type::Highpass, 3000);
    // sidechain pump on everything melodic
    const double beat = 60.0 / bpm_;
    pump_.cancelScheduledValues(t);
    pump_.setValueAtTime(0.22f, t);
    pump_.linearRampToValueAtTime(1.f, t + beat * 0.62);
}

void Synth::clap(double t) {
    for (int i = 0; i < 3; ++i) noiseHit(t + i * 0.011, i == 2 ? 0.16f : 0.02f, 0.45f, Biquad::Type::Bandpass, 1500, 1.4f);
}
void Synth::snare(double t, float vol, float pitch) { noiseHit(t, 0.09f, vol, Biquad::Type::Bandpass, pitch, 1.1f); }

void Synth::saws(int m, double t, float dur, float vol, std::initializer_list<float> detunes, float cutoff, Bus dest, float send) {
    Voice& v = newVoice(t, t + dur + 0.15, dest);
    v.filtered = true; v.filterType = Biquad::Type::Lowpass; v.q = 1.5f;
    v.filterFreq.setValueAtTime(cutoff, t);
    v.filterFreq.exponentialRampToValueAtTime(cutoff * 0.35f, t + dur + 0.1);
    v.gain.setValueAtTime(0.0001f, t);
    v.gain.exponentialRampToValueAtTime(vol, t + 0.006);
    v.gain.setValueAtTime(vol * 0.8f, t + dur);
    v.gain.exponentialRampToValueAtTime(0.0001f, t + dur + 0.12);
    v.delaySend = send;
    for (float dt : detunes) v.oscs.push_back(Osc{Wave::Sawtooth, Param(hz(float(m))), dt, 0.5 + 0.13 * double(v.oscs.size())});
}

void Synth::bass(int m, double t, float dur) {
    Voice& v = newVoice(t, t + dur + 0.05, Bus::Pump);
    v.oscs.push_back(Osc{Wave::Sawtooth, Param(hz(float(m))), 0.f, 0.5});
    v.oscs.push_back(Osc{Wave::Sine, Param(hz(float(m - 12))), 0.f, 0.0});
    v.filtered = true; v.filterType = Biquad::Type::Lowpass; v.q = 6;
    v.filterFreq.setValueAtTime(1400, t);
    v.filterFreq.exponentialRampToValueAtTime(220, t + dur);
    env(v.gain, t, 0.005f, 0.5f, dur);
}

void Synth::pluck(int m, double t, float vol) {
    Voice& v = newVoice(t, t + 0.25, Bus::Pump);
    v.oscs.push_back(Osc{Wave::Square, Param(hz(float(m))), 0.f, 0.0});
    v.oscs.push_back(Osc{Wave::Sawtooth, Param(hz(float(m))), 9.f, 0.5});
    v.filtered = true; v.filterType = Biquad::Type::Lowpass; v.q = 7;
    v.filterFreq.setValueAtTime(4200, t);
    v.filterFreq.exponentialRampToValueAtTime(380, t + 0.16);
    env(v.gain, t, 0.003f, vol, 0.2f);
    v.delaySend = 0.55f;
}

void Synth::crash(double t) {
    noiseHit(t, 1.6f, 0.35f, Biquad::Type::Highpass, 6000, 0.7f);
    Voice& v = newVoice(t, t + 1.2, Bus::Drums);
    Osc& o = v.oscs.emplace_back(Osc{Wave::Sine, Param(70), 0.f, 0.0});
    o.freq.setValueAtTime(70, t);
    o.freq.exponentialRampToValueAtTime(30, t + 1);
    env(v.gain, t, 0.01f, 0.7f, 1.1f);
}

void Synth::riser(double t, float dur) {
    Voice& v = newVoice(t, t + dur + 0.1, Bus::Drums);
    v.buffer = &noiseBuffer_; v.loop = true;
    v.filtered = true; v.filterType = Biquad::Type::Bandpass; v.q = 3;
    v.filterFreq.setValueAtTime(300, t);
    v.filterFreq.exponentialRampToValueAtTime(9000, t + dur);
    v.gain.setValueAtTime(0.0001f, t);
    v.gain.exponentialRampToValueAtTime(0.28f, t + dur);
    v.gain.linearRampToValueAtTime(0.0001f, t + dur + 0.05);
}

// rising "woop" siren at the top of each party bar
void Synth::woop(double t, float dur) {
    Voice& v = newVoice(t, t + dur + 0.05, Bus::Drums);
    Osc& o = v.oscs.emplace_back(Osc{Wave::Sawtooth, Param(320), 0.f, 0.5});
    o.freq.setValueAtTime(320, t);
    o.freq.exponentialRampToValueAtTime(1500, t + dur);
    v.filtered = true; v.filterType = Biquad::Type::Bandpass; v.q = 2.5f;
    v.filterFreq.setValueAtTime(600, t);
    v.filterFreq.exponentialRampToValueAtTime(3200, t + dur);
    v.gain.setValueAtTime(0.0001f, t);
    v.gain.exponentialRampToValueAtTime(0.16f, t + dur * 0.7);
    v.gain.exponentialRampToValueAtTime(0.0001f, t + dur);
}

// Surge party pattern: same key and chords as the main song, everything doubled up.
void Synth::schedulePartyStep(double t, double sixteenth) {
    const int s = partyStep_ % 16, bar = partyStep_ / 16;
    const Chord& ch = CH[bar % 4];
    if (s % 4 == 0 || s == 15) kick(t);                                    // four on the floor + double kick
    if (s == 4 || s == 12) clap(t);
    if (bar % 2 == 1 && s >= 12) snare(t, 0.1f + (s - 12) * 0.07f, 1400.f + (s - 12) * 450.f); // roll over the last beat
    noiseHit(t, 0.03f, s % 2 ? 0.13f : 0.06f, Biquad::Type::Highpass, 8000); // closed hats, off-beats accented
    if (s % 4 == 2) noiseHit(t, 0.12f, 0.17f, Biquad::Type::Highpass, 7000); // open hat on the off-beat 8th
    bass(s % 2 ? ch.bass + 12 : ch.bass, t, float(sixteenth * 0.85));       // octave bounce
    pluck(ch.arp[ARP[s]] + 24, t, 0.045f);                                  // arp two octaves up
    if (s % 4 == 2)
        for (int n : ch.stab) saws(n, t, float(sixteenth * 1.2), 0.035f, {-16, -6, 0, 7, 17}, 5200, Bus::Pump, 0.2f);
    const int n = LEAD[bar % 4][s];                                         // main hook, an octave up
    if (n > 0) {
        int len = 1;
        while (s + len < 16 && LEAD[bar % 4][s + len] == -1) len++;
        saws(n + 12, t, float(sixteenth * len * 0.92), 0.05f, {-20, -7, 6, 19}, 7000, Bus::Pump, 0.3f);
    }
    if (s == 0) woop(t, float(sixteenth * 8));
    partyStep_++;
}

void Synth::schedule() {
    const double sixteenth = 60.0 / bpm_ / 4.0;
    while (nextT_ < now_ + kLookahead) {
        if (party_) {
            delayTime_.setValueAtTime(float(sixteenth * 3), nextT_);
            schedulePartyStep(nextT_, sixteenth);
            nextT_ += sixteenth;
            continue;
        }
        const int s = step_ % 16, bar = (step_ / 16) % 32;
        const Chord& ch = CH[bar % 4];
        const double t = nextT_;
        const char sec = bar < 8 ? 'A' : bar < 16 ? 'B' : bar < 24 ? 'C' : 'D';
        const bool buildDrop = sec == 'C' && bar >= 22;
        delayTime_.setValueAtTime(float(sixteenth * 3), t);
        if (s == 0 && bar == 16) {
            filtFreq_.cancelScheduledValues(t);
            filtFreq_.setValueAtTime(900, t);
            filtFreq_.exponentialRampToValueAtTime(18000, t + sixteenth * 16 * 8);
            riser(t + sixteenth * 16 * 4, float(sixteenth * 16 * 4));
        }
        if (s == 0 && bar == 24) crash(t);
        // drums
        if (s % 4 == 0 && !buildDrop) kick(t);
        if ((s == 4 || s == 12) && sec != 'A' && !buildDrop) clap(t);
        if (sec != 'C' || !buildDrop) {
            noiseHit(t, 0.03f, s % 2 ? 0.12f : 0.06f, Biquad::Type::Highpass, 8000);
            if (s % 4 == 2) noiseHit(t, 0.12f, 0.16f, Biquad::Type::Highpass, 7000);
        }
        if (buildDrop) {
            const int every = bar == 22 ? (s < 8 ? 4 : 2) : 1;
            if (s % every == 0) snare(t, 0.12f + (bar - 22) * 0.12f + s * 0.008f, 1200.f + (bar - 22) * 900.f + s * 70.f);
        }
        // bass
        if (sec == 'D') { if (s % 4 != 0) bass(s % 4 == 3 ? ch.bass + 12 : ch.bass, t, float(sixteenth * 0.9)); }
        else if (s % 4 == 2 && !buildDrop) bass(s == 14 ? ch.bass + 12 : ch.bass, t, float(sixteenth * 1.7));
        // arp
        if (sec == 'A' ? s % 2 == 0 : true) pluck(ch.arp[ARP[s]] + (sec == 'D' ? 12 : 0), t, sec == 'D' ? 0.06f : 0.09f);
        // chord stabs
        if (sec == 'B' && (s == 0 || s == 3 || s == 6 || s == 10))
            for (int n : ch.stab) saws(n, t, float(sixteenth * 1.4), 0.035f, {-16, -6, 0, 7, 17}, 3200, Bus::Pump, 0.25f);
        if (sec == 'C' && s % 4 == 2)
            for (int n : ch.stab) saws(n, t, float(sixteenth * 1.2), 0.03f, {-12, 0, 12}, 2400, Bus::Pump, 0.3f);
        if (sec == 'D' && (s == 2 || s == 6 || s == 10 || s == 14))
            for (int n : ch.stab) saws(n, t, float(sixteenth * 1.6), 0.04f, {-18, -8, 0, 8, 18}, 5200, Bus::Pump, 0.2f);
        // drop lead
        if (sec == 'D') {
            const int n = LEAD[bar % 4][s];
            if (n > 0) {
                int len = 1;
                while (s + len < 16 && LEAD[bar % 4][s + len] == -1) len++;
                saws(n, t, float(sixteenth * len * 0.92), 0.07f, {-20, -7, 6, 19}, 6500, Bus::Pump, 0.35f);
                saws(n - 12, t, float(sixteenth * len * 0.92), 0.03f, {0}, 2500, Bus::Pump, 0);
            }
        }
        nextT_ += sixteenth;
        step_++;
    }
}

void Synth::powerDown() {
    if (muted_) return;
    const double t = now_;
    filtFreq_.cancelScheduledValues(t);
    filtFreq_.setValueAtTime(filtFreq_.at(t), t);
    filtFreq_.exponentialRampToValueAtTime(140, t + 0.7);
    Voice& v = newVoice(t, t + 1, Bus::Master);
    Osc& o = v.oscs.emplace_back(Osc{Wave::Sawtooth, Param(hz(57)), 0.f, 0.5});
    o.freq.setValueAtTime(hz(57), t);
    o.freq.exponentialRampToValueAtTime(28, t + 0.9);
    v.filtered = true; v.filterType = Biquad::Type::Lowpass; v.q = 1;
    v.filterFreq.setValueAtTime(2400, t);
    v.filterFreq.exponentialRampToValueAtTime(150, t + 0.9);
    env(v.gain, t, 0.01f, 0.14f, 0.9f);
}

void Synth::startMusic() {
    const double t = now_;
    step_ = 0; nextT_ = t + 0.08; playing_ = true; bpm_ = 126; party_ = false;
    out_.cancelScheduledValues(t);
    out_.setTargetAtTime(muted_ ? 0.f : 0.55f, t, 0.03f);
    // filtered intro that opens up over four bars
    filtFreq_.cancelScheduledValues(t);
    filtFreq_.setValueAtTime(280, t);
    filtFreq_.exponentialRampToValueAtTime(18000, t + (60.0 / bpm_) * 16);
    schedulerOn_ = true; schedulerUntil_ = 1e300;
}

void Synth::stopMusic(bool sad) {
    playing_ = false; party_ = false;
    if (sad) powerDown();
    out_.cancelScheduledValues(now_);
    out_.setTargetAtTime(0.f, now_, sad ? 0.25f : 0.06f);
    schedulerUntil_ = now_ + 1.2; // setTimeout(() => { if (!playing) clearInterval(timer); }, 1200)
}

// ---------------------------------------------------------------- rendering

void Synth::render(float* out, int frames, int channels) {
    int done = 0;
    while (done < frames) {
        const int n = std::min(kBlock, frames - done);
        renderBlock(mono_.data(), n);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c) out[(done + i) * channels + c] = mono_[i];
        done += n;
    }
}

void Synth::renderBlock(float* mono, int n) {
    processCommands();
    if (schedulerOn_) {
        if (playing_ || now_ < schedulerUntil_) schedule();
        else schedulerOn_ = false;
    }
    std::fill_n(busPump_.data(), n, 0.f);
    std::fill_n(busDrums_.data(), n, 0.f);
    std::fill_n(busDelay_.data(), n, 0.f);
    std::fill_n(busMaster_.data(), n, 0.f);
    const double dt = 1.0 / sr_, blockEnd = now_ + n * dt;

    for (Voice& v : voices_) {
        if (v.start >= blockEnd) continue;
        float* dest = v.dest == Bus::Pump ? busPump_.data() : v.dest == Bus::Drums ? busDrums_.data() : busMaster_.data();
        float g0 = 0.f;
        bool started = false;
        for (int s0 = 0; s0 < n; s0 += kSub) {
            const int s1 = std::min(n, s0 + kSub);
            const double tA = now_ + s0 * dt, tB = now_ + s1 * dt;
            if (tB <= v.start) continue; // not sounding yet in this slice
            // automation starts at the voice's own start time, not at the slice boundary (no onset click)
            if (!started) { g0 = v.gain.at(std::max(tA, v.start)); started = true; }
            const float g1 = v.gain.at(tB);
            if (v.filtered) v.bq.configure(v.filterType, v.filterFreq.at(tA), v.q, sr_);
            float inc[8];
            const size_t nosc = std::min<size_t>(v.oscs.size(), 8);
            for (size_t k = 0; k < nosc; ++k)
                inc[k] = v.oscs[k].freq.at(tA) * std::pow(2.f, v.oscs[k].detune / 1200.f) / sr_;
            for (int i = s0; i < s1; ++i) {
                const double t = now_ + i * dt;
                if (t < v.start || t >= v.stop) continue;
                float x = 0.f;
                for (size_t k = 0; k < nosc; ++k) {
                    Osc& o = v.oscs[k];
                    const float p = float(o.phase), d = inc[k];
                    switch (o.wave) {
                    case Wave::Sine: x += std::sin(kTwoPi * p); break;
                    case Wave::Triangle: x += p < 0.25f ? 4 * p : p < 0.75f ? 2 - 4 * p : 4 * p - 4; break;
                    case Wave::Sawtooth: x += 2 * p - 1 - blep(p, d); break;
                    case Wave::Square: {
                        const float q = p + 0.5f >= 1.f ? p - 0.5f : p + 0.5f;
                        x += (p < 0.5f ? 1.f : -1.f) + blep(p, d) - blep(q, d);
                        break;
                    }
                    }
                    o.phase += d;
                    if (o.phase >= 1.0) o.phase -= 1.0;
                }
                const std::vector<float>* buf = v.buffer ? v.buffer : (v.ownBuffer.empty() ? nullptr : &v.ownBuffer);
                if (buf) {
                    size_t idx = size_t(v.bufPos);
                    if (idx >= buf->size()) { if (v.loop) { v.bufPos = 0; idx = 0; } else idx = SIZE_MAX; }
                    if (idx != SIZE_MAX) x += (*buf)[idx];
                    v.bufPos += 1.0;
                }
                if (v.filtered) x = v.bq.process(x);
                const float g = g0 + (g1 - g0) * float(i - s0) / float(s1 - s0);
                const float y = x * g;
                dest[i] += y;
                if (v.delaySend > 0.f) busDelay_[i] += y * v.delaySend;
            }
            g0 = g1;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.stop <= blockEnd; }), voices_.end());

    // music master chain: pump -> filt; drums (0.9) -> filt; delay -> dlp(3200) -> fb(0.38) -> delay, dlp -> filt;
    // filt -> out -> compressor -> destination. SFX go straight to the destination.
    const float attack = std::exp(-1.f / (0.003f * sr_)), release = std::exp(-1.f / (0.2f * sr_));
    const size_t L = delayLine_.size();
    for (int s0 = 0; s0 < n; s0 += kSub) {
        const int s1 = std::min(n, s0 + kSub);
        const double tA = now_ + s0 * dt, tB = now_ + s1 * dt;
        const float p0 = pump_.at(tA), p1 = pump_.at(tB);
        const float o0 = out_.at(tA), o1 = out_.at(tB);
        filt_.configure(Biquad::Type::Lowpass, filtFreq_.at(tA), 2.f, sr_);
        dlp_.configure(Biquad::Type::Lowpass, 3200, 1.f, sr_);
        const float delaySamples = std::clamp(delayTime_.at(tA), 0.f, 1.f) * sr_;
        for (int i = s0; i < s1; ++i) {
            const float f = float(i - s0) / float(s1 - s0);
            // delay read (linear interpolation)
            double rp = double(delayWrite_) - delaySamples;
            while (rp < 0) rp += double(L);
            const size_t r0 = size_t(rp) % L, r1 = (r0 + 1) % L;
            const float frac = float(rp - std::floor(rp));
            const float delayed = delayLine_[r0] + (delayLine_[r1] - delayLine_[r0]) * frac;
            const float dl = dlp_.process(delayed);
            delayLine_[delayWrite_] = busDelay_[i] + dl * 0.38f;
            delayWrite_ = (delayWrite_ + 1) % L;

            float x = busPump_[i] * (p0 + (p1 - p0) * f) + busDrums_[i] * 0.9f + dl;
            x = filt_.process(x) * (o0 + (o1 - o0) * f);
            // DynamicsCompressor: threshold -14 dB, knee 30, ratio 4, attack 3 ms, release 200 ms, makeup
            const float red = 20.f * std::log10(float(musicCompressor().gainFor(std::fabs(x))));
            compEnvDb_ = red < compEnvDb_ ? red + (compEnvDb_ - red) * attack : red + (compEnvDb_ - red) * release;
            x *= std::pow(10.f, compEnvDb_ / 20.f) * compMakeup_;

            mono[i] = std::clamp(busMaster_[i] + x, -1.f, 1.f);
        }
    }
    now_ = blockEnd;
}

} // namespace cs::audio
