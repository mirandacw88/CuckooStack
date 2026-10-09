// HUD look shared by GameHud.cpp and GameUi.cpp: the web build's CSS palette, text styles and easing curves.
// Units are CSS px (= logical points).
#pragma once

#include "Font.h"
#include "Materials.h"

#include <cmath>

namespace cs::hud {

inline glm::vec3 css(std::string_view hex) { return srgbColor(hex); }
// :root custom properties, plus the meta-game accents
inline const glm::vec3 kNeon = css("#29e7ff"), kHot = css("#ff2bd6"), kVolt = css("#f4ff5a"), kInk = css("#07060f"),
                       kText = css("#eafaff"), kGreen = css("#2bff9a"), kGold = css("#ffd23a"), kPanel = css("#0e0a22"),
                       kDeep = css("#0a0818"), kViolet = css("#9a5bff"), kRed = css("#ff2b4a"), kMuted = css("#9aa3b8");

inline TextShadow hard(float dx, glm::vec3 c, float a = 1.f) { return {{dx, 0.f}, 0.f, c, a}; }
inline TextShadow blur(float r, glm::vec3 c, float a) { return {{0.f, 0.f}, r, c, a}; }

// cubic-bezier(.2,.8,.3,1) is a strong ease-out; a cubic ease-out matches it within a pixel or two
inline float easeOut(float t) { t = clampf(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
inline float easeOutBack(float t, float k = 1.9f) { t = clampf(t, 0.f, 1.f); const float c = k + 1; return 1 + c * std::pow(t - 1, 3.f) + k * std::pow(t - 1, 2.f); }
inline float seg(float t, float a, float b) { return clampf((t - a) / (b - a), 0.f, 1.f); } // 0..1 progress of t through [a, b]

// .overlay>* { animation: up .55s cubic-bezier(.2,.8,.3,1) both }  (opacity 0 -> 1, translateY 18px -> 0)
struct Up { float alpha, dy; };
inline Up up(float t, float delay) { const float e = easeOut((t - delay) / 0.55f); return {e, 18.f * (1.f - e)}; }

inline TextStyle style(FontId font, float size, glm::vec3 color, float letterSpacing = 0.f, bool upper = false, float lineHeight = 0.f) {
    TextStyle s;
    s.font = font; s.size = size; s.color = color; s.letterSpacing = letterSpacing; s.uppercase = upper; s.lineHeight = lineHeight;
    return s;
}

// "1,250"
inline std::string grouped(int n) {
    std::string s = std::to_string(n < 0 ? -n : n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return n < 0 ? "-" + s : s;
}

} // namespace cs::hud
