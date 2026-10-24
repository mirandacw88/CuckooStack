// Procedural course: wall segments, overhead barriers, saw blades and disco-ball pickups.
// generateUntil() lays it ahead of the run from a seeded mulberry32 stream; every climb and every saw is affordable
// (the egg budget).
#pragma once

#include "Difficulty.h"
#include "Gauntlet.h"
#include "Math.h"

#include <memory>
#include <vector>

namespace cs {

struct LevelData;
class LevelCatalog;

constexpr double U = 0.6;         // one egg / one crate
constexpr float Uf = 0.6f;
constexpr float DEPTH = 1.3f;     // crate wall depth
constexpr double SECTOR = 150.0;  // sector length, metres
constexpr double HEN_H = 1.05;    // hen hitbox height above the top egg

// a wall's kind: crates, hay, hay-topped; or a bouncer (its column a maze's bumper or one-way gate, its height its top:
// Level::buildZones makes each group of them a bounce zone)
enum class WallKind : uint8_t { Crate, Hay, Mix, Bumper, GateR, GateL };
inline bool bouncer(WallKind k) { return k == WallKind::Bumper || k == WallKind::GateR || k == WallKind::GateL; }

struct Segment {
    double x0 = 0, x1 = 0;
    int h = 0;
    WallKind kind = WallKind::Crate;
    int ceil = 0;              // barrier underside in eggs; 0 = no barrier
    int top = 0;               // a floating block's top (eggs), its underside the barrier's: ducked under, or climbed onto
                               // with a stack as high as its top (the eggs below it knocked off, as on any wall); 0: none
    bool gauntlet = false;     // a maze gauntlet's span (Level::gauntlets): its walls are its grid's, not this segment's
    double minHead = 9;        // tightest head clearance seen under this barrier
    bool hasMinHead = false, closeCallChecked = false;
    // ranges into the cached instance arrays
    int capStart = 0, capCount = 0, ceilStart = 0, ceilCount = 0;
};

struct Corn { double x, y; float phase; bool missed = false; };

// A gauntlet laid on the course: its grid's bottom-left corner at (x0, the street)
struct PlacedGauntlet {
    const Gauntlet* g = nullptr; // (a bounce zone's: Gauntlet::isZone)
    double x0 = 0;
    double x1() const { return x0 + g->cols() * 0.6; }
    int col(double x) const { return static_cast<int>(std::floor((x - x0) / 0.6)); }
};

// A circular saw blade set into the course (levels 11 on: Campaign::applyTo). Crossing it slices the bottom egg off the
// stack, once; with no egg under her it kills the hen (Game, GameSaw.cpp). A floor saw sits in the street or a stone
// wall's top; a cube saw in the top of a cyber-cube wall, a beat past its front edge (so a climb onto it that leaves no
// egg to spare ends on the blade). Its top half spins above the surface.
struct Saw {
    enum class State : uint8_t { Armed, Cut, Passed, Smashed };
    double x = 0;             // the hub along the course
    int y = 0;                // the surface it's set into, in eggs (0: the street)
    bool cube = false;        // in a cyber-cube wall (else the street or stone)
    State state = State::Armed;
    bool warned = false;      // its approach whine has played
    float sink = 0;           // after its cut, it sinks into its slot (0 up .. 1 gone)
};

// A wall laser (Hazards.h's laser; GameLaser.cpp): set into a wall's or a block's side (a face, at a column's edge) at
// a row (eggs up from the street), it fires along the course, left or right, as far as its reach: the open cells of
// that row out from its face, up to whatever's solid there (a wall, a block, a barrier, a maze's span; at most
// kLaserRange). As she comes near it charges (a pointer's thin line and dot along its line, a split second), then fires
// a thick beam while she's in its reach, then rests. It hits her once a pass (in its reach; zapped till she's out of
// every laser's): the first time its row meets her (her stack or her body, falling down through it too), an egg there
// is cut out (the eggs above drop), her body there cuts her in half. In a maze, a stone cell with a laser in its left
// or right face ('(' and ')').
struct Laser {
    enum class State : uint8_t { Idle, Charging, Charged, Firing, Resting };
    double x = 0;             // the face it's set into, metres along
    int row = 0;              // its beam's row, eggs up from the street
    int dir = -1;             // the way it fires: -1 left (a left face), 1 right
    double x0 = 0, x1 = -1;   // its reach along the course (x1 < x0: not traced yet: the course isn't laid that far)
    State state = State::Idle;
    float t = 0;              // time in its state, seconds
    bool warned = false;      // its charging hum has played (this charge)
    float flash = 0;          // the beam's flare after a hit (1 .. 0)
    double hitX = -1;         // where its beam struck her (the beam drawn up to there), -1: nowhere
    bool traced() const { return x1 >= x0; }
    double centre() const { return (row + 0.5) * 0.6; } // its beam's height, metres
};

// Lava (Hazards.h's lava; GameLava.cpp): a stretch of a surface that burns the egg under her stack, one each cell she
// crosses (with none, her). In a maze, a run of lava cells ('~') is one.
struct Lava {
    double x0 = 0, x1 = 0;    // along the course
    int floor = 0;            // the surface it's on (eggs)
    bool warned = false;      // its sizzle has played
    int cells() const { return static_cast<int>(std::lround((x1 - x0) / 0.6)); }
};

// A spring (Hazards.h's spring; GameSpring.cpp): a pad on a surface that launches her, stack and all, `power` rows up
// as she runs over it, her speed kept: she flies over what's ahead with no eggs, and lands where the arc comes down
// (CourseRules.h's springFlight). Its front meeting a wall's face below her feet crashes her, as any wall does
struct Spring {
    double x = 0;             // its middle, along the course
    int floor = 0;            // the surface it's on (eggs)
    int power = 6;            // rows of lift (2 to 12)
    bool fired = false;       // it's launched her (the game: once a pass)
    float squash = 0;         // its pad's bounce (the game's drawing: 1 as it fires, back to 0)
};

class Level {
public:
    struct Block { glm::vec3 pos; bool flipped, hay; float tint; };
    struct Tile { glm::vec3 pos; int gx, gz; };
    struct Girder { glm::vec3 pos; };
    struct Curtain { glm::vec3 pos; float width; };

