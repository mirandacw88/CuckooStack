// Math helpers shared by the game layer. Mirrors the small JS helpers in cuckoo-stack.html
// (lerp, hash, mulberry32, seedFor, THREE.Color hex parsing) so the port stays line-for-line comparable.
#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE   // Vulkan clip space: z in [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cs {

constexpr float kPi = 3.14159265358979323846f;

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }
inline float smoothstep01(float t) { t = clampf(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }
// JS Math.round: halves round toward +infinity
inline int jsRound(double v) { return static_cast<int>(std::floor(v + 0.5)); }

// const hash = n => { const s = Math.sin(n * 127.1 + 11.7) * 43758.5453; return s - Math.floor(s); };
inline float hash(double n) {
    const double s = std::sin(n * 127.1 + 11.7) * 43758.5453;
    return static_cast<float>(s - std::floor(s));
}

// Math.imul semantics: 32-bit wrapping multiply
inline uint32_t imul(uint32_t a, uint32_t b) { return a * b; }

// Same daily seed as the web build, so a given date produces the identical course on every platform.
inline uint32_t seedFor(std::string_view str) {
    uint32_t h = 1779033703u ^ static_cast<uint32_t>(str.size());
    for (unsigned char c : str) {
        h = imul(h ^ c, 3432918353u);
        h = (h << 13) | (h >> 19);
    }
    h = imul(h ^ (h >> 16), 2246822507u);
    h = imul(h ^ (h >> 13), 3266489909u);
    return h ^ (h >> 16);
}

// Deterministic level RNG (mulberry32), bit-exact with the JS version.
class Mulberry32 {
public:
    explicit Mulberry32(uint32_t seed = 0) : a_(seed) {}
    double next() {
        a_ += 0x6D2B79F5u;
        uint32_t t = imul(a_ ^ (a_ >> 15), 1u | a_);
        t = (t + imul(t ^ (t >> 7), 61u | t)) ^ t;
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }
private:
    uint32_t a_;
};

// Non-deterministic cosmetic RNG (replaces Math.random for particles, scenery, jitter).
class FastRandom {
public:
    explicit FastRandom(uint64_t seed = 0x9E3779B97F4A7C15ull) : s_(seed ? seed : 1) {}
    float next01() {
        s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17;
        return static_cast<float>((s_ >> 40) & 0xFFFFFF) / 16777216.f;
    }
    float range(float a, float b) { return a + next01() * (b - a); }
    int index(int n) { return std::min(n - 1, static_cast<int>(next01() * n)); }
private:
    uint64_t s_;
};

// THREE.Color('#rrggbb') stores linear values (ColorManagement converts sRGB hex to linear).
inline float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
constexpr uint32_t hexDigit(char c) {
    return c >= '0' && c <= '9' ? uint32_t(c - '0') : c >= 'a' && c <= 'f' ? uint32_t(c - 'a' + 10) : uint32_t(c - 'A' + 10);
}
inline glm::vec3 hexColor(std::string_view hex) {
    if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);
    auto byte = [&](size_t i) { return float(hexDigit(hex[i]) * 16 + hexDigit(hex[i + 1])) / 255.f; };
    return {srgbToLinear(byte(0)), srgbToLinear(byte(2)), srgbToLinear(byte(4))};
}
// fully saturated hue wheel colour, h in turns (wraps)
inline glm::vec3 hueColor(float h) {
    h -= std::floor(h);
    return glm::clamp(glm::abs(glm::fract(glm::vec3(h) + glm::vec3(0.f, 2.f / 3.f, 1.f / 3.f)) * 6.f - 3.f) - 1.f, 0.f, 1.f);
}

// CSS colour as authored (sRGB, no linearisation): for HUD/DOM-equivalent drawing after tone mapping.
inline glm::vec3 srgbColor(std::string_view hex) {
    if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);
    auto byte = [&](size_t i) { return float(hexDigit(hex[i]) * 16 + hexDigit(hex[i + 1])) / 255.f; };
    return {byte(0), byte(2), byte(4)};
}
// const glow = (hex, k) => new THREE.Color(hex).multiplyScalar(k);
inline glm::vec3 glow(std::string_view hex, float k) { return hexColor(hex) * k; }

// THREE.Object3D compose: T * R(euler XYZ) * S
inline glm::mat4 compose(const glm::vec3& p, const glm::vec3& eulerXYZ, const glm::vec3& s) {
    glm::mat4 m = glm::translate(glm::mat4(1.f), p);
    if (eulerXYZ.x != 0.f) m = glm::rotate(m, eulerXYZ.x, {1, 0, 0});
    if (eulerXYZ.y != 0.f) m = glm::rotate(m, eulerXYZ.y, {0, 1, 0});
    if (eulerXYZ.z != 0.f) m = glm::rotate(m, eulerXYZ.z, {0, 0, 1});
    return glm::scale(m, s);
}
inline glm::mat4 compose(const glm::vec3& p, const glm::vec3& s) {
    return glm::scale(glm::translate(glm::mat4(1.f), p), s);
}

} // namespace cs
