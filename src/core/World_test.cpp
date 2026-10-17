// The scenery (World.cpp): the street, tower rows, signs, traffic, rain, wind streaks, lamps and steam vents that
// follow the run. Checks that everything wraps around the camera seamlessly (the same amount of city wherever the
// hen is), rain and wind streaks switch with lite mode, vents follow the camera, and a seed gives the same city.
// Neon Egypt (levels 11-20): pyramids, obelisks, pylons, cartouches, the sun disc, barques and braziers on a Tron
// floor; no window towers, rain or vents; it wraps like the city, and switching zones is instant both ways.
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

size_t lit(const RenderList& l, MeshId m, Pattern p) {
    size_t n = 0;
    for (const Instance& i : l.get(Pass::Lit, m)) n += int(i.params.z + 0.5f) == int(p) && i.params.x >= 0;
    return n;
}

void egypt() {
    FastRandom rng(21);
    World w(rng);
    const RenderList city = frame(w, 120, 8, true, 1.f / 60, 2);
    w.setZone(Zone::Egypt);
    check(w.zone() == Zone::Egypt, "Egypt: the zone switches");
    const RenderList e = frame(w, 120, 8, true, 1.f / 60, 2);
    check(lit(e, MeshId::Pyramid, Pattern::Stone) >= 6, "...pyramids of glowing-edged stone");
    check(lit(e, MeshId::Frustum, Pattern::Stone) >= 10, "...obelisks, temple pylons, brazier columns and bollards");
    check(lit(e, MeshId::Box, Pattern::Windows) == 0 && lit(city, MeshId::Box, Pattern::Windows) > 20, "...and no window-lit towers (the city has them)");
    check(lit(e, MeshId::Plane, Pattern::TronFloor) == 1 && lit(e, MeshId::Plane, Pattern::Asphalt) == 0, "...an obsidian Tron floor instead of wet asphalt");
    check(!e.get(Pass::UnlitAdd, MeshId::RingFlat).empty() && !e.get(Pass::UnlitAdd, MeshId::Circle).empty(), "...cartouches with glowing icons");
    size_t sun = 0;
    for (const Instance& i : e.get(Pass::UnlitAdd, MeshId::Plane)) sun += std::abs(i.model[3].z + 96.f) < 0.01f;
    check(sun >= 30, "...the striped sun disc of Ra behind the pyramids");
    check(!e.get(Pass::Lit, MeshId::Cone).empty(), "...brazier bowls");
    check(w.vents().empty(), "...no steam vents in the desert");
    // weather: sand drifting sideways instead of rain falling
    size_t rainLike = 0, sandLike = 0;
    for (const Instance& i : e.get(Pass::UnlitAlpha, MeshId::Plane)) {
        const float sx = glm::length(glm::vec3(i.model[0])), sy = glm::length(glm::vec3(i.model[1]));
        rainLike += sy > 0.3f && sx < 0.03f;
        sandLike += sx > 0.15f && sy < 0.03f;
    }
    check(rainLike == 0 && sandLike >= 800, "...no rain: fine sand drifting on the wind");
    bool same = true;
    for (float camX : {37.f, 977.f, 12000.5f}) same = same && count(frame(w, camX, 8, true), Pass::Lit) == count(e, Pass::Lit);
    check(same, "...wrapping seamlessly like the city");
    // the skyline is laid out from its own seed: the same Egypt whatever the game's random stream did before
    FastRandom other(999);
    World w2(other);
    w2.setZone(Zone::Egypt);
    const RenderList e2 = frame(w2, 120, 8, true, 1.f / 60, 2);
    bool sameSky = e.get(Pass::Lit, MeshId::Pyramid).size() == e2.get(Pass::Lit, MeshId::Pyramid).size();
    for (size_t i = 0; sameSky && i < e.get(Pass::Lit, MeshId::Pyramid).size(); ++i)
        sameSky = e.get(Pass::Lit, MeshId::Pyramid)[i].model == e2.get(Pass::Lit, MeshId::Pyramid)[i].model;
    check(sameSky, "...the same skyline for everyone (its own seed)");
    // back to the city: exactly the city again
    w.setZone(Zone::City);
    const RenderList back = frame(w, 120, 8, true, 1.f / 60, 2);
    bool cityAgain = true;
    for (size_t p = 0; p < kPassCount; ++p)
        for (size_t m = 0; m < kMeshCount; ++m) cityAgain = cityAgain && back.buckets[p][m].size() == city.buckets[p][m].size();
    check(cityAgain && w.vents().size() == 4, "back to the city: the same scene as before (instant both ways)");
}

