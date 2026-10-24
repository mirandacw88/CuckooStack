// The level designer's checker (desktop builds; the level editor runs it): a level file (assets/levels, LevelData.h)
// judged as the tests judge the campaign's levels, and played by the bot in the real game; or a level baked by the
// generator (how the levels were first laid, and how a new one starts).
//   level_check --bake N [--gauntlet NAME|none]   level N's course as the generator lays it (its seed, its length, its
//                                                  saws, its gauntlet at 40% of the way), as a level file
//   level_check --file PATH|- --level N [--gauntlet-file PATH ...] [--json] [--game]
//       its course: walls, barriers, saws, disco balls, gauntlets; the egg budget (a careful player's meter, stacking
//       just enough: where a climb or a blade asks for more than it holds); notes where it reads unfairly (a saw on a
//       slope or under a barrier, no landing run after a drop, a wall no one can climb under the barrier before it, a
//       gauntlet's door off its forecourt's height); --game: the real game, the bot playing it from its start (headless,
//       as GameDev_test does), the layouts in the --gauntlet-files standing in for those gauntlets. --json: all of it
//       as one object (the level editor's)
//   level_check --refit NAME WIDTH --file PATH|-   the level with NAME's span made WIDTH cells wide, what's after it moved
//                                                  on or back (editor/LevelMerge.h: the level editors' save)
//   level_check --chunk PATH|- [--id ID] [--json] [--game]   a chunk file (Chunk.h, assets/chunks/<id>.txt) read as the generators
//                                                  read it, its body judged as the chunk editor's checker judges it (and flipped,
//                                                  if Mirror is on and it can be); --game: the bot plays each. The id is the file's
//                                                  name (--id: another). Prints the JSON (ReportJson.h: chunkReportJson); exit 1 if
//                                                  it doesn't read
//   level_check --registry-json   the course's rules the level editors lay by (ReportJson.h: courseRulesJson)
#include "core/Campaign.h"
#include "core/Gauntlet.h"
#include "core/LevelBake.h"
#include "core/LevelData.h"
#include "core/ReportJson.h"
#include "editor/LevelMerge.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace cs;

namespace {

std::vector<std::string> readLines(std::istream& in) {
    std::vector<std::string> out;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string file, gauntletName;
    std::vector<std::string> gauntletFiles;
    int level = 0, bakeN = 0;
    bool json = false, game = false, hasGauntlet = false, rules = false;
    std::string refitName, chunkFile, chunkId;
    int refitWidth = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bake") && i + 1 < argc) bakeN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--gauntlet") && i + 1 < argc) { gauntletName = argv[++i]; hasGauntlet = true; }
        else if (!std::strcmp(argv[i], "--gauntlet-file") && i + 1 < argc) gauntletFiles.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!std::strcmp(argv[i], "--level") && i + 1 < argc) level = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--json")) json = true;
        else if (!std::strcmp(argv[i], "--game")) game = true;
        else if (!std::strcmp(argv[i], "--chunk") && i + 1 < argc) chunkFile = argv[++i];
        else if (!std::strcmp(argv[i], "--id") && i + 1 < argc) chunkId = argv[++i];
        else if (!std::strcmp(argv[i], "--registry-json")) rules = true;
        else if (!std::strcmp(argv[i], "--refit") && i + 2 < argc) { refitName = argv[++i]; refitWidth = std::atoi(argv[++i]); }
    }
    // layouts standing in for gauntlets (the editor's unsaved grids): before anything lays a course with them
    for (const std::string& path : gauntletFiles) {
        std::ifstream in(path);
        std::vector<std::string> rows;
        for (const std::string& line : readLines(in)) if (!line.empty() && (line[0] != ';' || Gauntlet::hardOf(line) >= 0)) rows.push_back(line); // (a "hard N" note counts)
        std::string name = path.substr(path.find_last_of('/') + 1), why;
        name = name.substr(0, name.find('.'));
        if (!debugOverrideGauntlet(name, rows, &why)) {
            if (json) std::printf("{\"ok\":false,\"error\":\"%s\"}\n", jsonEsc(why).c_str());
            else std::printf("%s\n", why.c_str());
            return 1;
        }
    }
    if (rules) { std::printf("%s\n", courseRulesJson().c_str()); return 0; }
    if (!refitName.empty()) { // (the text as it is, not a line at a time: what isn't a course or hazard line stays as it was)
        std::ifstream f;
        std::istream* in = &std::cin;
        if (!file.empty() && file != "-") { f.open(file); in = &f; }
        std::stringstream text;
        text << in->rdbuf();
        std::printf("%s", ed::refit(text.str(), refitName, refitWidth).c_str());
        return 0;
    }
    if (!chunkFile.empty()) { // (a chunk's text, as it is: the header's lines count)
        std::ifstream f;
        std::istream* in = &std::cin;
        if (chunkFile != "-") { f.open(chunkFile); in = &f; }
        std::stringstream text;
        text << in->rdbuf();
        if (chunkId.empty() && chunkFile != "-") {
            chunkId = chunkFile.substr(chunkFile.find_last_of('/') + 1);
            chunkId = chunkId.substr(0, chunkId.rfind('.'));
        }
        const std::string out = chunkReportJson(chunkId, text.str(), game);
        std::printf("%s\n", out.c_str());
        return out.compare(0, 11, "{\"ok\":true,") == 0 ? 0 : 1;
    }
    if (bakeN > 0) {
        const Gauntlet* g = hasGauntlet ? (gauntletName == "none" ? nullptr : gauntletNamed(gauntletName)) : Campaign::gauntletOf(bakeN);
        if (hasGauntlet && gauntletName != "none" && !g) { std::fprintf(stderr, "no gauntlet called %s\n", gauntletName.c_str()); return 1; }
        std::printf("%s", levelText(bakeLevel(bakeN, g)).c_str());
        return 0;
    }
    if (file.empty() || level <= 0) {
        std::fprintf(stderr, "level_check --file PATH|- --level N [--gauntlet-file PATH ...] [--json] [--game] | --bake N [--gauntlet NAME|none] | --registry-json\n");
        return 2;
    }
    std::ifstream f;
    std::istream* in = &std::cin;
    if (file != "-") { f.open(file); in = &f; }
    std::string text;
    for (const std::string& line : readLines(*in)) text += line + "\n";
    std::string why;
    const std::optional<LevelData> d = parseLevel(level, text, &why);
    if (!d) {
        if (json) std::printf("{\"ok\":false,\"level\":%d,\"error\":\"%s\"}\n", level, jsonEsc(why).c_str());
        else std::printf("doesn't parse: %s\n", why.c_str());
        return 1;
    }
    if (game) debugOverrideLevel(level, text); // (the campaign lays this course)
    if (json) std::printf("%s\n", levelReportJson(*d, game).c_str());
    else std::printf("%s", levelReportText(*d).c_str());
    return 0;
}
