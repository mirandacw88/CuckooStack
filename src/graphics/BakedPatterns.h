// Procedural surface patterns that are constant over time, baked once at startup instead of being evaluated per
// pixel every frame (GPU / battery saving).
#pragma once

#include <cstdint>
#include <vector>

namespace cs {

// Wet-asphalt puddle mask for one 6 m street tile (wraps seamlessly), n x n R8. Same 14 ellipses lit.frag used to
// evaluate per pixel; 1 = puddle centre, 0 = dry.
std::vector<uint8_t> bakePuddleMask(int n);

// The cyber cube's static face design (lit.frag P_HAZARD), 4 circuit layouts in a 2 x 2 atlas of n x n faces (so
// 2n x 2n, RGBA8 each), supersampled 2 x 2 per texel. Only the animation (spark, shimmer, beat) stays per pixel.
//  surface: rgb = sqrt(linear base colour), a = surface kind (0 frame, 1/3 bolt, 2/3 glass panel, 1 hazard tape)
//  glow:    r = neon tube line, g = circuit traces / nodes, b = core ring, a = core centre dot
struct CyberCubeBake { std::vector<uint8_t> surface, glow; };
CyberCubeBake bakeCyberCube(int n);

} // namespace cs
