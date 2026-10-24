// Maze gauntlets (levels 11 on): enclosed sections of a level, authored as ASCII rows, one cell an egg (U) square.
// The hen runs through at full speed on her own and turns back only off bumpers; she climbs steps by stacking (the eggs
// below the step knocked off, as on any wall), drops down holes, and climbs shafts by stacking while she bounces between
// their walls. Stone at her body crashes her; so does a ceiling she stacks into. Saws work as on the run.
//
//   #  stone            H  a cyber cube (a block like stone, in the cubes' look)
//   B  bumper: bounces her back (climbable like stone when the stack gets her over it)
//   R  a one-way gate she passes running right (to her running left, a bumper); L  the same, running left
//   S  stone with a floor saw set in its top        C  a cyber cube with a cube saw set in its top
//   (  stone with a wall laser in its left face, firing left    )  the same in its right face, firing right
//      (along its row through the open cells out from its face, to the first solid one: its beam; it hits her once a
//      pass, the first time its row meets her: an egg there cut out, her body there cut in half; Level.h's Laser)
//   ~  lava: a floor that burns an egg from under her each cell she crosses on it (with none, her)
//   ^  a spring: a floor too (stone to everything else), a pad she launches off as she runs over its middle: up
//      kSpringPower rows and on, at kFlightSpeed (see Flight), no eggs laid in the air; she flies over what's in the air
//      and comes down on the first floor or ledge under her (a wall, a roof or a blade in her way ends her run; a bumper
//      or a gate against her bounces her mid-air). It can't land her on a blade, lava, a spring or past the exit
//   >  the entry, on the left edge                  X  the exit, on the right edge
//   .  air
//
// Gates make climbs one-way: through a gate into a shaft, she can't run back out of it, and bounces there till she's
// stacked over its far wall. A gate is never a floor (she drops through it).
// The grid sits on the street (row 0's floor is the street) and is closed: past its left edge is the entry's door
// (shut once she's in: a bumper), past its right edge the exit's porch (solid up to the exit's floor, open above it).
// The solver checks a layout the way the bot plays it: the exit reachable by stacking alone at a human tap rate (an egg
// every kLayGap cells crossed at most), by exactly one route through the maze, with no dead end that traps her; and it
// gives the bot its policy.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cs {

class Gauntlet {
public:
    enum class Cell : uint8_t { Air, Stone, Bumper, Saw, CubeSaw, Cube, GateR, GateL, LaserL, LaserR, Lava, Spring };
    static constexpr int kMaxEggs = 16;  // the most eggs the solver will stack (a hall 15 high, less her body)
    static constexpr int kBodyRows = 2;  // the hen's body (HEN_H = 1.05 m) fills two cells above her feet
    static constexpr int kLayGap = 4;    // cells crossed per egg laid, at the most: ~5 taps a second at full speed
    static constexpr int kSpringPower = 5;       // a spring's lift: rows to the top of the arc (rules::springLaunch)
    static constexpr double kFlightSpeed = 8.0;  // her speed along a spring's arc, m/s (the game's slowest run: the same everywhere,
                                                 // so the solver, the bot and the game fly the same arc whatever the level's speed)

    // rows top first, as written; false (and why in *error) if it isn't a closed, well-formed gauntlet. A line starting
    // ';' among them is a note and skipped, but for "; hard N" (0 to 10): the density it was made at (the generator's),
    // which the checklist bends its rules by (GauntletReport.h); none: 0
    static std::optional<Gauntlet> parse(std::string name, const std::vector<std::string>& rowsTopFirst, std::string* error = nullptr);
    // the density in a "hard N" note line (';' and a space first, or not), or -1 if it isn't one
    static int hardOf(const std::string& line);
    int hard() const { return hard_; }
    // a bounce zone on the course (Level::buildZones): its cells as given (row 0 at the bottom, cols x rows), open to
    // the sky (air over its top row) and with no doors of its own: she comes in at entryRow off the course before it
    // (past its left edge, as in a maze, the door shut behind her: a bumper) and leaves onto the course after it (its
    // porch at exitRow)
    static Gauntlet zone(std::string name, int cols, int rows, std::vector<Cell> cells, int entryRow, int exitRow);
    bool isZone() const { return zone_; }

