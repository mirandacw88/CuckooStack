#include "BakedPatterns.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {
// GLSL: fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453), in float like the GPU
float hash12(float x, float y) {
    const float s = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return s - std::floor(s);
}
float smoothstep(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
} // namespace

std::vector<uint8_t> bakePuddleMask(int n) {
    struct Puddle { float cx, cy, rx, ry; };
    Puddle p[14];
    for (int i = 0; i < 14; ++i) {
        const float r = 0.06f + (0.21f - 0.06f) * hash12(float(i), 4.3f);
        p[i] = {hash12(float(i), 1.7f), hash12(float(i), 9.1f), r, r * (0.35f + (0.7f - 0.35f) * hash12(float(i), 7.7f))};
    }
    std::vector<uint8_t> out(size_t(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float fx = (x + 0.5f) / n, fy = (y + 0.5f) / n;
            float puddle = 0.f;
            for (const Puddle& q : p) {
                float dx = fx - q.cx, dy = fy - q.cy;
                dx -= std::round(dx); dy -= std::round(dy); // wrap across tile edges
                const float d = std::sqrt((dx / q.rx) * (dx / q.rx) + (dy / q.ry) * (dy / q.ry));
                puddle = std::max(puddle, 1.f - smoothstep(0.7f, 1.f, d));
            }
            out[size_t(y) * n + x] = uint8_t(std::lround(puddle * 255.f));
        }
    return out;
}

} // namespace cs
