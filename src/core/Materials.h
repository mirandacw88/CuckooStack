// Material presets lifted from the MeshStandardMaterial / MeshBasicMaterial definitions in the web build.
#pragma once

#include "RenderList.h"

namespace cs {

struct LitMaterial {
    glm::vec3 color{1.f};
    glm::vec3 emissive{0.f};   // emissive * emissiveIntensity, linear
    float metalness = 0.f, roughness = 1.f;
    glm::vec3 rimColor{0.f};
    float rimStrength = 0.f, rimPow = 3.f;
    Pattern pattern = Pattern::None;
};

inline Instance makeLit(const glm::mat4& model, const LitMaterial& m, float seed = 0.f, float tint = 1.f) {
    Instance i;
    i.model = model;
    i.color = glm::vec4(m.color * tint, 1.f);
    i.emissive = glm::vec4(m.emissive, seed);
    i.rim = glm::vec4(m.rimColor, m.rimStrength);
    i.params = glm::vec4(m.metalness, m.roughness, float(m.pattern), m.rimPow);
    return i;
}

// MeshBasicMaterial with fog: drawn in the opaque lit pass (writes depth), shading skipped.
inline Instance makeEmissive(const glm::mat4& model, const glm::vec3& color) {
    Instance i;
    i.model = model;
    i.color = glm::vec4(0.f, 0.f, 0.f, 1.f);
    i.emissive = glm::vec4(color, 0.f);
    i.params = glm::vec4(-1.f, 1.f, 0.f, 1.f);
    return i;
}

inline Instance makeUnlit(const glm::mat4& model, const glm::vec3& color, float opacity = 1.f,
                          Shape shape = Shape::Solid, float shapeParam = 0.f) {
    Instance i;
    i.model = model;
    i.color = glm::vec4(color, opacity);
    i.params = glm::vec4(0.f, 0.f, float(shape), shapeParam);
    return i;
}

} // namespace cs
