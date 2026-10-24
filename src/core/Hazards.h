// The hazards a level places along its course, one row each, read by everything that handles a level: the level file
// (its section and its lines: LevelData.h), the course laid from it (Level::load), the checker (LevelJudge: its notes
// and the egg budget), and the level editors (registryJson: their tools, inspectors and drawings). A new hazard is a
// row here (hazards/<Name>.cpp) and the game's own code for it (its update and drawing); the parser, the checker, the
// editors' tools and sync need nothing more.
//
// The walls keep their grammar (a course line: cells, height, kind, barrier, block top: LevelData.h); their kinds'
// names and looks are rows here too (wallKinds), so the editors draw them from the registry as well.
#pragma once

#include "Level.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs {

struct LevelData;
struct Report;
struct JudgeView;

// a number of a placed hazard: its meaning, its unit and its range (the editors' sliders and steppers; the parser
// turns away what's outside it)
struct ParamSpec {
    const char* key;     // "x"
    const char* label;   // "Along"
    const char* unit;    // "m"
    double min, max, step, def;
};

// where one sits: on a surface (a wall's top or the street, its height from what's under it), anywhere (its height
// its own: the second number, metres), or in a wall's or a block's side (a face: x a column's edge, the second number
// its row in eggs up from the street, the third the way it faces: 0 left, 1 right; a wall laser)
enum class Anchor : uint8_t { Surface, Free, Face };

struct HazardType;

// one placed in a level: its type and its numbers (as its type's params: x, metres along, first)
struct Placed {
    const HazardType* type = nullptr;
    std::vector<double> v;
    double x() const { return v.empty() ? 0 : v[0]; }
    double at(size_t i) const { return i < v.size() ? v[i] : 0; }
    bool operator==(const Placed& o) const { return type == o.type && v == o.v; }
};

// what one costs a careful player on the run (the egg budget), and what to call it ("A blade needs an egg")
struct EggCost { double x = 0, y = 0, eggs = 1; const char* what = ""; };

struct HazardType {
    // the level file
    const char* id;            // "saw"
    const char* section;       // its section word: "saws"
    const char* form;          // what a line of it is (the parser's complaint): "a saw: where along, metres"
    std::vector<ParamSpec> params;
    int perLine = 1;           // one a line, or 0: as many as a line holds (written 8 to a line)
    bool alwaysWritten = false; // its section written even when it's empty (the game's first hazards: every level has it)
    // the editors
    const char* label;         // "Saw"
    const char* plural;        // "saws"
    const char* key;           // its tool's key: "s"
    const char* tip;           // what it does, in a sentence
    const char* icon;          // an SF Symbol (the iPhone and iPad editor)
    const char* glyph;         // how the editors draw it: "saw", "disco", or "marker" (a diamond in its colour)
    uint32_t color;            // 0xRRGGBB
    Anchor anchor = Anchor::Surface;
    double snapGap = 0;        // placed near another, it snaps this far from it (a saw: a row's gap, blades back to back)
    double minGap = 0;         // never closer than this to another (0: anywhere)
    bool offGauntlets = false; // never in a gauntlet's span (a gauntlet's own are its grid's)
    // the game and the checker
    std::string (*validate)(const Placed& p, const LevelData& d) = nullptr;       // "" when it's fine (parseLevel)
    void (*spawn)(Level& l, const Placed& p) = nullptr;                           // into a course (Level::load)
    void (*check)(const JudgeView& v, Report& r) = nullptr;                       // its notes (LevelJudge)
    void (*costs)(const Level& l, std::vector<EggCost>& out) = nullptr;           // the egg budget (LevelJudge)
    // a stretch, not a spot: the param that's its length in cells from x (lava: its second); -1: none
    int spanParam = -1;
    // the course generator lays it (Level: setSaws, setLasers, setLava, the disco balls), so the editors' generators
    // offer it as a switch
    bool generated = false;
    // the editors' tool groups: "hazards" (the iPhone and iPad rail's Hazards flyout), "pickups" (a disco ball: good
    // for her, its own button) or "bouncers" (a spring: the Bouncers flyout, with the bumpers and gates)
    const char* group = "hazards";
};

// every hazard, in the level file's order
const std::vector<const HazardType*>& hazardTypes();
const HazardType* hazardType(const std::string& id);
const HazardType* hazardSection(const std::string& section);
// a hazard the game doesn't have (tests: one row is all a hazard takes); not while games run on other threads
void registerHazard(const HazardType* t);
void unregisterHazard(const HazardType* t);

extern const HazardType kSawHazard, kDiscoHazard, kLaserHazard, kLavaHazard, kSpringHazard;

// a wall's kind: its word in the level file, and how the editors show it
struct WallKindInfo {
    WallKind kind;
    const char* id;            // "crate"
    const char* label;         // "Crates"
    const char* key;           // "1"
    const char* tip;
    uint32_t color;
    bool bouncer = false;      // a bumper or a gate (the editors' Bouncers tools; Level::buildZones)
};
const std::vector<WallKindInfo>& wallKinds();
const WallKindInfo* wallKindNamed(const std::string& id);
const WallKindInfo& wallKindInfo(WallKind k);

// all of it for the level editors: {"hazards":[...],"kinds":[...]}
std::string registryJson();

} // namespace cs
