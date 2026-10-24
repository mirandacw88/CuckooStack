// A level played in the real game, headless: the game's bot playing it from its start (as GameDev_test's
// refillKeepsUp does), its run sampled for the level checker and the level editors (cleared, the time, its trace and
// its near misses).
#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace cs {

class LevelCatalog;

struct GameRun {
    bool cleared = false;
    double seconds = 0, x = 0, y = 0;
    int perfects = 0, closeCalls = 0;
    int death = -1;                           // how she died (Game::Death), -1: she didn't
    std::vector<std::array<float, 3>> trace;  // (x, the floor her stack's on, eggs) every few frames
    std::vector<std::array<float, 3>> events; // (x, height, kind: 0 a PERFECT, 1 a close call or shave)
};

// the campaign's level n as cat has it (null: LevelCatalog::dev(), where a debugOverrideLevel stands in for its file),
// up to 3 minutes; stopped early (not cleared) once *cancel is true (a level editor's bot run, an edit later)
GameRun playLevel(int level, const LevelCatalog* cat = nullptr, const std::atomic<bool>* cancel = nullptr);

} // namespace cs
