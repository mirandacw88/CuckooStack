// Web Audio AudioParam automation (setValueAtTime, linear/exponential ramps, setTargetAtTime,
// cancelScheduledValues), evaluated against the synth's sample clock. Single-threaded: owned by the audio thread.
#pragma once

#include <cmath>
#include <vector>

namespace cs::audio {

class Param {
public:
    explicit Param(float v = 0.f) : anchorV_(v) {}

    void setValueAtTime(float v, double t) { insert({Ev::Set, t, v, 0.f}); }
    void linearRampToValueAtTime(float v, double t) { insert({Ev::Linear, t, v, 0.f}); }
    void exponentialRampToValueAtTime(float v, double t) { insert({Ev::Exp, t, v, 0.f}); }
    void setTargetAtTime(float target, double t, float tau) { insert({Ev::Target, t, target, tau}); }
    void cancelScheduledValues(double t) {
        size_t n = 0;
        for (const Ev& e : ev_) if (e.t < t) ev_[n++] = e;
        ev_.resize(n);
    }
    void setValue(float v) { ev_.clear(); target_ = false; anchorV_ = v; }

    // Value at time t. Queries must be (mostly) monotonic, like the audio clock.
    float at(double t) {
        while (!ev_.empty()) {
            const Ev& e = ev_.front();
            if (e.type == Ev::Set || e.type == Ev::Target) {
                if (t < e.t) break;
                const float start = current(e.t);
                if (e.type == Ev::Set) { anchorT_ = e.t; anchorV_ = e.v; target_ = false; }
                else { target_ = true; targetT_ = e.t; targetStart_ = start; targetV_ = e.v; tau_ = e.tau; anchorT_ = e.t; anchorV_ = start; }
                ev_.erase(ev_.begin());
                continue;
            }
            // ramps run from the previous event (time, value) to this one
            if (t < e.t) {
                const double t0 = anchorT_;
                const float v0 = target_ ? current(t0) : anchorV_;
                const double span = e.t - t0;
                const double k = span > 0 ? std::fmin(1.0, std::fmax(0.0, (t - t0) / span)) : 1.0;
                if (e.type == Ev::Linear) return v0 + (e.v - v0) * float(k);
                if (v0 == 0.f || (v0 > 0.f) != (e.v > 0.f)) return v0; // spec: no exponential ramp through zero
                return v0 * std::pow(e.v / v0, float(k));
            }
            anchorT_ = e.t; anchorV_ = e.v; target_ = false;
            ev_.erase(ev_.begin());
        }
        return current(t);
    }

private:
    struct Ev { enum Type { Set, Linear, Exp, Target } type; double t; float v; float tau; };
    void insert(const Ev& e) {
        auto it = ev_.begin();
        while (it != ev_.end() && it->t <= e.t) ++it; // stable: same-time events keep call order
        ev_.insert(it, e);
    }
    float current(double t) const {
        if (!target_) return anchorV_;
        const double dt = std::fmax(0.0, t - targetT_);
        return targetV_ + (targetStart_ - targetV_) * float(std::exp(-dt / std::fmax(1e-6, double(tau_))));
    }

    std::vector<Ev> ev_;
    double anchorT_ = 0;
    float anchorV_;
    bool target_ = false;
    double targetT_ = 0;
    float targetStart_ = 0, targetV_ = 0, tau_ = 1;
};

// BiquadFilterNode with the Web Audio spec's coefficient formulas (lowpass/highpass Q is in dB).
class Biquad {
public:
    enum class Type { Lowpass, Highpass, Bandpass };
    void configure(Type type, float freq, float q, float sampleRate) {
        if (freq == lastF_ && q == lastQ_ && type == type_) return;
        type_ = type; lastF_ = freq; lastQ_ = q;
        const float nyquist = sampleRate * 0.5f;
        const float f = std::fmin(std::fmax(freq, 1.f), nyquist * 0.999f);
        const float w0 = 2.f * 3.14159265f * f / sampleRate, c = std::cos(w0), s = std::sin(w0);
        float b0, b1, b2, a0, a1, a2;
        if (type == Type::Bandpass) {
            const float alpha = s / (2.f * std::fmax(q, 1e-4f));
            b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        } else {
            const float alpha = s / (2.f * std::pow(10.f, q / 20.f));
            const float k = type == Type::Lowpass ? (1 - c) : (1 + c);
            b0 = k / 2; b1 = type == Type::Lowpass ? k : -k; b2 = k / 2; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        }
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }
    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x; y2_ = y1_; y1_ = y;
        return y;
    }

private:
    Type type_ = Type::Lowpass;
    float lastF_ = -1, lastQ_ = -1;
    float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

} // namespace cs::audio
