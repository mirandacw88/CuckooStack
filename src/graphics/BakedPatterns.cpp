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

namespace {
float lin(float srgb) { return std::pow(srgb, 2.2f); }
struct Lin3 { float r, g, b; };
Lin3 lin3(float r, float g, float b) { return {lin(r), lin(g), lin(b)}; }
float fract(float x) { return x - std::floor(x); }

// one design pixel of the 512 x 512 face (lit.frag's former per-pixel P_HAZARD maths, minus the animation)
struct CyberSample { Lin3 base; int kind; float tube, circuit, ring, dot; };
CyberSample cyberSample(float px, float py, float seed) {
    CyberSample s{};
    const float cx = px - 256.f, cy = py - 256.f;
    const float e = std::min(std::min(px, py), std::min(512.f - px, 512.f - py));
    if (e < 36.f) { // armour frame: brushed gunmetal, a bright chamfer at the very edge, bolts in the corners
        const float v = 0.85f + 0.3f * hash12(std::floor(px / 2.f), std::floor(py / 24.f));
        s.base = lin3(0.075f, 0.08f, 0.10f);
        s.base = {s.base.r * v, s.base.g * v, s.base.b * v};
        const float ch = lin(0.35f) * (1.f - smoothstep(0.f, 3.5f, e));
        s.base = {s.base.r + ch, s.base.g + ch, s.base.b + ch};
        s.kind = 0;
        const float bx = std::abs(cx) - (256.f - 18.f), by = std::abs(cy) - (256.f - 18.f), bl = std::sqrt(bx * bx + by * by);
        if (bl < 7.f) { s.base = lin3(0.22f, 0.23f, 0.26f); s.kind = 1; }
        if (bl < 3.f) s.base = lin3(0.03f, 0.03f, 0.03f);
    } else { // dark smoked glass with scanlines, circuit traces, the core, hazard tape
        s.base = lin3(0.028f, 0.03f, 0.045f);
        const float sl = 0.9f + 0.1f * (fract(py / 6.f) >= 0.5f ? 1.f : 0.f);
        s.base = {s.base.r * sl, s.base.g * sl, s.base.b * sl};
        s.kind = 2;
        const float gx = (px - 64.f) / 48.f, gy = (py - 64.f) / 48.f;
        const float cellx = std::floor(gx), celly = std::floor(gy), fx = gx - cellx - 0.5f, fy = gy - celly - 0.5f;
        if (cellx >= 0 && cellx < 8 && celly >= 0 && celly < 8) {
            const float h = hash12(cellx + seed * 37.f, celly + seed * 37.f);
            const float lw = 0.055f;
            const float hl = std::abs(fy) < lw ? 1.f : 0.f, vl = std::abs(fx) < lw ? 1.f : 0.f;
            const float trace = h < 0.3f ? hl : h < 0.55f ? vl : h < 0.75f ? std::max(hl * (fx <= 0 ? 1.f : 0.f), vl * (fy >= 0 ? 1.f : 0.f)) : 0.f;
            const float node = (std::sqrt(fx * fx + fy * fy) < 0.11f && h >= 0.6f) ? 1.f : 0.f;
            s.circuit = std::max(trace * 0.5f, node * 0.9f) / 0.9f;
        }
        const float r = std::sqrt(cx * cx + cy * cy);
        if (r < 66.f) { s.circuit = 0; s.base = lin3(0.015f, 0.016f, 0.025f); }
        s.ring = std::abs(r - 58.f) < 4.f ? 1.f : 0.f;
        s.dot = r < 20.f ? 1.f : 0.f;
        if (py > 400.f && py < 432.f && std::abs(cx) < 150.f) {
            const bool stripe = std::fmod(px + py, 40.f) < 20.f;
            s.base = stripe ? lin3(0.95f, 0.62f, 0.05f) : lin3(0.03f, 0.03f, 0.03f);
            s.kind = 3; s.circuit = 0;
        }
    }
    const float qx = std::abs(cx) - (256.f - 44.f), qy = std::abs(cy) - (256.f - 44.f);
    const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    const float tube = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - 20.f;
    s.tube = std::abs(tube) < 3.f ? 1.f : 0.f;
    return s;
}
uint8_t u8(float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); }
} // namespace

CyberCubeBake bakeCyberCube(int n) {
    const int W = 2 * n;
    CyberCubeBake out;
    out.surface.resize(size_t(W) * W * 4);
    out.glow.resize(size_t(W) * W * 4);
    const float scale = 512.f / float(n);
    for (int v = 0; v < 4; ++v) {
        const float seed = (v + 0.5f) / 4.f; // the shader picks variant floor(seed * 4)
        const int ox = (v % 2) * n, oy = (v / 2) * n;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                float br = 0, bg = 0, bb = 0, tube = 0, circ = 0, ring = 0, dot = 0;
                for (int sy = 0; sy < 2; ++sy)
                    for (int sx = 0; sx < 2; ++sx) { // 2 x 2 samples per texel: anti-aliased edges
                        const CyberSample s = cyberSample((x + (sx + 0.5f) / 2.f) * scale, (y + (sy + 0.5f) / 2.f) * scale, seed);
                        br += s.base.r; bg += s.base.g; bb += s.base.b;
                        tube += s.tube; circ += s.circuit; ring += s.ring; dot += s.dot;
                    }
                const int kind = cyberSample((x + 0.5f) * scale, (y + 0.5f) * scale, seed).kind;
                const size_t i = (size_t(oy + y) * W + size_t(ox + x)) * 4;
                out.surface[i] = u8(std::sqrt(br / 4)); out.surface[i + 1] = u8(std::sqrt(bg / 4)); out.surface[i + 2] = u8(std::sqrt(bb / 4));
                out.surface[i + 3] = u8(kind / 3.f);
                out.glow[i] = u8(tube / 4); out.glow[i + 1] = u8(circ / 4); out.glow[i + 2] = u8(ring / 4); out.glow[i + 3] = u8(dot / 4);
            }
    }
    return out;
}

} // namespace cs
