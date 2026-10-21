// The scenery (World.cpp): the street, tower rows, signs, traffic, rain, wind streaks, lamps and steam vents that
// follow the run. Checks that everything wraps around the camera seamlessly (the same amount of city wherever the
// hen is), rain and wind streaks switch with lite mode, vents follow the camera, and a seed gives the same city.
// Neon Egypt (levels 11-20): the artist's sprites (EgyptBackdrop.h) in layers drawn back to front, every one of them
// coming by along a run, never a bare stretch, capstones lit, a canal and a plaza, alive (swaying, pulsing,
// twinkling), the same for everyone; its atlas decoded with clear gutters; a moon behind the ridge's silhouettes; gold
// dust blowing by, sparkling now and then; braziers on a Tron floor; no window towers, rain or vents; it wraps like the
// city, and switching zones is instant both ways.
#include "TestUtil.h"
#include "core/EgyptBackdrop.h"
#include "core/Font.h"
#include "core/HudImage.h"
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

// a backdrop sprite's id (EgyptBackdrop.h), from its atlas rect; -1 if it's none of them
int spriteOf(const Instance& i) {
    for (int s = 0; s < egypt::kSpriteCount; ++s) {
        const egypt::SpriteInfo& k = egypt::kSprites[s];
        if (std::abs(i.emissive.x - k.u0) < 1e-6f && std::abs(i.emissive.y - k.v0) < 1e-6f && std::abs(i.emissive.z - k.u1) < 1e-6f &&
            std::abs(i.emissive.w - k.v1) < 1e-6f)
            return s;
    }
    return -1;
}
bool standing(const Instance& i) { // (upright art, not a light or the ground)
    return i.params.y == 0.f && i.model[1].y > 0.7f * glm::length(glm::vec3(i.model[1]));
}

