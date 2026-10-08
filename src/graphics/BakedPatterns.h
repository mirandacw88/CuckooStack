// Procedural surface patterns that are constant over time, baked once at startup instead of being evaluated per
// pixel every frame (GPU / battery saving).
#pragma once

#include <cstdint>
#include <vector>

namespace cs {

// Wet-asphalt puddle mask for one 6 m street tile (wraps seamlessly), n x n R8. Same 14 ellipses lit.frag used to
// evaluate per pixel; 1 = puddle centre, 0 = dry.
std::vector<uint8_t> bakePuddleMask(int n);

} // namespace cs
