// The editor's bot in the background (BotJob.h): a level kicked plays a while later on its own thread and its report
// comes back once, for the revision it was kicked with; a newer kick stops the older run (only the newest comes back);
// a cancel brings nothing back.
#include "TestUtil.h"
#include "editor/BotJob.h"
#include "core/Chunk.h"
#include "editor/EditorDoc.h"

#include <chrono>
#include <thread>

using namespace cs;
using namespace cs::ed;
using test::check;

namespace {

bool wait(BotJob& job, std::string& json, int& revision, int seconds) {
    for (int i = 0; i < seconds * 20; ++i) {
        if (job.poll(json, revision)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

void runs() {
    BotJob job;
    auto doc = EditorDoc::open(1, levelText(*LevelCatalog::builtin().level(1)));
    job.kick(*doc, 10);
    std::string json;
    int revision = -1;
    check(wait(job, json, revision, 120) && revision == doc->revision(), "a level kicked: its report comes back, for its revision");
    check(json.find("\"game\":{\"cleared\":true") != std::string::npos, "...the bot played it in the real game: cleared");
    check(!job.poll(json, revision), "...once");
}

void newestOnly() {
    BotJob job;
    job.kick(1, levelText(*LevelCatalog::builtin().level(1)), 7, {}, 0);
    for (int i = 0; i < 2000 && !job.busy(); ++i) std::this_thread::sleep_for(std::chrono::microseconds(500)); // (its run under way)
    check(job.busy(), "a kick: its run starts");
    job.kick(2, levelText(*LevelCatalog::builtin().level(2)), 8, {}, 0);
    std::string json;
    int revision = -1;
    // (an older run that ended just before the newer kick may hand over its report first: the editor drops a report
    // for an older revision; what matters is the newest comes, and nothing after it)
    bool newest = false;
    for (int tries = 0; tries < 3 && !newest; ++tries) newest = wait(job, json, revision, 120) && revision == 8;
    check(newest && json.find("\"level\":2") != std::string::npos, "a newer kick: its report comes back");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(!job.poll(json, revision), "...the older run stopped, nothing of it");
    job.kick(3, levelText(*LevelCatalog::builtin().level(3)), 9, {}, 50);
    job.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check(!job.poll(json, revision) && !job.busy(), "cancelled: nothing comes back");
    job.kick(4, "length nope\n", 10, {}, 0);
    check(wait(job, json, revision, 10) && json.find("\"ok\":false") != std::string::npos, "a text that doesn't read: why, as its report");
}

// a chunk's body: the bot plays it on its own, and flipped too when the doc says so
void chunkFlipped() {
    auto count = [](const std::string& s, const std::string& what) {
        int n = 0;
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) ++n;
        return n;
    };
    const std::string body = levelText(blankCourseChunk("hurdles", 20).body);
    BotJob job;
    auto doc = EditorDoc::open(20, body);
    doc->setChunkMode(EditorDoc::ChunkMode::Chunk);
    job.kick(*doc, 10);
    std::string json;
    int revision = -1;
    check(wait(job, json, revision, 120) && count(json, "\"cleared\":true") == 1 && json.find("\"flipped\"") == std::string::npos, "a chunk's body: the bot clears it on its own, nothing flipped (Mirror off)");
    doc->setChunkMode(EditorDoc::ChunkMode::Flip);
    job.kick(*doc, 10);
    check(wait(job, json, revision, 120) && json.find("\"flipped\":{") != std::string::npos && count(json, "\"cleared\":true") == 2, "...Mirror on: the flipped chunk is played too, its report inside as \"flipped\", both cleared");
    const std::string flipped = json.substr(json.find("\"flipped\":{"));
    check(json.back() == '}' && flipped.find("\"game\"") != std::string::npos, "...the report still one JSON object");
}

} // namespace

int main() {
    chunkFlipped();
    runs();
    newestOnly();
    return test::finish("BotJob");
}
