#include "EndCard.h"
#include "HudIcons.h"
#include "HudStyle.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

struct Canvas {
    int w, h;
    std::vector<float> px; // linear-ish straight RGB in 0..1 (the HUD's colours are already display values)
    Canvas(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_ * 3, 0.f) {}
    void over(int x, int y, glm::vec3 c, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.f) return;
        float* p = &px[(size_t(y) * w + x) * 3];
        for (int i = 0; i < 3; ++i) p[i] = p[i] * (1.f - a) + c[i] * a;
    }
};

float sdf(const FontAtlas& f, float u, float v) { // bilinear
    const float x = u * f.width() - 0.5f, y = v * f.height() - 0.5f;
    const int x0 = std::clamp(int(std::floor(x)), 0, f.width() - 1), y0 = std::clamp(int(std::floor(y)), 0, f.height() - 1);
    const int x1 = std::min(x0 + 1, f.width() - 1), y1 = std::min(y0 + 1, f.height() - 1);
    const float fx = std::clamp(x - std::floor(x), 0.f, 1.f), fy = std::clamp(y - std::floor(y), 0.f, 1.f);
    const auto& p = f.pixels();
    const float a = p[size_t(y0) * f.width() + x0], b = p[size_t(y0) * f.width() + x1];
    const float c = p[size_t(y1) * f.width() + x0], d = p[size_t(y1) * f.width() + x1];
    return ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy) / 255.f;
}

// one glyph quad from TextLayout, shaded like text.frag
void drawGlyph(Canvas& cv, const FontAtlas& f, const Instance& in) {
    const glm::vec2 c{in.model[3][0], in.model[3][1]};
    const float gw = std::abs(in.model[0][0]), gh = std::abs(in.model[1][1]);
    const float x0 = c.x - gw / 2, y0 = c.y - gh / 2;
    const glm::vec4 uv = in.emissive;
    const float spread = in.params.x;
    for (int y = int(std::floor(y0)); y <= int(std::ceil(y0 + gh)); ++y)
        for (int x = int(std::floor(x0)); x <= int(std::ceil(x0 + gw)); ++x) {
            const float tx = (x + 0.5f - x0) / gw, ty = (y + 0.5f - y0) / gh;
            if (tx < 0 || tx > 1 || ty < 0 || ty > 1) continue;
            const float u = uv.x + (uv.z - uv.x) * tx, v = uv.y + (uv.w - uv.y) * ty;
            const float d = sdf(f, u, v);
            const float dx = std::abs(sdf(f, u + (uv.z - uv.x) / gw, v) - d), dy = std::abs(sdf(f, u, v + (uv.w - uv.y) / gh) - d);
            const float wdt = std::max((dx + dy) * 0.75f, 1e-3f);
            const float t = std::clamp((d - (0.5f - wdt)) / (2 * wdt), 0.f, 1.f);
            const float fill = t * t * (3 - 2 * t) * in.color.a;
            float glow = 0.f;
            if (spread > 0.f) { const float g = std::clamp((d - (0.5f - spread)) / spread, 0.f, 1.f); glow = g * g * in.rim.a; }
            if (glow > 0.f) cv.over(x, y, glm::vec3(in.rim), glow * (1.f - fill));
            if (fill > 0.f) cv.over(x, y, glm::vec3(in.color), fill);
        }
}

void drawText(Canvas& cv, const FontAtlas& f, const TextLayout& tl, std::string_view s, const TextStyle& st, float cx, float top) {
    std::vector<Instance> glyphs;
    tl.drawLine(glyphs, s, st, cx, top, TextAlign::Center, true);
    for (const Instance& g : glyphs) drawGlyph(cv, f, g);
}

