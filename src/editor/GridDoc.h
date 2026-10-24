// A gauntlet's maze open in the iPhone and iPad editor: its grid (rows top first, one character a cell, as its file and
// Gauntlet.h have them), its description (the file's ';' lines), and its undo history in words. Every edit must read as
// the game will read it (Gauntlet::parse), or it's turned away with the reason. The edits are the web editor's
// (tools/level_editor/static/js/grid.js), one for one: a cell, a box, a door moved, columns or rows copied in or taken
// out; a door is never painted over.
//
// A maze chunk's block of cells (Chunk.h) opens in the same document, in chunk mode: no doors (none to paint, to keep or to
// require), blocks as small as 3 x 3 (rows or columns can be taken out down to 3), the cells the maze has and no other, the
// description left to the chunk's header. Its checker is a plain corridor around it (chunkCorridor, MazeGen.h): checkRows()
// is that corridor, and flippedCheckRows() the corridor round the chunk mirrored, when the chunk may be (setMirrorCheck).
#pragma once

#include "core/Gauntlet.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cs::ed {

using Rows = std::vector<std::string>;

namespace grid {

bool isDoor(char ch);
int cols(const Rows& rows);
// each returns the rows edited (the same rows: nothing to do); i is a row index, counted from the top
Rows setCell(const Rows& rows, int c, int i, char ch);              // a door's character moves that door to row i
Rows fillRect(const Rows& rows, int c0, int i0, int c1, int i1, char ch);
Rows moveDoor(const Rows& rows, char door, int i);                  // '>' (the left edge) or 'X' (the right edge)
Rows duplicateCols(const Rows& rows, int c0, int c1, bool chunk = false);  // copied in after themselves (never the doors'; a chunk's: any)
Rows deleteCols(const Rows& rows, int c0, int c1, bool chunk = false);     // never the doors', never under 4 (a chunk's: any, never under 3)
Rows duplicateRows(const Rows& rows, int i0, int i1);              // copied in over themselves (doors as stone)
Rows deleteRows(const Rows& rows, int i0, int i1, bool chunk = false);     // never a door's, never under 4 (a chunk's: never under 3)
// a cell she can start in, clicked at (c, i): where she'd stand down that column (a row index), or nullopt
std::optional<int> standable(const Rows& rows, int c, int i);
// a new gauntlet's grid: a closed room, its doors a row up, one way through
Rows templateRoom(int width = 40, int height = 10);
// a new maze chunk's block: stone for the floor under rows of air
Rows templateChunk(int width = 9, int height = 5);
// why a maze chunk's block breaks the rules ("" if it keeps them): rows of one length, the maze's cells, no doors
std::string chunkProblem(const Rows& rows);
// the cells' names, as the editors call them ("Stone", "Gate →")
std::string cellName(char ch);

} // namespace grid

class GridDoc {
public:
    // a grid file's text (';' lines, then the rows): null, and why in *error, if it doesn't read as a gauntlet
    // (chunk: a maze chunk's block, in chunk mode; the text is rows of cells, with ';' lines for a description if it has any)
    static std::unique_ptr<GridDoc> open(const std::string& name, const std::string& text, std::string* error = nullptr, bool chunk = false);
    bool chunk() const { return chunk_; }
    // chunk mode: judge the chunk flipped too (the Mirror switch)
    void setMirrorCheck(bool on) { mirrorCheck_ = on; }
    bool mirrorCheck() const { return chunk_ && mirrorCheck_; }

    const std::string& name() const { return name_; }
    // its maze called by another name (Gauntlet.h's validGauntletName): not an edit, nothing in it changes
    bool rename(const std::string& name);
    const Rows& rows() const { return rows_; }
    // the rows as the checker reads them: with the description's "hard N" line (the density it was made at) first, if it has one
    // (a chunk: the test corridor round it)
    Rows checkRows() const;
    // chunk mode with the mirror check on: the test corridor round the chunk flipped; else none
    Rows flippedCheckRows() const;
    Rows plainCheckRows() const {
        Rows out;
        for (const std::string& line : description_) if (Gauntlet::hardOf("; " + line) >= 0) out.push_back("; " + line);
        out.insert(out.end(), rows_.begin(), rows_.end());
        return out;
    }
    const std::vector<std::string>& description() const { return description_; }
    std::string text() const;                          // as its file has it
    int revision() const { return revision_; }
    const std::string& lastError() const { return error_; }

