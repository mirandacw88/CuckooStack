// Font files embedded at build time from assets/fonts (cmake/EmbedFile.cmake). SIL OFL 1.1, see assets/fonts.
#pragma once

#include <cstddef>

namespace cs::fontdata {
extern const unsigned char display[];    // CuckooDisplay-Black.ttf (Orbitron 900, renamed per OFL)
extern const size_t display_size;
extern const unsigned char body[];       // ChakraPetch-Medium.ttf
extern const size_t body_size;
extern const unsigned char body_bold[];  // ChakraPetch-Bold.ttf
extern const size_t body_bold_size;
extern const unsigned char japanese[];   // NotoSansJP-Bold-signs.ttf
extern const size_t japanese_size;
} // namespace cs::fontdata
