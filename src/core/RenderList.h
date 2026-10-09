// The only contract between the game layer and the renderer. The game fills a RenderList every frame;
// the renderer uploads it. Nothing in here knows about Vulkan, and nothing in graphics/ knows game rules.
#pragma once

#include "Math.h"
#include "OutfitModels.h"

#include <array>
#include <cstddef>
#include <vector>

namespace cs {

// Procedural meshes built once by Geometry.cpp and uploaded by the renderer.
// Mesh vertex (Geometry.h builds them). 32 bytes, matches the vertex layout in VulkanPipeline.cpp.
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};
static_assert(sizeof(Vertex) == 32, "Vertex must stay 32 bytes (GPU layout)");

enum class MeshId : uint8_t {
    Box,         // 1x1x1, three.js BoxGeometry face layout (per-face UVs)
    Sphere,      // r=1, 32x24
    SphereLow,   // r=1, 24x12: disco ball facets
    Cylinder,    // r=1, h=1, centred
    Cone,        // base r=1, h=1, centred
    Egg,         // lathe profile from the web build, real size
    EggRing,     // torus 0.254 / 0.022
    Visor,       // torus arc 0.188 / 0.04, 2.5 rad
    SmallRing,   // torus 0.075 / 0.018 (bollard collars)
    Plane,       // 1x1 in XY, facing +Z
    RingFlat,    // ring 0.86..1 in XY (shockwaves)
    Circle,      // r=1 disc in XY
    // the hen model (HenModel.h), one mesh per colour region; skinned on the CPU every frame (RenderList::heroVerts)
    HenBody, HenAccent, HenLens, HenShade, HenBeak,
    HenOutfit0,  // outfits with their own textured hen model follow: HenOutfit0 + i (OutfitModels.inc)
    Count = int(HenOutfit0) + kOutfitModelCount
};
constexpr size_t kMeshCount = static_cast<size_t>(MeshId::Count);
static_assert(kMeshCount <= 255, "MeshId is a uint8_t");

// Procedural surface patterns, evaluated in the lit fragment shader instead of uploading canvas textures.
// HeroTexture: the textured hen model's albedo (frame set binding 2); warm bright texels glow by the emissive amount
enum class Pattern : uint8_t { None = 0, Container = 1, Hazard = 2, Asphalt = 3, Windows = 4, DiscoBall = 5, HeroTexture = 6 };

// Shape masks for the unlit shader (replaces the small canvas gradient textures).
enum class Shape : uint8_t {
    Solid = 0, RadialGlow = 1, Curtain = 2, Streak = 3, Feather = 4, EggCell = 5,
    PillOutline = 6, BottomFade = 7, SoftDisc = 8, HBeam = 9, Scanlines = 10,
    Laser = 11, Spot = 12
};

// One instanced draw item. 128 bytes, matches the per-instance vertex layout in VulkanPipeline.cpp.
struct Instance {
    glm::mat4 model{1.f};
    glm::vec4 color{1.f};      // lit: albedo (a unused) | unlit: rgb + opacity
    glm::vec4 emissive{0.f};   // lit: emissive radiance rgb, w = pattern seed
    glm::vec4 rim{0.f};        // lit: fresnel rim colour rgb, w = strength
    glm::vec4 params{0.f};     // lit: metalness (<0 = emissive only), roughness, pattern, rimPow | unlit: -, -, shape, shape param
};
static_assert(sizeof(Instance) == 128, "Instance must stay 128 bytes (GPU layout)");

// CPU-simulated particle drawn as a camera-facing quad. 32 bytes.
struct Particle {
    glm::vec4 posSize;     // xyz world position, w world-space diameter
    glm::vec4 colorAlpha;  // linear rgb, alpha
};
static_assert(sizeof(Particle) == 32, "Particle must stay 32 bytes (GPU layout)");

// LitTwoSided: lit without back-face culling (the hen's glass shards, open shells seen from both sides)
enum class Pass : uint8_t { Lit = 0, UnlitAlpha = 1, UnlitAdd = 2, LitTwoSided = 3, Count };
constexpr size_t kPassCount = static_cast<size_t>(Pass::Count);

struct PointLight { glm::vec3 pos{0.f}; glm::vec3 color{0.f}; float intensity = 0.f; float distance = 0.f; };

struct FrameParams {
    glm::mat4 view{1.f}, proj{1.f};
    glm::vec3 cameraPos{0.f};
    glm::vec3 fogColor{0.f}; float fogDensity = 0.016f;
    glm::vec3 skyTop{0.f}, skyMid{0.f}, skyBot{0.f}, skyHaze{0.f}; glm::vec3 moonDir{0, 1, 0};
    glm::vec3 hemiSky{0.f}, hemiGround{0.f}; float hemiIntensity = 0.f;
    glm::vec3 sunDir{0, 1, 0}; glm::vec3 sunColor{0.f};
    std::array<PointLight, 3> points{};
    float time = 0.f, pulse = 0.f;
    // final composite (chromaPass in the web build)
    float chromaAmount = 0.2f, vignette = 0.45f, bloomStrength = 0.85f;
    glm::vec4 vignetteTint{0.f}; // rgb (display space) + strength: party-mode colour edge
    // frosted glass: the scene is blurred inside this rounded rect (HUD points: centre xy, size zw) before the HUD
    // draws its glass panel on top. frostAmount 0 = off.
    bool showcase = false; // the hen is shown up close (Locker): draw the 3D scene at full resolution
    glm::vec4 frostRect{0.f};
    float frostRadius = 0.f, frostAmount = 0.f;
    // HUD space, in logical points
    float viewportW = 1.f, viewportH = 1.f;
};

class FontAtlas;
class HudImage;
enum HudKind : uint8_t { HudShape = 0, HudText = 1, HudSprite = 2 };

struct RenderList {
    std::array<std::array<std::vector<Instance>, kMeshCount>, kPassCount> buckets;
    std::vector<Particle> particlesAdd;   // additive sparks, dust, rain splashes
    std::vector<Particle> particlesSmoke; // normal-blended puffs
    // posed vertices of the hen meshes heroFirst .. heroFirst + heroMeshes - 1, in that order (the renderer streams them
    // over the rest pose); empty = draw the rest pose
    std::vector<Vertex> heroVerts;
    MeshId heroFirst = MeshId::HenBody;
    int heroMeshes = 0;
    int heroModel = 0; // HenModelId worn: a textured model's texture is uploaded when it changes
    // HUD in paint order (like DOM stacking): Plane quads (unlit shapes), SDF glyphs and sprites, interleaved.
    // hudKind[i] says which pipeline draws hud[i] (HudKind); the renderer batches consecutive runs.
    std::vector<Instance> hud;
    std::vector<uint8_t> hudKind;
    const class HudImage* hudImage = nullptr; // sprite atlas, uploaded once by the renderer
    std::vector<Instance> worldText;      // SDF glyphs in world space (neon signs, today's-best gate)
    const FontAtlas* fontAtlas = nullptr; // uploaded once by the renderer
    FrameParams frame;

    void clear() {
        for (auto& pass : buckets) for (auto& v : pass) v.clear();
        particlesAdd.clear(); particlesSmoke.clear(); hud.clear(); hudKind.clear(); worldText.clear(); heroVerts.clear();
    }
    Instance& add(Pass p, MeshId m) {
        return buckets[static_cast<size_t>(p)][static_cast<size_t>(m)].emplace_back();
    }
    const std::vector<Instance>& get(Pass p, MeshId m) const {
        return buckets[static_cast<size_t>(p)][static_cast<size_t>(m)];
    }
};

} // namespace cs
