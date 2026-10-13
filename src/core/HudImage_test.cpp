// The HUD sprite atlas (HudImage.cpp): coins and icons decoded once from the embedded PNG, premultiplied, with a mip
// chain that keeps every 192 px icon cell whole.
#include "TestUtil.h"
#include "core/HudIcons.h"
#include "core/HudImage.h"

using namespace cs;
using namespace test;

int main() {
    HudImage img;
    check(!img.built(), "nothing until built");
    check(img.buildIcons() && img.built(), "the embedded icon atlas decodes");
    check(img.width() % kIconCols == 0 && img.height() % kIconRows == 0 && img.width() / kIconCols == img.height() / kIconRows,
          "a grid of square cells, kIconCols x kIconRows");
    const int cell = img.width() / kIconCols;
    check(cell == 192, "192 px cells");
    const auto& px = img.pixels();
    check(px.size() == size_t(img.width()) * img.height() * 4, "RGBA8");
    bool premul = true, anyOpaque = false, anyClear = false;
    for (size_t i = 0; i < px.size(); i += 4) {
        premul = premul && px[i] <= px[i + 3] && px[i + 1] <= px[i + 3] && px[i + 2] <= px[i + 3];
        anyOpaque = anyOpaque || px[i + 3] == 255;
        anyClear = anyClear || px[i + 3] == 0;
    }
    check(premul, "premultiplied alpha (no colour brighter than its alpha: clean edges, no dark fringes)");
    check(anyOpaque && anyClear, "solid icons on a transparent background");
    const auto& mips = img.mips();
    check(mips.size() == 3, "three extra mip levels (96, 48, 24 px cells)");
    bool halves = true;
    int w = img.width(), h = img.height();
    for (const auto& m : mips) { w /= 2; h /= 2; halves = halves && m.size() == size_t(w) * h * 4; }
    check(halves, "each level halves the size");
    // a level-1 texel is the rounded average of its 2 x 2 parents
    const int x = 37, y = 51, W = img.width();
    unsigned sum = 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) sum += px[(size_t(2 * y + dy) * W + 2 * x + dx) * 4 + 3];
    check(mips[0][(size_t(y) * (W / 2) + x) * 4 + 3] == (sum + 2) / 4, "box-filtered (2 x 2 average)");
    bool stillPremul = true;
    for (const auto& m : mips) for (size_t i = 0; i < m.size(); i += 4) stillPremul = stillPremul && m[i] <= m[i + 3] + 1;
    check(stillPremul, "mips stay premultiplied");
    return finish("HudImage");
}
