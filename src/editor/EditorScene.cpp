#include "EditorScene.h"

#include "core/CourseRules.h"
#include "core/Json.h"
#include "core/Zone.h"

#include <algorithm>

namespace cs::ed {

namespace {

std::string str(const std::string& s) { return "\"" + jsonEsc(s) + "\""; }

std::string notes(const std::vector<Note>& v, const char* kind) {
    std::string o;
    for (const Note& n : v)
        o += std::string(o.empty() ? "" : ",") + "{\"x\":" + levelNum(n.x) + ",\"y\":" + levelNum(n.y * U) + ",\"text\":" + str(n.text) + ",\"kind\":\"" + kind + "\"}";
    return o;
}

} // namespace

std::string sceneJson(const EditorDoc& doc, const EditorTools* tools) {
    const Course& c = doc.course();
    const Zone z = zoneOf(c.number);
    int rows = 4;
    for (const Col& k : c.cols) rows = std::max({rows, k.h, k.top, k.ceil ? k.ceil + 1 : 0});
    std::string o = "{\"number\":" + std::to_string(c.number) + ",\"zone\":\"" + (z == Zone::Egypt ? "egypt" : z == Zone::Japan ? "japan" : "city") +
                    "\",\"cell\":" + levelNum(U) + ",\"length\":" + levelNum(c.length) + ",\"courseLength\":" + levelNum(courseLength(c)) +
                    ",\"revision\":" + std::to_string(doc.revision());
    o += ",\"walls\":[";
    bool first = true;
    for (size_t i = 0; i < c.cols.size();) {
        size_t j = i + 1;
        while (j < c.cols.size() && !c.cols[j].cut && alike(c.cols[i], c.cols[j])) ++j;
        const Col& k = c.cols[i];
        if (k.g.empty()) {
            o += std::string(first ? "" : ",") + "[" + std::to_string(i) + "," + std::to_string(j) + "," + std::to_string(k.h) + ",\"" + wallKindInfo(k.kind).id +
                 "\"," + std::to_string(k.ceil) + "," + std::to_string(k.top) + "]";
            first = false;
        }
        i = j;
    }
    o += "],\"gauntlets\":[";
    first = true;
    for (const Span& s : spans(c)) {
        o += std::string(first ? "" : ",") + "{\"name\":" + str(s.name) + ",\"c0\":" + std::to_string(s.c0) + ",\"c1\":" + std::to_string(s.c1);
        if (const Gauntlet* g = doc.catalog().gauntlet(s.name)) {
            rows = std::max(rows, g->rows());
            o += ",\"entryRow\":" + std::to_string(g->entryRow()) + ",\"exitRow\":" + std::to_string(g->exitRow()) + ",\"rows\":[";
            const std::vector<std::string> grid = g->rowsTopFirst();
            for (size_t r = 0; r < grid.size(); ++r) o += std::string(r ? "," : "") + str(grid[r]);
            o += "]";
        }
        o += "}";
        first = false;
    }
    o += "],\"items\":[";
    for (size_t i = 0; i < c.items.size(); ++i) {
        const Placed& p = c.items[i];
        o += std::string(i ? "," : "") + "{\"i\":" + std::to_string(i) + ",\"type\":" + str(p.type->id) + ",\"x\":" + levelNum(p.x()) + ",\"y\":" + levelNum(itemY(c, p)) +
             (p.type->spanParam >= 0 ? ",\"x1\":" + levelNum(itemEnd(p)) : std::string()) +
             (p.type->anchor == Anchor::Face ? ",\"reach\":[" + levelNum(faceReach(c, p).first) + "," + levelNum(faceReach(c, p).second) + "]" : std::string()) + ",\"v\":[";
        for (size_t k = 0; k < p.v.size(); ++k) o += std::string(k ? "," : "") + levelNum(p.v[k]);
        o += "]";
        if (p.type == &kSpringHazard) // its flight at the curve's speed (the game's, CourseRules.h): its arc, and how it ends
            for (const Spring& sp : doc.level().springs) {
                if (std::abs(sp.x - p.x()) > 1e-6) continue;
                if (doc.level().zoneAt(sp.x)) break; // (in a bounce zone: flown by its grid's rules, no course arc)
                const rules::Flight f = rules::springFlight(doc.level(), sp, doc.level().curveAt(sp.x).speed);
                static const char* ends[] = {"landed", "wall", "barrier", "off"};
                o += ",\"end\":\"" + std::string(ends[int(f.end)]) + "\",\"arc\":[";
                for (size_t k = 0; k + 1 < f.path.size(); k += 2) o += std::string(k ? "," : "") + "[" + levelNum(std::round(f.path[k] * 100) / 100) + "," + levelNum(std::round(f.path[k + 1] * 100) / 100) + "]";
                o += "]";
                break;
            }
        o += "}";
    }
    const Report& r = doc.report();
    std::string n = notes(r.notes, "note"), s = notes(r.short_, "short"), d = notes(r.doors, "door");
    std::string all = n;
    for (const std::string* part : {&s, &d}) if (!part->empty()) all += (all.empty() ? "" : ",") + *part;
    if (doc.chunkMode() != EditorDoc::ChunkMode::None) { // (a chunk: what breaks its rules, and the checker's word on it flipped)
        double at = 0;
        const std::string problem = doc.chunkProblem(&at);
        std::string extra;
        if (!problem.empty()) extra = notes({{at, 0, problem}}, "chunk");
        if (const Report* f = doc.flippedReport()) {
            const double len = doc.asChunk().reach();
            auto flipped = [&](const std::vector<Note>& v, const char* kind) {
                std::vector<Note> out;
                for (const Note& note : v) out.push_back({std::max(0.0, len - note.x), note.y, "Flipped: " + note.text});
                const std::string part = notes(out, kind);
                if (!part.empty()) extra += (extra.empty() ? "" : ",") + part;
            };
            flipped(f->notes, "note");
            flipped(f->short_, "short");
        }
        if (!extra.empty()) all += (all.empty() ? "" : ",") + extra;
    }
    o += "],\"notes\":[" + all + "],\"walk\":[";
    for (size_t i = 0; i < r.walk.size(); ++i) o += std::string(i ? "," : "") + "[" + levelNum(r.walk[i][0]) + "," + levelNum(r.walk[i][1]) + "]";
    o += "],\"lowest\":" + levelNum(r.lowest) + ",\"rows\":" + std::to_string(rows);
    o += ",\"start\":" + (doc.start ? levelNum(*doc.start) : std::string("null"));
    if (tools) {
        o += ",\"selection\":" + (tools->selC0 >= 0 ? "[" + std::to_string(std::min(tools->selC0, tools->selC1)) + "," + std::to_string(std::max(tools->selC0, tools->selC1)) + "]" : std::string("null"));
        o += ",\"picked\":" + std::to_string(tools->picked);
        const EditorTools::Preview& p = tools->preview();
        o += ",\"preview\":" + (p.kind.empty() ? std::string("null")
                                               : "{\"kind\":" + str(p.kind) + ",\"c0\":" + std::to_string(p.c0) + ",\"c1\":" + std::to_string(p.c1) + ",\"r0\":" + std::to_string(p.r0) +
                                                     ",\"r1\":" + std::to_string(p.r1) + ",\"erase\":" + (p.erase ? "true" : "false") + "}");
    }
    return o + "}";
}

std::string hitJson(const EditorDoc& doc, double x, double y) {
    const Course& c = doc.course();
    const int i = itemNear(c, nullptr, x, y, 0.6);
    if (i >= 0) {
        const Placed& p = c.items[size_t(i)];
        std::string o = "{\"what\":\"item\",\"i\":" + std::to_string(i) + ",\"type\":" + str(p.type->id) + ",\"v\":[";
        for (size_t k = 0; k < p.v.size(); ++k) o += std::string(k ? "," : "") + levelNum(p.v[k]);
        return o + "]}";
    }
    const int col = colAt(x);
    if (col < 0 || col >= int(c.cols.size())) return "{\"what\":\"none\"}";
    const Col& k = c.cols[size_t(col)];
    if (!k.g.empty()) return "{\"what\":\"gauntlet\",\"name\":" + str(k.g) + ",\"col\":" + std::to_string(col) + "}";
    return "{\"what\":\"wall\",\"col\":" + std::to_string(col) + ",\"h\":" + std::to_string(k.h) + ",\"kind\":\"" + wallKindInfo(k.kind).id + "\",\"ceil\":" +
           std::to_string(k.ceil) + ",\"top\":" + std::to_string(k.top) + "}";
}

std::string historyJson(const EditorDoc& doc) {
    auto steps = [](const std::vector<EditorDoc::Step>& v) {
        std::string o;
        for (const EditorDoc::Step& s : v)
            o += std::string(o.empty() ? "" : ",") + "{\"label\":" + str(s.label) + ",\"revision\":" + std::to_string(s.revision) + ",\"time\":" +
                 std::to_string(s.time) + ",\"at\":" + levelNum(s.at) + "}";
        return "[" + o + "]";
    };
    return "{\"undo\":" + steps(doc.history()) + ",\"redo\":" + steps(doc.redoSteps()) + ",\"canUndo\":" + (doc.canUndo() ? "true" : "false") +
           ",\"canRedo\":" + (doc.canRedo() ? "true" : "false") + "}";
}

std::string metaJson(const EditorDoc& doc) {
    const Course& c = doc.course();
    std::string o = "{\"number\":" + std::to_string(c.number) + ",\"notes\":[";
    for (size_t i = 0; i < c.notes.size(); ++i) o += std::string(i ? "," : "") + str(c.notes[i]);
    o += "],\"length\":" + levelNum(c.length) + ",\"diffOffset\":" + levelNum(c.diffOffset) + ",\"diffScale\":" + levelNum(c.diffScale) + ",\"raids\":" +
         (c.raids < 0 ? std::string("\"auto\"") : c.raids == 0 ? std::string("\"none\"") : std::to_string(c.raids));
    const std::vector<Span> sp = spans(c);
    o += ",\"gauntlet\":" + (sp.empty() ? std::string("null") : str(sp.front().name));
    o += ",\"gauntlets\":[";
    const std::vector<std::string> names = doc.catalog().gauntletNames();
    for (size_t i = 0; i < names.size(); ++i) o += std::string(i ? "," : "") + str(names[i]);
    return o + "],\"error\":" + str(doc.lastError()) + "}";
}

} // namespace cs::ed
