// Levels edited in two places (the iPhone or iPad editor away from home, and the Mac) brought together: a three-way
// merge of a level's text against the version both started from, field by field (its header), column by column (its
// course), hazard by hazard (each kind a set), line by line (its notes and the sections this build doesn't know). And a
// level refitted to its gauntlet's new width (what's after it moved on or back: level_check --refit, the editors' save).
#pragma once

#include "core/LevelCatalog.h"

#include <string>
#include <vector>

namespace cs::ed {

struct MergeResult {
    bool ok = false;                     // merged cleanly: text is the level both ways
    std::string text;                    // the merged level (ok), else mine with what merged cleanly of theirs
    std::vector<std::string> conflicts;  // what both changed differently: "length", "course", "notes", "difficulty"...
    std::string error;                   // a text that doesn't parse
};
// base: the level both started from; mine and theirs: each side's now. Parsed with cat's gauntlets (null: dev())
MergeResult merge3(int number, const std::string& base, const std::string& mine, const std::string& theirs, const LevelCatalog* cat = nullptr);

// the level's text with the named gauntlet's span made `width` cells wide: what's after it (hazards, the finish line)
// moved on or back; the text as it was if it isn't there or is that wide already. (No parse: what isn't a course or
// hazard line stays as it was, the sections this build doesn't know among them)
std::string refit(const std::string& text, const std::string& name, int width);

} // namespace cs::ed
