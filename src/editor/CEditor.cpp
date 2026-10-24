// cs_editor.h over the editor's C++ (EditorDoc, EditorTools, EditorScene, BotJob, LevelMerge)
#include "cs_editor.h"

#include "BotJob.h"
#include "EditorDoc.h"
#include "EditorScene.h"
#include "EditorTools.h"
#include "GridDoc.h"
#include "GridJob.h"
#include "LevelMerge.h"
#include "MazeGen.h"
#include "core/Chunk.h"
#include "core/Hazards.h"
#include "core/LevelBake.h"
#include "core/LevelCatalog.h"
#include "core/ReportJson.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>

using namespace cs;
using namespace cs::ed;

struct ed_doc {
    std::unique_ptr<EditorDoc> doc;
    std::unique_ptr<EditorTools> tools;
};

struct ed_bot {
    BotJob job;
};

struct gd_doc {
    std::unique_ptr<GridDoc> doc;
    std::unique_ptr<GridTools> tools;
};

struct gd_job {
    GridJob job;
    std::map<std::string, std::vector<std::string>> grids;
};

namespace {

char* dup(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

std::vector<std::string> lines(const char* text) {
    std::vector<std::string> out;
    std::istringstream in(text ? text : "");
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != ';') out.push_back(line);
    }
    return out;
}

std::string str(const std::string& s) { return "\"" + jsonEsc(s) + "\""; }

} // namespace

extern "C" {

void ed_free(char* s) { std::free(s); }

char* ed_registry_json(void) { return dup(courseRulesJson()); }

char* ed_builtin_numbers_json(void) {
    std::string o = "[";
    for (int n : LevelCatalog::builtin().levelNumbers()) o += (o.size() > 1 ? "," : "") + std::to_string(n);
    return dup(o + "]");
}

char* ed_builtin_level(int number) {
    const LevelData* d = LevelCatalog::builtin().level(number);
    return d ? dup(levelText(*d)) : nullptr;
}

char* ed_builtin_grids_json(void) {
    std::string o = "{";
    for (const std::string& name : LevelCatalog::builtin().gauntletNames()) {
        o += (o.size() > 1 ? "," : "") + str(name) + ":[";
        const std::vector<std::string> rows = LevelCatalog::builtin().gauntlet(name)->rowsTopFirst();
        for (size_t r = 0; r < rows.size(); ++r) o += (r ? "," : "") + str(rows[r]);
        o += "]";
    }
    return dup(o + "}");
}

char* ed_bake_level(int number) { return dup(levelText(bakeLevel(number, nullptr))); }

ed_doc* ed_open(int number, const char* text, char** error) { return ed_open_grids(number, text, nullptr, error); }

ed_doc* ed_open_grids(int number, const char* text, const char* grids, char** error) {
    std::vector<EditorDoc::Grid> gs;
    std::istringstream in(grids ? grids : "");
    bool next = true; // (a blank line: the next line names the next grid)
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) next = true;
        else if (next) { gs.push_back({line, {}}); next = false; }
        else if (line[0] != ';') gs.back().rows.push_back(line);
    }
    std::string why;
    auto doc = EditorDoc::open(number, text ? text : "", &why, gs);
    if (!doc) {
        if (error) *error = dup(why);
        return nullptr;
    }
    auto* d = new ed_doc;
    d->doc = std::move(doc);
    d->tools = std::make_unique<EditorTools>(*d->doc);
    return d;
}

void ed_close(ed_doc* doc) { delete doc; }
char* ed_text(ed_doc* doc) { return dup(doc->doc->text()); }
int ed_number(ed_doc* doc) { return doc->doc->number(); }
int ed_revision(ed_doc* doc) { return doc->doc->revision(); }
char* ed_last_error(ed_doc* doc) { return dup(doc->doc->lastError()); }
int ed_set_grid(ed_doc* doc, const char* name, const char* rows) { return doc->doc->setGrid(name, lines(rows)); }

