// The iPhone and iPad editor's course model and document (EditCourse.h, EditorDoc.h, EditorTools.h, EditorScene.h):
// the web editor's course.test.mjs cases, ported (every shipped level reads into columns and writes back as it was;
// walls, barriers and floating blocks; hazards from the registry; columns copied in and taken out; gauntlets placed,
// moved, taken out, refitted), and the document's own: a drag one step in the history, an edit the game wouldn't read
// turned away, the tools as a finger uses them, the scene, and a gauntlet's forecourt as the game's generator lays it.
#include "TestUtil.h"
#include "core/Campaign.h"
#include "core/CourseRules.h"
#include "core/Gauntlet.h"
#include "editor/EditorDoc.h"
#include "editor/GridDoc.h"
#include "editor/cs_editor.h"
#include "editor/EditorScene.h"
#include "editor/EditorTools.h"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace cs;
using namespace cs::ed;
using test::check;

namespace {

const char* kSmall = "; A test course.\nlength 30\ndifficulty 70 0.35\nraids auto\ncourse\n16 0 crate\n5 2 hay 6\n4 3 mix\n30 0 crate\nsaws\n14.1 22.5\ndiscos\n10.5 1.45\n";

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

Course small() { return fromData(*parseLevel(4, kSmall)); }
std::string text(const Course& c) { return levelText(toData(c)); }
std::vector<double> saws(const Course& c) { return toData(c).xsOf(kSawHazard); }
std::vector<double> discos(const Course& c) { return toData(c).xsOf(kDiscoHazard); }

void readsAndWritesBack() {
    int n = 0, same = 0;
    for (int number : LevelCatalog::builtin().levelNumbers()) {
        const std::string t = levelText(*LevelCatalog::builtin().level(number));
        ++n;
        same += levelText(toData(fromData(*parseLevel(number, t)))) == t;
    }
    check(n >= 30 && same == n, "every shipped level reads into columns and writes back as it was");
    const Course c = small();
    check(c.cols.size() == 55 && c.cols[16].h == 2 && c.cols[16].kind == WallKind::Hay && c.cols[16].ceil == 6, "a cell a column");
    const Course two = fromData(*parseLevel(4, std::string(kSmall).replace(std::string(kSmall).find("16 0 crate"), 10, "6 0 crate\n10 0 crate")));
    check(text(two).find("\n6 0 crate\n10 0 crate\n") != std::string::npos, "two like walls side by side stay two");
}

void walls() {
    Course c = small();
    check(setHeight(c, 3, 2, WallKind::Mix) && c.cols[3].h == 2 && c.cols[3].kind == WallKind::Mix, "street raised: the brush's kind");
    check(setHeight(c, 3, 4, WallKind::Crate) && c.cols[3].kind == WallKind::Mix, "a wall raised more keeps its kind");
    check(text(c).find("\n3 0 crate\n1 4 mix\n12 0 crate\n") != std::string::npos, "written back in runs");
    Course b = small();
    check(setHeight(b, 16, 5) && b.cols[16].ceil == 0, "a barrier now on her head goes");
    check(!setHeight(b, 17, 2), "nothing changed: false");
    check(!setCeil(b, 0, 1) && setCeil(b, 0, 4) && b.cols[0].ceil == 4, "a barrier 2 over the wall at least");
    Course l = small();
    for (int k = 16; k <= 20; ++k) setHeight(l, k, 0, WallKind::Crate);
    check(text(l).find("\n16 0 crate\n5 0 crate 6\n") != std::string::npos, "a wall taken down from under its barrier: the barrier stays");
    for (int k = 16; k <= 20; ++k) setCeil(l, k, 0);
    check(text(l).find("\n21 0 crate\n4 3 mix\n") != std::string::npos, "...and gone too: one street with the street before it");
    Course k = small();
    check(setBlock(k, 2, 2, 4, WallKind::Mix) && k.cols[2].ceil == 2 && k.cols[2].top == 4 && k.cols[2].kind == WallKind::Mix, "a floating block over the street: the kind picked");
    check(!setBlock(k, 2, 2, 2) && !setBlock(k, 17, 2, 5) && setBlock(k, 17, 3, 5), "its top over its underside, its underside 1 over the wall");
    check(!setBlock(k, 3, 2, 61), "its top up to 60");
    check(setBlock(k, 2, 0, 0) && k.cols[2].ceil == 0 && k.cols[2].top == 0, "taken off, its underside with it");
}

void hazards() {
    Course c = small();
    check(addItem(c, kSawHazard, {1.5}) >= 0 && saws(c) == std::vector<double>{1.5, 14.1, 22.5}, "a saw put in, in order");
    check(addItem(c, kSawHazard, {1.7}) < 0, "not on top of another (its row's gap apart at least)");
    check(near(snapX(c, kSawHazard, 15.8), 14.1 + Level::kSawRowGap), "near a saw's row gap: blades back to back");
    check(near(snapX(c, kSawHazard, 18.04), 18), "elsewhere: on the decimetre");
    check(near(snapX(c, kDiscoHazard, 15.83), 15.8), "a disco ball: no row");
    const int d = addItem(c, kDiscoHazard, {3, 2});
    check(d >= 0 && c.items[size_t(d)].at(1) == 2, "a disco ball where it's put");
    check(addItem(c, kDiscoHazard, {4, 0.1}) < 0, "...not on the street");
    check(addItem(c, kDiscoHazard, {4, 50}) < 0, "...nor past its row's range (40 m up)");
    const int s = itemNear(c, &kSawHazard, 14.2, 0, 0.6);
    check(s >= 0 && moveItem(c, s, {5}) && saws(c) == std::vector<double>{1.5, 5, 22.5}, "a saw moved, in order again");
    check(removeItem(c, itemNear(c, &kSawHazard, 1.5, 0, 0.6)) && saws(c).size() == 2, "...and taken out");
}

void columns() {
    Course c = small();
    check(duplicateCols(c, 16, 17) && c.cols.size() == 57 && text(c).find("\n7 2 hay 6\n") != std::string::npos, "copied in: the wall made longer, still one");
    check(saws(c) == std::vector<double>{15.3, 23.7} && near(c.length, 31.2), "...the saws past them, and the finish line, on");
    Course p = small();
    const auto clip = extractCols(p, 21, 24);
    check(clip && insertCols(p, 2, *clip) && p.cols[2].h == 3 && p.cols[2].kind == WallKind::Mix, "pasted");
    check(saws(p) == std::vector<double>{2.7, 16.5, 24.9} && near(p.length, 32.4), "...its saw with it; the others on");
    Course r = small();
    setLength(r, 40);
    const size_t had = r.cols.size();
    check(deleteCols(r, 0, 3) && r.cols.size() == had - 4 && near(r.length, 37.6), "taken out: the finish line back with them");
    check(near(saws(r)[0], 14.1 - 2.4) && near(discos(r)[0], 10.5 - 2.4), "...the saws and disco balls back too");
    Course s = small();
    check(!deleteCols(s, 0, 40), "the course keeps 30 m");
}

void gauntlets() {
    Course c = small();
    setLength(c, 120);
    addItem(c, kSawHazard, {25});
    const GridInfo grid{20, 1, 4, 2};
    check(placeGauntlet(c, "tomb", grid, 30, 23), "a gauntlet placed");
    const auto s = spanOf(c, "tomb");
    check(s && s->c0 == 30 && s->c1 == 50, "...its span");
    check(c.cols[29].h == 1 && c.cols[29].ceil == 4 && c.cols[10].h == 1 && c.cols[10].ceil == 0, "its lintel as low as its door; its forecourt at its door's height");
    check(c.cols[50].h == 2, "its porch at its exit's");
    check(saws(c) == std::vector<double>{}, "what was there goes");
    check(text(c).find("\n20 1 crate\n3 1 crate 4\n20 0 gauntlet tomb\n8 2 crate\n") != std::string::npos, "its forecourt, lintel and porch: the game's");
    Course m = c;
    check(placeGauntlet(m, "tomb", grid, 100, 23) && spans(m).size() == 1 && m.cols[35].h == 0, "moved: where it was, street");
    check(m.length >= (100 + 20 + 8) * U, "the finish line past its porch");
    Course w = c;
    check(fitGauntlet(w, "tomb", 24) && spanOf(w, "tomb")->c1 == 54 && near(w.length, c.length + 2.4), "refitted to its grid: what's after it on");
    Course o = c;
    check(removeGauntlet(o, "tomb", 23) && spans(o).empty() && o.cols[10].h == 0 && o.cols[29].ceil == 0, "taken out, its approach with it");
    check(!duplicateCols(c, 35, 35) && !deleteCols(c, 45, 55) && addItem(c, kSawHazard, {35 * U}) < 0, "never inside a gauntlet");
    check(!placeGauntlet(c, "other", grid, 40, 23), "not on top of another");
}

// the forecourt the editor lays is the game's: Level::placeGauntlet's, where it lays a level's gauntlet
void forecourtAsTheGameLaysIt() {
    bool all = true;
    int seen = 0;
    for (int n = 11; n <= 19; n += 2) {
        const Gauntlet* g = Campaign::gauntletOf(n);
        if (!g) continue;
        Level l;
        l.reset(Campaign::seedOf(n));
        l.setDifficulty(35.0 * (n - 1), 0.35);
        l.setFinish(300 + 10.0 * (n - 1));
        l.setGauntlet(g, Campaign::gauntletAt(n));
        l.generateUntil(l.finish() + 20);
        if (l.gauntlets.empty()) continue;
        const double door = l.gauntlets[0].x0;
        int cells = 0; // (the forecourt: its lintel's segment and the one before it, at the door's row)
        for (size_t i = 0; i < l.segs.size(); ++i)
            if (std::abs(l.segs[i].x1 - door) < 1e-6 && i >= 1) cells = int(std::lround((l.segs[i].x1 - l.segs[i - (l.segs[i].ceil ? 1 : 0)].x0) / U));
        auto doc = EditorDoc::open(n, levelText(*LevelCatalog::builtin().level(n)));
        if (doc) doc->setMeta("difficulty", levelNum(35.0 * (n - 1)) + " 0.35"); // (the generator's band)
        all = all && doc && doc->course().diffOffset == 35.0 * (n - 1) && cells > 0;
        const int fore = doc ? doc->forecourtAt(door - cells * U) : 0;
        all = all && fore == cells;
        ++seen;
    }
    check(seen >= 4 && all, "a gauntlet's forecourt: as long as the game's generator lays it");
}

void historyAndRejects() {
    auto doc = EditorDoc::open(4, kSmall);
    check(doc != nullptr, "a level open");
    if (!doc) return;
    EditorTools tools(*doc);
    tools.setTool("wall");
    tools.pointer(Phase::Begin, 2.1 * U, 3 * U);
    for (int k = 3; k <= 7; ++k) tools.pointer(Phase::Move, (k + 0.5) * U, 3 * U);
    tools.pointer(Phase::End, 7.5 * U, 3 * U);
    check(doc->course().cols[2].h == 3 && doc->course().cols[7].h == 3 && doc->history().size() == 1, "a drag across columns: walls, one step in the history");
    check(doc->history()[0].label == "Walls 3 high at 1.2 m", "...in words");
    check(doc->undo() && doc->course() == small() && doc->canRedo(), "undone: as it was");
    check(doc->redo() && doc->course().cols[5].h == 3, "redone");
    const int rev = doc->revision();
    check(!doc->setMeta("length", "6000") && !doc->lastError().empty() && doc->revision() == rev, "an edit the game wouldn't read: turned away, why said, nothing changed");
    tools.pointer(Phase::Begin, 20 * U, 6 * U);
    tools.pointer(Phase::Move, 21 * U, 6 * U);
    tools.pointer(Phase::Cancel, 21 * U, 6 * U);
    check(doc->history().size() == 1 && doc->course().cols[20].h == 2, "a gesture cancelled: as it was, no step");
}

// the history: when and where each step was, back to any step, a revert as a step of its own, and kept between
// launches (exported, and brought back step by step)
void historyKept() {
    auto doc = EditorDoc::open(4, kSmall);
    const std::string before = doc->text();
    check(doc->edit("Walls 3 high at 1.2 m", [](Course& c) { return setHeight(c, 2, 3); }), "an edit");
    check(doc->edit("A saw", [](Course& c) { return addItem(c, kSawHazard, {20}) >= 0; }), "another");
    const std::vector<EditorDoc::Step> h = doc->history();
    check(h.size() == 2 && h[0].time > 1700000000 && std::abs(h[0].at - 2.5 * U) < 1e-9 && std::abs(h[1].at - 20) < 1e-9, "each step: when, and where (its column, its hazard)");
    const std::string two = doc->text();
    check(doc->goTo(0) && doc->text() == before && doc->redoSteps().size() == 2 && doc->redoSteps()[0].label == "Walls 3 high at 1.2 m", "back to before them all; redo next first");
    check(doc->goTo(2) && doc->text() == two && !doc->canRedo(), "...and forward to the last");
    check(doc->restore(before, "Back to the synced version") && doc->text() == before && doc->history().size() == 3, "a revert: one step of its own");
    check(doc->undo() && doc->text() == two, "...undone like any");
    check(!doc->restore("length nope", "x") && !doc->lastError().empty(), "a text that doesn't read: turned away");
    doc->goTo(1);
    const std::string saved = doc->historyExport();
    const std::string one = doc->text(); // (after its first step)
    auto again = EditorDoc::open(4, one);
    again->clearHistory();
    bool ok = true;
    // (the export's texts, as the app brings them back: undo oldest first, redo next first)
    ok = ok && again->addHistory(false, "Walls 3 high at 1.2 m", h[0].time, h[0].at, before);
    ok = ok && again->addHistory(true, "A saw", h[1].time, h[1].at, two);
    ok = ok && again->addHistory(true, "Back to the synced version", 0, -1, before);
    check(ok && again->history().size() == 1 && again->redoSteps().size() == 2 && saved.find("\"redo\":[{\"label\":\"A saw\"") != std::string::npos,
          "kept between launches: exported, and brought back step by step");
    check(again->redo() && again->text() == two && again->redo() && again->text() == before && again->undo() && again->undo() && again->text() == one,
          "...and it undoes and redoes as it did");
    check(historyJson(*doc).find("\"time\":") != std::string::npos && historyJson(*doc).find("\"redo\":[{") != std::string::npos, "the history's JSON: when, where, redo steps whole");
}

void tools() {
    auto doc = EditorDoc::open(4, kSmall);
    EditorTools t(*doc);
    t.setTool("saw");
    t.pointer(Phase::Begin, 3.02, 0);
    t.pointer(Phase::End, 3.02, 0);
    check(saws(doc->course()) == std::vector<double>{3, 14.1, 22.5}, "the saw tool: a tap puts one in, on the decimetre");
    t.pointer(Phase::Begin, 3, 0);
    t.pointer(Phase::Move, 5.04, 0);
    t.pointer(Phase::End, 5.04, 0);
    check(saws(doc->course()) == std::vector<double>{5, 14.1, 22.5} && doc->history().size() == 2, "...a drag moves it: one step");
    t.setErase(true);
    t.pointer(Phase::Begin, 5, 0);
    t.pointer(Phase::End, 5, 0);
    check(saws(doc->course()).size() == 2, "...the eraser takes it out");
    t.setErase(false);
    t.setTool("disco");
    t.pointer(Phase::Begin, 20, 2.5);
    t.pointer(Phase::End, 20, 2.5);
    check(discos(doc->course()).size() == 2 && toData(doc->course()).items.size() == 4, "the disco tool: one where it's tapped");
    t.setTool("block");
    t.setKind(WallKind::Hay);
    t.pointer(Phase::Begin, 2 * U, 2 * U);
    t.pointer(Phase::Move, 4 * U, 4 * U);
    check(t.preview().kind == "block" && t.preview().c1 == 4 && doc->course().cols[3].top == 0, "the block tool: a box to drag, drawn till it's let go");
    t.pointer(Phase::End, 4 * U, 4 * U);
    check(doc->course().cols[3].ceil == 2 && doc->course().cols[3].top == 4 && doc->course().cols[3].kind == WallKind::Hay, "...then the blocks");
    t.setTool("select");
    t.pointer(Phase::Begin, 21 * U, 0);
    t.pointer(Phase::Move, 24 * U, 0);
    t.pointer(Phase::End, 24 * U, 0);
    const size_t cols = doc->course().cols.size();
    check(t.selC0 == 21 && t.selC1 == 24 && t.copySelection() && t.pasteAt(2) && doc->course().cols.size() == cols + 4, "select, copy, paste");
    t.setTool("start");
    t.pointer(Phase::Begin, 14.3, 0);
    t.pointer(Phase::End, 14.3, 0);
    check(doc->start && near(*doc->start, 14.3), "the start tool: where it's tapped, on the decimetre");
    t.pointer(Phase::Begin, 16.6, 0);
    t.pointer(Phase::End, 16.6, 0);
    check(doc->start && near(*doc->start, 17.6), "...off a saw, to its nearer side (it starts with no eggs)");
    const std::string scene = sceneJson(*doc, &t);
    check(scene.find("\"walls\":[[0,") != std::string::npos && scene.find("\"type\":\"saw\"") != std::string::npos && scene.find("\"walk\":[") != std::string::npos,
          "the scene: walls, hazards, the meter's walk");
    check(hitJson(*doc, saws(doc->course())[0], 0).find("\"what\":\"item\"") != std::string::npos, "what's under a finger: a hazard");
    check(hitJson(*doc, 23.5 * U, 1).find("\"what\":\"wall\",\"col\":23") != std::string::npos, "...a wall");
}

} // namespace

