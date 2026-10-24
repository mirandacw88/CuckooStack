// The generator's own course for level n, as a level file (how a level starts): level_check --bake, the level editors'
// "new level".
#pragma once

#include "Chunk.h"
#include "Gauntlet.h"
#include "LevelData.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cs {

// what the generator may lay (the editors' Level generator's switches): each hazard it places, by its id (Hazards.h's
// rows with `generated`); empty: all of them
struct GenOptions {
    std::vector<std::string> only;
    int density = 0;      // the rhythm-beat generator's density, 0 (as designed) to 10 (super hard): LevelBeats.h
    std::vector<Chunk> chunks; // the designer's set pieces (Chunk.h): the course ones whose ranges cover the level may be placed;
                               // none in range: the level is exactly as it is without them
    bool has(const char* id) const;
};

// the course the generator lays for level n (its seed, its length, its saws, lasers and lava, the gauntlet `g` at 40% of
// the way); with `seed`, another of its courses (the editors' Level generator: the same length, difficulty and
// hazards, laid anew), and only the hazards `options` allows
// (with no gauntlet: the rhythm-beat generator's, LevelBeats.h; with one: the classic generator's, a wall at a time);
// *used, if given, is the chunks it placed and where; *dropped, the ids of chunks it left out because no course passed with them
LevelData bakeLevel(int n, const Gauntlet* g, std::optional<uint32_t> seed = std::nullopt, const GenOptions& options = {},
                    std::vector<ChunkUse>* used = nullptr, std::vector<std::string>* dropped = nullptr);
// the classic generator's course (Level::generateUntil: a random wall at a time, its hazards spread evenly)
LevelData bakeClassic(int n, const Gauntlet* g, std::optional<uint32_t> seed = std::nullopt, const GenOptions& options = {});

} // namespace cs
