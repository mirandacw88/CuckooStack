// A maze gauntlet made at random (the iPhone and iPad editor's Gauntlet generator), as the hand-made ones are built
// (scripts/gauntlets: the tomb's floors and shafts): a tower of 3, 5 or 7 floors, stacked one over another and each 6 to 11
// high (one's `h`; the exit up to 58 rows: a course wall is 60 rows at the most), zig-zag: right along the first, up a shaft through a one-way gate stacked over its wall, left
// along the next, ... and the last floor runs right to the exit at the grid's right edge. Inside each floor's corridor the
// floor rises and falls: the corridor is cut into stretches at different heights along a floor path (makePath: linear, a
// ramp or a peak or a climb, or a random walk, a move of 2 to 4 rows a stretch): up, a stone wall she stacks over; down, a
// drop. The last stretch of a floor she climbs out of is high (the gate's climb is 8 eggs at the most), the first of the next
// at its base (the shaft's top); the top floor's last stretch may drop before the exit. Along each stretch, what the tomb
// has, at random: saws, saws back to back, the roof dipping (no egg or one under it), a dip over a saw, steps of stone or
// cyber cubes (a saw on some); wall lasers (in the face of a step she drops off, firing on along the floor: an egg cut out;
// or in a dip's face, firing back just over her head) and lava under the corridor (an egg a cell). A stretch 8 high or
// more that she drops into (4 rows or more) has ledges: stone in mid-air under the drop, one or two, stepping down to the
// floor, with a saw or lava in the top and at times a laser in the face (she reaches one only by dropping onto it, and runs
// off its end); springs ('^') fly her up a step in the floor, or onto a ledge hung in a tall stretch. The density (0 to 10)
// packs the hazards: shorter gaps, back to back, in bigger clusters. Each one made is judged as the tests judge a gauntlet
// (GauntletReport.h: one way through, no traps, out with the meter refilling 1.25x slower, 15 near misses; from density 6
// less spare and more near misses); one that fails is thrown away and another made, till one passes.
#pragma once

#include "core/Chunk.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs {
class LevelCatalog;
}

namespace cs::ed {

// what a maze may have (the editors' Gauntlet generator's switches): saws (in the floor, on steps, under dips), wall
// lasers, lava
// and the density, 0 to 10 (the course generator's too): at 0 the gaps between hazards run `cost x 1.4 + 1 to 6` cells, down to
// `cost x 0.7 + 0 to 2` at 10, with hazards back to back more often, and bigger (saw rows, lava runs); from 6 the
// checklist bends (GauntletReport.h) and the maze records it ("hard N")
// `game`: a maze ready by the solver is also played in the real game (put into the level as the editors put one, 40% of the
// way along) and kept only if the game shows its near misses as the checklist wants (as many as asked for, within 2 of the
// solver's count), as the iPad's checklist judges it; off, the solver's judgement alone (faster: for the tests that make many)
// `chunks`: the maze chunks the generator may stamp into a stretch of a floor (Chunk.h; layMaze takes all it is handed, and
// generateMaze keeps those whose ranges hold the level): a chunk at the ground of a stretch with the head room and length for
// it and no ledge or spring pad in it, hazards laid either side of it with the meter's gap; a chunk drawn running right stands
// as drawn on a floor that runs right and mirrored in the grid on one that runs left (she runs through it as drawn); one with
// Mirror on (and no spring) is at times flipped as she runs, and goes on any floor, one with Mirror off only on a floor that
// runs right, as drawn. A chunk's uses are counted per maze. With no chunks, the same seed gives the same maze as ever.
struct MazeOptions { bool saws = true, lasers = true, lava = true; int density = 0; bool springs = true, game = true; std::vector<Chunk> chunks; };

// where a chunk went in a maze: the grid's columns c0..c1 and rows r0..r1 (top first), the floor (0 at the street), and whether
// the grid shows it mirrored
struct MazeChunkUse {
    std::string id;
    int floor = 0, c0 = 0, c1 = 0, r0 = 0, r1 = 0;
    bool mirrored = false;
};

struct MazeGenResult {
    bool ok = false;
    std::vector<std::string> rows;        // top first
    std::vector<std::string> description; // its file's ';' lines
    uint32_t seed = 0;                    // the one that made it
    int tries = 0;
    std::vector<MazeChunkUse> chunks;     // the chunks it holds
    bool widened = false;                 // not ready at the width asked, in its tries: made wider (the fallback)
    // a roll never comes back empty because of a chunk: if no maze passed with the chunks (a dozen tries unsolvable), fewer of
    // them are tried (half the weight, one use each: .chunksReduced), then none (.chunksDropped); .chunkNote says so for the
    // notice and the history ("Left out: three-saws \xE2\x80\x94 no maze passed with it at level 20"); all empty if nothing was given up
    std::vector<std::string> chunksDropped, chunksReduced;
    std::string chunkNote;
    std::string why;                      // (not ok) what the last one failed on
};

// a maze of `floors` stacked floors (3, 5 or 7; odd, 3 to 7) and `width` cells wide (60 to 400), for `level` (its meter: cat's
// level, or the built-in one), from `seed` on (seed, seed + 1, ...: at most `tries` of them). A short tower has too few near
// misses to find 15: if none of them is ready at that width, `tries` more are made wider (a quarter wider, and 220, 150 or 110
// at the least for 3, 5 or 7 floors): .widened
MazeGenResult generateMaze(int level, int width, int floors, uint32_t seed, const LevelCatalog* cat = nullptr, int tries = 60, const MazeOptions& options = {});

// one maze from one seed, unjudged (the generator's tests; and the layout it judges)
std::vector<std::string> layMaze(int width, int floors, uint32_t seed, double cellsPerEgg, std::vector<std::string>* description = nullptr, const MazeOptions& options = {}, std::vector<MazeChunkUse>* used = nullptr);

} // namespace cs::ed
