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
    buildEgypt();
    buildJapan();
}

// Neon Japan's skyline, from its own fixed seed: far, neon-edged mountains (one snow-capped peak each); middle,
// pagodas, torii gates and cherry trees with banners and signs; near, torii, cherry trees and stone lanterns.
void World::buildJapan() {
    FastRandom r(0x5A4Bu);
    struct RowDef { float z, P, par; };
    const RowDef defs[] = {{-16, 150, 0.12f}, {-32, 170, 0.3f}, {-58, 190, 0.5f}};
    const char* cols[] = {"#ff5aa8", "#29e7ff", "#ffd36a", "#ff4a3a", "#c06bff"};
    const char* hWords[] = {"SAKURA", "TEA HOUSE", "RAMEN", "SHRINE", "KOI", "SHOGUN"};
    const char* vWords[] = {"\u30cb\u30ef\u30c8\u30ea", "\u30bf\u30de\u30b4", "\u5375\u5c4b", "\u990a\u9d8f\u5834", "\u30b3\u30b1\u30b3\u30c3\u30b3\u30fc"};
    for (int ri = 0; ri < 3; ++ri) {
        const RowDef& d = defs[ri];
        ERow row{d.z, d.P, d.par, {}, {}, {}, {}};
        float bx = r.range(0, 6);
        while (bx < d.P - 8) {
            if (ri == 2) { // mountains, overlapping in ranges
                const float w = r.range(40, 70), h = w * r.range(0.32f, 0.46f);
                row.monuments.push_back({Monument::Mountain, {bx + w / 2, h / 2, 0}, {w, h, std::min(w * 0.5f, 30.f)}, r.next01() * 100.f});
                bx += w * r.range(0.5f, 0.85f);
                continue;
            }
            const float pick = r.next01();
            if (pick < 0.32f) { // a cherry tree in bloom
                const float h = ri == 0 ? r.range(3.2f, 4.6f) : r.range(6, 9);
                row.monuments.push_back({Monument::Tree, {bx + h * 0.4f, 0, 0}, {h * 0.9f, h, h * 0.6f}, r.next01() * 100.f});
                bx += h * 0.8f + r.range(1, 3);
            } else if (pick < 0.55f) { // a torii gate
                const float w = ri == 0 ? r.range(4, 5.5f) : r.range(6, 9), h = w * 0.85f;
                row.monuments.push_back({Monument::Torii, {bx + w / 2, 0, 0}, {w, h, 0.5f}, r.next01() * 100.f});
                bx += w + r.range(2, 5);
            } else if (ri == 0 && pick < 0.75f) { // a stone lantern
                const float h = r.range(1.6f, 2.4f);
                row.monuments.push_back({Monument::Lantern, {bx + 0.5f, 0, 0}, {h * 0.45f, h, h * 0.45f}, r.next01() * 100.f});
                bx += r.range(2, 4);
            } else { // a pagoda, a banner or a sign beside it
                const int tiers = ri == 0 ? 3 : r.index(3) + 3;
                const float w = ri == 0 ? r.range(3.5f, 4.5f) : r.range(5, 7.5f), h = tiers * w * 0.55f;
                row.monuments.push_back({Monument::Pagoda, {bx + w / 2, 0, 0}, {w, h, float(tiers)}, r.next01() * 100.f});
                const float sx = bx + w + 1.2f;
                if (r.next01() < 0.5f) { // a vertical banner (nobori)
                    row.signs.push_back({{sx, std::min(h * 0.6f, r.range(3.5f, 6.f)), 0.3f}, {1.4f, 5.4f}, hexColor(cols[r.index(5)]),
                                         r.next01() < 0.25f, 0.f, 1.f, vWords[r.index(5)], true});
                } else {
                    const float sw = ri == 0 ? 3.6f : 5.f;
                    row.signs.push_back({{bx + w / 2, std::max(2.f, h * 0.35f), w * 0.55f}, {sw, sw / 4}, hexColor(cols[r.index(5)]),
                                         r.next01() < 0.25f, 0.f, 1.f, hWords[r.index(6)], false});
                }
                bx += w + r.range(3, 7);
            }
        }
        japan_.push_back(std::move(row));
    }
}