    // an edit, in words: false if it changed nothing or doesn't read (lastError says why)
    bool edit(const std::string& label, const std::function<Rows(const Rows&)>& change, int c = -1, int i = -1);
    bool setDescription(const std::vector<std::string>& lines);
    void beginGesture();
    void endGesture();
    bool inGesture() const { return gesture_; }
    const Rows& gestureBase() const { return gestureBase_; }
    void restoreGestureBase();

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    // a step: its words, when (seconds since 1970), where (the cell it began at; -1: none)
    struct Step { std::string label; int revision; long long time; int c, i; };
    std::vector<Step> history() const;
    std::vector<Step> redoSteps() const;
    bool goTo(size_t k);
    bool restore(const std::string& text, const std::string& label);
    // kept on the device between launches, as EditorDoc's ({"current","undo":[{label,time,c,i,text}],"redo":[...]})
    std::string historyExport() const;
    void clearHistory();
    bool addHistory(bool redo, const std::string& label, long long time, int c, int i, const std::string& text);

    // the play-test's start in the maze: a cell (column, row index), not an edit
    std::optional<std::pair<int, int>> start;

private:
    GridDoc() = default;
    struct State { Rows rows; std::vector<std::string> description; bool operator==(const State& o) const { return rows == o.rows && description == o.description; } };
    static std::optional<State> read(const std::string& name, const std::string& text, std::string* error, bool chunk = false);
    bool apply(const std::string& label, State next, int c, int i);
    void changed() { ++revision_; }
    std::string name_;
    Rows rows_;
    std::vector<std::string> description_;
    int revision_ = 0;
    bool chunk_ = false, mirrorCheck_ = false;
    std::string error_;
    struct Snapshot { State state; std::string label; int revision; long long time = 0; int c = -1, i = -1; };
    std::vector<Snapshot> undo_, redo_;
    bool gesture_ = false, gestureEdited_ = false;
    Rows gestureBase_;
    State current() const { return {rows_, description_}; }
};

// the maze's tools, as a finger or a Pencil uses them on the grid (a cell: column, row index from the top):
//   brush   tap or drag: the cell (erase: the second cell); a door's character moves that door
//   box     drag a box: filled with the cell (erase: the second cell)
//   door    tap or drag on a row: the nearer door (left half: the entry, right half: the exit) moved to it
//   cols    drag across columns: copied in after themselves (erase: taken out)
//   rows    drag across rows: copied in over themselves (erase: taken out)
//   start   tap: the play-test's start (erase: none)
class GridTools {
public:
    explicit GridTools(GridDoc& doc) : doc_(doc) {}
    void setTool(const std::string& tool) { tool_ = tool; }
    const std::string& tool() const { return tool_; }
    void setCell(char primary, char secondary) { primary_ = primary; secondary_ = secondary; }
    void setErase(bool erase) { erase_ = erase; }
    enum class Phase { Begin = 0, Move = 1, End = 2, Cancel = 3 };
    bool pointer(Phase phase, int c, int i);           // true if the grid changed
    // the gesture's preview: its kind ("box", "cols", "rows") and its cells
    struct Preview { std::string kind; int c0 = 0, i0 = 0, c1 = -1, i1 = -1; bool erase = false; };
    const Preview& preview() const { return preview_; }

private:
    GridDoc& doc_;
    std::string tool_ = "brush";
    char primary_ = '.', secondary_ = '#';
    bool erase_ = false;
    int lastC_ = -1, lastI_ = -1, startC_ = -1, startI_ = -1;
    Preview preview_;
};

} // namespace cs::ed
