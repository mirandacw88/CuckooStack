// What the editor's canvas draws, as one JSON object (the iPhone and iPad editor's Swift draws it; pans and zooms it
// itself), in metres along and up, cells of 0.6 m:
//   {"number","zone","cell","length","courseLength","rows" (the tallest thing, eggs),
//    "walls":[[c0,c1,h,kind,ceil,top],...]          runs of like columns (kind: the registry's id)
//    "gauntlets":[{"name","c0","c1","rows":[...top first],"entryRow","exitRow"}],
//    "items":[{"i","type","x","y","v":[...]}],     each hazard (y: where it's drawn, metres: a surface one on its surface)
//    "notes":[{"x","y","text","kind"}],             the checker's: "note", "short" (the egg budget), "door"
//    "walk":[[x,eggs],...], "lowest",                a careful player's meter along the course
//    "start", "selection":[c0,c1]|null, "picked", "preview":{...}|null}
// and what's under a finger: {"what":"item","i"} | {"what":"gauntlet","name"} | {"what":"wall","col","h","ceil","top"}
#pragma once

#include "EditorDoc.h"
#include "EditorTools.h"

#include <string>

namespace cs::ed {

std::string sceneJson(const EditorDoc& doc, const EditorTools* tools = nullptr);
std::string hitJson(const EditorDoc& doc, double x, double y);
// the history: {"undo":[{"label","revision","time","at"}...] oldest first, "redo":[...] next first, "canUndo", "canRedo"}
std::string historyJson(const EditorDoc& doc);
// its header: {"number","notes":[...],"length","diffOffset","diffScale","raids","gauntlet":name|null,"error"}
std::string metaJson(const EditorDoc& doc);

} // namespace cs::ed
