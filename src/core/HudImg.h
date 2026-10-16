// Artist PNGs packed into the HUD atlas by scripts/hud/make_hud_icons.py's paste step (the Daily Drop's frame, tiles,
// buttons): where each sits in its cell(s), and how it's cut to be drawn stretched. Pure maths, tested on its own
// (HudImg_test.cpp, which also checks the sizes below against the PNGs and the atlas pixels against them).
#pragma once

#include "HudIcons.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>

namespace cs {

// an image's pixel size and the cell padding it was packed with (make_hud_icons.py ICONS "paste": (name, pad, glow))
struct HudImg { Icon icon; float w, h, pad; };

// the Daily Drop art (scripts/hud/extract_drop_sprites.py -> assets/hud/shop/drop_*.png)
inline constexpr HudImg kDropFrame{Icon::DropFrame, 710, 178, 6};
inline constexpr HudImg kDropTileGreen{Icon::DropTileGreen, 233, 257, 4}; // (its glow reaches 12% past the others' edge)
inline constexpr HudImg kDropTilePurple{Icon::DropTilePurple, 207, 229, 4};
inline constexpr HudImg kDropTileBlue{Icon::DropTileBlue, 207, 229, 4};
inline constexpr HudImg kDropTilePink{Icon::DropTilePink, 207, 232, 4};
inline constexpr HudImg kDropClaim{Icon::DropClaim, 285, 123, 4};
inline constexpr HudImg kDropClaim2{Icon::DropClaim2, 319, 121, 4};

constexpr float kAtlasCellPx = 192.f;

// a rect of the image, in its pixels (x0, y0, x1, y1) -> atlas UV: the packer scaled it to fit its cell(s) inside the
// padding, keeping its aspect, and centred it
inline glm::vec4 hudImageUV(const HudImg& img, glm::vec4 px) {
    const int i = int(img.icon), span = iconSpan(img.icon), rows = iconRowSpan(img.icon);
    const float cw = kAtlasCellPx * span, ch = kAtlasCellPx * rows;
    const float s = std::min((cw - 2 * img.pad) / img.w, (ch - 2 * img.pad) / img.h);
    const float ox = (cw - img.w * s) / 2, oy = (ch - img.h * s) / 2;
    const float u0 = float(i % kIconCols) / kIconCols, v0 = float(i / kIconCols) / kIconRows;
    const float du = 1.f / (kIconCols * kAtlasCellPx), dv = 1.f / (kIconRows * kAtlasCellPx);
    return {u0 + (ox + px.x * s) * du, v0 + (oy + px.y * s) * dv, u0 + (ox + px.z * s) * du, v0 + (oy + px.w * s) * dv};
}

// one piece of a sliced image: its atlas UV rect, and its centre and size on screen
struct HudQuad { glm::vec4 uv; glm::vec2 c, size; };

// stretched sideways through its middle: the end caps keep their shape (each as wide as half the image's height, or
// `leftCap` image pixels on the left, when something there mustn't stretch); up to 3 pieces (fewer when too narrow)
inline int hudImageThreeSlice(const HudImg& img, glm::vec2 c, glm::vec2 size, float leftCap, std::array<HudQuad, 3>& out) {
    const float k = size.y / img.h; // image px -> screen, from the height
    const float rSrc = img.h / 2, lSrc = leftCap > 0 ? leftCap : rSrc;
    const float l = std::min(lSrc * k, size.x / 2), r = std::min(rSrc * k, size.x / 2);
    const float x0 = c.x - size.x / 2, x1 = c.x + size.x / 2;
    int n = 0;
    out[n++] = {hudImageUV(img, {0, 0, lSrc, img.h}), {x0 + l / 2, c.y}, {l, size.y}};
    if (size.x > l + r + 0.01f) out[n++] = {hudImageUV(img, {lSrc, 0, img.w - rSrc, img.h}), {(x0 + l + x1 - r) / 2, c.y}, {size.x - l - r, size.y}};
    out[n++] = {hudImageUV(img, {img.w - rSrc, 0, img.w, img.h}), {x1 - r / 2, c.y}, {r, size.y}};
    return n;
}

// 9-sliced: corners `src` image pixels big drawn `dst` big on screen (at most half the size), edges and middle stretched
inline int hudImageNineSlice(const HudImg& img, glm::vec2 c, glm::vec2 size, glm::vec2 src, glm::vec2 dst, std::array<HudQuad, 9>& out) {
    const float us[4] = {0, src.x, img.w - src.x, img.w}, vs[4] = {0, src.y, img.h - src.y, img.h};
    dst = glm::min(dst, size / 2.f);
    const float xs[4] = {c.x - size.x / 2, c.x - size.x / 2 + dst.x, c.x + size.x / 2 - dst.x, c.x + size.x / 2};
    const float ys[4] = {c.y - size.y / 2, c.y - size.y / 2 + dst.y, c.y + size.y / 2 - dst.y, c.y + size.y / 2};
    int n = 0;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            const float w = xs[x + 1] - xs[x], h = ys[y + 1] - ys[y];
            if (w <= 0.01f || h <= 0.01f) continue;
            out[n++] = {hudImageUV(img, {us[x], vs[y], us[x + 1], vs[y + 1]}), {(xs[x] + xs[x + 1]) / 2, (ys[y] + ys[y + 1]) / 2}, {w, h}};
        }
    return n;
}

} // namespace cs
