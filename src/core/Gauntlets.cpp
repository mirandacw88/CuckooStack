// The gauntlets, by name (Gauntlet.h for the cells). Each is designed around its own idea (docs/LEVEL_DESIGN.md); its
// grid is assets/gauntlets/<name>.txt (tuned in the level editor, tools/level_editor), built into GauntletLayouts.inc
// (scripts/gauntlets/build_gauntlets.py), and tested: one way through, reachable by stacking alone, no dead end that
// traps the hen, its near misses, the egg meter (Gauntlet_test), and cleared by the bot in a real run
// (GameGauntlet_test).
#include "Gauntlet.h"

#include <map>

namespace cs {

namespace {

std::map<std::string, std::vector<std::string>>& layouts() {
    static std::map<std::string, std::vector<std::string>> all = {
#include "GauntletLayouts.inc"
    };
    return all;
}

std::map<std::string, Gauntlet>& parsed() {
    static std::map<std::string, Gauntlet> all;
    return all;
}

} // namespace

const Gauntlet* gauntletNamed(const std::string& name) {
    if (const auto it = parsed().find(name); it != parsed().end()) return &it->second;
    const auto& all = layouts();
    const auto it = all.find(name);
    if (it == all.end()) return nullptr;
    auto g = Gauntlet::parse(name, it->second);
    if (!g) return nullptr;
    return &parsed().emplace(name, std::move(*g)).first->second;
}

bool debugOverrideGauntlet(const std::string& name, const std::vector<std::string>& rowsTopFirst, std::string* error) {
    auto g = Gauntlet::parse(name, rowsTopFirst, error);
    if (!g) return false;
    layouts()[name] = rowsTopFirst;
    // (in place if it's been parsed: whatever points at it sees the new layout)
    if (const auto it = parsed().find(name); it != parsed().end()) it->second = std::move(*g);
    return true;
}

std::vector<std::string> gauntletNames() {
    std::vector<std::string> out;
    for (const auto& [name, rows] : layouts()) out.push_back(name);
    return out;
}

} // namespace cs
