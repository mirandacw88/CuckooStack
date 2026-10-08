#include "World.h"
#include "Font.h"

namespace cs {

namespace {
constexpr int kRain = 900, kSpeedLines = 42;
constexpr float kStreakP = 60, kLaneP = 48, kLampP = 54, kVentP = 52;
const glm::vec3 kVents[] = {{6, 0.142f, -2.6f}, {19, 0.004f, 2.1f}, {33, 0.142f, -2.6f}, {45, 0.004f, 2.15f}};
const float kLamps[] = {8, 26, 44};

// tiled: o.position.x = floor((camX - 0.3P) / P) * P (each tile holds two copies)
float tileOffset(float camX, float P) { return std::floor((camX - 0.3f * P) / P) * P; }
// wrapped: base + round((camX + 0.25P - base) / P) * P
float wrapX(float bx, float camX, float P) { return bx + std::floor((camX + 0.25f * P - bx) / P + 0.5f) * P; }

LitMaterial metal(const char* hex) { LitMaterial m; m.color = hexColor(hex); m.metalness = 0.9f; m.roughness = 0.3f; return m; }
} // namespace

World::World(FastRandom& rng) : rng_(rng) {
    struct RowDef { float z, P, h0, h1, d, sign, par; };
    const RowDef defs[] = {{-16, 150, 12, 32, 8, 0.5f, 0.12f}, {-32, 170, 22, 50, 10, 0.35f, 0.3f}, {-58, 190, 30, 85, 12, 0.15f, 0.5f}};
    const char* cols[] = {"#ff2bd6", "#29e7ff", "#f4ff5a", "#ff5a3d", "#8a5bff"};
    const char* vWords[] = {"コケコッコー", "ニワトリ", "タマゴ", "ネオン", "卵屋", "養鶏場"};
    const char* hWords[] = {"EGG BAR", "24H", "CUCKOO", "NOODLES", "HEN·CORP", "YOLK"};
    for (const RowDef& d : defs) {
        Row row{d.z, d.P, d.par, {}, {}, {}};
        float bx = rng_.range(0, 4);
        while (bx < d.P - 5) {
            const float w = rng_.range(5, 10), h = rng_.range(d.h0, d.h1);
            row.towers.push_back({{bx + w / 2, h / 2, 0}, {w, h, d.d}, rng_.next01() * 100.f});
            if (rng_.next01() < d.sign) {
                const bool vertical = rng_.next01() < 0.55f;
                const glm::vec3 col = hexColor(cols[rng_.index(5)]);
                const std::string word = vertical ? vWords[rng_.index(6)] : hWords[rng_.index(6)];
                const float sw = vertical ? 1.6f : std::min(w * 0.85f, 6.f), sh = vertical ? 6.2f : sw / 4;
                const float sx = bx + w / 2 + (vertical ? (rng_.next01() < 0.5f ? -w / 2 + 1 : w / 2 - 1) : 0.f);
                row.signs.push_back({{sx, std::min(h - sh / 2 - 1, rng_.range(6, 14)), d.d / 2 + 0.08f}, {sw, sh}, col,
                                     rng_.next01() < 0.3f, 0.f, 1.f, word, vertical});
            }
            if (rng_.next01() < 0.3f) {
                const glm::vec3 p{bx + w / 2 + rng_.range(-w / 3, w / 3), h + 0.3f, 0};
                row.beacons.push_back({p, hash(p.x) * 6.f});
            }
            bx += w + rng_.range(0.6f, 3);
        }
        rows_.push_back(std::move(row));
    }
    for (int i = 0; i < 8; ++i) {
        const float dir = rng_.next01() < 0.5f ? 1.f : -1.f;
        cars_.push_back({rng_.range(-50, -22), rng_.range(10, 28), rng_.range(7, 14) * dir, rng_.range(0, 180), dir < 0,
                         glow(i % 2 ? "#29e7ff" : "#ff2bd6", 3.f)});
    }
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < 9; ++i)
            streaks_.push_back({{k * kStreakP + i * (kStreakP / 9) + rng_.range(0, 3), 0.012f, rng_.range(-0.6f, 1.4f)},
                                rng_.range(0.35f, 1.1f), rng_.range(4.5f, 7.5f), i % 4});
    for (int i = 0; i < kRain; ++i) rain_.push_back({{rng_.range(-12, 16), rng_.range(-2, 20), rng_.range(-9, 10)}});
    for (int i = 0; i < kSpeedLines; ++i) speed_.push_back({rng_.range(-14, 14), rng_.range(-2, 5), rng_.range(1.6f, 5.5f), rng_.range(0.7f, 1.6f)});
}

