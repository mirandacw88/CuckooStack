#include "EditorDoc.h"

#include "core/LevelBake.h"
#include "core/ReportJson.h"

#include "core/Json.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace cs::ed {

namespace {

// where an edit happened: the first column it changed (its middle), else the first hazard it put in or took out; -1
// for its settings alone
double changedAt(const Course& a, const Course& b) {
    const size_t n = std::min(a.cols.size(), b.cols.size());
    for (size_t i = 0; i < n; ++i)
        if (!(a.cols[i] == b.cols[i])) return (i + 0.5) * U;
    if (a.cols.size() != b.cols.size()) return (n + 0.5) * U;
    for (const Placed& p : b.items) if (std::find(a.items.begin(), a.items.end(), p) == a.items.end()) return p.x();
    for (const Placed& p : a.items) if (std::find(b.items.begin(), b.items.end(), p) == b.items.end()) return p.x();
    return -1;
}

} // namespace

std::unique_ptr<EditorDoc> EditorDoc::open(int number, const std::string& text, std::string* error, const std::vector<Grid>& grids) {
    std::unique_ptr<EditorDoc> doc(new EditorDoc());
    doc->cat_ = std::make_unique<LevelCatalog>();
    for (const Grid& g : grids) doc->cat_->setGauntlet(g.name, g.rows); // (one that doesn't parse: the built-in one)
    const auto d = parseLevel(number, text, error, doc->cat_.get());
    if (!d) return nullptr;
    doc->course_ = fromData(*d);
    return doc;
}

void EditorDoc::setChunkMode(ChunkMode m) {
    if (m == chunk_) return;
    chunk_ = m;
    changed(); // (its notes are not the same)
}

Chunk EditorDoc::asChunk() const {
    Chunk c;
    c.id = "x";
    c.ranges = {{course_.number, course_.number}};
    c.body = toData(course_);
    return c;
}

std::string EditorDoc::chunkProblem(double* at) const {
    if (at) *at = 0;
    return chunk_ == ChunkMode::None ? std::string() : chunkBodyProblem(asChunk(), at);
}

bool EditorDoc::setNumber(int n) {
    if (n < 1 || n == course_.number) return false;
    course_.number = gestureBase_.number = n;
    for (Snapshot& s : undo_) s.course.number = n;
    for (Snapshot& s : redo_) s.course.number = n;
    changed();
    return true;
}

const Report* EditorDoc::flippedReport() const {
    if (chunk_ != ChunkMode::Flip) return nullptr;
    if (!flipJudged_) {
        flipJudged_ = true;
        const Chunk c = asChunk();
        if (c.canMirror() && c.body.course.size() >= 2) {
            Level l;
            l.load(mirrored(c).body, cat_.get());
            l.generateUntil(course_.length + 20);
            flipReport_ = std::make_unique<Report>(judge(l));
        }
    }
    return flipReport_.get();
}

std::string EditorDoc::flippedText() const {
    if (chunk_ != ChunkMode::Flip) return "";
    const Chunk c = asChunk();
    return c.canMirror() && c.body.course.size() >= 2 ? levelText(mirrored(c).body) : std::string();
}

std::string EditorDoc::text() const { return levelText(toData(course_)); }

void EditorDoc::changed() {
    ++revision_;
    level_.reset();
    report_.reset();
    flipReport_.reset();
    flipJudged_ = false;
}

bool EditorDoc::setGrid(const std::string& name, const std::vector<std::string>& rowsTopFirst) {
    if (!cat_->setGauntlet(name, rowsTopFirst, &error_)) return false;
    if (const auto span = spanOf(course_, name); span && span->c1 - span->c0 != cat_->gauntlet(name)->cols()) // (its width: the course refitted)
        fitGauntlet(course_, name, cat_->gauntlet(name)->cols());
    changed();
    return true;
}

bool EditorDoc::renameGauntlet(const std::string& from, const std::string& to) {
    const Gauntlet* g = cat_->gauntlet(from);
    if (!g) { error_ = "there's no gauntlet called " + from; return false; }
    if (!validGauntletName(to)) { error_ = "a gauntlet's name: lower case letters, digits and _, a letter first"; return false; }
    if (to == from) return false;
    if (!cat_->setGauntlet(to, g->rowsTopFirst(), &error_)) return false;
    auto rename = [&](Course& c) { for (Col& col : c.cols) if (col.g == from) col.g = to; };
    rename(course_);
    rename(gestureBase_);
    for (Snapshot& s : undo_) rename(s.course);
    for (Snapshot& s : redo_) rename(s.course);
    changed();
    return true;
}

