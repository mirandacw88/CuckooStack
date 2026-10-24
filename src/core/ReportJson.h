// A level's report as text or as one JSON object (the level editors' protocol, level_check --json): its course
// judged (LevelJudge.h) and, if asked, played by the game's bot (BotRun.h).
#pragma once

#include "Json.h"
#include "LevelData.h"

#include <atomic>
#include <string>

namespace cs {

// its gauntlets cat's (null: LevelCatalog::dev()); `game`: play it too (cat must have this level as d.number: a
// LevelCatalog::setLevel, or a debugOverrideLevel)
// (cancel: the game's run stops early once it's true: BotRun.h)
std::string levelReportJson(const LevelData& d, bool game, const LevelCatalog* cat = nullptr, const std::atomic<bool>* cancel = nullptr);
std::string levelReportText(const LevelData& d, const LevelCatalog* cat = nullptr);

// a chunk file (Chunk.h) judged as the chunk editor's checker judges it (level_check --chunk): {"ok":false,"id","error"} if it
// doesn't read; else {"ok":true,"id","kind":"course"|"maze","ranges":[[from,to]],"weight","uses","mirror","mirrors" (it will be
// flipped), "mirrorNote" (why it can't be, if Mirror is on and it can't),"report" (a course chunk's body as levelReportJson has it,
// numbered by its first range), "flipped" (...the body flipped, when it mirrors)}. `game`: the bot plays each, alone
std::string chunkReportJson(const std::string& id, const std::string& text, bool game);

// the course's rules the level editors lay by, and the hazards' registry (level_check --registry-json: Hazards.h): the cell, a gauntlet's lintel and porch,
// the hen's body and the meter (a door needs its lintel when a full stack doesn't fit under it), and the forecourt in
// cells, its lintel among them, as Level::placeGauntlet lays it in levels 1-100 (at its gauntlet's place, at its
// difficulty as authored) and the longest of them (an editor laying one forecourt for every level)
std::string courseRulesJson();

} // namespace cs
