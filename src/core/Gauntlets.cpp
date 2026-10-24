// The gauntlets, by name (Gauntlet.h for the cells). Each is designed around its own idea (docs/LEVEL_DESIGN.md); its
// grid is assets/gauntlets/<name>.txt (tuned in the level editor, tools/level_editor), built into GauntletLayouts.inc
// (scripts/gauntlets/build_gauntlets.py), and tested: one way through, reachable by stacking alone, no dead end that
// traps the hen, its near misses, the egg meter (Gauntlet_test), and cleared by the bot in a real run
// (GameGauntlet_test). The game's own are LevelCatalog::builtin(); these read LevelCatalog::dev().
#include "Gauntlet.h"
#include "LevelCatalog.h"

#include <cctype>

namespace cs {

const Gauntlet* gauntletNamed(const std::string& name) { return LevelCatalog::dev().gauntlet(name); }

bool validGauntletName(const std::string& name) {
    if (name.empty() || name.size() > 32 || !(name[0] >= 'a' && name[0] <= 'z')) return false;
    for (char ch : name) if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
    return true;
}

std::string gauntletTitle(const std::string& name) {
    std::string n = "THE ";
    for (char ch : name) n += ch == '_' ? ' ' : char(std::toupper(static_cast<unsigned char>(ch)));
    return n;
}

bool debugOverrideGauntlet(const std::string& name, const std::vector<std::string>& rowsTopFirst, std::string* error) {
    return LevelCatalog::dev().setGauntlet(name, rowsTopFirst, error);
}

std::vector<std::string> gauntletNames() { return LevelCatalog::dev().gauntletNames(); }

} // namespace cs
