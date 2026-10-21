// Text (Font.cpp): the SDF font atlas (Latin fonts, the Japanese sign glyphs and the tombs' hieroglyphs), measuring
// with letter spacing and uppercase, CSS-style wrapping (greedy and text-wrap: balance), alignment, text-shadow order,
// one glyph fitted to a box (a hieroglyph on a tomb's wall), and UTF-8 decoding.
#include "TestUtil.h"
#include "core/Font.h"
#include "core/Hieroglyphs.h"

using namespace cs;
using namespace test;

namespace {

FontAtlas atlas;

TextStyle st(FontId f, float size, float spacing = 0.f, bool upper = false) {
    TextStyle s;
    s.font = f; s.size = size; s.letterSpacing = spacing; s.uppercase = upper;
    return s;
}
float minX(const std::vector<Instance>& v) { float m = 1e9f; for (const Instance& i : v) m = std::min(m, i.model[3].x - std::abs(i.model[0].x) / 2); return m; }
float maxX(const std::vector<Instance>& v) { float m = -1e9f; for (const Instance& i : v) m = std::max(m, i.model[3].x + std::abs(i.model[0].x) / 2); return m; }

void atlasBuilt() {
    check(atlas.built() && atlas.width() == FontAtlas::kWidth && atlas.height() > 0 && atlas.height() <= 4096, "the atlas builds (2048 wide)");
    bool latin = true;
    for (FontId f : {FontId::Display, FontId::Body, FontId::BodyBold})
        for (uint32_t c = 0x21; c <= 0x7E; ++c) latin = latin && atlas.glyph(f, c) != nullptr;
    check(latin, "every printable ASCII character in the display and body fonts");
    bool jp = true;
    const std::string_view words = "コケコッコーニワトリタマゴネオン卵屋養鶏場";
    for (size_t i = 0; i < words.size();) {
        FontId got = FontId::Body;
        jp = jp && atlas.glyph(FontId::Display, nextCodepoint(words, i), &got) != nullptr && got == FontId::Japanese;
    }
    check(jp, "the neon signs' Japanese words, falling back to the Japanese font");
    check(atlas.glyph(FontId::Body, 0x00B7) && atlas.glyph(FontId::Body, 0x2026) && atlas.glyph(FontId::Body, 0x2014), "the extras: · … —");
    check(atlas.glyph(FontId::Body, 0x1F600) == nullptr, "a glyph nobody has: none (skipped, not a crash)");
    bool hiero = true;
    for (uint32_t cp : hieroglyphs::kColumns) {
        FontId got = FontId::Body;
        const FontAtlas::Glyph* g = atlas.glyph(FontId::Hieroglyphs, cp, &got);
        hiero = hiero && g && got == FontId::Hieroglyphs && g->size.x > 0.5f && g->size.y > 0.5f;
    }
    const FontAtlas::Glyph* star = atlas.glyph(FontId::Hieroglyphs, hieroglyphs::kStar);
    bool name = true;
    for (uint32_t cp : hieroglyphs::kCuckoo) name = name && atlas.glyph(FontId::Hieroglyphs, cp);
    check(hiero && star && star->size.x > 0.5f && name, "every hieroglyph the tombs draw (their columns, stars and cartouche names)");
}

void measuring() {
    TextLayout tl(atlas);
    const TextStyle s = st(FontId::Body, 20);
    check(tl.measure("", s) == 0.f, "measure: empty is 0");
    const float ab = tl.measure("AB", s), abcd = tl.measure("ABCD", s);
    check(ab > 0 && abcd > ab * 1.5f, "measure: grows with the text");
    check(std::abs(tl.measure("AB", st(FontId::Body, 40)) - 2 * ab) < 1e-3f, "measure: scales with the size");
    check(std::abs(tl.measure("AB", st(FontId::Body, 20, 0.1f)) - (ab + 2 * 0.1f * 20)) < 1e-3f, "letter spacing: em per glyph, after each (CSS)");
    check(tl.measure("abc", st(FontId::Body, 20, 0, true)) == tl.measure("ABC", s), "uppercase style measures the uppercase text");
    check(tl.measure("A\xF0\x9F\x98\x80" "B", s) == tl.measure("AB", s), "missing glyphs take no space");
    check(tl.ch(s) == atlas.glyph(FontId::Body, '0')->advance * 20, "ch: the advance of '0'");
    check(tl.lineBox(st(FontId::Body, 20)) > 20.f && tl.lineBox([] { TextStyle t = st(FontId::Body, 20); t.lineHeight = 1.5f; return t; }()) == 30.f,
          "lineBox: CSS normal line height, or size x lineHeight");
}

void wrapping() {
    TextLayout tl(atlas);
    const TextStyle s = st(FontId::Body, 16);
    const std::string text = "Reach the finish line three hundred metres away to clear the level";
    const float maxW = 200;
    const auto lines = tl.wrap(text, s, maxW);
    bool fits = true;
    std::string joined;
    for (const auto& l : lines) { fits = fits && tl.measure(l, s) <= maxW; joined += (joined.empty() ? "" : " ") + l; }
    check(lines.size() > 1 && fits, "wrap: every line fits the width");
    check(joined == text, "wrap: no words lost, reordered or split");
    const auto balanced = tl.wrap(text, s, maxW, true);
    float widest = 0, widestGreedy = 0;
    for (const auto& l : balanced) widest = std::max(widest, tl.measure(l, s));
    for (const auto& l : lines) widestGreedy = std::max(widestGreedy, tl.measure(l, s));
    check(balanced.size() == lines.size() && widest <= widestGreedy, "balance: same number of lines, never wider");
    // a case greedy handles badly: two lines with a lone short word left over
    const std::string orphan = "Your best here 215 m of 300 attempts";
    const float wide = tl.measure("Your best here 215 m of 300", s) + 1;
    const auto g2 = tl.wrap(orphan, s, wide), b2 = tl.wrap(orphan, s, wide, true);
    auto spread = [&](const std::vector<std::string>& v) {
        float lo = 1e9f, hi = 0;
        for (const auto& l : v) { lo = std::min(lo, tl.measure(l, s)); hi = std::max(hi, tl.measure(l, s)); }
        return hi - lo;
    };
    check(g2.size() == 2 && b2.size() == 2 && spread(b2) < spread(g2) * 0.5f, "balance: evens the lines out (no lone word on the last line)");
    const auto two = tl.wrap("Cuckoo Stack", st(FontId::Display, 55), 300, true);
    check(two.size() == 2 && two[0] == "Cuckoo" && two[1] == "Stack", "the title logo wraps to two lines on a phone");
    const auto lone = tl.wrap("Supercalifragilistic", s, 30);
    check(lone.size() == 1 && lone[0] == "Supercalifragilistic", "a word wider than the line gets a line of its own (never split)");
    check(tl.wrap("  spaced   out  ", s, 1000) == std::vector<std::string>{"spaced out"}, "extra spaces collapse (CSS white-space: normal)");
    check(tl.wrap("", s, 100).empty(), "nothing to wrap: no lines");
}

void drawing() {
    TextLayout tl(atlas);
    TextStyle s = st(FontId::Display, 30);
    std::vector<Instance> c, l, r;
    tl.drawLine(c, "HELLO", s, 200, 10, TextAlign::Center);
    tl.drawLine(l, "HELLO", s, 200, 10, TextAlign::Left);
    tl.drawLine(r, "HELLO", s, 200, 10, TextAlign::Right);
    check(c.size() == 5 && l.size() == 5, "one glyph quad per letter");
    check(std::abs((minX(c) + maxX(c)) / 2 - 200) < 4, "centre: around the anchor");
    const float w = tl.measure("HELLO", s);
    check(std::abs(minX(l) - (minX(c) + w / 2)) < 1e-3f && std::abs(minX(r) - (minX(c) - w / 2)) < 1e-3f,
          "left starts at the anchor, right ends at it (the centred line moved by half its width)");
    std::vector<Instance> sp;
    tl.drawLine(sp, "A B", s, 0, 0, TextAlign::Left);
    check(sp.size() == 2, "spaces advance the pen but draw nothing");
    s.shadows = {{{-3, 0}, 0, glm::vec3(1, 0, 1), 1}, {{3, 0}, 0, glm::vec3(0, 1, 1), 1}, {{0, 0}, 20, glm::vec3(1, 0, 1), 0.5f}};
    std::vector<Instance> sh;
    tl.drawLine(sh, "AB", s, 100, 0, TextAlign::Center);
    check(sh.size() == 6, "hard shadows: a copy of the text each (the glow rides the text itself)");
    check(glm::vec3(sh[0].color) == glm::vec3(0, 1, 1) && glm::vec3(sh[2].color) == glm::vec3(1, 0, 1) && glm::vec3(sh[4].color) == s.color,
          "painted back to front: last shadow first (lowest), the text last (on top)");
    check(sh[4].rim == glm::vec4(1, 0, 1, 0.5f) && sh[4].params.x > 0 && sh[0].params.x == 0, "the blurred shadow becomes the text's glow");
    check(sh[2].model[3].x < sh[4].model[3].x && sh[0].model[3].x > sh[4].model[3].x, "shadow offsets: pink left, cyan right");
    std::vector<Instance> lines;
    const float h = tl.drawLines(lines, {"ONE", "TWO"}, s, 100, 0);
    check(h == 2 * tl.lineBox(s) && lines.size() == 18, "drawLines: stacked line boxes");
}

void utf8() {
    size_t i = 0;
    const std::string s = "A\xC2\xB7\xE3\x82\xB3\xF0\x9F\x98\x80";
    check(nextCodepoint(s, i) == 'A' && i == 1, "UTF-8: ASCII");
    check(nextCodepoint(s, i) == 0xB7 && i == 3, "UTF-8: two bytes (·)");
    check(nextCodepoint(s, i) == 0x30B3 && i == 6, "UTF-8: three bytes (コ)");
    check(nextCodepoint(s, i) == 0x1F600 && i == 10, "UTF-8: four bytes (an emoji)");
    const std::string cut = "\xE3\x82";
    size_t j = 0;
    check(nextCodepoint(cut, j) == 0xFFFD && j == 1, "UTF-8: a cut-off sequence -> U+FFFD, one byte at a time (no overrun)");
    check(toUpperAscii("Level 3 · égg") == "LEVEL 3 · éGG", "toUpperAscii: ASCII letters only (UTF-8 untouched)");
}

} // namespace

