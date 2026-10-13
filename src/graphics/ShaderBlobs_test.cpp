// The compiled shaders (ShaderBlobs.h): a guard on the heat fix - only the reflection shader writes depth (writing it
// in lit.frag disables the GPU's hidden-surface removal for every solid object).
#include "TestUtil.h"
#include "graphics/ShaderBlobs.h"
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

} // namespace

int main() {
    depthGuard();
    return finish("ShaderBlobs");
}
