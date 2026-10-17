// Procedural course: wall segments, overhead barriers and disco-ball pickups.
// generateUntil() lays it ahead of the run from a seeded mulberry32 stream; every climb is affordable (the egg budget).
#pragma once

#include "Difficulty.h"
#include "Math.h"

#include <vector>

namespace cs {

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
    double minHead = 9;        // tightest head clearance seen under this barrier
    bool hasMinHead = false, closeCallChecked = false;
    // ranges into the cached instance arrays
    int capStart = 0, capCount = 0, ceilStart = 0, ceilCount = 0;
};

struct Corn { double x, y; float phase; bool missed = false; };

class Level {
public:
    struct Block { glm::vec3 pos; bool flipped, hay; float tint; };
    struct Tile { glm::vec3 pos; int gx, gz; };
    struct Girder { glm::vec3 pos; };
    struct Curtain { glm::vec3 pos; float width; };

    void reset(uint32_t seed);
    bool generateUntil(double xmax);
    // Levels (Campaign.h): difficulty is read at offset + x * scale along the curve, and past `finish` (0 = endless)
    // only flat, wall-free runway is laid. The defaults give the endless daily course.
    void setDifficulty(double offset, double scale) { diffOffset_ = offset; diffScale_ = scale; }
    void setFinish(double x) { finish_ = x; }
    // The egg budget (on by default): every step up must be affordable from the egg meter of a player who stacks just
    // enough. The generator tracks that meter
    // (full at the start, refilling per metre at the curve's speed and refill) and, where a climb would need more eggs
    // than it holds, lengthens the run-up before it (or, past kMaxRunup, lowers the wall). The random stream is
    // consumed exactly as without it (off: the course as it was, for comparisons).
    void setEggBudget(bool on) { eggBudget_ = on; }
    static constexpr double kBudgetCap = 8.0;    // the egg meter's size (Game::MAXE)
    static constexpr double kBudgetMargin = 0.4; // eggs to spare at every climb (a tap's reaction, DDA slowing the refill)
    static constexpr int kMaxRunup = 40;         // units a run-up may be lengthened by (24 m) before the wall is lowered instead
    double finish() const { return finish_; }
    Curve curveAt(double x) const { return curve(diffOffset_ + std::max(0.0, x) * diffScale_); }
    void dropBehind(double x) { while (!segs.empty() && segs.front().x1 < x - 14) segs.erase(segs.begin()); }
    void rebuildBlocks();
    int heightAt(double x) const;

    std::vector<Segment> segs;
    std::vector<Corn> corns;
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
    Mulberry32 rng_;
    double genX_ = 0, diffOffset_ = 0, diffScale_ = 1, finish_ = 0;
    int lastH_ = 0, segN_ = 0, genSector_ = 0;
    bool eggBudget_ = true;
    double budget_ = kBudgetCap; // eggs in a careful player's meter at genX_
};

} // namespace cs
