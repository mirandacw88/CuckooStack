// The hen model: the artist's rigged hen, cleaned and auto-rigged by scripts/models/clean_hen_glb.py +
// build_hen_mesh.py into assets/models/hen.mesh (embedded in the build). Already in the hen's body space (Hen.cpp).
// Split into colour regions so every outfit recolours it with its own materials, and weighted to the game's own
// animated nodes (body, head, wings, tail), which bend it every frame (Hen::emit).
#pragma once

#include "RenderList.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cs {

struct HenModel {
    enum Region { Body, Accent, Lens, Shade, Beak, kRegions };  // = MeshId::HenBody + region
    enum Bone { BBody, BHead, BWing0, BWing1, BTail, kBones };
    struct V {
        glm::vec3 pos, normal;
        float w[kBones];
        float shard;                 // glass shard this vertex belongs to (Glass Shatter crash effect)
    };
    std::array<std::vector<V>, kRegions> verts;
    std::array<std::vector<uint16_t>, kRegions> indices;
    size_t vertexCount = 0;
    int shardCount = 0;
    bool ok = false;
};

// Parsed once from the embedded asset; ok = false if it's missing or malformed (the procedural hen is used then).
const HenModel& henModel();

} // namespace cs