bool EditorDoc::edit(const std::string& label, const std::function<bool(Course&)>& change) {
    Course next = course_;
    if (!change(next) || next == course_) return false;
    std::string why;
    const auto read = parseLevel(next.number, levelText(toData(next)), &why, cat_.get());
    if (!read) { // (as the game will read it, or not at all)
        error_ = why;
        return false;
    }
    if (chunk_ != ChunkMode::None) { // (a chunk's body keeps a chunk's rules, or is already breaking them: the way to mend it stays open)
        Chunk c;
        c.id = "x";
        c.ranges = {{next.number, next.number}};
        c.body = *read;
        if (const std::string problem = chunkBodyProblem(c); !problem.empty() && chunkProblem().empty()) {
            error_ = problem;
            return false;
        }
    }
    error_.clear();
    if (!gesture_ || !gestureEdited_) {
        undo_.push_back({gesture_ ? gestureBase_ : course_, label, revision_, static_cast<long long>(std::time(nullptr)), changedAt(course_, next)});
        if (undo_.size() > 300) undo_.erase(undo_.begin()); // (kept on the device too: the oldest go)
        redo_.clear();
        gestureEdited_ = gesture_;
    }
    course_ = std::move(next);
    changed();
    return true;
}

void EditorDoc::beginGesture() {
    gesture_ = true;
    gestureEdited_ = false;
    gestureBase_ = course_;
}

void EditorDoc::restoreGestureBase() {
    if (!gesture_ || course_ == gestureBase_) return;
    course_ = gestureBase_;
    changed();
}

void EditorDoc::endGesture() {
    if (gesture_ && gestureEdited_ && course_ == gestureBase_ && !undo_.empty()) undo_.pop_back(); // (back where it began: no step)
    gesture_ = gestureEdited_ = false;
}

bool EditorDoc::undo() {
    if (undo_.empty()) return false;
    endGesture();
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({course_, s.label, revision_, s.time, s.at});
    course_ = std::move(s.course);
    changed();
    return true;
}

bool EditorDoc::redo() {
    if (redo_.empty()) return false;
    endGesture();
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({course_, s.label, revision_, s.time, s.at});
    course_ = std::move(s.course);
    changed();
    return true;
}

std::vector<EditorDoc::Step> EditorDoc::history() const {
    std::vector<Step> out;
    for (const Snapshot& s : undo_) out.push_back({s.label, s.revision, s.time, s.at});
    return out;
}

std::vector<EditorDoc::Step> EditorDoc::redoSteps() const {
    std::vector<Step> out;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) out.push_back({it->label, it->revision, it->time, it->at});
    return out;
}

bool EditorDoc::goTo(size_t k) {
    bool moved = false;
    while (undo_.size() > k && undo()) moved = true;
    while (undo_.size() < k && redo()) moved = true;
    return moved;
}

bool EditorDoc::restore(const std::string& text, const std::string& label) {
    std::string why;
    const auto d = parseLevel(course_.number, text, &why, cat_.get());
    if (!d) { error_ = why; return false; }
    endGesture();
    return edit(label, [&](Course& c) { c = fromData(*d); return true; });
}

std::string EditorDoc::historyExport() const {
    auto steps = [](const std::vector<Snapshot>& v, bool reversed) {
        std::string o;
        auto one = [&](const Snapshot& s) {
            o += std::string(o.empty() ? "" : ",") + "{\"label\":\"" + jsonEsc(s.label) + "\",\"time\":" + std::to_string(s.time) + ",\"at\":" + levelNum(s.at) +
                 ",\"text\":\"" + jsonEsc(levelText(toData(s.course))) + "\"}";
        };
        if (reversed) for (auto it = v.rbegin(); it != v.rend(); ++it) one(*it);
        else for (const Snapshot& s : v) one(s);
        return "[" + o + "]";
    };
    return "{\"current\":\"" + jsonEsc(text()) + "\",\"undo\":" + steps(undo_, false) + ",\"redo\":" + steps(redo_, true) + "}";
}

void EditorDoc::clearHistory() {
    undo_.clear();
    redo_.clear();
}

bool EditorDoc::addHistory(bool redo, const std::string& label, long long time, double at, const std::string& text) {
    const auto d = parseLevel(course_.number, text, &error_, cat_.get());
    if (!d) return false;
    Snapshot s{fromData(*d), label, revision_, time, at};
    if (redo) redo_.insert(redo_.begin(), std::move(s)); // (given next first: the stack's top is its back)
    else undo_.push_back(std::move(s));
    return true;
}

std::vector<std::string> EditorDoc::redoLabels() const {
    std::vector<std::string> out;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) out.push_back(it->label);
    return out;
}

