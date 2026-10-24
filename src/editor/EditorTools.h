// The editor's tools, as a finger or a Pencil uses them on the course (metres along, metres up), the same on the iPhone
// and the iPad: a gesture begins, moves and ends, and each is one step in the history. The hazards' tools are the
// registry's (Hazards.h: one for each, by its id), so a new hazard has its tool with nothing written here.
//   wall      drag across columns: each as high as where the finger is
//   kind      drag across columns: the kind picked
//   barrier   drag across columns: a barrier's underside where the finger is
//   block     drag a box: a floating block from its underside to its top
//   <hazard>  tap: one put in (snapped as its row says); drag one along
//   cols      drag across columns: copied in after themselves (Select, then delete: taken out)
//   select    drag across columns: the selection (copy, cut, paste, delete); tap a hazard: picked (its inspector)
//   gauntlet  drag the level's gauntlet along (or put the picked one in where you tap)
//   start     tap or drag: the play-test's start
// The eraser is a tool of its own, whichever tool is picked: what's under the finger goes (a hazard or a pickup, a
// block, a barrier, the walls down to the street), dragged along, one step in the history
#pragma once

#include "EditorDoc.h"

#include <optional>
#include <set>
#include <string>

namespace cs::ed {

enum class Phase { Begin = 0, Move = 1, End = 2, Cancel = 3 };

class EditorTools {
public:
    explicit EditorTools(EditorDoc& doc) : doc_(doc) {}

    void setTool(const std::string& tool) { tool_ = tool; }
    const std::string& tool() const { return tool_; }
    void setKind(WallKind k) { kind_ = k; }
    WallKind kind() const { return kind_; }
    void setErase(bool erase) { erase_ = erase; }   // ⌥ on the iPad's keyboard, the eraser switch, a Pencil's flip
    void setGauntlet(const std::string& name) { gauntlet_ = name; } // the gauntlet tool's (the picker's)

    // a gesture's step at (x, y), metres; true if the course changed
    bool pointer(Phase phase, double x, double y);

    // what the gesture under way shows (the scene draws it dashed): its kind and its columns or box, rows in eggs
    struct Preview { std::string kind; int c0 = 0, c1 = -1, r0 = 0, r1 = 0; bool erase = false; };
    const Preview& preview() const { return preview_; }

    // the selection (select's columns), and a picked hazard (its index in the course's items, or -1)
    int selC0 = -1, selC1 = -1;
    int picked = -1;
    bool copySelection();
    bool cutSelection();
    bool pasteAt(int col);
    bool deleteSelection();
    bool hasClip() const { return clip_.has_value(); }

    // a hazard's number set (the inspector: its slider)
    bool setPickedParam(size_t param, double value);
    bool removePicked();

private:
    bool wallsAlong(int from, int to, const std::function<bool(Course&, int)>& each, const std::string& label);
    bool eraseAt(int from, int to, double x, double y);
    EditorDoc& doc_;
    std::string tool_ = "wall", gauntlet_;
    WallKind kind_ = WallKind::Crate;
    bool erase_ = false;
    Preview preview_;
    // the gesture under way
    int lastCol_ = -1, startCol_ = -1, startRow_ = 0, grab_ = 0, dragging_ = -1;
    std::set<int> erased_; // (the eraser's columns this gesture: each erased once, a thing then what's under it not)
    std::optional<Clip> clip_;
};

std::string describeKind(WallKind k);

} // namespace cs::ed
