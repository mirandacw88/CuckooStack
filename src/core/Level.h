// Procedural course: wall segments, overhead barriers, saw blades and disco-ball pickups.
// generateUntil() lays it ahead of the run from a seeded mulberry32 stream; every climb and every saw is affordable
// (the egg budget).
#pragma once

#include "Difficulty.h"
#include "Gauntlet.h"
#include "Math.h"

#include <vector>

namespace cs {

struct LevelData;

constexpr double U = 0.6;         // one egg / one crate
constexpr float Uf = 0.6f;
constexpr float DEPTH = 1.3f;     // crate wall depth
constexpr double SECTOR = 150.0;  // sector length, metres
constexpr double HEN_H = 1.05;    // hen hitbox height above the top egg

enum class WallKind : uint8_t { Crate, Hay, Mix };

struct Segment {
    double x0 = 0, x1 = 0;
    int h = 0;
    WallKind kind = WallKind::Crate;
    int ceil = 0;              // barrier underside in eggs; 0 = no barrier
    bool gauntlet = false;     // a maze gauntlet's span (Level::gauntlets): its walls are its grid's, not this segment's
    double minHead = 9;        // tightest head clearance seen under this barrier
    bool hasMinHead = false, closeCallChecked = false;
    // ranges into the cached instance arrays
    int capStart = 0, capCount = 0, ceilStart = 0, ceilCount = 0;
};

struct Corn { double x, y; float phase; bool missed = false; };

// A gauntlet laid on the course: its grid's bottom-left corner at (x0, the street)
struct PlacedGauntlet {
    const Gauntlet* g = nullptr;
    double x0 = 0;
    double x1() const { return x0 + g->cols() * 0.6; }
    int col(double x) const { return static_cast<int>(std::floor((x - x0) / 0.6)); }
};

// A circular saw blade set into the course (levels 11 on: Campaign::applyTo). Crossing it slices the bottom egg off the
// stack, once; with no egg under her it kills the hen (Game, GameSaw.cpp). A floor saw sits in the street or a stone
// wall's top; a cube saw in the top of a cyber-cube wall, a beat past its front edge (so a climb onto it that leaves no
// egg to spare ends on the blade). Its top half spins above the surface.
struct Saw {
    enum class State : uint8_t { Armed, Cut, Passed, Smashed };
    double x = 0;             // the hub along the course
    int y = 0;                // the surface it's set into, in eggs (0: the street)
    bool cube = false;        // in a cyber-cube wall (else the street or stone)
    State state = State::Armed;
    bool warned = false;      // its approach whine has played
    float sink = 0;           // after its cut, it sinks into its slot (0 up .. 1 gone)
};

class Level {
public:
    struct Block { glm::vec3 pos; bool flipped, hay; float tint; };
    struct Tile { glm::vec3 pos; int gx, gz; };
    struct Girder { glm::vec3 pos; };
    struct Curtain { glm::vec3 pos; float width; };

    void reset(uint32_t seed);
    bool generateUntil(double xmax);
    // A level as authored (LevelData.h, the campaign's): its walls, barriers and gauntlets (with their own saws), its
    // saws and disco balls, its finish line and difficulty band. Past its last wall generateUntil lays flat runway only
    void load(const LevelData& d);
    // Levels (Campaign.h): difficulty is read at offset + x * scale along the curve, and past `finish` (0 = endless)
    // only flat, wall-free runway is laid. The defaults give the endless daily course.
    void setDifficulty(double offset, double scale) { diffOffset_ = offset; diffScale_ = scale; }
    void setFinish(double x) { finish_ = x; }
    // The egg budget (on by default): every step up must be affordable from the egg meter of a player who stacks just
    // enough. The generator tracks that meter
    // (full at the start, refilling per metre at the curve's speed and refill) and, where a climb would need more eggs
    // than it holds, lengthens the run-up before it (or, past kMaxRunup, lowers the wall). The random stream is
    // consumed exactly as without it (off: the course as it was, for comparisons). Each saw costs an egg too.
    void setEggBudget(bool on) { eggBudget_ = on; }
    // Saws: `count` blades spread along the course up to the finish line (0, the default: none), alone or in rows of
    // up to `row` back to back (kSawRowGap apart: an egg sawn off at each). Each saw or row sits on its own flat
    // stretch (lengthened to fit): clear of barriers, past the landing from a drop and a reaction after it, with a
    // reaction's run after it before the next wall, one egg's refill from the last saw, and an egg per blade in the
    // budget. A course's first saw is alone, on the street when it can be. Placed from their own random stream, so the
    // walls are laid as without them (bar the stretches lengthened for them).
    void setSaws(int count, int row = 1) { sawCount_ = count; sawRow_ = std::max(1, row); }
    // A maze gauntlet (Gauntlet.h) about `at` metres along (nullptr, the default: none). The course is brought to its
    // entry's height by fair steps, then a forecourt (a reaction's run), the gauntlet, and a porch at its exit's
    // height; its saws join the course's. No barrier, saw or corn on the forecourt or the porch.
    void setGauntlet(const Gauntlet* g, double at) { gauntletDef_ = g; gauntletAt_ = at; }
    const PlacedGauntlet* gauntletAt(double x) const {
        for (const PlacedGauntlet& p : gauntlets) if (x >= p.x0 && x < p.x1()) return &p;
        return nullptr;
    }
    // the floor under (x, y), metres: in a gauntlet, its highest solid cell at or below y; else the course's walls
    double groundBelow(double x, double y) const;
    static constexpr double kSawR = 0.5;            // blade radius, metres (its hub at the surface)
    static constexpr double kSawReach = kSawR + 0.3; // the stack meets it this far before its hub; done this far past
    static constexpr double kSawRowGap = 3 * 0.6;    // blades in a row: hub to hub (each egg's cut done before the next)
    static constexpr double kBudgetCap = 8.0;    // the egg meter's size (Game::MAXE)
    static constexpr int kLintel = 3;            // a gauntlet's low door: the barrier at the end of its forecourt
    static constexpr double kBudgetMargin = 0.4; // eggs to spare at every climb (a tap's reaction, DDA slowing the refill)
    static constexpr int kMaxRunup = 40;         // units a run-up may be lengthened by (24 m) before the wall is lowered instead
    double finish() const { return finish_; }
    Curve curveAt(double x) const { return curve(diffOffset_ + std::max(0.0, x) * diffScale_); }
    void dropBehind(double x) {
        while (!segs.empty() && segs.front().x1 < x - 14) segs.erase(segs.begin());
        while (!saws.empty() && saws.front().x < x - 14) saws.erase(saws.begin());
        while (!gauntlets.empty() && gauntlets.front().x1() < x - 14) gauntlets.erase(gauntlets.begin());
    }
    void rebuildBlocks();
    int heightAt(double x) const;

