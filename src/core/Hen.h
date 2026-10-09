// The cyber-hen: a port of makeChicken() as a flat node hierarchy of primitive parts.
// Animated nodes (body, head, wings, legs, tail) are exposed so Game drives them exactly like the web build.
#pragma once

#include "HenModel.h"
#include "Materials.h"

#include <array>
#include <vector>

namespace cs {

// An outfit (Economy.h catalog): the colours of the hen's materials. Defaults = the original cyber-hen.
// An outfit's particle effect, emitted around the hen while it's on screen (Game::emitAura)
enum class Aura : uint8_t { None, Magma, Cryo, Toxic, Gold, Chrome, Tiger, Holo, Sakura, Vapor, Midnight, Glitch };

struct HenSkin {
    glm::vec3 body = hexColor("#f4f7ff");   // feathers
    float bodyMetal = 0.f, bodyRough = 0.62f, bodyGlow = 0.07f;
    glm::vec3 plate = hexColor("#2b3044");  // armour plates
    glm::vec3 chrome = hexColor("#dfe5f0"); // legs, antenna
    glm::vec3 gold = hexColor("#ffb347");   // beak
    glm::vec3 accent = hexColor("#ff2bd6"); // comb, tail and wing stripes (glowing)
    glm::vec3 visor = hexColor("#29e7ff");  // visor + LEDs
    bool rainbow = false;                   // accent and rim light cycle through the hue wheel (Glitch Hen)
    Aura aura = Aura::None;                 // particle effect worn with the outfit
    HenModelId model = kClassicHen;         // the 3D model worn (set from the outfit id: henModelFor)
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
    bool useModel = false;       // the artist's model (HenModel.h) with the robot legs, instead of the procedural body
    float time = 0.f;            // drives animated skins
    bool tipVisible = true;
    glm::vec3 rimColor{0.f};     // henRim.color, animated by danger
    glm::vec3 cyanColor{0.f};    // visor / LEDs, lerps toward red with danger
    float cyanIntensity = 3.2f;

    void emit(RenderList& out) const;

    // Glass Shatter (crash effect, model hen only): the hen as posed right now breaks into the model's pre-cut shards,
    // which fly away from `impact` with the hen's `velocity`, spin, bounce on `floorY`, then shrink away.
    bool canShatter() const { return useModel && model().ok; }
    const HenModel& model() const { return henModel(skin.model); }
    void shatter(glm::vec3 impact, glm::vec3 velocity, float floorY, uint32_t seed);
    void updateShatter(float dt);
    void unshatter() { shattered_ = false; }
    bool shattered() const { return shattered_; }
    static constexpr float kShatterLife = 2.4f;

private:
    struct Part { int node; MeshId mesh; Mat mat; glm::mat4 local; };
    int add(int parent, glm::vec3 pos = {}, glm::vec3 rot = {}, glm::vec3 scale = {1, 1, 1});
    void part(int parent, MeshId mesh, Mat mat, glm::vec3 pos, glm::vec3 rot, glm::vec3 scale);
    LitMaterial material(Mat m) const;
    glm::vec3 accent() const;
    std::vector<Part> parts_;
    int tipNode_ = -1;
    size_t firstLegPart_ = 0;                 // with the model, only the legs (parts from here on) stay procedural
    std::array<glm::mat4, 5> boneRestInv_{};  // body-space rest transform of each model bone's node, inverted
    void emitModel(RenderList& out, const std::vector<glm::mat4>& world) const;
    std::vector<glm::mat4> worldTransforms() const;
    void poseModel(const std::vector<glm::mat4>& world, std::vector<Vertex>& out) const; // body-space posed vertices
    void emitShards(RenderList& out) const;
    LitMaterial regionMaterial(int r) const;
    HenModelId shatterModel_ = kClassicHen;

    struct Shard { glm::vec3 c0, c, v, axis; float angle, spin; };
    bool shattered_ = false;
    float shatterT_ = 0.f, floorY_ = 0.f;
    std::vector<Shard> shards_;
    std::vector<Vertex> shatterRest_; // world-space vertices at the moment of the crash, in heroVerts order
};

} // namespace cs
