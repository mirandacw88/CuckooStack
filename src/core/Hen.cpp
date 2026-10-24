#include "Hen.h"
#include "HenModel.h"

namespace cs {

namespace {
constexpr Hen::Mat kRegionMat[HenModel::kRegions] = {Hen::Mat::White, Hen::Mat::PinkDim, Hen::Mat::Cyan, Hen::Mat::Plate, Hen::Mat::Gold};
}

int Hen::add(int parent, glm::vec3 pos, glm::vec3 rot, glm::vec3 scale) {
    nodes.push_back({parent, pos, rot, scale});
    return static_cast<int>(nodes.size()) - 1;
}

void Hen::part(int parent, MeshId mesh, Mat mat, glm::vec3 pos, glm::vec3 rot, glm::vec3 scale) {
    parts_.push_back({parent, mesh, mat, compose(pos, rot, scale)});
}

Hen::Hen() {
    cyanColor = hexColor("#29e7ff");
    root = add(-1, {}, {}, glm::vec3(1.1f)); // hen.root.scale.setScalar(1.1)
    body = add(root);
    auto S = [&](int parent, float r, Mat m, glm::vec3 pos, glm::vec3 scale = {1, 1, 1}, glm::vec3 rot = {}) {
        part(parent, MeshId::Sphere, m, pos, rot, scale * r);
    };

    S(body, 0.36f, Mat::White, {0, 0.52f, 0}, {1.15f, 0.95f, 0.86f});             // torso
    S(body, 0.27f, Mat::White, {0.2f, 0.5f, 0});                                  // breast
    S(body, 0.16f, Mat::Plate, {0.38f, 0.5f, 0}, {0.5f, 1, 1.1f});                // chest plate
    S(body, 0.025f, Mat::Cyan, {0.455f, 0.54f, 0});                               // status LED

    tail = add(body, {-0.33f, 0.66f, 0});
    const struct { float z, rz; Mat m; } feathers[] = {{-0.09f, 0.75f, Mat::PinkDim}, {0, 0.5f, Mat::White}, {0.09f, 0.3f, Mat::PinkDim}};
    for (const auto& f : feathers) S(tail, 0.15f, f.m, {-0.04f, 0.1f, f.z}, {0.5f, 1.35f, 0.4f}, {0, 0, f.rz});

    for (int k = 0; k < 2; ++k) {
        const float s = k == 0 ? -1.f : 1.f;
        const int pv = add(body, {0.04f, 0.64f, s * 0.29f});
        S(pv, 0.22f, Mat::Plate, {-0.12f, -0.08f, 0}, {1.25f, 0.72f, 0.32f}, {0, 0, 0.25f});
        part(pv, MeshId::Box, Mat::Cyan, {-0.12f, -0.05f, s * 0.07f}, {0, 0, 0.25f}, {0.34f, 0.022f, 0.02f});
        wings[k] = pv;
    }
    // battery pack and antenna
    part(body, MeshId::Box, Mat::Plate, {-0.16f, 0.82f, 0}, {0, 0, 0.2f}, {0.2f, 0.2f, 0.34f});
    part(body, MeshId::Box, Mat::Cyan, {-0.155f, 0.86f, 0}, {0, 0, 0.2f}, {0.212f, 0.03f, 0.352f});
    part(body, MeshId::Cylinder, Mat::Chrome, {-0.22f, 1.08f, 0.1f}, {0, 0, 0.15f}, {0.008f, 0.38f, 0.008f});
    tipNode_ = add(body, {-0.25f, 1.27f, 0.1f});
    part(tipNode_, MeshId::Sphere, Mat::TipRed, {}, {}, glm::vec3(0.03f));

    head = add(body, {0.3f, 0.92f, 0});
    S(head, 0.2f, Mat::White, {});
    for (int i = 0; i < 4; ++i) { // neon mohawk comb
        const float d = std::abs(i - 1.5f);
        part(head, MeshId::Cone, Mat::Pink, {-0.1f + i * 0.06f, 0.24f - d * 0.02f, 0}, {0, 0, 0.25f - i * 0.12f},
             {0.042f, 0.17f - d * 0.025f, 0.042f});
    }
    part(head, MeshId::Cone, Mat::Gold, {0.22f, -0.01f, 0}, {0, 0, -kPi / 2}, {0.058f, 0.17f, 0.058f}); // beak
    S(head, 0.05f, Mat::PinkDim, {0.16f, -0.12f, 0}, {0.8f, 1.35f, 0.6f});                         // wattle
    const int visorPivot = add(head, {0, 0.05f, 0}, {kPi / 2, 0, 0});
    part(visorPivot, MeshId::Visor, Mat::Cyan, {}, {0, 0, -1.25f}, {1, 1, 1});

    firstLegPart_ = parts_.size();
    for (int k = 0; k < 2; ++k) {
        const float s = k == 0 ? -1.f : 1.f;
        const int lp = add(root, {0.04f, 0.24f, s * 0.12f});
        // the thigh runs from the knee (-0.11) up into the body (+0.2), so the leg stays joined to it while the body
        // bobs (up to 0.06) and the leg swings; the shin hangs from the knee down to the foot (-0.22)
        part(lp, MeshId::Cylinder, Mat::Chrome, {0, 0.045f, 0}, {}, {0.023f, 0.31f, 0.023f});
        S(lp, 0.034f, Mat::Cyan, {});
        const int kn = add(lp, {0, -0.11f, 0});
        S(kn, 0.026f, Mat::Chrome, {});
        part(kn, MeshId::Cylinder, Mat::Chrome, {0, -0.055f, 0}, {}, {0.021f, 0.11f, 0.021f});
        const int ft = add(kn, {0, -0.11f, 0});
        for (float a : {-0.45f, 0.f, 0.45f}) // toes
            part(ft, MeshId::Cylinder, Mat::Chrome, {0.05f * std::cos(a), 0, -0.05f * std::sin(a)}, {0, a, -kPi / 2},
                 {0.0125f, 0.13f, 0.0125f});
        legs[k] = lp;
        knees[k] = kn;
        feet[k] = ft;
    }

    // the model's bones are the animated nodes; remember where each sits in body space at rest
    useModel = henModel(kClassicHen).ok;
    std::vector<glm::mat4> world(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const glm::mat4 local = compose(nodes[i].pos, nodes[i].rot, nodes[i].scale);
        world[i] = nodes[i].parent < 0 ? local : world[size_t(nodes[i].parent)] * local;
    }
    const int bones[HenModel::kBones] = {body, head, wings[0], wings[1], tail};
    const glm::mat4 bodyInv = glm::inverse(world[size_t(body)]);
    for (int k = 0; k < HenModel::kBones; ++k) boneRestInv_[size_t(k)] = glm::inverse(bodyInv * world[size_t(bones[k])]);
}

glm::vec3 Hen::accent() const { return skin.rainbow ? hueColor(time * 0.6f) : skin.accent; }

LitMaterial Hen::material(Mat m) const {
    LitMaterial l;
    const bool rimmed = m == Mat::White || m == Mat::Plate || m == Mat::Chrome || m == Mat::Gold;
    if (rimmed) { l.rimColor = rimColor; l.rimStrength = 2.1f; l.rimPow = 2.4f; }
    switch (m) {
    case Mat::White:   l.color = skin.body; l.metalness = skin.bodyMetal; l.roughness = skin.bodyRough; l.emissive = glm::vec3(skin.bodyGlow); break;
    case Mat::Plate:   l.color = skin.plate; l.metalness = 0.9f; l.roughness = 0.26f; break;
    case Mat::Chrome:  l.color = skin.chrome; l.metalness = 1.f; l.roughness = 0.22f; break;
    case Mat::Gold:    l.color = skin.gold; l.metalness = 1.f; l.roughness = 0.24f; break;
    case Mat::Pink:    l.color = accent(); l.emissive = l.color * 2.6f; l.roughness = 0.4f; break;
    case Mat::PinkDim: l.color = accent(); l.emissive = l.color * 0.9f; l.roughness = 0.4f; break;
    case Mat::Cyan:    l.color = cyanColor; l.emissive = cyanColor * cyanIntensity; l.roughness = 0.2f; break;
    case Mat::TipRed:  l.color = glm::vec3(0.f); l.emissive = glow("#ff2b4a", 5.f); break;
    }
    return l;
}

std::vector<glm::mat4> Hen::worldTransforms() const {
    std::vector<glm::mat4> world(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& n = nodes[i];
        const glm::mat4 local = compose(n.pos, n.rot, n.scale);
        world[i] = n.parent < 0 ? local : world[size_t(n.parent)] * local;
    }
    return world;
}

void Hen::emit(RenderList& out) const {
    if (shattered_) { emitShards(out); return; }
    const std::vector<glm::mat4> world = worldTransforms();
    for (size_t i = useModel && model().ok ? firstLegPart_ : 0; i < parts_.size(); ++i) {
        const Part& p = parts_[i];
        if (p.node == tipNode_ && !tipVisible) continue;
        out.add(Pass::Lit, p.mesh) = makeLit(world[p.node] * p.local, material(p.mat));
    }
    if (useModel && model().ok) emitModel(out, world);
}

// Linear-blend skinning on the CPU: each bone is one of the animated nodes, so everything that animates the
// procedural hen (wing flaps, head bob, tail wag, squash) bends the model the same way.
void Hen::poseModel(const std::vector<glm::mat4>& world, std::vector<Vertex>& out) const {
    const HenModel& m = model();
    const int bones[HenModel::kBones] = {body, head, wings[0], wings[1], tail};
    const glm::mat4 bodyInv = glm::inverse(world[size_t(body)]);
    glm::mat4 M[HenModel::kBones];
    glm::mat3 R[HenModel::kBones];
    for (int k = 0; k < HenModel::kBones; ++k) {
        M[k] = bodyInv * world[size_t(bones[k])] * boneRestInv_[size_t(k)];
        R[k] = glm::mat3(M[k]);
    }
    out.reserve(out.size() + m.vertexCount);
    for (int r = 0; r < m.regions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            glm::vec3 p{0.f}, n{0.f};
            for (int k = 0; k < HenModel::kBones; ++k) {
                if (v.w[k] <= 0.f) continue;
                p += v.w[k] * glm::vec3(M[k] * glm::vec4(v.pos, 1.f));
                n += v.w[k] * (R[k] * v.normal);
            }
            out.push_back({p, glm::normalize(n), v.uv});
        }
}


// Material of region r: Classic's colour regions take the outfit's materials; a textured model samples its texture,
// its hot cracks glowing and slowly pulsing.
LitMaterial Hen::regionMaterial(int r) const {
    if (!model().textured()) return material(kRegionMat[r]);
    LitMaterial l;
    l.color = glm::vec3(1.f);
    const HenModel& m = model();
    l.roughness = m.rough;
    l.metalness = m.metal;
    l.pattern = Pattern::HeroTexture;
    l.emissive = glm::vec3((2.6f + 0.8f * std::sin(time * 2.4f)) * m.glow); // the hot texels' glow, pulsing slowly
    // rim light in the outfit's warm trim colour (Magma: orange, Gold Rush: pale gold)
    l.rimColor = skin.gold * 1.4f; l.rimStrength = 1.6f; l.rimPow = 2.6f;
    return l;
}

void Hen::emitModel(RenderList& out, const std::vector<glm::mat4>& world) const {
    const HenModel& m = model();
    poseModel(world, out.heroVerts);
    out.heroFirst = m.firstMesh;
    out.heroMeshes = m.regions;
    out.heroModel = skin.model;
    const glm::mat4& bodyWorld = world[size_t(body)];
    for (int r = 0; r < m.regions; ++r)
        out.add(Pass::Lit, MeshId(int(m.firstMesh) + r)) = makeLit(bodyWorld, regionMaterial(r));
}

// the hen as posed now, in world space (shatterRest_), and each of the model's shards' centre (count: its vertices)
std::vector<glm::vec3> Hen::breakPose(std::vector<int>& count) {
    const HenModel& m = model();
    shatterModel_ = skin.model;
    const std::vector<glm::mat4> world = worldTransforms();
    std::vector<Vertex> posed;
    poseModel(world, posed);
    const glm::mat4& bodyWorld = world[size_t(body)];
    const glm::mat3 bodyRot = glm::mat3(bodyWorld);
    shatterRest_.resize(posed.size());
    std::vector<glm::vec3> sum(size_t(m.shardCount), glm::vec3(0.f));
    count.assign(size_t(m.shardCount), 0);
    size_t k = 0;
    for (int r = 0; r < m.regions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            const glm::vec3 p = glm::vec3(bodyWorld * glm::vec4(posed[k].pos, 1.f));
            shatterRest_[k] = {p, glm::normalize(bodyRot * posed[k].normal), v.uv};
            sum[size_t(v.shard)] += p;
            ++count[size_t(v.shard)];
            ++k;
        }
    for (size_t i = 0; i < sum.size(); ++i) if (count[i]) sum[i] /= float(count[i]);
    return sum;
}

namespace {

struct Rnd {
    uint32_t state;
    explicit Rnd(uint32_t seed) : state(seed * 747796405u + 2891336453u) {}
    float operator()(float lo, float hi) {
        state = state * 747796405u + 2891336453u;
        const uint32_t w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return lo + (hi - lo) * float((w >> 22u) ^ w) / 4294967296.f;
    }
};

} // namespace

void Hen::shatter(glm::vec3 impact, glm::vec3 velocity, float floorY, uint32_t seed) {
    if (!canShatter()) return;
    std::vector<int> cnt;
    const std::vector<glm::vec3> centre = breakPose(cnt);
    Rnd rnd(seed);
    shards_.assign(centre.size(), {});
    pieceOf_.resize(centre.size());
    for (size_t i = 0; i < shards_.size(); ++i) {
        pieceOf_[i] = int(i);
        Shard& s = shards_[i];
        s.c0 = s.c = cnt[i] ? centre[i] : impact;
        glm::vec3 out = s.c0 - impact + glm::vec3(rnd(-0.2f, 0.2f), rnd(0.f, 0.3f), rnd(-0.2f, 0.2f));
        out = glm::length(out) > 1e-4f ? glm::normalize(out) : glm::vec3(0, 1, 0);
        s.v = out * rnd(2.5f, 6.f) + velocity * 0.45f + glm::vec3(0, rnd(1.5f, 3.5f), rnd(-1.f, 1.f));
        s.axis = glm::normalize(glm::vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)) + glm::vec3(0.001f));
        s.angle = 0.f;
        s.spin = rnd(5.f, 14.f) * (rnd(0, 1) < 0.5f ? -1.f : 1.f);
    }
    breakup_ = Breakup::Glass;
    floorY_ = floorY;
    shatterT_ = 0.f;
    shattered_ = true;
}

