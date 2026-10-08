#include "Font.h"
#include "FontData.h"
#include "Log.h"
#include "Materials.h"

#include <algorithm>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#include <stb_truetype.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace cs {

struct FontAtlas::FontData {
    stbtt_fontinfo info{};
    float scale = 1.f; // font units -> bake pixels
};

FontAtlas::FontAtlas() = default;
FontAtlas::~FontAtlas() = default;

uint32_t nextCodepoint(std::string_view s, size_t& i) {
    const auto b = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(s[k])); };
    const uint32_t c = b(i);
    if (c < 0x80) { i += 1; return c; }
    if ((c >> 5) == 0x6 && i + 1 < s.size()) { const uint32_t r = ((c & 0x1F) << 6) | (b(i + 1) & 0x3F); i += 2; return r; }
    if ((c >> 4) == 0xE && i + 2 < s.size()) { const uint32_t r = ((c & 0x0F) << 12) | ((b(i + 1) & 0x3F) << 6) | (b(i + 2) & 0x3F); i += 3; return r; }
    if ((c >> 3) == 0x1E && i + 3 < s.size()) {
        const uint32_t r = ((c & 0x07) << 18) | ((b(i + 1) & 0x3F) << 12) | ((b(i + 2) & 0x3F) << 6) | (b(i + 3) & 0x3F);
        i += 4; return r;
    }
    i += 1;
    return 0xFFFD;
}

std::string toUpperAscii(std::string_view s) {
    std::string r(s);
    for (char& c : r) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return r;
}

bool FontAtlas::build() {
    struct Source { const unsigned char* data; size_t size; std::vector<uint32_t> cps; };
    std::vector<uint32_t> latin;
    for (uint32_t c = 0x20; c <= 0x7E; ++c) latin.push_back(c);
    for (uint32_t c : {0x00B7u, 0x00D7u, 0x2013u, 0x2014u, 0x2019u, 0x2026u}) latin.push_back(c);
    std::vector<uint32_t> jp;
    {
        const std::string_view text = "コケコッコーニワトリタマゴネオン卵屋養鶏場";
        for (size_t i = 0; i < text.size();) {
            const uint32_t cp = nextCodepoint(text, i);
            if (std::find(jp.begin(), jp.end(), cp) == jp.end()) jp.push_back(cp);
        }
    }
    const std::array<Source, size_t(FontId::Count)> sources = {{
        {fontdata::display, fontdata::display_size, latin},
        {fontdata::body, fontdata::body_size, latin},
        {fontdata::body_bold, fontdata::body_bold_size, latin},
        {fontdata::japanese, fontdata::japanese_size, jp},
    }};

    struct Pending { FontId font; uint32_t cp; unsigned char* sdf; int w, h, xoff, yoff; Glyph g; };
    std::vector<Pending> pending;
    for (size_t f = 0; f < sources.size(); ++f) {
        auto fd = std::make_unique<FontData>();
        if (!stbtt_InitFont(&fd->info, sources[f].data, stbtt_GetFontOffsetForIndex(sources[f].data, 0))) {
            CS_LOGE("Font %zu failed to load", f);
            return false;
        }
        fd->scale = stbtt_ScaleForMappingEmToPixels(&fd->info, kBakePx);
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&fd->info, &asc, &desc, &gap);
        metrics_[f] = {asc * fd->scale / kBakePx, desc * fd->scale / kBakePx, gap * fd->scale / kBakePx};
        for (uint32_t cp : sources[f].cps) {
            const int index = stbtt_FindGlyphIndex(&fd->info, static_cast<int>(cp));
            if (index == 0 && cp != 0x20) continue;
            int adv = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(&fd->info, index, &adv, &lsb);
            Pending p{FontId(f), cp, nullptr, 0, 0, 0, 0, {}};
            p.g.advance = adv * fd->scale / kBakePx;
            p.g.index = index;
            p.sdf = stbtt_GetGlyphSDF(&fd->info, fd->scale, index, static_cast<int>(kPadPx), 128, 128.f / kPadPx, &p.w, &p.h, &p.xoff, &p.yoff);
            pending.push_back(p);
        }
        fonts_[f] = std::move(fd);
    }

    // shelf packing, 1 px gutter
    int x = 0, y = 0, rowH = 0;
    struct Placed { int x, y; };
    std::vector<Placed> placed(pending.size());
    for (size_t i = 0; i < pending.size(); ++i) {
        const Pending& p = pending[i];
        if (!p.sdf) continue;
        if (x + p.w + 1 > kWidth) { x = 0; y += rowH + 1; rowH = 0; }
        placed[i] = {x, y};
        x += p.w + 1;
        rowH = std::max(rowH, p.h);
    }
    height_ = 64;
    while (height_ < y + rowH + 1) height_ *= 2;
    pixels_.assign(size_t(kWidth) * height_, 0);
    for (size_t i = 0; i < pending.size(); ++i) {
        Pending& p = pending[i];
        Glyph g = p.g;
        if (p.sdf) {
            for (int r = 0; r < p.h; ++r)
                std::memcpy(&pixels_[size_t(placed[i].y + r) * kWidth + placed[i].x], p.sdf + size_t(r) * p.w, size_t(p.w));
            g.uv = {float(placed[i].x) / kWidth, float(placed[i].y) / height_, float(placed[i].x + p.w) / kWidth, float(placed[i].y + p.h) / height_};
            g.offset = {p.xoff / kBakePx, p.yoff / kBakePx};
            g.size = {p.w / kBakePx, p.h / kBakePx};
            stbtt_FreeSDF(p.sdf, nullptr);
        } else {
            g.uv = glm::vec4(0.f); g.offset = glm::vec2(0.f); g.size = glm::vec2(0.f);
        }
        glyphs_[size_t(p.font)][p.cp] = g;
    }
    CS_LOGI("Font atlas %dx%d, %zu glyphs", kWidth, height_, pending.size());
    return true;
}

