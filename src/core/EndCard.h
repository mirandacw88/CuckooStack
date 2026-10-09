// The Share Replay end card ("412 m · Can you beat it?"), rasterised on the CPU with the game's own SDF fonts and
// icon atlas, so it looks like the HUD. Android blits it into the replay video after the clip (iOS builds its card
// with Core Animation instead). RGBA8, straight alpha, opaque.
#pragma once

#include "Font.h"
#include "HudImage.h"
#include "Services.h"

#include <cstdint>
#include <vector>

namespace cs {

std::vector<uint8_t> renderEndCard(const FontAtlas& fonts, const HudImage& icons, int width, int height, const ReplayMeta& meta);

} // namespace cs