bool EditorDoc::regenerate(uint32_t seed, const GenOptions& options) {
    std::vector<ChunkUse> used;
    std::vector<std::string> dropped;
    const LevelData d = bakeLevel(course_.number, nullptr, seed, options, &used, &dropped);
    start.reset();
    std::string chunkWords = used.empty() ? "" : "chunks: " + chunkUsesLabel(used);
    if (!dropped.empty()) {
        std::string ids;
        for (const std::string& id : dropped) ids += (ids.empty() ? "" : ", ") + id;
        chunkWords += std::string(chunkWords.empty() ? "" : "; ") + "left out: " + ids;
    }
    const std::string label = "A new course (seed " + std::to_string(seed) + (options.density ? ", density " + std::to_string(options.density) : std::string()) + ")" +
                              (chunkWords.empty() ? std::string() : ": " + chunkWords);
    const bool ok = edit(label, [&](Course& c) {
        const std::vector<std::string> notes = c.notes;
        c = fromData(d);
        c.notes = notes;
        return true;
    });
    if (ok) { rolled_ = used; rolledDropped_ = dropped; rolledLevel_ = course_.number; rollRevision_ = revision_; }
    return ok;
}

bool EditorDoc::setMeta(const std::string& key, const std::string& value) {
    std::istringstream in(value);
    if (key == "notes") {
        std::vector<std::string> notes;
        for (std::string line; std::getline(in, line);) notes.push_back(line);
        return edit("Notes", [&](Course& c) { c.notes = notes; return true; });
    }
    if (key == "length") {
        double x = 0;
        if (!(in >> x)) return false;
        return edit("Finish line at " + levelNum(mm(x)) + " m", [&](Course& c) { return setLength(c, x, minLength()); });
    }
    if (key == "difficulty") {
        double offset = 0, scale = 0;
        if (!(in >> offset >> scale) || offset < 0 || scale < 0) return false;
        return edit("Difficulty " + levelNum(offset) + " " + levelNum(scale), [&](Course& c) { c.diffOffset = offset; c.diffScale = scale; return true; });
    }
    if (key == "raids") {
        const int raids = value == "auto" ? -1 : value == "none" ? 0 : std::atoi(value.c_str());
        if (raids < -1 || raids > 6 || (raids == 0 && value != "none" && value != "0")) return false;
        return edit("Raids: " + value, [&](Course& c) { c.raids = raids; return true; });
    }
    return false;
}

std::optional<GridInfo> EditorDoc::gridInfo(const std::string& name) const {
    const Gauntlet* g = cat_->gauntlet(name);
    if (!g) return std::nullopt;
    return GridInfo{g->cols(), g->entryRow(), g->entryTop(), g->exitRow()};
}

int EditorDoc::forecourtAt(double x) const {
    Level l;
    l.setDifficulty(course_.diffOffset, course_.diffScale);
    return l.forecourtAt(x);
}

bool EditorDoc::placeGauntlet(const std::string& name, int at) {
    const std::optional<GridInfo> grid = gridInfo(name);
    if (!grid) { error_ = "no gauntlet called " + name; return false; }
    const int fore = forecourtAt(at * U);
    Level l;
    l.setDifficulty(course_.diffOffset, course_.diffScale);
    const double speed = l.curveAt((at + grid->cols + Level::kPorch) * U).speed; // (off its porch: a fair stair down)
    const double xPorch = (at + grid->cols + Level::kPorch) * U;
    const ed::StairUp up{l.eggsPerCell(xPorch), l.curveAt(xPorch).maxUp, l.curveAt(xPorch).minLen + 1}; // (...or, onto a wall too tall to climb, up)
    const bool moving = spanOf(course_, name).has_value();
    return edit((moving ? "Moved the " : "Placed the ") + name + " to " + levelNum(mm(at * U)) + " m",
                [&](Course& c) {
                    for (const Span& s : spans(c)) if (s.name != name) ed::removeGauntlet(c, s.name, forecourtAt(s.c0 * U)); // (one a level)
                    return ed::placeGauntlet(c, name, *grid, at, fore, speed, up);
                });
}

bool EditorDoc::removeGauntlet(const std::string& name) {
    const std::optional<Span> s = spanOf(course_, name);
    if (!s) return false;
    const int fore = forecourtAt(s->c0 * U);
    return edit("Took out the " + name, [&](Course& c) { return ed::removeGauntlet(c, name, fore); });
}

const Level& EditorDoc::level() const {
    if (!level_) {
        level_ = std::make_unique<Level>();
        level_->load(toData(course_), cat_.get());
        level_->generateUntil(course_.length + 20);
    }
    return *level_;
}

const Report& EditorDoc::report() const {
    if (!report_) report_ = std::make_unique<Report>(judge(level()));
    return *report_;
}

std::string EditorDoc::reportJson() const { return levelReportJson(toData(course_), false, cat_.get()); }

} // namespace cs::ed
