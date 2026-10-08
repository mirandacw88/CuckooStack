// Full-colour HUD sprites (e.g. the lives heart), decoded once from an embedded PNG sprite sheet.
#pragma once

#include <cstdint>
#include <vector>

namespace cs {

class HudImage {
public:
    // Decodes the embedded lives icon sheet (assets/hud/lives_icons.png); pixels are premultiplied RGBA8.
    bool buildLivesIcons();
    bool built() const { return !pixels_.empty(); }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> pixels_;
};

} // namespace cs