    void reset(uint32_t seed);
    bool generateUntil(double xmax);
    // A level as authored (LevelData.h, the campaign's): its walls, barriers and gauntlets (with their own saws), its
    // saws and disco balls, its finish line and difficulty band. Past its last wall generateUntil lays flat runway only
    void load(const LevelData& d, const LevelCatalog* cat = nullptr); // (its gauntlets cat's: null, LevelCatalog::dev())
    // load's hazards (Hazards.h: each row lays its own): a saw set into the wall at x (the course laid first; load sorts
    // them after), a disco ball
    void addSaw(double x);
    void addCorn(double x, double y) { spawnCorn(x, y); }
    void addLaser(double x, int row, int dir); // set into the face at x, its beam at `row` (eggs up), firing dir (-1, 1)
    // its reach (Laser::x0, x1), traced along the course as it's laid (false: not laid far enough yet)
    bool traceLaser(Laser& z) const;
    // she can't pass it short (an egg it cuts out): somewhere in its reach its row is at her body with no eggs, or she
    // drops down through it (into it from a higher wall), or she stacks up through it to climb the wall out of it (the
    // walls only: a floating block she can duck under, or climb)
    bool laserTakesAnEgg(Laser z) const;
    static constexpr double kLaserRange = 16.0; // a laser's reach at the most, metres
    // a cell of the course solid (a wall, a block, a barrier, a gauntlet's span: a beam stops there), x its middle
    bool solidAt(double x, int row) const;
    double generatedTo() const { return genX_; }
    void addLava(double x, int cells);   // lava from x, `cells` long, on the surface there
    void addSpring(double x, int power); // a spring at x (its middle) on the surface there, `power` rows of lift
    // Levels (Campaign.h): difficulty is read at offset + x * scale along the curve, and past `finish` (0 = endless)
    // only flat, wall-free runway is laid. The defaults give the endless daily course.
    void setDifficulty(double offset, double scale) { diffOffset_ = offset; diffScale_ = scale; }
    void setFinish(double x) { finish_ = x; }
    // super hard (LevelData's hard): the checker's human margins, scaled (CourseRules.h's marginScale; 1: as ever)
    void setHard(int hard) { hard_ = hard; }
    int hard() const { return hard_; }
    // The egg budget (on by default): every step up must be affordable from the egg meter of a player who stacks just
    // enough. The generator tracks that meter
    // (full at the start, refilling per metre at the curve's speed and refill) and, where a climb would need more eggs
    // than it holds, lengthens the run-up before it (or, past kMaxRunup, lowers the wall). The random stream is
    // consumed exactly as without it (off: the course as it was, for comparisons). Each saw costs an egg too.
    void setEggBudget(bool on) { eggBudget_ = on; }
    // Saws: `count` blades spread along the course up to the finish line (0, the default: none), alone or in rows of
    // up to `row` back to back (kSawRowGap apart: an egg sawn off at each). Each saw or row sits on its own flat
    // stretch (lengthened to fit): clear of barriers, past the landing from a drop and a reaction after it, with a
    // reaction's run after it before the next wall, one egg's refill from the last saw, and an egg per blade in the
    // budget. A course's first saw is alone, on the street when it can be. Placed from their own random stream, so the
    // walls are laid as without them (bar the stretches lengthened for them).
    void setSaws(int count, int row = 1) { sawCount_ = count; sawRow_ = std::max(1, row); }
    // Wall lasers and lava (0, the default: none): `count` of each spread along the course (clear of barriers, saws
    // and each other, a reaction's run after a climb or a drop and before the next wall). A laser is set into the face
    // of a drop, firing along the stretch below it: she falls down through its beam, so it takes an egg (in the
    // budget; she needs one at the edge); lava is 1 to 5 cells, an egg a cell in the budget. Placed from their own random stream, so the walls are
    // laid as without them (bar the stretches lengthened for them)
    void setLasers(int count) { laserCount_ = count; }
    void setLava(int count) { lavaCount_ = count; }
    // the disco balls the generator scatters (on by default; the editors' generator can leave them out)
    void setCorn(bool on) { cornOn_ = on; }
    // A maze gauntlet (Gauntlet.h) about `at` metres along (nullptr, the default: none). The course is brought to its
    // entry's height by fair steps, then a forecourt (a reaction's run), the gauntlet, and a porch at its exit's
    // height; its saws join the course's. No barrier, saw or corn on the forecourt or the porch.
    void setGauntlet(const Gauntlet* g, double at) { gauntletDef_ = g; gauntletAt_ = at; }
    const PlacedGauntlet* gauntletAt(double x) const {
        for (const PlacedGauntlet& p : gauntlets) if (x >= p.x0 && x < p.x1()) return &p;
        return nullptr;
    }
    // a bounce zone at x (Level::buildZones; none in a maze)
    const PlacedGauntlet* zoneAt(double x) const {
        for (const PlacedGauntlet& p : zones) if (x >= p.x0 && x < p.x1()) return &p;
        return nullptr;
    }
    // The bounce zones: each group of bouncer walls (bumpers and gates, kZoneGap cells apart at the most), from the
    // column before its first to its last, made an open-air Gauntlet of the course's cells there (its walls stone,
    // barriers and blocks too, saws on wall tops; its bumpers and gates the maze's), so she plays it by the maze's
    // rules (bounced back and forth, stacking to climb out) and the solver, the checker and the bot see it as one.
    // load makes them; a course with none has none
    void buildZones();
    static constexpr int kZoneGap = 40;
    // the floor under (x, y), metres: in a gauntlet, its highest solid cell at or below y; else the course's walls
    double groundBelow(double x, double y) const;
    static constexpr double kSawR = 0.5;            // blade radius, metres (its hub at the surface)
    static constexpr double kSawReach = kSawR + 0.3; // the stack meets it this far before its hub; done this far past
    static constexpr double kSawRowGap = 3 * 0.6;    // blades in a row: hub to hub (each egg's cut done before the next)
    static constexpr double kBudgetCap = 8.0;    // the egg meter's size (Game::MAXE)
    static constexpr int kLintel = 3;            // a gauntlet's low door: the barrier at the end of its forecourt
    static constexpr int kPorch = 8;             // the run at a gauntlet's exit height past it
    static constexpr double kBudgetMargin = 0.4; // eggs to spare at every climb (a tap's reaction, DDA slowing the refill)
    static constexpr int kMaxRunup = 40;         // units a run-up may be lengthened by (24 m) before the wall is lowered instead
    double finish() const { return finish_; }
    Curve curveAt(double x) const { return curve(diffOffset_ + std::max(0.0, x) * diffScale_); }
    int forecourtAt(double x) const; // a gauntlet's forecourt with its door at x, in cells (its lintel among them)
    double eggsPerCell(double x) const { return eggsPerUnit(x); } // a careful player's meter refilling at x: eggs a cell of run (the checker's, the editors' stairs)
    void dropBehind(double x) {
        while (!segs.empty() && segs.front().x1 < x - 14) segs.erase(segs.begin());
        while (!saws.empty() && saws.front().x < x - 14) saws.erase(saws.begin());
        while (!lasers.empty() && lasers.front().x < x - 14 - kLaserRange) lasers.erase(lasers.begin());
        while (!lavas.empty() && lavas.front().x1 < x - 14) lavas.erase(lavas.begin());
        while (!springs.empty() && springs.front().x < x - 14) springs.erase(springs.begin());
        while (!gauntlets.empty() && gauntlets.front().x1() < x - 14) gauntlets.erase(gauntlets.begin());
        while (!zones.empty() && zones.front().x1() < x - 14) zones.erase(zones.begin());
    }
    void rebuildBlocks();
    int heightAt(double x) const;
    // the surface she stands on at x with her feet at `feet` (metres): a floating block's top once she's up on it,
    // else the wall's (the street's) under it
    int floorAt(double x, double feet) const;

