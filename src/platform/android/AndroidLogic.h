// The Android entry point's pure decisions (AndroidMain.cpp), kept apart from the NDK calls so they're tested on
// desktop (AndroidLogic_test.cpp): screen density, safe-area insets in points, thermal status -> quality level.
#pragma once

#include "../../graphics/Quality.h"

#include <algorithm>

namespace cs::android {

// AConfiguration density (dpi) -> points per pixel scale (160 dpi = 1 point per pixel); unknown -> 1
inline float densityFromDpi(int dpi) { return dpi > 0 ? float(dpi) / 160.f : 1.f; }

// the larger of the system bars and the display cutout on each edge, in points
struct Insets { float top, bottom; };
inline Insets insetsInPoints(int barsTop, int barsBottom, int cutoutTop, int cutoutBottom, float density) {
    const float d = density > 0.f ? density : 1.f;
    return {float(std::max(barsTop, cutoutTop)) / d, float(std::max(barsBottom, cutoutBottom)) / d};
}

// PowerManager thermal status (0 none, 1 light, 2 moderate, 3 severe, 4+ critical / emergency / shutdown)
inline quality::Thermal thermalFromStatus(int status) {
    if (status >= 4) return quality::Thermal::Critical;
    if (status == 3) return quality::Thermal::Serious;
    if (status == 2) return quality::Thermal::Fair;
    return quality::Thermal::Nominal;
}

} // namespace cs::android