// Neon Egypt's skyline, laid out from its own fixed seed (the game's random stream is left as it was): the same
// three parallax rows as the city's towers. Far: pyramids. Middle: obelisks and temple pylons (pairs flanking a gate)
// with cartouches and signs. Near: palms, small obelisks and pylons with signs.
void World::buildEgypt() {
    FastRandom r(0xE6E1u);
    struct RowDef { float z, P, par; };
    const RowDef defs[] = {{-16, 150, 0.12f}, {-32, 170, 0.3f}, {-58, 190, 0.5f}};
    const char* cols[] = {"#ffb43a", "#29e7ff", "#ffd36a", "#2bffd2", "#ff7a3a"};
    const char* hWords[] = {"RA\u00b7CORP", "NILE", "SPHINX 24H", "SCARAB", "PHARAOH", "ANKH BAR"};
    for (int ri = 0; ri < 3; ++ri) {
        const RowDef& d = defs[ri];
        ERow row{d.z, d.P, d.par, {}, {}, {}, {}};
        float bx = r.range(0, 6);
        while (bx < d.P - 8) {
            if (ri == 2) { // pyramids, overlapping like Giza's, a beacon on each capstone
                const float w = r.range(26, 54), h = w * 0.62f;
                row.monuments.push_back({Monument::Pyramid, {bx + w / 2, h / 2, 0}, {w, h, std::min(w * 0.6f, 28.f)}, r.next01() * 100.f});
                row.beacons.push_back({{bx + w / 2, h + 0.4f, 0}, r.next01() * 6.f});
                bx += w * r.range(0.55f, 0.95f);
                continue;
            }
            const float pick = r.next01();
            if (ri == 0 && pick < 0.4f) { // a palm
                const float h = r.range(6, 11);
                row.monuments.push_back({Monument::Palm, {bx + 1, h / 2, 0}, {0.35f, h, 0.35f}, r.next01() * 100.f});
                bx += r.range(2.5f, 5);
            } else if (pick < (ri == 0 ? 0.62f : 0.42f)) { // an obelisk
                const float w = ri == 0 ? r.range(0.9f, 1.3f) : r.range(1.3f, 2.2f), h = ri == 0 ? r.range(5, 8) : r.range(14, 24);
                row.monuments.push_back({Monument::Obelisk, {bx + w / 2, h / 2, 0}, {w, h, w}, r.next01() * 100.f});
                bx += w + r.range(2, 6);
            } else { // a pair of temple pylons flanking a gate, a cartouche or a sign on each
                const float w = ri == 0 ? r.range(5, 7) : r.range(7, 11), h = ri == 0 ? r.range(4.5f, 6.5f) : r.range(10, 16), dz = ri == 0 ? 2.5f : 4;
                for (int k = 0; k < 2; ++k) {
                    const float cx = bx + w / 2 + k * (w + 2.6f);
                    row.monuments.push_back({Monument::Pylon, {cx, h / 2, 0}, {w, h, dz}, r.next01() * 100.f});
                    if (r.next01() < 0.55f) {
                        const float ch = ri == 0 ? 3.6f : 6.2f;
                        Cartouche c{{cx + r.range(-w / 4, w / 4), std::min(h - ch / 2 - 0.4f, r.range(ch / 2 + 0.8f, 8)), dz / 2 + 0.1f}, ch, {}, r.next01() * 10.f};
                        for (int i = 0; i < 4; ++i) c.icons[i] = r.index(4);
                        if (c.pos.y > ch / 2 + 0.4f) row.cartouches.push_back(c);
                    } else {
                        const float sw = std::min(w * 0.7f, 6.f), sh = sw / 4;
                        row.signs.push_back({{cx, std::min(h - sh, r.range(5, 9)), dz / 2 + 0.1f}, {sw, sh}, hexColor(cols[r.index(5)]),
                                             r.next01() < 0.3f, 0.f, 1.f, hWords[r.index(6)], false});
                    }
                }
                bx += 2 * w + 2.6f + r.range(3, 8);
            }
        }
        egypt_.push_back(std::move(row));
    }
}

