#include "LevelData.h"

#include "LevelCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace cs {

double LevelData::courseLength(const LevelCatalog* cat) const {
    if (!cat) cat = &LevelCatalog::dev();
    int cells = 0;
    for (const Wall& w : course) {
        const Gauntlet* g = w.gauntlet.empty() ? nullptr : cat->gauntlet(w.gauntlet);
        cells += g ? g->cols() : w.cells;
    }
    return cells * U;
}

std::vector<double> LevelData::xsOf(const HazardType& t) const {
    std::vector<double> out;
    for (const Placed& p : items) if (p.type == &t) out.push_back(p.x());
    return out;
}

size_t LevelData::countOf(const HazardType& t) const {
    size_t n = 0;
    for (const Placed& p : items) n += p.type == &t;
    return n;
}

namespace {

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

// a section's word: lower-case letters, 3 or more (a hazard's, or one a newer game knows)
bool sectionWord(const std::string& s) {
    if (s.size() < 3) return false;
    for (char c : s) if (c < 'a' || c > 'z') return false;
    return s != "length" && s != "difficulty" && s != "raids" && s != "hard";
}

} // namespace

std::string levelNum(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

std::optional<LevelData> parseLevel(int number_, const std::string& text, std::string* error, const LevelCatalog* cat) {
    if (!cat) cat = &LevelCatalog::dev();
    LevelData d;
    d.number = number_;
    d.diffOffset = 35.0 * (number_ - 1);
    int ln = 0;
    auto fail = [&](const std::string& why) -> std::optional<LevelData> {
        if (error) *error = "level " + std::to_string(number_) + (ln ? ", line " + std::to_string(ln) : std::string()) + ": " + why;
        return std::nullopt;
    };
    enum class Part { Head, Course, Hazard, Unknown } part = Part::Head;
    const HazardType* hazard = nullptr;
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
        const std::string raw = line;
        std::istringstream words(line);
        std::vector<std::string> w;
        for (std::string t; words >> t;) w.push_back(t);
        if (w.empty()) { if (part == Part::Unknown && semi == std::string::npos) d.unknown.push_back(raw); continue; }
        started = true;
        if (w[0] == "course" && w.size() == 1) { part = Part::Course; continue; }
        if (const HazardType* t = w.size() == 1 ? hazardSection(w[0]) : nullptr) { hazard = t; part = Part::Hazard; continue; }
        if (w.size() == 1 && part != Part::Head && sectionWord(w[0])) { part = Part::Unknown; d.unknown.push_back(raw); continue; } // (a newer game's)
        if (part == Part::Unknown) { d.unknown.push_back(raw); continue; }
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
        if (w[0] == "hard") {
            int n = 0;
            if (w.size() != 2 || !whole(w[1], n) || n < 0 || n > 10) return fail("hard: 0 to 10 (the generator's density; 8 on, the checker's margins shrink)");
            d.hard = n;
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
            if (w.size() < 3 || !whole(w[0], wall.cells) || wall.cells < 1 || wall.cells > 4000 || !whole(w[1], wall.h) || wall.h < 0 || wall.h > 60)
                return fail("a wall: how long (cells), how high (eggs, up to 60), its kind, and a barrier's underside if there's one");
            if (w[2] == "gauntlet") {
                if (w.size() != 4) return fail("a gauntlet: its width, 0, gauntlet, and its name");
                wall.gauntlet = w[3];
                const Gauntlet* g = cat->gauntlet(wall.gauntlet);
                if (!g) return fail("there's no gauntlet called " + wall.gauntlet + " (assets/gauntlets)");
                if (wall.h != 0) return fail("a gauntlet stands on the street: its height is 0");
                if (wall.cells != g->cols()) return fail("the " + wall.gauntlet + " is " + std::to_string(g->cols()) + " cells wide, not " + std::to_string(wall.cells));
            } else {
                const WallKindInfo* kind = wallKindNamed(w[2]);
                if (!kind) return fail("a wall's kind: crate, hay, mix, bumper, gate> or gate< (or a gauntlet)");
                wall.kind = kind->kind;
                if (kind->bouncer && w.size() > 3) return fail("a bumper or a gate: no barrier or floating block over it");
                if (kind->bouncer && wall.h < 1) return fail("a bumper or a gate: at least 1 egg high");
                if (kind->bouncer && d.course.empty()) return fail("a bumper or a gate: not the course's first wall (she comes to it)");
                if (w.size() > 5) return fail("a wall: cells, height, kind, a barrier's underside and a floating block's top (nothing more)");
                if (w.size() == 5 && (!whole(w[4], wall.top) || !whole(w[3], wall.ceil) || wall.ceil < wall.h + 1 || wall.top <= wall.ceil || wall.top > 60))
                    return fail("a floating block: its underside at least 1 egg over its wall, its top over its underside, up to 60");
                if (w.size() == 4 && (!whole(w[3], wall.ceil) || wall.ceil < wall.h + 2 || wall.ceil > 80))
                    return fail("a barrier's underside: at least 2 eggs over its wall (her body under it), up to 80");
            }
            d.course.push_back(wall);
            continue;
        }
        if (part == Part::Hazard) { // (its numbers, as many a line as its type takes, each in its range)
            const size_t n = hazard->params.size();
            if (w.size() % n || (hazard->perLine == 1 && w.size() != n)) return fail(hazard->form);
            for (size_t i = 0; i < w.size(); i += n) {
                Placed p{hazard, std::vector<double>(n)};
                for (size_t k = 0; k < n; ++k)
                    if (!number(w[i + k], p.v[k]) || p.v[k] < hazard->params[k].min || p.v[k] > hazard->params[k].max) return fail(hazard->form);
                d.items.push_back(std::move(p));
            }
            continue;
        }
        std::string sections;
        for (const HazardType* t : hazardTypes()) sections += std::string(", ") + t->section;
        return fail("what's '" + w[0] + "'? (length, difficulty, raids, then course" + sections + ")");
    }
    ln = 0;
    if (!hasLength) return fail("no length: its finish line");
    if (d.course.empty()) return fail("no course");
    const double reach = d.courseLength(cat);
    if (reach + 1e-6 < d.length) return fail("its course ends at " + levelNum(reach) + " m, short of its finish line (" + levelNum(d.length) + " m)");
    for (const Placed& p : d.items)
        if (p.type->validate)
            if (const std::string why = p.type->validate(p, d); !why.empty()) return fail(why);
    while (!d.unknown.empty() && d.unknown.back().find_first_not_of(" \t") == std::string::npos) d.unknown.pop_back();
    return d;
}

