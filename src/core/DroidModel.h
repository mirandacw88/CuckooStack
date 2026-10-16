// The jet droid that flies on the title screen, opposite the hen: the artist's Blender model, exported by
// scripts/models/export_droid.py (assets/models/droid.mesh + droid.jpg + droid_n.jpg) and embedded in the build.
// Parts are in the droid's own space (metres, y up, its nose facing -x, centred on its body); the fans are around
// their own pivots so they can spin. The Blender animation is procedural (cycled fan spins, noise on the flames), so
// the game replays it (Game::emitDroid) from the rates and sizes stored here.
#pragma once

#include "Math.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cs {

struct DroidModel {
    enum Part { Body, Eye, FanL, FanR, Shroud, Backplate, Flame, kParts };
    struct V { glm::vec3 pos, normal; glm::vec2 uv; };
    struct Mesh {
        std::vector<V> verts;
        std::vector<uint32_t> indices;
        bool textured = false, glowing = false;
        glm::vec3 color{1.f};
        float metal = 0.f, rough = 0.5f, glow = 0.f;
        glm::vec3 pivot{0.f}, axis{1.f, 0.f, 0.f}; // fans: where and around what they spin (droid space)
        float spin = 0.f;                         // radians per second
    };
    struct Anchor { glm::vec3 pos{0.f}, dir{1.f, 0.f, 0.f}; glm::vec4 size{0.f}; };
    std::array<Mesh, kParts> parts;
    // eye; exhaust_l / exhaust_r (size: flame length, radius, core length, core radius)
    Anchor eye, exhaust[2];
    // the textures (embedded JPEGs; the renderer uploads them once)
    const unsigned char* texture = nullptr;
    size_t textureSize = 0;
    const unsigned char* normalMap = nullptr;
    size_t normalMapSize = 0;
    bool ok = false;
};

// Parsed once from the embedded asset; ok = false if it's missing or malformed (the title screen then goes without it)
const DroidModel& droidModel();
// The same, from bytes (tests)
DroidModel parseDroidModel(const unsigned char* data, size_t size);

} // namespace cs