    const std::string& name() const { return name_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int entryRow() const { return entryRow_; } // the row the hen's feet are in at the entry (its floor: row - 1's top)
    int entryTop() const { return entryTop_; } // the first row above the entry's opening (its lintel)
    int exitRow() const { return exitRow_; }
    Cell at(int c, int r) const;               // outside the grid: the door, the street, the porch (see above)
    // a gate in the way of her running way d (it bounces her like a bumper); a gate her way is open
    static bool gate(Cell k) { return k == Cell::GateR || k == Cell::GateL; }
    static bool gateBlocks(Cell k, int d) { return (k == Cell::GateR && d < 0) || (k == Cell::GateL && d > 0); }
    // in her way running d: a bumper or a gate against her bounces her; any other solid cell is a wall
    static bool open(Cell k) { return k == Cell::Air; }
    bool blocks(int c, int r, int d) const { const Cell k = at(c, r); return !open(k) && (!gate(k) || gateBlocks(k, d)); }
    bool bounces(int c, int r, int d) const { const Cell k = at(c, r); return k == Cell::Bumper || gateBlocks(k, d); }
    // solid to stand on or under (gates are neither)
    bool solid(int c, int r) const { const Cell k = at(c, r); return !open(k) && !gate(k); }
    // a laser's beam along the cell (its reach: a '(' or ')' firing through it), and any beam in the column
    bool beamAt(int c, int r) const { return c >= 0 && c < cols_ && r >= 0 && r < rows_ && beam_[size_t(r * cols_ + c)]; }
    bool beamColumn(int c) const { return c >= 0 && c < cols_ && beamCol_[size_t(c)]; }
    static bool laser(Cell k) { return k == Cell::LaserL || k == Cell::LaserR; }
    bool lavaAt(int c, int r) const { return at(c, r) == Cell::Lava; }
    bool springAt(int c, int r) const { return at(c, r) == Cell::Spring; }
    bool sawAt(int c, int r) const { const Cell k = at(c, r); return k == Cell::Saw || k == Cell::CubeSaw; }
    std::vector<std::string> rowsTopFirst() const;

    // ---- the solver: the hen as (column, the row her stack stands on, eggs, direction, cells before she can lay
    // again, zapped: a laser has hit her this pass, till she's in a column no beam reaches), moving a cell at a time
    struct State { int c = 0, b = 0, n = 0, d = 1, k = 0, z = 0; };
    enum class Move : uint8_t { Ok, Bounce, Crash, Exit };
    // what a move did: rows climbed (exact: with no egg to spare, a PERFECT), rows fallen, a saw (lastEgg: it took her
    // last one, a CLOSE SHAVE), and whether she ends it with her head brushing what's over her (a squeeze)
    // ...and the hazards: a laser that cut an egg out of her stack (cut), lava that burned one (lastEgg too when it was
    // the egg right under her or her last), a laser's beam just over her head as she came into its reach (brush: a
    // CLOSE CALL)
    struct StepInfo { int climbed = 0, fell = 0, cut = 0, endCol = 0; bool flew = false, exact = false, sawn = false, burned = false, lastEgg = false, squeeze = false, brush = false; };
    // she lays `lay` eggs (if she can yet, and her head fits) and moves a cell on: where she ends up and what happened
    // (Crash too for an egg she can't lay yet)
    Move step(const State& s, int lay, State& out, StepInfo* info = nullptr) const;
    // ---- a spring's flight: from the spring in column c (she stands on it, her stack's foot on row b, n eggs, running d),
    // her middle over the cell's middle at t = 0, flying at kFlightSpeed along d and up at rules::springLaunch(kSpringPower)
    // under rules::kGravity, as the game flies her (Game::updateGauntlet follows this plan). It is worked out at 240 steps a
    // second: a column her front edge comes into (kHb ahead of her middle) is looked at as the solver looks at it on the
    // ground (a bumper or a gate against her bounces her, any other wall in her way crashes her), and a solid cell
    // anywhere over her stack or body crashes her (a roof). She lands on the highest floor under her feet (either side of
    // her footprint) the arc brings her down to. Anything the game's saws, lasers and lava would do to her in the air
    // or where she lands is out, so it crashes her here: a blade within 0.9 rows of her feet and 1.5 cells along, a laser's
    // beam within a row of her, a landing on a blade, lava or another spring, her front edge past the exit (or past 4 s)
    // (zapped: a laser has hit her already as she came to the pad; its beams can't again while she's flying through the columns
    // beams cross. A beam within a row of her stack or head otherwise ends her run: the game's hit, or its brush)
    struct Flight {
        enum class End : uint8_t { Landed, Wall, Ceiling }; // Wall: her body into a wall (the game's die(false)); Ceiling: her stack or head (die(true))
        struct Bounce { double t = 0; int c = 0, r = 0; };
        End end = End::Wall;
        double seconds = 0;          // when it ends (since she left the spring)
        int col = 0, floor = 0, dir = 1; // Landed: her front edge's column, the row her feet stand on (the floor's top), her way
        bool zapped = false;         // Landed: a laser has hit her this pass still (she is in a column a beam crosses, as she was)
        int startCol = 0, startDir = 1, startRow = 0;
        std::vector<Bounce> bounces; // the walls that bounced her mid-air, in order
        double xAt(double t) const;   // her middle along the grid at t, in cells (the launch cell's middle: startCol + 0.5)
        double rowAt(double t) const; // her stack's foot at t, in rows (continuous)
        double vyAt(double t) const;  // her rise, m/s
    };
    Flight flightFrom(int c, int b, int n, int d, bool zapped = false) const;
    State start() const { return {0, entryRow_, startEggs_, 1, 0, 0}; }
    // a bounce zone she comes into with `eggs` stacked already (the course before it is open: she may): its solver,
    // its bot's way and its meter run from there (a maze's door: none)
    Gauntlet arriving(int eggs) const { Gauntlet g = *this; g.startEggs_ = std::clamp(eggs, 0, kMaxEggs); return g; }
    struct Solution {
        bool reachable = false;   // the exit, by stacking alone
        int routes = 0;           // 1: one way through (the bot's way covers every cell on a way to the exit); more:
                                  // cells of other ways
        int minEggs = -1;         // the fewest eggs laid on the way through
        int routeCells = 0, deadEndCells = 0; // cells on the way through; cells reachable but leading nowhere
        int softLocks = 0;        // places off the way through where, left alone, she'd bounce forever (must be 0:
                                  // a dead end has to end her run, not trap it)
        int moves = 0;            // cells the bot's way crosses (a measure of the gauntlet's length in time)
        // the near misses the bot's way can't avoid, as the game shows them: PERFECTs (climbs with no egg to spare),
        // CLOSE SHAVEs (a saw taking the last egg), CLOSE CALLs (a squeeze, her head brushing the roof, with neither)
        int perfects = 0, shaves = 0, squeezes = 0, nearMisses = 0;
        // where her feet can get to, cell by cell (row 0 at the bottom, as cells_): 0 nowhere she gets, 1 a dead end
        // (she gets there, and no further), 2 on a way through (the level editor shades them)
        std::vector<uint8_t> cellMap;
    };
    Solution solve() const;

    // the bot's policy: in state s, lay an egg now? (only meaningful on a way to the exit)
    bool shouldLay(const State& s) const;
    // the bot's way through: the states it passes, from the entry to the exit (empty if it doesn't get out)
    std::vector<State> botPath() const;
    // ...each of its moves: the state it starts from, the eggs laid, what happened (empty if it doesn't get out)
    struct TraceStep { State s; int lay = 0; Move move = Move::Ok; StepInfo info; };
    std::vector<TraceStep> botTrace() const;
    // ...played with a real egg meter, as the game's bot plays it (holding `atDoor` eggs at the door, one back every
    // cellsPerEgg cells crossed, cap at most; an egg laid only when there's one): its moves (empty if it doesn't get
    // out), and the cells it took to get out (0 if it doesn't)
    std::vector<TraceStep> botTraceWithMeter(double cellsPerEgg, double atDoor, int cap = 8) const;
    int botRunWithMeter(double cellsPerEgg, double atDoor, int cap = 8) const { return int(botTraceWithMeter(cellsPerEgg, atDoor, cap).size()); }
    // the near misses along a way (the bot's, with or without the meter) in order, as the game shows them: where they
    // come (the column she comes into, the row her stack stands on there; a CLOSE CALL where its squeeze ends)
    enum class NearMiss : uint8_t { Perfect, Shave, CloseCall };
    struct NearMissAt { int c = 0, b = 0; NearMiss kind = NearMiss::Perfect; int step = 0; }; // (step: the move it came in)
    static std::vector<NearMissAt> nearMisses(const std::vector<TraceStep>& way);
    // the same layout however it's written: the grid and its mirror image give the same fingerprint
    std::string fingerprint() const;

private:
    int index(const State& s) const {
        return ((((s.c * rows_ + s.b) * (kMaxEggs + 1) + s.n) * 2 + (s.d > 0 ? 1 : 0)) * kLayGap + s.k) * 2 + s.z;
    }
    State stateAt(int i) const;
    int stateCount() const { return cols_ * rows_ * (kMaxEggs + 1) * 2 * kLayGap * 2; }
    void computeCoReach() const;
    void traceBeams();
    std::string name_;
    bool zone_ = false;
    int hard_ = 0;
    int startEggs_ = 0;
    int cols_ = 0, rows_ = 0, entryRow_ = 0, entryTop_ = 0, exitRow_ = 0;
    std::vector<Cell> cells_; // cols x rows, row 0 at the bottom
    std::vector<uint8_t> beam_, beamCol_; // the lasers' beams, cell by cell (as cells_), and the columns they cross
    mutable std::vector<int> costToExit_; // the fewest moves (then eggs) to the exit from each state (-1: none);
                                          // lazily computed
};

// The gauntlets, by name (Gauntlets.cpp); nullptr if there's none by that name
const Gauntlet* gauntletNamed(const std::string& name);
std::vector<std::string> gauntletNames();
// a gauntlet's name: 1-32 lower case letters, digits and '_', a letter first ("sandy_maze")
bool validGauntletName(const std::string& name);
// its banner as she enters: "THE SANDY MAZE"
std::string gauntletTitle(const std::string& name);
// Developer tools (the level editor's play-test, gauntlet_check --game): a layout to play instead of the built-in one
// of that name, from now on (before a course is laid with it); false, and why in *error, if it doesn't parse
bool debugOverrideGauntlet(const std::string& name, const std::vector<std::string>& rowsTopFirst, std::string* error = nullptr);

} // namespace cs
