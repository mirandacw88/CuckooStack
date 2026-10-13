// The render list (RenderList.h): the wet street's reflection mirroring (litMark / reflectSince), one- and
// two-sided instances.
#include "TestUtil.h"
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

// ---------------------------------------------------------------- the reflection's mirroring

void mirroring() {
    RenderList list;
    list.add(Pass::Lit, MeshId::Box).model = glm::translate(glm::mat4(1.f), glm::vec3(1, 2, 3)); // before the mark
    const auto mark = list.litMark();
    list.add(Pass::Lit, MeshId::Egg).model = glm::translate(glm::mat4(1.f), glm::vec3(4, 1.5f, -1));
    Instance& tinted = list.add(Pass::Lit, MeshId::Egg);
    tinted.model = glm::translate(glm::mat4(1.f), glm::vec3(0, 3, 0));
    tinted.color = glm::vec4(0.2f, 0.4f, 0.6f, 1.f);
    list.add(Pass::LitTwoSided, MeshId::HenBody).model = glm::translate(glm::mat4(1.f), glm::vec3(0, 0.8f, 0)); // shards
    list.add(Pass::UnlitAdd, MeshId::Plane);                                                                     // not lit
    list.reflectSince(mark);
    const auto& refl = list.buckets[size_t(Pass::Reflect)];
    check(refl[size_t(MeshId::Box)].empty(), "mirror: instances added before the mark aren't reflected");
    check(refl[size_t(MeshId::Egg)].size() == 2, "mirror: lit instances since the mark are reflected");
    check(refl[size_t(MeshId::HenBody)].size() == 1, "mirror: two-sided instances too (the crash's glass shards)");
    check(refl[size_t(MeshId::Plane)].empty(), "mirror: unlit effects aren't");
    const glm::vec4 p = refl[size_t(MeshId::Egg)][0].model * glm::vec4(0, 0, 0, 1);
    check(std::abs(p.x - 4) < 1e-5f && std::abs(p.y + 1.5f) < 1e-5f && std::abs(p.z + 1) < 1e-5f, "mirror: flipped about the street (y -> -y)");
    const glm::vec4 up = refl[size_t(MeshId::Egg)][1].model * glm::vec4(0, 1, 0, 0);
    check(up.y < 0, "mirror: upside down");
    check(refl[size_t(MeshId::Egg)][1].color == tinted.color, "mirror: the material is kept");
    check(list.buckets[size_t(Pass::Lit)][size_t(MeshId::Egg)].size() == 2, "mirror: the originals are untouched");
    list.clear();
    check(list.buckets[size_t(Pass::Reflect)][size_t(MeshId::Egg)].empty(), "mirror: cleared with the frame");
}

} // namespace

int main() {
    mirroring();
    return finish("RenderList");
}
