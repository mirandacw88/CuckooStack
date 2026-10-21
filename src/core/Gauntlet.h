// Maze gauntlets (levels 11 on): enclosed sections of a level, authored as ASCII rows, one cell an egg (U) square.
// The hen runs through at full speed on her own and turns back only off bumpers; she climbs steps by stacking (the eggs
// below the step knocked off, as on any wall), drops down holes, and climbs shafts by stacking while she bounces between
// their walls. Stone at her body crashes her; so does a ceiling she stacks into. Saws work as on the run.
//
//   #  stone            H  a cyber cube (a block like stone, in the cubes' look)
//   B  bumper: bounces her back (climbable like stone when the stack gets her over it)
//   R  a one-way gate she passes running right (to her running left, a bumper); L  the same, running left
//   S  stone with a floor saw set in its top        C  a cyber cube with a cube saw set in its top
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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cs {

class Gauntlet {
public:
    enum class Cell : uint8_t { Air, Stone, Bumper, Saw, CubeSaw, Cube, GateR, GateL };
    static constexpr int kMaxEggs = 16;  // the most eggs the solver will stack (a hall 15 high, less her body)
    static constexpr int kBodyRows = 2;  // the hen's body (HEN_H = 1.05 m) fills two cells above her feet
    static constexpr int kLayGap = 4;    // cells crossed per egg laid, at the most: ~5 taps a second at full speed

    // rows top first, as written; false (and why in *error) if it isn't a closed, well-formed gauntlet
    static std::optional<Gauntlet> parse(std::string name, const std::vector<std::string>& rowsTopFirst, std::string* error = nullptr);

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
    bool blocks(int c, int r, int d) const { const Cell k = at(c, r); return k != Cell::Air && (!gate(k) || gateBlocks(k, d)); }
    bool bounces(int c, int r, int d) const { const Cell k = at(c, r); return k == Cell::Bumper || gateBlocks(k, d); }
    // solid to stand on or under (gates are neither)
    bool solid(int c, int r) const { const Cell k = at(c, r); return k != Cell::Air && !gate(k); }
    bool sawAt(int c, int r) const { const Cell k = at(c, r); return k == Cell::Saw || k == Cell::CubeSaw; }
    std::vector<std::string> rowsTopFirst() const;

    // ---- the solver: the hen as (column, the row her stack stands on, eggs, direction, cells before she can lay
    // again), moving a cell at a time
    struct State { int c = 0, b = 0, n = 0, d = 1, k = 0; };
    enum class Move : uint8_t { Ok, Bounce, Crash, Exit };
    // what a move did: rows climbed (exact: with no egg to spare, a PERFECT), rows fallen, a saw (lastEgg: it took her
    // last one, a CLOSE SHAVE), and whether she ends it with her head brushing what's over her (a squeeze)
    struct StepInfo { int climbed = 0, fell = 0; bool exact = false, sawn = false, lastEgg = false, squeeze = false; };
    // she lays `lay` eggs (if she can yet, and her head fits) and moves a cell on: where she ends up and what happened
    // (Crash too for an egg she can't lay yet)
    Move step(const State& s, int lay, State& out, StepInfo* info = nullptr) const;
    State start() const { return {0, entryRow_, 0, 1, 0}; }
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
        return (((s.c * rows_ + s.b) * (kMaxEggs + 1) + s.n) * 2 + (s.d > 0 ? 1 : 0)) * kLayGap + s.k;
    }
    State stateAt(int i) const;
    int stateCount() const { return cols_ * rows_ * (kMaxEggs + 1) * 2 * kLayGap; }
    void computeCoReach() const;
    std::string name_;
    int cols_ = 0, rows_ = 0, entryRow_ = 0, entryTop_ = 0, exitRow_ = 0;
    std::vector<Cell> cells_; // cols x rows, row 0 at the bottom
    mutable std::vector<int> costToExit_; // the fewest moves (then eggs) to the exit from each state (-1: none);
                                          // lazily computed
};

// The gauntlets, by name (Gauntlets.cpp); nullptr if there's none by that name
const Gauntlet* gauntletNamed(const std::string& name);
std::vector<std::string> gauntletNames();
// Developer tools (the level editor's play-test, gauntlet_check --game): a layout to play instead of the built-in one
// of that name, from now on (before a course is laid with it); false, and why in *error, if it doesn't parse
bool debugOverrideGauntlet(const std::string& name, const std::vector<std::string>& rowsTopFirst, std::string* error = nullptr);

} // namespace cs
