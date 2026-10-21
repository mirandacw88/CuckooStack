#include "Gauntlet.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <limits>
#include <queue>

namespace cs {

std::optional<Gauntlet> Gauntlet::parse(std::string name, const std::vector<std::string>& rowsTopFirst, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<Gauntlet> {
        if (error) *error = name + ": " + why;
        return std::nullopt;
    };
    if (rowsTopFirst.size() < 4) return fail("too few rows");
    Gauntlet g;
    g.name_ = name;
    g.rows_ = int(rowsTopFirst.size());
    g.cols_ = int(rowsTopFirst.front().size());
    if (g.cols_ < 4) return fail("too few columns");
    g.cells_.assign(size_t(g.cols_ * g.rows_), Cell::Air);
    int entries = 0, exits = 0;
    for (int i = 0; i < g.rows_; ++i) {
        const std::string& row = rowsTopFirst[size_t(i)];
        if (int(row.size()) != g.cols_) return fail("row " + std::to_string(i) + " is " + std::to_string(row.size()) + " wide, not " + std::to_string(g.cols_));
        const int r = g.rows_ - 1 - i;
        for (int c = 0; c < g.cols_; ++c) {
            Cell k = Cell::Air;
            switch (row[size_t(c)]) {
            case '#': k = Cell::Stone; break;
            case 'H': k = Cell::Cube; break;
            case 'B': k = Cell::Bumper; break;
            case 'R': k = Cell::GateR; break;
            case 'L': k = Cell::GateL; break;
            case 'S': k = Cell::Saw; break;
            case 'C': k = Cell::CubeSaw; break;
            case '.': break;
            case '>':
                if (c != 0) return fail("the entry '>' must be on the left edge");
                g.entryRow_ = r; ++entries;
                break;
            case 'X':
                if (c != g.cols_ - 1) return fail("the exit 'X' must be on the right edge");
                g.exitRow_ = r; ++exits;
                break;
            default: return fail(std::string("unknown cell '") + row[size_t(c)] + "'");
            }
            g.cells_[size_t(r * g.cols_ + c)] = k;
        }
    }
    if (entries != 1 || exits != 1) return fail("needs exactly one entry '>' and one exit 'X'");
    // closed: the top row and both edges solid but for the doors, each door tall enough for the hen
    for (int c = 0; c < g.cols_; ++c)
        if (g.cells_[size_t((g.rows_ - 1) * g.cols_ + c)] == Cell::Air) return fail("the top row must be solid");
    auto door = [&](int c, int row, const char* what) -> std::string {
        if (row > 0 && !g.solid(c, row - 1)) return std::string(what) + " has no floor";
        for (int r = row; r < row + kBodyRows; ++r)
            if (r >= g.rows_ || g.cells_[size_t(r * g.cols_ + c)] != Cell::Air) return std::string(what) + " is too low for the hen";
        int top = row; // (the door's opening: its rows up from its floor while air)
        while (top < g.rows_ && g.cells_[size_t(top * g.cols_ + c)] == Cell::Air) ++top;
        for (int r = 0; r < g.rows_; ++r)
            if (g.cells_[size_t(r * g.cols_ + c)] == Cell::Air && (r < row || r >= top)) return std::string(what) + "'s wall is open at row " + std::to_string(r);
        return "";
    };
    if (const std::string e = door(0, g.entryRow_, "the entry"); !e.empty()) return fail(e);
    g.entryTop_ = g.entryRow_;
    while (g.entryTop_ < g.rows_ && g.cells_[size_t(g.entryTop_ * g.cols_)] == Cell::Air) ++g.entryTop_;
    if (const std::string e = door(g.cols_ - 1, g.exitRow_, "the exit"); !e.empty()) return fail(e);
    return g;
}

Gauntlet::Cell Gauntlet::at(int c, int r) const {
    if (r < 0) return Cell::Stone;                                    // the street
    if (c < 0) return Cell::Bumper;                                   // the entry's door, shut behind her
    if (c >= cols_) return r < exitRow_ ? Cell::Stone : Cell::Air;    // the porch past the exit
    if (r >= rows_) return Cell::Stone;
    return cells_[size_t(r * cols_ + c)];
}

std::vector<std::string> Gauntlet::rowsTopFirst() const {
    std::vector<std::string> out;
    for (int r = rows_ - 1; r >= 0; --r) {
        std::string row;
        for (int c = 0; c < cols_; ++c) {
            if (c == 0 && r == entryRow_) { row += '>'; continue; }
            if (c == cols_ - 1 && r == exitRow_) { row += 'X'; continue; }
            switch (at(c, r)) {
            case Cell::Air: row += '.'; break;
            case Cell::Stone: row += '#'; break;
            case Cell::Cube: row += 'H'; break;
            case Cell::Bumper: row += 'B'; break;
            case Cell::GateR: row += 'R'; break;
            case Cell::GateL: row += 'L'; break;
            case Cell::Saw: row += 'S'; break;
            case Cell::CubeSaw: row += 'C'; break;
            }
        }
        out.push_back(row);
    }
    return out;
}

// One cell on, by the game's rules (Game::updateGauntlet): an egg laid only every kLayGap cells, and only where her
// head fits (stacking into a ceiling crashes her); in the cell ahead, anything in her way at her body bounces her (a
// bumper, a gate against her) or crashes her (any other wall); anything in her way at her stack is a step she climbs
// (it must rise from her floor; the eggs below its top knocked off); then she drops down any hole straight to the
// floor below (through gates: they're no floor), and a saw in the floor she's on takes an egg (with none, her)
Gauntlet::Move Gauntlet::step(const State& s, int lay, State& out, StepInfo* info) const {
    StepInfo scratch;
    StepInfo& in = info ? *info : scratch;
    in = {};
    if (lay && s.k > 0) return Move::Crash; // (too soon after the last egg)
    State t = s;
    t.n += lay;
    if (t.n > kMaxEggs) return Move::Crash;
    for (int r = t.b; r < t.b + t.n + kBodyRows; ++r)
        if (solid(t.c, r)) return Move::Crash;
    t.k = lay ? kLayGap - 1 : std::max(0, s.k - 1);
    const int a = t.c + t.d;
    const int feet = t.b + t.n;
    for (int r = feet; r < feet + kBodyRows; ++r)
        if (blocks(a, r, t.d)) {
            if (!bounces(a, r, t.d)) return Move::Crash;
            t.d = -t.d;
            out = t;
            return Move::Bounce;
        }
    int top = t.b;
    while (top < feet && blocks(a, top, t.d)) ++top;
    for (int r = top; r < feet; ++r)
        if (blocks(a, r, t.d)) return Move::Crash; // (something in the way of her stack that doesn't rise from her floor)
    in.climbed = top - t.b;
    in.exact = in.climbed > 0 && t.n == in.climbed;
    t.n -= top - t.b;
    t.b = top;
    t.c = a;
    if (a >= cols_) { out = t; return Move::Exit; }
    const int before = t.b;
    while (!solid(a, t.b - 1)) --t.b;
    in.fell = before - t.b;
    if (sawAt(a, t.b - 1)) {
        if (t.n == 0) return Move::Crash;
        --t.n;
        in.sawn = true;
        in.lastEgg = t.n == 0;
    }
    in.squeeze = solid(a, t.b + t.n + in.sawn + kBodyRows); // (as she came into the cell: the blade's egg was still under her)
    out = t;
    return Move::Ok;
}

Gauntlet::State Gauntlet::stateAt(int i) const {
    State s;
    s.k = i % kLayGap; i /= kLayGap;
    s.d = i % 2 ? 1 : -1; i /= 2;
    s.n = i % (kMaxEggs + 1); i /= kMaxEggs + 1;
    s.b = i % rows_;
    s.c = i / rows_;
    return s;
}

namespace {
constexpr int kMoveCost = 1000; // the solver's cost: moves first, then eggs laid
constexpr int kNone = std::numeric_limits<int>::max();
constexpr int kToCrash = -1, kToExit = -2;
} // namespace

void Gauntlet::computeCoReach() const {
    if (!costToExit_.empty()) return;
    const int n = stateCount();
    // every state's two moves (lay or not): the state they lead to, or the crash or the exit
    std::vector<int> succ(size_t(n) * 2);
    std::vector<int> inDeg(size_t(n) + 1, 0);
    for (int i = 0; i < n; ++i)
        for (int lay = 0; lay <= 1; ++lay) {
            State o;
            const Move m = step(stateAt(i), lay, o);
            const int j = m == Move::Crash ? kToCrash : m == Move::Exit ? kToExit : index(o);
            succ[size_t(i) * 2 + size_t(lay)] = j;
            if (j >= 0) ++inDeg[size_t(j)];
        }
    // the moves reversed (to the state, the ones leading to it), then the cost to the exit by Dijkstra (moves, then eggs)
    std::vector<int> start(size_t(n) + 1, 0);
    for (int i = 0; i < n; ++i) start[size_t(i) + 1] = start[size_t(i)] + inDeg[size_t(i)];
    std::vector<int> from(static_cast<size_t>(start[size_t(n)])), fill(start.begin(), start.end() - 1);
    for (int i = 0; i < n; ++i)
        for (int lay = 0; lay <= 1; ++lay)
            if (const int j = succ[size_t(i) * 2 + size_t(lay)]; j >= 0) from[size_t(fill[size_t(j)]++)] = i * 2 + lay;
    std::vector<int> cost(size_t(n), kNone);
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> q;
    for (int i = 0; i < n; ++i)
        for (int lay = 0; lay <= 1; ++lay)
            if (succ[size_t(i) * 2 + size_t(lay)] == kToExit && kMoveCost + lay < cost[size_t(i)]) { cost[size_t(i)] = kMoveCost + lay; q.push({cost[size_t(i)], i}); }
    while (!q.empty()) {
        const auto [k, i] = q.top();
        q.pop();
        if (k > cost[size_t(i)]) continue;
        for (int e = start[size_t(i)]; e < start[size_t(i) + 1]; ++e) {
            const int f = from[size_t(e)] / 2, lay = from[size_t(e)] % 2;
            if (k + kMoveCost + lay < cost[size_t(f)]) { cost[size_t(f)] = k + kMoveCost + lay; q.push({cost[size_t(f)], f}); }
        }
    }
    costToExit_ = std::move(cost);
}

bool Gauntlet::shouldLay(const State& s) const {
    computeCoReach();
    auto via = [&](int lay) {
        State o;
        const Move m = step(s, lay, o);
        if (m == Move::Crash) return kNone;
        if (m == Move::Exit) return kMoveCost + lay;
        const int k = costToExit_[size_t(index(o))];
        return k == kNone ? kNone : k + kMoveCost + lay;
    };
    const int now = via(1), later = via(0);
    return now != kNone && now <= later; // (an egg that's needed anyway: lay it early)
}

std::vector<Gauntlet::TraceStep> Gauntlet::botTrace() const {
    std::vector<TraceStep> trace;
    State s = start();
    for (int moves = 0; moves < 50 * cols_ * rows_; ++moves) {
        TraceStep t{s, shouldLay(s) ? 1 : 0, Move::Ok, {}};
        State o;
        t.move = step(s, t.lay, o, &t.info);
        if (t.move == Move::Crash) return {};
        trace.push_back(t);
        if (t.move == Move::Exit) return trace;
        s = o;
    }
    return {};
}

std::vector<Gauntlet::State> Gauntlet::botPath() const {
    const std::vector<TraceStep> trace = botTrace();
    if (trace.empty()) return {};
    std::vector<State> path;
    for (const TraceStep& t : trace) path.push_back(t.s);
    return path;
}

std::vector<Gauntlet::TraceStep> Gauntlet::botTraceWithMeter(double cellsPerEgg, double atDoor, int cap) const {
    std::vector<TraceStep> trace;
    State s = start();
    double meter = atDoor;
    for (int moves = 0; moves < 50 * cols_ * rows_; ++moves) {
        meter = std::min(double(cap), meter + 1.0 / cellsPerEgg);
        TraceStep t{s, shouldLay(s) && meter >= 1 ? 1 : 0, Move::Ok, {}};
        meter -= t.lay;
        State o;
        t.move = step(s, t.lay, o, &t.info);
        if (t.move == Move::Crash) return {};
        trace.push_back(t);
        if (t.move == Move::Exit) return trace;
        s = o;
    }
    return {};
}

Gauntlet::Solution Gauntlet::solve() const {
    computeCoReach();
    Solution out;
    const int n = stateCount();
    // everywhere she can get to (with any stacking)
    std::vector<uint8_t> seen(size_t(n), 0);
    std::deque<int> q{index(start())};
    seen[size_t(q.front())] = 1;
    bool exit = false;
    while (!q.empty()) {
        const State s = stateAt(q.front());
        q.pop_front();
        for (int lay = 0; lay <= 1; ++lay) {
            State o;
            const Move m = step(s, lay, o);
            if (m == Move::Crash) continue;
            if (m == Move::Exit) { exit = true; continue; }
            if (!seen[size_t(index(o))]) { seen[size_t(index(o))] = 1; q.push_back(index(o)); }
        }
    }
    out.reachable = exit;
    if (exit) out.minEggs = costToExit_[size_t(index(start()))] % kMoveCost;
    // the cells of the way(s) through, and of what's reachable but leads nowhere
    std::vector<uint8_t> route(size_t(cols_ * rows_), 0), any(size_t(cols_ * rows_), 0);
    for (int i = 0; i < n; ++i) {
        if (!seen[size_t(i)]) continue;
        const State s = stateAt(i);
        any[size_t(s.b * cols_ + s.c)] = 1;
        if (costToExit_[size_t(i)] != kNone) route[size_t(s.b * cols_ + s.c)] = 1;
    }
    out.cellMap.assign(route.size(), 0);
    for (size_t i = 0; i < route.size(); ++i) {
        out.routeCells += route[i];
        out.deadEndCells += any[i] && !route[i];
        out.cellMap[i] = route[i] ? 2 : any[i] ? 1 : 0;
    }
    // off the way through, left alone (no eggs laid) she must come to an end, not bounce about forever: follow each
    // state's move without an egg; going round again without ever getting back on a way to the exit is a trap
    std::vector<uint8_t> mark(size_t(n), 0); // 0 unseen, 1 on the walk now, 2 ends (crash, exit, back on a way), 3 trapped
    for (int i0 = 0; i0 < n; ++i0) {
        if (!seen[size_t(i0)] || costToExit_[size_t(i0)] != kNone || mark[size_t(i0)]) continue;
        std::vector<int> walk;
        int i = i0;
        uint8_t outcome = 2;
        while (true) {
            if (mark[size_t(i)] == 1) { outcome = 3; break; }
            if (mark[size_t(i)] >= 2) { outcome = mark[size_t(i)]; break; }
            if (costToExit_[size_t(i)] != kNone) { outcome = 2; break; }
            mark[size_t(i)] = 1;
            walk.push_back(i);
            State o;
            const Move m = step(stateAt(i), 0, o);
            if (m == Move::Crash || m == Move::Exit) { outcome = 2; break; }
            i = index(o);
        }
        for (int w : walk) mark[size_t(w)] = outcome;
        if (outcome == 3) out.softLocks += int(walk.size());
    }
    if (!exit) return out;
    for (const NearMissAt& m : nearMisses(botTrace())) {
        out.perfects += m.kind == NearMiss::Perfect;
        out.shaves += m.kind == NearMiss::Shave;
        out.squeezes += m.kind == NearMiss::CloseCall;
    }
    out.nearMisses = out.perfects + out.shaves + out.squeezes;
    // one way through: the bot's way covers every cell of every way to the exit
    const std::vector<State> path = botPath();
    out.moves = int(path.size());
    std::vector<uint8_t> onPath(size_t(cols_ * rows_), 0);
    for (const State& s : path) onPath[size_t(s.b * cols_ + s.c)] = 1;
    out.routes = path.empty() ? 0 : 1;
    for (size_t i = 0; i < route.size(); ++i) out.routes += route[i] && !onPath[i];
    return out;
}

// The near misses along a way, as the game shows them: each PERFECT and CLOSE SHAVE, and each squeeze (her head
// brushing the roof two columns or more running, bounces not counted, ended by room or a drop) in which neither
// happened: a CLOSE CALL (Game::endSqueeze). A near miss in the move that ends a squeeze is the squeeze's (the game's
// climb comes before it looks over her head; its blade finishes the cut a cell on); one where she lands from a drop
// isn't (the drop ended the squeeze first), and the column she lands in starts none (the game looks as she comes into
// a column)
std::vector<Gauntlet::NearMissAt> Gauntlet::nearMisses(const std::vector<TraceStep>& way) {
    std::vector<NearMissAt> out;
    int run = 0, i = 0;
    bool eventful = false;
    State last;
    auto end = [&] { // (shown as she has room again: this move)
        if (run >= 2 && !eventful) out.push_back({last.c, last.b, NearMiss::CloseCall, std::min(i, int(way.size()) - 1)});
        run = 0;
        eventful = false;
    };
    for (; i < int(way.size()); ++i) {
        const TraceStep& t = way[size_t(i)];
        if (t.move == Move::Bounce) continue;
        const int c = t.s.c + t.s.d;
        if (t.info.exact) out.push_back({c, t.s.b + t.info.climbed, NearMiss::Perfect, i});
        if (t.info.lastEgg) out.push_back({c, t.s.b + t.info.climbed - t.info.fell, NearMiss::Shave, i});
        eventful = eventful || t.info.exact;
        if (t.info.fell) { end(); continue; }
        eventful = eventful || t.info.lastEgg;
        if (t.info.squeeze && t.move == Move::Ok) { ++run; last = {c, t.s.b + t.info.climbed}; continue; }
        end();
    }
    end();
    return out;
}

std::string Gauntlet::fingerprint() const {
    std::string a, m;
    for (const std::string& row : rowsTopFirst()) {
        a += row + '/';
        std::string r(row.rbegin(), row.rend());
        for (char& ch : r) ch = ch == '>' ? 'X' : ch == 'X' ? '>' : ch == 'R' ? 'L' : ch == 'L' ? 'R' : ch;
        m += r + '/';
    }
    return std::min(a, m);
}

} // namespace cs
