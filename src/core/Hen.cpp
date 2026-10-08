#include "Hen.h"

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
}

LitMaterial Hen::material(Mat m) const {
    LitMaterial l;
    const bool rimmed = m == Mat::White || m == Mat::Plate || m == Mat::Chrome || m == Mat::Gold;
    if (rimmed) { l.rimColor = rimColor; l.rimStrength = 2.1f; l.rimPow = 2.4f; }
    switch (m) {
    case Mat::White:   l.color = hexColor("#f4f7ff"); l.roughness = 0.62f; l.emissive = glm::vec3(0.07f); break;
    case Mat::Plate:   l.color = hexColor("#2b3044"); l.metalness = 0.9f; l.roughness = 0.26f; break;
    case Mat::Chrome:  l.color = hexColor("#dfe5f0"); l.metalness = 1.f; l.roughness = 0.22f; break;
    case Mat::Gold:    l.color = hexColor("#ffb347"); l.metalness = 1.f; l.roughness = 0.24f; break;
    case Mat::Pink:    l.color = hexColor("#ff2bd6"); l.emissive = l.color * 2.6f; l.roughness = 0.4f; break;
    case Mat::PinkDim: l.color = hexColor("#ff2bd6"); l.emissive = l.color * 0.9f; l.roughness = 0.4f; break;
    case Mat::Cyan:    l.color = cyanColor; l.emissive = cyanColor * cyanIntensity; l.roughness = 0.2f; break;
    case Mat::TipRed:  l.color = glm::vec3(0.f); l.emissive = glow("#ff2b4a", 5.f); break;
    }
    return l;
}

void Hen::emit(RenderList& out) const {
    std::vector<glm::mat4> world(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& n = nodes[i];
        const glm::mat4 local = compose(n.pos, n.rot, n.scale);
        world[i] = n.parent < 0 ? local : world[n.parent] * local;
    }
    for (const Part& p : parts_) {
        if (p.node == tipNode_ && !tipVisible) continue;
        out.add(Pass::Lit, p.mesh) = makeLit(world[p.node] * p.local, material(p.mat));
    }
}

} // namespace cs
