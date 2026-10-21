#include "LevelData.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace cs {

double LevelData::courseLength() const {
    int cells = 0;
    for (const Wall& w : course) {
        const Gauntlet* g = w.gauntlet.empty() ? nullptr : gauntletNamed(w.gauntlet);
        cells += g ? g->cols() : w.cells;
    }
    return cells * U;
}

namespace {

const char* kindName(WallKind k) { return k == WallKind::Hay ? "hay" : k == WallKind::Mix ? "mix" : "crate"; }

bool number(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

bool whole(const std::string& s, int& out) {
    double v = 0;
    if (!number(s, v) || v != std::floor(v) || std::abs(v) > 1e6) return false;
    out = int(v);
    return true;
}

// a number as short as it reads (to the millimetre)
std::string fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

} // namespace

std::optional<LevelData> parseLevel(int number_, const std::string& text, std::string* error) {
    LevelData d;
    d.number = number_;
    d.diffOffset = 35.0 * (number_ - 1);
    int ln = 0;
    auto fail = [&](const std::string& why) -> std::optional<LevelData> {
        if (error) *error = "level " + std::to_string(number_) + (ln ? ", line " + std::to_string(ln) : std::string()) + ": " + why;
        return std::nullopt;
    };
    enum class Part { Head, Course, Saws, Discos } part = Part::Head;
    bool started = false, hasLength = false;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t semi = line.find(';');
        if (semi != std::string::npos) {
            if (!started && line.find_first_not_of(" \t") == semi) { // (a note at the top: its description)
                std::string note = line.substr(semi + 1);
                if (!note.empty() && note[0] == ' ') note.erase(0, 1);
                d.notes.push_back(note);
                continue;
            }
            line.erase(semi);
        }
        std::istringstream words(line);
        std::vector<std::string> w;
        for (std::string t; words >> t;) w.push_back(t);
        if (w.empty()) continue;
        started = true;
        if (w[0] == "course" && w.size() == 1) { part = Part::Course; continue; }
        if (w[0] == "saws" && w.size() == 1) { part = Part::Saws; continue; }
        if (w[0] == "discos" && w.size() == 1) { part = Part::Discos; continue; }
        if (w[0] == "length") {
            if (w.size() != 2 || !number(w[1], d.length) || d.length <= 0 || d.length > 5000) return fail("length: the finish line, metres along (up to 5000)");
            hasLength = true;
            continue;
        }
        if (w[0] == "difficulty") {
            if (w.size() != 3 || !number(w[1], d.diffOffset) || !number(w[2], d.diffScale) || d.diffOffset < 0 || d.diffScale < 0)
                return fail("difficulty: where its band starts on the curve, and how fast it moves along it (two numbers, 0 or more)");
            continue;
        }
        if (w[0] == "raids") {
            int n = 0;
            if (w.size() == 2 && w[1] == "auto") d.raids = -1;
            else if (w.size() == 2 && w[1] == "none") d.raids = 0;
            else if (w.size() == 2 && whole(w[1], n) && n >= 0 && n <= 6) d.raids = n;
            else return fail("raids: auto, none, or how many (up to 6)");
            continue;
        }
        if (part == Part::Course) {
            LevelData::Wall wall;
            if (w.size() < 3 || !whole(w[0], wall.cells) || wall.cells < 1 || wall.cells > 4000 || !whole(w[1], wall.h) || wall.h < 0 || wall.h > 40)
                return fail("a wall: how long (cells), how high (eggs, up to 40), its kind, and a barrier's underside if there's one");
            if (w[2] == "gauntlet") {
                if (w.size() != 4) return fail("a gauntlet: its width, 0, gauntlet, and its name");
                wall.gauntlet = w[3];
                const Gauntlet* g = gauntletNamed(wall.gauntlet);
                if (!g) return fail("there's no gauntlet called " + wall.gauntlet + " (assets/gauntlets)");
                if (wall.h != 0) return fail("a gauntlet stands on the street: its height is 0");
                if (wall.cells != g->cols()) return fail("the " + wall.gauntlet + " is " + std::to_string(g->cols()) + " cells wide, not " + std::to_string(wall.cells));
            } else {
                if (w[2] == "crate") wall.kind = WallKind::Crate;
                else if (w[2] == "hay") wall.kind = WallKind::Hay;
                else if (w[2] == "mix") wall.kind = WallKind::Mix;
                else return fail("a wall's kind: crate, hay or mix (or a gauntlet)");
                if (w.size() > 4) return fail("a wall: cells, height, kind, and a barrier's underside (nothing more)");
                if (w.size() == 4 && (!whole(w[3], wall.ceil) || wall.ceil < wall.h + 2 || wall.ceil > 60))
                    return fail("a barrier's underside: at least 2 eggs over its wall (her body under it), up to 60");
            }
            d.course.push_back(wall);
            continue;
        }
        if (part == Part::Saws) {
            for (const std::string& t : w) {
                double x = 0;
                if (!number(t, x) || x < 0) return fail("a saw: where along, metres");
                d.saws.push_back(x);
            }
            continue;
        }
        if (part == Part::Discos) {
            LevelData::Disco c;
            if (w.size() != 2 || !number(w[0], c.x) || !number(w[1], c.y) || c.x < 0 || c.y < 0 || c.y > 40)
                return fail("a disco ball: where along, how high (metres), one a line");
            d.discos.push_back(c);
            continue;
        }
        return fail("what's '" + w[0] + "'? (length, difficulty, raids, then course, saws, discos)");
    }
    ln = 0;
    if (!hasLength) return fail("no length: its finish line");
    if (d.course.empty()) return fail("no course");
    const double reach = d.courseLength();
    if (reach + 1e-6 < d.length) return fail("its course ends at " + fmt(reach) + " m, short of its finish line (" + fmt(d.length) + " m)");
    double x = 0;
    for (const LevelData::Wall& wall : d.course) { // (no saw in a gauntlet: its own are its grid's)
        const double x1 = x + wall.cells * U;
        if (!wall.gauntlet.empty())
            for (double s : d.saws)
                if (s >= x && s < x1) return fail("a saw at " + fmt(s) + " m is inside the " + wall.gauntlet + " (its blades are its grid's)");
        x = x1;
    }
    for (double s : d.saws) if (s > reach) return fail("a saw at " + fmt(s) + " m is past the course's end");
    for (const LevelData::Disco& c : d.discos) if (c.x > reach) return fail("a disco ball at " + fmt(c.x) + " m is past the course's end");
    return d;
}