void fitting() {
    // a hieroglyph on a tomb's wall: its ink (not its SDF padding) fills the box, centred, in the plane's units
    const TextLayout tl(atlas);
    TextStyle s = st(FontId::Hieroglyphs, 99);
    s.weight = 0.04f;
    s.shadows = {{{0.f, 0.f}, 0.07f, glm::vec3(1.f), 0.5f}};
    const float pad = 2.f * FontAtlas::kPadPx / FontAtlas::kBakePx;
    bool fits = true, centred = true, styled = true;
    for (uint32_t cp : {hieroglyphs::kColumns[0], hieroglyphs::kColumns[23], hieroglyphs::kStar}) { // (a chick, a lion, a star)
        std::vector<Instance> out;
        tl.drawGlyphFit(out, cp, s, {3.f, 2.f}, 0.4f, compose({0.f, 0.f, -0.5f}, glm::vec3(1.f)));
        const FontAtlas::Glyph* g = atlas.glyph(FontId::Hieroglyphs, cp);
        if (out.size() != 1 || !g) { fits = false; continue; }
        const float perEm = out[0].model[0].x / g->size.x; // (the quad: the glyph's padded box, scaled)
        const float ink = std::max(g->size.x - pad, g->size.y - pad) * perEm;
        fits = fits && std::abs(ink - 0.4f) < 1e-4f && std::abs(out[0].model[1].y / g->size.y - perEm) < 1e-4f;
        centred = centred && std::abs(out[0].model[3].x - 3.f) < 1e-5f && std::abs(out[0].model[3].y - 2.f) < 1e-5f &&
                  std::abs(out[0].model[3].z + 0.5f) < 1e-5f;
        styled = styled && out[0].params.y == 0.04f && out[0].params.x > 0.f && out[0].rim.a == 0.5f;
    }
    check(fits, "one glyph fitted to a box: its ink fills the box (the wider way), its shape kept");
    check(centred, "...centred where it's put, on the plane it's drawn on");
    check(styled, "...with the style's weight (strokes filled out) and glow");
    std::vector<Instance> none;
    tl.drawGlyphFit(none, 0x1F600, s, {0.f, 0.f}, 0.4f);
    check(none.empty(), "...and nothing for a glyph the font hasn't got");
}

int main() {
    atlas.build();
    atlasBuilt();
    measuring();
    wrapping();
    drawing();
    fitting();
    utf8();
    return finish("Font");
}
