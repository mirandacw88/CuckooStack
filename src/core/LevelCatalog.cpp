#include "LevelCatalog.h"

#include <algorithm>
#include <cstdio>

namespace cs {

LevelCatalog::LevelCatalog(Builtin) {
    static const std::pair<const char*, std::vector<std::string>> grids[] = {
#include "GauntletLayouts.inc"
    };
    for (const auto& [name, rows] : grids) {
        std::string why;
        if (auto g = Gauntlet::parse(name, rows, &why)) gauntlets_.emplace(name, std::move(*g));
        else std::fprintf(stderr, "[CuckooStack] gauntlet %s: %s\n", name, why.c_str());
    }
    static const std::pair<int, const char*> texts[] = {
#include "LevelLayouts.inc"
    };
    for (const auto& [number, text] : texts) {
        std::string why;
        if (auto d = parseLevel(number, text, &why, this)) levels_.emplace(number, std::move(*d));
        else std::fprintf(stderr, "[CuckooStack] %s\n", why.c_str());
    }
    static const std::pair<const char*, const char*> files[] = {
        {"", ""}, // (an array can't be empty: the first is no chunk)
#include "ChunkLayouts.inc"
    };
    std::vector<std::pair<std::string, std::string>> chunkFiles;
    for (const auto& [id, text] : files)
        if (*id) chunkFiles.push_back({id, text});
    chunks_ = chunksFrom(chunkFiles);
    ownChunks_ = true;
}

std::vector<Chunk> LevelCatalog::chunksFrom(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<Chunk> out;
    for (const auto& [id, text] : files) {
        std::string why;
        if (auto c = parseChunk(id, text, &why)) out.push_back(std::move(*c));
        else std::fprintf(stderr, "[CuckooStack] chunk %s: %s\n", id.c_str(), why.c_str());
    }
    std::stable_sort(out.begin(), out.end(), chunkBefore);
    return out;
}

const LevelCatalog& LevelCatalog::builtin() {
    static const LevelCatalog all{Builtin{}}; // (parsed once, by the first to ask: C++ makes the others wait)
    return all;
}

LevelCatalog& LevelCatalog::dev() {
    static LevelCatalog all(&builtin());
    return all;
}

const LevelData* LevelCatalog::level(int number) const {
    if (removed_.count(number)) return nullptr;
    if (const auto it = levels_.find(number); it != levels_.end()) return &it->second;
    return base_ ? base_->level(number) : nullptr;
}

const Gauntlet* LevelCatalog::gauntlet(const std::string& name) const {
    if (const auto it = gauntlets_.find(name); it != gauntlets_.end()) return &it->second;
    return base_ ? base_->gauntlet(name) : nullptr;
}

std::vector<int> LevelCatalog::levelNumbers() const {
    std::set<int> all;
    if (base_) for (int n : base_->levelNumbers()) all.insert(n);
    for (const auto& [n, d] : levels_) all.insert(n);
    std::vector<int> out;
    for (int n : all) if (!removed_.count(n)) out.push_back(n);
    return out;
}

std::vector<std::string> LevelCatalog::gauntletNames() const {
    std::set<std::string> all;
    if (base_) for (const std::string& n : base_->gauntletNames()) all.insert(n);
    for (const auto& [n, g] : gauntlets_) all.insert(n);
    return {all.begin(), all.end()};
}

std::vector<std::string> LevelCatalog::standInGauntlets() const {
    std::vector<std::string> out;
    for (const auto& [n, g] : gauntlets_) out.push_back(n);
    return out;
}

const std::vector<Chunk>& LevelCatalog::chunks() const {
    static const std::vector<Chunk> none;
    if (ownChunks_) return chunks_;
    return base_ ? base_->chunks() : none;
}

void LevelCatalog::setChunks(std::vector<Chunk> chunks) {
    chunks_ = std::move(chunks);
    std::stable_sort(chunks_.begin(), chunks_.end(), chunkBefore);
    ownChunks_ = true;
}

bool LevelCatalog::setLevel(int number, const std::string& text, std::string* error) {
    auto d = parseLevel(number, text, error, this);
    if (!d) return false;
    setLevel(std::move(*d));
    return true;
}

void LevelCatalog::setLevel(LevelData d) {
    removed_.erase(d.number);
    if (const auto it = levels_.find(d.number); it != levels_.end()) it->second = std::move(d);
    else levels_.emplace(d.number, std::move(d));
}

bool LevelCatalog::setGauntlet(const std::string& name, const std::vector<std::string>& rowsTopFirst, std::string* error) {
    auto g = Gauntlet::parse(name, rowsTopFirst, error);
    if (!g) return false;
    if (const auto it = gauntlets_.find(name); it != gauntlets_.end()) it->second = std::move(*g);
    else gauntlets_.emplace(name, std::move(*g));
    return true;
}

void LevelCatalog::removeLevel(int number) {
    levels_.erase(number);
    removed_.insert(number);
}

} // namespace cs