void ed_set_tool(ed_doc* doc, const char* tool) { doc->tools->setTool(tool); }
void ed_set_kind(ed_doc* doc, const char* kind) {
    if (const WallKindInfo* k = wallKindNamed(kind)) doc->tools->setKind(k->kind);
}
void ed_set_erase(ed_doc* doc, int erase) { doc->tools->setErase(erase != 0); }
void ed_set_gauntlet_pick(ed_doc* doc, const char* name) { doc->tools->setGauntlet(name ? name : ""); }
int ed_pointer(ed_doc* doc, int phase, double x, double y) { return doc->tools->pointer(static_cast<Phase>(phase), x, y); }
int ed_undo(ed_doc* doc) { return doc->doc->undo(); }
int ed_redo(ed_doc* doc) { return doc->doc->redo(); }
int ed_copy(ed_doc* doc) { return doc->tools->copySelection(); }
int ed_cut(ed_doc* doc) { return doc->tools->cutSelection(); }
int ed_paste(ed_doc* doc, int col) { return doc->tools->pasteAt(col); }
int ed_delete_selection(ed_doc* doc) { return doc->tools->deleteSelection(); }
void ed_select(ed_doc* doc, int c0, int c1) { doc->tools->selC0 = c0; doc->tools->selC1 = c0 < 0 ? -1 : c1; }
int ed_picked(ed_doc* doc) { return doc->tools->picked; }
void ed_pick(ed_doc* doc, int i) { doc->tools->picked = i >= 0 && i < int(doc->doc->course().items.size()) ? i : -1; }
int ed_set_picked_param(ed_doc* doc, int param, double value) { return param >= 0 && doc->tools->setPickedParam(size_t(param), value); }
int ed_remove_picked(ed_doc* doc) { return doc->tools->removePicked(); }
namespace {
std::vector<std::string> idsOf(const char* list) { // ("saw,laser" -> {saw, laser}; null or "": none, i.e. all)
    std::vector<std::string> out;
    std::istringstream in(list ? list : "");
    for (std::string id; std::getline(in, id, ',');) if (!id.empty()) out.push_back(id);
    return out;
}
} // namespace

