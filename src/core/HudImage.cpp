#include "HudImage.h"
#include "Log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG // the textured hen model (HenModel, Renderer::uploadHeroTexture)
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
extern const unsigned char hud_icons[];
extern const size_t hud_icons_size;
} // namespace cs::assets

namespace cs {

bool HudImage::buildIcons() {
    int n = 0;
    unsigned char* data = stbi_load_from_memory(assets::hud_icons, int(assets::hud_icons_size), &w_, &h_, &n, 4);
    if (!data) { CS_LOGE("HUD icon atlas failed to decode"); return false; }
    pixels_.assign(data, data + size_t(w_) * h_ * 4);
    stbi_image_free(data);
    // premultiply alpha: soft edges filter cleanly (no dark fringes) and blend with ONE / ONE_MINUS_SRC_ALPHA
    for (size_t i = 0; i < pixels_.size(); i += 4) {
        const unsigned a = pixels_[i + 3];
        for (int c = 0; c < 3; ++c) pixels_[i + c] = uint8_t((pixels_[i + c] * a + 127) / 255);
    }
    // 3 extra levels: 192 px cells stay whole (96, 48, 24), so a level never mixes two icons
    mips_.clear();
    const std::vector<uint8_t>* src = &pixels_;
    int w = w_, h = h_;
    for (int level = 1; level <= 3 && w % 2 == 0 && h % 2 == 0; ++level) {
        const int nw = w / 2, nh = h / 2;
        std::vector<uint8_t> dst(size_t(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto px = [&](int sx, int sy) { return unsigned((*src)[(size_t(sy) * w + sx) * 4 + c]); };
                    dst[(size_t(y) * nw + x) * 4 + c] = uint8_t((px(2 * x, 2 * y) + px(2 * x + 1, 2 * y) + px(2 * x, 2 * y + 1) + px(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        mips_.push_back(std::move(dst));
        src = &mips_.back();
        w = nw; h = nh;
    }
    return true;
}

} // namespace cs