// Two pieces, the model's shards above the cut and those below: the top half thrown up and on, tumbling over; the
// bottom half toppling back where it stood
void Hen::slice(float cutY, glm::vec3 velocity, float floorY, uint32_t seed) {
    if (!canShatter()) return;
    std::vector<int> cnt;
    const std::vector<glm::vec3> centre = breakPose(cnt);
    Rnd rnd(seed);
    pieceOf_.resize(centre.size());
    glm::vec3 sum[2] = {glm::vec3(0.f), glm::vec3(0.f)};
    int n[2] = {0, 0};
    for (size_t i = 0; i < centre.size(); ++i) {
        const int piece = centre[i].y >= cutY ? 0 : 1;
        pieceOf_[i] = piece;
        sum[piece] += centre[i] * float(cnt[i]);
        n[piece] += cnt[i];
    }
    shards_.assign(2, {});
    for (int p = 0; p < 2; ++p) {
        Shard& s = shards_[size_t(p)];
        s.c0 = s.c = n[p] ? sum[p] / float(n[p]) : glm::vec3(0.f, cutY, 0.f);
        if (p == 0) { // the top: thrown up and on, turning end over end
            s.v = glm::vec3(velocity.x * 0.55f + rnd(0.6f, 1.2f), rnd(3.2f, 4.2f), rnd(0.8f, 1.6f));
            s.axis = glm::normalize(glm::vec3(rnd(-0.3f, 0.3f), rnd(-0.3f, 0.3f), 1.f));
            s.spin = -rnd(4.f, 6.f);
        } else { // the bottom: toppling back, slowly
            s.v = glm::vec3(velocity.x * 0.15f, rnd(0.6f, 1.2f), rnd(-0.3f, 0.3f));
            s.axis = glm::vec3(0.f, 0.f, 1.f);
            s.spin = rnd(1.5f, 2.5f);
        }
        s.angle = 0.f;
    }
    breakup_ = Breakup::Slice;
    cutY_ = cutY;
    floorY_ = floorY;
    shatterT_ = 0.f;
    shattered_ = true;
}

