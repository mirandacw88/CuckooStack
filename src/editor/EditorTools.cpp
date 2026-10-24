#include "EditorTools.h"

#include <algorithm>
#include <cmath>

namespace cs::ed {

namespace {

std::string at(int col) { return " at " + levelNum(mm(col * U)) + " m"; }


} // namespace

std::string describeKind(WallKind k) { return wallKindInfo(k).label; }

bool EditorTools::wallsAlong(int from, int to, const std::function<bool(Course&, int)>& each, const std::string& label) {
    const int a = std::min(from, to), b = std::max(from, to);
    return doc_.edit(label, [&](Course& c) {
        bool any = false;
        for (int k = a; k <= b; ++k) any = each(c, k) || any;
        return any;
    });
}

// The eraser, along the columns the finger crossed at its height: a hazard or a pickup near it; else in a column, the
// block or barrier it's at (or over), else the walls under it, down to the street. Not a gauntlet's span (the Gauntlet
// tool takes a maze out)
bool EditorTools::eraseAt(int from, int to, double x, double y) {
    const Course& course = doc_.course();
    const int i = itemNear(course, nullptr, x, y, 0.6);
    if (i >= 0) {
        const Placed& p = course.items[size_t(i)];
        erased_.insert(colAt(x));
        return doc_.edit(std::string("Erased a ") + p.type->label + " at " + levelNum(p.x()) + " m", [&](Course& c) { return removeItem(c, i); });
    }
    const double eggs = y / U; // (the finger's height, in eggs)
    const int a = std::min(from, to), b = std::max(from, to);
    std::string what;
    const bool done = doc_.edit("Erased" + at(a), [&](Course& c) {
        bool any = false;
        for (int k = a; k <= b; ++k) {
            const Col& col = c.cols[size_t(k)];
            if (!col.g.empty() || erased_.count(k)) continue;
            bool did = false;
            if (col.ceil && eggs >= col.ceil - 0.5) did = col.top ? setBlock(c, k, 0, 0) : setCeil(c, k, 0); // (at it, or over it)
            else if (col.h && eggs < col.h + 0.5) did = setHeight(c, k, 0);
            if (did) erased_.insert(k);
            any = did || any;
        }
        return any;
    });
    return done;
}

bool EditorTools::pointer(Phase phase, double x, double y) {
    const Course& course = doc_.course();
    if (course.cols.empty()) return false;
    const int col = std::clamp(colAt(x), 0, int(course.cols.size()) - 1);
    const int row = std::max(0, int(std::lround(y / U)));
    if (phase == Phase::Cancel) {
        const bool was = doc_.inGesture() && !(doc_.course() == doc_.gestureBase());
        doc_.restoreGestureBase();
        doc_.endGesture();
        preview_ = {};
        dragging_ = -1;
        return was;
    }
    if (phase == Phase::Begin) {
        doc_.beginGesture();
        lastCol_ = startCol_ = col;
        startRow_ = row;
        erased_.clear();
        preview_ = {};
        dragging_ = -1;
    }
    bool changed = false;
    const HazardType* hazard = hazardType(tool_);
    if (erase_) {
        changed = eraseAt(lastCol_, col, x, y);
        picked = -1;
    } else if (tool_ == "wall") {
        changed = wallsAlong(lastCol_, col, [&](Course& c, int k) { return setHeight(c, k, row, kind_); }, "Walls " + std::to_string(row) + " high" + at(startCol_));
    } else if (const WallKindInfo* b = wallKindNamed(tool_); b && b->bouncer) { // a bumper or a gate, as high as the finger
        changed = wallsAlong(lastCol_, col, [&](Course& c, int k) { return setBouncer(c, k, std::max(1, row), b->kind); },
                             std::string(b->label) + " " + std::to_string(std::max(1, row)) + " high" + at(startCol_));
    } else if (tool_ == "kind") {
        changed = wallsAlong(lastCol_, col, [&](Course& c, int k) { return c.cols[size_t(k)].h > 0 && ed::setKind(c, k, kind_); }, describeKind(kind_) + at(startCol_));
    } else if (tool_ == "barrier") {
        changed = wallsAlong(lastCol_, col, [&](Course& c, int k) { return setCeil(c, k, row); }, "Barrier at " + std::to_string(row) + at(startCol_));
    } else if (tool_ == "block") {
        preview_ = {"block", std::min(startCol_, col), std::max(startCol_, col), std::min(startRow_, row), std::max(startRow_, row)};
        if (phase == Phase::End) {
            const int bottom = preview_.r0, top = std::max(preview_.r1, preview_.r0 + 1);
            changed = wallsAlong(preview_.c0, preview_.c1, [&](Course& c, int k) { return setBlock(c, k, bottom, top, kind_); },
                                 "Block " + std::to_string(bottom) + " to " + std::to_string(top) + at(preview_.c0));
        }
    } else if (tool_ == "cols") {
        preview_ = {"cols", std::min(startCol_, col), std::max(startCol_, col), 0, 0};
        if (phase == Phase::End) {
            const int a = preview_.c0, b = preview_.c1, n = b - a + 1;
            changed = doc_.edit("Copied in " + std::to_string(n) + (n == 1 ? " column" : " columns") + at(a), [&](Course& c) { return duplicateCols(c, a, b); });
        }
    } else if (tool_ == "select") {
        if (phase == Phase::Begin) {
            picked = itemNear(course, nullptr, x, y, 0.6);
            if (picked < 0) selC0 = selC1 = col;
        } else if (picked < 0) selC1 = col;
    } else if (hazard) {
        const double tol = 0.6;
        if (phase == Phase::Begin) {
            const int i = itemNear(course, hazard, x, y, tol);
            if (i >= 0) {
                dragging_ = picked = i;
            } else {
                std::vector<double> v;
                for (const ParamSpec& p : hazard->params) v.push_back(p.def);
                v[0] = snapX(course, *hazard, x);
                if (hazard->anchor == Anchor::Free && v.size() > 1) v[1] = std::max(0.2, mm(y));
                if (hazard->anchor == Anchor::Face) { // (set into the wall's or block's side nearest the finger, at its row)
                    const auto f = faceNear(course, x, y);
                    if (!f) { picked = -1; lastCol_ = col; return false; }
                    v = *f;
                }
                int added = -1;
                changed = doc_.edit(std::string("Put in a ") + hazard->label + " at " + levelNum(mm(v[0])) + " m", [&](Course& c) { return (added = addItem(c, *hazard, v)) >= 0; });
                dragging_ = picked = added;
            }
        } else if (phase == Phase::Move && dragging_ >= 0 && dragging_ < int(course.items.size())) {
            std::vector<double> v = course.items[size_t(dragging_)].v;
            v[0] = snapX(course, *hazard, x, dragging_);
            if (hazard->anchor == Anchor::Free && v.size() > 1) v[1] = std::max(0.2, mm(y));
            if (hazard->anchor == Anchor::Face) { // (along to the side nearest the finger, at its row)
                const auto f = faceNear(course, x, y);
                if (f) v = *f;
                else v = course.items[size_t(dragging_)].v;
            }
            int moved = -1;
            if (doc_.edit(std::string("Moved a ") + hazard->label, [&](Course& c) { return (moved = moveItemTo(c, dragging_, v)) >= 0; })) {
                changed = true;
                dragging_ = picked = moved;
            }
        }
    } else if (tool_ == "gauntlet") {
        if (phase == Phase::Begin) {
            grab_ = -1;
            for (const Span& s : spans(course))
                if (col >= s.c0 && col < s.c1) { grab_ = col - s.c0; gauntlet_ = s.name; }
            if (grab_ < 0 && !gauntlet_.empty()) changed = doc_.placeGauntlet(gauntlet_, col); // (the picked one, put in here)
        }
        if (grab_ >= 0 && !gauntlet_.empty()) {
            const int door = std::max(0, col - grab_);
            const std::optional<GridInfo> grid = doc_.gridInfo(gauntlet_);
            const int fore = doc_.forecourtAt(door * U);
            if (grid) preview_ = {"gauntlet", door - fore, door + grid->cols + Level::kPorch, grid->entryRow, grid->exitRow, false};
            if (phase == Phase::End) changed = doc_.placeGauntlet(gauntlet_, door);
        }
    } else if (tool_ == "start") {
        doc_.start = startOnCourse(course, x);
    }
    lastCol_ = col;
    if (phase == Phase::End) {
        doc_.endGesture();
        preview_ = {};
        dragging_ = -1;
    }
    return changed;
}

bool EditorTools::copySelection() {
    if (selC0 < 0) return false;
    clip_ = extractCols(doc_.course(), selC0, selC1);
    return clip_.has_value();
}

bool EditorTools::cutSelection() { return copySelection() && deleteSelection(); }

bool EditorTools::pasteAt(int col) {
    if (!clip_) return false;
    const int n = int(clip_->cols.size());
    return doc_.edit("Pasted " + std::to_string(n) + (n == 1 ? " column" : " columns") + at(col), [&](Course& c) { return insertCols(c, col, *clip_); });
}

bool EditorTools::deleteSelection() {
    if (selC0 < 0) return false;
    const int a = std::min(selC0, selC1), b = std::max(selC0, selC1), n = b - a + 1;
    const bool done = doc_.edit("Took out " + std::to_string(n) + (n == 1 ? " column" : " columns") + at(a), [&](Course& c) { return deleteCols(c, a, b, doc_.minLength()); });
    if (done) selC0 = selC1 = -1;
    return done;
}

bool EditorTools::setPickedParam(size_t param, double value) {
    if (picked < 0 || picked >= int(doc_.course().items.size())) return false;
    const Placed& p = doc_.course().items[size_t(picked)];
    if (param >= p.type->params.size()) return false;
    std::vector<double> v = p.v;
    v[param] = value;
    int moved = -1;
    const bool done = doc_.edit(std::string(p.type->label) + ": " + p.type->params[param].label + " " + levelNum(mm(value)),
                                [&](Course& c) { return (moved = moveItemTo(c, picked, v)) >= 0; });
    if (done) picked = moved;
    return done;
}

bool EditorTools::removePicked() {
    if (picked < 0 || picked >= int(doc_.course().items.size())) return false;
    const Placed p = doc_.course().items[size_t(picked)];
    const int i = picked;
    const bool done = doc_.edit(std::string("Took out a ") + p.type->label + " at " + levelNum(p.x()) + " m", [&](Course& c) { return removeItem(c, i); });
    if (done) picked = -1;
    return done;
}

} // namespace cs::ed
