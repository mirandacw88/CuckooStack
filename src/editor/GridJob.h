// The maze's checker, in the background: a moment after the last edit (150 ms) it judges the grid as edited
// (gauntlet_check --json's report, GauntletReport.h: the solver, the egg meter, its near misses, where it lands on its
// level's course, its design's signature), and a while after that (1.2 s) plays it in the real game, headless, the bot
// playing, and reports again with the game's run. On a thread of its own with its own catalog (this grid, the other
// grids as the editor has them, the level that plays it as edited). A newer edit drops what's under way; poll() hands
// over the newest result once. Each report also says which other grids it's the same as ("sameAs": mirror images count).
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cs::ed {

class GridJob {
public:
    GridJob();
    ~GridJob();
    GridJob(const GridJob&) = delete;
    GridJob& operator=(const GridJob&) = delete;

    struct Grid { std::string name; std::vector<std::string> rows; };
    struct Request {
        std::string name;
        std::vector<std::string> rows;
        int revision = -1;
        int level = 0;                 // the level that plays it (0: none: no course, no game run)
        std::string levelText;         // that level as edited ("": the catalog's)
        std::vector<std::string> flipRows; // (a maze chunk's) the same flipped, judged too: the report's "flipped"
        bool noGame = false;           // judged by the solver alone, the level only for its meter (a maze chunk's corridor)
        std::vector<Grid> grids;       // the other grids, as the editor has them
        int delayMs = 150, gameDelayMs = 1200;
    };
    void kick(Request r);
    bool poll(std::string& json, int& revision);
    bool busy() const { return busy_; }
    void cancel();

private:
    void run();
    std::mutex m_;
    std::condition_variable cv_;
    Request next_;
    bool pending_ = false, quit_ = false;
    unsigned generation_ = 0;
    std::atomic<bool> busy_{false};
    std::string result_;
    int resultRevision_ = -1;
    bool fresh_ = false;
    std::thread worker_;
};

} // namespace cs::ed
