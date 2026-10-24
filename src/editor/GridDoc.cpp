#include "GridDoc.h"

#include "MazeGen.h"
#include "core/Chunk.h"
#include "core/Gauntlet.h"
#include "core/Json.h"

#include <algorithm>
#include <ctime>
#include <sstream>

namespace cs::ed {

namespace grid {

namespace {

bool air(char ch) { return ch == '.'; }
bool inside(const Rows& rows, int c, int i) { return i >= 0 && i < int(rows.size()) && c >= 0 && c < cols(rows); }

} // namespace

bool isDoor(char ch) { return ch == '>' || ch == 'X'; }
int cols(const Rows& rows) { return rows.empty() ? 0 : int(rows[0].size()); }

Rows setCell(const Rows& rows, int c, int i, char ch) {
    if (!inside(rows, c, i)) return rows;
    if (isDoor(ch)) return moveDoor(rows, ch, i);
    if (isDoor(rows[size_t(i)][size_t(c)]) || rows[size_t(i)][size_t(c)] == ch) return rows;
    Rows out = rows;
    out[size_t(i)][size_t(c)] = ch;
    return out;
}

Rows fillRect(const Rows& rows, int c0, int i0, int c1, int i1, char ch) {
    if (isDoor(ch) || rows.empty()) return rows;
    const int ca = std::max(0, std::min(c0, c1)), cb = std::min(cols(rows) - 1, std::max(c0, c1));
    const int ia = std::max(0, std::min(i0, i1)), ib = std::min(int(rows.size()) - 1, std::max(i0, i1));
    Rows out = rows;
    for (int i = ia; i <= ib; ++i)
        for (int c = ca; c <= cb; ++c)
            if (!isDoor(out[size_t(i)][size_t(c)])) out[size_t(i)][size_t(c)] = ch;
    return out;
}

// its old opening walled up, the new one as tall as the passage it opens onto (the air over that row, a column in)
Rows moveDoor(const Rows& rows, char door, int i) {
    const int n = cols(rows);
    if (n < 3 || i < 1 || i >= int(rows.size()) - 1) return rows;
    const size_t c = door == '>' ? 0 : size_t(n - 1), inner = door == '>' ? 1 : size_t(n - 2);
    Rows out = rows;
    for (std::string& row : out) row[c] = '#';
    out[size_t(i)][c] = door;
    for (int k = i - 1; k > 0 && out[size_t(k)][inner] == '.'; --k) out[size_t(k)][c] = '.';
    return out;
}

Rows duplicateCols(const Rows& rows, int c0, int c1, bool chunk) {
    const int n = cols(rows);
    if (n < (chunk ? 1 : 3)) return rows;
    auto in = [&](int c) { return chunk ? std::max(0, std::min(n - 1, c)) : std::max(1, std::min(n - 2, c)); };
    const int a = in(std::min(c0, c1)), b = in(std::max(c0, c1));
    Rows out;
    for (const std::string& row : rows) out.push_back(row.substr(0, size_t(b + 1)) + row.substr(size_t(a), size_t(b - a + 1)) + row.substr(size_t(b + 1)));
    return out;
}

Rows deleteCols(const Rows& rows, int c0, int c1, bool chunk) {
    const int n = cols(rows);
    const int a = std::max(chunk ? 0 : 1, std::min(c0, c1)), b = std::min(chunk ? n - 1 : n - 2, std::max(c0, c1));
    if (a > b || n - (b - a + 1) < (chunk ? 3 : 4)) return rows;
    Rows out;
    for (const std::string& row : rows) out.push_back(row.substr(0, size_t(a)) + row.substr(size_t(b + 1)));
    return out;
}

Rows duplicateRows(const Rows& rows, int i0, int i1) {
    if (rows.empty()) return rows;
    const int a = std::max(0, std::min(i0, i1)), b = std::min(int(rows.size()) - 1, std::max(i0, i1));
    Rows out(rows.begin(), rows.begin() + a);
    for (int i = a; i <= b; ++i) {
        std::string row = rows[size_t(i)];
        for (char& ch : row) if (isDoor(ch)) ch = '#';
        out.push_back(row);
    }
    out.insert(out.end(), rows.begin() + a, rows.end());
    return out;
}

Rows deleteRows(const Rows& rows, int i0, int i1, bool chunk) {
    const int a = std::max(0, std::min(i0, i1)), b = std::min(int(rows.size()) - 1, std::max(i0, i1));
    if (int(rows.size()) - (b - a + 1) < (chunk ? 3 : 4)) return rows;
    for (int i = a; i <= b; ++i)
        if (rows[size_t(i)].find_first_of(">X") != std::string::npos) return rows;
    Rows out(rows.begin(), rows.begin() + a);
    out.insert(out.end(), rows.begin() + b + 1, rows.end());
    return out;
}

// her feet in it and her body in the cell over it, both air, on a floor (stone, a cube, a bumper: a gate is no floor,
// a blade would get her); never a door's column. Clicked higher up: where she'd land, down the column
std::optional<int> standable(const Rows& rows, int c, int i) {
    const int n = cols(rows), h = int(rows.size());
    if (c < 1 || c > n - 2 || i < 1 || i >= h - 1 || !air(rows[size_t(i)][size_t(c)])) return std::nullopt;
    int k = i;
    while (k + 1 < h && air(rows[size_t(k + 1)][size_t(c)])) ++k;
    if (k + 1 >= h || std::string("#HB").find(rows[size_t(k + 1)][size_t(c)]) == std::string::npos || !air(rows[size_t(k - 1)][size_t(c)])) return std::nullopt;
    return k;
}

Rows templateRoom(int width, int height) {
    Rows out;
    for (int i = 0; i < height; ++i) {
        const int r = height - 1 - i;
        if (r == 0 || r == height - 1) { out.push_back(std::string(size_t(width), '#')); continue; }
        const bool open = r >= 1 && r <= 3;
        out.push_back(std::string(1, r == 1 ? '>' : open ? '.' : '#') + std::string(size_t(width - 2), '.') + (r == 1 ? 'X' : open ? '.' : '#'));
    }
    return out;
}

Rows templateChunk(int width, int height) {
    width = std::max(3, width);
    height = std::max(3, height);
    Rows out(size_t(height), std::string(size_t(width), '.'));
    out.back() = std::string(size_t(width), '#');
    return out;
}

std::string chunkProblem(const Rows& rows) {
    Chunk c;
    c.maze = true;
    c.rows = rows;
    return chunkBodyProblem(c);
}

std::string cellName(char ch) {
    switch (ch) {
    case '.': return "Air";
    case '#': return "Stone";
    case 'H': return "Cyber cube";
    case 'B': return "Bumper";
    case 'R': return "Gate \xE2\x86\x92";
    case 'L': return "Gate \xE2\x86\x90";
    case 'S': return "Saw";
    case 'C': return "Cube saw";
    case '(': return "Wall laser, firing left";
    case ')': return "Wall laser, firing right";
    case '~': return "Lava";
    case '^': return "Spring";
    case '>': return "Entry";
    case 'X': return "Exit";
    default: return std::string(1, ch);
    }
}

} // namespace grid

namespace {

std::string plural(int n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }

} // namespace

std::optional<GridDoc::State> GridDoc::read(const std::string& name, const std::string& text, std::string* error, bool chunk) {
    State s;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == ';') s.description.push_back(line.size() > 1 && line[1] == ' ' ? line.substr(2) : line.substr(1));
        else {
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
            if (!line.empty()) s.rows.push_back(line);
        }
    }
    if (chunk) {
        const std::string why = grid::chunkProblem(s.rows);
        if (!why.empty()) { if (error) *error = why; return std::nullopt; }
    } else if (!Gauntlet::parse(name, s.rows, error)) return std::nullopt;
    return s;
}