// wall lasers and lava: a laser placed with its beam at the finger's height (eggs up from the wall there) and dragged up
// and down; lava on the cells, hit anywhere along its stretch, kept on the course and off gauntlets, gone when columns
// cut into it, copied with columns only whole; both in the scene (lava's end too)
// The eraser, whatever tool is picked: what's under the finger goes (a pickup, a hazard, a barrier, the walls)
void eraser() {
    auto doc = EditorDoc::open(4, kSmall);
    EditorTools t(*doc);
    t.setTool("wall");
    t.setErase(true);
    auto tap = [&](double x, double y) { t.pointer(Phase::Begin, x, y); t.pointer(Phase::End, x, y); };
    tap(10.5, 1.45);
    check(discos(doc->course()).empty() && doc->course().cols[17].h == 2, "with the Walls tool picked: a disco ball erased, not the walls");
    tap(14.1, 3 * U);
    check(saws(doc->course()) == std::vector<double>{22.5}, "...a saw");
    tap(18 * U + 0.3, 6 * U);
    check(doc->course().cols[18].ceil == 0 && doc->course().cols[18].h == 2, "...a barrier (the wall under it kept)");
    const size_t steps = doc->history().size();
    t.pointer(Phase::Begin, 16 * U + 0.3, U);
    t.pointer(Phase::Move, 20 * U + 0.3, U);
    t.pointer(Phase::Move, 24 * U + 0.3, U);
    t.pointer(Phase::End, 24 * U + 0.3, U);
    bool street = true;
    for (int k = 16; k <= 24; ++k) street = street && doc->course().cols[size_t(k)].h == 0;
    check(street && doc->history().size() == steps + 1, "...the walls it's dragged across, down to the street: one step");
    t.setErase(false);
    tap(30 * U + 0.3, 2 * U);
    check(doc->course().cols[30].h == 2, "the eraser off: the Walls tool builds again");
    check(doc->edit("A block", [](Course& c) { return setBlock(c, 40, 3, 5); }), "(a floating block)");
    t.setErase(true);
    tap(40 * U + 0.3, 4 * U);
    check(doc->course().cols[40].top == 0, "the eraser: a floating block");
    auto maze = EditorDoc::open(4, kSmall);
    EditorTools m(*maze);
    check(maze->placeGauntlet("tomb", 30), "(a maze in the course)");
    const auto span = spanOf(maze->course(), "tomb");
    m.setErase(true);
    m.pointer(Phase::Begin, (span->c0 + 2) * U, U);
    m.pointer(Phase::Move, (span->c0 + 8) * U, U);
    m.pointer(Phase::End, (span->c0 + 8) * U, U);
    check(spanOf(maze->course(), "tomb").has_value() && spanOf(maze->course(), "tomb")->c1 == span->c1, "...never a maze's span (the Gauntlet tool takes one out)");
}

