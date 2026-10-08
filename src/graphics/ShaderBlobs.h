// SPIR-V 1.0 blobs compiled offline from assets/shaders (see cmake/EmbedShaders.cmake). Embedding avoids
// asset I/O on every platform (no AAssetManager, no NSBundle lookups) and keeps shaders in sync with the binary.
#pragma once

#include <cstddef>
#include <cstdint>

#define CS_DECLARE_SHADER(name)              \
    namespace cs::spv {                      \
    extern const uint32_t name[];            \
    extern const size_t name##_size; /* bytes */ \
    }

CS_DECLARE_SHADER(sky_vert)
CS_DECLARE_SHADER(sky_frag)
CS_DECLARE_SHADER(lit_vert)
CS_DECLARE_SHADER(lit_frag)
CS_DECLARE_SHADER(unlit_vert)
CS_DECLARE_SHADER(unlit_frag)
CS_DECLARE_SHADER(particle_vert)
CS_DECLARE_SHADER(particle_frag)
CS_DECLARE_SHADER(fullscreen_vert)
CS_DECLARE_SHADER(composite_frag)
CS_DECLARE_SHADER(bloom_frag)
CS_DECLARE_SHADER(text_vert)
CS_DECLARE_SHADER(text_frag)
CS_DECLARE_SHADER(image_frag)

#undef CS_DECLARE_SHADER
