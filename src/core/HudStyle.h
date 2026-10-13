// HUD look shared by GameHud.cpp and GameUi.cpp: the web build's CSS palette, text styles and easing curves.
// Units are CSS px (= logical points).
#pragma once

#include "Font.h"
#include "Materials.h"

#include <algorithm>
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

// The logo's OVERDRIVE line, drawn in two passes. `over`: dark letters outlined in kNeon (the blocks' neon blue) by a
// ring of hard shadows. `under`, drawn first and shifted by underDx: the "Cuckoo Stack" line's effect, a pink copy
// left and a blue copy right with a pink glow. (A glow is painted with the letters themselves, on top of the hard
// shadows, so in one pass it would tint the outline.) Half the logo's size.
struct OverdriveLook { TextStyle under, over; float underDx; };
inline OverdriveLook overdriveLook(float logoSize) {
    OverdriveLook l;
    l.over = style(FontId::Display, logoSize * 0.5f, kInk, 0.16f, true, 1.f);
    const float r = std::max(1.5f, l.over.size * 0.065f);
    for (int i = 0; i < 16; ++i) {
        const float a = float(i) * 2.f * kPi / 16.f;
        l.over.shadows.push_back({{r * std::cos(a), r * std::sin(a)}, 0.f, kNeon, 1.f});
    }
    const float split = r + logoSize / 24.f; // the logo's 3 px split (at 72 px), out past the outline
    l.under = l.over;
    l.under.color = kHot;
    l.under.shadows = {hard(2 * split, kNeon), blur(logoSize * 0.47f, kHot, 0.45f)}; // the logo's blur(34) at 72 px
    l.underDx = -split;
    return l;
}

// The "Level Complete" banner over the finish-line fireworks, `t` seconds after crossing the line (the celebration
// lasts `total`): it glitches in with a pop, jolts every 1.5 s like the logo, and fades just before the results.
struct BannerAnim { float alpha, scale, dx; };
inline BannerAnim completeBanner(float t, float total) {
    BannerAnim b{0.f, 1.f, 0.f};
    if (t < 0.06f) return b;
    b.alpha = 1.f - seg(t, total - 0.45f, total);
    b.scale = 0.6f + 0.4f * easeOutBack(seg(t, 0.06f, 0.5f), 2.4f);
    if (t < 0.5f) {
        const float g = (t - 0.06f) / 0.44f;
        b.dx = g < 0.25f ? -12.f : g < 0.45f ? 9.f : g < 0.65f ? -5.f : g < 0.85f ? 2.f : 0.f;
    } else {
        const float c = std::fmod(t - 0.5f, 1.5f) / 1.5f;
        b.dx = c >= 0.9f && c < 0.93f ? -5.f : c >= 0.93f && c < 0.96f ? 4.f : c >= 0.96f ? -2.f : 0.f;
    }
    return b;
}

// Fit one line of text into `room` px: tighter letter spacing first, then a smaller size (the boost tiles' names)
inline TextStyle fitWidth(const TextLayout& tl, std::string_view text, TextStyle s, float room, float tightSpacing = 0.05f) {
    if (tl.measure(text, s) > room) s.letterSpacing = std::min(s.letterSpacing, tightSpacing);
    if (const float w = tl.measure(text, s); w > room) s.size *= room / w;
    return s;
}

// "1,250"
inline std::string grouped(int n) {
    std::string s = std::to_string(n < 0 ? -n : n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return n < 0 ? "-" + s : s;
}

} // namespace cs::hud
