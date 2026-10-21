// Full-colour sprite atlases, decoded once from embedded PNGs: the HUD's (coins, icons; HudIcons.h, from
// assets/hud/hud_icons.png) and Neon Egypt's backdrop (EgyptBackdrop.h, from assets/backdrop/egypt_backdrop.png).
#pragma once

#include <cstdint>
#include <vector>

namespace cs {

class HudImage {
public:
    // Decodes the embedded icon atlas; pixels are premultiplied RGBA8.
    bool buildIcons();
    // ...or the backdrop's, with five smaller levels (its sprites sit in 16 px gutters, so none bleeds into another)
    bool buildEgyptBackdrop();
    bool built() const { return !pixels_.empty(); }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }
    // smaller mip levels (level 1, 2, ...), box-filtered in premultiplied space; icons shrink without shimmering
    const std::vector<std::vector<uint8_t>>& mips() const { return mips_; }

private:
    bool decode(const unsigned char* png, size_t size, int levels, const char* what);
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> pixels_;
    std::vector<std::vector<uint8_t>> mips_;
};

} // namespace cs