std::unique_ptr<GridDoc> GridDoc::open(const std::string& name, const std::string& text, std::string* error, bool chunk) {
    const std::optional<State> s = read(name, text, error, chunk);
    if (!s) return nullptr;
    std::unique_ptr<GridDoc> doc(new GridDoc());
    doc->chunk_ = chunk;
    doc->name_ = name;
    doc->rows_ = s->rows;
    doc->description_ = s->description;
    return doc;
}

Rows GridDoc::checkRows() const {
    if (!chunk_) return plainCheckRows();
    Chunk c;
    c.maze = true;
    c.rows = rows_;
    return chunkCorridor(c);
}

Rows GridDoc::flippedCheckRows() const {
    if (!mirrorCheck()) return {};
    Chunk c;
    c.maze = true;
    c.rows = rows_;
    return chunkCorridor(mirrored(c));
}

bool GridDoc::rename(const std::string& name) {
    if (chunk_) { // (a chunk's name is its file's: the chunk store's)
        if (name.empty() || name == name_) return false;
        name_ = name;
        changed();
        return true;
    }
    if (!validGauntletName(name)) { error_ = "a gauntlet's name: lower case letters, digits and _, a letter first"; return false; }
    if (name == name_) return false;
    name_ = name;
    changed();
    return true;
}

std::string GridDoc::text() const {
    std::string o;
    for (const std::string& line : description_) o += line.empty() ? ";\n" : "; " + line + "\n";
    for (const std::string& row : rows_) o += row + "\n";
    return o;
}

