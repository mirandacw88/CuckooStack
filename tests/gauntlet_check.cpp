// The gauntlet designer's checker (desktop builds; the level editor runs it after every edit): a gauntlet's way
// through, its near misses and the egg meter, judged as Gauntlet_test judges them, and the bot's way drawn on the grid.
//   gauntlet_check NAME [--level N] [--map] [--events] [--reach]   a registered gauntlet (as its level plays it)
// --map draws the bot's way; --reach marks everywhere she can get to ('+', her feet), the way through or not (where a
// design that doesn't get out stops); --events lists its near misses (as the game shows them, played with the meter)
//   gauntlet_check --file PATH|- [--name NAME] [--level N] [...]   a layout, rows top first (';' lines are notes;
//                                                                   '-': stdin), as NAME's level plays it (or N's)
//   ... --level-file PATH   that level as in PATH (LevelData.h), its course laying the layout (the level editor's level,
//                           unsaved edits and all)
//   gauntlet_check --all                                            every registered gauntlet
//   ... --json [--game]   all of it as one JSON object (the level editor's): the solution and where her feet can get,
//                         the bot's way, the meter runs step by step, the near misses, where it lands on its level's
//                         course, its design's signature (GauntletDesigns.h); --game also plays it in the real game,
//                         the bot playing (headless, as GameGauntlet_test does), the layout standing in for the named one
// The report itself (the meter runs, the design checks, the game run): core/GauntletReport.h
#include "core/Campaign.h"
#include "core/Gauntlet.h"
#include "core/GauntletReport.h"
#include "core/LevelData.h"
#include "core/ReportJson.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace cs;

namespace {

// a level standing in for the built-in one (the level editor's, unsaved edits and all): why it doesn't, if it doesn't
std::string& levelError() { static std::string why; return why; }
bool overrideLevel(int level, const std::string& path, FILE* say) {
    std::ifstream in(path);
    std::string text, line;
    while (std::getline(in, line)) text += line + "\n";
    if (!in.eof() || !debugOverrideLevel(level, text, &levelError())) {
        if (levelError().empty()) levelError() = "can't read " + path;
        if (say) std::fprintf(say, "--level-file %s: %s\n", path.c_str(), levelError().c_str());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string name, file, levelFile;
    int level = 0;
    bool map = false, events = false, all = false, reach = false, json = false, game = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!std::strcmp(argv[i], "--level") && i + 1 < argc) level = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--level-file") && i + 1 < argc) levelFile = argv[++i];
        else if (!std::strcmp(argv[i], "--map")) map = true;
        else if (!std::strcmp(argv[i], "--events")) events = true;
        else if (!std::strcmp(argv[i], "--reach")) reach = true;
        else if (!std::strcmp(argv[i], "--all")) all = true;
        else if (!std::strcmp(argv[i], "--json")) json = true;
        else if (!std::strcmp(argv[i], "--game")) game = true;
        else name = argv[i];
    }
    if (all) {
        for (const std::string& n : gauntletNames())
            if (const Gauntlet* g = gauntletNamed(n)) std::printf("%s", gauntletReportText(*g, level ? level : gauntletLevelOf(n), false, false, false).c_str());
        return 0;
    }
    if (!file.empty()) {
        std::vector<std::string> rows;
        std::ifstream f;
        std::istream* in = &std::cin;
        if (file != "-") { f.open(file); in = &f; }
        for (std::string line; std::getline(*in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && (line[0] != ';' || Gauntlet::hardOf(line) >= 0)) rows.push_back(line); // (a "hard N" note counts)
        }
        if (name.empty()) {
            name = file == "-" ? "design" : file.substr(file.find_last_of('/') + 1);
            name = name.substr(0, name.find('.'));
        }
        if (!level) level = gauntletLevelOf(name);
        if (!level) level = 13;
        std::string err;
        const std::optional<Gauntlet> g = Gauntlet::parse(name, rows, &err);
        if (!g) {
            if (json) std::printf("{\"ok\":false,\"name\":\"%s\",\"level\":%d,\"error\":\"%s\"}\n", jsonEsc(name).c_str(), level, jsonEsc(err).c_str());
            else std::printf("doesn't parse: %s\n", err.c_str());
            return 1;
        }
        debugOverrideGauntlet(name, rows); // (the level's course lays this layout)
        if (!levelFile.empty() && !overrideLevel(level, levelFile, json ? nullptr : stdout)) {
            if (json) std::printf("{\"ok\":false,\"name\":\"%s\",\"level\":%d,\"error\":\"level %d: %s\"}\n", jsonEsc(name).c_str(), level, level, jsonEsc(levelError()).c_str());
            return 1;
        }
        if (json) std::printf("%s\n", gauntletReportJson(*g, name, level, game).c_str());
        else std::printf("%s", gauntletReportText(*g, level, map, events, reach).c_str());
        return 0;
    }
    const Gauntlet* g = gauntletNamed(name);
    if (!g) { std::printf("no gauntlet named '%s' (try --all)\n", name.c_str()); return 1; }
    const int at = level ? level : gauntletLevelOf(name);
    if (json) std::printf("%s\n", gauntletReportJson(*g, name, at, game).c_str());
    else std::printf("%s", gauntletReportText(*g, at, map, events, reach).c_str());
    return 0;
}
