#include "Campaign.h"

#include <algorithm>
#include <string>

namespace cs {

Campaign::Campaign(IStorage* storage) : rec_(storage, "cluckstack-campaign") {
    level_ = std::max(1, rec_.i("level", 1));
    cleared_ = std::max(0, rec_.i("cleared"));
    attempts_ = std::max(0, rec_.i("attempts"));
    best_ = std::max(0, rec_.i("best_dm")) / 10.0;    // stored in decimetres
}

uint32_t Campaign::seedOf(int level) { return seedFor("cluckstack:level:" + std::to_string(level)); }

// Difficulty bands: level 1 covers the easy start of the curve (0..105), each level starts 35 further on, and level
// 10 reaches the plateau (315..420); later levels stay there.
void Campaign::applyTo(Level& course) const {
    course.reset(seedOf(level_));
    course.setDifficulty(35.0 * (level_ - 1), 0.35);
    course.setFinish(length());
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
