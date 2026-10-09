// Outfits with their own hen model (scripts/models/add_outfit_model.py writes the list in OutfitModels.inc).
#pragma once

namespace cs {

// how many outfit models are embedded (each needs one library mesh: MeshId::HenOutfit0 + i)
constexpr int kOutfitModelCount = 0
#define CS_OUTFIT_MODEL(id) +1
#include "OutfitModels.inc"
#undef CS_OUTFIT_MODEL
    ;

} // namespace cs
