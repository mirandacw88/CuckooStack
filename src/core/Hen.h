// The cyber-hen: a port of makeChicken() as a flat node hierarchy of primitive parts.
// Animated nodes (body, head, wings, legs, tail) are exposed so Game drives them exactly like the web build.
#pragma once

#include "Materials.h"

#include <vector>

namespace cs {

// An outfit (Economy.h catalog): the colours of the hen's materials. Defaults = the original cyber-hen.
struct HenSkin {
    glm::vec3 body = hexColor("#f4f7ff");   // feathers
    float bodyMetal = 0.f, bodyRough = 0.62f, bodyGlow = 0.07f;
    glm::vec3 plate = hexColor("#2b3044");  // armour plates
    glm::vec3 chrome = hexColor("#dfe5f0"); // legs, antenna
    glm::vec3 gold = hexColor("#ffb347");   // beak
    glm::vec3 accent = hexColor("#ff2bd6"); // comb, tail and wing stripes (glowing)
    glm::vec3 visor = hexColor("#29e7ff");  // visor + LEDs
    bool rainbow = false;                   // accent and rim light cycle through the hue wheel (Glitch Hen)
};

class Hen {
public:
    struct Node {
        int parent = -1;
        glm::vec3 pos{0.f}, rot{0.f}, scale{1.f};
    };
    enum class Mat : uint8_t { White, Plate, Chrome, Gold, Pink, PinkDim, Cyan, TipRed };

    Hen();

    // animated handles
    int root, body, head, tail;
    int wings[2], legs[2];
    std::vector<Node> nodes;

    HenSkin skin;
    float time = 0.f;            // drives animated skins
    bool tipVisible = true;
    glm::vec3 rimColor{0.f};     // henRim.color, animated by danger
    glm::vec3 cyanColor{0.f};    // visor / LEDs, lerps toward red with danger
    float cyanIntensity = 3.2f;

    void emit(RenderList& out) const;

private:
    struct Part { int node; MeshId mesh; Mat mat; glm::mat4 local; };
    int add(int parent, glm::vec3 pos = {}, glm::vec3 rot = {}, glm::vec3 scale = {1, 1, 1});
    void part(int parent, MeshId mesh, Mat mat, glm::vec3 pos, glm::vec3 rot, glm::vec3 scale);
    LitMaterial material(Mat m) const;
    glm::vec3 accent() const;
    std::vector<Part> parts_;
    int tipNode_ = -1;
};

} // namespace cs
