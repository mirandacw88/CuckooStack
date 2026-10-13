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

void basics() {
    check(sizeof(Instance) == 128 && sizeof(Particle) == 32, "GPU layouts: Instance 128 bytes, Particle 32");
    RenderList list;
    Instance& a = list.add(Pass::UnlitAdd, MeshId::Plane);
    a.color = glm::vec4(1, 0, 0, 0.5f);
    list.add(Pass::UnlitAdd, MeshId::Plane);
    list.add(Pass::Lit, MeshId::Box);
    check(list.get(Pass::UnlitAdd, MeshId::Plane).size() == 2 && list.get(Pass::Lit, MeshId::Box).size() == 1, "add: one bucket per pass and mesh");
    check(list.get(Pass::UnlitAdd, MeshId::Plane)[0].color.a == 0.5f, "add: returns the new instance to fill in");
    check(list.get(Pass::Lit, MeshId::Plane).empty() && list.get(Pass::UnlitAlpha, MeshId::Box).empty(), "other buckets stay empty");
    list.hud.emplace_back(); list.hudKind.push_back(HudText);
    list.particlesAdd.emplace_back(); list.particlesSmoke.emplace_back(); list.worldText.emplace_back(); list.heroVerts.emplace_back();
    list.frame.lite = true;
    list.clear();
    bool empty = list.hud.empty() && list.hudKind.empty() && list.particlesAdd.empty() && list.particlesSmoke.empty() &&
                 list.worldText.empty() && list.heroVerts.empty();
    for (size_t p = 0; p < kPassCount; ++p)
        for (size_t m = 0; m < kMeshCount; ++m) empty = empty && list.buckets[p][m].empty();
    check(empty, "clear: every bucket, the HUD, particles, world text and the posed hen");
    check(list.frame.lite, "clear: the frame parameters are left for the game to set");
    const RenderList::LitMark m = list.litMark();
    list.add(Pass::UnlitAlpha, MeshId::Egg);
    list.reflectSince(m);
    check(list.get(Pass::Reflect, MeshId::Egg).empty(), "only lit instances are reflected (not glows or particles)");
}

} // namespace

int main() {
    mirroring();
    basics();
    return finish("RenderList");
}
