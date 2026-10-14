// Material presets (Materials.h): how a lit, emissive-only or unlit material is packed into the 128-byte Instance
// the shaders read (lit.frag / unlit.frag: color, emissive + pattern seed, rim, params).
#include "TestUtil.h"
#include "core/Materials.h"

using namespace cs;
using namespace test;

int main() {
    const glm::mat4 model = compose({1, 2, 3}, glm::vec3(0.f), glm::vec3(2.f));
    LitMaterial m;
    m.color = {0.2f, 0.4f, 0.6f};
    m.emissive = {1, 0.5f, 0};
    m.metalness = 0.9f; m.roughness = 0.26f;
    m.rimColor = {0, 1, 1}; m.rimStrength = 2.1f; m.rimPow = 2.4f;
    m.pattern = Pattern::Hazard;
    const Instance lit = makeLit(model, m, 0.75f, 0.5f);
    check(lit.model == model, "lit: the model matrix");
    check(lit.color == glm::vec4(0.1f, 0.2f, 0.3f, 1.f), "lit: albedo times the tint, alpha 1");
    check(lit.emissive == glm::vec4(1, 0.5f, 0, 0.75f), "lit: emissive rgb, the pattern seed in w");
    check(lit.rim == glm::vec4(0, 1, 1, 2.1f), "lit: rim colour, strength in w");
    check(lit.params == glm::vec4(0.9f, 0.26f, float(Pattern::Hazard), 2.4f), "lit: metalness, roughness, pattern, rim power");
    const Instance plain = makeLit(model, LitMaterial{});
    check(plain.params.x >= 0.f && plain.params.z == float(Pattern::None) && plain.emissive.w == 0.f && plain.color == glm::vec4(1.f),
          "lit defaults: white, rough, no pattern, seed 0");

    const Instance glow = makeEmissive(model, {0, 2, 3});
    check(glow.params.x < 0.f, "emissive-only: metalness < 0 tells the lit shader to skip shading");
    check(glow.emissive == glm::vec4(0, 2, 3, 0) && glow.color == glm::vec4(0, 0, 0, 1), "emissive-only: just the glow, black albedo");

    const Instance quad = makeUnlit(model, {1, 0.5f, 0.25f}, 0.4f, Shape::PillOutline, 0.1f);
    check(quad.color == glm::vec4(1, 0.5f, 0.25f, 0.4f), "unlit: rgb + opacity");
    check(quad.params.z == float(Shape::PillOutline) && quad.params.w == 0.1f, "unlit: the shape and its parameter");
    check(makeUnlit(model, glm::vec3(1)).params.z == float(Shape::Solid) && makeUnlit(model, glm::vec3(1)).color.a == 1.f,
          "unlit defaults: a solid, opaque quad");
    return finish("Materials");
}
