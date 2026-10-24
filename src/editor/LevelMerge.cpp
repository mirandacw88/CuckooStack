#include "LevelMerge.h"

#include "EditCourse.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace cs::ed {

namespace {

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    size_t at = 0;
    for (size_t nl; (nl = text.find('\n', at)) != std::string::npos; at = nl + 1) out.push_back(text.substr(at, nl - at));
    out.push_back(text.substr(at));
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> w;
    for (std::string t; in >> t;) w.push_back(t);
    return w;
}

// a gauntlet's course line: its width, 0, gauntlet, its name (as build_levels.py's COURSE_LINE reads it)
bool gauntletLine(const std::vector<std::string>& w, int& width, std::string& name) {
    if (w.size() != 4 || w[1] != "0" || w[2] != "gauntlet" || w[0].find_first_not_of("0123456789") != std::string::npos || w[0].empty()) return false;
    width = std::atoi(w[0].c_str());
    name = w[3];
    return true;
}

// a side's change against the base: theirs if only they changed it, mine if only I did, a conflict if both did
// differently
template <class T>
T pick(const T& base, const T& mine, const T& theirs, const char* what, std::vector<std::string>& conflicts) {
    if (mine == theirs || theirs == base) return mine;
    if (mine == base) return theirs;
    conflicts.push_back(what);
    return mine;
}

// each kind of hazard as a set: what either side added, less what either side took out
std::vector<Placed> mergeItems(const std::vector<Placed>& base, const std::vector<Placed>& mine, const std::vector<Placed>& theirs) {
    auto key = [](const Placed& p) { return std::make_pair(std::string(p.type->id), p.v); };
    std::map<std::pair<std::string, std::vector<double>>, int> b, m, t;
    std::map<std::pair<std::string, std::vector<double>>, const Placed*> any;
    for (const Placed& p : base) { ++b[key(p)]; any[key(p)] = &p; }
    for (const Placed& p : mine) { ++m[key(p)]; any[key(p)] = &p; }
    for (const Placed& p : theirs) { ++t[key(p)]; any[key(p)] = &p; }
    std::vector<Placed> out;
    for (const auto& [k, p] : any) {
        const int nb = b[k], dm = m[k] - nb, dt = t[k] - nb;
        // (each side's change: both taking one out, or both putting the same one in, is one change, not two)
        const int d = dm < 0 && dt < 0 ? std::min(dm, dt) : dm > 0 && dt > 0 ? std::max(dm, dt) : dm + dt;
        for (int i = 0; i < nb + d; ++i) out.push_back(*p);
    }
    std::stable_sort(out.begin(), out.end(), [](const Placed& a, const Placed& c) { return a.x() < c.x(); });
    return out;
}

} // namespace

std::string refit(const std::string& text, const std::string& name, int width) {
    std::vector<std::string> lines = split(text);
    std::string part;
    int cells = 0, old = 0;
    double at = -1;
    for (std::string& line : lines) {
        const std::string body = line.substr(0, line.find(';'));
        const std::vector<std::string> w = words(body);
        if (w.size() == 1 && (w[0] == "course" || hazardSection(w[0]) || std::all_of(w[0].begin(), w[0].end(), [](char c) { return c >= 'a' && c <= 'z'; }))) {
            part = w[0];
            continue;
        }
        if (part != "course" || w.empty()) continue;
        int width0 = 0;
        std::string named;
        if (gauntletLine(w, width0, named) && named == name && at < 0) {
            old = width0;
            if (old == width) return text;
            at = (cells + old) * U; // (what's past here moves)
            line = std::to_string(width) + " 0 gauntlet " + name;
        }
        cells += std::atoi(w[0].c_str());
    }
    if (at < 0) return text;
    const double shift = (width - old) * U;
    auto on = [&](double x) { return x >= at - 1e-9 ? x + shift : x; };
    part.clear();
    for (std::string& line : lines) {
        const std::string body = line.substr(0, line.find(';'));
        const std::vector<std::string> w = words(body);
        if (w.size() == 1 && (w[0] == "course" || hazardSection(w[0]) || std::all_of(w[0].begin(), w[0].end(), [](char c) { return c >= 'a' && c <= 'z'; }))) {
            part = w[0];
            continue;
        }
        if (w.empty()) continue;
        if (part.empty() && w[0] == "length" && w.size() > 1) { line = "length " + levelNum(on(std::atof(w[1].c_str()))); continue; }
        const HazardType* t = hazardSection(part);
        if (!t || t->params.empty()) continue;
        std::string out; // (each one's x moved, its other numbers as they were written)
        for (size_t i = 0; i < w.size(); ++i) out += (i ? " " : "") + (i % t->params.size() == 0 ? levelNum(on(std::atof(w[i].c_str()))) : w[i]);
        line = out;
    }
    std::string o;
    for (size_t i = 0; i < lines.size(); ++i) o += (i ? "\n" : "") + lines[i];
    return o;
}

MergeResult merge3(int number, const std::string& base, const std::string& mine, const std::string& theirs, const LevelCatalog* cat) {
    MergeResult r;
    if (mine == theirs || theirs == base) { r.ok = true; r.text = mine; return r; }
    if (mine == base) { r.ok = true; r.text = theirs; return r; }
    std::string why;
    const auto b = parseLevel(number, base, &why, cat);
    const auto m = b ? parseLevel(number, mine, &why, cat) : std::nullopt;
    const auto t = m ? parseLevel(number, theirs, &why, cat) : std::nullopt;
    if (!t) { r.error = why; r.text = mine; return r; }
    const Course cb = fromData(*b), cm = fromData(*m), ct = fromData(*t);
    Course out = cm;
    out.notes = pick(cb.notes, cm.notes, ct.notes, "notes", r.conflicts);
    out.length = pick(cb.length, cm.length, ct.length, "length", r.conflicts);
    const auto diff = pick(std::make_pair(cb.diffOffset, cb.diffScale), std::make_pair(cm.diffOffset, cm.diffScale),
                           std::make_pair(ct.diffOffset, ct.diffScale), "difficulty", r.conflicts);
    out.diffOffset = diff.first; out.diffScale = diff.second;
    out.raids = pick(cb.raids, cm.raids, ct.raids, "raids", r.conflicts);
    out.hard = pick(cb.hard, cm.hard, ct.hard, "hard", r.conflicts);
    out.unknown = pick(cb.unknown, cm.unknown, ct.unknown, "sections this build doesn't know", r.conflicts);
    // the course: column by column where the three are as long; else whichever side changed it
    if (cb.cols.size() == cm.cols.size() && cb.cols.size() == ct.cols.size()) {
        bool clash = false;
        for (size_t i = 0; i < cb.cols.size(); ++i) {
            std::vector<std::string> none;
            out.cols[i] = pick(cb.cols[i], cm.cols[i], ct.cols[i], "course", none);
            clash = clash || !none.empty();
        }
        if (clash) r.conflicts.push_back("course");
    } else {
        out.cols = pick(cb.cols, cm.cols, ct.cols, "course", r.conflicts);
    }
    out.items = mergeItems(cb.items, cm.items, ct.items);
    while (out.cols.size() * U < out.length - 1e-9) out.cols.push_back(Col{});
    const LevelData merged = toData(out);
    r.text = levelText(merged);
    if (!parseLevel(number, r.text, &why, cat)) { // (each side fine, together not: a saw now in a gauntlet...)
        r.conflicts.push_back("together: " + why);
        r.text = mine;
    }
    r.ok = r.conflicts.empty();
    return r;
}

} // namespace cs::ed
