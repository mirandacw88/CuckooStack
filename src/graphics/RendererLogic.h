// The renderer's CPU-side work, kept apart from the Vulkan calls so it's tested on desktop (RendererLogic_test.cpp):
// packing the frame parameters into the shaders' uniform block, and box-filtered mip chains for textures.
#pragma once

#include "../core/RenderList.h"

#include <cstdint>
#include <vector>

namespace cs::gpu {

// std140 mirror of assets/shaders/frame.glsl (every member a mat4 or vec4, so no padding)
struct GpuFrame {
    glm::mat4 view, proj, viewProj, invViewProj;
    glm::vec4 cameraPos, fogColor, skyTop, skyMid, skyBot, skyHaze, moonDir, hemiSky, hemiGround, sunDir, sunColor;
    glm::vec4 pointPos[3], pointColor[3];
    glm::vec4 misc;
};

inline GpuFrame toGpu(const FrameParams& f) {
    GpuFrame g{};
    g.view = f.view; g.proj = f.proj; g.viewProj = f.proj * f.view; g.invViewProj = glm::inverse(g.viewProj);
    g.cameraPos = glm::vec4(f.cameraPos, f.time);
    g.fogColor = glm::vec4(f.fogColor, f.fogDensity);
    g.skyTop = glm::vec4(f.skyTop, 0); g.skyMid = glm::vec4(f.skyMid, 0); g.skyBot = glm::vec4(f.skyBot, 0); g.skyHaze = glm::vec4(f.skyHaze, 0);
    g.moonDir = glm::vec4(f.moonDir, 0);
    g.hemiSky = glm::vec4(f.hemiSky, f.hemiIntensity); g.hemiGround = glm::vec4(f.hemiGround, 0);
    g.sunDir = glm::vec4(f.sunDir, 0); g.sunColor = glm::vec4(f.sunColor, 0);
    for (int i = 0; i < 3; ++i) {
        g.pointPos[i] = glm::vec4(f.points[i].pos, f.points[i].distance);
        g.pointColor[i] = glm::vec4(f.points[i].color * f.points[i].intensity, 0);
    }
    g.misc = glm::vec4(f.pulse, f.lite ? 1.f : 0.f, 0, 0);
    return g;
}

// An RGBA8 image and its box-filtered mip levels (each half the size, rounded down), down to 1 px on the shorter
// side or 11 levels in all. Level 0 is a copy of the image.
inline std::vector<std::vector<uint8_t>> boxMipChain(const uint8_t* px, int w, int h) {
    std::vector<std::vector<uint8_t>> levels{std::vector<uint8_t>(px, px + size_t(w) * h * 4)};
    for (int lw = w, lh = h; lw > 1 && lh > 1 && levels.size() < 11;) {
        const std::vector<uint8_t>& src = levels.back();
        const int nw = lw / 2, nh = lh / 2;
        std::vector<uint8_t> dst(size_t(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto at = [&](int xx, int yy) { return int(src[(size_t(yy) * lw + xx) * 4 + c]); };
                    dst[(size_t(y) * nw + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        levels.push_back(std::move(dst));
        lw = nw; lh = nh;
    }
    return levels;
}

} // namespace cs::gpu