// The shards, scarcely moving: she slumps where she stood, charring, and crumbles into ash sinking into the lava
void Hen::burn(glm::vec3 velocity, float floorY, uint32_t seed) {
    if (!canShatter()) return;
    std::vector<int> cnt;
    const std::vector<glm::vec3> centre = breakPose(cnt);
    Rnd rnd(seed);
    shards_.assign(centre.size(), {});
    pieceOf_.resize(centre.size());
    for (size_t i = 0; i < shards_.size(); ++i) {
        pieceOf_[i] = int(i);
        Shard& s = shards_[i];
        s.c0 = s.c = cnt[i] ? centre[i] : glm::vec3(0.f, floorY, 0.f);
        s.v = glm::vec3(velocity.x * 0.08f + rnd(-0.4f, 0.4f), rnd(-0.2f, 0.9f), rnd(-0.4f, 0.4f));
        s.axis = glm::normalize(glm::vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)) + glm::vec3(0.001f));
        s.angle = 0.f;
        s.spin = rnd(0.5f, 2.f) * (rnd(0, 1) < 0.5f ? -1.f : 1.f);
    }
    breakup_ = Breakup::Char;
    floorY_ = floorY;
    shatterT_ = 0.f;
    shattered_ = true;
}

void Hen::updateShatter(float dt) {
    if (!shattered_) return;
    shatterT_ += dt;
    const bool charred = breakup_ == Breakup::Char;
    // (ash: it holds together a beat as it burns, then falls apart, sinking below the surface)
    const float gravity = charred ? (shatterT_ < 0.7f ? 0.f : 4.f) : 14.f;
    const float rest = charred ? -0.25f : 0.04f;
    for (Shard& s : shards_) {
        if (charred && shatterT_ < 0.7f) { s.angle += s.spin * dt * 0.2f; continue; }
        s.v.y -= gravity * dt;
        s.c += s.v * dt;
        s.angle += s.spin * dt;
        if (s.c.y < floorY_ + rest && s.v.y < 0.f) { // bounce, scattering on the ground (ash: sinking)
            s.c.y = floorY_ + rest;
            s.v.y = charred ? 0.f : -s.v.y * 0.3f;
            s.v.x *= 0.55f; s.v.z *= 0.55f;
            s.spin *= 0.5f;
        }
    }
}

