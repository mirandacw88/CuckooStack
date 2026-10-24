#include "Chunk.h"

#include "Hazards.h"
#include "LevelBake.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cs {

namespace {

bool wholeNum(const std::string& s, int& out) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    out = std::stoi(s);
    return true;
}

bool plain(const LevelData::Wall& w) { return w.gauntlet.empty() && !w.ceil && !w.top && !bouncer(w.kind); }

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 60) return false;
    for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

double round3(double v) { return std::round(v * 1000) / 1000; }

} // namespace

bool Chunk::inRange(int level) const {
    for (const ChunkRange& r : ranges) if (level >= r.from && level <= r.to) return true;
    return false;
}

bool Chunk::canMirror(std::string* why) const {
    if (maze) {
        for (const std::string& r : rows)
            if (r.find('^') != std::string::npos) { if (why) *why = "a spring only launches her the way she runs"; return false; }
        return true;
    }
    for (const LevelData::Wall& w : body.course)
        if (bouncer(w.kind)) { if (why) *why = "a bumper or a gate depends on which way she runs"; return false; }
    for (const Placed& p : body.items)
        if (p.type == &kSpringHazard) { if (why) *why = "a spring only launches her rightwards"; return false; }
    return true;
}

int Chunk::entryHeight() const { return body.course.empty() ? 0 : body.course.front().h; }
int Chunk::exitHeight() const { return body.course.empty() ? 0 : body.course.back().h; }
int Chunk::runIn() const { return body.course.empty() ? 0 : body.course.front().cells; }
int Chunk::runOut() const { return body.course.empty() ? 0 : body.course.back().cells; }
double Chunk::reach() const {
    int cells = 0;
    for (const LevelData::Wall& w : body.course) cells += w.cells;
    return cells * U;
}

bool splitChunk(const std::string& id, const std::string& text, Chunk& out, std::string& body, std::string* error) {
    Chunk c;
    c.id = id;
    int ln = 0;
    auto fail = [&](const std::string& why) {
        if (error) *error = "chunk " + id + (ln ? ", line " + std::to_string(ln) : std::string()) + ": " + why;
        return false;
    };
    if (!validId(id)) return fail("its name: lower case letters, digits, '-' and '_'");
    bool hasKind = false, hasRanges = false;
    std::istringstream in(text);
    bool inBody = false;
    body.clear();
    for (std::string line; std::getline(in, line);) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (inBody) { body += line + "\n"; continue; }
        const size_t semi = line.find(';');
        if (semi != std::string::npos) {
            if (line.find_first_not_of(" \t") == semi) {
                std::string note = line.substr(semi + 1);
                if (!note.empty() && note[0] == ' ') note.erase(0, 1);
                c.notes.push_back(note);
                continue;
            }
            line.erase(semi);
        }
        std::istringstream words(line);
        std::vector<std::string> w;
        for (std::string t; words >> t;) w.push_back(t);
        if (w.empty()) continue;
        if (w[0] == "---" && w.size() == 1) { inBody = true; continue; }
        if (w[0] == "chunk") {
            if (w.size() != 2 || (w[1] != "course" && w[1] != "maze")) return fail("chunk: course or maze");
            c.maze = w[1] == "maze";
            hasKind = true;
        } else if (w[0] == "ranges") {
            if (w.size() < 2) return fail("ranges: one or more runs of levels, like 1-5 or 7");
            for (size_t i = 1; i < w.size(); ++i) {
                ChunkRange r;
                const size_t dash = w[i].find('-');
                if (dash == std::string::npos ? !wholeNum(w[i], r.from) : !(wholeNum(w[i].substr(0, dash), r.from) && wholeNum(w[i].substr(dash + 1), r.to)))
                    return fail("a range: a level or a run of levels, like 1-5 or 7");
                if (dash == std::string::npos) r.to = r.from;
                if (r.from < 1 || r.to < r.from || r.to > 9999) return fail("a range: from 1, and not backwards");
                c.ranges.push_back(r);
            }
            hasRanges = true;
        } else if (w[0] == "weight") {
            if (w.size() != 2 || !wholeNum(w[1], c.weight) || c.weight < 1 || c.weight > 10) return fail("weight: 1 to 10");
        } else if (w[0] == "uses") {
            if (w.size() != 2 || !(w[1] == "any" ? (c.uses = 0, true) : wholeNum(w[1], c.uses) && c.uses >= 1 && c.uses <= 99)) return fail("uses: how many a course holds (1 or more), or any");
        } else if (w[0] == "mirror") {
            if (w.size() != 2 || (w[1] != "yes" && w[1] != "no")) return fail("mirror: yes or no");
            c.mirror = w[1] == "yes";
        } else {
            return fail("what's '" + w[0] + "'? (chunk, ranges, weight, uses, mirror, then ---)");
        }
    }
    ln = 0;
    if (!hasKind) return fail("no 'chunk course' (or maze) line");
    if (!hasRanges) return fail("no ranges: the levels it may appear in");
    if (!inBody) return fail("no --- line before its body");
    out = std::move(c);
    return true;
}

