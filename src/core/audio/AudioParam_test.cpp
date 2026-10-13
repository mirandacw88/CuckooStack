// Web Audio automation (AudioParam.h): setValueAtTime, linear and exponential ramps, setTargetAtTime, cancelling, and
// the BiquadFilterNode, evaluated the way the Web Audio spec defines them (the synth ports the web build's sounds 1:1).
#include "TestUtil.h"
#include "core/audio/AudioParam.h"

#include <cmath>
#include <vector>

using namespace cs::audio;
using namespace test;

namespace {

bool near(double a, double b, double eps = 1e-4) { return std::abs(a - b) <= eps; }

void automation() {
    Param p(0.5f);
    check(p.at(0) == 0.5f && p.at(10) == 0.5f, "a constant until something is scheduled");
    Param s(0);
    s.setValueAtTime(1, 1.0);
    check(s.at(0.5) == 0 && s.at(1.0) == 1 && s.at(2) == 1, "setValueAtTime: jumps at its time");
    Param lin(0);
    lin.setValueAtTime(0, 0);
    lin.linearRampToValueAtTime(10, 2);
    check(near(lin.at(0.5), 2.5) && near(lin.at(1), 5) && near(lin.at(2), 10) && near(lin.at(5), 10), "linear ramp: from the previous event, straight line");
    Param ex(1);
    ex.setValueAtTime(1, 0);
    ex.exponentialRampToValueAtTime(100, 2);
    check(near(ex.at(1), 10, 1e-3) && near(ex.at(2), 100, 1e-3), "exponential ramp: geometric (1 -> 10 -> 100)");
    Param zero(0);
    zero.setValueAtTime(0, 0);
    zero.exponentialRampToValueAtTime(5, 1);
    check(zero.at(0.5) == 0, "exponential ramp from 0: holds (the spec forbids ramping through zero)");
    Param flip(2);
    flip.setValueAtTime(2, 0);
    flip.exponentialRampToValueAtTime(-2, 1);
    check(flip.at(0.5) == 2, "exponential ramp across a sign change: holds");
    Param t(1);
    t.setTargetAtTime(0, 1.0, 0.5f);
    check(t.at(0.5) == 1 && near(t.at(1.5), std::exp(-1.0), 1e-4) && near(t.at(3.0), std::exp(-4.0), 1e-4), "setTargetAtTime: exponential approach, tau 0.5 s");
    Param c(0);
    c.setValueAtTime(0, 0);
    c.linearRampToValueAtTime(1, 1);
    c.setValueAtTime(5, 3);
    c.cancelScheduledValues(2);
    check(near(c.at(0.5), 0.5) && near(c.at(4), 1), "cancelScheduledValues: events from that time on are dropped");
    Param v(0);
    v.linearRampToValueAtTime(1, 1);
    v.setValue(7);
    check(v.at(0.5) == 7 && v.at(9) == 7, "setValue: drops everything, a new constant");
    Param order(0);
    order.setValueAtTime(1, 1);
    order.setValueAtTime(2, 1);
    check(order.at(1) == 2, "same-time events apply in call order");
}

void filters() {
    const float sr = 48000;
    auto gainAt = [&](Biquad::Type type, float cutoff, float freq) {
        Biquad b;
        b.configure(type, cutoff, type == Biquad::Type::Bandpass ? 1.f : 0.f, sr);
        double peak = 0;
        for (int i = 0; i < 9600; ++i) {
            const float y = b.process(std::sin(2.0 * 3.14159265358979 * freq * i / sr));
            if (i > 4800) peak = std::max(peak, double(std::abs(y)));
        }
        return peak;
    };
    check(gainAt(Biquad::Type::Lowpass, 1000, 100) > 0.9 && gainAt(Biquad::Type::Lowpass, 1000, 10000) < 0.05, "lowpass: lows pass, highs cut");
    check(gainAt(Biquad::Type::Highpass, 1000, 10000) > 0.9 && gainAt(Biquad::Type::Highpass, 1000, 100) < 0.05, "highpass: highs pass, lows cut");
    check(gainAt(Biquad::Type::Bandpass, 2000, 2000) > 0.9 && gainAt(Biquad::Type::Bandpass, 2000, 100) < 0.1, "bandpass: around its centre only");
    check(gainAt(Biquad::Type::Lowpass, 1e9f, 1000) > 0.9 && std::isfinite(gainAt(Biquad::Type::Lowpass, 0, 1000)), "out-of-range cutoffs are clamped (stable, finite)");
}

} // namespace

int main() {
    automation();
    filters();
    return finish("AudioParam");
}