// a maze renamed: the course and every step of its history lay it by the new name (an undo never brings back a name
// that's gone), the grid the same; never to a bad name or another grid's; the text a level file holds, rewritten
void renamed() {
    auto doc = EditorDoc::open(4, kSmall);
    check(doc->placeGauntlet("tomb", 30), "(a maze in the course)");
    check(doc->edit("A wall", [](Course& c) { return setHeight(c, 20, 3); }), "(an edit after it)");
    const int steps = int(doc->history().size()), cols = spanOf(doc->course(), "tomb")->c1 - spanOf(doc->course(), "tomb")->c0;
    check(doc->renameGauntlet("tomb", "sand_tomb"), "renamed");
    check(!spanOf(doc->course(), "tomb") && spanOf(doc->course(), "sand_tomb") &&
              spanOf(doc->course(), "sand_tomb")->c1 - spanOf(doc->course(), "sand_tomb")->c0 == cols, "...the course lays it by its new name, as wide");
    check(int(doc->history().size()) == steps, "...not a step in the history");
    check(doc->text().find(" 0 gauntlet sand_tomb\n") != std::string::npos, "...and its text says so");
    check(doc->undo() && spanOf(doc->course(), "sand_tomb").has_value(), "an undo: still its new name");
    check(doc->redo() && doc->undo() && doc->undo() && !spanOf(doc->course(), "tomb") && !spanOf(doc->course(), "sand_tomb"), "...back before it was laid: none");
    check(doc->redo() && spanOf(doc->course(), "sand_tomb").has_value(), "a redo lays it by its new name");
    check(doc->historyExport().find("gauntlet tomb") == std::string::npos, "the history kept on the device: the new name throughout");
    check(!doc->renameGauntlet("sand_tomb", "Sand Tomb") && doc->lastError().find("name") != std::string::npos, "never a bad name");
    check(doc->renameGauntlet("sand_tomb", "tomb") && spanOf(doc->course(), "tomb").has_value(), "renamed back (a built-in name: its own)");
    check(!doc->renameGauntlet("nope", "nope_two"), "never one there's none of");
    check(validGauntletName("maze_20_4") && !validGauntletName("4maze") && !validGauntletName("") && !validGauntletName(std::string(33, 'a')),
          "a name: lower case letters, digits and _, a letter first, 32 at most");
    check(gauntletTitle("hall_of_saws") == "THE HALL OF SAWS", "its banner as she enters: spaced, upper case");
    const std::string file = "; tomb\ncourse\n30 1 crate\n168  0 gauntlet tomb\n40 0 gauntlet tombs\nsaws\n";
    check(renameGauntletIn(file, "tomb", "crypt") == "; tomb\ncourse\n30 1 crate\n168  0 gauntlet crypt\n40 0 gauntlet tombs\nsaws\n",
          "a level file's text: its course line alone, its spacing kept");
}

// a maze coming out high (its exit's porch): a fair stair down off it to the course, a drop of 2 at the most a step, each
// onto a landing long enough to land and react on; the walls under it raised (a barrier over one goes); it ends where
// the course is as high as it, and its lasers' sides go
void stairDown() {
    Course c = small();
    setLength(c, 200);
    for (int k = 60; k < 70; ++k) c.cols[size_t(k)].ceil = 5; // (a barrier the stair will cover)
    addItem(c, kDiscoHazard, {38.0, 1.5}); // (a disco ball it will bury)
    addItem(c, kDiscoHazard, {38.6, 9.0}); // (one high over it)
    const GridInfo grid{20, 1, 4, 7}; // (out at row 7)
    const double speed = 8.0;
    check(placeGauntlet(c, "tomb", grid, 30, 23, speed), "a maze placed, out high");
    const int z = 30 + 20 + Level::kPorch;
    check(c.cols[size_t(z - 1)].h == 7, "...its porch at its exit's height");
    int h = 7, at = z;
    bool fair = true;
    std::vector<int> steps;
    for (int i = z; i < int(c.cols.size()) && h > 0; ++i) {
        const int k = c.cols[size_t(i)].h;
        if (k == h) continue;
        fair = fair && k < h && h - k <= 2 && (i - at >= rules::landingCells(speed, 2) || at == z); // (each landing long enough)
        steps.push_back(h - k);
        h = k; at = i;
    }
    check(steps == std::vector<int>{2, 2, 2, 1} && fair, "...a stair down off it, 2 at the most a step, each landing long enough to land and react");
    check(c.cols[62].ceil == 0, "...the walls under it raised, a barrier over them gone");
    check(discos(c) == std::vector<double>{38.6}, "...a disco ball it buried gone, one over it kept");
    // saws it left on the stair's edges or drops (the level's own, on the street the walls were raised over): the ones that
    // stand unfair go (the checker finds them: on the edge of a wall, too soon after a drop, too close to the next wall), those on a
    // flat landing stay
    Course saws = small();
    setLength(saws, 200);
    int placedSaws = 0;
    for (double x = 36; x < 70; x += 2.1) placedSaws += addItem(saws, kSawHazard, {x}) >= 0;
    placeGauntlet(saws, "tomb", grid, 30, 23, speed);
    int kept = 0;
    bool onFlats = true;
    for (const Placed& p : saws.items) {
        if (p.type != &kSawHazard) continue;
        ++kept;
        const int a = colAt(p.x() - Level::kSawReach - 0.01), b = colAt(p.x() + Level::kSawReach + 0.01);
        onFlats = onFlats && saws.cols[size_t(a)].h == saws.cols[size_t(b)].h && saws.cols[size_t(a - 1)].h <= saws.cols[size_t(a)].h; // (the flat either side of it, no drop just before)
    }
    check(placedSaws > 8 && kept > 0 && kept < placedSaws && onFlats, "...a saw it left on a step's edge or right after a drop gone, those on a flat landing kept");
    // a lone saw just where the stair's first drop would land her on it: the drop moves on past it (its flat runs on, a
    // reaction's run to the drop), and the saw stays; another a step lower likewise
    Course lone = small();
    setLength(lone, 200);
    addItem(lone, kSawHazard, {z * U + 1.0});
    addItem(lone, kSawHazard, {z * U + 14.0});
    placeGauntlet(lone, "tomb", grid, 30, 23, speed);
    int keptLone = 0;
    for (const Placed& p : lone.items) {
        if (p.type != &kSawHazard) continue;
        ++keptLone;
        const int a = colAt(p.x() - Level::kSawReach - 0.01), b = colAt(p.x() + Level::kSawReach + 0.01);
        check(lone.cols[size_t(a)].h == lone.cols[size_t(b)].h, "...a lone saw on a flat landing, a drop's edge moved on past it");
    }
    check(keptLone == 2 && lone.cols[size_t(colAt(z * U + 1.0))].h == 7, "...both kept (the first on the porch's height, the drop after it)");
    Course tall = small();
    setLength(tall, 200);
    for (int k = 66; k < 80; ++k) tall.cols[size_t(k)].h = 6;
    placeGauntlet(tall, "tomb", grid, 30, 23, speed);
    check(tall.cols[size_t(z)].h == 6 && tall.cols[66].h == 6 && tall.cols[80].h == 0, "...a step onto a wall it meets (no climb after it), ending there (from there, the course's own)");
    Course plain = small();
    setLength(plain, 200);
    placeGauntlet(plain, "tomb", grid, 30, 23);
    check(plain.cols[size_t(z)].h == 0, "(no speed given: no stair, as before)");
    // its forecourt, at the height of a wall just before it: one wall with it (a landing onto them whole)
    Course onto = small();
    setLength(onto, 200);
    for (int k = 30; k < 33; ++k) { onto.cols[size_t(k)].h = 1; onto.cols[size_t(k)].kind = WallKind::Hay; }
    placeGauntlet(onto, "tomb", grid, 56, 23, speed);
    const std::string t = text(onto);
    check(t.find("\n23 1 hay\n3 1 hay 4\n20 0 gauntlet tomb\n") != std::string::npos,
          "its forecourt one wall with a like one just before it (its kind), not a short run of its own");
}