void egyptZone() {
    using namespace egypt;
    FastRandom rng(21);
    World w(rng);
    const RenderList city = frame(w, 120, 8, true, 1.f / 60, 2);
    w.setZone(Zone::Egypt);
    check(w.zone() == Zone::Egypt, "Egypt: the zone switches");
    const RenderList e = frame(w, 120, 8, true, 1.f / 60, 2);
    check(city.worldSprites.empty() && !e.worldSprites.empty(), "...its backdrop: the artist's sprites (the city has none)");
    bool atlas = true, lightsAdded = true;
    for (const Instance& i : e.worldSprites) {
        const int s = spriteOf(i);
        atlas = atlas && s >= 0;
        const bool isLight = s == GlowDot || s == GlowLine || s == GlowBeam || s == GlowRing;
        const bool cloud = s == GlowDot && i.params.y == 0.f && i.model[3].z < -170 && std::max({i.color.r, i.color.g, i.color.b}) < 0.05f; // (the moon's)
        lightsAdded = lightsAdded && (isLight && !cloud ? i.params.y == 1.f : true) && (standing(i) && !cloud ? !isLight : true);
    }
    check(atlas, "...every one of them from the atlas (EgyptBackdrop.h)");
    check(lightsAdded, "...its lights added in their place among the sprites, its art laid over what's behind");
    // back to front: the sky, the pyramids, the temples, the scarabs, the guardians and palms, then the canal and plaza
    auto band = [](float z) { return z < -120 ? 0 : z < -60 ? 1 : z < -25 ? 2 : 3; };
    int last = 0;
    bool ordered = true;
    for (const Instance& i : e.worldSprites)
        if (standing(i) || i.params.y == 1.f) { const int b = band(i.model[3].z); ordered = ordered && b >= last; last = std::max(last, b); }
    check(ordered, "...drawn back to front, layer by layer (the sky, Giza, the temples, the near layer)");
    // a long run past it: every piece of the artist's art comes by, nothing is ever bare, and a pyramid's capstone is lit
    std::vector<int> seen(kSpriteCount, 0);
    bool everFar = true, everMid = true, everNear = true, capstones = true, canal = true, plaza = true;
    for (float camX = 0; camX < 6000; camX += 23.f) {
        const RenderList l = frame(w, camX, 8, true);
        int far = 0, mid = 0, near = 0, caps = 0, pyramids = 0, water = 0, tiles = 0;
        for (const Instance& i : l.worldSprites) {
            const int s = spriteOf(i);
            if (s < 0) continue;
            ++seen[size_t(s)];
            const float z = i.model[3].z, dx = std::abs(i.model[3].x - camX);
            if (standing(i) && z < -60 && dx < 40) ++far;
            if (standing(i) && z < -25 && z >= -60 && dx < 25) ++mid;
            if (standing(i) && z >= -25 && dx < 16) ++near;
            if ((s == PyramidCircuit || s == PyramidPlain) && standing(i) && z > -120) ++pyramids; // (Giza's: the ridge is silhouettes)
            if (s == GlowDot && z < -60 && z > -120) ++caps;
            if (s == Solid && !standing(i) && z < -8 && z > -16) ++water;
            if (s >= TileA && s <= TileE) ++tiles;
        }
        everFar = everFar && far > 0; everMid = everMid && mid > 0; everNear = everNear && near > 0;
        capstones = capstones && caps >= pyramids;
        canal = canal && water == 1;
        plaza = plaza && tiles > 40;
    }
    bool all = true;
    for (int s = 0; s < kSpriteCount; ++s) all = all && seen[size_t(s)] > 0;
    check(all, "...along 6 km every sprite comes by (pyramids, gates, temples, obelisks, the sphinx, the watchtower, the gods, "
               "pharaohs, palms, busts, stelae, scarabs, tiles, lights)");
    check(everFar && everMid && everNear, "...and never a bare stretch: Giza, a temple and the near layer always in view");
    // obelisks are landmarks, not a crowd: on a phone held upright, three in view at the most, two or fewer nearly always
    {
        int most = 0, crowded = 0, frames = 0;
        for (float camX = 0; camX < 6000; camX += 4.f) {
            const RenderList l = frame(w, camX, 8, true);
            int n = 0;
            for (const Instance& i : l.worldSprites) {
                const int s = spriteOf(i);
                const bool obelisk = s == ObeliskGlyphs || s == ObeliskCyan || s == ObeliskGold || s == ObeliskSmallCyan || s == ObeliskSmallGold;
                n += obelisk && standing(i) && std::abs((i.model[3].x - camX) / (18.f - i.model[3].z)) < 0.24f; // (a portrait phone's width)
            }
            most = std::max(most, n);
            crowded += n > 2;
            ++frames;
        }
        check(most <= 3 && crowded * 20 < frames, "...obelisks landmarks, not a crowd: three in view at the most, two or fewer nearly always");
    }
    check(capstones, "...every one of Giza's pyramids with its capstone lit");
    // grounded: every piece of art stands on the ground (a pyramid seen 3/4 on at its foot line, the floor hiding the
    // rest), however high the camera's gone (the hen up on a wall)
    bool grounded = true;
    for (float camY : {2.f, 7.f}) {
        w.update(1.f / 60, 0.f, 500, camY, 8, true);
        RenderList l;
        w.emit(l, 500, camY, 0.f, 0.f, glm::vec3(1.f), nullptr);
        for (const Instance& i : l.worldSprites) {
            const int s = spriteOf(i);
            if (!standing(i) || s < 0 || (s >= ScarabGold && s <= ScarabGoldSmall) || s >= GlowDot) continue; // (art, not the sky's)
            grounded = grounded && std::abs((i.model * glm::vec4(0.f, -0.5f + kSprites[s].footY, 0.f, 1.f)).y) < 0.08f; // (its foot line)
        }
    }
    check(grounded, "...every piece standing on the ground (pyramids at their foot line), the camera low or high");
    check(canal && plaza, "...the canal and the plaza along the street, all the way");
    // it breathes: the same moment drawn twice is the same picture; a moment later the palms have swayed, the lights
    // pulsed, the stars twinkled
    frame(w, 300, 8, true);
    RenderList t0, t1, later;
    w.emit(t0, 300, 2.f, 1.f, 0.f, glm::vec3(1.f), nullptr);
    w.emit(t1, 300, 2.f, 1.f, 0.f, glm::vec3(1.f), nullptr);
    w.emit(later, 300, 2.f, 2.5f, 0.f, glm::vec3(1.f), nullptr);
    bool same = t0.worldSprites.size() == t1.worldSprites.size(), moved = false;
    for (size_t i = 0; same && i < t0.worldSprites.size(); ++i) same = t0.worldSprites[i].model == t1.worldSprites[i].model;
    for (size_t i = 0; i < std::min(t0.worldSprites.size(), later.worldSprites.size()); ++i)
        moved = moved || t0.worldSprites[i].model != later.worldSprites[i].model || t0.worldSprites[i].color != later.worldSprites[i].color;
    check(same && moved, "...alive: palms swaying, lights pulsing, stars twinkling, water shimmering (the same moment, the same picture)");
    check(lit(e, MeshId::Plane, Pattern::TronFloor) == 1 && lit(e, MeshId::Box, Pattern::Windows) == 0 && lit(city, MeshId::Box, Pattern::Windows) > 20,
          "...on an obsidian Tron floor (no window-lit towers: the city has them)");
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
    bool wraps = true;
    for (float camX : {37.f, 977.f, 12000.5f}) wraps = wraps && count(frame(w, camX, 8, true), Pass::Lit) == count(e, Pass::Lit);
    check(wraps, "...its street wrapping seamlessly like the city's");
    // the backdrop is laid out from its own seed: the same Egypt whatever the game's random stream did before
    FastRandom other(999);
    World w2(other);
    w2.setZone(Zone::Egypt);
    const RenderList e2 = frame(w2, 120, 8, true, 1.f / 60, 2);
    auto art = [](const RenderList& l) { // (its standing art; the scarabs fly the game's own traffic lanes)
        std::vector<std::pair<int, glm::mat4>> v;
        for (const Instance& i : l.worldSprites)
            if (const int s = spriteOf(i); standing(i) && (s < ScarabGold || s > ScarabGoldSmall)) v.push_back({s, i.model});
        return v;
    };
    check(!art(e).empty() && art(e) == art(e2), "...the same backdrop for everyone (its own seed)");
    // back to the city: exactly the city again
    w.setZone(Zone::City);
    const RenderList back = frame(w, 120, 8, true, 1.f / 60, 2);
    bool cityAgain = back.worldSprites.empty();
    for (size_t p = 0; p < kPassCount; ++p)
        for (size_t m = 0; m < kMeshCount; ++m) cityAgain = cityAgain && back.buckets[p][m].size() == city.buckets[p][m].size();
    check(cityAgain && w.vents().size() == 4, "back to the city: the same scene as before (instant both ways)");
}