bool GridDoc::apply(const std::string& label, State next, int c, int i) {
    if (next == current()) return false;
    if (chunk_) { // (a block of the maze's cells that reads as a chunk, or not at all)
        error_ = grid::chunkProblem(next.rows);
        if (!error_.empty()) return false;
    } else if (!Gauntlet::parse(name_, next.rows, &error_)) return false; // (as the game will read it, or not at all)
    error_.clear();
    if (!gesture_ || !gestureEdited_) {
        undo_.push_back({gesture_ ? State{gestureBase_, description_} : current(), label, revision_, static_cast<long long>(std::time(nullptr)), c, i});
        if (undo_.size() > 300) undo_.erase(undo_.begin());
        redo_.clear();
        gestureEdited_ = gesture_;
    }
    rows_ = std::move(next.rows);
    description_ = std::move(next.description);
    changed();
    return true;
}

bool GridDoc::edit(const std::string& label, const std::function<Rows(const Rows&)>& change, int c, int i) {
    return apply(label, {change(rows_), description_}, c, i);
}

bool GridDoc::setDescription(const std::vector<std::string>& lines) {
    endGesture();
    return apply("Description", {rows_, lines}, -1, -1);
}

void GridDoc::beginGesture() {
    gesture_ = true;
    gestureEdited_ = false;
    gestureBase_ = rows_;
}

void GridDoc::restoreGestureBase() {
    if (!gesture_ || rows_ == gestureBase_) return;
    rows_ = gestureBase_;
    changed();
}

void GridDoc::endGesture() {
    if (gesture_ && gestureEdited_ && rows_ == gestureBase_ && !undo_.empty()) undo_.pop_back(); // (back where it began: no step)
    gesture_ = gestureEdited_ = false;
}

bool GridDoc::undo() {
    if (undo_.empty()) return false;
    endGesture();
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({current(), s.label, revision_, s.time, s.c, s.i});
    rows_ = std::move(s.state.rows);
    description_ = std::move(s.state.description);
    changed();
    return true;
}

bool GridDoc::redo() {
    if (redo_.empty()) return false;
    endGesture();
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({current(), s.label, revision_, s.time, s.c, s.i});
    rows_ = std::move(s.state.rows);
    description_ = std::move(s.state.description);
    changed();
    return true;
}

std::vector<GridDoc::Step> GridDoc::history() const {
    std::vector<Step> out;
    for (const Snapshot& s : undo_) out.push_back({s.label, s.revision, s.time, s.c, s.i});
    return out;
}

std::vector<GridDoc::Step> GridDoc::redoSteps() const {
    std::vector<Step> out;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) out.push_back({it->label, it->revision, it->time, it->c, it->i});
    return out;
}

bool GridDoc::goTo(size_t k) {
    bool moved = false;
    while (undo_.size() > k && undo()) moved = true;
    while (undo_.size() < k && redo()) moved = true;
    return moved;
}

bool GridDoc::restore(const std::string& text, const std::string& label) {
    std::optional<State> s = read(name_, text, &error_, chunk_);
    if (!s) return false;
    endGesture();
    return apply(label, std::move(*s), -1, -1);
}

std::string GridDoc::historyExport() const {
    auto textOf = [](const State& s) {
        std::string o;
        for (const std::string& line : s.description) o += line.empty() ? ";\n" : "; " + line + "\n";
        for (const std::string& row : s.rows) o += row + "\n";
        return o;
    };
    auto steps = [&](const std::vector<Snapshot>& v, bool reversed) {
        std::string o;
        auto one = [&](const Snapshot& s) {
            o += std::string(o.empty() ? "" : ",") + "{\"label\":\"" + jsonEsc(s.label) + "\",\"time\":" + std::to_string(s.time) + ",\"c\":" + std::to_string(s.c) +
                 ",\"i\":" + std::to_string(s.i) + ",\"text\":\"" + jsonEsc(textOf(s.state)) + "\"}";
        };
        if (reversed) for (auto it = v.rbegin(); it != v.rend(); ++it) one(*it);
        else for (const Snapshot& s : v) one(s);
        return "[" + o + "]";
    };
    return "{\"current\":\"" + jsonEsc(text()) + "\",\"undo\":" + steps(undo_, false) + ",\"redo\":" + steps(redo_, true) + "}";
}

void GridDoc::clearHistory() {
    undo_.clear();
    redo_.clear();
}

bool GridDoc::addHistory(bool redo, const std::string& label, long long time, int c, int i, const std::string& text) {
    std::optional<State> s = read(name_, text, &error_, chunk_);
    if (!s) return false;
    Snapshot snap{std::move(*s), label, revision_, time, c, i};
    if (redo) redo_.insert(redo_.begin(), std::move(snap)); // (given next first: the stack's top is its back)
    else undo_.push_back(std::move(snap));
    return true;
}