std::string chunkBodyProblem(const Chunk& c, double* at) {
    if (at) *at = 0;
    if (c.maze) {
        if (c.rows.empty()) return "a maze chunk: its rows of cells after the ---";
        for (const std::string& r : c.rows) {
            if (r.size() != c.rows.front().size()) return "a maze chunk: every row as long as the first";
            if (r.find_first_not_of("#HBSC()~^.RL") != std::string::npos) return "a maze chunk holds cells of the maze (# H B S C ( ) ~ ^ . R L), no doors";
        }
        return "";
    }
    if (c.body.course.size() < 3) return "a course chunk: a run-in, something between, and a run-out";
    for (const LevelData::Wall& w : c.body.course)
        if (!w.gauntlet.empty()) return "a course chunk holds no gauntlet";
    if (!plain(c.body.course.front()) || !plain(c.body.course.back())) return "a course chunk starts with a flat run-in and ends with a flat run-out (plain walls: no barrier, block or bouncer)";
    // (the pads are stripped when it's placed, so nothing is set into them)
    const double from = c.runIn() * U, to = c.reach() - c.runOut() * U;
    for (const Placed& p : c.body.items) {
        const double end = p.x() + (p.type->spanParam >= 0 ? p.at(size_t(p.type->spanParam)) * U : 0);
        if (p.x() < from - 1e-6 || end > to + 1e-6) {
            if (at) *at = p.x();
            return "a hazard at " + levelNum(p.x()) + " m is in the run-in or the run-out: set hazards between them (the pads are stripped when the chunk is placed)";
        }
    }
    return "";
}

std::optional<Chunk> parseChunk(const std::string& id, const std::string& text, std::string* error) {
    Chunk c;
    std::string body;
    if (!splitChunk(id, text, c, body, error)) return std::nullopt;
    auto fail = [&](const std::string& why) -> std::optional<Chunk> {
        if (error) *error = "chunk " + id + ": " + why;
        return std::nullopt;
    };
    if (c.maze) {
        std::istringstream rows(body);
        for (std::string row; std::getline(rows, row);) {
            while (!row.empty() && (row.back() == ' ' || row.back() == '\t')) row.pop_back();
            if (!row.empty()) c.rows.push_back(row);
        }
        if (const std::string why = chunkBodyProblem(c); !why.empty()) return fail(why);
        return c;
    }
    std::string why;
    const auto d = parseLevel(c.ranges.front().from, body, &why);
    if (!d) return fail(why);
    c.body = *d;
    if (const std::string problem = chunkBodyProblem(c); !problem.empty()) return fail(problem);
    return c;
}

std::string chunkHeaderText(const Chunk& c) {
    std::string o;
    for (const std::string& n : c.notes) o += n.empty() ? ";\n" : "; " + n + "\n";
    o += c.maze ? "chunk maze\n" : "chunk course\n";
    o += "ranges";
    for (const ChunkRange& r : c.ranges) o += " " + std::to_string(r.from) + (r.to == r.from ? std::string() : "-" + std::to_string(r.to));
    o += "\nweight " + std::to_string(c.weight) + "\n";
    o += "uses " + (c.uses ? std::to_string(c.uses) : std::string("any")) + "\n";
    o += std::string("mirror ") + (c.mirror ? "yes" : "no") + "\n---\n";
    return o;
}

std::string chunkText(const Chunk& c) {
    std::string o = chunkHeaderText(c);
    if (c.maze) { for (const std::string& r : c.rows) o += r + "\n"; return o; }
    return o + levelText(c.body);
}

Chunk blankCourseChunk(const std::string& id, int first) {
    Chunk c;
    c.id = id;
    c.ranges = {{first, first}};
    c.uses = 0;
    c.body = bakeLevel(first, nullptr); // (its difficulty band: the rest is ours)
    c.body.number = first;
    c.body.notes.clear();
    c.body.items.clear();
    c.body.unknown.clear();
    c.body.raids = -1;
    c.body.hard = 0;
    LevelData::Wall pad, mid;
    pad.cells = 13;
    mid.cells = 10;
    mid.kind = WallKind::Hay;
    c.body.course = {pad, mid, pad};
    c.body.length = round3(c.reach());
    return c;
}

Chunk blankMazeChunk(const std::string& id, int first) {
    Chunk c;
    c.id = id;
    c.maze = true;
    c.ranges = {{first, first}};
    c.uses = 0;
    c.rows = {".........", ".........", ".........", ".........", "####S####"};
    return c;
}

char mirroredCell(char ch) {
    switch (ch) {
    case '(': return ')';
    case ')': return '(';
    case 'R': return 'L';
    case 'L': return 'R';
    default: return ch;
    }
}

Chunk mirrored(const Chunk& c) {
    Chunk m = c;
    if (c.maze) { // the columns reversed, a laser or a gate facing the other way
        for (std::string& r : m.rows) {
            std::reverse(r.begin(), r.end());
            for (char& ch : r) ch = mirroredCell(ch);
        }
        return m;
    }
    const double len = c.reach();
    std::reverse(m.body.course.begin(), m.body.course.end());
    for (Placed& p : m.body.items) {
        const double x = p.x();
        if (p.type == &kLavaHazard) p.v[0] = round3(len - (x + p.at(1) * U));
        else p.v[0] = round3(len - x);
        if (p.type == &kLaserHazard && p.v.size() > 2) p.v[2] = 1 - p.v[2];
    }
    std::stable_sort(m.body.items.begin(), m.body.items.end(), [](const Placed& a, const Placed& b) { return a.x() < b.x(); });
    return m;
}

