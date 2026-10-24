// The campaign's levels and gauntlets a game plays: the built-in ones (LevelLayouts.inc, GauntletLayouts.inc), with
// any stand-ins on top (a level or a grid as the level editor has it, unsaved edits and all). Each game reads its own
// (GameServices::levels), so the level editor's play-test and its bot runs, each a game on its own thread with its
// own stand-ins, never share a mutable map; the built-in catalog is parsed once and never changed.
//
// The free functions (levelData, gauntletNamed: LevelData.h, Gauntlet.h) and a game without a catalog of its own read
// the developer tools' (dev(): the built-in one with debugOverrideLevel's and debugOverrideGauntlet's stand-ins).
#pragma once

#include "Chunk.h"
#include "Gauntlet.h"
#include "LevelData.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cs {

class LevelCatalog {
public:
    static const LevelCatalog& builtin();
    static LevelCatalog& dev();

    // stand-ins over `base` (null: none, an empty catalog)
    explicit LevelCatalog(const LevelCatalog* base = &builtin()) : base_(base) {}
    LevelCatalog(const LevelCatalog&) = delete; // (what it hands out are pointers into it)
    LevelCatalog& operator=(const LevelCatalog&) = delete;

    const LevelData* level(int number) const;              // nullptr: none (the generator lays it)
    const Gauntlet* gauntlet(const std::string& name) const; // nullptr: none by that name
    std::vector<int> levelNumbers() const;                 // in order
    std::vector<std::string> gauntletNames() const;        // in order
    std::vector<std::string> standInGauntlets() const;     // the grids this one has over its base's, in order
    // the designer's chunks (Chunk.h), in the list's order: the built-in ones are assets/chunks (ChunkLayouts.inc); a catalog
    // that was given its own (setChunks) has those instead. None built in: the generated levels are as they always were
    const std::vector<Chunk>& chunks() const;
    // chunk files ({id, text}: ChunkLayouts.inc's) read, in the list's order; one that doesn't read is left out, and said on stderr
    static std::vector<Chunk> chunksFrom(const std::vector<std::pair<std::string, std::string>>& files);

    // a stand-in, from now on: false, and why in *error, if it doesn't parse (a level, with this catalog's grids; a
    // grid standing in for one already here keeps its address: a course laid with it sees the new one)
    bool setLevel(int number, const std::string& text, std::string* error = nullptr);
    bool setGauntlet(const std::string& name, const std::vector<std::string>& rowsTopFirst, std::string* error = nullptr);
    void setLevel(LevelData d);                            // (already parsed: checked against this catalog by the caller)
    void removeLevel(int number);                          // gone, the base's too (the generator lays it)
    void setChunks(std::vector<Chunk> chunks);             // this catalog's own chunks, over its base's (sorted into the list's order)

private:
    struct Builtin {};
    explicit LevelCatalog(Builtin);
    const LevelCatalog* base_ = nullptr;
    std::map<int, LevelData> levels_;
    std::map<std::string, Gauntlet> gauntlets_;
    std::set<int> removed_;
    std::vector<Chunk> chunks_;
    bool ownChunks_ = false;
};

} // namespace cs
