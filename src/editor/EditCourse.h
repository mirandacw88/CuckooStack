// The level editor's course model (the iPhone and iPad editor's; the web editor's course.js, ported): a level read
// into columns, one a cell (0.6 m) along the course, each a slice of a wall (how high, its kind, a barrier's underside
// over it, a floating block's top) or of a maze gauntlet's span; its hazards (Hazards.h: x, metres along, first); its
// finish line, difficulty band and raids. Each edit changes the course in place and says whether it did (EditorDoc
// keeps whole courses for undo).
//
// A wall is a run of like columns; `cut` marks where the file starts a new one beside a like one (the game treats them
// as two), so a level reads and writes back as it was. An edit that makes a column like its neighbour joins them.
#pragma once

#include "core/Hazards.h"
#include "core/LevelData.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace cs::ed {

constexpr int kMaxH = 60, kMaxCeil = 80;
constexpr double kMinLength = 30;      // a level; a chunk may be shorter (kMinChunkLength)
constexpr double kMinChunkLength = 12;

struct Col {
    std::string g;                  // a gauntlet's span (its name); else a wall's slice
    int h = 0;
    WallKind kind = WallKind::Crate;
    int ceil = 0, top = 0;
    bool cut = false;
    bool operator==(const Col& o) const { return g == o.g && h == o.h && kind == o.kind && ceil == o.ceil && top == o.top && cut == o.cut; }
};
bool alike(const Col& a, const Col& b);

// a gauntlet as the course lays it (its door's and exit's rows, its width)
struct GridInfo { int cols = 0, entryRow = 0, entryTop = 0, exitRow = 0; };

struct Course {
    int number = 0;
    std::vector<std::string> notes;
    double length = 0, diffOffset = 0, diffScale = 0.35;
    int raids = -1;
    int hard = 0;                   // super hard (LevelData's): the checker's margins less
    std::vector<Col> cols;
    std::vector<Placed> items;      // in order along the course within each kind
    std::vector<std::string> unknown;
    bool operator==(const Course& o) const {
        return number == o.number && notes == o.notes && length == o.length && diffOffset == o.diffOffset && diffScale == o.diffScale && raids == o.raids && hard == o.hard &&
               cols == o.cols && items == o.items && unknown == o.unknown;
    }
};

Course fromData(const LevelData& d);
LevelData toData(const Course& c);   // its walls: runs of like columns, cut where it says
double mm(double v);                 // to the millimetre

// reading it
inline double courseLength(const Course& c) { return c.cols.size() * U; }
inline int colAt(double x) { return static_cast<int>(std::floor(x / U + 1e-9)); }
struct Span { std::string name; int c0 = 0, c1 = 0; };
std::vector<Span> spans(const Course& c);
std::optional<Span> spanOf(const Course& c, const std::string& name);
bool inGauntlet(const Course& c, double x);
int itemNear(const Course& c, const HazardType* t, double x, double y, double tol); // its index in items, or -1 (y: free ones)
// a placed hazard's height to draw it at, metres: a surface one on what it's set into (a floating block's top, or the
// wall), a free one at its own height, one in a face the middle of its row
double itemY(const Course& c, const Placed& p);
// a cell solid (a wall, a block, a barrier, a gauntlet's span; past either end: no)
bool solidCell(const Course& c, int col, int row);
// the wall's or block's side nearest (x, y) within `cols` columns, at the finger's row: its numbers as a face hazard's
// (x its edge, its row, 1 if it faces right), or none
std::optional<std::vector<double>> faceNear(const Course& c, double x, double y, int cols = 2);
// a face hazard's reach along its row, metres (out from its face to the first solid cell, at most 16 m)
std::pair<double, double> faceReach(const Course& c, const Placed& p);
// the surface at x, eggs (a floating block's top, or the wall)
int surfaceAt(const Course& c, double x);
// a stretch's end (lava: x + its length in cells); a spot's is its x
double itemEnd(const Placed& p);

// walls
bool setHeight(Course& c, int col, int h, std::optional<WallKind> kind = std::nullopt);
bool setKind(Course& c, int col, WallKind kind);
// a bouncer wall (a bumper or a gate) `h` high (1 at least) in the column: never the course's first, nothing over it
// (its barrier or block goes)
bool setBouncer(Course& c, int col, int h, WallKind kind);
bool setCeil(Course& c, int col, int ceil);
bool setBlock(Course& c, int col, int bottom, int top, std::optional<WallKind> kind = std::nullopt);

// hazards: anywhere their row allows (on the course, off gauntlets if it says so, apart by its minGap); x snapped to
// its row's gap from a neighbour within reach (`snap`)
double snapX(const Course& c, const HazardType& t, double x, int exclude = -1);
bool itemFits(const Course& c, const HazardType& t, const std::vector<double>& v, int exclude = -1);
int addItem(Course& c, const HazardType& t, std::vector<double> v); // its index, or -1
bool moveItem(Course& c, int i, std::vector<double> v);              // (its index may change: items stay in order)
int moveItemTo(Course& c, int i, std::vector<double> v);             // ...its new index, or -1
bool removeItem(Course& c, int i);
bool setItemParam(Course& c, int i, size_t param, double value);

// columns: copied out, put in (what's after them moves on), taken out (what's after moves back)
struct Clip { std::vector<Col> cols; std::vector<Placed> items; };
std::optional<Clip> extractCols(const Course& c, int c0, int c1);
bool insertCols(Course& c, int at, const Clip& clip);
bool duplicateCols(Course& c, int c0, int c1);
bool deleteCols(Course& c, int c0, int c1, double minLength = kMinLength); // (the course keeps minLength)

bool setLength(Course& c, double x, double minLength = kMinLength);

// what a stair up off a porch needs of the course there (placeGauntlet; none: eggsPerCell 0): the meter's refill, a cell
// of run (Level::eggsPerCell), the curve's tallest climb, the shortest wall of its stair
struct StairUp { double eggsPerCell = 0; int maxUp = 0, minLen = 4; };

// gauntlets (one in a level at most): its door at column `at`, its forecourt (its lintel among it) `forecourt` cells
// (Level::forecourtAt), a porch; what was there goes, hazards with it. Moved: where it was goes back to street
// (speed: the run's at its porch; > 0: a fair stair down off the porch to the course, stairDown)
bool placeGauntlet(Course& c, const std::string& name, const GridInfo& grid, int at, int forecourt, double speed = 0, const StairUp& up = {});
int stairDown(Course& c, int z, int h, double speed);
// ...and off a porch onto a wall too tall to climb (more than the curve's maxUp above it, or more than a careful player's
// meter holds: the porch's budget is 1 egg and 8 cells' refill, then the porch's own cells'): a stair up, laid in front
// of the wall (the course after it moves on), each climb the curve's maxUp at the most and the meter's to spare, on a
// run-up long enough to refill it (Level::affordClimb's rules). Its end (a column: the wall's first)
int stairUp(Course& c, int z, int h, const StairUp& up);
bool removeGauntlet(Course& c, const std::string& name, int forecourt);
bool fitGauntlet(Course& c, const std::string& name, int width);

// the play-test's start: x metres along, on the decimetre, 1 m clear of any saw, short of the finish (none in a gauntlet)
std::optional<double> startOnCourse(const Course& c, double x);

} // namespace cs::ed
