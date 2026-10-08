// Text for the HUD, overlays, popups and neon signs: a signed-distance-field glyph atlas baked at startup
// from the embedded web fonts (stb_truetype), plus CSS-like layout (font-size, letter-spacing, line-height,
// text-transform: uppercase, max-width word wrap, kerning, CJK fallback). Output is RenderList instances.
#pragma once

#include "RenderList.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cs {

enum class FontId : uint8_t {
    Display = 0,   // Orbitron 900 (shipped as "Cuckoo Display"): --display
    Body = 1,      // Chakra Petch 500: --body
    BodyBold = 2,  // Chakra Petch 700: tags, labels, pills
    Japanese = 3,  // Noto Sans JP 700 subset: katakana / kanji on the neon signs (fallback only)
    Count
};

class FontAtlas {
public:
    static constexpr float kBakePx = 48.f;   // em size the SDF is rasterised at
    static constexpr float kPadPx = 12.f;    // SDF spread: enough for CSS glows up to ~0.25 em
    static constexpr int kWidth = 2048;

    struct Glyph {
        glm::vec4 uv;          // u0, v0, u1, v1
        glm::vec2 offset;      // quad top-left relative to the pen on the baseline, in em
        glm::vec2 size;        // quad size, in em
        float advance;         // in em
        int index;             // stb glyph index (kerning)
    };
    struct Metrics { float ascent, descent, lineGap; }; // em, descent negative

    bool build();
    bool built() const { return !pixels_.empty(); }
    int width() const { return kWidth; }
    int height() const { return height_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }

    // Falls back to the Japanese font for codepoints the requested font lacks.
    const Glyph* glyph(FontId font, uint32_t cp, FontId* resolved = nullptr) const;
    float kern(FontId font, const Glyph* a, const Glyph* b) const;
    const Metrics& metrics(FontId f) const { return metrics_[size_t(f)]; }

    FontAtlas();
    ~FontAtlas();
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

private:
    struct FontData; // stbtt_fontinfo lives in Font.cpp only
    std::vector<uint8_t> pixels_;
    int height_ = 0;
    std::array<std::unordered_map<uint32_t, Glyph>, size_t(FontId::Count)> glyphs_;
    std::array<Metrics, size_t(FontId::Count)> metrics_{};
    std::array<std::unique_ptr<FontData>, size_t(FontId::Count)> fonts_;
};

// CSS text-shadow entry: hard offset copies (blur 0) or a glow (blur > 0) around the glyph.
struct TextShadow { glm::vec2 offset{0.f}; float blur = 0.f; glm::vec3 color{0.f}; float alpha = 1.f; };

struct TextStyle {
    FontId font = FontId::Body;
    float size = 16.f;             // px (HUD) or world units
    float letterSpacing = 0.f;     // em, added after every glyph (CSS behaviour)
    float lineHeight = 0.f;        // multiple of size; 0 = CSS `normal` (font ascent - descent + line gap)
    bool uppercase = false;
    glm::vec3 color{1.f};
    float opacity = 1.f;
    std::vector<TextShadow> shadows;
};

enum class TextAlign { Left, Center, Right };

class TextLayout {
public:
    explicit TextLayout(const FontAtlas& atlas) : atlas_(atlas) {}

    float measure(std::string_view text, const TextStyle& s) const;
    // advance of '0' (CSS ch unit)
    float ch(const TextStyle& s) const;
    std::vector<std::string> wrap(std::string_view text, const TextStyle& s, float maxWidth, bool balance = false) const;
    float lineBox(const TextStyle& s) const;

    // Draws one line whose line box top is at `top`. `yDown`: HUD space (pixels, Y down) vs world (Y up).
    // In world space (yDown = false) `top` is the line box top in the plane's local units and `world` places the plane.
    void drawLine(std::vector<Instance>& out, std::string_view text, const TextStyle& s, float anchorX, float top,
                  TextAlign align, bool yDown = true, const glm::mat4& world = glm::mat4(1.f)) const;
    // Canvas-style placement: `baseline` is the alphabetic baseline (fillText semantics).
    void drawBaseline(std::vector<Instance>& out, std::string_view text, const TextStyle& s, float anchorX, float baseline,
                      TextAlign align, bool yDown = true, const glm::mat4& world = glm::mat4(1.f)) const;
    // Multiple lines, each centred on anchor.x; returns total height.
    float drawLines(std::vector<Instance>& out, const std::vector<std::string>& lines, const TextStyle& s, float centerX, float top,
                    bool yDown = true) const;

private:
    void emitGlyphs(std::vector<Instance>& out, std::string_view text, const TextStyle& s, float penX, float baseline,
                    bool yDown, const glm::mat4& world, glm::vec4 color, glm::vec4 glow, float glowSpread) const;
    const FontAtlas& atlas_;
};

std::string toUpperAscii(std::string_view s);
// Decode one UTF-8 codepoint, advancing i
uint32_t nextCodepoint(std::string_view s, size_t& i);

} // namespace cs