void World::update(float dt, float time, float camX, float camY, float speed, bool playing) {
    const float k = playing ? clampf((speed - 9.f) / 2.6f, 0.f, 1.f) : 0.f;
    speedOpacity_ += (k * 0.32f - speedOpacity_) * std::min(1.f, dt * 3.f);
    speedSegs_.clear();
    if (speedOpacity_ > 0.01f)
        for (Line& d : speed_) {
            d.x -= speed * 2.4f * dt;
            if (d.x < -14) { d.x += 28; d.y = rng_.range(-2, 5); }
            const float len = d.l * (0.5f + speed * 0.1f);
            speedSegs_.push_back({camX + d.x, camY + d.y, d.z});
            speedSegs_.push_back({camX + d.x + len, camY + d.y, d.z});
        }
    for (Drop& d : rain_) {
        d.p.y -= 22 * dt; d.p.x -= 2.5f * dt;
        if (d.p.y < -2) d.p = {rng_.range(-12, 16), rng_.range(17, 21), rng_.range(-9, 10)};
    }
    rainOrigin_ = {camX, camY - 4, 0};
    for (Car& c : cars_) {
        c.off += c.v * dt;
        const float base = c.off + camX * 0.4f;
        c.pos = {base + std::floor((camX - base) / 180.f + 0.5f) * 180.f, c.y + (camY - 1.3f) * 0.3f + std::sin(time * 0.8f + c.off) * 0.3f, c.z};
    }
    for (Row& r : rows_) {
        for (Sign& s : r.signs) {
            if (!s.flicker) continue;
            s.t -= dt;
            if (s.t <= 0) { s.opacity = rng_.next01() < 0.25f ? 0.15f : 1.f; s.t = rng_.range(0.04f, s.opacity < 1 ? 0.12f : 1.6f); }
        }
        const float off = camX * r.par;
        r.x = off + std::floor((camX - off - 0.3f * r.P) / r.P) * r.P;
        r.y = (camY - 1.3f) * r.par * 0.7f;
    }
    ventWorld_.clear();
    for (const glm::vec3& v : kVents) ventWorld_.push_back({wrapX(v.x, camX, kVentP), v.y, v.z});
}

// signTex(): 512x128 (or 128x512 vertical) canvas, neon border + text drawn in the sign colour with an 18 px glow,
// then again in white at 55% (a hot core); the plane is MeshBasicMaterial colour glow('#ffffff', .96), flickering.
void World::emitSignText(RenderList& out, const TextLayout& text, const Sign& s, const glm::vec3& origin) const {
    const float cw = s.vertical ? 128.f : 512.f, ch = s.vertical ? 512.f : 128.f;
    const glm::mat4 plane = compose(origin + s.pos, {s.size.x / cw, s.size.y / ch, 1.f}); // local units: canvas px, Y up
    TextStyle st;
    st.font = FontId::BodyBold;
    st.color = glm::mix(s.color, glm::vec3(1.f), 0.55f) * 0.96f;
    st.opacity = s.opacity;
    st.shadows = {{{0.f, 0.f}, 18.f, s.color * 0.96f, 0.9f}};
    // textBaseline = 'middle': each line box is centred on the canvas y the web build draws at
    if (s.vertical) {
        std::vector<std::string> chars;
        for (size_t i = 0; i < s.word.size();) { const size_t a = i; nextCodepoint(s.word, i); chars.push_back(s.word.substr(a, i - a)); }
        const float step = (ch - 60.f) / float(chars.size());
        st.size = std::min(86.f, step * 0.88f);
        for (size_t i = 0; i < chars.size(); ++i) {
            const float cy = 30.f + step * (float(i) + 0.5f);
            text.drawLine(out.worldText, chars[i], st, 0.f, ch / 2.f - cy + text.lineBox(st) / 2.f, TextAlign::Center, false, plane);
        }
    } else {
        st.size = 74.f;
        const float w = text.measure(s.word, st), squeeze = std::min(1.f, (cw - 70.f) / std::max(1.f, w));
        const float cy = ch / 2.f + 4.f;
        text.drawLine(out.worldText, s.word, st, 0.f, ch / 2.f - cy + text.lineBox(st) / 2.f, TextAlign::Center, false,
                      plane * compose({0, 0, 0}, {squeeze, 1.f, 1.f}));
    }
}