void World::update(float dt, float time, float camX, float camY, float speed, bool playing) {
    const float k = playing ? clampf((speed - 9.f) / 2.6f, 0.f, 1.f) : 0.f;
    speedOpacity_ += (k * 0.32f - speedOpacity_) * std::min(1.f, dt * 3.f);
    speedSegs_.clear();
    if (speedOpacity_ > 0.01f && rainOn_) // (lite mode: no wind streaks either)
        for (Line& d : speed_) {
            d.x -= speed * 2.4f * dt;
            if (d.x < -14) { d.x += 28; d.y = rng_.range(-2, 5); }
            const float len = d.l * (0.5f + speed * 0.1f);
            speedSegs_.push_back({camX + d.x, camY + d.y, d.z});
            speedSegs_.push_back({camX + d.x + len, camY + d.y, d.z});
        }
    const bool egypt = zone_ == Zone::Egypt, japan = zone_ == Zone::Japan;
    // Neon Japan's wind: a breeze, and every few seconds a gust that sweeps the blossoms along
    gustT_ -= dt;
    if (gustT_ <= 0) gustT_ = rng_.range(3.5f, 7.f);
    const float gustAim = gustT_ > 2.2f ? 0.f : std::sin(gustT_ / 2.2f * kPi); // (each gust swells and dies over 2.2 s)
    gust_ += (gustAim - gust_) * std::min(1.f, dt * 2.5f);
    for (size_t i = 0; i < rain_.size(); ++i) {
        Drop& d = rain_[i];
        if (!rainOn_ && !japan) break;
        if (japan) { // cherry blossoms: falling slowly, fluttering, blown along (more on a gust), past the running hen
            const float ph = float(i) * 2.399f;
            d.p.x -= (1.2f + 6.f * gust_ + speed * 0.35f) * dt + std::sin(time * 1.7f + ph) * 0.6f * dt;
            d.p.y -= (0.9f + 0.4f * std::sin(time * 2.3f + ph * 1.3f) - 1.2f * gust_ * 0.4f) * dt;
            d.p.z += std::cos(time * 1.1f + ph) * 0.3f * dt;
            if (d.p.x < -12 || d.p.y < -2.5f) d.p = {rng_.range(-6, 18), d.p.y < -2.5f ? rng_.range(15, 20) : rng_.range(-2, 18), rng_.range(-9, 10)};
            continue;
        }
        if (egypt) { // sand drifting sideways on the desert wind, low over the street
            d.p.x -= (7 + speed * 0.4f) * dt; d.p.y += std::sin(time * 1.3f + d.p.z) * 0.25f * dt;
            if (d.p.x < -12 || d.p.y > 9 || d.p.y < -2.5f) d.p = {rng_.range(14, 18), rng_.range(-2.2f, 6), rng_.range(-9, 10)};
            continue;
        }
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
    for (ERow& r : japan ? japan_ : egypt_) {
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
    if (!egypt && !japan) // (no steam vents in the desert or old Japan)
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

void World::emit(RenderList& out, float camX, float camY, float time, float pz, const glm::vec3& gradeLight, const TextLayout* text, float windowBoost) const {
    if (zone_ == Zone::Egypt) emitEgypt(out, camX, camY, time, pz, text);
    else if (zone_ == Zone::Japan) emitJapan(out, camX, camY, time, pz, text);
    else emitCity(out, camX, time, pz, gradeLight, text, windowBoost);
    // speed lines (both zones)
    const glm::vec3 lineCol = glow("#d8f6ff", 1.f);
    for (size_t i = 0; i + 1 < speedSegs_.size(); i += 2) {
        const glm::vec3 a = speedSegs_[i], b = speedSegs_[i + 1];
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose((a + b) * 0.5f, {b.x - a.x, 0.016f, 1}), lineCol, speedOpacity_);
    }
}

void World::emitCity(RenderList& out, float camX, float time, float pz, const glm::vec3& gradeLight, const TextLayout* text, float windowBoost) const {
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
    // rain streaks (LineSegments in the web build, thin quads here)
    {
        const glm::vec3 rainCol = hexColor("#a8dcff");
        const float tilt = -std::atan2(0.06f, 0.45f), len = std::sqrt(0.06f * 0.06f + 0.45f * 0.45f);
        for (const Drop& d : rain_)
            if (rainOn_) out.add(Pass::UnlitAlpha, MeshId::Plane) =
                makeUnlit(compose(rainOrigin_ + d.p + glm::vec3(0.03f, 0.225f, 0), {0, 0, tilt}, {0.014f, len, 1}), rainCol, 0.24f);
    }
}

// A cartouche icon, from flat shapes in additive light (in the sign's plane, facing the camera): 0 the ankh, 1 the
// eye of Horus, 2 the sun disc, 3 a scarab
void World::emitIcon(RenderList& out, int kind, glm::vec3 c, float s, const glm::vec3& col, float a) const {
    auto box = [&](glm::vec2 o, glm::vec2 sz, float rot = 0) {
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(c + glm::vec3(o * s, 0.01f), {0, 0, rot}, {sz.x * s, sz.y * s, 1}), col, a);
    };
    auto ring = [&](glm::vec2 o, glm::vec2 sz) {
        out.add(Pass::UnlitAdd, MeshId::RingFlat) = makeUnlit(compose(c + glm::vec3(o * s, 0.01f), {sz.x * s, sz.y * s, 1}), col, a);
    };
    auto disc = [&](glm::vec2 o, glm::vec2 sz) {
        out.add(Pass::UnlitAdd, MeshId::Circle) = makeUnlit(compose(c + glm::vec3(o * s, 0.01f), {sz.x * s, sz.y * s, 1}), col, a);
    };
    switch (kind) {
    case 0: ring({0, 0.22f}, {0.2f, 0.26f}); box({0, -0.16f}, {0.09f, 0.52f}); box({0, 0.0f}, {0.56f, 0.08f}); break; // ankh
    case 1: ring({0, 0.05f}, {0.42f, 0.2f}); disc({0, 0.05f}, {0.1f, 0.1f}); box({-0.12f, -0.2f}, {0.06f, 0.3f}, 0.4f);
            box({0.18f, -0.18f}, {0.3f, 0.06f}, -0.5f); break;                                                           // eye
    case 2: disc({0, 0}, {0.24f, 0.24f}); ring({0, 0}, {0.4f, 0.4f}); break;                                             // sun
    default: disc({0, -0.02f}, {0.18f, 0.26f}); disc({0, 0.3f}, {0.09f, 0.08f}); box({-0.24f, 0.04f}, {0.3f, 0.07f}, 0.6f);
             box({0.24f, 0.04f}, {0.3f, 0.07f}, -0.6f); break;                                                           // scarab
    }
}

void World::emitEgypt(RenderList& out, float camX, float camY, float time, float pz, const TextLayout* text) const {
    const float gx = std::floor(camX / 6) * 6;
    const glm::vec3 gold = glow("#ffb43a", 1.f), cyan = glow("#29e7ff", 1.f);
    LitMaterial stone; stone.color = hexColor("#10121f"); stone.metalness = 0.6f; stone.roughness = 0.35f; stone.pattern = Pattern::Stone;
    auto stoneOf = [&](const glm::vec3& edge, float courses, const glm::mat4& m, float seed, Pass pass = Pass::Lit, MeshId mesh = MeshId::Box) {
        LitMaterial mat = stone; mat.emissive = edge;
        Instance& i = out.add(pass, mesh) = makeLit(m, mat, seed);
        i.color.a = courses;
    };
    // the street: obsidian under a glowing grid; a gold kerb line
    {
        LitMaterial floor; floor.color = glm::vec3(1.f); floor.metalness = 0.6f; floor.roughness = 0.3f; floor.pattern = Pattern::TronFloor;
        floor.emissive = glow("#ffbd40", 1.f) * (0.9f + pz * 0.3f); floor.rimColor = glow("#29e7ff", 1.f) * (0.9f + pz * 0.3f); // gold / cyan lines
        out.add(Pass::Lit, MeshId::Plane) = makeLit(compose({gx, 0, -50}, {-kPi / 2, 0, 0}, {300, 220, 1}), floor);
        LitMaterial walk; walk.color = hexColor("#16131f"); walk.metalness = 0.4f; walk.roughness = 0.45f;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({gx, 0.07f, -3.1f}, {300, 0.14f, 3.2f}), walk);
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({gx, 0.15f, -1.5f}, {300, 0.035f, 0.05f}), glow("#ffb43a", 0.96f));
    }
    // gold and cyan light pooled on the glossy floor
    {
        const glm::vec3 streakCols[] = {glow("#ffb43a", 0.5f + pz * 0.25f), glow("#29e7ff", 0.5f), glow("#ffd36a", 0.45f), glow("#2bffd2", 0.45f)};
        const float t = tileOffset(camX, kStreakP);
        for (const Streak& s : streaks_)
            out.add(Pass::UnlitAdd, MeshId::Plane) =
                makeUnlit(compose(s.pos + glm::vec3(t, 0, 0), {-kPi / 2, 0, 0}, {s.w, s.l, 1}), streakCols[s.mat], 1.f, Shape::Streak);
    }
    // lane markings, a gold rail on stone posts, obelisk bollards with glowing tips
    {
        const float t = tileOffset(camX, kLaneP);
        const glm::vec3 lane = glow("#29e7ff", 0.8f) * (1.f + pz * 0.3f), rail = glow("#ffb43a", 1.f) * (1.f + pz * 0.25f);
        const LitMaterial post = metal("#1c1a28");
        for (int k = 0; k < 2; ++k) {
            const float o = t + k * kLaneP;
            for (float x = 0; x < kLaneP; x += 2.4f)
                for (float z : {1.05f, -1.05f})
                    out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x, 0.008f, z}, {1.2f, 0.01f, 0.07f}), lane);
            for (float x = 0; x < kLaneP; x += 2.f) {
                out.add(Pass::Lit, MeshId::Box) = makeLit(compose({o + x, 0.55f, -4.5f}, {0.12f, 0.8f, 0.12f}), post);
                out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x + 1, 0.95f, -4.5f}, {2.02f, 0.05f, 0.05f}), rail);
            }
            for (float x = 0; x < kLaneP; x += 3.2f) {
                stoneOf(gold * 0.8f, 1, compose({o + x, 0.2f, 2.75f}, {0.16f, 0.4f, 0.16f}), x, Pass::Lit, MeshId::Frustum);
                out.add(Pass::Lit, MeshId::Pyramid) = makeEmissive(compose({o + x, 0.44f, 2.75f}, {0.12f, 0.1f, 0.12f}), rail);
            }
        }
    }
    // the sun disc of Ra, low behind the pyramids: gold to ember, cut by gaps that drift down (a Tron sunset)
    {
        const glm::vec3 c{camX * 0.96f + 10.f, 9.f + camY * 0.3f, -96};
        const float R = 15;
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(c, glm::vec3(R * 3.f)), glow("#ff9a3a", 0.35f), 0.4f, Shape::RadialGlow);
        const int kBands = 40;
        const float drift = std::fmod(time * 0.35f, 1.f);
        for (int i = 0; i < kBands; ++i) {
            const float y0 = 1.f - 2.f * (float(i) + 0.f) / kBands, y1 = 1.f - 2.f * (float(i) + 1.f) / kBands; // top .. bottom
            const float mid = (y0 + y1) / 2;
            // the lower half: gaps that widen toward the bottom, drifting down
            float fill = 1.f;
            if (mid < 0.1f) { const float g = (0.1f - mid) / 1.1f; fill = clampf(1.f - g * 0.9f - 0.12f * std::sin((mid + drift * 0.18f) * 40.f), 0.15f, 1.f); }
            const float hy = (y0 - y1) * R * fill, w = 2.f * R * std::sqrt(std::max(0.f, 1.f - mid * mid));
            const glm::vec3 col = glm::mix(glow("#ffc24a", 0.75f), glow("#ff3d6a", 0.7f), clampf(0.5f - mid * 0.5f, 0.f, 1.f));
            const float shimmer = 0.9f + 0.1f * std::sin(time * 3.f + i);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(c + glm::vec3(0, mid * R, 0), {w, hy, 1}), col * shimmer, 0.7f);
        }
    }
    // the skyline: pyramids (far), obelisks and pylons (middle), palms, obelisks and pylons (near); two copies per row
    {
        const glm::vec3 beacon = glow("#29e7ff", 1.3f);
        for (const ERow& r : egypt_)
            for (int k = 0; k < 2; ++k) {
                const glm::vec3 o{r.x + k * r.P, r.y, r.z};
                for (const Monument& m : r.monuments) {
                    const glm::vec3 p = o + m.pos;
                    switch (m.kind) {
                    case Monument::Pyramid: stoneOf(gold * (0.9f + pz * 0.3f), 18, compose(p, m.size), m.seed, Pass::Lit, MeshId::Pyramid); break;
                    case Monument::Obelisk: {
                        stoneOf(gold * 0.9f, 1, compose(p, m.size), m.seed, Pass::Lit, MeshId::Frustum);
                        const float tip = m.size.x * 0.7f;
                        out.add(Pass::Lit, MeshId::Pyramid) = makeEmissive(compose(p + glm::vec3(0, m.size.y / 2 + tip * 0.6f, 0), {tip, tip * 1.2f, tip}),
                                                                           glow("#ffd36a", 1.6f) * (0.85f + 0.3f * std::sin(time * 2.f + m.seed)));
                        break;
                    }
                    case Monument::Pylon: stoneOf(cyan * 0.75f + gold * 0.25f, 9, compose(p, m.size), m.seed, Pass::Lit, MeshId::Frustum); break;
                    case Monument::Palm: {
                        LitMaterial trunk = metal("#141220"); trunk.roughness = 0.6f;
                        out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose(p, m.size), trunk);
                        const glm::vec3 top = p + glm::vec3(0, m.size.y / 2, 0);
                        for (int f = 0; f < 5; ++f) { // fronds: a glowing spine drooping from the crown, swaying
                            const float yaw = f * (2 * kPi / 5) + m.seed, sway = 0.08f * std::sin(time * 1.1f + m.seed + f);
                            const glm::mat4 fr = compose(top, {0, yaw, -0.55f - sway}, {1, 1, 1});
                            out.add(Pass::Lit, MeshId::Box) = makeLit(fr * compose({1.1f, 0, 0}, {2.2f, 0.06f, 0.5f}), trunk);
                            out.add(Pass::Lit, MeshId::Box) = makeEmissive(fr * compose({1.1f, 0.04f, 0}, {2.2f, 0.03f, 0.04f}), glow("#2bffd2", 0.9f));
                        }
                        break;
                    }
                    }
                }
                for (const Beacon& b : r.beacons) { // capstone lights, each sweeping a beam across the sky
                    const glm::vec3 p = o + b.pos;
                    out.add(Pass::Lit, MeshId::Sphere) = makeEmissive(compose(p, glm::vec3(0.6f)), beacon * (0.8f + 0.4f * std::sin(time * 2.2f + b.phase)));
                    const float ang = 0.55f * std::sin(time * 0.35f + b.phase);
                    out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(p, {0, 0, ang}, {1.f, 1.f, 1.f}) * compose({0, 28, 0}, {1.4f, 56, 1}),
                                                                       glow("#29e7ff", 0.7f), 0.1f, Shape::BottomFade);
                }
                for (const Sign& s : r.signs) {
                    Instance frame = makeUnlit(compose(o + s.pos, {s.size.x, s.size.y, 1}), s.color * 0.96f, s.opacity, Shape::PillOutline, 0.06f);
                    frame.params.x = s.size.x; frame.params.y = s.size.y;
                    out.add(Pass::UnlitAlpha, MeshId::Plane) = frame;
                    if (text) emitSignText(out, *text, s, o);
                }
                for (const Cartouche& c : r.cartouches) { // a rounded gold frame; its icons light up one after another
                    const glm::vec3 p = o + c.pos;
                    const glm::vec3 col = glow("#ffb43a", 1.1f);
                    const float cw = c.h * 0.27f;
                    Instance frame = makeUnlit(compose(p, {cw, c.h, 1}), col, 0.95f, Shape::PillOutline, 0.06f);
                    frame.params.x = cw; frame.params.y = c.h;
                    out.add(Pass::UnlitAlpha, MeshId::Plane) = frame;
                    const float seq = std::fmod(time * 0.5f + c.seed, 3.f) * 2.f; // 0..6: icons come on in turn, all hold, then reset
                    for (int i = 0; i < 4; ++i) {
                        const float on = clampf(seq - float(i), 0.f, 1.f), a = 0.25f + 0.75f * on;
                        emitIcon(out, c.icons[i], p + glm::vec3(0, c.h * 0.36f - i * c.h * 0.24f, 0), c.h * 0.2f, glow(i % 2 ? "#29e7ff" : "#ffd36a", 1.2f), a);
                    }
                }
            }
    }
    // solar barques gliding across the sky: dark hulls, upswept gold prow and stern, a cyan keel glow
    {
        LitMaterial hull; hull.color = hexColor("#1a1726"); hull.metalness = 0.9f; hull.roughness = 0.25f;
        const glm::vec3 trim = glow("#ffb43a", 1.2f);
        for (const Car& c : cars_) {
            const glm::mat4 g = compose(c.pos, {0, c.flip ? kPi : 0.f, 0}, {1, 1, 1});
            out.add(Pass::Lit, MeshId::Sphere) = makeLit(g * compose({0, 0, 0}, {1.3f, 0.2f, 0.34f}), hull);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({0, 0.14f, 0}, {2.4f, 0.04f, 0.4f}), trim);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({1.35f, 0.32f, 0}, {0, 0, -0.7f}, {0.7f, 0.06f, 0.08f}), trim);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({-1.35f, 0.32f, 0}, {0, 0, 0.7f}, {0.7f, 0.06f, 0.08f}), trim);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(g * compose({0, -0.22f, 0}, {1.8f, 0.03f, 0.5f}), glow("#29e7ff", 1.f));
        }
    }
    // brazier columns: stone shafts with gold edges, a bowl, and a flickering fire on top
    {
        LitMaterial bowl = metal("#2a2116");
        for (float bx : kLamps) {
            const glm::vec3 o{wrapX(bx, camX, kLampP), 0, 4.4f};
            stoneOf(gold, 1, compose(o + glm::vec3(0, 1.6f, 0), {0.5f, 3.2f, 0.5f}), bx, Pass::Lit, MeshId::Frustum);
            out.add(Pass::Lit, MeshId::Cone) = makeLit(compose(o + glm::vec3(0, 3.35f, 0), {kPi, 0, 0}, {0.42f, 0.32f, 0.42f}), bowl);
            const float f1 = 0.75f + 0.25f * std::sin(time * 13.f + bx) * std::sin(time * 7.3f + bx * 2.f);
            out.add(Pass::Lit, MeshId::Sphere) = makeEmissive(compose(o + glm::vec3(0, 3.6f, 0), {0.22f, 0.32f * f1, 0.22f}), glow("#ffd36a", 2.2f));
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(o + glm::vec3(0, 3.75f, 0.2f), glm::vec3(1.6f * f1, 2.2f * f1, 1)),
                                                               glow("#ff8a2b", 1.4f), 0.85f, Shape::RadialGlow);
        }
    }
    // drifting sand (instead of rain): short low streaks
    {
        const glm::vec3 sand = hexColor("#e8c890");
        for (const Drop& d : rain_)
            if (rainOn_) out.add(Pass::UnlitAlpha, MeshId::Plane) =
                makeUnlit(compose(rainOrigin_ + d.p + glm::vec3(0, 4, 0), {0.22f, 0.012f, 1}), sand, 0.2f);
    }
}

