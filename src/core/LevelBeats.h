// The rhythm-beat level generator (after Smith and Whitehead's Launchpad, Compton and Mateas's rhythm patterns, Tanagra
// and the papers on them: docs/LEVEL_DESIGN.md, "Generated levels"). A level is laid in two tiers:
//
// 1. Rhythm: groups of beats, each a short timeline of what she has to do (stack, shed, duck, wait, launch, bounce) at a
//    tempo (regular, swing, random), a density and a length, with a rest between groups (a flat breather with disco
//    balls: the cadence).
// 2. Geometry: each beat becomes a piece of course from a grammar (stack: a climb, a spring; shed: a drop; duck: a
//    barrier; wait: a saw, lava; bounce: a bounce box), as tight as the beats allow: a piece whose time runs into the
//    next beat (a fall's landing, a spring's flight) is never chosen. Pieces come in difficulty tiers chosen apart
//    from their kind; groups are basic, complex (a tweak growing), compound (two alternating) or composite (two in one).
//
// A level's arc: a star verb (from what the level allows), taught alone and gently, tested with the basics, twisted
// in a composite, and a finale of the densest group. Many candidates are laid from the seed (rhythms × geometries),
// every one judged by the game's checker (LevelJudge: no notes, the egg budget, every bounce zone solved); the critics
// score the ones that pass (the interest curve: a hook, a varied middle, a build-up, a climax; the mix of pieces
// against the zone's; variety) and the best is the level. The same seed gives the same level.
#pragma once

#include "LevelBake.h"
#include "LevelData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::beats {

// what a beat asks of her
enum class Verb : uint8_t { Stack, Shed, Duck, Wait, Launch, Bounce };
const char* verbName(Verb v);
enum class Tempo : uint8_t { Regular, Swing, Random };

struct Beat {
    double t = 0;         // seconds into its group
    Verb verb = Verb::Stack;
};
struct Rhythm {
    Tempo tempo = Tempo::Regular;
    int density = 1;      // 0 low, 1 medium, 2 high
    double length = 10;   // seconds: 5, 10, 15 or 20
    std::vector<Beat> beats;
};

// a group's role in the level's arc
enum class Role : uint8_t { Teach, Test, Twist, Finale };
const char* roleName(Role r);

// a group as laid: where, its role and rhythm, its pieces (one letter each: the edit distance's alphabet) and how
// hard it is (the critics' intensity: eggs to stack and hazards met, over the seconds she has for them)
struct Group {
    double x0 = 0, x1 = 0;
    Role role = Role::Test;
    Rhythm rhythm;
    std::string pieces;   // A ascent, F spring onto a floating floor, G spring to a higher one, D descent (the storeys), c climb, d drop, b barrier, s saw, S saw row, l lava, j spring, x bounce box, t staircase,
                          // v valley, m mesa, k saw on a climb's top, o spring over lava, z a laser drop, u a climb under a barrier,
                          // Q a chunk of the designer's (Chunk.h)
    double intensity = 0;
};

struct Plan {
    LevelData data;
    int density = 0;      // the density it was laid at (lower than asked: nothing passed at that)
    Verb star = Verb::Stack;
    std::vector<Group> groups;
    std::vector<ChunkUse> chunks; // the designer's chunks placed (Chunk.h), in order, and where
    // chunks in range left out because no course passed with them (they spoiled every candidate, or the bot cleared none): the
    // course is laid without them, at the density asked (only if it can't be, a notch less dense: with the chunks again)
    std::vector<std::string> chunksDropped;
    std::string floors;   // the floor path: u up a storey, d down (a level may finish on any floor)
    bool linearFloors = false; // a steady ramp, a peak or a long climb (else a random walk)
    double score = 0;     // the critics' (higher: better)
    int tried = 0, passed = 0;
};

struct Options {
    GenOptions hazards;   // only these hazards (none listed: all)
    int rhythms = 20, geometries = 10; // candidates: rhythms × geometries
    int enough = 40;      // ...stopping once this many pass the checker (the critics choose among them)
    // how dense (0 to 10): 0 the generator as it was, 10 the densest it lays (super hard). Launchpad's knobs, turned up
    // together: beats closer (its rhythm density), each piece only the run it needs, rests fewer and shorter (its
    // cadence), groups longer (Endless Web's segments, longer and faster), the hard tier more often (its tiers). Nothing
    // the checker and the bot pass at that density: one notch down, and so on
    int density = 0;
};

// the seconds between beats at a rhythm's density (0 low, 1, 2 high) and the slider's (0 to 10): Launchpad's rhythm
// density, its spacing down to a quarter at 10
double beatSpacing(int rhythmDensity, int density);
// moments (a climb, a drop, a hazard) a second the density critic aims a level at
double targetMoments(int density);

// level n's course from `seed` (its length and difficulty as the campaign has them), its plan with it
Plan compose(int n, uint32_t seed, const Options& options = {});

// the critics, each on its own (the generator's tests, tools/level_range)
double curveFit(const std::vector<Group>& groups);                    // 0 .. 1: how well the intensities follow the arc
double mixChiSquare(const std::vector<Group>& groups, int level);    // lower: closer to the zone's mix
int editDistance(const std::string& a, const std::string& b);        // Levenshtein over piece letters

// expressive range (Smith and Whitehead, PCG 2010): emergent traits of a laid course, not the generator's inputs
struct Traits {
    double linearity = 0; // R² of a line through the floor's midpoints (1: a straight slope)
    double leniency = 0;  // hazards and climbs weighted by the time she has for them, a metre (lower: harsher)
    double pacing = 0;    // mean seconds between moments (a climb, a drop, a hazard)
    int tallest = 0;      // the highest floor (a floating block's top too)
    double above = 0;     // the share of the course she runs over the street (on a storey, a floating floor, a block)
    double meanFloor = 0; // the floor she runs at, in rows, on average along the course
};
Traits traits(const LevelData& d);

} // namespace cs::beats