int ed_regenerate(ed_doc* doc, unsigned seed, const char* hazards, int density) {
    GenOptions o;
    o.only = idsOf(hazards);
    o.density = std::clamp(density, 0, 10);
    if (o.only.empty() && hazards && std::string(hazards) == "none") o.only = {"-"}; // (nothing switched on)
    return doc->doc->regenerate(seed, o);
}
int ed_regenerate_chunks(ed_doc* doc, unsigned seed, const char* hazards, int density, const char* chunks) {
    GenOptions o;
    o.only = idsOf(hazards);
    o.density = std::clamp(density, 0, 10);
    if (o.only.empty() && hazards && std::string(hazards) == "none") o.only = {"-"};
    std::string why;
    if (!parseChunks(chunks ? chunks : "", o.chunks, &why)) { doc->doc->setError(why); return 0; } // (every chunk that reads is kept; one that doesn't stops the roll)
    return doc->doc->regenerate(seed, o);
}
char* ed_chunks_used_json(ed_doc* doc) {
    std::string o;
    for (const ChunkUse& u : doc->doc->chunksUsed())
        o += std::string(o.empty() ? "" : ",") + "{\"id\":" + str(u.id) + ",\"x0\":" + levelNum(u.x0) + ",\"x1\":" + levelNum(u.x1) + ",\"mirrored\":" + (u.mirrored ? "true" : "false") + "}";
    return dup("[" + o + "]");
}
char* ed_chunks_note(ed_doc* doc) { return dup(doc->doc->chunksNote()); }
void ed_set_chunk(ed_doc* doc, int mode) {
    doc->doc->setChunkMode(mode >= 2 ? EditorDoc::ChunkMode::Flip : mode == 1 ? EditorDoc::ChunkMode::Chunk : EditorDoc::ChunkMode::None);
}
int ed_set_number(ed_doc* doc, int number) { return doc->doc->setNumber(number); }
char* ed_chunk_json(ed_doc* doc) {
    const Chunk c = doc->doc->asChunk();
    std::string why;
    const bool can = c.canMirror(&why);
    char reach[32];
    std::snprintf(reach, sizeof reach, "%.3f", c.reach());
    return dup(std::string("{\"canMirror\":") + (can ? "true" : "false") + ",\"why\":" + str(why) + ",\"problem\":" + str(chunkBodyProblem(c)) + ",\"reach\":" + reach +
               ",\"runIn\":" + std::to_string(c.runIn()) + ",\"runOut\":" + std::to_string(c.runOut()) + "}");
}
int ed_set_meta(ed_doc* doc, const char* key, const char* value) { return doc->doc->setMeta(key, value ? value : ""); }
int ed_restore(ed_doc* doc, const char* text, const char* label) { return doc->doc->restore(text ? text : "", label ? label : "Reverted"); }
int ed_history_goto(ed_doc* doc, int k) { return k >= 0 && doc->doc->goTo(size_t(k)); }
char* ed_history_export(ed_doc* doc) { return dup(doc->doc->historyExport()); }
void ed_history_clear(ed_doc* doc) { doc->doc->clearHistory(); }
int ed_history_add(ed_doc* doc, int redo, const char* label, long long time, double at, const char* text) {
    return doc->doc->addHistory(redo != 0, label ? label : "", time, at, text ? text : "");
}
int ed_place_gauntlet(ed_doc* doc, const char* name, int col) { return doc->doc->placeGauntlet(name, col); }
int ed_remove_gauntlet(ed_doc* doc, const char* name) { return doc->doc->removeGauntlet(name); }
int ed_rename_gauntlet(ed_doc* doc, const char* from, const char* to) { return doc->doc->renameGauntlet(from ? from : "", to ? to : ""); }
int ed_valid_gauntlet_name(const char* name) { return validGauntletName(name ? name : ""); }
char* ed_rename_in_text(const char* text, const char* from, const char* to) { return dup(renameGauntletIn(text ? text : "", from ? from : "", to ? to : "")); }
double ed_start(ed_doc* doc) { return doc->doc->start ? *doc->doc->start : -1; }
void ed_set_start(ed_doc* doc, double x) { doc->doc->start = x < 0 ? std::nullopt : startOnCourse(doc->doc->course(), x); }

char* ed_scene_json(ed_doc* doc) { return dup(sceneJson(*doc->doc, doc->tools.get())); }
char* ed_hit_json(ed_doc* doc, double x, double y) { return dup(hitJson(*doc->doc, x, y)); }
char* ed_history_json(ed_doc* doc) { return dup(historyJson(*doc->doc)); }
char* ed_meta_json(ed_doc* doc) { return dup(metaJson(*doc->doc)); }
char* ed_report_json(ed_doc* doc) { return dup(doc->doc->reportJson()); }

ed_bot* ed_bot_new(void) { return new ed_bot; }
void ed_bot_free(ed_bot* bot) { delete bot; }
void ed_bot_kick(ed_bot* bot, ed_doc* doc, int delay_ms) { bot->job.kick(*doc->doc, delay_ms); }
void ed_bot_cancel(ed_bot* bot) { bot->job.cancel(); }
int ed_bot_busy(ed_bot* bot) { return bot->job.busy(); }
char* ed_bot_poll(ed_bot* bot, int* revision) {
    std::string json;
    int rev = -1;
    if (!bot->job.poll(json, rev)) return nullptr;
    if (revision) *revision = rev;
    return dup(json);
}

