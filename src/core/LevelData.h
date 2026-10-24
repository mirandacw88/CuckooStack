// The campaign's levels as authored: assets/levels/level_NN.txt (tuned in the level editor, tools/level_editor), built
// into LevelLayouts.inc (scripts/levels/build_levels.py). Each is its whole course, wall by wall (a maze gauntlet's span
// among them), its saws and disco balls, its finish line, its difficulty band and its droid raids. Campaign lays a
// level from its data (Level::load); a level past the last one is laid by the generator, as ever (Level.h).
//
// A level file (';' starts a note: the notes at its top are its description):
//   length 420               the finish line, metres along
//   difficulty 420 0.35      its difficulty band: the curve (Difficulty.h) read at 420 + 0.35 per metre (speed, refill)
//   raids auto               the droid's raids: auto (the game's own rule), none, or how many (at random in its middle)
//   course                   its walls, left to right from the start, one a line:
//   16 0 crate                 how long (cells: 0.6 m each), how high (eggs), its kind (crate, hay or mix), and, if
//   7 2 hay 6                  there's a barrier over it, the barrier's underside (eggs up), and, if that's a floating
//   5 0 crate 3 5              block's, the block's top (ducked under, or climbed onto as high as its top);
//   168 0 gauntlet gallery     or a maze gauntlet: its grid's width, 0, gauntlet and its name (assets/gauntlets)
//   saws                     the blades set into its walls and street, where along (metres, the hub), any number a
//   112.5 130.2                line (one on a hay or mix wall's top is a cube saw; a gauntlet's are its grid's own)
//   discos                   the disco balls: where along, how high (metres), one a line
//   30.15 1.45
// The hazards' sections (saws, discos, ...) are the registry's (Hazards.h): each its word and its numbers. A section
// this build doesn't know (a newer game's hazard) is kept as it is, and written back after the others.
#pragma once

#include "Hazards.h"
#include "Level.h"

#include <optional>
#include <string>
#include <vector>

namespace cs {

class LevelCatalog;

struct LevelData {
    int number = 0;
    std::vector<std::string> notes;         // its description
    double length = 0;                      // the finish line, metres
    double diffOffset = 0, diffScale = 0.35; // Level::setDifficulty
    int raids = -1;                         // -1: the game's own rule (Game::planRaids); else that many
    int hard = 0;                           // super hard (the generator's density 6 to 10): the checker's human margins
                                            // shrink (CourseRules.h's marginScale); 0: as ever
    struct Wall {
        int cells = 0, h = 0;
        WallKind kind = WallKind::Crate;
        int ceil = 0;                       // a barrier's underside (eggs); 0: none
        int top = 0;                        // a floating block's top (eggs; its underside is ceil); 0: a barrier only
        std::string gauntlet;               // a maze gauntlet's span (its name), not a wall
    };
    std::vector<Wall> course;
    std::vector<Placed> items;              // its hazards, in the file's order (the gauntlets' own come from their grids)
    std::vector<std::string> unknown;       // sections this build doesn't know: their lines, as they were

    std::vector<double> xsOf(const HazardType& t) const; // where its hazards of a kind are, metres along, in order
    size_t countOf(const HazardType& t) const;

    double courseLength(const LevelCatalog* cat = nullptr) const; // how far its walls reach, metres (its gauntlets cat's:
                                                                  // null, LevelCatalog::dev())
};

// a level file's text: its data, or why not (a wall too high for its barrier, a gauntlet that isn't one or isn't as
// wide as its span, a course short of its finish line...); its gauntlets cat's (null: LevelCatalog::dev())
std::optional<LevelData> parseLevel(int number, const std::string& text, std::string* error = nullptr, const LevelCatalog* cat = nullptr);
// ...and back (the level editor's, the bake's)
std::string levelText(const LevelData& d);
// a level file's text with its gauntlet `from` laid as `to` instead (its "N 0 gauntlet NAME" line; the rest as it was)
std::string renameGauntletIn(const std::string& text, const std::string& from, const std::string& to);
// a number as a level file writes it: to the millimetre, no trailing zeros
std::string levelNum(double v);

// LevelCatalog::dev()'s (LevelCatalog.h):
const LevelData* levelData(int number); // an authored level (nullptr: none, the generator lays it)
std::vector<int> levelNumbers();        // every authored level's number, in order
// Developer tools (the level editor's play-test, level_check --game): a level to play instead of the built-in one, from
// now on (before a course is laid from it), in every game without a catalog of its own; false, and why in *error, if
// it doesn't parse
bool debugOverrideLevel(int number, const std::string& text, std::string* error = nullptr);

} // namespace cs