void moonAndSilhouettes() {
    // Neon Egypt's moon: one, high in the sky behind the ridge, keeping its place on screen as she runs; its halo; the sky
    // shader's small moon isn't drawn there (Game: its direction points down). The ridge: silhouettes, everything else not
    using namespace egypt;
    FastRandom rng(51);
    World w(rng);
    check(frame(w, 300, 8, true).worldSprites.empty(), "the city: no moon of the backdrop's");
    w.setZone(Zone::Egypt);
    bool one = true, high = true, keeps = true, haloed = true, silhouettes = true, others = true, behind = true;
    float offset = 0;
    for (float camX : {300.f, 1300.f, 4321.f}) {
        const RenderList l = frame(w, camX, 8, true);
        int moons = 0, firstRidge = -1, moonAt = -1;
        bool halo = false;
        for (size_t i = 0; i < l.worldSprites.size(); ++i) {
            const Instance& q = l.worldSprites[i];
            const int s = spriteOf(q);
            const glm::vec3 c = glm::vec3(q.model[3]);
            if (s == MoonFace) {
                ++moons;
                moonAt = int(i);
                high = high && c.y > 25 && c.z < -170;
                if (camX == 300.f) offset = c.x - camX;
                keeps = keeps && std::abs(c.x - camX - offset) < 1e-3f;
            }
            if (s == GlowDot && q.params.y == 1.f && c.z < -170 && glm::length(glm::vec3(q.model[0])) > 40) halo = true;
            const bool ridge = c.z < -140 && c.z > -165 && (s == PyramidCircuit || s == PyramidPlain);
            if (ridge) { silhouettes = silhouettes && q.params.z == 1.f && q.rim.a == 1.f; if (firstRidge < 0) firstRidge = int(i); }
            else others = others && q.params.z == 0.f;
        }
        one = one && moons == 1;
        haloed = haloed && halo;
        behind = behind && moonAt >= 0 && firstRidge > moonAt;
    }
    check(one && high && keeps && haloed, "Egypt: a glowing moon high in the sky, haloed, keeping its place on screen as she runs");
    check(behind, "...behind the ridge (the pyramids slide past it)");
    check(silhouettes && others, "...the ridge's pyramids silhouettes against the night; nothing else is");
}