    std::vector<Segment> segs;
    std::vector<Corn> corns;
    std::vector<Saw> saws;      // in course order
    std::vector<PlacedGauntlet> gauntlets;
    // a gauntlet's cells to draw (rebuildBlocks): its solid cells (`open`: bit 0 air on its left, bit 1 on its right),
    // and its back wall behind every air cell
    struct GCell { glm::vec3 pos; Gauntlet::Cell kind; uint8_t open; };
    std::vector<GCell> gcells;
    std::vector<glm::vec3> gback;
    // ...and its inscriptions (a tomb's hieroglyphs, Hieroglyphs.h; GameGauntlet.cpp draws them): on its back wall, just
    // in front of it, a sign per cell in columns (kind 0), a star along the top of a tall room (1), the signs of a
    // cartouche (2) and its frame; rules between the columns, under the stars and along the blank dado (size: w, h)
    struct GSign { glm::vec3 pos; uint32_t cp; uint8_t kind; float seed; };
    struct GRule { glm::vec3 pos; glm::vec2 size; };
    struct GCartouche { glm::vec3 pos; glm::vec2 size; };
    std::vector<GSign> gsigns;
    std::vector<GRule> grules;
    std::vector<GCartouche> gcartouches;
    std::vector<Block> blocks;
    std::vector<Tile> tiles;
    std::vector<Girder> girders;
    std::vector<Curtain> curtains;

    static constexpr int TILE_X = 2, TILE_Z = 4;
    static constexpr float TW = Uf / TILE_X, TD = DEPTH / TILE_Z;

private:
    void pushSeg(int len, int h, WallKind kind);
    double eggsPerUnit(double x) const; // the egg budget: eggs refilled per unit (one egg's width) of run at x
    void creditRun(double units, double x) { budget_ = std::min(kBudgetCap, budget_ + units * eggsPerUnit(x)); }
    void spawnCorn(double x, double y);
    void placeSaw(int prevH, int prevCeil, double budgetAtStart);
    void placeGauntlet();
    void inscribe(const PlacedGauntlet& p); // (rebuildBlocks: its inscriptions)
    void affordClimb(int& h); // the egg budget before a step up to h: a longer run-up, or a lower step
    double sawTarget(int k);
    Mulberry32 rng_, sawRng_;
    double genX_ = 0, diffOffset_ = 0, diffScale_ = 1, finish_ = 0;
    int lastH_ = 0, segN_ = 0, genSector_ = 0;
    bool eggBudget_ = true;
    bool authored_ = false; // (load: the course is the level's data, not the generator's)
    double budget_ = kBudgetCap; // eggs in a careful player's meter at genX_
    int sawCount_ = 0, sawsPlaced_ = 0, sawRow_ = 1, nextSawRow_ = 1;
    const Gauntlet* gauntletDef_ = nullptr;
    double gauntletAt_ = 0;
    bool gauntletPlaced_ = false;
    double gauntletEnd_ = 0; // where its porch ends, once placed
    double nextSawX_ = -1, lastSawX_ = -1e9; // where the next saw (or row) is due (-1: not drawn yet); the last blade placed
};

} // namespace cs
