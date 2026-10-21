// The hieroglyphs Neon Egypt's tombs are inscribed with (GameGauntlet.cpp draws them on a gauntlet's back walls), from
// Noto Sans Egyptian Hieroglyphs (SIL OFL 1.1). The font atlas bakes exactly these, and the embedded font is subset to
// them (scripts/subset_hieroglyphs.py reads this file). By Gardiner number.
#pragma once

#include <array>
#include <cstdint>

namespace cs::hieroglyphs {

// the signs of the walls' columns: birds first (a hen's ancestors' tomb) and the egg, then the signs every tomb has
inline constexpr std::array<uint32_t, 34> kColumns = {
    0x13171, // G43 quail chick (w)
    0x1316D, // G38 goose
    0x13153, // G17 owl (m)
    0x1313F, // G1 vulture (a)
    0x13143, // G5 falcon (Horus)
    0x1315C, // G25 crested ibis
    0x13168, // G36 swallow
    0x13177, // G47 duckling
    0x13187, // H8 egg
    0x13184, // H6 feather (Maat)
    0x132F9, // S34 ankh
    0x132BD, // R11 djed pillar
    0x13300, // S40 was sceptre
    0x13080, // D10 eye of Horus
    0x13079, // D4 eye
    0x131F3, // N5 sun
    0x13216, // N35 water (n)
    0x131CB, // M17 reed (i)
    0x131D3, // M23 sedge
    0x131B8, // M9 lotus
    0x131A3, // L1 scarab
    0x13193, // I10 cobra (dj)
    0x13191, // I9 horned viper (f)
    0x130ED, // E23 lion (rw)
    0x130E0, // E13 cat
    0x130F9, // E34 hare
    0x1319D, // K1 fish
    0x13376, // V9 shen ring
    0x1339B, // V28 wick (h)
    0x133A1, // V31 basket (k)
    0x133CF, // X1 bread (t)
    0x1308B, // D21 mouth (r)
    0x130A7, // D46 hand (d)
    0x13274, // O24 pyramid
};
inline constexpr int kBirds = 9;            // kColumns' first nine: the birds and the egg (drawn more often)
inline constexpr uint32_t kStar = 0x131FC;  // N14: the stars of a tall room's ceiling
// a cartouche's name, read top down: k-w-k-w, "Cuckoo" (the basket and the quail chick)
inline constexpr std::array<uint32_t, 4> kCuckoo = {0x133A1, 0x13171, 0x133A1, 0x13171};

} // namespace cs::hieroglyphs
