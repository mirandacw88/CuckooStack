#include "EditCourse.h"

#include "core/CourseRules.h"

#include "core/Gauntlet.h"

#include <algorithm>
#include <cmath>

namespace cs::ed {

namespace {

Col street(bool cut = false) { Col k; k.cut = cut; return k; }

// a column changed: like its neighbours now, one wall with them
void join(std::vector<Col>& cols, int c) {
    if (c > 0 && c < int(cols.size()) && cols[size_t(c)].cut && alike(cols[size_t(c - 1)], cols[size_t(c)])) cols[size_t(c)].cut = false;
    if (c >= 0 && c + 1 < int(cols.size()) && cols[size_t(c + 1)].cut && alike(cols[size_t(c)], cols[size_t(c + 1)])) cols[size_t(c + 1)].cut = false;
}

bool validCol(const Course& c, int col) { return col >= 0 && col < int(c.cols.size()) && c.cols[size_t(col)].g.empty(); }

void sortItems(Course& c) { std::stable_sort(c.items.begin(), c.items.end(), [](const Placed& a, const Placed& b) { return a.x() < b.x(); }); }

// every hazard's x past `at` moved by `shift` (columns put in or taken out, a gauntlet grown)
void shiftItems(Course& c, double at, double shift) {
    for (Placed& p : c.items) if (!p.v.empty() && p.v[0] >= at - 1e-9) p.v[0] = mm(p.v[0] + shift);
}

bool overlapsSpan(const Course& c, int a, int b) {
    for (const Span& s : spans(c)) if (a < s.c1 && b >= s.c0) return true;
    return false;
}

} // namespace

double mm(double v) { return std::round(v * 1000) / 1000; }

bool alike(const Col& a, const Col& b) {
    return a.g == b.g && (!a.g.empty() || (a.h == b.h && a.kind == b.kind && a.ceil == b.ceil && a.top == b.top));
}

Course fromData(const LevelData& d) {
    Course c;
    c.number = d.number; c.notes = d.notes; c.length = d.length; c.diffOffset = d.diffOffset; c.diffScale = d.diffScale; c.raids = d.raids; c.hard = d.hard;
    for (const LevelData::Wall& w : d.course)
        for (int i = 0; i < w.cells; ++i) {
            Col k;
            k.g = w.gauntlet;
            if (w.gauntlet.empty()) { k.h = w.h; k.kind = w.kind; k.ceil = w.ceil; k.top = w.top; }
            k.cut = i == 0;
            c.cols.push_back(k);
        }
    c.items = d.items;
    c.unknown = d.unknown;
    return c;
}

LevelData toData(const Course& c) {
    LevelData d;
    d.number = c.number; d.notes = c.notes; d.length = c.length; d.diffOffset = c.diffOffset; d.diffScale = c.diffScale; d.raids = c.raids; d.hard = c.hard;
    for (size_t i = 0; i < c.cols.size();) {
        size_t j = i + 1;
        while (j < c.cols.size() && !c.cols[j].cut && alike(c.cols[i], c.cols[j])) ++j;
        LevelData::Wall w;
        w.cells = int(j - i);
        const Col& k = c.cols[i];
        if (!k.g.empty()) w.gauntlet = k.g;
        else { w.h = k.h; w.kind = k.kind; w.ceil = k.ceil; w.top = k.top; }
        d.course.push_back(w);
        i = j;
    }
    d.items = c.items;
    d.unknown = c.unknown;
    return d;
}

std::vector<Span> spans(const Course& c) {
    std::vector<Span> out;
    for (size_t i = 0; i < c.cols.size();) {
        size_t j = i + 1;
        while (j < c.cols.size() && !c.cols[j].cut && alike(c.cols[i], c.cols[j])) ++j;
        if (!c.cols[i].g.empty()) out.push_back({c.cols[i].g, int(i), int(j)});
        i = j;
    }
    return out;
}

std::optional<Span> spanOf(const Course& c, const std::string& name) {
    for (const Span& s : spans(c)) if (s.name == name) return s;
    return std::nullopt;
}

bool inGauntlet(const Course& c, double x) {
    const int col = colAt(x);
    return col >= 0 && col < int(c.cols.size()) && !c.cols[size_t(col)].g.empty();
}

int surfaceAt(const Course& c, double x) {
    const int col = colAt(x);
    if (col < 0 || col >= int(c.cols.size()) || !c.cols[size_t(col)].g.empty()) return 0;
    const Col& k = c.cols[size_t(col)];
    return k.top ? k.top : k.h;
}

double itemY(const Course& c, const Placed& p) {
    if (p.type->anchor == Anchor::Free) return p.at(1);
    if (p.type->anchor == Anchor::Face) return (p.at(1) + 0.5) * U;
    return surfaceAt(c, p.x()) * U;
}

bool solidCell(const Course& c, int col, int row) {
    if (col < 0 || col >= int(c.cols.size())) return false;
    const Col& k = c.cols[size_t(col)];
    if (!k.g.empty() || row < k.h) return true;
    if (k.top) return row >= k.ceil && row < k.top;
    return k.ceil && row >= k.ceil;
}

std::optional<std::vector<double>> faceNear(const Course& c, double x, double y, int cols) {
    const int row = std::clamp(static_cast<int>(std::floor(y / U)), 0, 14);
    const int mid = static_cast<int>(std::lround(x / U));
    for (int k = 0; k <= cols; ++k)
        for (int e : {mid - k, mid + k}) { // (each edge out from the finger: a solid cell on one side, open on the other)
            if (e < 1 || e > int(c.cols.size()) - 1) continue;
            const bool l = solidCell(c, e - 1, row), r = solidCell(c, e, row);
            if (l == r || !c.cols[size_t(l ? e - 1 : e)].g.empty()) continue;
            return std::vector<double>{e * U, double(row), l ? 1.0 : 0.0};
        }
    return std::nullopt;
}

std::pair<double, double> faceReach(const Course& c, const Placed& p) {
    const int row = static_cast<int>(std::lround(p.at(1))), d = p.at(2) > 0.5 ? 1 : -1;
    int e = static_cast<int>(std::lround(p.x() / U)); // (the edge its beam has reached)
    for (int k = 0; k < static_cast<int>(16.0 / U); ++k) {
        const int cell = d > 0 ? e : e - 1;
        if (cell < 0 || cell >= int(c.cols.size()) || solidCell(c, cell, row)) break;
        e += d;
    }
    return {std::min(p.x(), e * U), std::max(p.x(), e * U)};
}

double itemEnd(const Placed& p) { return p.type->spanParam >= 0 ? p.x() + p.at(size_t(p.type->spanParam)) * U : p.x(); }

int itemNear(const Course& c, const HazardType* t, double x, double y, double tol) {
    int best = -1;
    double bd = tol;
    for (size_t i = 0; i < c.items.size(); ++i) {
        const Placed& p = c.items[i];
        if (t && p.type != t) continue;
        const double dy = p.type->anchor == Anchor::Surface ? 0 : itemY(c, p) - y;
        const double dx = x < p.x() ? p.x() - x : x > itemEnd(p) ? x - itemEnd(p) : 0; // (along a stretch: on it)
        const double d = std::hypot(dx, dy);
        if (d <= bd) { bd = d; best = int(i); }
    }
    return best;
}

// ---- walls

bool setHeight(Course& c, int col, int h, std::optional<WallKind> kind) {
    if (!validCol(c, col)) return false;
    Col& k = c.cols[size_t(col)];
    h = std::clamp(h, 0, kMaxH);
    const bool keep = k.ceil && k.ceil >= h + (k.top ? 1 : 2); // (a barrier or block now on her head goes)
    Col nk = k;
    nk.h = h;
    if (kind && (k.h == 0 || h == 0)) nk.kind = *kind;
    if (h == 0 && bouncer(nk.kind)) nk.kind = WallKind::Crate; // (a bouncer down to the street: street)
    nk.ceil = keep ? k.ceil : 0;
    nk.top = keep ? k.top : 0;
    if (nk == k) return false;
    k = nk;
    join(c.cols, col);
    return true;
}

bool setKind(Course& c, int col, WallKind kind) {
    if (!validCol(c, col) || c.cols[size_t(col)].kind == kind || bouncer(kind) != bouncer(c.cols[size_t(col)].kind)) return false; // (the Bouncers tools make bouncers)
    c.cols[size_t(col)].kind = kind;
    join(c.cols, col);
    return true;
}

bool setBouncer(Course& c, int col, int h, WallKind kind) {
    if (!validCol(c, col) || col == 0 || !bouncer(kind) || bouncer(c.cols[0].kind)) return false;
    Col& k = c.cols[size_t(col)];
    Col nk = k;
    nk.h = std::clamp(h, 1, kMaxH); nk.kind = kind; nk.ceil = 0; nk.top = 0;
    if (nk == k) return false;
    k = nk;
    join(c.cols, col);
    return true;
}

// a barrier's underside over a wall (0: none, and no block): 2 eggs over its top at least (her body under it); a
// floating block's underside 1 at least, under its top
bool setCeil(Course& c, int col, int ceil) {
    if (!validCol(c, col)) return false;
    Col& k = c.cols[size_t(col)];
    ceil = std::clamp(ceil, 0, kMaxCeil);
    if (ceil && bouncer(k.kind)) return false; // (nothing over a bumper or a gate)
    if ((ceil && ceil < k.h + (k.top ? 1 : 2)) || (ceil && k.top && ceil >= k.top) || k.ceil == ceil) return false;
    k.ceil = ceil;
    if (!ceil) k.top = 0;
    join(c.cols, col);
    return true;
}

// a floating block: its underside (1 egg over the wall at least) and its top (over its underside, up to kMaxH); top 0:
// none (a barrier under it goes too); of `kind` over the street (a block's kind is its wall's)
bool setBlock(Course& c, int col, int bottom, int top, std::optional<WallKind> kind) {
    if (!validCol(c, col)) return false;
    Col& k = c.cols[size_t(col)];
    if (!top) {
        if (!k.top) return false;
        k.ceil = 0; k.top = 0;
        join(c.cols, col);
        return true;
    }
    if (bouncer(k.kind)) return false;
    const WallKind nkind = kind && !k.h ? *kind : k.kind;
    if (bottom < k.h + 1 || top <= bottom || top > kMaxH || (k.ceil == bottom && k.top == top && k.kind == nkind)) return false;
    k.kind = nkind; k.ceil = bottom; k.top = top;
    join(c.cols, col);
    return true;
}

// ---- hazards

double snapX(const Course& c, const HazardType& t, double x, int exclude) {
    if (t.spanParam >= 0) return mm(std::round(x / U) * U); // (a stretch: on the cells)
    double best = std::round(x * 10) / 10;
    if (t.snapGap <= 0) return best;
    double bd = 0.7;
    for (size_t i = 0; i < c.items.size(); ++i) {
        if (int(i) == exclude || c.items[i].type != &t) continue;
        for (const double at : {c.items[i].x() - t.snapGap, c.items[i].x() + t.snapGap})
            if (std::abs(at - x) < bd) { bd = std::abs(at - x); best = mm(at); }
    }
    return best;
}

bool itemFits(const Course& c, const HazardType& t, const std::vector<double>& v, int exclude) {
    if (v.size() != t.params.size()) return false;
    for (size_t k = 0; k < v.size(); ++k) if (v[k] < t.params[k].min - 1e-9 || v[k] > t.params[k].max + 1e-9) return false;
    const double x = v[0];
    if (x <= 0 || x >= courseLength(c)) return false;
    if (t.offGauntlets && inGauntlet(c, x)) return false;
    if (t.anchor == Anchor::Free && v.size() > 1 && v[1] < 0.2) return false;
    if (t.spanParam >= 0) { // (a stretch: all of it on the course, none of it in a gauntlet)
        const double end = x + v[size_t(t.spanParam)] * U;
        if (end > courseLength(c) + 1e-9) return false;
        if (t.offGauntlets)
            for (double at = x; at < end - 1e-9; at += U) if (inGauntlet(c, at + 1e-6)) return false;
    }
    if (t.minGap > 0)
        for (size_t i = 0; i < c.items.size(); ++i)
            if (int(i) != exclude && c.items[i].type == &t && std::abs(c.items[i].x() - x) < t.minGap) return false;
    return true;
}

int addItem(Course& c, const HazardType& t, std::vector<double> v) {
    for (double& n : v) n = mm(n);
    if (!itemFits(c, t, v)) return -1;
    c.items.push_back({&t, v});
    const Placed* added = &c.items.back();
    const Placed copy = *added;
    sortItems(c);
    for (size_t i = 0; i < c.items.size(); ++i) if (c.items[i] == copy) return int(i);
    return -1;
}

int moveItemTo(Course& c, int i, std::vector<double> v) {
    if (i < 0 || i >= int(c.items.size())) return -1;
    for (double& n : v) n = mm(n);
    const HazardType& t = *c.items[size_t(i)].type;
    if (c.items[size_t(i)].v == v || !itemFits(c, t, v, i)) return -1;
    c.items[size_t(i)].v = v;
    const Placed copy = c.items[size_t(i)];
    sortItems(c);
    for (size_t k = 0; k < c.items.size(); ++k) if (c.items[k] == copy) return int(k);
    return -1;
}

bool moveItem(Course& c, int i, std::vector<double> v) { return moveItemTo(c, i, std::move(v)) >= 0; }

bool removeItem(Course& c, int i) {
    if (i < 0 || i >= int(c.items.size())) return false;
    c.items.erase(c.items.begin() + i);
    return true;
}

bool setItemParam(Course& c, int i, size_t param, double value) {
    if (i < 0 || i >= int(c.items.size()) || param >= c.items[size_t(i)].v.size()) return false;
    std::vector<double> v = c.items[size_t(i)].v;
    v[param] = value;
    return moveItem(c, i, v);
}

// ---- columns

std::optional<Clip> extractCols(const Course& c, int c0, int c1) {
    const int a = std::max(0, std::min(c0, c1)), b = std::min(int(c.cols.size()) - 1, std::max(c0, c1));
    if (b < a || overlapsSpan(c, a, b)) return std::nullopt;
    const double x0 = a * U, x1 = (b + 1) * U;
    Clip clip;
    for (int i = a; i <= b; ++i) { Col k = c.cols[size_t(i)]; k.cut = i > a && k.cut; clip.cols.push_back(k); }
    for (const Placed& p : c.items) // (a stretch: copied only when all of it's in them)
        if (p.x() >= x0 && p.x() < x1 && itemEnd(p) <= x1 + 1e-9) { Placed q = p; q.v[0] = mm(p.x() - x0); clip.items.push_back(q); }
    return clip;
}

bool insertCols(Course& c, int at, const Clip& clip) {
    if (clip.cols.empty() || at < 0 || at > int(c.cols.size())) return false;
    for (const Span& s : spans(c)) if (at > s.c0 && at < s.c1) return false; // (never inside a gauntlet)
    const double x = at * U, shift = clip.cols.size() * U;
    shiftItems(c, x, shift);
    c.cols.insert(c.cols.begin() + at, clip.cols.begin(), clip.cols.end());
    for (const Placed& p : clip.items) { Placed q = p; q.v[0] = mm(p.x() + x); c.items.push_back(q); }
    sortItems(c);
    if (c.length >= x - 1e-9) c.length = mm(c.length + shift);
    return true;
}

bool duplicateCols(Course& c, int c0, int c1) {
    const std::optional<Clip> clip = extractCols(c, c0, c1);
    return clip && insertCols(c, std::max(c0, c1) + 1, *clip);
}

bool deleteCols(Course& c, int c0, int c1, double minLength) {
    const int a = std::max(0, std::min(c0, c1)), b = std::min(int(c.cols.size()) - 1, std::max(c0, c1));
    if (b < a || overlapsSpan(c, a, b)) return false;
    const double x0 = a * U, x1 = (b + 1) * U, shift = (b - a + 1) * U;
    const double length = c.length >= x1 - 1e-9 ? mm(c.length - shift) : c.length > x0 ? x0 : c.length;
    if (length < minLength) return false;
    c.items.erase(std::remove_if(c.items.begin(), c.items.end(), [&](const Placed& p) { // (a stretch: gone if they cut into it)
                      return (p.x() >= x0 && p.x() < x1) || (p.x() < x0 && itemEnd(p) > x0 + 1e-9); }),
                  c.items.end());
    shiftItems(c, x1, -shift);
    c.cols.erase(c.cols.begin() + a, c.cols.begin() + b + 1);
    c.length = length;
    while (c.cols.size() * U < length - 1e-9) c.cols.push_back(street());
    if (a < int(c.cols.size())) join(c.cols, a);
    return true;
}

bool setLength(Course& c, double x, double minLength) {
    const double length = std::max(minLength, mm(x));
    if (std::abs(length - c.length) < 1e-9) return false;
    while (c.cols.size() * U < length - 1e-9) c.cols.push_back(street());
    c.length = length;
    return true;
}

// ---- gauntlets

bool removeGauntlet(Course& c, const std::string& name, int forecourt) {
    const std::optional<Span> s = spanOf(c, name);
    if (!s) return false;
    const int a = std::max(0, s->c0 - forecourt), z = std::min(int(c.cols.size()), s->c1 + Level::kPorch);
    for (int i = a; i < z; ++i)
        if (c.cols[size_t(i)].g.empty() || c.cols[size_t(i)].g == name) c.cols[size_t(i)] = street(c.cols[size_t(i)].cut && (i == a || i == z));
    if (z < int(c.cols.size())) c.cols[size_t(z)].cut = true;
    for (int i = a; i <= z && i < int(c.cols.size()); ++i) join(c.cols, i);
    return true;
}

// Off a gauntlet's porch (column z on, its exit's height h): down a stair to the course, a drop of 2 at the most a step
// (or less, onto the highest wall its landing would cover), each onto a landing long enough to land and react on
// (rules::landingCells at `speed`), never onto a barrier or a block; the walls under it raised to it (a barrier or a
// block over one goes); it ends where the course is as high as it (from there, the course's own). Its end (a column)
int stairDown(Course& c, int z, int h, double speed) {
    const int n = int(c.cols.size());
    const WallKind kind = z > 0 ? c.cols[size_t(z - 1)].kind : WallKind::Crate; // (the porch's: each step one wall, one landing)
    auto raise = [&](int j, int to) {
        Col& k = c.cols[size_t(j)];
        if (k.h > to) return;
        if (k.h < to) { k.h = to; k.ceil = 0; k.top = 0; }
        k.kind = kind;
        k.cut = false; // (one wall a step: its landing whole)
    };
    // a saw, lava or spring the course already has (it rides the surface the stair raises): a drop's edge goes only where each
    // is clear of it as the checker wants, on the flat before the edge (a reaction's run to the wall) or well after it (she's
    // landed and had a reaction): else the flat runs on a cell, up to `defer` of them (what's left is cleared out by placeGauntlet)
    struct Rider { double x, xe, reach, ahead, dropReach; }; // (ahead: the run it wants to the wall after it; dropReach: a saw's reach counts after a drop)
    std::vector<Rider> riders;
    for (const Placed& p : c.items) {
        if (p.type->anchor != Anchor::Surface || p.x() < z * U - 1e-9) continue;
        if (p.type == &kSawHazard) riders.push_back({p.x(), p.x(), Level::kSawReach, std::max(2.5 * U, speed * 0.45 + Level::kSawR), Level::kSawReach});
        else if (p.type == &kLavaHazard) riders.push_back({p.x(), p.x() + p.at(1) * U, 0.05, std::max(2.5 * U, speed * 0.45), 0.0});
        else riders.push_back({p.x(), p.x(), 0.3, 0.3, 0.0});
    }
    const double react = speed * 0.6;
    auto clearEdge = [&](int e) {
        const double E = e * U;
        for (const Rider& r : riders) {
            const bool before = E - r.xe >= std::max(r.ahead, r.reach) - 1e-6, after = r.x - r.dropReach - E >= react - 1e-6 && r.x - r.reach >= E - 1e-6;
            if (!before && !after) return false;
        }
        return true;
    };
    int i = z, defer = 0;
    while (h > 0 && i < n && c.cols[size_t(i)].g.empty() && c.cols[size_t(i)].h < h) {
        // the step: down 2, or to the highest wall its landing would cover (no climb right after it)
        int high = 0;
        bool blocked = false; // (a maze, a barrier or a block where it would land: no room to land and react under it)
        for (int j = i; j < std::min(n, i + rules::landingCells(speed, 2)); ++j) {
            const Col& k = c.cols[size_t(j)];
            high = std::max(high, k.h);
            blocked = blocked || !k.g.empty() || k.ceil || k.top;
        }
        if (blocked || high >= h) { raise(i++, h); continue; } // (not yet: on at its height to the wall ahead, or over what's in the way)
        if (defer < 40 && !clearEdge(i)) { raise(i++, h); ++defer; continue; } // (...or past a hazard the edge would crowd)
        defer = 0;
        const int next = std::max(h - 2, high), land = rules::landingCells(speed, h - next);
        for (int j = i; j < std::min(n, i + land); ++j) raise(j, next);
        c.cols[size_t(i)].cut = true;
        h = next;
        i += land;
    }
    if (i < n) c.cols[size_t(i)].cut = true;
    return i;
}

int stairUp(Course& c, int z, int h, const StairUp& up) {
    const int n = int(c.cols.size());
    if (up.eggsPerCell <= 0 || up.maxUp < 1 || z >= n) return z;
    const Col& wall = c.cols[size_t(z)];
    if (!wall.g.empty() || wall.top || wall.h <= h) return z;
    const double cap = Level::kBudgetCap, margin = Level::kBudgetMargin, epc = up.eggsPerCell;
    // what a careful player holds at the wall, as the checker walks it: 1 egg and 8 cells' refill after the maze, then the porch
    double b = std::min(cap, std::min(cap, 1.0 + 8 * epc) + Level::kPorch * epc);
    const int H = wall.h;
    if (H - h <= up.maxUp && b + 1e-6 >= (H - h) + margin) return z;
    // the climbs, a run-up before each (to refill it); a plateau at each height we pass on the way
    struct Step { int h, len; };
    std::vector<Step> steps;
    int cur = h;
    while (cur < H) {
        const int k = std::max(1, std::min({H - cur, up.maxUp, static_cast<int>(std::floor(cap - margin))}));
        const double need = k + margin;
        int len = std::max(up.minLen, static_cast<int>(std::ceil(std::max(0.0, need - b) / epc - 1e-9)));
        len = std::min(len, Level::kMaxRunup + up.minLen);
        b = std::min(cap, b + len * epc);
        b = std::max(0.0, b - k);
        steps.push_back({cur, len});
        cur += k;
    }
    const WallKind kind = z > 0 ? c.cols[size_t(z - 1)].kind : WallKind::Crate;
    std::vector<Col> add;
    for (size_t s = 0; s < steps.size(); ++s)
        for (int i = 0; i < steps[s].len; ++i) {
            Col k;
            k.h = steps[s].h;
            k.kind = kind;
            k.cut = s > 0 && i == 0; // (the first plateau is the porch's own: no cut)
            add.push_back(k);
        }
    shiftItems(c, z * U, int(add.size()) * U);
    c.cols.insert(c.cols.begin() + z, add.begin(), add.end());
    c.length = mm(c.length + int(add.size()) * U);
    c.cols[size_t(z) + add.size()].cut = true; // (the wall: whole)
    return z + int(add.size());
}

// Is a saw, lava or spring the stair's walls were raised under still fair where it stands? As the checker (LevelJudge, the
// hazards' own checks) finds it, at the strictest margins: on one flat, a landing's run after a drop (rules::sawAfterDrop), a
// reaction's run before the next wall. The stair puts a step's edge, or its drop, wherever the course's own hazards were.
bool fairOnStair(const Course& c, const Placed& p, double speed) {
    const int n = int(c.cols.size());
    const bool saw = p.type == &kSawHazard, lava = p.type == &kLavaHazard;
    const double x = p.x(), xe = lava ? x + p.at(1) * U : x;
    const int i = colAt(x);
    if (i < 0 || i >= n || !c.cols[size_t(i)].g.empty()) return true;
    int s0 = i, s1 = i + 1; // (the wall it stands on: like columns, no cut between)
    while (s0 > 0 && !c.cols[size_t(s0)].cut && alike(c.cols[size_t(s0 - 1)], c.cols[size_t(s0)])) --s0;
    while (s1 < n && !c.cols[size_t(s1)].cut && alike(c.cols[size_t(s1 - 1)], c.cols[size_t(s1)])) ++s1;
    const double reach = saw ? Level::kSawReach : lava ? 0.05 : 0.3; // (the flat the checker wants either side of it)
    if (x - reach < s0 * U - 1e-6 || xe + reach > s1 * U + 1e-6) return false;
    const int floor = c.cols[size_t(i)].h;
    if (s0 > 0 && c.cols[size_t(s0 - 1)].g.empty() && c.cols[size_t(s0 - 1)].h > floor) {
        const double need = rules::sawAfterDrop(speed, speed * 0.6, c.cols[size_t(s0 - 1)].h - floor);
        if (x - (saw ? Level::kSawReach : 0.0) - s0 * U < need - 1e-6) return false;
    }
    if (s1 < n && (saw || lava) && s1 * U - xe < std::max(2.5 * U, speed * 0.45 + (saw ? Level::kSawR : 0.0)) - 1e-6) return false;
    return true;
}

bool placeGauntlet(Course& c, const std::string& name, const GridInfo& grid, int at, int forecourt, double speed, const StairUp& up) {
    at = std::max(forecourt, at);
    const int a = at - forecourt, z = at + grid.cols + Level::kPorch;
    Course was = c;
    removeGauntlet(c, name, forecourt);
    for (int i = a; i < std::min(z, int(c.cols.size())); ++i)
        if (!c.cols[size_t(i)].g.empty()) { c = was; return false; } // (another gauntlet's in the way)
    while (int(c.cols.size()) < z) c.cols.push_back(street());
    const bool lowDoor = grid.entryTop - grid.entryRow - Gauntlet::kBodyRows < int(Level::kBudgetCap); // (rules::doorNeedsLintel)
    // (at the height of the wall before it: one with it, its kind, so a landing onto it is whole)
    const Col before = a > 0 ? c.cols[size_t(a - 1)] : Col{};
    const bool onFrom = a > 0 && before.g.empty() && before.h == grid.entryRow && !before.ceil && !before.top;
    for (int i = a; i < at; ++i) {
        Col k;
        k.h = grid.entryRow;
        if (onFrom) k.kind = before.kind;
        k.ceil = lowDoor && i >= at - Level::kLintel ? grid.entryTop : 0;
        k.cut = (i == a && !(a > 0 && alike(c.cols[size_t(a - 1)], k))) || i == at - Level::kLintel; // (one wall with a like one before it: its landing whole)
        c.cols[size_t(i)] = k;
    }
    for (int i = at; i < at + grid.cols; ++i) { Col k; k.g = name; k.cut = i == at; c.cols[size_t(i)] = k; }
    for (int i = at + grid.cols; i < z; ++i) { Col k; k.h = grid.exitRow; k.cut = i == at + grid.cols; c.cols[size_t(i)] = k; }
    if (z < int(c.cols.size())) c.cols[size_t(z)].cut = true;
    int stairEnd = speed > 0 ? stairDown(c, z, grid.exitRow, speed) : z;
    if (speed > 0 && up.eggsPerCell > 0) stairEnd = std::max(stairEnd, stairUp(c, z, grid.exitRow, up));
    const double x0 = a * U, x1 = z * U;
    c.length = std::max(c.length, mm(x1 + 6 * U));
    while (c.cols.size() * U < c.length - 1e-9) c.cols.push_back(street());
    c.items.erase(std::remove_if(c.items.begin(), c.items.end(),
                                 [&](const Placed& p) {
                                     return (p.x() >= x0 && p.x() < x1) || (p.type->offGauntlets && inGauntlet(c, p.x())) ||
                                            (p.x() >= x1 && p.x() <= stairEnd * U && // (the stair: the sides it raised over, what it buried; a saw, lava or spring it left unfair)
                                             (p.type->anchor == Anchor::Face || (p.type->anchor == Anchor::Free && p.at(1) < surfaceAt(c, p.x()) * U + 0.3) ||
                                              (p.type->anchor == Anchor::Surface && speed > 0 && !fairOnStair(c, p, speed))));
                                 }),
                  c.items.end());
    return !(c == was);
}

bool fitGauntlet(Course& c, const std::string& name, int width) {
    const std::optional<Span> s = spanOf(c, name);
    if (!s || s->c1 - s->c0 == width || width < 4) return false;
    const double shift = (width - (s->c1 - s->c0)) * U, at = s->c1 * U;
    std::vector<Col> span(static_cast<size_t>(width), Col{});
    for (int i = 0; i < width; ++i) { span[size_t(i)].g = name; span[size_t(i)].cut = i == 0; }
    std::vector<Col> cols(c.cols.begin(), c.cols.begin() + s->c0);
    cols.insert(cols.end(), span.begin(), span.end());
    cols.insert(cols.end(), c.cols.begin() + s->c1, c.cols.end());
    c.cols = std::move(cols);
    shiftItems(c, at, shift);
    if (c.length >= at - 1e-9) c.length = mm(c.length + shift);
    return true;
}

std::optional<double> startOnCourse(const Course& c, double x) {
    if (!(x >= 0) || c.length < 10) return std::nullopt;
    double s = std::round(std::max(1.0, std::min(x, c.length - 5)) * 10) / 10;
    auto nearSaw = [&](double at) -> const Placed* {
        for (const Placed& p : c.items) if (p.type == &kSawHazard && std::abs(p.x() - at) < 1) return &p;
        return nullptr;
    };
    for (int pass = 0; pass < 4; ++pass) {
        const Placed* n = nearSaw(s);
        if (!n) break;
        s = std::round((s < n->x() ? n->x() - 1.1 : n->x() + 1.1) * 10) / 10; // (to its nearer side)
    }
    if (s < 0 || s > c.length - 1 || inGauntlet(c, s) || nearSaw(s)) return std::nullopt;
    return s;
}

} // namespace cs::ed
