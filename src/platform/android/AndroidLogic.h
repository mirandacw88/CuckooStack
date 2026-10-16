// The Android entry point's pure decisions (AndroidMain.cpp), kept apart from the NDK calls so they're tested on
// desktop (AndroidLogic_test.cpp): screen density, safe-area insets in points, thermal status -> quality level.
#pragma once

#include "../../core/Touch.h"
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

// A GameActivity motion event -> what the game is told: a new finger (the first, or another one: POINTER_DOWN, at its
// own index) presses; the first finger moving drags; it lifting (or the gesture cancelled) releases; another finger
// lifting means nothing. Action codes as in <android/input.h>: ACTION_MASK 0xff, POINTER_INDEX 0xff00 >> 8.
struct MotionTouch { bool any; TouchPhase phase; int pointer; };
inline MotionTouch touchFromMotion(int action, int pointerCount) {
    constexpr int kDown = 0, kUp = 1, kMove = 2, kCancel = 3, kPointerDown = 5;
    const int a = action & 0xff, index = (action & 0xff00) >> 8;
    if (pointerCount <= 0) return {false, TouchPhase::Began, 0};
    if (a == kDown) return {true, TouchPhase::Began, 0};
    if (a == kPointerDown) return {index < pointerCount, TouchPhase::Began, index};
    if (a == kMove) return {true, TouchPhase::Moved, 0};
    if (a == kUp) return {true, TouchPhase::Ended, 0};
    if (a == kCancel) return {true, TouchPhase::Cancelled, 0};
    return {false, TouchPhase::Began, 0}; // (POINTER_UP, hover, scroll ...)
}

} // namespace cs::android
