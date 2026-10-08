#include "Level.h"
#include "Difficulty.h"

namespace cs {

void Level::reset(uint32_t seed) {
    rng_ = Mulberry32(seed);
    segs.clear(); corns.clear();
    genX_ = 0; lastH_ = 0; segN_ = 0; genSector_ = 0;
}

void Level::pushSeg(int len, int h, WallKind kind) {
    Segment s;
    s.x0 = genX_; s.x1 = genX_ + len * U; s.h = h; s.kind = kind;
    segs.push_back(s);
    genX_ += len * U; lastH_ = h; segN_++;
}

// The web build uses Math.random() for the bob phase; a position hash keeps it cosmetic and deterministic.
void Level::spawnCorn(double x, double y) { corns.push_back({x, y, hash(x * 1.7) * 6.f}); }

int Level::heightAt(double x) const {
    for (size_t i = segs.size(); i-- > 0;) {
        const Segment& s = segs[i];
        if (x >= s.x0 && x < s.x1) return s.h;
        if (x >= s.x1) break;
    }
    return 0;
}

bool Level::generateUntil(double xmax) {
    bool changed = false;
    while (genX_ < xmax) {
        changed = true;
        if (segN_ == 0) { pushSeg(16, 0, WallKind::Crate); genSector_ = 0; continue; }
        // sector breather: a flat stretch with a rising line of corn
        const int sec = static_cast<int>(std::floor(genX_ / SECTOR));
        if (sec > genSector_) {
            genSector_ = sec;
            const double x0 = genX_;
            pushSeg(12, 0, WallKind::Crate);
            for (int k = 0; k < 3; ++k) spawnCorn(x0 + (2.5 + k * 3) * U, (2 + k) * U + 0.25);
            continue;
        }
        const Curve c = curve(genX_);
        const int lastCeil = segs.empty() ? 0 : segs.back().ceil;
        const bool afterDrop = segs.size() >= 2 && segs[segs.size() - 2].h > lastH_;
        int h;
        if (lastH_ > 0 && rng_.next() < c.drop) h = std::max(0, lastH_ - 1 - static_cast<int>(std::floor(rng_.next() * lastH_)));
        else h = std::min(c.cap, lastH_ + 1 + static_cast<int>(std::floor(rng_.next() * c.maxUp)));
        // fairness: after a barrier, the next wall must be climbable while still under that barrier
        if (lastCeil) h = std::min(h, lastCeil - static_cast<int>(std::ceil(HEN_H / U)));
        int len = c.minLen + static_cast<int>(std::floor(rng_.next() * (c.maxLen - c.minLen + 1)));
        // fairness: after a drop, leave enough flat run to finish falling and react to the next wall
        if (h < lastH_) {
            const double fall = std::sqrt(2.0 * (lastH_ - h) * U / 36.0);
            len = std::max(len, static_cast<int>(std::ceil(c.speed * (fall + 0.45) / U)));
        }
        const double r = rng_.next();
        const int prevH = lastH_;
        pushSeg(len, h, r < 0.4 ? WallKind::Crate : r < 0.7 ? WallKind::Hay : WallKind::Mix);
        Segment& seg = segs.back();
        // barriers only over rising walls that don't follow a drop (the hen would still be falling)
        if (genX_ > 30 && h > prevH && !afterDrop && rng_.next() < c.ceil) {
            seg.ceil = h + c.gap + (rng_.next() < 0.35 ? 1 : 0);
            // fairness: you can't pre-stack past a barrier, so it must last long enough to react and lay under it
            const int minUnits = static_cast<int>(std::ceil(c.speed * 0.5 / U));
            const int cur = jsRound((seg.x1 - seg.x0) / U);
            if (cur < minUnits) { seg.x1 = seg.x0 + minUnits * U; genX_ = seg.x1; }
        }
        if (rng_.next() < c.corn) {
            const double y = seg.ceil ? h * U + 0.7 : (h + 2 + static_cast<int>(std::floor(rng_.next() * 3))) * U + 0.25;
            spawnCorn(genX_ - len * U * 0.5, y);
        }
    }
    return changed;
}

void Level::rebuildBlocks() {
    blocks.clear(); tiles.clear(); girders.clear(); curtains.clear();
    for (const Segment& s : segs) {
        const int cols = jsRound((s.x1 - s.x0) / U);
        for (int i = 0; i < cols; ++i)
            for (int j = 0; j < s.h; ++j) {
                const double x = s.x0 + (i + 0.5) * U, y = (j + 0.5) * U;
                const bool hay = s.kind == WallKind::Hay || (s.kind == WallKind::Mix && j == s.h - 1);
                blocks.push_back({glm::vec3(float(x), float(y), 0.f), hash(x * 3.1 + j * 7.7) >= 0.5f, hay,
                                  0.82f + 0.18f * hash(x * 9.3 + j * 1.9)});
            }
    }
    // disco dance floor on every wall top: 2 x 4 light tiles per column
    for (Segment& s : segs) {
        s.capStart = static_cast<int>(tiles.size()); s.capCount = 0;
        if (s.h <= 0) continue;
        const int cols = jsRound((s.x1 - s.x0) / U);
        for (int i = 0; i < cols; ++i)
            for (int a = 0; a < TILE_X; ++a)
                for (int b = 0; b < TILE_Z; ++b) {
                    const float tx = float(s.x0 + i * U) + (a + 0.5f) * TW, tz = -DEPTH / 2 + (b + 0.5f) * TD;
                    tiles.push_back({{tx, float(s.h * U) + 0.025f, tz}, jsRound((s.x0 + i * U) / U) * TILE_X + a, b});
                    s.capCount++;
                }
    }
    // overhead barriers: a hazard-striped girder, a bright underside edge, and a red laser curtain above it
    for (Segment& s : segs) {
        s.ceilStart = static_cast<int>(girders.size()); s.ceilCount = 0;
        if (!s.ceil) continue;
        const int cols = jsRound((s.x1 - s.x0) / U);
        const float cy = float(s.ceil * U);
        for (int i = 0; i < cols; ++i) {
            girders.push_back({{float(s.x0 + (i + 0.5) * U), cy, 0.f}});
            s.ceilCount++;
        }
        curtains.push_back({{float((s.x0 + s.x1) / 2), cy + 0.7f, 0.f}, float(s.x1 - s.x0)});
    }
}

} // namespace cs