// a maze coming out low onto a wall too tall to climb (more than the curve's maxUp, or than the meter holds): a stair up
// in front of the wall, each climb the curve's maxUp at the most and the meter's to spare (a careful player's: 1 egg and 8
// cells' refill after the maze, then the porch's cells), on a run-up that refills it; the course after moves on
void stairUp() {
    Course c = small();
    setLength(c, 200);
    for (int k = 58; k < 80; ++k) c.cols[size_t(k)].h = 9; // (right after the porch of a maze at 30: a wall 9 high)
    addItem(c, kDiscoHazard, {40.0, 3.0}); // (a disco ball past it: it moves on with the wall)
    const GridInfo grid{20, 1, 4, 1}; // (out at row 1: 8 below the wall)
    const double epc = 0.11; // (eggs a cell at full speed)
    const StairUp up{epc, 4, 4};
    const double was = c.length;
    check(placeGauntlet(c, "tomb", grid, 30, 23, 12.4, up), "a maze placed, out low, onto a wall 9 high");
    const int z = 30 + 20 + Level::kPorch;
    check(c.cols[size_t(z - 1)].h == 1, "...its porch at its exit's height");
    // walk it as the checker does: 1 egg and 8 cells' refill after the maze, each cell's refill, each climb paid for with a margin
    double b = std::min(8.0, 1.0 + 8 * epc);
    int h = 1, climbs = 0, tallest = 0;
    bool afford = true;
    size_t i = size_t(z) - Level::kPorch;
    for (; i < c.cols.size() && c.cols[i].h < 9 + 1 && h < 9; ++i) {
        const int k = c.cols[i].h;
        if (k > h) { afford = afford && b + 1e-6 >= (k - h) + Level::kBudgetMargin && k - h <= 4; tallest = std::max(tallest, k - h); b -= k - h; h = k; ++climbs; }
        b = std::min(8.0, b + epc);
    }
    check(h == 9 && climbs >= 2 && afford && tallest <= 4, "...a stair up in front of it: each climb 4 at the most, the meter's to spare");
    check(c.cols[i - 1].h == 9 && c.cols[i - 1].cut, "...ending on the wall, whole");
    check(c.length > was && discos(c).size() == 1 && std::abs(discos(c)[0] - (40.0 + (c.length - was))) < 1e-6, "...the course after it moved on (its disco ball too)");
    Course low = small();
    setLength(low, 200);
    for (int k = 58; k < 80; ++k) low.cols[size_t(k)].h = 2;
    const double lowWas = low.length;
    placeGauntlet(low, "tomb", grid, 30, 23, 12.4, up);
    check(low.length == lowWas && low.cols[size_t(z)].h == 2, "a wall 2 up (a climb the meter holds): as it was, no stair");
    Course none = small();
    setLength(none, 200);
    for (int k = 58; k < 80; ++k) none.cols[size_t(k)].h = 9;
    placeGauntlet(none, "tomb", grid, 30, 23, 12.4);
    check(none.cols[size_t(z)].h == 9, "(no rules given: no stair, as before)");
}

// a laser set into a wall's or a block's side (faceNear): the side nearest the finger at its row, a barrier's too, never
// a maze's; its reach (faceReach) out along its row to the first solid cell, 26 cells at the most
void faces() {
    const Course c = small(); // (16 .. 20: a wall 2 high under a barrier at 6; 21 .. 24: 3 high)
    const auto left = faceNear(c, 16 * U - 0.1, 0.5 * U), right = faceNear(c, 25 * U + 0.1, 2.5 * U), girder = faceNear(c, 16 * U - 0.1, 7.5 * U);
    check(left && near((*left)[0], 16 * U) && near((*left)[1], 0) && near((*left)[2], 0), "a wall's left side, at the finger's row, firing left");
    check(right && near((*right)[0], 25 * U) && near((*right)[1], 2) && near((*right)[2], 1), "...a wall's right side, firing right");
    check(girder && near((*girder)[0], 16 * U) && near((*girder)[1], 7) && near((*girder)[2], 0), "...a barrier's side, over the wall");
    check(!faceNear(c, 3, 0.3), "...none out on the open street");
    Course g = c;
    for (int k = 16; k <= 24; ++k) g.cols[size_t(k)].g = "tomb";
    check(!faceNear(g, 16 * U - 0.1, 0.5 * U), "...none on a maze's span (its lasers are its grid's)");
    const auto reach = [&](std::vector<double> v) { return faceReach(c, Placed{&kLaserHazard, std::move(v)}); };
    check(near(reach(*left).first, 0) && near(reach(*left).second, 16 * U), "its reach: back along the street to the start");
    check(near(reach(*right).first, 25 * U) && near(reach(*right).second, 51 * U), "...on along the street, 26 cells at the most");
    check(near(reach({21 * U, 2, 0}).first, 0) && near(reach({21 * U, 1, 0}).first, 21 * U), "...over a lower wall; into a wall at its row: none");
    check(solidCell(c, 18, 1) && solidCell(c, 18, 6) && !solidCell(c, 18, 3) && !solidCell(c, 60, 0), "the cells: a wall's, a barrier's from its underside up; open between; none past the end");
    // dragged: to the side nearest the finger, at its row; carried along with the columns copied in before it
    auto doc = EditorDoc::open(4, kSmall);
    EditorTools t(*doc);
    t.setTool("laser");
    t.pointer(Phase::Begin, 16 * U - 0.1, 0.5 * U);
    t.pointer(Phase::Move, 25 * U + 0.1, 2.5 * U);
    t.pointer(Phase::End, 25 * U + 0.1, 2.5 * U);
    const auto laser = [&](const Course& k) { for (const Placed& p : k.items) if (p.type == &kLaserHazard) return p; return Placed{}; };
    check(laser(doc->course()).type && near(laser(doc->course()).x(), 25 * U) && near(laser(doc->course()).at(2), 1), "a laser dragged to the far wall's right side: set into it, firing right");
    Course k = doc->course();
    check(duplicateCols(k, 2, 3) && near(laser(k).x(), 27 * U), "...columns copied in before it: it moves on with its wall");
}