// an icon frame from the HUD atlas (premultiplied RGBA), bilinear, centred at c
void drawIcon(Canvas& cv, const HudImage& img, Icon icon, int frame, glm::vec2 c, float size) {
    if (!img.built()) return;
    const int i = int(icon) + frame;
    const float cw = float(img.width()) / kIconCols, ch = float(img.height()) / kIconRows;
    const float sx = (i % kIconCols) * cw, sy = (i / kIconCols) * ch;
    const auto& p = img.pixels();
    for (int y = int(c.y - size / 2); y < int(c.y + size / 2); ++y)
        for (int x = int(c.x - size / 2); x < int(c.x + size / 2); ++x) {
            const float u = (x + 0.5f - (c.x - size / 2)) / size * cw + sx, v = (y + 0.5f - (c.y - size / 2)) / size * ch + sy;
            const int ix = std::clamp(int(u), 0, img.width() - 1), iy = std::clamp(int(v), 0, img.height() - 1);
            const uint8_t* q = &p[(size_t(iy) * img.width() + ix) * 4];
            const float a = q[3] / 255.f;
            if (a <= 0.003f) continue;
            cv.over(x, y, glm::vec3(q[0], q[1], q[2]) / (255.f * a), a);
        }
}

} // namespace

std::vector<uint8_t> renderEndCard(const FontAtlas& fonts, const HudImage& icons, int W, int H, const ReplayMeta& m) {
    using namespace hud;
    Canvas cv(W, H);
    // background: deep ink with a magenta glow behind the number, faint scanlines
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float dx = (x - W * 0.5f) / W, dy = (y - H * 0.42f) / H;
            const float g = std::exp(-(dx * dx * 6 + dy * dy * 9) * 3.f);
            glm::vec3 c = glm::mix(css("#07060f"), css("#3a0b4a"), g);
            if (y % 3 == 0) c *= 0.93f;
            float* p = &cv.px[(size_t(y) * W + x) * 3];
            p[0] = c.r; p[1] = c.g; p[2] = c.b;
        }
    if (!fonts.built()) return {};
    TextLayout tl(fonts);
    const float S = W / 720.f; // designed at 720 px wide
    const float mid = H * 0.42f;
    TextStyle tag = style(FontId::BodyBold, 34 * S, m.newBest ? kVolt : kNeon, 0.26f, true);
    tag.shadows = {blur(14 * S, m.newBest ? kVolt : kNeon, 0.8f)};
    drawText(cv, fonts, tl, m.newBest ? "New best!" : "Daily run", tag, W / 2.f, mid - 250 * S);
    TextStyle big = style(FontId::Display, 150 * S, kText, 0.f, false, 1.f);
    big.shadows = {hard(-5 * S, kHot), hard(5 * S, kNeon), blur(40 * S, kHot, 0.5f)};
    drawText(cv, fonts, tl, std::to_string(m.distance) + " m", big, W / 2.f, mid - 190 * S);
    TextStyle q = style(FontId::BodyBold, 52 * S, kText, 0.02f);
    q.shadows = {blur(16 * S, kHot, 0.6f)};
    drawText(cv, fonts, tl, "Can you beat it?", q, W / 2.f, mid + 10 * S);
    TextStyle day = style(FontId::BodyBold, 30 * S, kNeon, 0.18f, true);
    drawText(cv, fonts, tl, m.day, day, W / 2.f, mid + 95 * S);
    // brand block: spinning-coin logo + name + where to get it
    drawIcon(cv, icons, Icon::Coin, 0, {W / 2.f, mid + 260 * S}, 120 * S);
    TextStyle name = style(FontId::Display, 76 * S, kText, 0.02f, false, 1.f);
    name.shadows = {hard(-3 * S, kHot), hard(3 * S, kNeon), blur(28 * S, kHot, 0.45f)};
    drawText(cv, fonts, tl, "Cuckoo Stack", name, W / 2.f, mid + 340 * S);
    TextStyle store = style(FontId::BodyBold, 30 * S, kText, 0.14f, true);
    store.opacity = 0.9f;
    drawText(cv, fonts, tl, "Free on iOS & Android", store, W / 2.f, mid + 440 * S);

    std::vector<uint8_t> out(size_t(W) * H * 4);
    for (size_t i = 0; i < size_t(W) * H; ++i) {
        for (int c = 0; c < 3; ++c) out[i * 4 + c] = uint8_t(std::clamp(cv.px[i * 3 + c], 0.f, 1.f) * 255.f + 0.5f);
        out[i * 4 + 3] = 255;
    }
    return out;
}

} // namespace cs
