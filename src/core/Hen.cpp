#include "Hen.h"
#include "HenModel.h"

namespace cs {

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
        part(lp, MeshId::Cylinder, Mat::Chrome, {0, -0.11f, 0}, {}, {0.023f, 0.22f, 0.023f});
        S(lp, 0.034f, Mat::Cyan, {});
        for (float a : {-0.45f, 0.f, 0.45f}) // toes
            part(lp, MeshId::Cylinder, Mat::Chrome, {0.05f * std::cos(a), -0.22f, -0.05f * std::sin(a)}, {0, a, -kPi / 2},
                 {0.0125f, 0.13f, 0.0125f});
        legs[k] = lp;
    }

    // the model's bones are the animated nodes; remember where each sits in body space at rest
    useModel = henModel().ok;
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
    for (size_t i = useModel ? firstLegPart_ : 0; i < parts_.size(); ++i) {
        const Part& p = parts_[i];
        if (p.node == tipNode_ && !tipVisible) continue;
        out.add(Pass::Lit, p.mesh) = makeLit(world[p.node] * p.local, material(p.mat));
    }
    if (useModel) emitModel(out, world);
}

// Linear-blend skinning on the CPU: each bone is one of the animated nodes, so everything that animates the
// procedural hen (wing flaps, head bob, tail wag, squash) bends the model the same way.
void Hen::poseModel(const std::vector<glm::mat4>& world, std::vector<Vertex>& out) const {
    const HenModel& m = henModel();
    const int bones[HenModel::kBones] = {body, head, wings[0], wings[1], tail};
    const glm::mat4 bodyInv = glm::inverse(world[size_t(body)]);
    glm::mat4 M[HenModel::kBones];
    glm::mat3 R[HenModel::kBones];
    for (int k = 0; k < HenModel::kBones; ++k) {
        M[k] = bodyInv * world[size_t(bones[k])] * boneRestInv_[size_t(k)];
        R[k] = glm::mat3(M[k]);
    }
    out.reserve(out.size() + m.vertexCount);
    for (int r = 0; r < HenModel::kRegions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            glm::vec3 p{0.f}, n{0.f};
            for (int k = 0; k < HenModel::kBones; ++k) {
                if (v.w[k] <= 0.f) continue;
                p += v.w[k] * glm::vec3(M[k] * glm::vec4(v.pos, 1.f));
                n += v.w[k] * (R[k] * v.normal);
            }
            out.push_back({p, glm::normalize(n), {0.f, 0.f}});
        }
}

namespace {
constexpr Hen::Mat kRegionMat[HenModel::kRegions] = {Hen::Mat::White, Hen::Mat::PinkDim, Hen::Mat::Cyan, Hen::Mat::Plate, Hen::Mat::Gold};
}

void Hen::emitModel(RenderList& out, const std::vector<glm::mat4>& world) const {
    poseModel(world, out.heroVerts);
    const glm::mat4& bodyWorld = world[size_t(body)];
    for (int r = 0; r < HenModel::kRegions; ++r)
        out.add(Pass::Lit, MeshId(int(MeshId::HenBody) + r)) = makeLit(bodyWorld, material(kRegionMat[r]));
}