void lasersAndLava() {
    auto doc = EditorDoc::open(4, kSmall);
    EditorTools tools(*doc);
    tools.setTool("laser");
    tools.pointer(Phase::Begin, 3, 0.3); // (nowhere near a wall's side)
    tools.pointer(Phase::End, 3, 0.3);
    const auto laser = [&] { for (const Placed& p : doc->course().items) if (p.type == &kLaserHazard) return p; return Placed{}; };
    check(!laser().type, "a laser: none on the open street (no wall's side to set it into)");
    tools.pointer(Phase::Begin, 16 * U - 0.1, 1.5 * U); // (by the wall at column 16, 2 high: its left side)
    tools.pointer(Phase::End, 16 * U - 0.1, 1.5 * U);
    check(laser().type && near(laser().x(), 16 * U) && near(laser().at(1), 1) && near(laser().at(2), 0), "...one set into the wall's side, at the finger's row, firing left");
    check(near(itemY(doc->course(), laser()), 1.5 * U), "...drawn level with the middle of its row");
    tools.pointer(Phase::Begin, 16 * U - 0.1, 1.5 * U);
    tools.pointer(Phase::Move, 16 * U - 0.1, 0.5 * U);
    tools.pointer(Phase::End, 16 * U - 0.1, 0.5 * U);
    check(near(laser().at(1), 0) && doc->history().size() == 2, "...dragged down its side: a row lower, one step in the history");
    check(sceneJson(*doc, &tools).find("\"reach\":[0," + levelNum(16 * U) + "]") != std::string::npos, "...its reach in the scene: back along the street");
    tools.setTool("lava");
    tools.pointer(Phase::Begin, 3.1 * U, 0.1);
    tools.pointer(Phase::End, 3.1 * U, 0.1);
    const auto lava = [&] { for (const Placed& p : doc->course().items) if (p.type == &kLavaHazard) return p; return Placed{}; };
    check(lava().type && near(lava().x(), 3 * U) && near(lava().at(1), 3), "lava placed on the cells, three long");
    check(itemNear(doc->course(), &kLavaHazard, 5.4 * U, 0, 0.3) >= 0 && itemNear(doc->course(), &kLavaHazard, 7.5 * U, 0, 0.3) < 0,
          "...hit anywhere along it, not past its end");
    check(!itemFits(doc->course(), kLavaHazard, {53 * U, 4}), "...never running off the course's end");
    check(sceneJson(*doc, &tools).find("\"x1\":" + levelNum(6 * U)) != std::string::npos, "...its end in the scene");
    Course c = doc->course();
    const auto clip = extractCols(c, 0, 4);
    bool copied = false;
    for (const Placed& p : clip->items) copied = copied || p.type == &kLavaHazard;
    check(!copied, "columns copied that cut into a stretch of lava: it isn't copied");
    c.length = courseLength(c); // (room to take one out)
    check(deleteCols(c, 4, 4), "a column taken out of it...");
    bool still = false;
    for (const Placed& p : c.items) still = still || p.type == &kLavaHazard;
    check(!still, "...takes the lava with it");
    check(levelText(toData(doc->course())).find("lasers\n") != std::string::npos && parseLevel(4, doc->text()).has_value(), "the level writes and reads with them");
}

// a spring: placed on the surface, its lift its second number (6 by default); the scene carries its flight (its arc
// and how it ends) for the canvas to draw; one against a wall's face says so
void springs() {
    auto doc = EditorDoc::open(4, "; A spring.\nlength 40\ndifficulty 0 0.35\nraids none\ncourse\n20 0 crate\n30 6 crate\n30 0 crate\nsaws\ndiscos\n");
    EditorTools tools(*doc);
    tools.setTool("spring");
    tools.pointer(Phase::Begin, 4.03, 0.1);
    tools.pointer(Phase::End, 4.03, 0.1);
    const auto spring = [&] { for (const Placed& p : doc->course().items) if (p.type == &kSpringHazard) return p; return Placed{}; };
    check(spring().type && near(spring().x(), 4.0) && near(spring().at(1), 6), "a spring placed on the street, on the decimetre, its lift 6");
    const std::string scene = sceneJson(*doc, &tools);
    check(scene.find("\"arc\":[[4,0]") != std::string::npos && scene.find("\"end\":\"") != std::string::npos, "...its flight in the scene, from its pad");
    check(doc->edit("Closer", [](Course& c) { for (Placed& p : c.items) if (p.type == &kSpringHazard) p.v[0] = 11.5; return true; }), "(moved up against the tower)");
    check(sceneJson(*doc, &tools).find("\"end\":\"wall\"") != std::string::npos, "...into its face: the scene says so");
    auto hard = EditorDoc::open(4, "length 40\ndifficulty 0 0.35\nraids none\nhard 9\ncourse\n70 0 crate\nsaws\ndiscos\n");
    check(hard && hard->course().hard == 9 && hard->text().find("\nhard 9\n") != std::string::npos, "a super hard level's hard line: kept by the editor");
    check(doc->text().find("springs\n11.5 6\n") != std::string::npos && parseLevel(4, doc->text()).has_value(), "the level writes and reads with it");
}

// the Bouncers tools: a bumper or a gate wall as high as the finger (1 at least), never the course's first column,
// nothing over it; the level writes them as wall kinds and the game makes their group a bounce zone
void bouncers() {
    auto doc = EditorDoc::open(4, "; Bouncers.\nlength 40\ndifficulty 0 0.35\nraids none\ncourse\n70 0 crate\nsaws\ndiscos\n");
    EditorTools tools(*doc);
    auto drag = [&](const char* tool, double x0, double x1, double y) {
        tools.setTool(tool);
        tools.pointer(Phase::Begin, x0, y); tools.pointer(Phase::Move, x1, y); tools.pointer(Phase::End, x1, y);
    };
    drag("gate>", 10 * U + 0.1, 10 * U + 0.1, 3 * U);
    drag("bumper", 22 * U + 0.1, 23 * U + 0.1, 4 * U);
    const Course& c = doc->course();
    check(c.cols[10].kind == WallKind::GateR && c.cols[10].h == 3 && c.cols[22].kind == WallKind::Bumper && c.cols[23].h == 4, "a gate 3 high, a bumper 4 high, as high as the finger");
    check(doc->text().find("\n1 3 gate>\n11 0 crate\n2 4 bumper\n") != std::string::npos, "...written as wall kinds");
    drag("bumper", 0.1, 0.1, 3 * U);
    check(c.cols[0].kind == WallKind::Crate && c.cols[0].h == 0, "never the course's first column");
    tools.setTool("barrier");
    tools.pointer(Phase::Begin, 22 * U + 0.1, 8 * U); tools.pointer(Phase::End, 22 * U + 0.1, 8 * U);
    check(c.cols[22].ceil == 0, "no barrier over a bumper");
    check(doc->level().zones.size() == 1 && !doc->report().notes.size(), "the game's one bounce zone, and nothing to note");
    drag("wall", 22 * U + 0.1, 23 * U + 0.1, 0);
    check(c.cols[22].h == 0 && c.cols[22].kind == WallKind::Crate, "a bumper lowered to the street: street again");
}

// Roll a new course with the designer's chunks (Chunk.h): the history step names them, the doc says where they are
void rollWithChunks() {
    const auto hurdle = parseChunk("hurdle", "chunk course\nranges 31-40\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n16.8\n");
    check(hurdle.has_value(), "a chunk reads");
    if (!hurdle) return;
    auto doc = EditorDoc::open(33, levelText(bakeLevel(33, nullptr)));
    if (!doc) { check(false, "a level 33 open"); return; }
    GenOptions with;
    with.chunks = {*hurdle};
    uint32_t seed = 1;
    while (seed < 12 && (!doc->regenerate(seed, with) || doc->chunksUsed().empty())) ++seed;
    check(!doc->chunksUsed().empty() && doc->chunksUsed()[0].id == "hurdle", "a roll with a chunk in range: the doc says which it placed");
    const std::string label = doc->history().back().label;
    check(label.find("A new course (seed " + std::to_string(seed) + ")") == 0 && label.find(": chunks: hurdle") != std::string::npos, "...and the history step names it");
    EditorTools tools(*doc);
    tools.setTool("wall");
    tools.pointer(Phase::Begin, 2.1 * U, 3 * U);
    tools.pointer(Phase::End, 2.1 * U, 3 * U);
    check(doc->chunksUsed().empty(), "...none once the course has been edited since");
    check(doc->regenerate(seed, {}) && doc->chunksUsed().empty() && doc->history().back().label == "A new course (seed " + std::to_string(seed) + ")", "a roll without chunks: no chunks, the plain label");
    GenOptions far;
    far.chunks = {*hurdle};
    far.chunks[0].ranges = {{50, 60}};
    check(doc->regenerate(seed + 1, far) && doc->chunksUsed().empty() && [&] { Course c = doc->course(); c.notes.clear(); Course e = fromData(bakeLevel(33, nullptr, seed + 1)); e.notes.clear(); return levelText(toData(c)) == levelText(toData(e)); }(), "a chunk out of range: the roll is as without it");

    // a chunk that spoils every course: the roll goes ahead without it at the density asked, and the doc says so
    const auto spoiler = parseChunk("poison", "chunk course\nranges 31-40\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n15.6\n16.8\n18\n");
    auto doc35 = EditorDoc::open(35, levelText(bakeLevel(35, nullptr)));
    if (!spoiler || !doc35) { check(false, "a spoiling chunk and a level 35 open"); return; }
    GenOptions spoil;
    spoil.chunks = {*spoiler};
    spoil.density = 5;
    check(doc35->regenerate(2, spoil) && doc35->chunksDropped() == std::vector<std::string>{"poison"} && doc35->chunksUsed().empty(), "a chunk that spoils every course: the roll still gives a course, and the doc names what it left out");
    check(doc35->chunksNote() == "Left out: poison \xE2\x80\x94 no course passed with it at level 35", "...with the notice's words");
    check(doc35->history().back().label == "A new course (seed 2, density 5): left out: poison", "...and the history step says so");
    EditorTools tools35(*doc35);
    tools35.setTool("wall");
    tools35.pointer(Phase::Begin, 2.1 * U, 3 * U);
    tools35.pointer(Phase::End, 2.1 * U, 3 * U);
    check(doc35->chunksDropped().empty() && doc35->chunksNote().empty(), "...none once the course has been edited since");
    check(doc->regenerate(seed, {}) && doc->chunksDropped().empty() && doc->chunksNote().empty(), "a roll that left nothing out: no note");
}