// a cherry tree: a dark trunk, branches, and clouds of glowing pink bloom that sway in the wind
void World::emitBlossomTree(RenderList& out, glm::vec3 base, const glm::vec3& size, float seed, float time) const {
    LitMaterial bark = metal("#1a1220"); bark.roughness = 0.7f;
    const float h = size.y, sway = 0.04f * std::sin(time * 1.3f + seed) + 0.05f * gust_;
    out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose(base + glm::vec3(0, h * 0.3f, 0), {0, 0, sway * 0.5f}, {h * 0.05f, h * 0.6f, h * 0.05f}), bark);
    const glm::vec3 crown = base + glm::vec3(0, h * 0.62f, 0);
    for (int k = 0; k < 2; ++k) // two branches reaching out
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose(crown + glm::vec3((k ? 1 : -1) * size.x * 0.18f, 0, 0), {0, 0, (k ? -0.6f : 0.6f) + sway},
                                                          {size.x * 0.4f, h * 0.035f, h * 0.035f}), bark);
    for (int b = 0; b < 9; ++b) { // the bloom: small soft clusters along the branches, deeper pink inside, pale at the tips
        const float a = b * 0.7f + seed, rr = 0.1f + 0.05f * float(b % 3);
        const float reach = 0.18f + 0.07f * float(b % 4);
        const glm::vec3 c = crown + glm::vec3(std::cos(a) * size.x * reach * 1.6f + sway * h, h * (0.08f + 0.06f * float(b % 3)), std::sin(a) * size.z * 0.25f);
        const float pulse = 0.85f + 0.15f * std::sin(time * 1.6f + seed + b);
        out.add(Pass::Lit, MeshId::Sphere) = makeEmissive(compose(c, glm::vec3(size.x * rr, h * rr * 0.55f, size.z * rr)),
                                                          glow(b % 3 == 0 ? "#ffd6ea" : b % 3 == 1 ? "#ff9ccf" : "#ff6fb4", 0.7f) * pulse);
    }
    out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(crown + glm::vec3(0, h * 0.12f, 0.5f), glm::vec3(size.x * 1.4f, h * 0.8f, 1)),
                                                       glow("#ff5aa8", 0.5f), 0.25f, Shape::RadialGlow);
}