void World::emit(RenderList& out, float camX, float /*camY*/, float time, float pz, const glm::vec3& gradeLight, const TextLayout* text, float windowBoost) const {
    // street
    const float gx = std::floor(camX / 6) * 6;
    {
        LitMaterial asphalt; asphalt.color = glm::vec3(1.f); asphalt.metalness = 0.35f; asphalt.roughness = 1.f; asphalt.pattern = Pattern::Asphalt;
        out.add(Pass::Lit, MeshId::Plane) = makeLit(compose({gx, 0, -50}, {-kPi / 2, 0, 0}, {300, 220, 1}), asphalt);
        LitMaterial walk; walk.color = hexColor("#1e1d2b"); walk.metalness = 0.3f; walk.roughness = 0.5f;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({gx, 0.07f, -3.1f}, {300, 0.14f, 3.2f}), walk);
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({gx, 0.15f, -1.5f}, {300, 0.035f, 0.05f}), glow("#ff2bd6", 0.96f));
    }
    // neon reflections on the wet street
    {
        const glm::vec3 streakCols[] = {gradeLight * (0.55f + pz * 0.3f), glow("#29e7ff", 0.55f), glow("#8a5bff", 0.55f), glow("#ff8a3d", 0.55f)};
        const float t = tileOffset(camX, kStreakP);
        for (const Streak& s : streaks_)
            out.add(Pass::UnlitAdd, MeshId::Plane) =
                makeUnlit(compose(s.pos + glm::vec3(t, 0, 0), {-kPi / 2, 0, 0}, {s.w, s.l, 1}), streakCols[s.mat], 1.f, Shape::Streak);
    }
    // lane markings, guardrail, bollards
    {
        const float t = tileOffset(camX, kLaneP);
        const glm::vec3 lane = glow("#29e7ff", 0.9f) * (1.f + pz * 0.3f), rail = glow("#29e7ff", 0.96f);
        const LitMaterial post = metal("#2a2e40"), bollard = metal("#262a3a");
        for (int k = 0; k < 2; ++k) {
            const float o = t + k * kLaneP;
            for (float x = 0; x < kLaneP; x += 2.4f)
                for (float z : {1.05f, -1.05f})
                    out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x, 0.008f, z}, {1.2f, 0.01f, 0.07f}), lane);
            for (float x = 0; x < kLaneP; x += 2.f) {
                out.add(Pass::Lit, MeshId::Box) = makeLit(compose({o + x, 0.55f, -4.5f}, {0.1f, 0.8f, 0.1f}), post);
                out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x + 1, 0.95f, -4.5f}, {2.02f, 0.05f, 0.05f}), rail);
            }
            for (float x = 0; x < kLaneP; x += 3.2f) {
                out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose({o + x, 0.22f, 2.75f}, {0.09f, 0.44f, 0.09f}), bollard);
                out.add(Pass::Lit, MeshId::SmallRing) = makeEmissive(compose({o + x, 0.45f, 2.75f}, {kPi / 2, 0, 0}, {1, 1, 1}), rail);
            }
        }
    }
    // skyline rows (two copies per row so they wrap seamlessly)
    {
        LitMaterial tower; tower.color = glm::vec3(1.f); tower.metalness = 0.55f; tower.roughness = 0.5f; tower.pattern = Pattern::Windows;
        tower.emissive = glm::vec3(1.05f + windowBoost); // window emissive intensity, pattern decides where it lights
        const glm::vec3 beacon = glow("#ff3b3b", 1.12f);
        for (const Row& r : rows_)
            for (int k = 0; k < 2; ++k) {
                const glm::vec3 o{r.x + k * r.P, r.y, r.z};
                for (const Tower& tw : r.towers)
                    out.add(Pass::Lit, MeshId::Box) = makeLit(compose(o + tw.pos, tw.size), tower, tw.seed);
                for (const Sign& s : r.signs) {
                    Instance frame = makeUnlit(compose(o + s.pos, {s.size.x, s.size.y, 1}), s.color * 0.96f, s.opacity, Shape::PillOutline, 0.06f);
                    frame.params.x = s.size.x; frame.params.y = s.size.y;
                    out.add(Pass::UnlitAlpha, MeshId::Plane) = frame;
                    if (text) emitSignText(out, *text, s, o);
                }
                for (const Beacon& b : r.beacons)
                    if (std::sin(time * 2.2f + b.phase) > 0.4f)
                        out.add(Pass::Lit, MeshId::Sphere) = makeEmissive(compose(o + b.pos, glm::vec3(0.25f)), beacon);
            }
    }
    // flying traffic
    {
        LitMaterial hull; hull.color = hexColor("#232838"); hull.metalness = 0.9f; hull.roughness = 0.25f;
        const glm::vec3 tail = glow("#ff2b4a", 1.12f), head = glow("#f2fbff", 1.12f);
        for (const Car& c : cars_) {
            const glm::mat4 g = compose(c.pos, {0, c.flip ? kPi : 0.f, 0}, {1, 1, 1});
            out.add(Pass::Lit, MeshId::Sphere) = makeLit(g * compose({0, 0, 0}, {1.1f, 0.24f, 0.32f}), hull);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({-1.1f, 0, 0}, {0.06f, 0.12f, 0.55f}), tail);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({1.1f, 0, 0}, {0.06f, 0.1f, 0.5f}), head);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({0, -0.25f, 0}, {1.6f, 0.03f, 0.5f}), c.under);
        }
    }
    // street lamps and steam vents (foreground, rush past)
    {
        const LitMaterial m = metal("#262a3a");
        const glm::vec3 lampHead = glow("#ff2bd6", 1.08f), vent = glow("#ff6a2b", 0.84f);
        for (float bx : kLamps) {
            const glm::vec3 o{wrapX(bx, camX, kLampP), 0, 4.4f};
            out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose(o + glm::vec3(0, 3.3f, 0), {0.085f, 6.6f, 0.085f}), m);
            out.add(Pass::Lit, MeshId::Box) = makeLit(compose(o + glm::vec3(0, 6.5f, -0.85f), {0.08f, 0.08f, 1.7f}), m);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(o + glm::vec3(0, 6.42f, -1.6f), {0.55f, 0.08f, 0.3f}), lampHead);
        }
        for (const glm::vec3& v : ventWorld_) {
            out.add(Pass::Lit, MeshId::Plane) = makeEmissive(compose(v, {-kPi / 2, 0, 0}, {0.8f, 0.5f, 1}), vent);
            for (int k = 0; k < 6; ++k)
                out.add(Pass::Lit, MeshId::Box) = makeLit(compose(v + glm::vec3(-0.4f + k * 0.16f, 0.015f, 0), {0.05f, 0.03f, 0.56f}), m);
        }
    }
    // rain streaks and speed lines (LineSegments in the web build, thin quads here)
    {
        const glm::vec3 rainCol = hexColor("#a8dcff");
        const float tilt = -std::atan2(0.06f, 0.45f), len = std::sqrt(0.06f * 0.06f + 0.45f * 0.45f);
        for (const Drop& d : rain_)
            out.add(Pass::UnlitAlpha, MeshId::Plane) =
                makeUnlit(compose(rainOrigin_ + d.p + glm::vec3(0.03f, 0.225f, 0), {0, 0, tilt}, {0.014f, len, 1}), rainCol, 0.24f);
        const glm::vec3 lineCol = glow("#d8f6ff", 1.f);
        for (size_t i = 0; i + 1 < speedSegs_.size(); i += 2) {
            const glm::vec3 a = speedSegs_[i], b = speedSegs_[i + 1];
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose((a + b) * 0.5f, {b.x - a.x, 0.016f, 1}), lineCol, speedOpacity_);
        }
    }
}

} // namespace cs
