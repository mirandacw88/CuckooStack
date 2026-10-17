// The compiled shaders (ShaderBlobs.h): a guard on the heat fix - only the reflection shader writes depth (writing it
// in lit.frag disables the GPU's hidden-surface removal for every solid object).
#include "TestUtil.h"
#include "graphics/RendererLogic.h"
#include "graphics/ShaderBlobs.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <tuple>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;
using namespace test;

namespace {

// ---------------------------------------------------------------- heat guard: who writes depth

// SPIR-V: does the module decorate a variable as BuiltIn FragDepth (OpDecorate %id BuiltIn FragDepth)?
bool writesDepth(const uint32_t* words, size_t bytes) {
    const size_t n = bytes / 4;
    for (size_t i = 5; i < n;) {
        const uint32_t op = words[i] & 0xffff, len = words[i] >> 16;
        if (len == 0) break;
        if (op == 71 /* OpDecorate */ && len >= 4 && words[i + 2] == 11 /* BuiltIn */ && words[i + 3] == 22 /* FragDepth */) return true;
        i += len;
    }
    return false;
}

void depthGuard() {
    check(spv::lit_frag_size > 20 && spv::lit_frag[0] == 0x07230203, "shaders: lit.frag is SPIR-V");
    check(spv::lit_reflect_frag_size > 20 && spv::lit_reflect_frag[0] == 0x07230203, "shaders: lit_reflect.frag is SPIR-V");
    check(!writesDepth(spv::lit_frag, spv::lit_frag_size), "heat: lit.frag never writes depth (keeps hidden-surface removal)");
    check(writesDepth(spv::lit_reflect_frag, spv::lit_reflect_frag_size), "heat: only the reflection shader moves depth to the street");
}

// ---------------------------------------------------------------- the scene set's bindings, read from the SPIR-V

// each named variable's (set, binding): OpName (5) gives the name, OpDecorate (71) DescriptorSet (34) / Binding (33)
struct Binding { int set = -1, binding = -1; };
std::map<std::string, Binding> bindings(const uint32_t* w, size_t bytes) {
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, Binding> deco;
    const size_t n = bytes / 4;
    for (size_t i = 5; i < n;) {
        const uint32_t op = w[i] & 0xffff, len = w[i] >> 16;
        if (len == 0) break;
        if (op == 5) names[w[i + 1]] = std::string(reinterpret_cast<const char*>(&w[i + 2]));
        if (op == 71 && len >= 4 && w[i + 2] == 34) deco[w[i + 1]].set = int(w[i + 3]);
        if (op == 71 && len >= 4 && w[i + 2] == 33) deco[w[i + 1]].binding = int(w[i + 3]);
        i += len;
    }
    std::map<std::string, Binding> out;
    for (const auto& [id, b] : deco) if (names.count(id) && b.binding >= 0) out[names[id]] = b;
    return out;
}

std::string source(const char* rel) {
    std::ifstream f(std::string(CS_SOURCE_DIR) + "/" + rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void sceneSet() {
    // both scene shaders: the droid's albedo and normal map at the bindings the renderer writes them to (6, 7), every
    // binding inside the set layout the pipeline creates, one texture per binding 1..7
    for (const auto& [name, words, bytes] : {std::tuple{"lit.frag", spv::lit_frag, spv::lit_frag_size},
                                              std::tuple{"lit_reflect.frag", spv::lit_reflect_frag, spv::lit_reflect_frag_size}}) {
        const auto b = bindings(words, bytes);
        const std::string n(name);
        check(b.count("tDroid") && b.at("tDroid").set == 0 && b.at("tDroid").binding == gpu::kDroidAlbedoBinding && b.count("tDroidN") &&
                  b.at("tDroidN").binding == gpu::kDroidNormalBinding,
              (n + ": the droid's textures at bindings 6 / 7 (where the renderer writes them)").c_str());
        bool inside = true;
        std::set<int> samplers;
        for (const auto& [var, bd] : b) {
            if (bd.set != 0) continue;
            inside = inside && bd.binding < gpu::kFrameSetBindings;
            if (bd.binding > 0) samplers.insert(bd.binding);
        }
        check(inside && int(samplers.size()) == gpu::kFrameSetSamplers, (n + ": every binding inside the scene set's layout, one texture each on 1..7").c_str());
    }
    check(gpu::descriptorPoolSamplers(2) == gpu::kStaticSetSamplers + 2 * gpu::kFrameSetSamplers && gpu::kFrameSetSamplers == 7,
          "the descriptor pool holds every scene set's 7 textures, per frame in flight, plus the other sets'");
    // the pattern id the game tags the droid with is the shader's
    std::smatch m;
    const std::string glsl = source("assets/shaders/lit_common.glsl");
    check(std::regex_search(glsl, m, std::regex(R"(P_DROID = (\d+))")) && std::stoi(m[1]) == int(Pattern::DroidTexture),
          "lit_common.glsl's P_DROID is Pattern::DroidTexture");
    std::smatch st, fl;
    check(std::regex_search(glsl, st, std::regex(R"(P_STONE = (\d+))")) && std::stoi(st[1]) == int(Pattern::Stone) &&
              std::regex_search(glsl, fl, std::regex(R"(P_TRON_FLOOR = (\d+))")) && std::stoi(fl[1]) == int(Pattern::TronFloor),
          "...and Neon Egypt's P_STONE / P_TRON_FLOOR are Pattern::Stone / TronFloor");
    // the stone pattern follows the mesh's own unit-space position (lit.vert passes it through)
    const std::string vert = source("assets/shaders/lit.vert");
    check(vert.find("out vec3 vLocal") != std::string::npos && glsl.find("in vec3 vLocal") != std::string::npos &&
              bindings(spv::lit_frag, spv::lit_frag_size).size() > 0,
          "lit.vert hands the fragment shaders the unit-space position (vLocal) the stone lines are measured in");
}

} // namespace

int main() {
    depthGuard();
    sceneSet();
    return finish("ShaderBlobs");
}
