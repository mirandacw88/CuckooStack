#include "HudImage.h"
#include "Log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace cs::assets {
extern const unsigned char lives_icons[];
extern const size_t lives_icons_size;
} // namespace cs::assets

namespace cs {

bool HudImage::buildLivesIcons() {
    int n = 0;
    unsigned char* data = stbi_load_from_memory(assets::lives_icons, int(assets::lives_icons_size), &w_, &h_, &n, 4);
    if (!data) { CS_LOGE("Lives icon sheet failed to decode"); return false; }
    pixels_.assign(data, data + size_t(w_) * h_ * 4);
    stbi_image_free(data);
    // premultiply alpha: soft edges filter cleanly (no dark fringes) and blend with ONE / ONE_MINUS_SRC_ALPHA
    for (size_t i = 0; i < pixels_.size(); i += 4) {
        const unsigned a = pixels_[i + 3];
        for (int c = 0; c < 3; ++c) pixels_[i + c] = uint8_t((pixels_[i + c] * a + 127) / 255);
    }
    return true;
}

} // namespace cs
