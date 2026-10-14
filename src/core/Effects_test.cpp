// CPU effects (Effects.cpp): the particle system behind sparks, feathers, smoke and rain splashes (cap, lifetime,
// drag, gravity, size and alpha curves) and the pooled shockwave rings.
#include "TestUtil.h"
#include "core/Effects.h"

using namespace cs;
using namespace test;

namespace {

void particles() {
    ParticleSystem ps(3);
    for (int i = 0; i < 5; ++i) ps.emit({0, 0, 0}, {1, 0, 0}, 1.f, 0.1f, 0.3f, {1, 0, 0});
    check(ps.size() == 3, "the pool is capped (extra emits are dropped, never reallocating mid-frame)");
    ps.update(0.5f);
    std::vector<Particle> out;
    ps.emitTo(out);
    check(out.size() == 3 && std::abs(out[0].posSize.x - 0.5f) < 1e-5f, "moves by velocity x time (no drag)");
    check(std::abs(out[0].posSize.w - 0.2f) < 1e-5f, "size runs from s0 to s1 over its life (halfway: halfway)");
    ps.update(0.6f);
    check(ps.size() == 0, "removed once its life runs out");
    ps.emitTo(out);
    check(out.size() == 3, "emitTo appends to what's there");

    ParticleSystem fall(10);
    fall.emit({0, 10, 0}, {0, 0, 0}, 2.f, 1, 1, {1, 1, 1}, 1.f, 0.f, 9.8f);
    for (int i = 0; i < 60; ++i) fall.update(1.f / 60);
    std::vector<Particle> f;
    fall.emitTo(f);
    check(f[0].posSize.y < 10.f - 4.f && f[0].posSize.y > 10.f - 5.5f, "gravity: about g t^2 / 2 after a second");

    ParticleSystem drag(10);
    drag.emit({0, 0, 0}, {10, 0, 0}, 5.f, 1, 1, {1, 1, 1}, 1.f, 3.f);
    ParticleSystem none(10);
    none.emit({0, 0, 0}, {10, 0, 0}, 5.f, 1, 1, {1, 1, 1});
    for (int i = 0; i < 60; ++i) { drag.update(1.f / 60); none.update(1.f / 60); }
    std::vector<Particle> d, n;
    drag.emitTo(d); none.emitTo(n);
    check(d[0].posSize.x < n[0].posSize.x * 0.5f, "drag slows it down");

    // alpha: fades in over the first 6% of its life, then out to 0
    ParticleSystem a(10);
    a.emit({0, 0, 0}, {0, 0, 0}, 1.f, 1, 1, {1, 1, 1}, 0.8f);
    std::vector<Particle> born;
    a.emitTo(born);
    check(born[0].colorAlpha.a == 0.f, "a new particle starts invisible (no pop)");
    float peak = 0, last = 0, endA = 1;
    for (int i = 1; i <= 99; ++i) {
        a.update(0.01f);
        std::vector<Particle> p;
        a.emitTo(p);
        if (p.empty()) break;
        peak = std::max(peak, p[0].colorAlpha.a);
        endA = p[0].colorAlpha.a;
        if (i == 3) last = p[0].colorAlpha.a;
    }
    check(std::abs(peak - 0.8f) < 0.02f && last < peak, "alpha: fades in to its full alpha quickly");
    check(endA < 0.05f, "alpha: fades out by the end of its life");
    a.clear();
    check(a.size() == 0, "clear");
}

void rings() {
    Shockwaves w;
    w.spawn({1, 2, 3}, {0, 1, 1}, 4.f, 0.5f, true);
    w.spawn({0, 0, 0}, {1, 0, 1}, 2.f, 1.0f, false);
    RenderList list;
    w.emitTo(list);
    const auto& r = list.get(Pass::UnlitAdd, MeshId::RingFlat);
    check(r.size() == 2, "a ring per shockwave, additive");
    check(r[0].color.a == 1.f && glm::length(glm::vec3(r[0].model[0])) < 4.f * 0.2f, "it starts small and opaque");
    const glm::vec3 vNormal = glm::vec3(r[0].model * glm::vec4(0, 0, 1, 0)), fNormal = glm::vec3(r[1].model * glm::vec4(0, 0, 1, 0));
    check(std::abs(vNormal.z) > 0.9f * glm::length(vNormal) && std::abs(fNormal.y) > 0.9f * glm::length(fNormal), "vertical rings face the camera, flat ones lie on the floor");
    w.update(0.4f);
    list.clear();
    w.emitTo(list);
    check(list.get(Pass::UnlitAdd, MeshId::RingFlat)[0].color.a < 0.2f, "it fades as it grows");
    check(glm::length(glm::vec3(list.get(Pass::UnlitAdd, MeshId::RingFlat)[0].model[0])) > 3.f, "and grows toward its full size");
    w.update(0.2f);
    list.clear();
    w.emitTo(list);
    check(list.get(Pass::UnlitAdd, MeshId::RingFlat).size() == 1, "gone after its duration (the other lives on)");
    w.clear();
    list.clear();
    w.emitTo(list);
    check(list.get(Pass::UnlitAdd, MeshId::RingFlat).empty(), "clear");
}

} // namespace

int main() {
    particles();
    rings();
    return finish("Effects");
}
