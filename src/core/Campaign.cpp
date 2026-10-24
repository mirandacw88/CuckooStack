#include "Campaign.h"
#include "LevelBake.h"
#include "LevelData.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>

namespace cs {

Campaign::Campaign(IStorage* storage, const LevelCatalog* cat) : rec_(storage, "cluckstack-campaign"), cat_(cat ? cat : &LevelCatalog::dev()) {
    level_ = std::max(1, rec_.i("level", 1));
    cleared_ = std::max(0, rec_.i("cleared"));
    attempts_ = std::max(0, rec_.i("attempts"));
    best_ = std::max(0, rec_.i("best_dm")) / 10.0;    // stored in decimetres
}

uint32_t Campaign::seedOf(int level) { return seedFor("cluckstack:level:" + std::to_string(level)); }

// Difficulty bands: level 1 covers the easy start of the curve (0..105), each level starts 35 further on, and level
// 10 reaches the plateau (315..420); later levels stay there. An authored level (LevelData.h) brings its own band,
// course and finish line: the generator lays only the levels past them.
void Campaign::applyTo(Level& course) const {
    if (const LevelData* d = cat_->level(level_)) {
        course.load(*d, cat_);
        return;
    }
    applyGenerated(course);
}

void Campaign::applyClassic(Level& course) const {
    course.reset(seedOf(level_));
    course.setDifficulty(35.0 * (level_ - 1), 0.35);
    course.setFinish(length());
    course.setSaws(sawsOf(level_), sawRowOf(level_));
    course.setLasers(lasersOf(level_));
    course.setLava(lavaOf(level_));
    course.setGauntlet(nullptr, 0);
}

// a level with no file: the rhythm-beat generator's (LevelBeats.h), from the level's own seed (the same course every
// time), laid once a run of the app (its candidates take a moment)
void Campaign::applyGenerated(Level& course) const {
    static std::mutex m;
    static std::map<std::pair<int, std::string>, LevelData> laid;
    // the designer's chunks (Chunk.h) whose ranges cover the level, a level with no file of its own only (a saved level never
    // changes); none: exactly the level the generator always laid. The catalogs differ in their chunks, so the cache has them
    GenOptions options;
    if (!cat_->level(level_))
        for (const Chunk& c : cat_->chunks())
            if (!c.maze && c.inRange(level_)) options.chunks.push_back(c);
    const std::pair<int, std::string> key{level_, chunksText(options.chunks)};
    LevelData d;
    {
        std::lock_guard<std::mutex> lock(m);
        auto it = laid.find(key);
        if (it == laid.end()) it = laid.emplace(key, bakeLevel(level_, nullptr, std::nullopt, options)).first;
        d = it->second;
    }
    course.load(d, cat_);
}

double Campaign::lengthOf(int level, const LevelCatalog* cat) {
    const LevelData* d = (cat ? cat : &LevelCatalog::dev())->level(level);
    return d ? d->length : 300.0 + 10.0 * (level - 1);
}

// Each level's own (LevelData.h: a gauntlet's span in its course); Neon Egypt's every other level from 11, each its
// own (docs/LEVEL_DESIGN.md)
const Gauntlet* Campaign::gauntletOf(int level, const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    if (const LevelData* d = cat->level(level))
        for (const LevelData::Wall& w : d->course)
            if (!w.gauntlet.empty()) return cat->gauntlet(w.gauntlet);
    return nullptr;
}

double Campaign::gauntletAt(int level, const LevelCatalog* cat) {
    if (const LevelData* d = (cat ? cat : &LevelCatalog::dev())->level(level)) {
        int cells = 0;
        for (const LevelData::Wall& w : d->course) {
            if (!w.gauntlet.empty()) return cells * U;
            cells += w.cells;
        }
    }
    return 0.4 * lengthOf(level, cat);
}

int Campaign::gauntletLevel(const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    for (int level : cat->levelNumbers())
        if (gauntletOf(level, cat)) return level;
    return 0;
}

int Campaign::raidsOf(int level, const LevelCatalog* cat) {
    const LevelData* d = (cat ? cat : &LevelCatalog::dev())->level(level);
    return d ? d->raids : -1;
}

void Campaign::attempt() { ++attempts_; save(); }

bool Campaign::recordRun(double meters) {
    if (meters <= best_) return false;
    best_ = meters;
    save();
    return true;
}

void Campaign::clear() {
    ++level_; ++cleared_;
    attempts_ = 0; best_ = 0;
    save();
}

void Campaign::debugSetLevel(int level) {
    level_ = std::max(1, level);
    attempts_ = 0; best_ = 0;
    save();
}

void Campaign::debugReset() {
    level_ = 1; cleared_ = 0; attempts_ = 0; best_ = 0;
    save();
}

void Campaign::save() {
    rec_.set("level", level_);
    rec_.set("cleared", cleared_);
    rec_.set("attempts", attempts_);
    rec_.set("best_dm", int64_t(best_ * 10.0));
    rec_.save();
}

} // namespace cs
