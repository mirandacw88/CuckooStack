// The renderer's CPU side (RendererLogic.h): the frame uniform block must match assets/shaders/frame.glsl member for
// member (std140 offsets), the values are packed where the shaders read them, and texture mip chains are proper box
// filters down to 1 px.
#include "TestUtil.h"
#include "graphics/RendererLogic.h"

#include <cstddef>
#include <fstream>
#include <regex>
#include <sstream>

using namespace cs;
using namespace cs::gpu;
using namespace test;

namespace {

void layout() {
    std::ifstream f(std::string(CS_SOURCE_DIR) + "/assets/shaders/frame.glsl");
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();
    const std::string block = src.substr(src.find("uniform Frame {"), src.find("} F;") - src.find("uniform Frame {"));
    // std140: mat4 = 64 bytes, vec4 = 16, a vec4 array element = 16
    struct Member { std::string name; size_t offset; };
    std::vector<Member> glsl;
    size_t off = 0;
    const std::regex re(R"((mat4|vec4)\s+(\w+)(\[(\d+)\])?\s*;)");
    for (std::sregex_iterator it(block.begin(), block.end(), re), end; it != end; ++it) {
        glsl.push_back({(*it)[2], off});
        const size_t n = (*it)[4].matched ? size_t(std::stoi((*it)[4])) : 1;
        off += ((*it)[1] == "mat4" ? 64 : 16) * n;
    }
#define CS_MEMBER(m) {#m, offsetof(GpuFrame, m)}
    const Member cpp[] = {CS_MEMBER(view), CS_MEMBER(proj), CS_MEMBER(viewProj), CS_MEMBER(invViewProj), CS_MEMBER(cameraPos),
                          CS_MEMBER(fogColor), CS_MEMBER(skyTop), CS_MEMBER(skyMid), CS_MEMBER(skyBot), CS_MEMBER(skyHaze),
                          CS_MEMBER(moonDir), CS_MEMBER(hemiSky), CS_MEMBER(hemiGround), CS_MEMBER(sunDir), CS_MEMBER(sunColor),
                          CS_MEMBER(pointPos), CS_MEMBER(pointColor), CS_MEMBER(misc)};
#undef CS_MEMBER
    bool same = glsl.size() == std::size(cpp);
    for (size_t i = 0; same && i < glsl.size(); ++i) same = glsl[i].name == cpp[i].name && glsl[i].offset == cpp[i].offset;
    check(glsl.size() > 10, "frame.glsl's Frame block is found and read");
    check(same, "GpuFrame matches frame.glsl member for member (names and std140 offsets)");
    check(sizeof(GpuFrame) == off, "...and in total size");
}

void packing() {
    FrameParams f;
    f.view = glm::lookAt(glm::vec3(1, 2, 3), glm::vec3(0), glm::vec3(0, 1, 0));
    f.proj = glm::perspective(1.f, 0.5f, 0.1f, 100.f);
    f.cameraPos = {1, 2, 3}; f.time = 12.5f;
    f.fogColor = {0.1f, 0.2f, 0.3f}; f.fogDensity = 0.02f;
    f.hemiSky = {0.5f, 0.6f, 0.7f}; f.hemiIntensity = 1.5f;
    f.points[1].pos = {4, 5, 6}; f.points[1].distance = 9; f.points[1].color = {1, 0.5f, 0}; f.points[1].intensity = 2;
    f.pulse = 0.75f; f.lite = true;
    const GpuFrame g = toGpu(f);
    check(g.viewProj == f.proj * f.view, "viewProj = proj x view");
    const glm::mat4 id = g.viewProj * g.invViewProj;
    bool identity = true;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) identity = identity && std::abs(id[c][r] - (c == r ? 1.f : 0.f)) < 1e-3f;
    check(identity, "invViewProj inverts it");
    check(g.cameraPos == glm::vec4(1, 2, 3, 12.5f) && g.fogColor.w == 0.02f, "time rides in cameraPos.w, fog density in fogColor.w");
    check(g.hemiSky.w == 1.5f, "hemisphere intensity in hemiSky.w");
    check(g.pointPos[1] == glm::vec4(4, 5, 6, 9) && g.pointColor[1] == glm::vec4(2, 1, 0, 0), "point lights: cutoff in w, colour x intensity");
    check(g.misc == glm::vec4(0.75f, 1, 0, 0), "misc: x = beat pulse, y = lite");
    f.lite = false;
    check(toGpu(f).misc.y == 0, "misc.y = 0 with full effects");
}

void mips() {
    const int w = 8, h = 4;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = uint8_t((i * 37) % 251);
    const auto levels = boxMipChain(px.data(), w, h);
    check(levels.size() == 3 && levels[0] == px, "8x4: levels 8x4, 4x2, 2x1 (stops when a side reaches 1)");
    check(levels[1].size() == 4 * 2 * 4 && levels[2].size() == 2 * 1 * 4, "each level half the size");
    bool avg = true;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 4; ++c) {
                int s = 0;
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) s += px[(size_t(2 * y + dy) * w + 2 * x + dx) * 4 + c];
                avg = avg && levels[1][(size_t(y) * 4 + x) * 4 + c] == (s + 2) / 4;
            }
    check(avg, "box filter: each texel is the rounded average of its 2 x 2 parents");
    std::vector<uint8_t> flat(size_t(512) * 512 * 4, 200);
    const auto chain = boxMipChain(flat.data(), 512, 512);
    bool constant = true;
    for (const auto& l : chain) for (uint8_t v : l) constant = constant && v == 200;
    check(chain.size() == 10 && chain.back().size() == 4 && constant, "512x512: down to 1x1, a flat colour stays flat");
    std::vector<uint8_t> big(size_t(4096) * 4096 * 4, 1);
    check(boxMipChain(big.data(), 4096, 4096).size() == 11, "at most 11 levels");
}

} // namespace

void standIns() {
    // a texture that failed to load binds a stand-in, so the descriptor is always valid
    int puddle = 1, droid = 2;
    int *none = nullptr;
    check(gpu::orStandIn(&droid, &puddle) == &droid, "stand-ins: a loaded texture binds itself");
    check(gpu::orStandIn(none, &puddle) == &puddle, "...one that failed (or a missing asset) binds the stand-in, never nothing");
    check(gpu::orStandIn(none, gpu::orStandIn(none, &puddle)) == &puddle, "...the droid's normal map falls back to its albedo's binding, itself possibly the stand-in");
}

int main() {
    standIns();
    layout();
    packing();
    mips();
    return finish("RendererLogic");
}