void japan() {
    FastRandom rng(31);
    World w(rng);
    const RenderList city = frame(w, 120, 8, true, 1.f / 60, 2);
    w.setZone(Zone::Japan);
    const RenderList j = frame(w, 120, 8, true, 1.f / 60, 2);
    check(lit(j, MeshId::Box, Pattern::Windows) == 0 && lit(j, MeshId::Pyramid, Pattern::Stone) >= 4, "Japan: no towers; neon-edged mountains");
    check(lit(j, MeshId::Frustum, Pattern::Stone) >= 6, "...pagoda roofs and stone lanterns");
    check(lit(j, MeshId::Plane, Pattern::TronFloor) == 1, "...an obsidian floor under a glowing grid");
    size_t floorPink = 0;
    for (const Instance& i : j.get(Pass::Lit, MeshId::Plane)) floorPink += int(i.params.z + 0.5f) == int(Pattern::TronFloor) && i.rim.r > i.rim.g && i.rim.w == 0.f;
    check(floorPink == 1, "...its grid pink and vermilion (no rim glow)");
    size_t bloom = 0;
    for (const Instance& i : j.get(Pass::Lit, MeshId::Sphere)) bloom += i.params.x < 0 && i.emissive.r > i.emissive.g && i.emissive.b > i.emissive.g;
    check(bloom >= 30, "...cherry trees in glowing pink bloom");
    check(!j.get(Pass::UnlitAdd, MeshId::Circle).empty() && !j.get(Pass::UnlitAdd, MeshId::RingFlat).empty(), "...a full moon with a neon rim");
    check(w.vents().empty(), "...no steam vents");
    // the blossoms: hundreds of small pink petals (no rain streaks), tumbling and blown sideways
    auto petals = [](const RenderList& l) {
        std::vector<glm::vec3> p;
        for (const Instance& i : l.get(Pass::UnlitAlpha, MeshId::Plane))
            if (int(i.params.z + 0.5f) == int(Shape::SoftDisc) && i.color.r > 0.9f && i.color.g < i.color.r && glm::length(glm::vec3(i.model[0])) < 0.3f)
                p.push_back(glm::vec3(i.model[3]));
        return p;
    };
    const std::vector<glm::vec3> a = petals(j);
    check(a.size() >= 800, "...cherry blossoms throughout (hundreds of petals)");
    const RenderList j2 = frame(w, 120, 8, true, 1.f / 60, 60);
    const std::vector<glm::vec3> b = petals(j2);
    double dx = 0, dy = 0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) { if (std::abs(b[i].x - a[i].x) < 5 && std::abs(b[i].y - a[i].y) < 5) { dx += b[i].x - a[i].x; dy += b[i].y - a[i].y; } }
    check(n > 0 && dx / n < -0.5 && dy / n < -0.2, "...falling slowly and blown along by the wind");
    // gusts: over a while the wind picks up (petals move much faster at times)
    float slow = 1e9f, fast = 0;
    std::vector<glm::vec3> prev = b;
    for (int s = 0; s < 40; ++s) {
        const std::vector<glm::vec3> c = petals(frame(w, 120, 8, false, 1.f / 60, 15));
        double v = 0; size_t m = 0;
        for (size_t i = 0; i < std::min(c.size(), prev.size()); ++i) if (std::abs(c[i].x - prev[i].x) < 4) { v += prev[i].x - c[i].x; ++m; }
        if (m) { slow = std::min(slow, float(v / m)); fast = std::max(fast, float(v / m)); }
        prev = c;
    }
    check(fast > 2.f * slow, "...in gusts: at times the wind blows them along much faster");
    // lite mode keeps a quarter of them (they're the zone's signature)
    w.setRain(false);
    const size_t lite = petals(frame(w, 120, 8, true)).size();
    check(lite >= 200 && lite <= 260, "...lite mode: a quarter of the petals, not none");
    w.setRain(true);
    bool same = true;
    for (float camX : {37.f, 977.f, 12000.5f}) same = same && count(frame(w, camX, 8, true), Pass::Lit) == count(j, Pass::Lit);
    check(same, "...wrapping seamlessly");
    w.setZone(Zone::City);
    const RenderList back = frame(w, 120, 8, true, 1.f / 60, 2);
    bool cityAgain = true;
    for (size_t p = 0; p < kPassCount; ++p)
        for (size_t m = 0; m < kMeshCount; ++m) cityAgain = cityAgain && back.buckets[p][m].size() == city.buckets[p][m].size();
    check(cityAgain, "back to the city: the same scene as before");
}

} // namespace

int main() {
    wraps();
    vents();
    rainAndWind();
    deterministic();
    signs();
    egypt();
    japan();
    return finish("World");
}
