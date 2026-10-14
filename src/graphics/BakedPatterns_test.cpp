// The cyber cube bake (BakedPatterns.cpp): atlas layout, regions, glow channels, the four circuit layouts.
#include "TestUtil.h"
#include "graphics/BakedPatterns.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

// ---------------------------------------------------------------- cyber cube bake

void cyberBake() {
    constexpr int n = 64; // small: same maths as the game's 256
    const CyberCubeBake b = bakeCyberCube(n);
    const size_t W = 2 * n;
    check(b.surface.size() == W * W * 4 && b.glow.size() == W * W * 4, "bake: two RGBA atlases, 2 x 2 faces of n x n");
    auto at = [&](const std::vector<uint8_t>& img, int variant, float u, float v, int ch) {
        const int x = (variant % 2) * n + std::min(n - 1, int(u * n)), y = (variant / 2) * n + std::min(n - 1, int(v * n));
        return int(img[(size_t(y) * W + size_t(x)) * 4 + size_t(ch)]);
    };
    const int kFrame = 0, kBolt = 85, kPanel = 170, kTape = 255; // surface alpha: kind / 3
    bool regions = true, glowOk = true;
    for (int v = 0; v < 4; ++v) {
        regions = regions && at(b.surface, v, 0.02f, 0.5f, 3) == kFrame;          // the armour frame at the edge
        regions = regions && at(b.surface, v, 18.f / 512.f, 18.f / 512.f, 3) == kBolt; // a bolt in the corner
        regions = regions && at(b.surface, v, 0.3f, 0.3f, 3) == kPanel;           // the glass panel inside
        regions = regions && at(b.surface, v, 0.5f, 0.815f, 3) == kTape;          // the hazard tape at the bottom
        glowOk = glowOk && at(b.glow, v, 0.5f, 0.5f, 3) > 200;                    // the core's centre dot
        glowOk = glowOk && at(b.glow, v, 0.5f + 58.f / 512.f, 0.5f, 2) > 100;     // the core ring
        glowOk = glowOk && at(b.glow, v, 0.5f, 24.f / 512.f, 0) > 100;            // the neon tube line, near the top edge
        glowOk = glowOk && at(b.glow, v, 0.02f, 0.5f, 0) == 0 && at(b.glow, v, 0.02f, 0.5f, 1) == 0; // nothing on the frame
    }
    check(regions, "bake: frame, bolts, glass panel and hazard tape where the design puts them, in every layout");
    check(glowOk, "bake: tube, core ring and centre dot glow; the frame doesn't");
    // the four layouts differ in their circuits (and circuits only glow inside the panel)
    std::set<std::vector<uint8_t>> layouts;
    bool circuitsInPanel = true;
    for (int v = 0; v < 4; ++v) {
        std::vector<uint8_t> c;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int g = at(b.glow, v, (x + 0.5f) / n, (y + 0.5f) / n, 1);
                c.push_back(uint8_t(g));
                // (texels on the panel's border may catch a little: the bake is anti-aliased; never on the frame)
                if (g > 0 && at(b.surface, v, (x + 0.5f) / n, (y + 0.5f) / n, 3) == kFrame) circuitsInPanel = false;
            }
        layouts.insert(c);
    }
    check(layouts.size() == 4, "bake: four different circuit layouts");
    check(circuitsInPanel, "bake: circuit traces stay off the armour frame");
    // colours are stored as sqrt(linear): the dark panel stays dark, the amber tape is amber
    const int pr = at(b.surface, 0, 0.3f, 0.3f, 0), pb = at(b.surface, 0, 0.3f, 0.3f, 2);
    check(pr < 40 && pb < 50, "bake: the glass panel is dark");
    bool amber = false;
    for (float u = 0.4f; u < 0.6f; u += 0.01f)
        amber = amber || (at(b.surface, 0, u, 0.815f, 0) > 200 && at(b.surface, 0, u, 0.815f, 2) < 100);
    check(amber, "bake: the hazard tape has amber stripes");
    check(bakeCyberCube(n).surface == b.surface, "bake: deterministic (same atlas every launch)");
}

} // namespace

int main() {
    cyberBake();
    return finish("BakedPatterns");
}
