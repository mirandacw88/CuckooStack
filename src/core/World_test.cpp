// The scenery (World.cpp): the street, tower rows, signs, traffic, rain, wind streaks, lamps and steam vents that
// follow the run. Checks that everything wraps around the camera seamlessly (the same amount of city wherever the
// hen is), rain and wind streaks switch with lite mode, vents follow the camera, and a seed gives the same city.
#include "TestUtil.h"
#include "core/Font.h"
#include "core/World.h"

using namespace cs;
using namespace test;

namespace {

size_t count(const RenderList& l, Pass p) {
    size_t n = 0;
    for (const auto& v : l.buckets[size_t(p)]) n += v.size();
    return n;
}
RenderList frame(World& w, float camX, float speed, bool playing, float dt = 1.f / 60, int frames = 1, const TextLayout* text = nullptr) {
    for (int i = 0; i < frames; ++i) w.update(dt, float(i) * dt, camX, 2.f, speed, playing);
    RenderList l;
    w.emit(l, camX, 2.f, 1.f, 0.f, glm::vec3(1.f), text);
    return l;
}

void wraps() {
    FastRandom rng(11);
    World w(rng);
    const RenderList start = frame(w, 0, 8, true);
    bool same = true, near = true;
    for (float camX : {37.f, 151.f, 977.f, 4321.f, 12000.5f}) {
        const RenderList l = frame(w, camX, 8, true);
        same = same && count(l, Pass::Lit) == count(start, Pass::Lit);
        // every lit object (towers, lamps, the street) stays in front of / around the camera, never left behind
        for (const auto& v : l.buckets[size_t(Pass::Lit)])
            for (const Instance& i : v) near = near && std::abs(i.model[3].x - camX) < 400.f;
    }
    check(same, "the same amount of city wherever the hen is (seamless wrapping)");
    check(near, "nothing is left behind: everything stays around the camera");
}

void vents() {
    FastRandom rng(3);
    World w(rng);
    bool around = true, four = true;
    for (float camX : {0.f, 25.f, 260.f, 3000.3f}) {
        w.update(1.f / 60, 0, camX, 2, 8, true);
        four = four && w.vents().size() == 4;
        for (const glm::vec3& v : w.vents()) around = around && v.x >= camX - 0.25f * 52 - 0.01f && v.x <= camX + 0.75f * 52 + 0.01f;
    }
    check(four, "four steam vents every frame");
    check(around, "vents wrap to stay around the camera (a 52 m period)");
}

void rainAndWind() {
    FastRandom rng(5);
    World w(rng);
    auto rain = [](const RenderList& l) { return l.get(Pass::UnlitAlpha, MeshId::Plane).size(); };
    auto wind = [](const RenderList& l) { return l.get(Pass::UnlitAdd, MeshId::Plane).size(); };
    const RenderList calm = frame(w, 0, 8, true, 1.f / 60, 30);
    const RenderList fast = frame(w, 100, 12, true, 1.f / 60, 90);
    check(rain(calm) >= 900, "rain: 900 streaks");
    check(wind(fast) >= wind(calm) + 40, "wind streaks once the run is fast (42 lines)");
    const RenderList title = frame(w, 200, 12, false, 1.f / 60, 120);
    check(wind(title) + 40 <= wind(fast), "no wind streaks when not playing (title, results)");
    w.setRain(false);
    const RenderList lite = frame(w, 300, 12, true, 1.f / 60, 90);
    check(rain(lite) + 800 <= rain(calm), "lite: no rain");
    check(wind(lite) + 40 <= wind(fast), "lite: no wind streaks");
    w.setRain(true);
    check(rain(frame(w, 400, 12, true)) >= 900, "rain comes back with full effects");
}

void deterministic() {
    FastRandom a(77), b(77), c(78);
    World wa(a), wb(b), wc(c);
    const RenderList la = frame(wa, 50, 9, true), lb = frame(wb, 50, 9, true), lc = frame(wc, 50, 9, true);
    bool same = true;
    for (size_t p = 0; p < kPassCount; ++p)
        for (size_t m = 0; m < kMeshCount; ++m) {
            same = same && la.buckets[p][m].size() == lb.buckets[p][m].size();
            for (size_t i = 0; same && i < la.buckets[p][m].size(); ++i) same = la.buckets[p][m][i].model == lb.buckets[p][m][i].model;
        }
    bool differs = false;
    for (size_t i = 0; i < std::min(la.get(Pass::Lit, MeshId::Box).size(), lc.get(Pass::Lit, MeshId::Box).size()); ++i)
        differs = differs || la.get(Pass::Lit, MeshId::Box)[i].model != lc.get(Pass::Lit, MeshId::Box)[i].model;
    check(same, "the same seed builds the same city");
    check(differs, "another seed, another skyline");
}

void signs() {
    FontAtlas fonts;
    check(fonts.build(), "fonts");
    TextLayout tl(fonts);
    FastRandom rng(9);
    World w(rng);
    const RenderList withText = frame(w, 0, 8, true, 1.f / 60, 1, &tl);
    const RenderList without = frame(w, 0, 8, true, 1.f / 60, 1, nullptr);
    check(!withText.worldText.empty() && without.worldText.empty(), "neon signs get their words when the font is ready (none without)");
}

} // namespace

int main() {
    wraps();
    vents();
    rainAndWind();
    deterministic();
    signs();
    return finish("World");
}
