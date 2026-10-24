// The levels a game plays (LevelCatalog.h): the built-in ones, parsed once and never changed; stand-ins over them,
// a game's own (a level, a grid, a level taken out); and two games on two threads, each with its own stand-ins, playing
// as they do alone (the level editor's play-test and its bot runs at once). Each the bot playing in the real game.
#include "TestUtil.h"
#include "core/BotRun.h"
#include "core/Campaign.h"
#include "core/Chunk.h"
#include "core/LevelCatalog.h"

#include <cmath>
#include <string>
#include <thread>

using namespace cs;
using test::check;

namespace {

bool same(const GameRun& a, const GameRun& b) {
    return a.cleared == b.cleared && std::abs(a.seconds - b.seconds) < 1e-9 && std::abs(a.x - b.x) < 1e-9 && a.perfects == b.perfects &&
           a.closeCalls == b.closeCalls && a.trace == b.trace;
}

void builtinAndStandIns() {
    const LevelCatalog& builtin = LevelCatalog::builtin();
    check(builtin.levelNumbers().size() >= 30 && builtin.level(1) && builtin.gauntlet("tomb"), "the built-in levels and grids");
    check(&LevelCatalog::builtin() == &builtin, "...parsed once");

    LevelCatalog mine;
    check(mine.level(3) == builtin.level(3) && mine.gauntlet("tomb") == builtin.gauntlet("tomb"), "no stand-ins: the built-in ones");
    std::string why;
    const std::string one = levelText(*builtin.level(1));
    check(mine.setLevel(3, one, &why), "a stand-in for level 3");
    check(mine.level(3) != builtin.level(3) && mine.level(3)->length == builtin.level(1)->length, "...read in its place");
    check(builtin.level(3)->length != builtin.level(1)->length, "...the built-in one untouched");
    const LevelData* at = mine.level(3);
    check(mine.setLevel(3, levelText(*builtin.level(2)), &why) && mine.level(3) == at, "a stand-in replaced: at the same address");
    check(!mine.setLevel(4, "length 10\n", &why) && !why.empty() && mine.level(4) == builtin.level(4), "one that doesn't parse: why, and nothing changed");

    std::vector<std::string> rows = builtin.gauntlet("tomb")->rowsTopFirst();
    check(mine.setGauntlet("tomb", rows, &why) && mine.gauntlet("tomb") != builtin.gauntlet("tomb"), "a stand-in grid");
    const Gauntlet* tomb = mine.gauntlet("tomb");
    check(mine.setGauntlet("tomb", rows, &why) && mine.gauntlet("tomb") == tomb, "...replaced: at the same address (a course laid with it sees it)");
    check(!mine.setGauntlet("tomb", {"#"}, &why) && mine.gauntlet("tomb") == tomb, "a grid that doesn't parse: nothing changed");
    check(mine.setGauntlet("crypt", rows, &why) && mine.gauntlet("crypt") && !builtin.gauntlet("crypt"), "a new grid, this catalog's alone");
    const std::vector<std::string> names = mine.gauntletNames();
    check(std::find(names.begin(), names.end(), "crypt") != names.end() && names.size() == builtin.gauntletNames().size() + 1, "...listed with the rest");

    mine.removeLevel(5);
    const std::vector<int> numbers = mine.levelNumbers();
    check(!mine.level(5) && std::find(numbers.begin(), numbers.end(), 5) == numbers.end(), "a level taken out: gone (the generator lays it)");
    check(Campaign::lengthOf(5, &mine) == 300.0 + 10.0 * 4 && Campaign::lengthOf(3, &mine) == builtin.level(2)->length, "Campaign reads the catalog it's given");
    check(mine.setLevel(5, one, &why) && mine.level(5), "...and back");

    LevelCatalog none(nullptr);
    check(none.levelNumbers().empty() && !none.gauntlet("tomb") && !none.level(1), "over nothing: empty");
}

Chunk chunkAt(const char* id, const char* ranges) {
    return *parseChunk(id, std::string("chunk course\nranges ") + ranges + "\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\n");
}

void chunks() {
    // the built-in chunks (assets/chunks, ChunkLayouts.inc): whatever the designer has made, each read, in the list's order
    const std::vector<Chunk>& built = LevelCatalog::builtin().chunks();
    bool ordered = true;
    for (size_t i = 1; i < built.size(); ++i) ordered = ordered && !chunkBefore(built[i], built[i - 1]);
    check(ordered, "the built-in chunks come in the list's order (bands of five levels)");
    check(&LevelCatalog::dev().chunks() == &built && &LevelCatalog().chunks() == &built, "...the developer tools' catalog and one over the built-in one hold the same");
    check(LevelCatalog(nullptr).chunks().empty(), "a catalog over nothing: no chunks");
    // a catalog's own chunks stand in for its base's, sorted into order; the base's are untouched
    LevelCatalog mine;
    mine.setChunks({chunkAt("late", "16-20"), chunkAt("early", "1-5"), chunkAt("mid", "6-10")});
    check(mine.chunks().size() == 3 && mine.chunks()[0].id == "early" && mine.chunks()[1].id == "mid" && mine.chunks()[2].id == "late", "setChunks: its own chunks, in the list's order");
    check(&LevelCatalog::builtin().chunks() == &built, "...the built-in ones untouched");
    LevelCatalog over(&mine);
    check(&over.chunks() == &mine.chunks(), "a catalog over it sees its chunks");
    over.setChunks({});
    check(over.chunks().empty() && mine.chunks().size() == 3, "...and can have none of its own (an empty list is its own)");
    LevelCatalog bare(nullptr);
    bare.setChunks({chunkAt("x", "31-40")});
    check(bare.chunks().size() == 1 && bare.chunks()[0].inRange(35) && !bare.chunks()[0].inRange(41), "chunks keep their ranges");
    // chunk files read as ChunkLayouts.inc's are: in the list's order, one that doesn't read left out
    const std::string body = "\n---\nlength 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\n";
    const std::vector<Chunk> read = LevelCatalog::chunksFrom({{"late", "chunk course\nranges 31-35" + body}, {"broken", "chunk course\nranges nonsense" + body}, {"early", "chunk course\nranges 1-5" + body},
                                                              {"maze-one", "chunk maze\nranges 31-35\n---\n.....\n##S##\n"}});
    check(read.size() == 3 && read[0].id == "early" && read[1].id == "late" && read[2].id == "maze-one", "chunksFrom: the files that read, in the list's order (the one that doesn't is left out)");
    check(read[2].maze && read[2].rows.size() == 2 && read[1].inRange(33) && !read[1].inRange(36), "...each as the file says: kind, rows, ranges");
    check(LevelCatalog::chunksFrom({}).empty(), "no files: no chunks");
}

void twoGamesTwoThreads() {
    // level 3 as level 1's course on one, as built in on the other; level 11 (the tomb, a built-in grid both share)
    LevelCatalog a, b;
    std::string why;
    check(a.setLevel(3, levelText(*LevelCatalog::builtin().level(1)), &why), "a's stand-in");
    GameRun a3, b3, a11, b11;
    std::thread ta([&] { a3 = playLevel(3, &a); a11 = playLevel(11, &a); });
    std::thread tb([&] { b11 = playLevel(11, &b); b3 = playLevel(3, &b); });
    ta.join();
    tb.join();
    const GameRun alone3a = playLevel(3, &a), alone3b = playLevel(3, &b), alone11 = playLevel(11, &b);
    check(same(a3, alone3a) && same(b3, alone3b), "level 3 on two threads at once: each as it plays alone");
    check(!same(a3, b3), "...each its own catalog's course");
    check(same(a11, alone11) && same(b11, alone11), "level 11 on both at once: the shared built-in grid, as alone");
    check(a3.cleared && b3.cleared && a11.cleared && b11.cleared, "the bot clears them all");
}

} // namespace

int main() {
    builtinAndStandIns();
    chunks();
    twoGamesTwoThreads();
    return test::finish("LevelCatalog");
}