char* ck_parse(const char* id, const char* text) {
    Chunk c;
    std::string body, why;
    if (!splitChunk(id ? id : "", text ? text : "", c, body, &why)) return dup("{\"ok\":false,\"error\":" + str(why) + "}");
    std::string error;
    parseChunk(id ? id : "", text ? text : "", &error);
    std::string whyNot;
    bool can = true;
    if (c.maze) {
        std::istringstream rows(body);
        for (std::string row; std::getline(rows, row);) if (!row.empty()) c.rows.push_back(row);
        can = c.canMirror(&whyNot);
    } else if (const auto d = parseLevel(c.ranges.front().from, body)) {
        c.body = *d;
        can = c.canMirror(&whyNot);
    }
    std::string o = "{\"ok\":true,\"error\":" + str(error) + ",\"id\":" + str(c.id) + ",\"kind\":\"" + (c.maze ? "maze" : "course") + "\",\"notes\":[";
    for (size_t i = 0; i < c.notes.size(); ++i) o += (i ? "," : "") + str(c.notes[i]);
    o += "],\"ranges\":[";
    for (size_t i = 0; i < c.ranges.size(); ++i) o += (i ? "," : std::string()) + ("[" + std::to_string(c.ranges[i].from) + "," + std::to_string(c.ranges[i].to) + "]");
    return dup(o + "],\"weight\":" + std::to_string(c.weight) + ",\"uses\":" + std::to_string(c.uses) + ",\"mirror\":" + (c.mirror ? "true" : "false") +
               ",\"canMirror\":" + (can ? "true" : "false") + ",\"whyNot\":" + str(whyNot) + ",\"body\":" + str(body) + ",\"first\":" + std::to_string(c.ranges.front().from) + "}");
}

char* ck_text(int maze, const char* notes, const char* ranges, int weight, int uses, int mirror, const char* body) {
    Chunk c;
    c.maze = maze != 0;
    std::istringstream nin(notes ? notes : "");
    for (std::string line; std::getline(nin, line);) c.notes.push_back(line);
    std::istringstream rin(ranges ? ranges : "");
    for (std::string t; rin >> t;) {
        ChunkRange r;
        const size_t dash = t.find('-');
        try {
            r.from = std::stoi(t.substr(0, dash));
            r.to = dash == std::string::npos ? r.from : std::stoi(t.substr(dash + 1));
        } catch (...) { continue; }
        if (r.from >= 1 && r.to >= r.from && r.to <= 9999) c.ranges.push_back(r);
    }
    if (c.ranges.empty()) c.ranges.push_back({1, 1});
    c.weight = std::clamp(weight, 1, 10);
    c.uses = std::clamp(uses, 0, 99);
    c.mirror = mirror != 0;
    return dup(chunkHeaderText(c) + (body ? body : ""));
}

char* ck_blank(const char* id, int first) { return dup(chunkText(blankCourseChunk(id ? id : "", std::max(1, first)))); }

char* ck_blank_maze(const char* id, int first) { return dup(chunkText(blankMazeChunk(id ? id : "", std::max(1, first)))); }

char* ck_sort(const char* chunks) {
    std::vector<Chunk> ok;
    std::vector<std::string> bad;
    std::istringstream in(chunks ? chunks : "");
    std::string id, text;
    bool have = false;
    auto flush = [&]() {
        if (!have) return;
        Chunk c;
        std::string body;
        if (splitChunk(id, text, c, body)) ok.push_back(std::move(c)); else bad.push_back(id);
    };
    for (std::string line; std::getline(in, line);) {
        if (line.compare(0, 4, "=== ") == 0) {
            flush();
            id = line.substr(4);
            while (!id.empty() && (id.back() == ' ' || id.back() == '\r')) id.pop_back();
            text.clear();
            have = true;
        } else if (have) text += line + "\n";
    }
    flush();
    std::sort(ok.begin(), ok.end(), chunkBefore);
    std::sort(bad.begin(), bad.end());
    std::string o;
    for (const Chunk& c : ok) o += (o.empty() ? "" : ",") + str(c.id);
    for (const std::string& b : bad) o += (o.empty() ? "" : ",") + str(b);
    return dup("[" + o + "]");
}

