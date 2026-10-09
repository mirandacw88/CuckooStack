// Full-colour HUD sprites (coins, icons; HudIcons.h), decoded once from the embedded atlas assets/hud/hud_icons.png.
#pragma once

#include <cstdint>
#include <vector>

namespace cs {

class HudImage {
public:
    // Decodes the embedded icon atlas; pixels are premultiplied RGBA8.
    bool buildIcons();
    bool built() const { return !pixels_.empty(); }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }
    // smaller mip levels (level 1, 2, ...), box-filtered in premultiplied space; icons shrink without shimmering
    const std::vector<std::vector<uint8_t>>& mips() const { return mips_; }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> pixels_;
    std::vector<std::vector<uint8_t>> mips_;
};

} // namespace cs
