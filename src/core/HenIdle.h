// The title-screen hen's idle loop: three moves (jog in place, a big stretch, two jumps), each followed by a rest
// where she just breathes. Pure functions of the time spent on the title screen, so the choreography is testable.
#pragma once

#include "Math.h"

#include <algorithm>
#include <cmath>

namespace cs::idle {

enum class Move { Rest, Jog, Stretch, Jumps };

struct Step { Move move; float length; };
// One loop. It opens with a rest, so arriving on the title screen she stands for a moment first.
inline constexpr Step kLoop[] = {
    {Move::Rest, 2.0f}, {Move::Jog, 3.2f},     {Move::Rest, 2.6f},
    {Move::Stretch, 3.0f}, {Move::Rest, 2.6f}, {Move::Jumps, 2.4f},
};
inline constexpr float kRestBlend = 0.3f; // jog and stretch ease in and out over this long

inline float loopLength() { float s = 0; for (const Step& st : kLoop) s += st.length; return s; }

struct Now { Move move; float t; float length; }; // the step playing, and how far into it
inline Now at(float time) {
    float t = std::fmod(std::max(0.f, time), loopLength());
    for (const Step& st : kLoop) {
        if (t < st.length) return {st.move, t, st.length};
        t -= st.length;
    }
    return {Move::Rest, 0, kLoop[0].length};
}

// The hen's pose offsets. Hip and knee angles are about z (+ swings a segment forward). A bird's crouch puts the
// thigh back and the shin forward (hip < 0, knee > 0), so the joint points backward; the foot counter-rotates so the
// toes stay flat.
struct Pose {
    float hip[2] = {0, 0}, knee[2] = {0, 0};
    float wing[2] = {0, 0}; // how far each wing is lifted (rad)
    float lean = 0;         // added to the hen's tilt (+ leans back)
    float bodyY = 0;        // body bob
    float stretch = 0;      // > 0 taller and thinner, < 0 squashed
    float air = 0;          // feet above the ground
    float tail = 0;         // tail lift
};

inline constexpr float kLegSeg = 0.11f; // thigh and shin length (hen space)

// How far the hips sit below standing height for these leg angles: the lower foot touches the ground.
inline float hipDrop(const Pose& p) {
    float reach = 0;
    for (int k = 0; k < 2; ++k)
        reach = std::max(reach, kLegSeg * (std::cos(p.hip[k]) + std::cos(p.hip[k] + p.knee[k])));
    return 2 * kLegSeg - reach;
}

namespace detail {
inline float smooth(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); }
// 0 → 1 over [a, a + in], held, 1 → 0 over [b - out, b]
inline float bump(float x, float a, float in, float b, float out) { return smooth(a, a + in, x) * (1 - smooth(b - out, b, x)); }
} // namespace detail

inline Pose jog(float t, float len) {
    using namespace detail;
    Pose p;
    const float w = bump(t, 0, kRestBlend, len, kRestBlend);
    const float ph = t * 15.f; // about 2.4 strides a second
    for (int k = 0; k < 2; ++k) {
        const float s = std::sin(ph + k * kPi);
        p.hip[k] = s * 0.5f * w;
        p.knee[k] = -std::max(0.f, s) * 1.1f * w; // the forward leg folds its foot up under
        p.wing[k] = (0.12f + std::abs(std::sin(ph)) * 0.18f) * w;
    }
    p.bodyY = std::abs(std::sin(ph)) * 0.05f * w;
    p.lean = -0.08f * w;
    p.tail = std::sin(ph * 2) * 0.1f * w;
    return p;
}

// Up on her toes with both wings thrown high and the body drawn tall, then the bird stretch: one wing fanned back
// along with the same-side leg, leaning forward.
inline Pose stretch(float t, float len) {
    using namespace detail;
    Pose p;
    const float up = bump(t, 0.1f, 0.5f, 1.45f, 0.35f);
    const float side = bump(t, 1.5f, 0.45f, len, 0.45f);
    const float shiver = std::sin(t * 30.f) * 0.04f * up; // a tremble at full stretch
    p.wing[0] = up * (1.25f + shiver) + side * 0.9f;
    p.wing[1] = up * (1.25f - shiver) + side * 0.15f;
    p.stretch = up * 0.09f - side * 0.02f;
    p.air = up * 0.04f; // on tiptoes
    p.lean = up * 0.12f - side * 0.18f;
    p.tail = up * 0.35f + side * 0.15f;
    p.hip[0] = -side * 0.75f; // leg 0 reaches back with its wing
    p.knee[0] = side * 0.2f;
    p.hip[1] = -side * 0.12f;
    p.knee[1] = side * 0.25f;
    return p;
}

// Two jumps: a deep knee bend to load each one, the legs driving straight with the feet still planted, a flight with
// the wings flapping and the legs tucked, and a soft landing that sinks back down into the knees.
inline Pose jumps(float t, float len) {
    using namespace detail;
    Pose p;
    constexpr float kLoad = 0.32f, kPush = 0.14f, kFly = 0.44f, kLand = 0.24f, kJump = kLoad + kPush + kFly + kLand, kHeight = 0.42f;
    float bend = 0, air = 0, fly = -1, wing = 0;
    for (int j = 0; j < 2; ++j) {
        const float u = t - j * kJump;
        if (u < 0 || u >= kJump) continue;
        if (u < kLoad) { bend = smooth(0, kLoad, u); wing = 0.35f * bend; }               // load: sink into the knees
        else if (u < kLoad + kPush) { bend = 1 - smooth(0, kPush, u - kLoad); wing = 0.35f; } // push off: feet down
        else if (u < kLoad + kPush + kFly) { fly = (u - kLoad - kPush) / kFly; air = 4 * fly * (1 - fly) * kHeight; }
        else {                                                                             // land and recover
            const float l = (u - kLoad - kPush - kFly) / kLand;
            bend = std::sin(l * kPi) * 0.75f;
            wing = 0.35f * (1 - smooth(0, 1, l));
        }
    }
    (void)len;
    const float crouch = bend * 1.05f; // thigh angle back; the knee bends twice as far
    for (int k = 0; k < 2; ++k) {
        p.hip[k] = -crouch;
        p.knee[k] = 2 * crouch;
        p.wing[k] = wing;
        if (fly >= 0) { // in the air: the legs tuck up, then reach for the ground; the wings flap
            const float tuck = std::sin(fly * kPi);
            p.hip[k] = -0.35f * tuck;
            p.knee[k] = 0.9f * tuck;
            p.wing[k] = 0.35f + tuck * (0.25f + std::sin(fly * kPi * 4) * 0.4f);
        }
    }
    p.air = air;
    p.stretch = fly >= 0 ? 0.06f * std::sin(fly * kPi) : -0.1f * bend;
    p.lean = -0.22f * bend;
    p.tail = fly >= 0 ? 0.3f * std::sin(fly * kPi) : -0.1f * bend;
    return p;
}

inline Pose pose(float time) {
    const Now n = at(time);
    switch (n.move) {
    case Move::Jog: return jog(n.t, n.length);
    case Move::Stretch: return stretch(n.t, n.length);
    case Move::Jumps: return jumps(n.t, n.length);
    case Move::Rest: break;
    }
    return {};
}

} // namespace cs::idle