namespace {
std::string filesJson(const std::vector<Chunk>& chunks) { // ({"id":"file text"} in the list's order)
    std::string o = "{";
    for (const Chunk& c : chunks) o += (o.size() > 1 ? "," : "") + str(c.id) + ":" + str(chunkText(c));
    return o + "}";
}
} // namespace

char* ck_builtin_json(void) { return dup(filesJson(LevelCatalog::builtin().chunks())); }

char* ck_files_json(const char* chunks) {
    std::vector<Chunk> all;
    std::string ignored;
    parseChunks(chunks ? chunks : "", all, &ignored);
    std::stable_sort(all.begin(), all.end(), chunkBefore);
    return dup(filesJson(all));
}

int ck_valid_id(const char* id) {
    Chunk c;
    std::string body;
    return splitChunk(id ? id : "", "chunk course\nranges 1\n---\n", c, body);
}

char* ed_merge3(int number, const char* base, const char* mine, const char* theirs) {
    const MergeResult r = merge3(number, base ? base : "", mine ? mine : "", theirs ? theirs : "");
    std::string o = "{\"ok\":" + std::string(r.ok ? "true" : "false") + ",\"text\":" + str(r.text) + ",\"conflicts\":[";
    for (size_t i = 0; i < r.conflicts.size(); ++i) o += (i ? "," : "") + str(r.conflicts[i]);
    return dup(o + "],\"error\":" + str(r.error) + "}");
}

char* ed_refit(const char* text, const char* name, int width) { return dup(refit(text ? text : "", name ? name : "", width)); }

char* ed_check_text(int number, const char* text) {
    std::string why;
    const bool ok = parseLevel(number, text ? text : "", &why).has_value();
    return dup("{\"ok\":" + std::string(ok ? "true" : "false") + ",\"error\":" + str(why) + "}");
}

int ed_set_dev_grid(const char* name, const char* rows) { return LevelCatalog::dev().setGauntlet(name ? name : "", lines(rows)); }

gd_doc* gd_open(const char* name, const char* text, char** error) { return gd_open_mode(name, text, 0, error); }

gd_doc* gd_open_mode(const char* name, const char* text, int chunk, char** error) {
    std::string why;
    auto doc = GridDoc::open(name ? name : "", text ? text : "", &why, chunk != 0);
    if (!doc) {
        if (error) *error = dup(why);
        return nullptr;
    }
    auto* d = new gd_doc;
    d->doc = std::move(doc);
    d->tools = std::make_unique<GridTools>(*d->doc);
    return d;
}

void gd_close(gd_doc* doc) { delete doc; }
void gd_set_mirror_check(gd_doc* doc, int on) { doc->doc->setMirrorCheck(on != 0); }
char* gd_text(gd_doc* doc) { return dup(doc->doc->text()); }
int gd_revision(gd_doc* doc) { return doc->doc->revision(); }
char* gd_last_error(gd_doc* doc) { return dup(doc->doc->lastError()); }
int gd_rename(gd_doc* doc, const char* name) { return doc->doc->rename(name ? name : ""); }
void gd_set_tool(gd_doc* doc, const char* tool) { doc->tools->setTool(tool ? tool : "brush"); }
void gd_set_cells(gd_doc* doc, int primary, int secondary) { doc->tools->setCell(char(primary), char(secondary)); }
void gd_set_erase(gd_doc* doc, int erase) { doc->tools->setErase(erase != 0); }
int gd_pointer(gd_doc* doc, int phase, int col, int row) { return doc->tools->pointer(static_cast<GridTools::Phase>(phase), col, row); }
int gd_undo(gd_doc* doc) { return doc->doc->undo(); }
int gd_redo(gd_doc* doc) { return doc->doc->redo(); }
int gd_set_description(gd_doc* doc, const char* text) {
    std::vector<std::string> out;
    std::istringstream in(text ? text : "");
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return doc->doc->setDescription(out);
}
int gd_restore(gd_doc* doc, const char* text, const char* label) { return doc->doc->restore(text ? text : "", label ? label : "Reverted"); }
void gd_set_start(gd_doc* doc, int col, int row) {
    if (col < 0) { doc->doc->start.reset(); return; }
    if (const std::optional<int> k = grid::standable(doc->doc->rows(), col, row)) doc->doc->start = std::make_pair(col, *k);
}