const FontAtlas::Glyph* FontAtlas::glyph(FontId font, uint32_t cp, FontId* resolved) const {
    for (FontId f : {font, FontId::Japanese, FontId::BodyBold}) {
        const auto& map = glyphs_[size_t(f)];
        const auto it = map.find(cp);
        if (it != map.end()) { if (resolved) *resolved = f; return &it->second; }
    }
    return nullptr;
}

float FontAtlas::kern(FontId font, const Glyph* a, const Glyph* b) const {
    if (!a || !b || !fonts_[size_t(font)]) return 0.f;
    const FontData& fd = *fonts_[size_t(font)];
    return stbtt_GetGlyphKernAdvance(&fd.info, a->index, b->index) * fd.scale / kBakePx;
}

// ---------------------------------------------------------------- layout

float TextLayout::measure(std::string_view text, const TextStyle& s) const {
    const std::string str = s.uppercase ? toUpperAscii(text) : std::string(text);
    float w = 0.f;
    const FontAtlas::Glyph* prev = nullptr;
    FontId prevFont = s.font;
    for (size_t i = 0; i < str.size();) {
        FontId f = s.font;
        const FontAtlas::Glyph* g = atlas_.glyph(s.font, nextCodepoint(str, i), &f);
        if (!g) continue;
        if (prev && prevFont == f) w += atlas_.kern(f, prev, g) * s.size;
        w += (g->advance + s.letterSpacing) * s.size;
        prev = g; prevFont = f;
    }
    return w;
}

float TextLayout::ch(const TextStyle& s) const {
    const FontAtlas::Glyph* g = atlas_.glyph(s.font, '0');
    return g ? g->advance * s.size : s.size * 0.5f;
}

