#include "BotJob.h"

#include "EditorDoc.h"
#include "core/LevelCatalog.h"
#include "core/ReportJson.h"

#include <chrono>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

namespace cs::ed {

BotJob::BotJob() : worker_([this] { run(); }) {}

BotJob::~BotJob() {
    {
        const std::lock_guard<std::mutex> lock(m_);
        quit_ = true;
        stop_ = true;
    }
    cv_.notify_all();
    worker_.join();
}

void BotJob::kick(int number, const std::string& text, int revision, std::vector<Grid> grids, int delayMs, const std::string& flipText) {
    {
        const std::lock_guard<std::mutex> lock(m_);
        next_ = {number, text, revision, std::move(grids), delayMs, flipText};
        pending_ = true;
        ++generation_;
        stop_ = true; // (the run under way is for an older edit)
    }
    cv_.notify_all();
}

void BotJob::kick(const EditorDoc& doc, int delayMs) {
    std::vector<Grid> grids;
    for (const std::string& name : doc.catalog().standInGauntlets()) grids.push_back({name, doc.catalog().gauntlet(name)->rowsTopFirst()});
    kick(doc.number(), doc.text(), doc.revision(), std::move(grids), delayMs, doc.flippedText());
}

bool BotJob::poll(std::string& json, int& revision) {
    const std::lock_guard<std::mutex> lock(m_);
    if (!fresh_) return false;
    json = result_;
    revision = resultRevision_;
    fresh_ = false;
    return true;
}

void BotJob::cancel() {
    const std::lock_guard<std::mutex> lock(m_);
    pending_ = false;
    ++generation_;
    stop_ = true;
}

void BotJob::run() {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0); // (a background job: the editor's drawing and touches first)
#endif
    std::unique_lock<std::mutex> lock(m_);
    for (;;) {
        cv_.wait(lock, [&] { return quit_ || pending_; });
        if (quit_) return;
        const unsigned gen = generation_;
        const Request r = next_;
        // (a while after the last edit: a newer one starts the wait again)
        if (cv_.wait_for(lock, std::chrono::milliseconds(r.delayMs), [&] { return quit_ || generation_ != gen; })) {
            if (quit_) return;
            continue;
        }
        pending_ = false;
        stop_ = false;
        busy_ = true;
        lock.unlock();
        LevelCatalog cat;
        std::string why, json;
        for (const Grid& g : r.grids) cat.setGauntlet(g.name, g.rows, &why);
        if (!cat.setLevel(r.number, r.text, &why)) json = "{\"ok\":false,\"level\":" + std::to_string(r.number) + ",\"error\":\"" + jsonEsc(why) + "\"}";
        else {
            json = levelReportJson(*cat.level(r.number), true, &cat, &stop_);
            if (!r.flipText.empty() && !stop_ && cat.setLevel(r.number, r.flipText, &why) && json.size() > 1 && json.back() == '}') { // (a chunk flipped: the same play)
                const std::string flipped = levelReportJson(*cat.level(r.number), true, &cat, &stop_);
                json.pop_back();
                json += ",\"flipped\":" + flipped + "}";
            }
        }
        lock.lock();
        busy_ = false;
        if (!stop_ && generation_ == gen) {
            result_ = std::move(json);
            resultRevision_ = r.revision;
            fresh_ = true;
        }
    }
}

} // namespace cs::ed