    std::vector<Segment> segs;
    std::vector<Corn> corns;
    std::vector<Saw> saws;      // in course order
    std::vector<Laser> lasers;  // in course order
    std::vector<Lava> lavas;    // in course order
    std::vector<Spring> springs; // in course order
    std::vector<Spring> gsprings; // a maze's spring cells' pads (gauntletHazards), in course order: drawn and squashed as the course's, flown by the maze's rules
    std::vector<PlacedGauntlet> gauntlets;
    std::vector<PlacedGauntlet> zones;      // bounce zones (buildZones), in course order
    // a gauntlet's cells to draw (rebuildBlocks): its solid cells (`open`: bit 0 air on its left, bit 1 on its right),
    // and its back wall behind every air cell
    struct GCell { glm::vec3 pos; Gauntlet::Cell kind; uint8_t open; };
    std::vector<GCell> gcells;
    std::vector<glm::vec3> gback;
    // ...and its inscriptions (a tomb's hieroglyphs, Hieroglyphs.h; GameGauntlet.cpp draws them): on its back wall, just
    // in front of it, a sign per cell in columns (kind 0), a star along the top of a tall room (1), the signs of a
    // cartouche (2) and its frame; rules between the columns, under the stars and along the blank dado (size: w, h)
    struct GSign { glm::vec3 pos; uint32_t cp; uint8_t kind; float seed; };
    struct GRule { glm::vec3 pos; glm::vec2 size; };
    struct GCartouche { glm::vec3 pos; glm::vec2 size; };
    std::vector<GSign> gsigns;
    std::vector<GRule> grules;
    std::vector<GCartouche> gcartouches;
    std::vector<Block> blocks;
    std::vector<Tile> tiles;
    std::vector<Girder> girders;
    std::vector<Curtain> curtains;