bool chunkBefore(const Chunk& a, const Chunk& b) {
    const ChunkRange ra = a.ranges.empty() ? ChunkRange{} : a.ranges.front(), rb = b.ranges.empty() ? ChunkRange{} : b.ranges.front();
    const int ba = (ra.from - 1) / 5, bb = (rb.from - 1) / 5;
    if (ba != bb) return ba < bb;
    if (ra.from != rb.from) return ra.from < rb.from;
    if (ra.to != rb.to) return ra.to < rb.to;
    return a.id < b.id;
}

bool parseChunks(const std::string& text, std::vector<Chunk>& out, std::string* error) {
    bool ok = true;
    std::istringstream in(text);
    std::string id, body;
    bool have = false;
    auto flush = [&]() {
        if (!have) return;
        std::string why;
        if (auto c = parseChunk(id, body, &why)) out.push_back(std::move(*c));
        else { if (ok && error) *error = why; ok = false; }
    };
    for (std::string line; std::getline(in, line);) {
        if (line.compare(0, 4, "=== ") == 0) {
            flush();
            id = line.substr(4);
            while (!id.empty() && (id.back() == ' ' || id.back() == '\r')) id.pop_back();
            body.clear();
            have = true;
            continue;
        }
        if (have) body += line + "\n";
    }
    flush();
    return ok;
}

std::string chunksText(const std::vector<Chunk>& chunks) {
    std::string o;
    for (const Chunk& c : chunks) o += "=== " + c.id + "\n" + chunkText(c);
    return o;
}

std::string chunkUsesLabel(const std::vector<ChunkUse>& used) {
    std::vector<std::pair<std::string, int>> counts;
    for (const ChunkUse& u : used) {
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& p) { return p.first == u.id; });
        if (it == counts.end()) counts.push_back({u.id, 1});
        else ++it->second;
    }
    std::string o;
    for (const auto& p : counts) o += (o.empty() ? "" : ", ") + p.first + (p.second > 1 ? " \xC3\x97" + std::to_string(p.second) : std::string());
    return o;
}

void stampMazeChunk(const Chunk& chunk, int c0, int g0, int roof, const std::function<void(int, int, char)>& set) {
    if (chunk.rows.empty()) return;
    const int H = int(chunk.rows.size()), Wc = int(chunk.rows.front().size());
    for (int j = 0; j < H; ++j)
        for (int x = 0; x < Wc; ++x) set(c0 + x, g0 + (H - 1 - j), chunk.rows[size_t(j)][size_t(x)]);
    for (int x = 0; x < Wc; ++x) {
        const char top = chunk.rows.front()[size_t(x)];
        if ((top == '#' || top == 'H') && g0 + H <= roof)
            for (int r = g0 + H; r <= roof; ++r) set(c0 + x, r, '#');
    }
}

std::vector<std::string> chunkCorridor(const Chunk& chunk) {
    if (chunk.rows.empty()) return {};
    const int H = int(chunk.rows.size()), Wc = int(chunk.rows.front().size());
    const int air = std::max(7, H), W = Wc + 26, runIn = 12, R = air + 2;
    std::vector<std::string> cells(size_t(R), std::string(size_t(W), '#')); // (row 0 at the street)
    auto put = [&](int r0, int r1, int c0, int c1, char ch) {
        for (int r = std::max(0, r0); r <= std::min(R - 1, r1); ++r)
            for (int c = std::max(0, c0); c <= std::min(W - 1, c1); ++c) cells[size_t(r)][size_t(c)] = ch;
    };
    put(1, air, 1, W - 2, '.');
    for (const int x : {0, W - 1}) {
        put(1, 3, x, x, '.');
        put(4, R - 1, x, x, '#');
    }
    cells[1][0] = '>';
    cells[1][size_t(W - 1)] = 'X';
    stampMazeChunk(chunk, 1 + runIn, 0, air + 1, [&](int c, int r, char ch) { if (r >= 0 && r < R && c >= 0 && c < W) cells[size_t(r)][size_t(c)] = ch; });
    return {cells.rbegin(), cells.rend()};
}

std::string chunksLeftOutNote(const std::vector<std::string>& ids, const std::string& what, int level, bool fewer) {
    if (ids.empty()) return "";
    std::string list;
    for (size_t i = 0; i < ids.size(); ++i) list += (i ? ", " : "") + ids[i];
    const std::string at = " at level " + std::to_string(level);
    return fewer ? "Fewer of: " + list + " \xE2\x80\x94 no " + what + " passed with all of them" + at
                 : "Left out: " + list + " \xE2\x80\x94 no " + what + " passed with " + (ids.size() == 1 ? "it" : "them") + at;
}

} // namespace cs
