// Procedural course: wall segments, overhead barriers and disco-ball pickups.
// generateUntil() consumes the daily mulberry32 stream in exactly the same order as the web build.
#pragma once

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
    void spawnCorn(double x, double y);
    Mulberry32 rng_;
    double genX_ = 0;
    int lastH_ = 0, segN_ = 0, genSector_ = 0;
};

} // namespace cs