// ---- the tools

bool GridTools::pointer(Phase phase, int c, int i) {
    const Rows& rows = doc_.rows();
    if (rows.empty()) return false;
    const int n = grid::cols(rows), h = int(rows.size());
    c = std::clamp(c, 0, n - 1);
    i = std::clamp(i, 0, h - 1);
    auto row = [&](int k) { return std::to_string(h - 1 - k); }; // (as the game counts them: 0 at the street)
    if (phase == Phase::Cancel) {
        const bool was = doc_.inGesture() && doc_.rows() != doc_.gestureBase();
        doc_.restoreGestureBase();
        doc_.endGesture();
        preview_ = {};
        return was;
    }
    if (phase == Phase::Begin) {
        doc_.beginGesture();
        lastC_ = startC_ = c;
        lastI_ = startI_ = i;
        preview_ = {};
    }
    bool changed = false;
    const char ch = erase_ ? secondary_ : primary_;
    if (tool_ == "brush") {
        if (grid::isDoor(ch)) {
            if (phase != Phase::End) changed = doc_.edit(grid::cellName(ch) + " to row " + row(i), [&](const Rows& r) { return grid::moveDoor(r, ch, i); }, c, i);
        } else {
            // the cells between the last one and this (Bresenham: a quick stroke leaves no gaps)
            int x0 = lastC_, y0 = lastI_;
            const int dx = std::abs(c - x0), dy = -std::abs(i - y0), sx = x0 < c ? 1 : -1, sy = y0 < i ? 1 : -1;
            std::vector<std::pair<int, int>> cells;
            for (int err = dx + dy;;) {
                cells.push_back({x0, y0});
                if (x0 == c && y0 == i) break;
                const int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
            changed = doc_.edit(grid::cellName(ch) + " at col " + std::to_string(startC_) + ", row " + row(startI_), [&](const Rows& r) {
                Rows out = r;
                for (const auto& [x, y] : cells) out = grid::setCell(out, x, y, ch);
                return out;
            }, startC_, startI_);
        }
    } else if (tool_ == "box") {
        preview_ = {"box", std::min(startC_, c), std::min(startI_, i), std::max(startC_, c), std::max(startI_, i), erase_};
        if (phase == Phase::End) {
            const Preview p = preview_;
            changed = doc_.edit(grid::cellName(ch) + ", " + std::to_string(p.c1 - p.c0 + 1) + " \xC3\x97 " + std::to_string(p.i1 - p.i0 + 1) + " at col " + std::to_string(p.c0),
                                [&](const Rows& r) { return grid::fillRect(r, p.c0, p.i0, p.c1, p.i1, ch); }, p.c0, p.i0);
        }
    } else if (tool_ == "door") {
        const char door = startC_ < n / 2 ? '>' : 'X';
        if (phase != Phase::End) changed = doc_.edit(grid::cellName(door) + " to row " + row(i), [&](const Rows& r) { return grid::moveDoor(r, door, i); }, door == '>' ? 0 : n - 1, i);
    } else if (tool_ == "cols") {
        preview_ = {"cols", std::min(startC_, c), 0, std::max(startC_, c), h - 1, erase_};
        if (phase == Phase::End) {
            const int a = preview_.c0, b = preview_.c1;
            changed = doc_.edit((erase_ ? "Took out " : "Copied in ") + plural(b - a + 1, "column") + " at col " + std::to_string(a),
                                [&](const Rows& r) { return erase_ ? grid::deleteCols(r, a, b, doc_.chunk()) : grid::duplicateCols(r, a, b, doc_.chunk()); }, a, -1);
        }
    } else if (tool_ == "rows") {
        preview_ = {"rows", 0, std::min(startI_, i), n - 1, std::max(startI_, i), erase_};
        if (phase == Phase::End) {
            const int a = preview_.i0, b = preview_.i1;
            changed = doc_.edit((erase_ ? "Took out " : "Copied in ") + plural(b - a + 1, "row") + " at row " + row(b),
                                [&](const Rows& r) { return erase_ ? grid::deleteRows(r, a, b, doc_.chunk()) : grid::duplicateRows(r, a, b); }, -1, b);
        }
    } else if (tool_ == "start") {
        if (erase_) doc_.start.reset();
        else if (const std::optional<int> k = grid::standable(rows, c, i)) doc_.start = std::make_pair(c, *k);
    }
    lastC_ = c;
    lastI_ = i;
    if (phase == Phase::End) {
        doc_.endGesture();
        preview_ = {};
    }
    return changed;
}

} // namespace cs::ed