std::string renameGauntletIn(const std::string& text, const std::string& from, const std::string& to) {
    std::string o;
    size_t i = 0;
    while (i < text.size()) {
        const size_t e = std::min(text.find('\n', i), text.size());
        std::string line = text.substr(i, e - i);
        std::istringstream in(line);
        std::string n, h, kind, name, extra;
        if (in >> n >> h >> kind >> name && !(in >> extra) && kind == "gauntlet" && name == from)
            line.replace(line.rfind(from), from.size(), to);
        o += line;
        if (e < text.size()) o += '\n';
        i = e + 1;
    }
    return o;
}

std::string levelText(const LevelData& d) {
    std::string o;
    for (const std::string& n : d.notes) o += n.empty() ? ";\n" : "; " + n + "\n";
    o += "length " + levelNum(d.length) + "\n";
    o += "difficulty " + levelNum(d.diffOffset) + " " + levelNum(d.diffScale) + "\n";
    o += "raids " + (d.raids < 0 ? std::string("auto") : d.raids == 0 ? std::string("none") : std::to_string(d.raids)) + "\n";
    if (d.hard) o += "hard " + std::to_string(d.hard) + "\n";
    o += "course\n";
    for (const LevelData::Wall& w : d.course) {
        if (!w.gauntlet.empty()) { o += std::to_string(w.cells) + " 0 gauntlet " + w.gauntlet + "\n"; continue; }
        o += std::to_string(w.cells) + " " + std::to_string(w.h) + " " + wallKindInfo(w.kind).id + (w.ceil ? " " + std::to_string(w.ceil) : std::string()) +
             (w.top ? " " + std::to_string(w.top) : std::string()) + "\n";
    }
    for (const HazardType* t : hazardTypes()) { // (each its section: one a line, or as many as a line holds, 8 to a line)
        std::vector<const Placed*> mine;
        for (const Placed& p : d.items) if (p.type == t) mine.push_back(&p);
        if (mine.empty() && !t->alwaysWritten) continue;
        o += std::string(t->section) + "\n";
        for (size_t i = 0; i < mine.size(); ++i) {
            std::string one;
            for (size_t k = 0; k < mine[i]->v.size(); ++k) one += (k ? " " : "") + levelNum(mine[i]->v[k]);
            o += one + (t->perLine == 1 || i + 1 == mine.size() || i % 8 == 7 ? "\n" : " ");
        }
    }
    for (const std::string& line : d.unknown) o += line + "\n";
    return o;
}

const LevelData* levelData(int number_) { return LevelCatalog::dev().level(number_); }

std::vector<int> levelNumbers() { return LevelCatalog::dev().levelNumbers(); }

bool debugOverrideLevel(int number_, const std::string& text, std::string* error) { return LevelCatalog::dev().setLevel(number_, text, error); }

} // namespace cs