// the C API Swift calls: ed_regenerate_chunks and ed_chunks_used_json
void cApi() {
    const std::string text = levelText(bakeLevel(33, nullptr));
    ed_doc* doc = ed_open(33, text.c_str(), nullptr);
    if (!doc) { check(false, "a level open through the C API"); return; }
    const std::string bundle = "=== hurdle\nchunk course\nranges 31-40\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n16.8\n"
                               "=== well\nchunk maze\nranges 31-40\nweight 3\nuses 1\nmirror yes\n---\n##.##\n#...#\n";
    bool placed = false;
    for (unsigned seed = 1; seed < 12 && !placed; ++seed) {
        if (!ed_regenerate_chunks(doc, seed, nullptr, 0, bundle.c_str())) continue;
        char* js = ed_chunks_used_json(doc);
        placed = std::string(js).find("\"id\":\"hurdle\"") != std::string::npos;
        ed_free(js);
    }
    check(placed, "ed_regenerate_chunks: a course with the chunk in it, ed_chunks_used_json says where (a maze chunk in the bundle is left alone)");
    check(ed_regenerate_chunks(doc, 50, nullptr, 0, "") == 1, "an empty bundle rolls as ed_regenerate does");
    char* js = ed_chunks_used_json(doc);
    check(std::string(js) == "[]", "...no chunks used");
    ed_free(js);
    char* quiet = ed_chunks_note(doc);
    check(std::string(quiet).empty(), "ed_chunks_note: nothing left out, nothing said");
    ed_free(quiet);
    ed_close(doc);
    doc = ed_open(35, levelText(bakeLevel(35, nullptr)).c_str(), nullptr);
    const std::string spoiler = "=== poison\nchunk course\nranges 31-40\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n15.6\n16.8\n18\n";
    check(doc && ed_regenerate_chunks(doc, 2, nullptr, 5, spoiler.c_str()) == 1, "a roll with a chunk that spoils every course goes ahead");
    char* note = doc ? ed_chunks_note(doc) : nullptr;
    check(note && std::string(note) == "Left out: poison \xE2\x80\x94 no course passed with it at level 35", "ed_chunks_note: what it left out and why");
    ed_free(note);
    if (!doc) return;
    check(ed_regenerate_chunks(doc, 51, nullptr, 0, "=== broken\nchunk course\n") == 0, "a chunk that doesn't read: no roll");
    char* why = ed_last_error(doc);
    check(std::string(why).find("broken") != std::string::npos, "...and the error names it");
    ed_free(why);
    ed_close(doc);
}