char* gd_scene_json(gd_doc* doc) {
    const GridDoc& d = *doc->doc;
    const Rows& rows = d.rows();
    auto cell = [](int c, int i) { return "[" + std::to_string(c) + "," + std::to_string(i) + "]"; };
    std::string entry = "null", exit = "null";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].empty() && rows[i].front() == '>') entry = cell(0, int(i));
        if (!rows[i].empty() && rows[i].back() == 'X') exit = cell(int(rows[i].size()) - 1, int(i));
    }
    std::string o = "{\"name\":" + str(d.name()) + ",\"cols\":" + std::to_string(grid::cols(rows)) + ",\"rows\":[";
    for (size_t i = 0; i < rows.size(); ++i) o += (i ? "," : "") + str(rows[i]);
    o += "],\"description\":[";
    for (size_t i = 0; i < d.description().size(); ++i) o += (i ? "," : "") + str(d.description()[i]);
    o += "],\"revision\":" + std::to_string(d.revision()) + ",\"canUndo\":" + (d.canUndo() ? "true" : "false") + ",\"canRedo\":" + (d.canRedo() ? "true" : "false");
    o += ",\"entry\":" + entry + ",\"exit\":" + exit;
    const GridTools::Preview& p = doc->tools->preview();
    o += ",\"preview\":" + (p.kind.empty() ? std::string("null") : "{\"kind\":" + str(p.kind) + ",\"c0\":" + std::to_string(p.c0) + ",\"i0\":" + std::to_string(p.i0) +
                                                                       ",\"c1\":" + std::to_string(p.c1) + ",\"i1\":" + std::to_string(p.i1) + ",\"erase\":" + (p.erase ? "true" : "false") + "}");
    o += ",\"start\":" + (d.start ? cell(d.start->first, d.start->second) : std::string("null"));
    o += std::string(",\"chunk\":") + (d.chunk() ? "true" : "false");
    return dup(o + ",\"error\":" + str(d.lastError()) + "}");
}

char* gd_history_json(gd_doc* doc) {
    auto steps = [](const std::vector<GridDoc::Step>& v) {
        std::string o;
        for (const GridDoc::Step& s : v)
            o += std::string(o.empty() ? "" : ",") + "{\"label\":" + str(s.label) + ",\"revision\":" + std::to_string(s.revision) + ",\"time\":" +
                 std::to_string(s.time) + ",\"at\":" + std::to_string(s.c) + ",\"row\":" + std::to_string(s.i) + "}";
        return "[" + o + "]";
    };
    const GridDoc& d = *doc->doc;
    return dup("{\"undo\":" + steps(d.history()) + ",\"redo\":" + steps(d.redoSteps()) + ",\"canUndo\":" + (d.canUndo() ? "true" : "false") +
               ",\"canRedo\":" + (d.canRedo() ? "true" : "false") + "}");
}

int gd_history_goto(gd_doc* doc, int k) { return k >= 0 && doc->doc->goTo(size_t(k)); }
char* gd_history_export(gd_doc* doc) { return dup(doc->doc->historyExport()); }
void gd_history_clear(gd_doc* doc) { doc->doc->clearHistory(); }
int gd_history_add(gd_doc* doc, int redo, const char* label, long long time, int col, int row, const char* text) {
    return doc->doc->addHistory(redo != 0, label ? label : "", time, col, row, text ? text : "");
}

