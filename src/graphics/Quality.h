// Battery / heat policy shared by the mobile platforms: internal render resolution and frame-rate cap,
// stepped down as the OS reports thermal pressure.
#pragma once

#include <algorithm>

namespace cs::quality {

enum class Thermal { Nominal, Fair, Serious, Critical };

constexpr float PX_PER_POINT = 2.f;   // scene pixels per HUD point: plenty under bloom + fog (3x phones -> 0.67)
constexpr float MIN_SCALE = 0.5f;
constexpr int FPS = 60;               // ProMotion / 120 Hz displays would double the GPU work for little gain
constexpr int FPS_CRITICAL = 30;

inline float thermalFactor(Thermal t) {
    switch (t) {
    case Thermal::Nominal: return 1.f;
    case Thermal::Fair: return 0.9f;
    case Thermal::Serious: return 0.75f;
    case Thermal::Critical: return 0.6f;
    }
    return 1.f;
}

// pixelsPerPoint: the screen's density (iOS contentScaleFactor, Android DisplayMetrics.density)
inline float renderScale(float pixelsPerPoint, Thermal t) {
    const float base = std::clamp(PX_PER_POINT / std::max(1.f, pixelsPerPoint), MIN_SCALE, 1.f);
    return std::max(MIN_SCALE * 0.8f, base * thermalFactor(t));
}

inline int targetFps(Thermal t) { return t == Thermal::Critical ? FPS_CRITICAL : FPS; }

} // namespace cs::quality