    static constexpr int TILE_X = 2, TILE_Z = 4;
    static constexpr float TW = Uf / TILE_X, TD = DEPTH / TILE_Z;

private:
    void pushSeg(int len, int h, WallKind kind);
    double eggsPerUnit(double x) const; // the egg budget: eggs refilled per unit (one egg's width) of run at x
    void creditRun(double units, double x) { budget_ = std::min(kBudgetCap, budget_ + units * eggsPerUnit(x)); }
    void spawnCorn(double x, double y);
    void placeSaw(int prevH, int prevCeil, double budgetAtStart);
    void placeGauntlet();
    void inscribe(const PlacedGauntlet& p); // (rebuildBlocks: its inscriptions)
    void affordClimb(int& h); // the egg budget before a step up to h: a longer run-up, or a lower step
    double sawTarget(int k);
    void placeLaserOrLava(int prevH, int prevCeil, double budgetAtStart); // (one due on the segment just laid)
    double hazardTarget(int k);
    void gauntletHazards(const PlacedGauntlet& p); // (its grid's saws, beams and lava, joining the course's)
    Mulberry32 rng_, sawRng_, hazRng_;
    int laserCount_ = 0, lasersPlaced_ = 0, lavaCount_ = 0, lavaPlaced_ = 0;
    bool cornOn_ = true;
    double lastHazX_ = -1e9, nextHazX_ = -1; // where the last laser or lava ended; where the next is due (-1: not drawn yet)
    double genX_ = 0, diffOffset_ = 0, diffScale_ = 1, finish_ = 0;
    int lastH_ = 0, segN_ = 0, genSector_ = 0;
    bool eggBudget_ = true;
    bool authored_ = false; // (load: the course is the level's data, not the generator's)
    double budget_ = kBudgetCap; // eggs in a careful player's meter at genX_
    int sawCount_ = 0, sawsPlaced_ = 0, sawRow_ = 1, nextSawRow_ = 1;
    const Gauntlet* gauntletDef_ = nullptr;
    double gauntletAt_ = 0;
    int hard_ = 0;
    std::vector<std::shared_ptr<const Gauntlet>> zoneGrids_; // (the zones' grids: shared by a copy of the level)
    bool gauntletPlaced_ = false;
    double gauntletEnd_ = 0; // where its porch ends, once placed
    double nextSawX_ = -1, lastSawX_ = -1e9; // where the next saw (or row) is due (-1: not drawn yet); the last blade placed
};

} // namespace cs