void Hen::shatter(glm::vec3 impact, glm::vec3 velocity, float floorY, uint32_t seed) {
    if (!useModel) return;
    const HenModel& m = henModel();
    const std::vector<glm::mat4> world = worldTransforms();
    std::vector<Vertex> posed;
    poseModel(world, posed);
    const glm::mat4& bodyWorld = world[size_t(body)];
    const glm::mat3 bodyRot = glm::mat3(bodyWorld);
    shatterRest_.resize(posed.size());
    std::vector<glm::vec3> sum(size_t(m.shardCount), glm::vec3(0.f));
    std::vector<int> cnt(size_t(m.shardCount), 0);
    size_t k = 0;
    for (int r = 0; r < HenModel::kRegions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            const glm::vec3 p = glm::vec3(bodyWorld * glm::vec4(posed[k].pos, 1.f));
            shatterRest_[k] = {p, glm::normalize(bodyRot * posed[k].normal), {0.f, 0.f}};
            sum[size_t(v.shard)] += p;
            ++cnt[size_t(v.shard)];
            ++k;
        }
    uint32_t state = seed * 747796405u + 2891336453u;
    auto rnd = [&state](float lo, float hi) {
        state = state * 747796405u + 2891336453u;
        const uint32_t w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return lo + (hi - lo) * float((w >> 22u) ^ w) / 4294967296.f;
    };
    shards_.assign(size_t(m.shardCount), {});
    for (size_t i = 0; i < shards_.size(); ++i) {
        Shard& s = shards_[i];
        s.c0 = s.c = cnt[i] ? sum[i] / float(cnt[i]) : impact;
        glm::vec3 out = s.c0 - impact + glm::vec3(rnd(-0.2f, 0.2f), rnd(0.f, 0.3f), rnd(-0.2f, 0.2f));
        out = glm::length(out) > 1e-4f ? glm::normalize(out) : glm::vec3(0, 1, 0);
        s.v = out * rnd(2.5f, 6.f) + velocity * 0.45f + glm::vec3(0, rnd(1.5f, 3.5f), rnd(-1.f, 1.f));
        s.axis = glm::normalize(glm::vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)) + glm::vec3(0.001f));
        s.angle = 0.f;
        s.spin = rnd(5.f, 14.f) * (rnd(0, 1) < 0.5f ? -1.f : 1.f);
    }
    floorY_ = floorY;
    shatterT_ = 0.f;
    shattered_ = true;
}

void Hen::updateShatter(float dt) {
    if (!shattered_) return;
    shatterT_ += dt;
    for (Shard& s : shards_) {
        s.v.y -= 14.f * dt;
        s.c += s.v * dt;
        s.angle += s.spin * dt;
        if (s.c.y < floorY_ + 0.04f && s.v.y < 0.f) { // bounce, scattering on the ground
            s.c.y = floorY_ + 0.04f;
            s.v.y = -s.v.y * 0.3f;
            s.v.x *= 0.55f; s.v.z *= 0.55f;
            s.spin *= 0.5f;
        }
    }
}

// The shards, each a rigid piece: rotated about its own centre, carried along its path, shrinking away at the end.
void Hen::emitShards(RenderList& out) const {
    const HenModel& m = henModel();
    const float fade = 1.f - glm::smoothstep(kShatterLife * 0.6f, kShatterLife, shatterT_);
    if (fade <= 0.f) return;
    std::vector<glm::mat3> rot(shards_.size());
    for (size_t i = 0; i < shards_.size(); ++i) rot[i] = glm::mat3(glm::rotate(glm::mat4(1.f), shards_[i].angle, shards_[i].axis));
    out.heroVerts.reserve(m.vertexCount);
    size_t k = 0;
    for (int r = 0; r < HenModel::kRegions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            const size_t si = size_t(v.shard);
            const Shard& s = shards_[si];
            const Vertex& rest = shatterRest_[k++];
            out.heroVerts.push_back({s.c + rot[si] * ((rest.pos - s.c0) * fade), rot[si] * rest.normal, {0.f, 0.f}});
        }
    // glassy: a bright, cool fresnel edge on every shard
    for (int r = 0; r < HenModel::kRegions; ++r) {
        LitMaterial l = material(kRegionMat[r]);
        l.rimColor = glm::vec3(0.75f, 0.95f, 1.f);
        l.rimStrength = 3.2f;
        l.rimPow = 2.f;
        l.roughness = std::min(l.roughness, 0.18f);
        out.add(Pass::LitTwoSided, MeshId(int(MeshId::HenBody) + r)) = makeLit(glm::mat4(1.f), l);
    }
}

} // namespace cs
