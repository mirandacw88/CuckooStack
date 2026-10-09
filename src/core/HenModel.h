// The hen models: the artist's hens, embedded in the build. The Classic hen (scripts/models/build_hen_mesh.py) is split
// into colour regions that every outfit recolours; an outfit can also have its own textured model
// (scripts/models/add_outfit_model.py, listed in OutfitModels.inc and looked up by outfit id). All are already in the
// hen's body space (Hen.cpp), weighted to the game's own animated nodes (body, head, wings, tail), which bend them
// every frame (Hen::emit), and pre-cut into glass shards for the crash.
#pragma once

#include "RenderList.h"

#include <array>
#include <cstdint>
#include <vector>
#include "OutfitModels.h"

namespace cs {

// 0 = Classic; 1 .. kOutfitModelCount = the outfit models, in OutfitModels.inc order
using HenModelId = int;
constexpr HenModelId kClassicHen = 0;

struct HenModel {
    enum Region { Body, Accent, Lens, Shade, Beak, kRegions };  // Classic: = MeshId::HenBody + region
    enum Bone { BBody, BHead, BWing0, BWing1, BTail, kBones };
    struct V {
        glm::vec3 pos, normal;
        float w[kBones];
        float shard;                 // glass shard this vertex belongs to (crash shatter)
        glm::vec2 uv;                // textured models only
    };
    int regions = 0;                 // Classic: kRegions; textured: 1
    std::array<std::vector<V>, kRegions> verts;
    std::array<std::vector<uint16_t>, kRegions> indices;
    size_t vertexCount = 0;
    int shardCount = 0;
    MeshId firstMesh = MeshId::HenBody; // region r is drawn as MeshId(firstMesh + r)
    // textured models: an embedded JPEG (the renderer uploads it; the lit shader's hero-texture pattern samples it)
    const unsigned char* texture = nullptr;
    size_t textureSize = 0;
    const unsigned char* normalMap = nullptr; // tangent-space detail baked from the full-detail model (JPEG)
    size_t normalMapSize = 0;
    bool textured() const { return texture != nullptr; }
    float glow = 1.f, metal = 0.05f, rough = 0.55f; // textured models' look (add_outfit_model.py --glow/--metal/--rough)
    bool ok = false;
};

// Parsed once from the embedded assets; ok = false if missing or malformed (the procedural hen is used then).
const HenModel& henModel(HenModelId id = kClassicHen);
// the model an outfit wears: its own if it has one, otherwise the Classic hen
HenModelId henModelFor(const char* outfitId);

} // namespace cs