std::string levelText(const LevelData& d) {
    std::string o;
    for (const std::string& n : d.notes) o += n.empty() ? ";\n" : "; " + n + "\n";
    o += "length " + fmt(d.length) + "\n";
    o += "difficulty " + fmt(d.diffOffset) + " " + fmt(d.diffScale) + "\n";
    o += "raids " + (d.raids < 0 ? std::string("auto") : d.raids == 0 ? std::string("none") : std::to_string(d.raids)) + "\n";
    o += "course\n";
    for (const LevelData::Wall& w : d.course) {
        if (!w.gauntlet.empty()) { o += std::to_string(w.cells) + " 0 gauntlet " + w.gauntlet + "\n"; continue; }
        o += std::to_string(w.cells) + " " + std::to_string(w.h) + " " + kindName(w.kind) + (w.ceil ? " " + std::to_string(w.ceil) : std::string()) + "\n";
    }
    o += "saws\n";
    for (size_t i = 0; i < d.saws.size(); ++i) o += fmt(d.saws[i]) + (i + 1 == d.saws.size() || i % 8 == 7 ? "\n" : " ");
    o += "discos\n";
    for (const LevelData::Disco& c : d.discos) o += fmt(c.x) + " " + fmt(c.y) + "\n";
    return o;
}

namespace {

std::map<int, std::string>& texts() {
    static std::map<int, std::string> all = {
#include "LevelLayouts.inc"
    };
    return all;
}

std::map<int, LevelData>& parsed() {
    static std::map<int, LevelData> all;
    return all;
}

} // namespace

const LevelData* levelData(int number_) {
    if (const auto it = parsed().find(number_); it != parsed().end()) return &it->second;
    const auto it = texts().find(number_);
    if (it == texts().end()) return nullptr;
    std::string why;
    auto d = parseLevel(number_, it->second, &why);
    if (!d) { std::fprintf(stderr, "[CuckooStack] %s\n", why.c_str()); return nullptr; }
    return &parsed().emplace(number_, std::move(*d)).first->second;
}

std::vector<int> levelNumbers() {
    std::vector<int> out;
    for (const auto& [n, text] : texts()) out.push_back(n);
    return out;
}

bool debugOverrideLevel(int number_, const std::string& text, std::string* error) {
    auto d = parseLevel(number_, text, error);
    if (!d) return false;
    texts()[number_] = text;
    if (const auto it = parsed().find(number_); it != parsed().end()) it->second = std::move(*d);
    else parsed().emplace(number_, std::move(*d));
    return true;
}

} // namespace cs