void goldDust() {
    // Neon Egypt's gold dust: warm specks blowing by on the wind, all round the camera wherever it is, glinting; none in
    // the city; lite mode keeps a quarter of it
    auto specks = [](const RenderList& l) {
        std::vector<Particle> v;
        for (const Particle& p : l.particlesAdd)
            if (p.colorAlpha.r > p.colorAlpha.g && p.colorAlpha.g > p.colorAlpha.b && p.posSize.w < 0.6f) v.push_back(p);
        return v;
    };
    FastRandom rng(41);
    World w(rng);
    check(specks(frame(w, 200, 8, true, 1.f / 60, 2)).empty(), "gold dust: none in the city");
    w.setZone(Zone::Egypt);
    const std::vector<Particle> a = specks(frame(w, 200, 8, true, 1.f / 60, 2));
    check(a.size() >= 300, "Egypt: gold dust in the air, hundreds of specks");
    bool around = true;
    for (float camX : {900.f, 5000.f, 12000.f}) {
        const std::vector<Particle> s = specks(frame(w, camX, 8, true, 1.f / 60, 30));
        int near = 0;
        for (const Particle& p : s) near += std::abs(p.posSize.x - camX) < 20;
        around = around && near >= 100;
    }
    check(around, "...all round the camera, however far it's run (blown out of sight, it comes again ahead)");
    // blown along: a while later the specks have moved against the run, some much further (gusts, their own pace)
    FastRandom rng2(41);
    World w2(rng2);
    w2.setZone(Zone::Egypt);
    const std::vector<Particle> b0 = specks(frame(w2, 200, 8, true, 1.f / 60, 2));
    const std::vector<Particle> b1 = specks(frame(w2, 200, 8, true, 1.f / 60, 60));
    double dx = 0;
    size_t n = 0;
    float brightest = 0, typical = 0;
    for (size_t i = 0; i < std::min(b0.size(), b1.size()); ++i)
        if (std::abs(b1[i].posSize.x - b0[i].posSize.x) < 10) { dx += b1[i].posSize.x - b0[i].posSize.x; ++n; }
    for (const Particle& p : b1) { brightest = std::max(brightest, p.colorAlpha.r); typical += p.colorAlpha.r; }
    typical /= float(std::max<size_t>(1, b1.size()));
    check(n > 0 && dx / double(n) < -0.5, "...blown along by the desert wind");
    check(brightest > 2.f * typical, "...glinting: now and then a speck catches the light, far brighter");
    // ...and, at a glint's height, a small sparkle: one in twenty specks or fewer at any moment (most of them off a
    // phone's screen), never a crowd; noticeable but subtle (10 s of a run)
    size_t most = 0, frames = 0;
    for (int f = 0; f < 600; ++f) {
        const float t = 3.f + float(f) / 60.f, camX = 200.f + float(f) * 0.2f;
        w2.update(1.f / 60, t, camX, 2.f, 8, true);
        RenderList l;
        w2.emit(l, camX, 2.f, t, 0.f, glm::vec3(1.f), nullptr);
        most = std::max(most, l.particlesSparkle.size());
        frames += !l.particlesSparkle.empty();
    }
    check(frames > 300 && most > 0 && most <= b1.size() / 20, "...a small sparkle now and then as one catches the light: a few at a time at most");
    w2.setRain(false);
    const size_t lite = specks(frame(w2, 200, 8, true)).size();
    check(lite > 0 && lite <= b1.size() / 3, "...lite mode keeps a quarter of it");
}

void backdropArt() {
    // the atlas behind the sprites: decoded and premultiplied, its mips; every sprite's rect holds its art, its gutter
    // clear (no sprite bleeds into another down the mips)
    using namespace egypt;
    HudImage art;
    check(art.buildEgyptBackdrop() && art.width() == kAtlasW && art.height() == kAtlasH && art.mips().size() == 5,
          "Egypt's backdrop atlas decodes: 4096 x 2048, five smaller levels");
    const auto& px = art.pixels();
    auto at = [&](int x, int y) { return px.data() + (size_t(y) * size_t(kAtlasW) + size_t(x)) * 4; };
    bool premul = true;
    for (size_t i = 0; i < px.size(); i += 4 * 997) premul = premul && px[i] <= px[i + 3] && px[i + 1] <= px[i + 3] && px[i + 2] <= px[i + 3];
    check(premul, "...premultiplied (no colour brighter than its alpha)");
    bool filled = true, gutters = true, aspects = true;
    for (int s = 0; s < kSpriteCount; ++s) {
        const SpriteInfo& k = kSprites[s];
        const int x0 = int(std::lround(k.u0 * kAtlasW)), y0 = int(std::lround(k.v0 * kAtlasH)), x1 = int(std::lround(k.u1 * kAtlasW)),
                  y1 = int(std::lround(k.v1 * kAtlasH));
        aspects = aspects && std::abs(float(x1 - x0) / float(y1 - y0) - k.aspect) < 0.01f;
        int opaque = 0;
        for (int y = y0; y < y1; y += 3)
            for (int x = x0; x < x1; x += 3) opaque += at(x, y)[3] > 200;
        filled = filled && opaque > 0;
        for (int x = x0 - 8; x < x1 + 8; ++x) gutters = gutters && at(x, y0 - 8)[3] == 0 && at(x, y1 + 7)[3] == 0;
        for (int y = y0 - 8; y < y1 + 8; ++y) gutters = gutters && at(x0 - 8, y)[3] == 0 && at(x1 + 7, y)[3] == 0;
    }
    check(filled && aspects, "...every sprite's art where its rect says, at the aspect the table gives");
    check(gutters, "...each in a clear gutter (none bleeds into another as the mips shrink)");
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
    egyptZone();
    moonAndSilhouettes();
    goldDust();
    backdropArt();
    japan();
    return finish("World");
}
