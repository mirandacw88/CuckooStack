// A maze open in the iPhone and iPad editor (GridDoc.h): the web editor's grid edits, one for one (its
// tools/level_editor/tests/grid.test.mjs cases); every shipped grid reads and writes back byte for byte; an edit the game
// won't read is turned away with why; a drag is one step in the history, and the history goes out and comes back; the
// tools; and the maze's checker in the background (GridJob.h) reports what gauntlet_check --json does.
#include "TestUtil.h"
#include "core/Gauntlet.h"
#include "core/GauntletReport.h"
#include "core/LevelCatalog.h"
#include "core/Chunk.h"
#include "editor/EditorDoc.h"
#include "editor/MazeGen.h"
#include "editor/GridDoc.h"
#include "editor/LevelMerge.h"
#include "editor/GridJob.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

using namespace cs;
using namespace cs::ed;
using test::check;

namespace {

const Rows kRoom = {
    "##########",
    "#........#",
    "..........",
    ">........X",
    "###S######",
};

void edits() {
    check(grid::setCell(kRoom, 2, 1, 'B')[1] == "#.B......#", "a cell painted");
    check(grid::setCell(kRoom, 0, 3, '.') == kRoom, "...never over a door");
    const Rows filled = grid::fillRect(kRoom, 0, 1, 9, 3, '#');
    check(filled[1] == "##########" && filled[3] == ">########X", "a box filled, the doors kept");

    const Rows taller = {"##########", "#........#", "#........#", "#........#", ">........X", "##########"};
    const Rows moved = grid::moveDoor(taller, '>', 2);
    std::string left;
    for (const std::string& r : moved) left += r[0];
    check(left == "#.>###", "the entry moved: its old opening walled up, the new one as tall as its passage");
    std::string right;
    for (const std::string& r : grid::moveDoor(taller, 'X', 3)) right += r[9];
    check(right == "#..X##", "...the exit too");
    check(grid::moveDoor(taller, '>', 0) == taller, "...never to the top row");

    const Rows wider = grid::duplicateCols(kRoom, 3, 3);
    check(grid::cols(wider) == 11 && wider[4] == "###SS######", "a column copied in after itself");
    check(grid::duplicateCols(kRoom, 0, 0)[3] == ">.........X", "...a door's column: the one next to it copied");
    check(grid::deleteCols(kRoom, 3, 4)[4] == "########", "columns taken out");
    check(grid::deleteCols(kRoom, 9, 9)[3] == kRoom[3], "...never the exit's");
    check(grid::deleteCols(kRoom, 1, 8) == kRoom, "...never under 4 columns");

    const Rows up = grid::duplicateRows(kRoom, 1, 1);
    check(up.size() == 6 && up[1] == "#........#" && up[4] == ">........X", "a row copied in over itself; the door keeps its row from the street");
    int doors = 0;
    for (const std::string& r : grid::duplicateRows(kRoom, 3, 3)) doors += r.find('>') != std::string::npos;
    check(doors == 1, "...a door is never copied");
    check(grid::deleteRows(kRoom, 3, 3) == kRoom, "a door's row is never taken out");
    check(grid::deleteRows(kRoom, 1, 1).size() == 4, "a row taken out");

    const Rows g = {"##########", "#........#", "#........#", "#..##....#", "#........#", ">........X", "####S#####"};
    check(grid::standable(g, 2, 1) == 5, "a start clicked in the air: where she lands, down the column");
    check(grid::standable(g, 3, 1) == 2, "...on a block, room over her");
    check(!grid::standable(g, 4, 5), "...not on a blade");
    check(!grid::standable(g, 0, 5), "...never a door's column");

    const Rows t = grid::templateRoom(30, 8);
    check(t.size() == 8 && grid::cols(t) == 30 && t[0] == std::string(30, '#') && Gauntlet::parse("t", t).has_value(), "a new gauntlet's room reads");
}

std::string fileText(const std::string& name) {
    std::ifstream in(std::string(CS_SOURCE_DIR) + "/assets/gauntlets/" + name + ".txt", std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

void roundTrips() {
    for (const std::string& name : LevelCatalog::builtin().gauntletNames()) {
        const std::string text = fileText(name);
        std::string why;
        auto doc = GridDoc::open(name, text, &why);
        check(doc && doc->text() == text, ("the " + name + " reads and writes back byte for byte " + why).c_str());
        check(doc && doc->rows() == LevelCatalog::builtin().gauntlet(name)->rowsTopFirst(), "...its rows the game's");
    }
}

void history() {
    std::string text;
    for (const std::string& r : kRoom) text += r + "\n";
    auto doc = GridDoc::open("room", text);
    check(doc != nullptr, "a grid opens");
    GridTools tools(*doc);
    tools.setTool("brush");
    tools.setCell('B', '#');
    tools.pointer(GridTools::Phase::Begin, 1, 1);
    tools.pointer(GridTools::Phase::Move, 4, 1);
    tools.pointer(GridTools::Phase::End, 4, 1);
    check(doc->rows()[1] == "#BBBB....#", "a brush stroke: every cell it crossed");
    check(doc->history().size() == 1 && doc->history()[0].c == 1, "...one step in the history, where it began");
    check(!doc->edit("roof", [](const Rows& r) { return grid::setCell(r, 4, 0, '.'); }) && !doc->lastError().empty(),
          ("air in the top row: turned away, with why (" + doc->lastError() + ")").c_str());
    tools.setErase(true);
    tools.setTool("box");
    tools.pointer(GridTools::Phase::Begin, 5, 1);
    tools.pointer(GridTools::Phase::Move, 6, 2);
    check(tools.preview().kind == "box" && tools.preview().c1 == 6, "a box: its preview while it's dragged");
    tools.pointer(GridTools::Phase::End, 6, 2);
    check(doc->rows()[1] == "#BBBB##..#" && doc->rows()[2] == ".....##...", "...filled with the second cell (the eraser)");
    tools.setErase(false);
    tools.setTool("door");
    tools.pointer(GridTools::Phase::Begin, 9, 2);
    tools.pointer(GridTools::Phase::End, 9, 2);
    check(doc->rows()[2].back() == 'X' && doc->rows()[3].back() == '#', "the door tool: the nearer door (the exit) moved");
    tools.setTool("cols");
    tools.pointer(GridTools::Phase::Begin, 7, 0);
    tools.pointer(GridTools::Phase::End, 8, 0);
    check(grid::cols(doc->rows()) == 12, "the columns tool: two copied in");
    tools.setTool("start");
    tools.pointer(GridTools::Phase::Begin, 7, 1);
    tools.pointer(GridTools::Phase::End, 7, 1);
    check(doc->start && doc->start->second == 3, "the start tool: where she'd stand (not an edit)");
    check(doc->history().size() == 4, "four edits, four steps");

    const std::string exported = doc->historyExport();
    const Rows now = doc->rows();
    check(doc->undo() && doc->undo() && grid::cols(doc->rows()) == 10, "undone twice");
    check(doc->goTo(4) && doc->rows() == now, "...and back to the last step");
    check(doc->setDescription({"A test room.", "", "  indented"}) && doc->text().rfind("; A test room.\n;\n;   indented\n", 0) == 0, "its description: written as its file has it");
    auto again = GridDoc::open("room", doc->text());
    check(again && again->description().size() == 3 && again->description()[2] == "  indented", "...and read back");
    check(doc->setDescription({"A maze.", "hard 8"}) && doc->checkRows().front() == "; hard 8" && doc->checkRows().size() == doc->rows().size() + 1 &&
              Gauntlet::parse("room", doc->checkRows()) && Gauntlet::parse("room", doc->checkRows())->hard() == 8,
          "a 'hard N' description line: round-trips in the file, and goes to the checker with the rows (hard 8)");
    check(doc->setDescription({"hardly"}) && doc->checkRows() == doc->rows(), "...any other line doesn't");
    doc->setDescription({"A test room.", "", "  indented"});
    check(exported.find("\"label\":\"Bumper at col 1, row 3\"") != std::string::npos, "the history exported in words");
    again->clearHistory();
    check(again->addHistory(false, "before", 1, 0, 0, text) && again->canUndo() && again->undo() && again->rows() == kRoom, "...and a step brought back undoes to it");
}

void judged() {
    GridJob job;
    for (const std::string& name : LevelCatalog::builtin().gauntletNames()) {
        GridJob::Request r;
        r.name = name;
        r.rows = LevelCatalog::builtin().gauntlet(name)->rowsTopFirst();
        r.revision = 3;
        r.level = 0; // (the solver's report alone: no game run)
        r.delayMs = 0;
        job.kick(r);
        std::string json;
        int revision = -1;
        bool got = false;
        for (int i = 0; i < 600 && !got; ++i) {
            got = job.poll(json, revision);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const std::string want = gauntletReportJson(*LevelCatalog::builtin().gauntlet(name), name, 0, false, &LevelCatalog::builtin());
        const std::string head = "{\"sameAs\":[],\"gameRun\":false,";
        check(got && revision == 3 && json.rfind(head, 0) == 0 && "{" + json.substr(head.size()) == want, ("the " + name + " judged in the background: gauntlet_check --json's report").c_str());
    }
    // with its level: the solver's report first, then the game's run (the bot playing it in the real game)
    GridJob::Request r;
    r.name = "tomb";
    r.rows = LevelCatalog::builtin().gauntlet("tomb")->rowsTopFirst();
    r.revision = 4;
    r.level = gauntletLevelOf("tomb", &LevelCatalog::builtin());
    r.delayMs = r.gameDelayMs = 0;
    job.kick(r);
    std::vector<std::string> got;
    for (int i = 0; i < 2400 && got.size() < 2; ++i) {
        std::string json;
        int revision = -1;
        if (job.poll(json, revision)) got.push_back(json);
        else std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(got.size() == 2 && got[0].find("\"gameRun\":false") != std::string::npos && got[1].find("\"gameRun\":true") != std::string::npos,
          "with its level: the solver's report, then the game's");
    check(got.size() == 2 && got[1].find("\"game\":{\"entered\":true,\"out\":true") != std::string::npos, "...the game's bot gets out of the tomb");
}

// a level laying a maze made wider here reads with that grid standing in for the built-in one (EditorDoc::open's grids)
void levelWithGrid() {
    const LevelCatalog& cat = LevelCatalog::builtin();
    const int level = gauntletLevelOf("tomb", &cat);
    const Rows wider = grid::duplicateCols(cat.gauntlet("tomb")->rowsTopFirst(), 50, 52);
    const std::string text = refit(levelText(*cat.level(level)), "tomb", grid::cols(wider));
    std::string why;
    check(!EditorDoc::open(level, text, &why), "a level refitted to a wider tomb: without that grid, it doesn't read");
    auto doc = EditorDoc::open(level, text, &why, {{"tomb", wider}});
    check(doc && doc->text() == text, "...with it, it reads (and writes back as it was)");
}

} // namespace

// the wall laser's cells: painted, named, written as the game reads them
void laserCells() {
    std::string text;
    for (const std::string& r : kRoom) text += r + "\n";
    auto doc = GridDoc::open("room", text);
    if (!doc) { check(false, "a room to paint in"); return; }
    GridTools tools(*doc);
    tools.setTool("brush");
    tools.setCell(')', '.');
    tools.pointer(GridTools::Phase::Begin, 3, 2);
    tools.pointer(GridTools::Phase::End, 3, 2);
    check(doc->rows()[2] == "...)......", "a wall laser painted in");
    check(Gauntlet::parse("room", doc->rows()).has_value() && Gauntlet::parse("room", doc->rows())->beamAt(4, 2), "...a maze the game reads, its beam along its row");
    check(grid::cellName('(') == "Wall laser, firing left" && grid::cellName(')') == "Wall laser, firing right" && grid::cellName('~') == "Lava" && grid::cellName('^') == "Spring",
          "its cells' names (the editors' hover and history)");
}

// a maze renamed: its name alone (not an edit: the rows, the history, the text as they were); never a bad name
void renamed() {
    std::string text;
    for (const std::string& r : kRoom) text += r + "\n";
    auto doc = GridDoc::open("room", text);
    if (!doc) { check(false, "a room to rename"); return; }
    const int rev = doc->revision();
    check(doc->rename("big_room") && doc->name() == "big_room" && doc->revision() > rev, "renamed (a scene redrawn)");
    check(doc->text() == text && !doc->canUndo(), "...the same grid, nothing to undo");
    check(!doc->rename("Big Room") && doc->name() == "big_room" && !doc->lastError().empty(), "never a bad name");
    check(!doc->rename("big_room"), "its name already: nothing to do");
}

// a maze chunk's block in chunk mode: no doors, as small as 3 x 3, the maze's cells and no other, its checker a test corridor
void chunkMode() {
    const std::string text = ".....\n.....\n##...\n##S##\n";
    std::string why;
    check(!GridDoc::open("well", text, &why), "a block with no doors is no maze");
    auto doc = GridDoc::open("well", text, &why, true);
    check(doc && doc->chunk() && doc->rows().size() == 4 && doc->text() == text, "...but a chunk (read and written back as it was)");
    if (!doc) return;
    check(!GridDoc::open("well", ".....\n....\n", &why, true) && !why.empty(), "a chunk's rows all as long as each other");
    check(!GridDoc::open("well", ".>...\n##S##\n", &why, true) && !why.empty(), "...no door in it");
    check(!GridDoc::open("well", "..z..\n##S##\n", &why, true) && !why.empty(), "...no cell the maze hasn't");
    check(!GridDoc::open("well", "; only a note\n", &why, true), "...and some rows");
    auto noted = GridDoc::open("well", "; a well\n" + text, &why, true);
    check(noted && noted->description() == std::vector<std::string>{"a well"} && noted->text() == "; a well\n" + text, "a note in front of the rows is its description");

    // edits: a door can't be painted in, a cell can, a box can
    GridTools tools(*doc);
    tools.setTool("brush");
    tools.setCell('>', '.');
    tools.pointer(GridTools::Phase::Begin, 0, 1);
    tools.pointer(GridTools::Phase::End, 0, 1);
    check(doc->rows()[1] == "....." && !doc->lastError().empty() && !doc->canUndo(), "a door can't be painted into a chunk, and why");
    tools.setCell('H', '.');
    tools.pointer(GridTools::Phase::Begin, 3, 2);
    tools.pointer(GridTools::Phase::End, 3, 2);
    check(doc->rows()[2] == "##.H." && doc->canUndo(), "a cell painted");
    tools.setTool("box");
    tools.setCell('#', '.');
    tools.pointer(GridTools::Phase::Begin, 0, 0);
    tools.pointer(GridTools::Phase::End, 4, 0);
    check(doc->rows()[0] == "#####" && doc->rows()[3] == "##S##", "a box filled");
    check(doc->undo() && doc->undo() && doc->rows() == Rows{".....", ".....", "##...", "##S##"}, "...undone");

    // taking out and copying in: any column (a chunk has no doors), nothing under 3
    check(grid::deleteCols(doc->rows(), 0, 0, true)[0] == "....", "a chunk's first column taken out (a maze's never)");
    check(grid::deleteCols(doc->rows(), 0, 0) == doc->rows(), "...");
    check(grid::deleteCols(Rows{"...", "###", "###"}, 0, 0, true) == Rows({"...", "###", "###"}), "...not under 3 columns");
    check(grid::deleteCols(Rows{"....", "####", "####"}, 3, 3, true)[0] == "...", "...3 columns left is fine");
    check(grid::deleteRows(doc->rows(), 0, 0, true).size() == 3, "a row taken out, 3 left (a maze keeps 4)");
    check(grid::deleteRows(doc->rows(), 0, 0).size() == 4, "...");
    check(grid::deleteRows(Rows{"...", "...", "###"}, 0, 0, true).size() == 3, "...never under 3 rows");
    check(grid::duplicateCols(Rows{"ab.", "##S"}, 0, 0, true)[1] == "###S", "the first column copied in after itself");
    check(grid::duplicateCols(Rows{"..#", "##S"}, 2, 2, true)[1] == "##SS", "...and the last");
    tools.setTool("cols");
    tools.setErase(true);
    tools.pointer(GridTools::Phase::Begin, 0, 0);
    tools.pointer(GridTools::Phase::End, 0, 0);
    check(doc->rows()[0] == "...." && doc->rows()[3] == "#S##", "the Columns tool takes out the first column of a chunk");
    tools.setErase(false);
    tools.pointer(GridTools::Phase::Begin, 3, 0);
    tools.pointer(GridTools::Phase::End, 3, 0);
    check(doc->rows()[3] == "#S###", "...and copies the last in");
    check(grid::templateChunk().size() == 5 && grid::templateChunk()[0] == "........." && grid::templateChunk()[4] == "#########" && grid::chunkProblem(grid::templateChunk()).empty(), "a new chunk's block: air over a floor");
    check(grid::templateChunk(1, 1).size() == 3 && grid::templateChunk(1, 1)[0].size() == 3, "...at 3 x 3 at the least");

    // history: restore, export and bring back
    check(doc->restore(text, "Back") && doc->rows().size() == 4 && !doc->restore("..\n>X\n", "Bad"), "restored (and a text with doors turned away)");
    auto again = GridDoc::open("well", doc->text(), nullptr, true);
    check(again && again->addHistory(false, "before", 1, 0, 0, ".....\n#####\n") && again->undo() && again->rows().size() == 2, "a step of its history brought back, read as a chunk");
    check(doc->rename("deep well") && doc->name() == "deep well" && !doc->rename(""), "a chunk renamed as the store names it (its file's id)");

    // its checker: the test corridor, the flipped one only with the mirror check on
    const Rows corridor = doc->checkRows();
    check(corridor == chunkCorridor([&] { Chunk c; c.maze = true; c.rows = doc->rows(); return c; }()), "its checker is the corridor round it");
    const auto g = Gauntlet::parse("corridor", corridor, &why);
    check(g.has_value(), "...which reads as a gauntlet");
    check(doc->flippedCheckRows().empty() && !doc->mirrorCheck(), "...and no flipped one with Mirror off");
    doc->setMirrorCheck(true);
    Chunk flipped;
    flipped.maze = true;
    flipped.rows = doc->rows();
    check(doc->mirrorCheck() && doc->flippedCheckRows() == chunkCorridor(mirrored(flipped)) && doc->flippedCheckRows() != corridor, "...with it on, the corridor round the mirrored chunk");
    auto maze = GridDoc::open("room", "##########\n#........#\n..........\n>........X\n###S######\n");
    maze->setMirrorCheck(true);
    check(!maze->chunk() && !maze->mirrorCheck() && maze->flippedCheckRows().empty() && maze->checkRows() == kRoom, "a maze's checker is as it was (no mirror check, no corridor)");
}

// a chunk's corridor (MazeGen.h): a door, a run-in, the chunk on the floor, a run-out, the exit; judged in the background
// with the flipped one, and no game run
void chunkChecked() {
    Chunk c;
    c.maze = true;
    c.rows = {".....", ".....", "##...", "##S##"};
    const Rows corridor = chunkCorridor(c);
    check(corridor.size() == 9 && corridor[0] == std::string(31, '#'), "the corridor: a roof over 7 rows of air at the least, a floor under");
    check(corridor[7][0] == '>' && corridor[7][30] == 'X', "...a door each side, on the floor");
    check(corridor[8].substr(13, 5) == "##S##" && corridor[7].substr(13, 5) == "##..." && corridor[6].substr(13, 5) == ".....", "...the chunk 12 cells in, on the floor (its bottom row the floor's stone)");
    Chunk roofed = c;
    roofed.rows = {"#.#", "...", "#S#"};
    const Rows r2 = chunkCorridor(roofed);
    check(r2[0][13] == '#' && r2[1][13] == '#' && r2[2][13] == '#' && r2[2][14] == '.' && r2[1][14] == '.', "stone in a chunk's top row reaches up to the roof");
    Chunk tall = c;
    tall.rows = Rows(12, "....");
    tall.rows.back() = "####";
    check(chunkCorridor(tall).size() == 14, "a taller chunk: a taller corridor");
    check(chunkCorridor(Chunk{}).empty(), "no rows, no corridor");
    const auto g = Gauntlet::parse("corridor", corridor);
    const Gauntlet::Solution s = g->solve();
    check(s.reachable && s.routes == 1 && s.softLocks == 0, "the well in its corridor: out, one way, no traps");

    GridJob job;
    GridJob::Request r;
    r.name = "well";
    r.rows = corridor;
    r.flipRows = chunkCorridor(mirrored(c));
    r.level = 20;
    r.noGame = true;
    r.revision = 5;
    r.delayMs = r.gameDelayMs = 0;
    job.kick(r);
    std::string json;
    int revision = -1;
    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) { got = job.poll(json, revision); if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    check(got && revision == 5 && json.find("\"flipped\":{\"sameAs\"") != std::string::npos && json.find("\"name\":\"well_flipped\"") != std::string::npos, "judged in the background, the flipped one with it");
    check(got && json.find("\"gameRun\":false") != std::string::npos && json.find("\"solution\":{\"reachable\":true") != std::string::npos, "...the solver's report");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::string more;
    check(!job.poll(more, revision), "...and no game run to follow (a chunk's corridor is not a level)");
}

int main() {
    chunkMode();
    chunkChecked();
    edits();
    laserCells();
    renamed();
    roundTrips();
    history();
    judged();
    levelWithGrid();
    return test::finish("GridDoc");
}
