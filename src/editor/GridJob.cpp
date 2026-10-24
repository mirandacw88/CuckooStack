#include "GridJob.h"

#include "core/GauntletReport.h"
#include "core/Json.h"
#include "core/LevelCatalog.h"

#include <chrono>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

namespace cs::ed {

namespace {

// the report with its "sameAs" (the catalog's other grids laid out the same, mirror images counting)
std::string judged(const GridJob::Request& r, bool game) {
    LevelCatalog cat;
    std::string why;
    for (const GridJob::Grid& g : r.grids) if (g.name != r.name) cat.setGauntlet(g.name, g.rows, &why);
    if (!cat.setGauntlet(r.name, r.rows, &why)) return "{\"ok\":false,\"name\":\"" + jsonEsc(r.name) + "\",\"error\":\"" + jsonEsc(why) + "\"}";
    if (r.level > 0 && !r.levelText.empty() && !cat.setLevel(r.level, r.levelText, &why)) game = false; // (its level doesn't read: no game run)
    const Gauntlet& g = *cat.gauntlet(r.name);
    std::string same;
    for (const std::string& other : cat.gauntletNames())
        if (other != r.name && cat.gauntlet(other)->fingerprint() == g.fingerprint()) same += std::string(same.empty() ? "" : ",") + "\"" + jsonEsc(other) + "\"";
    std::string o = gauntletReportJson(g, r.name, r.level, game && r.level > 0, &cat);
    std::string flipped;
    if (!r.flipRows.empty()) {
        GridJob::Request f = r;
        f.name = r.name + "_flipped";
        f.rows = r.flipRows;
        f.flipRows.clear();
        flipped = "\"flipped\":" + judged(f, false) + ",";
    }
    return "{\"sameAs\":[" + same + "]," + flipped + "\"gameRun\":" + (game && r.level > 0 ? "true" : "false") + "," + o.substr(1);
}

} // namespace

GridJob::GridJob() : worker_([this] { run(); }) {}

GridJob::~GridJob() {
    {
        const std::lock_guard<std::mutex> lock(m_);
        quit_ = true;
    }
    cv_.notify_all();
    worker_.join();
}

void GridJob::kick(Request r) {
    {
        const std::lock_guard<std::mutex> lock(m_);
        next_ = std::move(r);
        pending_ = true;
        ++generation_;
    }
    cv_.notify_all();
}

bool GridJob::poll(std::string& json, int& revision) {
    const std::lock_guard<std::mutex> lock(m_);
    if (!fresh_) return false;
    json = result_;
    revision = resultRevision_;
    fresh_ = false;
    return true;
}

void GridJob::cancel() {
    const std::lock_guard<std::mutex> lock(m_);
    pending_ = false;
    ++generation_;
}

void GridJob::run() {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0); // (a background job: the editor's drawing and touches first)
#endif
    std::unique_lock<std::mutex> lock(m_);
    for (;;) {
        cv_.wait(lock, [&] { return quit_ || pending_; });
        if (quit_) return;
        const unsigned gen = generation_;
        const Request r = next_;
        pending_ = false;
        // the solver's report, then the game's run: each after its wait, each dropped by a newer edit
        for (const bool game : {false, true}) {
            if (game && (r.level <= 0 || r.noGame)) break;
            if (cv_.wait_for(lock, std::chrono::milliseconds(game ? r.gameDelayMs : r.delayMs), [&] { return quit_ || generation_ != gen; })) break;
            busy_ = true;
            lock.unlock();
            std::string json = judged(r, game);
            lock.lock();
            busy_ = false;
            if (quit_ || generation_ != gen) break;
            result_ = std::move(json);
            resultRevision_ = r.revision;
            fresh_ = true;
        }
        if (quit_) return;
    }
}

} // namespace cs::ed
