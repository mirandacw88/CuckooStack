// A chunk file judged (ReportJson.h's chunkReportJson, which level_check --chunk prints and the level editor's sync checks a
// pushed chunk with): one that reads gives its kind, settings and its body's report (and the flipped body's, when it mirrors);
// one that doesn't gives why; the bot plays it on request. A level's report (levelReportJson) is LevelCatalog_test's and
// Level_test's.
#include "TestUtil.h"
#include "core/Chunk.h"
#include "core/ReportJson.h"

#include <string>

using namespace cs;
using test::check;

namespace {

const char* kHeader = "; Test chunk\nchunk course\nranges 31-35 2-3 7\nweight 4\nuses 2\nmirror yes\n---\n";
const char* kBody = "length 26\ndifficulty 1085 0.35\nraids auto\ncourse\n13 0 crate\n8 3 hay\n10 1 crate\n13 1 crate\nsaws\n9.6\ndiscos\nlasers\n12.6 1 1\nlava\n14.4 3\n";

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

void reads() {
    const std::string out = chunkReportJson("test-chunk", std::string(kHeader) + kBody, false);
    check(has(out, "{\"ok\":true,\"id\":\"test-chunk\",\"kind\":\"course\",\"ranges\":[[31,35],[2,3],[7,7]],\"weight\":4,\"uses\":2,\"mirror\":true,\"mirrors\":true"),
          "a chunk that reads: its id, kind, ranges, weight, uses and whether it flips");
    check(has(out, "\"report\":{\"ok\":true,\"level\":31") && has(out, "\"notes\":["), "...its body judged as a level, numbered by its first range");
    check(has(out, "\"flipped\":{\"ok\":true,\"level\":31"), "...and flipped too, since Mirror is on");
    check(!has(out, "\"game\""), "...no bot run unless asked");
    std::string off = std::string(kHeader) + kBody;
    off.replace(off.find("mirror yes"), 10, "mirror no");
    const std::string plain = chunkReportJson("test-chunk", off, false);
    check(has(plain, "\"mirrors\":false") && has(plain, "\"report\"") && !has(plain, "\"flipped\"") && !has(plain, "mirrorNote"), "Mirror off: judged as it is, not flipped");
}

void cantFlip() {
    std::string spring = std::string(kHeader) + kBody;
    spring.replace(spring.find("lasers\n12.6 1 1\n"), 16, "");
    spring += "springs\n12.0 6\n";
    const std::string out = chunkReportJson("spring", spring, false);
    check(has(out, "\"ok\":true") && has(out, "\"mirror\":true,\"mirrors\":false") && has(out, "\"mirrorNote\":\"") && !has(out, "\"flipped\""),
          "Mirror on but a spring in it: judged as it is, the note says it can't be flipped");
}

void turnsDown() {
    const std::string bad = chunkReportJson("t", "chunk course\nranges 1\n---\nlength 30\ncourse\n10 0 crate\n", false);
    check(has(bad, "{\"ok\":false,\"id\":\"t\",\"error\":\"") && !has(bad, "\"error\":\"\""), "a chunk that doesn't read (no run-out, too few walls): not ok, and why");
    check(has(chunkReportJson("Bad Name", std::string(kHeader) + kBody, false), "\"ok\":false"), "a bad name: not ok");
    check(has(chunkReportJson("t", "nonsense", false), "\"ok\":false"), "a file that isn't a chunk: not ok");
    check(has(chunkReportJson("t", std::string(kHeader) + "length 26\ncourse\n13 0 crate\n8 3 hay\n10 1 crate\n13 1 crate\nsaws\n3.0\n", false), "\"ok\":false"),
          "a saw set into the run-in: not ok (the pads are stripped when it's placed)");
}

void maze() {
    const std::string well = "chunk maze\nranges 21-30\nweight 2\nuses 1\nmirror yes\n---\n.....\n.....\n##...\n##S##\n";
    const std::string out = chunkReportJson("well", well, false);
    check(has(out, "\"ok\":true") && has(out, "\"kind\":\"maze\"") && has(out, "\"mirrors\":true"), "a maze chunk that reads and plays: ok");
    check(has(out, "\"report\":{") && has(out, "\"solution\":{\"reachable\":true") && has(out, "\"routes\":1"), "...judged in its corridor: the gauntlet report, reachable, one route");
    check(has(out, "\"flipped\":{") && !has(out, "\"error\""), "...flipped too, since Mirror is on");
    std::string off = well;
    off.replace(off.find("mirror yes"), 10, "mirror no");
    const std::string plain = chunkReportJson("well", off, false);
    check(has(plain, "\"ok\":true") && has(plain, "\"report\":{") && !has(plain, "\"flipped\""), "Mirror off: judged as it is");
    const std::string pit = chunkReportJson("pit", "chunk maze\nranges 21-30\nweight 2\nuses 1\nmirror yes\n---\n##.##\n#...#\n", false);
    check(has(pit, "{\"ok\":false,\"id\":\"pit\"") && has(pit, "\"error\":\"unplayable in a corridor: ") && has(pit, "\"report\":{"), "a chunk with a pit she can't cross: turned down, the reason given, the report still there");
    const std::string wall = chunkReportJson("wall", "chunk maze\nranges 1\nweight 2\nuses 1\nmirror no\n---\n#####\n#####\n#####\n", false);
    check(has(wall, "\"ok\":false") && has(wall, "unplayable in a corridor: no way to the exit"), "a block of solid stone: no way to the exit");
    // playable as drawn, a one-way gate that faces her; flipped it faces the other way
    const std::string gate = "chunk maze\nranges 1\nweight 2\nuses 1\nmirror MIRROR\n---\n..R..\n..R..\n..R..\n..R..\n..R..\n..R..\n..R..\n..R..\n#####\n";
    std::string gateOff = gate, gateOn = gate;
    gateOff.replace(gateOff.find("MIRROR"), 6, "no");
    gateOn.replace(gateOn.find("MIRROR"), 6, "yes");
    const std::string gateAs = chunkReportJson("gate", gateOff, false), gateFlipped = chunkReportJson("gate", gateOn, false);
    check(has(gateAs, "{\"ok\":true") && has(gateAs, "\"reachable\":true"), "a wall of one-way gates that face her: playable as drawn");
    check(has(gateFlipped, "{\"ok\":false") && has(gateFlipped, "\"error\":\"unplayable in a corridor: flipped: no way to the exit\"") && has(gateFlipped, "\"flipped\":{"), "...with Mirror on it is turned down, because flipped the gates face away: the reason says flipped");
    const std::string spring = chunkReportJson("spring", "chunk maze\nranges 1\nweight 2\nuses 1\nmirror yes\n---\n.....\n.....\n##...\n##^##\n", false);
    check(has(spring, "\"mirrorNote\":\"") && !has(spring, "\"flipped\""), "a maze chunk with a spring: judged as drawn, the note says it can't be flipped");
}

void bot() {
    const std::string out = chunkReportJson("test-chunk", std::string(kHeader) + kBody, true);
    check(has(out, "\"report\":{\"ok\":true") && has(out, "\"game\":{\"cleared\":") && has(out, "\"flipped\":{\"ok\":true"), "game: the bot plays the body (and the flipped one) on its own");
    size_t plays = 0;
    for (size_t at = out.find("\"game\":{"); at != std::string::npos; at = out.find("\"game\":{", at + 1)) ++plays;
    check(plays == 2, "...two runs: the chunk and its flip");
}

} // namespace

int main() {
    reads();
    cantFlip();
    turnsDown();
    maze();
    bot();
    return test::finish("ReportJson");
}