// The shards, each a rigid piece: rotated about its own centre, carried along its path, shrinking away at the end.
void Hen::emitShards(RenderList& out) const {
    const HenModel& m = henModel(shatterModel_); // the model that broke, even if the outfit changes meanwhile
    const float fade = 1.f - glm::smoothstep(kShatterLife * 0.6f, kShatterLife, shatterT_);
    if (fade <= 0.f) return;
    std::vector<glm::mat3> rot(shards_.size());
    for (size_t i = 0; i < shards_.size(); ++i) rot[i] = glm::mat3(glm::rotate(glm::mat4(1.f), shards_[i].angle, shards_[i].axis));
    out.heroVerts.reserve(m.vertexCount);
    size_t k = 0;
    for (int r = 0; r < m.regions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            const size_t si = size_t(pieceOf_.empty() ? v.shard : pieceOf_[size_t(v.shard)]);
            const Shard& s = shards_[si];
            const Vertex& rest = shatterRest_[k++];
            out.heroVerts.push_back({s.c + rot[si] * ((rest.pos - s.c0) * fade), rot[si] * rest.normal, rest.uv});
        }
    out.heroFirst = m.firstMesh;
    out.heroMeshes = m.regions;
    out.heroModel = shatterModel_;
    for (int r = 0; r < m.regions; ++r) {
        LitMaterial l = m.textured() ? regionMaterial(r) : material(kRegionMat[r]);
        switch (breakup_) {
        case Breakup::Glass: // glassy: a bright, cool fresnel edge on every shard
            l.rimColor = glm::vec3(0.75f, 0.95f, 1.f);
            l.rimStrength = 3.2f;
            l.rimPow = 2.f;
            l.roughness = std::min(l.roughness, 0.18f);
            break;
        case Breakup::Slice: { // her own colours, the cut's heat glowing on her edges and fading
            const float heat = std::max(0.f, 1.f - shatterT_ / 1.2f);
            l.rimColor = glm::vec3(1.f, 0.35f, 0.12f) * (0.6f + 2.4f * heat);
            l.rimStrength = 1.5f + 2.5f * heat;
            l.rimPow = 2.4f;
            l.emissive += glm::vec3(1.f, 0.25f, 0.05f) * 0.5f * heat;
            break;
        }
        case Breakup::Char: { // charring black from her feet up, embers glowing in it, then ash
            const float burnt = glm::smoothstep(0.f, 0.6f, shatterT_);
            const float ember = (0.6f + 0.4f * std::sin(shatterT_ * 23.f + float(r))) * (1.f - glm::smoothstep(0.8f, kShatterLife, shatterT_));
            l.color = glm::mix(l.color, glm::vec3(0.07f, 0.05f, 0.04f), burnt);
            l.metalness *= 1.f - burnt;
            l.roughness = glm::mix(l.roughness, 1.f, burnt);
            // (a textured hen glows by its emissive wherever her texels are warm, which is all over: kept to a smoulder,
            // the embers are in the rim, her edges glowing)
            l.emissive = glm::mix(l.emissive, glm::vec3(1.f, 0.32f, 0.06f) * 0.12f * ember, burnt);
            l.rimColor = glm::vec3(1.f, 0.4f, 0.1f) * (0.3f + 1.2f * ember);
            l.rimStrength = 1.6f * burnt;
            l.rimPow = 4.f;
            break;
        }
        }
        out.add(Pass::LitTwoSided, MeshId(int(m.firstMesh) + r)) = makeLit(glm::mat4(1.f), l);
    }
}

} // namespace cs
