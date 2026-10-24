// The editor's bot, in the background: a short while after the last edit (300 ms) it lays the level as edited and
// plays it in the real game, headless, the bot playing (level_check --json --game's report, ReportJson.h), on a thread
// of its own with its own catalog (the level's text, the grids as the editor has them: LevelCatalog.h). A newer edit
// stops the run under way (its result would be stale) and starts again; poll() hands over the newest result once.
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cs::ed {

class EditorDoc;

class BotJob {
public:
    BotJob();
    ~BotJob();
    BotJob(const BotJob&) = delete;
    BotJob& operator=(const BotJob&) = delete;

    struct Grid { std::string name; std::vector<std::string> rows; };
    // this level as it is now (its text, revision, the grids its catalog has over the built-in ones); `flipText`: a chunk's
    // body flipped, played too (the result has "flipped": its report, as the other, inside it)
    void kick(int number, const std::string& text, int revision, std::vector<Grid> grids = {}, int delayMs = 300, const std::string& flipText = "");
    void kick(const EditorDoc& doc, int delayMs = 300);
    // the newest finished result not handed over yet: its report (JSON) and the revision it's for
    bool poll(std::string& json, int& revision);
    bool busy() const { return busy_; }
    void cancel();                                    // the run under way stopped, nothing new

private:
    void run();
    struct Request { int number = 0; std::string text; int revision = -1; std::vector<Grid> grids; int delayMs = 0; std::string flipText; };
    std::mutex m_;
    std::condition_variable cv_;
    Request next_;
    bool pending_ = false, quit_ = false;
    unsigned generation_ = 0;
    std::atomic<bool> stop_{false}, busy_{false};
    std::string result_;
    int resultRevision_ = -1;
    bool fresh_ = false;
    std::thread worker_;
};

} // namespace cs::ed
