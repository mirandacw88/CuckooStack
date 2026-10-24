// A course as authored, judged as the tests judge the campaign's levels: what the generator promises a careful
// player (Level.h, Level_test), looked for in a level file. Shared by the level checker (tests/level_check.cpp), the
// level editors and their bot runs.
#pragma once

#include "Gauntlet.h"
#include "Level.h"
#include "LevelData.h"

#include <array>
#include <string>
#include <vector>

namespace cs {

const char* zoneName(int level);

struct Note { double x = 0, y = 0; std::string text; };

struct Report {
    std::vector<Note> notes;
    std::vector<Note> short_;          // the egg budget: where it runs short
    std::vector<Note> doors;           // a gauntlet's door off its forecourt's height, its porch off its exit's (the tests fail)
    std::vector<std::array<double, 2>> walk; // the careful player's meter along the course: (x, eggs) at each climb and blade
    double lowest = 8;                 // the least eggs a careful player holds at a climb or a blade
    int walls = 0, barriers = 0, blocks = 0, tallest = 0;
};
// the course as the judge sees it, for the hazards' own checks (Hazards.h): the height she runs at over each segment
// (a floating block's top where she takes it over), and whether she takes each over
struct JudgeView {
    const Level& l;
    std::vector<int> eh;
    std::vector<bool> over;
    size_t ix(const Segment* s) const { return size_t(s - l.segs.data()); }
};

// judge a laid course (Level::load, then generateUntil its length): its walls, each hazard's own checks, the egg
// budget (its climbs, and what each hazard on the run costs), its gauntlets' doors
Report judge(const Level& l);

} // namespace cs