void World::emitJapan(RenderList& out, float camX, float camY, float time, float pz, const TextLayout* text) const {
    const float gx = std::floor(camX / 6) * 6;
    const glm::vec3 pink = glow("#ff5aa8", 1.f), vermilion = glow("#ff4a3a", 1.f);
    LitMaterial stone; stone.color = hexColor("#120e1c"); stone.metalness = 0.6f; stone.roughness = 0.35f; stone.pattern = Pattern::Stone;
    auto stoneOf = [&](const glm::vec3& edge, float courses, const glm::mat4& m, float seed, MeshId mesh = MeshId::Box) {
        LitMaterial mat = stone; mat.emissive = edge;
        Instance& i = out.add(Pass::Lit, mesh) = makeLit(m, mat, seed);
        i.color.a = courses;
    };
    // the street: obsidian under a pink and vermilion grid; a vermilion kerb line
    {
        LitMaterial floor; floor.color = glm::vec3(1.f); floor.metalness = 0.6f; floor.roughness = 0.3f; floor.pattern = Pattern::TronFloor;
        floor.emissive = glow("#ff4a3a", 1.f) * (0.9f + pz * 0.3f); floor.rimColor = glow("#ff5aa8", 0.9f) * (0.9f + pz * 0.3f);
        out.add(Pass::Lit, MeshId::Plane) = makeLit(compose({gx, 0, -50}, {-kPi / 2, 0, 0}, {300, 220, 1}), floor);
        LitMaterial walk; walk.color = hexColor("#1a1220"); walk.metalness = 0.4f; walk.roughness = 0.45f;
        out.add(Pass::Lit, MeshId::Box) = makeLit(compose({gx, 0.07f, -3.1f}, {300, 0.14f, 3.2f}), walk);
        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({gx, 0.15f, -1.5f}, {300, 0.035f, 0.05f}), glow("#ff4a3a", 0.96f));
    }
    // fallen petals and pink light pooled on the glossy floor
    {
        const glm::vec3 streakCols[] = {glow("#ff5aa8", 0.5f + pz * 0.25f), glow("#ffc4e0", 0.45f), glow("#ff4a3a", 0.4f), glow("#c06bff", 0.4f)};
        const float t = tileOffset(camX, kStreakP);
        for (const Streak& s : streaks_)
            out.add(Pass::UnlitAdd, MeshId::Plane) =
                makeUnlit(compose(s.pos + glm::vec3(t, 0, 0), {-kPi / 2, 0, 0}, {s.w, s.l, 1}), streakCols[s.mat], 1.f, Shape::Streak);
    }
    // lane markings, a vermilion rail on dark posts, small lantern posts with pink caps
    {
        const float t = tileOffset(camX, kLaneP);
        const glm::vec3 lane = glow("#ff5aa8", 0.75f) * (1.f + pz * 0.3f), rail = glow("#ff4a3a", 1.f) * (1.f + pz * 0.25f);
        const LitMaterial post = metal("#1c1424");
        for (int k = 0; k < 2; ++k) {
            const float o = t + k * kLaneP;
            for (float x = 0; x < kLaneP; x += 2.4f)
                for (float z : {1.05f, -1.05f})
                    out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x, 0.008f, z}, {1.2f, 0.01f, 0.07f}), lane);
            for (float x = 0; x < kLaneP; x += 2.f) {
                out.add(Pass::Lit, MeshId::Box) = makeLit(compose({o + x, 0.55f, -4.5f}, {0.12f, 0.8f, 0.12f}), post);
                out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x + 1, 0.95f, -4.5f}, {2.02f, 0.05f, 0.05f}), rail);
            }
            for (float x = 0; x < kLaneP; x += 3.2f) {
                out.add(Pass::Lit, MeshId::Box) = makeLit(compose({o + x, 0.2f, 2.75f}, {0.14f, 0.4f, 0.14f}), post);
                out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose({o + x, 0.44f, 2.75f}, {0.2f, 0.06f, 0.2f}), glow("#ff8cc6", 1.2f));
            }
        }
    }
    // the full moon, huge and pale pink, a thin cloud drifting across it
    {
        const glm::vec3 c{camX * 0.96f + 12.f, 15.f + camY * 0.3f, -96};
        const float R = 13;
        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(c, glm::vec3(R * 3.6f)), glow("#ff9ccf", 0.35f), 0.45f, Shape::RadialGlow);
        out.add(Pass::UnlitAdd, MeshId::Circle) = makeUnlit(compose(c, glm::vec3(R)), glow("#ffe6f2", 0.95f), 0.9f);
        out.add(Pass::UnlitAdd, MeshId::RingFlat) = makeUnlit(compose(c + glm::vec3(0, 0, 0.1f), glm::vec3(R * 1.04f)), glow("#ff5aa8", 1.2f), 0.8f);
        for (int k = 0; k < 3; ++k) { // cloud wisps: dark bands drifting slowly across the moon
            const float y = (k - 1) * R * 0.45f + std::sin(time * 0.1f + k) * 0.5f, x = std::fmod(time * (0.6f + 0.3f * k) + k * 9.f, 50.f) - 25.f;
            out.add(Pass::UnlitAlpha, MeshId::Plane) = makeUnlit(compose(c + glm::vec3(x, y, 0.3f), {R * 1.6f, R * 0.14f, 1}), hexColor("#140a26"), 0.75f, Shape::SoftDisc);
        }
    }
    // the skyline
    {
        for (const ERow& r : japan_)
            for (int k = 0; k < 2; ++k) {
                const glm::vec3 o{r.x + k * r.P, r.y, r.z};
                for (const Monument& m : r.monuments) {
                    const glm::vec3 p = o + m.pos;
                    switch (m.kind) {
                    case Monument::Mountain: {
                        stoneOf(glow("#c06bff", 1.f) * (0.8f + pz * 0.3f), 14, compose(p, m.size), m.seed, MeshId::Pyramid);
                        const glm::vec3 cap = m.size * glm::vec3(0.28f, 0.28f, 0.28f); // the snow cap, glowing pale
                        out.add(Pass::Lit, MeshId::Pyramid) = makeEmissive(compose(p + glm::vec3(0, m.size.y / 2 - cap.y / 2 + 0.05f, 0), cap), glow("#ffe6f2", 0.8f));
                        break;
                    }
                    case Monument::Pagoda: { // tiers: a body and a wide roof each, getting smaller toward the spire
                        const int tiers = int(m.size.z + 0.5f);
                        const float tierH = m.size.y / tiers;
                        float y = p.y;
                        for (int t = 0; t < tiers; ++t) {
                            const float w = m.size.x * (1.f - 0.15f * t);
                            stoneOf(vermilion * 0.9f, 1, compose({p.x, y + tierH * 0.35f, p.z}, {w * 0.7f, tierH * 0.7f, w * 0.7f}), m.seed + t, MeshId::Box);
                            stoneOf(pink * (1.f + pz * 0.3f), 1, compose({p.x, y + tierH * 0.78f, p.z}, {w * 1.25f, tierH * 0.32f, w * 1.25f}), m.seed + t, MeshId::Frustum);
                            y += tierH;
                        }
                        out.add(Pass::Lit, MeshId::Cylinder) = makeEmissive(compose({p.x, y + tierH * 0.4f, p.z}, {0.08f, tierH * 0.8f, 0.08f}), glow("#ffd36a", 1.4f));
                        break;
                    }
                    case Monument::Torii: { // two pillars, the kasagi (top beam, with upswept ends) and the nuki below it
                        const float w = m.size.x, h = m.size.y, post = w * 0.07f;
                        const glm::vec3 red = glow("#ff4a3a", 1.f) * (1.f + pz * 0.3f);
                        LitMaterial lac = metal("#3a0e14"); lac.roughness = 0.35f; lac.emissive = red * 0.15f;
                        for (int s = -1; s <= 1; s += 2) {
                            out.add(Pass::Lit, MeshId::Cylinder) = makeLit(compose(p + glm::vec3(s * w * 0.36f, h * 0.45f, 0), {post, h * 0.9f, post}), lac);
                            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(p + glm::vec3(s * w * 0.36f + s * post * 0.55f, h * 0.45f, 0.06f), {0.04f, h * 0.9f, 0.04f}), red);
                        }
                        out.add(Pass::Lit, MeshId::Box) = makeLit(compose(p + glm::vec3(0, h * 0.72f, 0), {w * 0.86f, h * 0.06f, post * 1.2f}), lac);   // nuki
                        out.add(Pass::Lit, MeshId::Box) = makeLit(compose(p + glm::vec3(0, h * 0.92f, 0), {w * 1.12f, h * 0.08f, post * 1.4f}), lac);  // kasagi
                        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(p + glm::vec3(0, h * 0.965f, 0), {w * 1.14f, 0.05f, post * 1.45f}), red); // its glowing top
                        for (int s = -1; s <= 1; s += 2) // upswept ends
                            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(p + glm::vec3(s * w * 0.6f, h * 0.97f, 0), {0, 0, s * 0.35f}, {w * 0.12f, 0.06f, post * 1.4f}), red);
                        break;
                    }
                    case Monument::Tree: emitBlossomTree(out, p, m.size, m.seed, time); break;
                    case Monument::Lantern: { // a stone lantern (toro): base, a glowing light box, a roof
                        stoneOf(pink * 0.8f, 1, compose(p + glm::vec3(0, m.size.y * 0.3f, 0), {m.size.x * 0.5f, m.size.y * 0.6f, m.size.z * 0.5f}), m.seed, MeshId::Frustum);
                        const float f = 0.85f + 0.15f * std::sin(time * 9.f + m.seed) * std::sin(time * 5.3f + m.seed);
                        out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(p + glm::vec3(0, m.size.y * 0.72f, 0), glm::vec3(m.size.x * 0.55f, m.size.y * 0.2f, m.size.z * 0.55f)),
                                                                     glow("#ffb36a", 1.8f) * f);
                        stoneOf(pink, 1, compose(p + glm::vec3(0, m.size.y * 0.9f, 0), {m.size.x * 1.2f, m.size.y * 0.18f, m.size.z * 1.2f}), m.seed, MeshId::Pyramid);
                        out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(p + glm::vec3(0, m.size.y * 0.72f, 0.4f), glm::vec3(m.size.y * 1.4f * f)),
                                                                           glow("#ff9a4a", 1.f), 0.6f, Shape::RadialGlow);
                        break;
                    }
                    default: break;
                    }
                }
                for (const Sign& s : r.signs) {
                    Instance frame = makeUnlit(compose(o + s.pos, {s.size.x, s.size.y, 1}), s.color * 0.96f, s.opacity, Shape::PillOutline, 0.06f);
                    frame.params.x = s.size.x; frame.params.y = s.size.y;
                    out.add(Pass::UnlitAlpha, MeshId::Plane) = frame;
                    if (text) emitSignText(out, *text, s, o);
                }
            }
    }
    // sky lanterns drifting across the night: warm paper glowing from inside, rising and falling slowly
    {
        for (const Car& c : cars_) {
            const glm::vec3 p = c.pos + glm::vec3(0, std::sin(time * 0.6f + c.off) * 0.8f, 0);
            const float f = 0.85f + 0.15f * std::sin(time * 7.f + c.off);
            out.add(Pass::Lit, MeshId::Cylinder) = makeEmissive(compose(p, {0.32f, 0.55f, 0.32f}), glow("#ff9a4a", 1.5f) * f);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(p + glm::vec3(0, -0.3f, 0), {0.4f, 0.05f, 0.4f}), glow("#ff4a3a", 1.2f));
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(p + glm::vec3(0, 0, 0.3f), glm::vec3(2.4f * f)), glow("#ff9a4a", 0.8f), 0.55f, Shape::RadialGlow);
        }
    }
    // the street lamps: tall stone lanterns, the light flickering like a flame
    {
        for (float bx : kLamps) {
            const glm::vec3 o{wrapX(bx, camX, kLampP), 0, 4.4f};
            stoneOf(pink * 0.9f, 1, compose(o + glm::vec3(0, 1.3f, 0), {0.5f, 2.6f, 0.5f}), bx, MeshId::Frustum);
            const float f = 0.8f + 0.2f * std::sin(time * 11.f + bx) * std::sin(time * 6.1f + bx * 2.f);
            out.add(Pass::Lit, MeshId::Box) = makeEmissive(compose(o + glm::vec3(0, 2.95f, 0), {0.62f, 0.55f, 0.62f}), glow("#ffb36a", 1.9f) * f);
            stoneOf(pink, 1, compose(o + glm::vec3(0, 3.45f, 0), {1.1f, 0.5f, 1.1f}), bx, MeshId::Pyramid);
            out.add(Pass::UnlitAdd, MeshId::Plane) = makeUnlit(compose(o + glm::vec3(0, 2.95f, 0.4f), glm::vec3(2.4f * f)), glow("#ff9a4a", 1.1f), 0.75f, Shape::RadialGlow);
        }
    }
    // cherry blossoms falling and blowing on the wind, everywhere (in lite mode a quarter of them): little pink
    // petals tumbling as they go, a few big ones drifting right past the camera
    {
        const size_t n = rainOn_ ? rain_.size() : rain_.size() / 4;
        for (size_t i = 0; i < n; ++i) {
            const Drop& d = rain_[i];
            const float ph = float(i) * 2.399f, spin = time * (2.f + float(i % 5)) + ph;
            const float s = (i % 9 == 0) ? 0.24f : 0.11f + 0.04f * float(i % 3); // (one in nine big, near-looking)
            const glm::vec3 col = i % 4 == 0 ? hexColor("#ffe2f0") : i % 4 == 1 ? hexColor("#ff9ccf") : hexColor("#ffbcdc");
            out.add(Pass::UnlitAlpha, MeshId::Plane) =
                makeUnlit(compose(rainOrigin_ + d.p + glm::vec3(0, 4, 0), {std::sin(spin) * 0.9f, std::cos(spin * 0.7f), spin}, {s, s * 0.7f, 1}), col, 0.9f, Shape::SoftDisc);
        }
    }
}

} // namespace cs