char* gd_generate(int level, const char* level_text, int width, int floors, unsigned seed, const char* hazards, int density) {
    return gd_generate_chunks(level, level_text, width, floors, seed, hazards, density, "");
}

char* gd_generate_chunks(int level, const char* level_text, int width, int floors, unsigned seed, const char* hazards, int density, const char* chunks) {
    LevelCatalog cat;
    if (level_text && *level_text) cat.setLevel(level, level_text);
    const std::vector<std::string> ids = idsOf(hazards);
    auto on = [&](const char* id) { return (ids.empty() && !(hazards && std::string(hazards) == "none")) || std::find(ids.begin(), ids.end(), id) != ids.end(); };
    MazeOptions options{on("saw"), on("laser"), on("lava"), density};
    if (chunks && *chunks) { std::vector<Chunk> all; std::string ignored; parseChunks(chunks, all, &ignored); for (Chunk& c : all) if (c.maze) options.chunks.push_back(std::move(c)); }
    const MazeGenResult r = generateMaze(level, width, floors, seed, &cat, 60, options);
    std::string text;
    for (const std::string& line : r.description) text += line.empty() ? ";\n" : "; " + line + "\n";
    for (const std::string& row : r.rows) text += row + "\n";
    auto idList = [](const std::vector<std::string>& v) { std::string o; for (const std::string& id : v) o += (o.empty() ? "" : ",") + str(id); return "[" + o + "]"; };
    std::string used;
    for (const MazeChunkUse& u : r.chunks)
        used += std::string(used.empty() ? "" : ",") + "{\"id\":" + str(u.id) + ",\"floor\":" + std::to_string(u.floor) + ",\"c0\":" + std::to_string(u.c0) + ",\"c1\":" + std::to_string(u.c1) +
                ",\"r0\":" + std::to_string(u.r0) + ",\"r1\":" + std::to_string(u.r1) + ",\"mirrored\":" + (u.mirrored ? "true" : "false") + "}";
    return dup("{\"ok\":" + std::string(r.ok ? "true" : "false") + ",\"text\":" + str(text) + ",\"seed\":" + std::to_string(r.seed) +
               ",\"tries\":" + std::to_string(r.tries) + ",\"widened\":" + (r.widened ? "true" : "false") + ",\"why\":" + str(r.why) + ",\"chunks\":[" + used + "],\"chunkNote\":" + str(r.chunkNote) + ",\"dropped\":" + idList(r.chunksDropped) + ",\"reduced\":" + idList(r.chunksReduced) + "}");
}

gd_job* gd_job_new(void) { return new gd_job; }
void gd_job_free(gd_job* job) { delete job; }
void gd_job_set_grid(gd_job* job, const char* name, const char* rows) { job->grids[name ? name : ""] = lines(rows); }
void gd_job_kick(gd_job* job, gd_doc* doc, int level, const char* level_text) {
    GridJob::Request r;
    r.name = doc->doc->name();
    r.rows = doc->doc->checkRows();
    r.revision = doc->doc->revision();
    r.level = level;
    r.levelText = level_text ? level_text : "";
    r.flipRows = doc->doc->flippedCheckRows();
    r.noGame = doc->doc->chunk();
    for (const auto& [name, rows] : job->grids) r.grids.push_back({name, rows});
    job->job.kick(std::move(r));
}
void gd_job_cancel(gd_job* job) { job->job.cancel(); }
int gd_job_busy(gd_job* job) { return job->job.busy(); }
char* gd_job_poll(gd_job* job, int* revision) {
    std::string json;
    int rev = -1;
    if (!job->job.poll(json, rev)) return nullptr;
    if (revision) *revision = rev;
    return dup(json);
}

} // extern "C"
