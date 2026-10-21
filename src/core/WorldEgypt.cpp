// Neon Egypt's backdrop (World.h): the artist's neon line art (assets/backdrop, EgyptBackdrop.h) laid out after the
// demo (assets/backdrop/egypt/source/egypt_mockup.jpg) in parallax layers, each wrapping at its own long period, so the
// same stretch comes round only every few hundred metres and never in step with another layer.
//   - The sky: constellations traced in cyan, twinkling, over the shader's star field; and the moon, glowing, a neon
//     ring round it, clouds drifting across it, its light glittering on the canal.
//   - The ridge: pyramids shoulder to shoulder along the horizon, silhouettes against the night (the demo's far ones),
//     their edges catching the moonlight; they slide past the moon as she runs.
//   - Giza: pyramids in clusters, the great circuit-traced ones and plain dark ones, the sphinx at their feet; every
//     capstone lit, the great ones sweeping a beam across the sky. Always one in view.
//   - Temples: a pylon gate between carved obelisks (its doorway glowing), a columned hall guarded by pharaohs, the
//     sphinx's court, a watchtower signalling, the great obelisk between pharaohs, a lone gate among palms; wide and
//     tall in turn, with tall palms and the odd distant temple behind them. Obelisks are landmarks: two or three in
//     view at the most, often none.
//   - Near: Anubis and Horus standing guard (facing each other across a stela, as in the demo), palm groves, shrines of
//     small obelisks and busts, pharaohs, in turn, with bamboo behind now and then; palms sway.
//   - The canal: a band of water shimmering cyan between gold banks.
//   - The plaza: dark stone slabs with cyan seams, between the street's rail and the canal.
//   - Scarabs fly over the temples.
// The further a layer, the more it fades into the night's haze and the slower it scrolls. Everything stands on the
// ground (a pyramid seen 3/4 on at its foot line, where its base's outer corners meet the ground, the floor hiding the
// rest). The neon lines breathe. All of it is one list drawn back to front (RenderList::worldSprites); its lights are
// added in their place among the sprites (a light behind a pyramid stays behind it).
#include "EgyptBackdrop.h"
#include "World.h"

#include <algorithm>