std::vector<std::string> TextLayout::wrap(std::string_view text, const TextStyle& s, float maxWidth, bool balance) const {
    std::vector<std::string> words;
    {
        std::string cur;
        for (char c : text) {
            if (c == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) words.push_back(cur);
    }
    auto greedy = [&](float width) {
        std::vector<std::string> lines;
        std::string line;
        for (const std::string& w : words) {
            const std::string trial = line.empty() ? w : line + " " + w;
            if (!line.empty() && measure(trial, s) > width) { lines.push_back(line); line = w; }
            else line = trial;
        }
        if (!line.empty()) lines.push_back(line);
        return lines;
    };
    std::vector<std::string> lines = greedy(maxWidth);
    if (balance && lines.size() > 1) {
        // text-wrap: balance -> the narrowest width that keeps the same line count
        float lo = maxWidth * 0.3f, hi = maxWidth;
        for (int it = 0; it < 12; ++it) {
            const float mid = (lo + hi) * 0.5f;
            if (greedy(mid).size() <= lines.size()) hi = mid; else lo = mid;
        }
        lines = greedy(hi);
    }
    return lines;
}

void TextLayout::emitGlyphs(std::vector<Instance>& out, std::string_view text, const TextStyle& s, float penX, float baseline,
                            bool yDown, const glm::mat4& world, glm::vec4 color, glm::vec4 glow, float glowSpread) const {
    const float ySign = yDown ? 1.f : -1.f;
    const FontAtlas::Glyph* prev = nullptr;
    FontId prevFont = s.font;
    for (size_t i = 0; i < text.size();) {
        FontId f = s.font;
        const FontAtlas::Glyph* g = atlas_.glyph(s.font, nextCodepoint(text, i), &f);
        if (!g) continue;
        if (prev && prevFont == f) penX += atlas_.kern(f, prev, g) * s.size;
        if (g->size.x > 0.f) {
            const glm::vec2 c{penX + (g->offset.x + g->size.x * 0.5f) * s.size, baseline + ySign * (g->offset.y + g->size.y * 0.5f) * s.size};
            Instance inst;
            // negative Y scale in HUD space keeps mesh-top (uv.y = 1) at the top of the glyph on screen
            inst.model = world * compose({c, 0.f}, {g->size.x * s.size, g->size.y * s.size * (yDown ? -1.f : 1.f), 1.f});
            inst.color = color;
            inst.emissive = g->uv;
            inst.rim = glow;
            inst.params = glm::vec4(glowSpread, 0.f, 0.f, 0.f);
            out.push_back(inst);
        }
        penX += (g->advance + s.letterSpacing) * s.size;
        prev = g; prevFont = f;
    }
}

float TextLayout::lineBox(const TextStyle& s) const {
    if (s.lineHeight > 0.f) return s.size * s.lineHeight;
    const FontAtlas::Metrics& m = atlas_.metrics(s.font);
    return s.size * (m.ascent - m.descent + m.lineGap);
}

void TextLayout::drawLine(std::vector<Instance>& out, std::string_view text, const TextStyle& s, float anchorX, float top,
                          TextAlign align, bool yDown, const glm::mat4& world) const {
    // CSS line box: half-leading above the content area, baseline at ascent below it
    const FontAtlas::Metrics& m = atlas_.metrics(s.font);
    const float content = (m.ascent - m.descent) * s.size;
    const float halfLeading = (lineBox(s) - content) * 0.5f;
    const float baseline = yDown ? top + halfLeading + m.ascent * s.size : top - halfLeading - m.ascent * s.size;
    drawBaseline(out, text, s, anchorX, baseline, align, yDown, world);
}

void TextLayout::drawBaseline(std::vector<Instance>& out, std::string_view text, const TextStyle& s, float anchorX, float baseline,
                              TextAlign align, bool yDown, const glm::mat4& world) const {
    const std::string str = s.uppercase ? toUpperAscii(text) : std::string(text);
    const float w = measure(str, s);
    const float x0 = align == TextAlign::Left ? anchorX : align == TextAlign::Center ? anchorX - w * 0.5f : anchorX - w;

    // text-shadow: painted back to front (the last shadow in the CSS list is lowest), then the text itself,
    // which also carries the widest blurred shadow as its glow
    glm::vec4 glow(0.f);
    float spread = 0.f;
    for (auto it = s.shadows.rbegin(); it != s.shadows.rend(); ++it) {
        const TextShadow& sh = *it;
        if (sh.blur > 0.f) {
            const float sp = std::min(0.5f, sh.blur * (FontAtlas::kBakePx / std::max(1.f, s.size)) / (2.f * FontAtlas::kPadPx));
            if (sp > spread) { spread = sp; glow = glm::vec4(sh.color, sh.alpha * s.opacity); }
            continue;
        }
        const glm::vec2 o = sh.offset * glm::vec2(1.f, yDown ? 1.f : -1.f);
        emitGlyphs(out, str, s, x0 + o.x, baseline + o.y, yDown, world, glm::vec4(sh.color, sh.alpha * s.opacity), glm::vec4(0.f), 0.f);
    }
    emitGlyphs(out, str, s, x0, baseline, yDown, world, glm::vec4(s.color, s.opacity), glow, spread);
}

float TextLayout::drawLines(std::vector<Instance>& out, const std::vector<std::string>& lines, const TextStyle& s, float centerX,
                            float top, bool yDown) const {
    float y = top;
    for (const std::string& l : lines) {
        drawLine(out, l, s, centerX, y, TextAlign::Center, yDown);
        y += yDown ? lineBox(s) : -lineBox(s);
    }
    return lineBox(s) * float(lines.size());
}

} // namespace cs