// the maze side of the C API Swift calls (stage 4): a maze chunk open in chunk mode, its checker's report with the flipped one,
// and gd_generate_chunks
void mazeCApi() {
    char* err = nullptr;
    check(gd_open_mode("well", ">.\n", 1, &err) == nullptr && err && *err, "gd_open_mode: a chunk with a door in it doesn't open, and says why");
    ed_free(err);
    gd_doc* doc = gd_open_mode("well", ".....\n.....\n##...\n##S##\n", 1, nullptr);
    if (!doc) { check(false, "a maze chunk open through the C API"); return; }
    char* scene = gd_scene_json(doc);
    check(std::string(scene).find("\"chunk\":true") != std::string::npos && std::string(scene).find("\"entry\":null") != std::string::npos, "its scene says it's a chunk, with no doors");
    ed_free(scene);
    gd_doc* maze = gd_open("room", "##########\n#........#\n..........\n>........X\n###S######\n", nullptr);
    scene = maze ? gd_scene_json(maze) : nullptr;
    check(scene && std::string(scene).find("\"chunk\":false") != std::string::npos, "a maze's scene says it isn't");
    ed_free(scene);
    if (maze) gd_close(maze);
    gd_set_mirror_check(doc, 1);
    gd_job* job = gd_job_new();
    gd_job_kick(job, doc, 20, "");
    std::string json;
    for (int i = 0; i < 400 && json.empty(); ++i) {
        int rev = -1;
        char* p = gd_job_poll(job, &rev);
        if (p) json = p;
        ed_free(p);
        if (json.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(json.find("\"flipped\":{") != std::string::npos && json.find("\"solution\":{\"reachable\":true") != std::string::npos, "a chunk's job: the corridor's report, the flipped one in it");
    std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    int rev = -1;
    char* more = gd_job_poll(job, &rev);
    check(more == nullptr && gd_job_busy(job) == 0, "...and no game run after it (the game's wait is 1.2 s: by now it would be playing)");
    ed_free(more);
    gd_job_free(job);
    gd_close(doc);

    // gd_generate_chunks: a maze chunk in range laid and reported; out of range, the maze as gd_generate makes it
    const std::string bundle = "=== well\nchunk maze\nranges 20\nweight 10\nuses any\nmirror yes\n---\n.....\n.....\n##...\n##S##\n"
                               "=== hurdle\nchunk course\nranges 20\nweight 10\nuses any\nmirror no\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\n";
    char* made = gd_generate_chunks(20, "", 170, 3, 31u, "", 0, bundle.c_str());
    const std::string with = made;
    ed_free(made);
    check(with.find("\"ok\":true") != std::string::npos && with.find("\"chunks\":[{\"id\":\"well\",\"floor\":") != std::string::npos && with.find("chunks: well") != std::string::npos, "gd_generate_chunks: a maze with the chunk in it, where it went in its JSON and its description");
    check(with.find("hurdle") == std::string::npos || with.find("chunks: well") != std::string::npos, "...a course chunk in the bundle is left alone");
    made = gd_generate_chunks(21, "", 170, 3, 31u, "", 0, bundle.c_str());
    char* plain = gd_generate(21, "", 170, 3, 31u, "", 0);
    const std::string out = made, none = plain;
    ed_free(made);
    ed_free(plain);
    check(out == none && out.find("\"chunks\":[]") != std::string::npos && out.find("\"ok\":true") != std::string::npos, "out of range: exactly the maze gd_generate makes, no chunks");
    check(with.find("\"chunkNote\":\"\"") != std::string::npos && with.find("\"dropped\":[],\"reduced\":[]") != std::string::npos, "a chunk that fits: no note in the JSON, nothing dropped or reduced");

    // a chunk no maze holds: a maze all the same, and the JSON says what was given up
    const std::string demanding = "=== three-saws\nchunk maze\nranges 20\nweight 10\nuses any\nmirror yes\n---\n.....\n..#..\n.###.\nS#S#S\n";
    made = gd_generate_chunks(20, "", 170, 7, 977u, "", 0, demanding.c_str());
    const std::string gaveUp = made;
    ed_free(made);
    check(gaveUp.find("\"ok\":true") != std::string::npos && (gaveUp.find("\"chunkNote\":\"Fewer of: three-saws") != std::string::npos || gaveUp.find("\"chunkNote\":\"Left out: three-saws") != std::string::npos) &&
          (gaveUp.find("\"dropped\":[\"three-saws\"]") != std::string::npos || gaveUp.find("\"reduced\":[\"three-saws\"]") != std::string::npos), "gd_generate_chunks: a demanding chunk is given up on, a maze comes back, chunkNote says so");
}

// a chunk's body open (EditorDoc::setChunkMode): shorter, kept to a chunk's rules, judged flipped, renumbered
const char* kWellBody = "length 26\ndifficulty 1085 0.35\nraids auto\ncourse\n13 0 crate\n8 3 hay\n10 1 crate\n13 1 crate\nsaws\n9.6\ndiscos\nlasers\n12.6 1 1\nlava\n14.4 3\n";
const char* kFlatBody = "length 21\ndifficulty 70 0.35\nraids auto\ncourse\n13 0 crate\n10 0 hay\n13 0 crate\n";

void chunkBody() {
    auto level = EditorDoc::open(31, kFlatBody);
    auto doc = EditorDoc::open(31, kFlatBody);
    if (!doc || !level) { check(false, "a chunk body opens"); return; }
    level->setMeta("length", "10");
    check(near(level->course().length, 30) && level->minLength() == kMinLength, "a level: the finish line keeps 30 m");
    doc->setChunkMode(EditorDoc::ChunkMode::Chunk);
    check(doc->chunkMode() == EditorDoc::ChunkMode::Chunk && doc->minLength() == kMinChunkLength, "a chunk's body: a shorter minimum");
    check(doc->setMeta("length", "15") && near(doc->course().length, 15), "...the finish line at 15 m");
    check(doc->setMeta("length", "5") && near(doc->course().length, 12), "...never under 12 m");
    check(!doc->history().empty() && doc->history().back().label.find("Finish line at") == 0, "...a step of the history like any");
    Course c = doc->course();
    check(!deleteCols(c, 0, 40, 12) && deleteCols(c, 20, 24, 12) && c.length < 21, "taking columns out keeps the minimum given, not 30");

    // the Columns tool's take-out keeps the same minimum
    auto lvl2 = EditorDoc::open(31, kFlatBody);
    auto chk2 = EditorDoc::open(31, kFlatBody);
    chk2->setChunkMode(EditorDoc::ChunkMode::Chunk);
    EditorTools lt(*lvl2), ct(*chk2);
    lt.selC0 = ct.selC0 = 14;
    lt.selC1 = ct.selC1 = 18;
    check(!lt.deleteSelection() && ct.deleteSelection() && chk2->course().length < 21 && chk2->course().length >= kMinChunkLength, "columns taken out (the selection): a level keeps its 30 m, a chunk its 12 m");
    ct.selC0 = 0;
    ct.selC1 = 25;
    check(!ct.deleteSelection(), "...and no further than that");

    // a hazard into a pad is turned away, with the reason
    const size_t had = doc->history().size();
    const bool saw = doc->edit("A saw", [](Course& k) { return addItem(k, kSawHazard, {3.0}) >= 0; });
    check(!saw && doc->lastError().find("run-in") != std::string::npos && doc->history().size() == had, "a saw into the run-in: turned away, nothing in the history, the run-in named");
    check(!doc->edit("A saw", [](Course& k) { return addItem(k, kSawHazard, {19.0}) >= 0; }) && doc->lastError().find("run-out") != std::string::npos, "...a saw in the run-out, too");
    const std::string beforeBarrier = doc->text();
    const bool barrier = doc->edit("Barrier", [](Course& k) { return setCeil(k, 0, 4); });
    check(!barrier && doc->text() == beforeBarrier, "a barrier over the run-in's first cell (its wall no longer plain): turned away");
    check(doc->edit("A saw", [](Course& k) { return addItem(k, kSawHazard, {10.2}) >= 0; }) && doc->chunkProblem().empty(), "a saw between the pads: kept");
    auto asLevel = EditorDoc::open(31, kFlatBody);
    check(asLevel->edit("A saw", [](Course& k) { return addItem(k, kSawHazard, {3.0}) >= 0; }), "(the same edit in a level is not turned away)");

    // already breaking the rules: the way to mend it stays open
    auto broken = EditorDoc::open(31, "length 21\ndifficulty 70 0.35\nraids auto\ncourse\n13 0 crate\n10 0 hay\n13 0 crate\nsaws\n3\n");
    broken->setChunkMode(EditorDoc::ChunkMode::Chunk);
    double at = -1;
    check(!broken->chunkProblem(&at).empty() && near(at, 3), "a body with a saw in its run-in: the problem and where");
    check(broken->edit("Another", [](Course& k) { return setHeight(k, 15, 1); }), "...an edit that leaves it so is let through (the way to mend it stays open)");
    check(broken->edit("Mend", [](Course& k) { return removeItem(k, 0); }) && broken->chunkProblem().empty(), "...and the mend clears the problem");
    broken->undo();
    const std::string scene = sceneJson(*broken, nullptr);
    check(scene.find("\"kind\":\"chunk\"") != std::string::npos && scene.find("run-in") != std::string::npos, "the scene's notes carry it, kind chunk, for the canvas");
    check(sceneJson(*level, nullptr).find("\"kind\":\"chunk\"") == std::string::npos, "(a level has none)");

    // renumbered: the zone and difficulty are the level's, the history goes with it
    auto num = EditorDoc::open(5, kFlatBody);
    num->setChunkMode(EditorDoc::ChunkMode::Chunk);
    num->setMeta("length", "15");
    check(!num->setNumber(5) && !num->setNumber(0) && num->setNumber(14) && num->number() == 14, "setNumber: another level, not the same or under 1");
    check(sceneJson(*num, nullptr).find("\"zone\":\"egypt\"") != std::string::npos, "...a chunk numbered 14 looks like Egypt's");
    num->undo();
    check(num->number() == 14 && near(num->course().length, 21), "...an undo keeps the new number");
    num->redo();
    check(num->number() == 14, "...and a redo");
}

void chunkFlipped() {
    auto doc = EditorDoc::open(31, kWellBody);
    if (!doc) { check(false, "a chunk body opens"); return; }
    doc->setChunkMode(EditorDoc::ChunkMode::Chunk);
    check(doc->flippedReport() == nullptr && doc->flippedText().empty(), "Mirror off: nothing judged flipped");
    const std::string plain = sceneJson(*doc, nullptr);
    check(plain.find("Flipped:") == std::string::npos, "...no note says flipped");
    doc->setChunkMode(EditorDoc::ChunkMode::Flip);
    check(doc->flippedReport() != nullptr && !doc->flippedText().empty(), "Mirror on: the flipped chunk is judged, its text kept for the bot");
    const std::string flipped = sceneJson(*doc, nullptr);
    check(flipped.find("\"text\":\"Flipped: ") != std::string::npos, "...and the scene's notes say what is unfair flipped");
    const auto back = parseLevel(31, doc->flippedText());
    check(back && back->course.size() == 4 && back->course.front().cells == 13 && back->course[1].h == 1, "...the text is the walls reversed");
    // a note at x flipped lands at reach - x
    const double len = doc->asChunk().reach();
    check(len > 26 && flipped.find("\"x\":9.6,\"y\":1.8,\"text\":\"Flipped: ") != std::string::npos, "...the notes at the places they are unflipped (the chunk's own x, where the hazard is drawn)");
    doc->edit("A spring", [](Course& k) { return addItem(k, kSpringHazard, {10.2}) >= 0; });
    check(!doc->asChunk().canMirror() ? doc->flippedReport() == nullptr && doc->flippedText().empty() : true, "a spring in it: it can't be flipped, nothing judged flipped");
    auto fair = EditorDoc::open(31, kFlatBody);
    fair->setChunkMode(EditorDoc::ChunkMode::Flip);
    check(fair->flippedReport() && fair->flippedReport()->notes.empty() && sceneJson(*fair, nullptr).find("Flipped:") == std::string::npos, "a flat chunk: fair flipped, no note");
    fair->setChunkMode(EditorDoc::ChunkMode::None);
    check(fair->flippedReport() == nullptr, "a level: nothing flipped");
}

// the chunk files through the C API Swift calls
void chunkFiles() {
    char* bm = ck_blank_maze("well", 7);
    const std::string mazeFile = bm;
    ed_free(bm);
    const auto maze = parseChunk("well", mazeFile);
    check(maze && maze->maze && maze->ranges.size() == 1 && maze->ranges[0] == ChunkRange{7, 7} && maze->rows.size() == 5 && maze->rows[0].size() == 9 && maze->rows.back() == "####S####" && !maze->mirror && maze->uses == 0 && maze->weight == 3,
          "ck_blank_maze: a new maze chunk that reads: a 9 x 5 block, a saw in its floor, level 7 only");
    const auto doc2 = maze ? GridDoc::open("well", std::string(mazeFile.substr(mazeFile.find("---\n") + 4)), nullptr, true) : nullptr;
    check(doc2 && doc2->rows() == maze->rows && Gauntlet::parse("c", doc2->checkRows()).has_value() && Gauntlet::parse("c", doc2->checkRows())->solve().reachable, "...which opens in chunk mode and whose corridor can be crossed");
    char* pj = ck_parse("well", mazeFile.c_str());
    const std::string parsedJson = pj;
    ed_free(pj);
    check(parsedJson.find("\"kind\":\"maze\"") != std::string::npos && parsedJson.find("\"canMirror\":true") != std::string::npos, "ck_parse: a maze chunk can be mirrored");
    pj = ck_parse("well", (mazeFile.substr(0, mazeFile.find("####S####")) + "####^####\n").c_str());
    check(std::string(pj).find("\"canMirror\":false") != std::string::npos && std::string(pj).find("spring") != std::string::npos, "...not with a spring, and why");
    ed_free(pj);
    char* blank = ck_blank("hurdles", 20);
    const std::string file = blank;
    ed_free(blank);
    const auto parsed = parseChunk("hurdles", file);
    check(parsed && parsed->ranges.size() == 1 && parsed->ranges[0] == ChunkRange{20, 20} && parsed->body.course.size() == 3 && parsed->uses == 0 && !parsed->mirror && parsed->weight == 3,
          "ck_blank: a new course chunk that reads: a run-in, a hay wall, a run-out, level 20 only");
    check(parsed && parsed->body.length <= parsed->reach() + 1e-6 && parsed->runIn() == 13 && parsed->runOut() == 13, "...its pads about 8 m, its finish line where its walls end");
    ed_doc* doc = ed_open(20, parsed ? levelText(parsed->body).c_str() : "", nullptr);
    check(doc != nullptr, "...and its body opens as a level");
    if (doc) {
        ed_set_chunk(doc, 2);
        char* js = ed_chunk_json(doc);
        const std::string info = js;
        ed_free(js);
        check(info.find("\"canMirror\":true") != std::string::npos && info.find("\"runIn\":13") != std::string::npos && info.find("\"runOut\":13") != std::string::npos && info.find("\"problem\":\"\"") != std::string::npos,
              "ed_chunk_json: it can be flipped, its pads, no problem");
        check(ed_set_number(doc, 33) == 1 && ed_number(doc) == 33 && ed_set_number(doc, 33) == 0, "ed_set_number renumbers it");
        ed_set_chunk(doc, 0);
        ed_close(doc);
    }
    char* p = ck_parse("hurdles", file.c_str());
    const std::string js = p;
    ed_free(p);
    check(js.find("\"ok\":true") != std::string::npos && js.find("\"error\":\"\"") != std::string::npos && js.find("\"kind\":\"course\"") != std::string::npos && js.find("\"ranges\":[[20,20]]") != std::string::npos &&
              js.find("\"weight\":3") != std::string::npos && js.find("\"uses\":0") != std::string::npos && js.find("\"first\":20") != std::string::npos && js.find("\"canMirror\":true") != std::string::npos,
          "ck_parse: the header's fields, its first level");
    // a text from parts reads back
    char* t = ck_text(0, "Hurdles.\nTwo lines.", "20-22 5", 7, 2, 1, "length 21\ndifficulty 70 0.35\nraids auto\ncourse\n13 0 crate\n10 0 hay\n13 0 crate\n");
    const std::string made = t;
    ed_free(t);
    const auto round = parseChunk("hurdles", made);
    check(round && round->notes.size() == 2 && round->ranges.size() == 2 && round->ranges[1] == ChunkRange{5, 5} && round->weight == 7 && round->uses == 2 && round->mirror, "ck_text: header and body make a file that reads: notes, ranges, weight, uses, mirror");
    t = ck_text(0, "", "bad 0 9-2 -", 99, 500, 0, "x\n");
    const std::string clamped = t;
    ed_free(t);
    check(clamped.find("ranges 1\n") != std::string::npos && clamped.find("weight 10") != std::string::npos && clamped.find("uses 99") != std::string::npos && clamped.find("mirror no") != std::string::npos, "...bad ranges left out (1 when none are left), weight and uses clamped");
    // lenient: a body that breaks the rules is still read, with why
    std::string bad = file;
    bad += "saws\n3\n";
    p = ck_parse("hurdles", bad.c_str());
    const std::string lenient = p;
    ed_free(p);
    check(lenient.find("\"ok\":true") != std::string::npos && lenient.find("run-in") != std::string::npos && lenient.find("\"body\":\"length") != std::string::npos, "ck_parse: a body breaking the rules: header and body still come back, the error says why");
    p = ck_parse("Bad Name", file.c_str());
    check(std::string(p).find("\"ok\":false") != std::string::npos, "...a bad name: not ok");
    ed_free(p);
    p = ck_parse("x", "chunk course\nweight 3\n---\n");
    check(std::string(p).find("\"ok\":false") != std::string::npos && std::string(p).find("ranges") != std::string::npos, "...no ranges: not ok, why");
    ed_free(p);
    check(ck_valid_id("sacred-wells_2") == 1 && ck_valid_id("Sacred") == 0 && ck_valid_id("") == 0 && ck_valid_id("a b") == 0, "ck_valid_id: lower case letters, digits, '-' and '_'");
    // the game's own chunks (assets/chunks): an object, each id with the text of a file that reads back as that chunk
    char* builtin = ck_builtin_json();
    const std::string builtinJson = builtin ? builtin : "";
    ed_free(builtin);
    check(builtinJson.size() >= 2 && builtinJson.front() == '{' && builtinJson.back() == '}', "ck_builtin_json: the game's chunks as {\"id\":\"file text\"}");
    size_t count = 0;
    for (const Chunk& c : LevelCatalog::builtin().chunks()) {
        ++count;
        const std::string text = chunkText(c);
        std::string esc;
        for (char ch : text) esc += ch == '\n' ? std::string("\\n") : ch == '"' ? std::string("\\\"") : std::string(1, ch);
        check(builtinJson.find("\"" + c.id + "\":\"" + esc + "\"") != std::string::npos && parseChunk(c.id, text).has_value(), "...each chunk built in: its id and its file");
    }
    check(count > 0 || builtinJson == "{}", "...none built in: an empty object");
    // a stand-in catalog: the same JSON for any bundle, in the list's order, a chunk that doesn't read left out
    const std::string standIn = "=== late\nchunk course\nranges 31-35\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\n"
                                "=== broken\nchunk course\n"
                                "=== early\nchunk maze\nranges 1-5\n---\n.....\n##S##\n";
    char* files = ck_files_json(standIn.c_str());
    const std::string filesJson = files ? files : "";
    ed_free(files);
    check(filesJson.front() == '{' && filesJson.find("\"early\":\"") == 1 && filesJson.find("\"late\":\"") > filesJson.find("\"early\":\"") && filesJson.find("broken") == std::string::npos, "ck_files_json: each chunk that reads as {\"id\":\"file text\"}, in the list's order, the broken one left out");
    check(filesJson.find("\"early\":\"; ") == std::string::npos && filesJson.find("chunk maze\\nranges 1-5\\n") != std::string::npos && filesJson.find("##S##\\n") != std::string::npos && filesJson.find("length 33\\ndifficulty 1050 0.35\\nraids auto\\ncourse\\n13 0 crate\\n") != std::string::npos, "...the text escaped, the files as chunkText writes them");
    files = ck_files_json("");
    check(std::string(files) == "{}", "...an empty bundle: an empty object");
    ed_free(files);
    files = ck_files_json(nullptr);
    check(std::string(files) == "{}", "...no bundle: the same");
    ed_free(files);
    // the list's order
    const std::string bundle = "=== late\nchunk course\nranges 16-20\nweight 1\nuses any\nmirror no\n---\nx\n=== wide\nchunk course\nranges 2-9\nweight 1\nuses any\nmirror no\n---\nx\n"
                               "=== early\nchunk maze\nranges 1-5\nweight 1\nuses any\nmirror no\n---\nx\n=== broken\nnonsense\n=== also\nchunk course\nranges 2-9\nweight 1\nuses any\nmirror no\n---\nx\n";
    p = ck_sort(bundle.c_str());
    check(std::string(p) == "[\"early\",\"also\",\"wide\",\"late\",\"broken\"]", "ck_sort: the list's order (bands of five by the first range's start, then start, end, id), one that doesn't read last");
    ed_free(p);
    p = ck_sort("");
    check(std::string(p) == "[]", "...none: []");
    ed_free(p);
}

int main() {
    mazeCApi();
    chunkBody();
    chunkFlipped();
    chunkFiles();
    rollWithChunks();
    cApi();
    readsAndWritesBack();
    walls();
    hazards();
    columns();
    gauntlets();
    forecourtAsTheGameLaysIt();
    historyAndRejects();
    historyKept();
    tools();
    eraser();
    faces();
    stairDown();
    stairUp();
    lasersAndLava();
    renamed();
    springs();
    bouncers();
    return test::finish("EditorDoc");
}