namespace cs {

namespace {
using namespace egypt;
const glm::vec3& nightHaze() { static const glm::vec3 h = hexColor("#0b2234"); return h; } // (the grade's horizon)
float aspect(uint8_t s) { return kSprites[s].aspect; } // a sprite's width over its height

// the plaza and the canal: true depth (the street's own perspective), between the rail and the near layer
constexpr float kPlazaNear = -4.8f, kPlazaFar = -8.2f, kCanalNear = -8.4f, kCanalFar = -15.f, kTile = 2.f;
constexpr float kScarabZ = -30.f;
constexpr float kMoonR = 11.f; // the moon's radius (m: it's far off, behind the ridge)
enum : size_t { kRidge, kFar, kMid, kNear, kEgyptLayers }; // (World::egypt_, far to near)

Instance quad(uint8_t id, const glm::mat4& model, const glm::vec4& color, float haze, float neon, float add) {
    const SpriteInfo& s = kSprites[id];
    Instance i;
    i.model = model;
    i.color = color;
    i.emissive = {s.u0, s.v0, s.u1, s.v1};
    i.rim = glm::vec4(nightHaze(), haze);
    i.params = {neon, add, 0.f, 0.f};
    return i;
}
} // namespace

void World::buildEgypt() {
    FastRandom r(0xE6E1u); // (its own seed: the same Egypt for everyone, whatever the game's random stream did)
    auto put = [](Layer& l, uint8_t s, float x, float h, float dz = 0.f, bool flip = false) { l.props.push_back({s, x, h, dz, flip, 0.f}); };
    // A layer: vignettes picked in turn, with gaps between, till the period's full; then what's left over is shared out
    // among the gaps, so the one across the wrap is no longer than the rest. Balance: never the same vignette twice
    // running, never two tall ones running (tall: a vignette's class 1), never three wide ones
    auto fill = [&](Layer& l, float margin, float gap0, float gap1, int kinds, const int* tall, auto&& vignette) {
        const float first = r.range(gap0, gap1) / 2 + std::max(0.f, -gap0);
        float x = first, end = first;
        int last = -1, talls = 0, wides = 0;
        std::vector<size_t> starts;
        while (true) {
            int k = r.index(kinds);
            for (int tries = 0; tries < kinds * 2; ++tries, k = (k + 1) % kinds) {
                const bool t = tall && tall[k];
                if (k != last && !(t && talls >= 1) && !(!t && tall && wides >= 2)) break;
            }
            const size_t before = l.props.size();
            const float w = vignette(k, x);
            if (x + w > l.P - margin) { l.props.resize(before); break; }
            starts.push_back(before);
            last = k;
            const bool t = tall && tall[k];
            talls = t ? talls + 1 : 0;
            wides = t ? 0 : wides + 1;
            end = x + w;
            x = end + r.range(gap0, gap1);
        }
        const float slack = (l.P - end + first) - (gap0 + gap1) / 2; // (the wrap's gap, over an everyday one)
        for (size_t v = 0; v < starts.size(); ++v)
            for (size_t i = starts[v]; i < (v + 1 < starts.size() ? starts[v + 1] : l.props.size()); ++i)
                l.props[i].x += slack * float(v) / float(starts.size());
        for (Prop& p : l.props) p.seed = r.next01();
        std::stable_sort(l.props.begin(), l.props.end(), [](const Prop& a, const Prop& b) { return a.dz < b.dz; }); // (far first)
    };
    egypt_.assign(kEgyptLayers, Layer{});
    auto width = [](uint8_t s, float h) { return h * aspect(s); };

    // the ridge: pyramids shoulder to shoulder along the horizon, silhouettes against the night (the demo's far ones)
    Layer& ridge = egypt_[kRidge] = {-150, 900, 0.72f, 0.34f, 0.62f, 0.45f, {}};
    ridge.sil = 1;
    fill(ridge, 30, -16, 2, 3, nullptr, [&](int k, float x) -> float {
        const bool m = r.next01() < 0.5f;
        if (k == 2) { // two, the smaller in front
            const float h1 = r.range(30, 38), h2 = r.range(20, 27);
            put(ridge, PyramidPlain, x + width(PyramidPlain, h1) / 2, h1, 0, m);
            put(ridge, PyramidPlain, x + width(PyramidPlain, h1) * 0.85f, h2, 4, !m);
            return width(PyramidPlain, h1) * 0.85f + width(PyramidPlain, h2) / 2;
        }
        const uint8_t s = k == 0 ? PyramidPlain : PyramidCircuit;
        const float h = k == 0 ? r.range(28, 40) : r.range(34, 46);
        put(ridge, s, x + width(s, h) / 2, h, 0, m);
        return width(s, h);
    });

    // Giza: pyramids in clusters, the sphinx at their feet, close together (always one in view)
    Layer& far = egypt_[kFar] = {-88, 640, 0.5f, 0.82f, 0.28f, 1.1f, {}};
    fill(far, 20, 4, 14, 5, nullptr, [&](int k, float x) -> float {
        const bool m = r.next01() < 0.5f;
        switch (k) {
        case 0: // the great one with two smaller in front of it
            put(far, PyramidCircuit, x + 20, r.range(28, 32), 0, m);
            put(far, PyramidPlain, x + 46, r.range(18, 21), 6);
            put(far, PyramidPlain, x + 63, r.range(11, 13), 12, !m);
            return 74;
        case 1: // twins
            put(far, PyramidPlain, x + 15, r.range(20, 23), 4, m);
            put(far, PyramidCircuit, x + 38, r.range(25, 28), -4);
            return 56;
        case 2: // a lone great pyramid, the sphinx before it
            put(far, PyramidCircuit, x + 23, r.range(32, 35), 0, m);
            put(far, Sphinx, x + 30, r.range(5, 6), 18, r.next01() < 0.5f);
            return 47;
        case 3: // the sphinx between two
            put(far, PyramidPlain, x + 12, r.range(15, 17), 0, m);
            put(far, Sphinx, x + 26, r.range(5.5f, 6.5f), 14);
            put(far, PyramidCircuit, x + 42, r.range(23, 26), -2, !m);
            return 58;
        default: // three in a row, rising
            put(far, PyramidPlain, x + 9, r.range(12, 14), 6, m);
            put(far, PyramidPlain, x + 24, r.range(17, 19), 3, m);
            put(far, PyramidCircuit, x + 44, r.range(26, 29), 0, m);
            return 63;
        }
    });

    // temples: the vignettes in front, wide and tall in turn, close together; behind them tall palms and the odd distant
    // temple (the ridge keeps the skyline whole). Obelisks are landmarks, never a crowd: one or two to a vignette
    Layer& mid = egypt_[kMid] = {-42, 520, 0.3f, 0.74f, 0.22f, 1.f, {}};
    static const int midTall[] = {0, 0, 0, 1, 1, 0};
    int greats = 0; // (the great obelisks: carved and plain in turn)
    fill(mid, 14, 2, 7, 6, midTall, [&](int k, float x) -> float {
        switch (k) {
        case 0: // a pylon gate between carved obelisks
            put(mid, ObeliskGlyphs, x + 1.5f, 15, 1);
            put(mid, PylonGate, x + 13, 11);
            put(mid, ObeliskGlyphs, x + 24.5f, 15, 1, true);
            return 26;
        case 1: // the columned hall, a pharaoh at either end
            put(mid, PharaohViolet, x + 0.8f, 4.8f, 2);
            put(mid, Temple, x + 10.5f, 9);
            put(mid, PharaohViolet, x + 20.2f, 4.8f, 2, true);
            return 21;
        case 2: { // the sphinx's court: a gold obelisk at one end, a palm at the other
            const bool left = r.next01() < 0.5f;
            put(mid, ObeliskGold, x + (left ? 1.6f : 20.4f), 12, 0, !left);
            put(mid, Sphinx, x + 11, 6.5f, 2, r.next01() < 0.5f);
            put(mid, PalmTall, x + (left ? 20.f : 2.f), 9, 1, left);
            return 22;
        }
        case 3: // a watchtower signalling, bamboo and a palm at its foot
            put(mid, Bamboo, x + 1.2f, 5, 2);
            put(mid, Watchtower, x + 5, 13);
            put(mid, PalmTall, x + 9.5f, 9, 1, r.next01() < 0.5f);
            return 12;
        case 4: // the great obelisk, a pharaoh either side of it
            put(mid, Pharaoh, x + 1, 5.5f, 1.5f);
            put(mid, greats++ % 2 ? ObeliskCyan : ObeliskGlyphs, x + 4.6f, 17);
            put(mid, Pharaoh, x + 8.2f, 5.5f, 1.5f, true);
            return 9.2f;
        default: // a lone gate among palms
            put(mid, PalmA, x + 2.8f, 7.5f, 2, r.next01() < 0.5f);
            put(mid, PylonGate, x + 12, 9);
            put(mid, PalmB, x + 20.5f, 8, 2, true);
            return 23.5f;
        }
    });
    const uint8_t backRow[] = {PalmTall, PalmA, PalmTall, PalmB, Temple};
    fill(mid, 10, 8, 20, 5, nullptr, [&](int k, float x) -> float { // (the back row: 6 m behind)
        const uint8_t s = backRow[k];
        const float h = s == Temple ? r.range(5, 6.5f) : s == PalmTall ? r.range(10, 12) : r.range(8.5f, 10.5f);
        put(mid, s, x + width(s, h) / 2, h, -6, r.next01() < 0.5f);
        return width(s, h);
    });

    // near: the guardians, palm groves, shrines and pharaohs in turn, close together; bamboo behind now and then
    Layer& near = egypt_[kNear] = {-16, 420, 0.15f, 0.58f, 0.12f, 0.6f, {}};
    const uint8_t palms[] = {PalmA, PalmB, PalmSmall, PalmTall};
    static const int nearTall[] = {0, 1, 0, 0, 1};
    fill(near, 10, 2, 6, 5, nearTall, [&](int k, float x) -> float {
        switch (k) {
        case 0: // Anubis and Horus facing each other across a stela
            put(near, Anubis, x + 1.8f, 8);
            put(near, r.next01() < 0.5f ? StelaA : StelaB, x + 5.6f, 2.2f, -1);
            put(near, Horus, x + 9.4f, 8);
            return 11.2f;
        case 1: { // a palm grove
            float px = 0;
            const int n = 1 + r.index(3);
            for (int i = 0; i < n; ++i) {
                const uint8_t s = palms[r.index(4)];
                const float h = s == PalmSmall ? r.range(5.5f, 7) : s == PalmTall ? r.range(8.5f, 10.5f) : r.range(6.5f, 8.5f);
                put(near, s, x + px + width(s, h) * 0.45f, h, r.range(-1.5f, 1.5f), r.next01() < 0.5f);
                px += width(s, h) * 0.62f + r.range(0.5f, 2.5f);
                if (r.next01() < 0.3f) { put(near, Bamboo, x + px, r.range(4, 5.5f), -0.8f); px += 1.5f; }
            }
            return px;
        }
        case 2: // a shrine: a small obelisk, a bust, a stela
            put(near, r.next01() < 0.5f ? ObeliskSmallCyan : ObeliskSmallGold, x + 0.6f, 4.5f);
            put(near, r.next01() < 0.5f ? HorusBust : SphinxBust, x + 2.6f, 3);
            put(near, r.next01() < 0.5f ? StelaA : StelaB, x + 4.3f, 2.2f, 0.5f);
            return 5;
        case 3: // two pharaohs, a palm behind them
            put(near, Pharaoh, x + 0.9f, 5.5f);
            put(near, PalmSmall, x + 3.2f, 6, -1.5f, r.next01() < 0.5f);
            put(near, PharaohViolet, x + 5.6f, 5, 0, true);
            return 6.5f;
        default: { // one tall palm
            const float h = r.range(9.5f, 11);
            put(near, PalmTall, x + width(PalmTall, h) / 2, h, 0, r.next01() < 0.5f);
            return width(PalmTall, h);
        }
        }
    });
    fill(near, 8, 7, 18, 1, nullptr, [&](int, float x) -> float { // (bamboo, 3 m behind)
        const float h = r.range(4.5f, 6);
        put(near, Bamboo, x + width(Bamboo, h) / 2, h, -3, r.next01() < 0.5f);
        return width(Bamboo, h);
    });

    // the sky: constellations, a few stars each joined in a line or a branch
    starLayer_ = {-160, 900, 0.97f, 1.f, 0.f, 0.f, {}};
    stars_.clear();
    for (float x = 20; x < starLayer_.P - 40; x += r.range(35, 70)) {
        Constellation c;
        c.at = {x, r.range(26, 60)};
        c.size = r.range(9, 15);
        c.phase = r.next01() * 6.28f;
        const int n = 4 + r.index(4);
        glm::vec2 p{0, 0};
        for (int i = 0; i < n; ++i) {
            c.stars.push_back(p);
            if (i > 0) c.links.push_back({i == 3 && n > 5 ? 1 : i - 1, i}); // (a branch off the second star now and then)
            p += glm::vec2(r.range(0.5f, 1.2f), r.range(-0.7f, 0.7f)) * (c.size / 4);
        }
        stars_.push_back(std::move(c));
    }
}

void World::emitEgyptBackdrop(RenderList& out, float camX, float time) const {
    std::vector<Instance>& list = out.worldSprites;
    // a light (one of the white light shapes): centred at c, w x h, turned by rot, added in this colour
    auto light = [&](uint8_t id, glm::vec3 c, glm::vec2 size, float rot, const glm::vec3& color, float a) {
        list.push_back(quad(id, compose(c, {0, 0, rot}, {size.x, size.y, 1}), glm::vec4(color, a), 0.f, 0.f, 1.f));
    };
    // a sprite standing at foot, h tall, mirrored or not, leaning by tilt (about its foot); its foot line on the ground
    auto stand = [&](uint8_t id, glm::vec3 foot, float h, bool flip, float tilt, float light, float haze, float neon) {
        const float w = h * kSprites[id].aspect, sink = kSprites[id].footY * h;
        list.push_back(quad(id, compose(foot, {0, 0, tilt}, glm::vec3(1.f)) * compose({0, h / 2 - sink, 0}, {flip ? -w : w, h, 1}),
                            glm::vec4(glm::vec3(light), 1.f), haze, neon, 0.f));
    };
    // what of a layer is in view: two copies cover it, wherever the camera is (a generous width: wide screens, zoomed out)
    auto each = [&](const Layer& l, auto&& draw) {
        const float reach = (std::abs(l.z) + 25.f) * 1.1f;
        for (int k = 0; k < 2; ++k)
            for (const Prop& p : l.props) {
                const glm::vec3 foot{l.x + k * l.P + p.x, l.y, l.z + p.dz};
                const float half = p.h * kSprites[p.sprite].aspect * 0.5f + p.h;
                if (foot.x + half < camX - reach || foot.x - half > camX + reach) continue;
                draw(p, foot);
            }
    };
    const glm::vec3 cyan = glow("#29e7ff", 1.f), gold = glow("#ffb43a", 1.f), white = glow("#e9fbff", 1.f);

    // the sky: constellations (links, then stars twinkling)
    for (int k = 0; k < 2; ++k)
        for (const Constellation& c : stars_) {
            const glm::vec3 o{starLayer_.x + k * starLayer_.P + c.at.x, starLayer_.y + c.at.y, starLayer_.z};
            if (std::abs(o.x + c.size * 0.5f - camX) > 260) continue;
            const float lines = 0.28f + 0.12f * std::sin(time * 0.4f + c.phase);
            for (const auto& [a, b] : c.links) {
                const glm::vec2 pa = c.stars[size_t(a)], pb = c.stars[size_t(b)], d = pb - pa;
                light(GlowLine, o + glm::vec3((pa + pb) * 0.5f, 0), {glm::length(d), 0.22f}, std::atan2(d.y, d.x), cyan * 0.9f, lines);
            }
            for (size_t i = 0; i < c.stars.size(); ++i) {
                const float tw = 0.65f + 0.35f * std::sin(time * (1.3f + 0.4f * float(i % 3)) + c.phase + float(i) * 1.7f);
                const float s = i == 0 ? 2.2f : 1.3f; // (its brightest star first)
                light(GlowDot, o + glm::vec3(c.stars[i], 0), glm::vec2(s), 0, glm::mix(white, cyan, 0.35f) * 1.6f, tw);
            }
        }

    // a pyramid's capstone (it stands sunk to its foot line)
    auto capstone = [&](const Prop& p, glm::vec3 foot) {
        const SpriteInfo& s = kSprites[p.sprite];
        const float w = p.h * s.aspect, tx = (p.flip ? 0.5f - s.topX : s.topX - 0.5f) * w;
        return foot + glm::vec3(tx, p.h * (1.f - s.topY - s.footY) - p.h * 0.045f, 0.05f);
    };
    // the moon: a halo, the disc, a thin neon ring round it, clouds drifting across it (behind the ridge: the
    // pyramids slide past it)
    const glm::vec3 moon = moon_;
    {
        const float R = kMoonR, breathe = 1.f + 0.08f * std::sin(time * 0.7f);
        light(GlowDot, moon, glm::vec2(R * 7.5f * breathe), 0, glow("#7ff3ff", 0.6f), 0.5f);
        light(GlowDot, moon, glm::vec2(R * 3.2f), 0, glow("#e6fdff", 1.f), 0.55f * breathe);
        list.push_back(quad(MoonFace, compose(moon, glm::vec3(R * 2.f, R * 2.f, 1.f)), glm::vec4(glm::vec3(1.3f), 1.f), 0.f, 0.25f, 0.f));
        light(GlowRing, moon + glm::vec3(0, 0, 0.05f), glm::vec2(R * 2.8f), 0, cyan * 1.8f, 0.45f + 0.1f * std::sin(time * 2.3f));
        for (int i = 0; i < 3; ++i) { // (clouds: soft dark wisps, each at its own height and pace, wrapping round)
            const float span = R * 10.f, x = std::fmod(time * (0.35f + 0.2f * float(i)) + float(i) * R * 3.3f, span) - span / 2;
            const glm::vec3 c = moon + glm::vec3(x, R * (0.55f - 0.45f * float(i)), 0.1f);
            list.push_back(quad(GlowDot, compose(c, glm::vec3(R * (3.4f - 0.5f * float(i)), R * 0.36f, 1.f)),
                                glm::vec4(hexColor("#06141f"), 0.72f), 0.f, 0.f, 0.f));
        }
    }
    // the ridge: silhouettes, filled flat with the night, their outlines catching the moonlight
    const Layer& ridge = egypt_[kRidge];
    each(ridge, [&](const Prop& p, glm::vec3 foot) {
        const SpriteInfo& s = kSprites[p.sprite];
        const float w = p.h * s.aspect, sink = s.footY * p.h;
        Instance i = quad(p.sprite, compose(foot, glm::vec3(1.f)) * compose({0, p.h / 2 - sink, 0}, {p.flip ? -w : w, p.h, 1}),
                          glm::vec4(cyan * 0.55f, 1.f), 0.f, 0.f, 0.f);
        i.rim = glm::vec4(hexColor("#06141d"), 1.f); // (the fill)
        i.params.z = ridge.sil;
        list.push_back(i);
    });

    // Giza: the pyramids, each capstone lit as it's drawn (a light behind a pyramid stays behind it); the great ones
    // sweep a beam across the sky
    const Layer& far = egypt_[kFar];
    each(far, [&](const Prop& p, glm::vec3 foot) {
        const float breathe = 1.f + 0.05f * std::sin(time * 1.1f + p.seed * 6.28f);
        stand(p.sprite, foot, p.h, p.flip, 0, far.light * breathe, far.haze, far.neon);
        if (p.sprite != PyramidCircuit && p.sprite != PyramidPlain) return;
        const glm::vec3 cap = capstone(p, foot);
        const float pulse = 0.75f + 0.25f * std::sin(time * 2.1f + p.seed * 9.f);
        light(GlowDot, cap, glm::vec2(p.h * 0.32f), 0, glm::mix(cyan, white, 0.4f) * 1.4f, 0.55f * pulse);
        if (p.sprite == PyramidCircuit) {
            const float sweep = 0.5f * std::sin(time * 0.3f + p.seed * 6.f);
            light(GlowBeam, cap + glm::vec3(-std::sin(sweep), std::cos(sweep), 0) * 45.f, {2.2f, 90.f}, sweep, cyan * 1.2f, 0.16f);
        }
    });

    // temples: the pylons' doorways glow and flicker, the watchtower signals, obelisk tips glint
    const Layer& mid = egypt_[kMid];
    each(mid, [&](const Prop& p, glm::vec3 foot) {
        const float breathe = 1.f + 0.05f * std::sin(time * 1.3f + p.seed * 6.28f);
        stand(p.sprite, foot, p.h, p.flip, 0, mid.light * breathe, mid.haze, mid.neon);
        const SpriteInfo& s = kSprites[p.sprite];
        const float w = p.h * s.aspect;
        auto at = [&](float fx, float fy) { return foot + glm::vec3((p.flip ? 0.5f - fx : fx - 0.5f) * w, p.h * (1.f - fy), 0.05f); };
        if (p.sprite == PylonGate) {
            const float flick = 0.8f + 0.12f * std::sin(time * 9.f + p.seed * 20.f) * std::sin(time * 5.3f + p.seed * 7.f) + 0.08f * std::sin(time * 1.7f);
            light(GlowDot, at(s.doorX, s.doorY), {p.h * 0.5f, p.h * 0.62f}, 0, glow("#ffc94a", 1.6f), 0.7f * flick);
        } else if (p.sprite == Watchtower) {
            const glm::vec3 top = at(s.topX, s.topY + 0.02f);
            const float beat = std::fmod(time * 0.55f + p.seed, 1.f);
            light(GlowDot, top, glm::vec2(1.6f + 0.6f * std::sin(time * 4.f)), 0, cyan * 1.8f, 0.8f);
            light(GlowRing, top, glm::vec2(0.6f + beat * 5.f), 0, cyan * 1.4f, 0.7f * (1.f - beat)); // (the signal going out)
        } else if (p.sprite == ObeliskGlyphs || p.sprite == ObeliskCyan || p.sprite == ObeliskGold) {
            const float glint = std::pow(std::max(0.f, std::sin(time * 0.7f + p.seed * 13.f)), 30.f);
            if (glint > 0.01f) light(GlowDot, at(s.topX, s.topY + 0.01f), glm::vec2(2.4f), 0, p.sprite == ObeliskGold ? gold * 1.6f : cyan * 1.6f, glint);
        }
    });

    // scarabs flying over the temples (the city's traffic lanes): leaning into their flight, wobbling, a faint glow
    const uint8_t scarabs[] = {ScarabGoldBig, ScarabCyan, ScarabGold, ScarabCyanB, ScarabGoldSmall, ScarabDark};
    for (size_t i = 0; i < cars_.size(); ++i) {
        const Car& c = cars_[i];
        if (std::abs(c.pos.x - camX) > 80) continue;
        const float dir = c.flip ? -1.f : 1.f, size = 1.3f + 0.15f * float(i % 4);
        const glm::vec3 p{c.pos.x, c.pos.y, kScarabZ - float(i % 3) * 2.f};
        const bool goldOne = scarabs[i % 6] == ScarabGoldBig || scarabs[i % 6] == ScarabGold || scarabs[i % 6] == ScarabGoldSmall;
        light(GlowDot, p, glm::vec2(size * 2.4f), 0, goldOne ? gold : cyan, 0.22f);
        const float lean = -dir * 0.32f + 0.08f * std::sin(time * 7.f + float(i));
        list.push_back(quad(scarabs[i % 6], compose(p, {0, 0, lean}, {size * kSprites[scarabs[i % 6]].aspect, size, 1}),
                            glm::vec4(glm::vec3(1.15f), 1.f), 0.05f, 1.4f, 0.f));
    }

    // near: the guardians, palms (swaying about their feet), shrines and pharaohs
    const Layer& nearL = egypt_[kNear];
    each(nearL, [&](const Prop& p, glm::vec3 foot) {
        const bool palm = p.sprite == PalmA || p.sprite == PalmB || p.sprite == PalmSmall || p.sprite == PalmTall || p.sprite == Bamboo;
        const float sway = palm ? 0.018f * std::sin(time * 0.9f + p.seed * 6.28f) + 0.006f * std::sin(time * 2.3f + p.seed * 3.f) : 0.f;
        const float breathe = 1.f + 0.06f * std::sin(time * 1.2f + p.seed * 6.28f);
        stand(p.sprite, foot, p.h, p.flip, sway, nearL.light * breathe, nearL.haze, nearL.neon);
    });

    // the canal: water between gold banks, shimmering cyan as it flows
    {
        const float x0 = std::floor(camX / 10) * 10 - 70, len = 160, mid = (kCanalNear + kCanalFar) / 2, depth = kCanalNear - kCanalFar;
        list.push_back(quad(Solid, compose({x0 + len / 2, 0.03f, mid}, {-kPi / 2, 0, 0}, {len, depth, 1}), glm::vec4(glow("#0d8aa8", 0.32f), 1.f), 0, 0, 0));
        for (float z : {kCanalNear, kCanalFar}) // (its gold banks)
            list.push_back(quad(GlowLine, compose({x0 + len / 2, 0.04f, z}, {-kPi / 2, 0, 0}, {len, 0.22f, 1}), glm::vec4(gold * 1.2f, 0.7f), 0, 0, 1.f));
        // the moon's light on the water: a column of glints under it, wider toward the near bank, flickering
        const float path = camX + (moon.x - camX) * (18.f - kCanalNear) / (18.f - moon.z); // (under it on screen, seen from the camera)
        for (int i = 0; i < 26; ++i) {
            const float f = (float(i) + 0.5f) / 26.f, h1 = hash(i * 13.1), h2 = hash(i * 3.3 + 7.f);
            const float z = kCanalFar + 0.3f + f * (depth - 0.6f), spread = 0.4f + 1.4f * f;
            const float a = std::max(0.f, std::sin(time * (2.5f + 3.f * h1) + h2 * 30.f));
            list.push_back(quad(GlowLine, compose({path + (h1 - 0.5f) * 2.f * spread, 0.05f, z}, {-kPi / 2, 0, 0}, {0.8f + 1.6f * h2, 0.14f, 1}),
                                glm::vec4(glow("#dffcff", 2.4f), 0.8f * a), 0, 0, 1.f));
        }
        for (int i = 0; i < 70; ++i) { // shimmer: flecks of light drifting with the current (each the copy nearest the camera)
            const float h1 = hash(i * 17.3), h2 = hash(i * 5.1 + 2.f), h3 = hash(i * 9.7 + 1.f);
            const float wx = h1 * len + time * (0.6f + h2 * 0.8f), x = wx + std::floor((camX - wx) / len + 0.5f) * len;
            const float a = std::max(0.f, std::sin(time * (0.7f + h3) + h1 * 20.f));
            const glm::vec3 c{x, 0.045f, kCanalFar + 0.4f + h2 * (depth - 0.8f)};
            list.push_back(quad(GlowLine, compose(c, {-kPi / 2, 0, 0}, {1.2f + 2.8f * h3, 0.16f, 1}), glm::vec4(cyan * 2.f, 0.7f * a), 0, 0, 1.f));
        }
    }

    // the plaza: dark slabs with cyan seams (each its own tile from the five, turned its own way), dimmed
    {
        const float x0 = std::floor(camX / kTile) * kTile - 46;
        const uint8_t tiles[] = {TileA, TileB, TileC, TileD, TileE};
        for (float z = kPlazaNear - kTile / 2; z > kPlazaFar; z -= kTile)
            for (int i = 0; i < 48; ++i) {
                const float x = x0 + i * kTile + kTile / 2;
                const float h = hash(x * 0.37 + z * 1.31);
                const float fx = hash(x * 1.7 + z) < 0.5f ? -1.f : 1.f, fy = hash(x * 2.9 - z) < 0.5f ? -1.f : 1.f;
                list.push_back(quad(tiles[size_t(h * 5) % 5], compose({x, 0.03f, z}, {-kPi / 2, 0, 0}, {kTile * fx, kTile * fy, 1}),
                                    glm::vec4(glm::vec3(0.8f), 1.f), 0.f, 1.2f, 0.f));
            }
    }
}

} // namespace cs
