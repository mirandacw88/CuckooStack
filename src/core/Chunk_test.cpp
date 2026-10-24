// Chunks (Chunk.h): the file reads and writes back, its ranges and settings, the list's order, a course chunk flipped
// (heights, hazards, a laser's way), what can't be flipped, several chunks in one text. How the generator places them is
// LevelBeats_test's.
#include "TestUtil.h"
#include "core/Chunk.h"
#include "core/Hazards.h"

#include <cmath>
#include <string>

using namespace cs;
using test::check;

namespace {

// a run-in, a climb of 3, a crest and a drop to a run-out one row up; a saw, lava and a laser set between
const char* kHeader = "; Test chunk\n; two notes\nchunk course\nranges 31-35 2-3 7\nweight 4\nuses 2\nmirror yes\n---\n";
const char* kBody = "length 26\ndifficulty 1085 0.35\nraids auto\ncourse\n13 0 crate\n8 3 hay\n10 1 crate\n13 1 crate\nsaws\n9.6\ndiscos\nlasers\n12.6 1 1\nlava\n14.4 3\n";

std::string wellText() { return std::string(kHeader) + kBody; }

void reads() {
    std::string why;
    const auto c = parseChunk("test-chunk", wellText(), &why);
    check(c.has_value(), "a course chunk reads");
    if (!c) { std::printf("  %s\n", why.c_str()); return; }
    check(c->id == "test-chunk" && !c->maze && c->weight == 4 && c->uses == 2 && c->mirror && c->notes.size() == 2, "...its id, kind, weight, uses, mirror and notes");
    check(c->ranges.size() == 3 && c->ranges[0] == ChunkRange{31, 35} && c->ranges[1] == ChunkRange{2, 3} && c->ranges[2] == ChunkRange{7, 7}, "...its ranges, overlapping and single ones too");
    check(c->body.course.size() == 4 && c->body.number == 31 && c->body.items.size() == 3, "...its body a level, numbered by its first range");
    check(c->entryHeight() == 0 && c->exitHeight() == 1 && c->runIn() == 13 && c->runOut() == 13, "...its pads: the run-in's floor, the run-out's height, their cells");
    check(std::abs(c->reach() - 44 * U) < 1e-9, "...how far it reaches");
    const std::string back = chunkText(*c);
    check(back == wellText(), "written back it's the file it came from");
    const auto again = parseChunk("test-chunk", back);
    check(again && chunkText(*again) == back, "...and reads the same again");
    check(c->inRange(31) && c->inRange(35) && c->inRange(2) && c->inRange(3) && c->inRange(7) && !c->inRange(36) && !c->inRange(4) && !c->inRange(30), "inRange: any of its ranges");
    // uses any, mirror no, a maze chunk
    std::string t = wellText();
    t.replace(t.find("uses 2"), 6, "uses any");
    t.replace(t.find("mirror yes"), 10, "mirror no");
    const auto any = parseChunk("x", t);
    check(any && any->uses == 0 && !any->mirror && chunkText(*any).find("uses any") != std::string::npos && chunkText(*any).find("mirror no") != std::string::npos, "uses any is 0, no limit; mirror no");
    const auto maze = parseChunk("well", "chunk maze\nranges 21-30\nweight 2\nuses 1\nmirror yes\n---\n##.##\n#...#\n");
    check(maze && maze->maze && maze->rows.size() == 2 && chunkText(*maze).find("##.##\n#...#\n") != std::string::npos && maze->inRange(25), "a maze chunk keeps its rows");
}

void turnsDown() {
    auto bad = [&](const std::string& text, const char* what) {
        std::string why;
        check(!parseChunk("t", text, &why).has_value() && !why.empty(), what);
    };
    const std::string ok = wellText();
    bad(std::string(kHeader).replace(std::string(kHeader).find("ranges 31-35 2-3 7\n"), 19, "") + kBody, "no ranges: turned down");
    { std::string t = ok; t.replace(t.find("31-35"), 5, "35-31"); bad(t, "a backwards range: turned down"); }
    { std::string t = ok; t.replace(t.find("weight 4"), 8, "weight 11"); bad(t, "a weight of 11: turned down"); }
    { std::string t = ok; t.replace(t.find("uses 2"), 6, "uses 0"); bad(t, "uses 0: turned down (use 'any')"); }
    { std::string t = ok; t.replace(t.find("mirror yes"), 10, "mirror maybe"); bad(t, "mirror maybe: turned down"); }
    { std::string t = ok; t.replace(t.find("chunk course\n"), 13, "chunk course\nfoo 1\n"); bad(t, "an unknown header word: turned down"); }
    { std::string t = ok; t.replace(t.find("---\n"), 4, ""); bad(t, "no --- line: turned down"); }
    { std::string t = ok; t.replace(t.find("13 0 crate\n"), 11, "13 0 crate 4\n"); bad(t, "a run-in with a barrier over it: turned down"); }
    { std::string t = ok; t.replace(t.find("saws\n9.6"), 8, "saws\n3.0"); bad(t, "a saw in the run-in: turned down (the pads are stripped)"); }
    check(!parseChunk("Bad Name", ok).has_value(), "a name with capitals and spaces: turned down");
}

void flips() {
    const auto c = parseChunk("test-chunk", wellText());
    if (!c) { check(false, "the chunk reads"); return; }
    check(c->canMirror() && c->mirrors(), "a chunk of walls, a saw, lava and a laser can be flipped");
    const Chunk m = mirrored(*c);
    const double len = c->reach();
    bool heights = m.body.course.size() == 4;
    for (size_t i = 0; heights && i < 4; ++i) heights = m.body.course[i].h == c->body.course[3 - i].h && m.body.course[i].cells == c->body.course[3 - i].cells;
    check(heights, "flipped: the walls reversed");
    check(m.entryHeight() == c->exitHeight() && m.exitHeight() == c->entryHeight(), "...the new entry is the old exit (heights re-based on it)");
    check(m.body.items.size() == 3, "...the same hazards");
    const Placed* saw = nullptr; const Placed* lava = nullptr; const Placed* laser = nullptr;
    for (const Placed& p : m.body.items) { if (p.type == &kSawHazard) saw = &p; if (p.type == &kLavaHazard) lava = &p; if (p.type == &kLaserHazard) laser = &p; }
    check(saw && std::abs(saw->x() - (len - 9.6)) < 1e-6, "...a saw at len - x");
    check(lava && std::abs(lava->x() - (len - (14.4 + 3 * U))) < 1e-6 && lava->at(1) == 3, "...lava from len - (x + its cells)");
    check(laser && std::abs(laser->x() - (len - 12.6)) < 1e-6 && laser->at(1) == 1 && laser->at(2) == 0, "...a laser at len - x firing the other way");
    check(m.ranges == c->ranges && m.weight == c->weight && m.uses == c->uses && m.mirror && m.id == c->id, "...its settings as they were");
    std::string why;
    const auto back = parseChunk("test-chunk", chunkText(m), &why);
    check(back.has_value(), "...and it reads as a chunk (every hazard still in place for the checker)");
    if (!back) std::printf("  %s\n", why.c_str());
    const Chunk twice = mirrored(m);
    check(chunkText(twice) == chunkText(*c), "flipped twice: the chunk it was");
    // what can't be flipped
    std::string spring = wellText();
    spring.replace(spring.find("lasers\n12.6 1 1\n"), 16, "");
    spring += "springs\n12.0 6\n";
    const auto sp = parseChunk("s", spring);
    std::string why2;
    check(sp && !sp->canMirror(&why2) && !why2.empty() && !sp->mirrors(), "a spring: it can't be flipped, and says why (the Mirror flag has no effect)");
    const auto bp = parseChunk("b", "chunk course\nranges 1-5\nweight 1\nuses any\nmirror yes\n---\nlength 20\ncourse\n13 0 crate\n8 0 crate\n2 3 bumper\n8 3 crate\n13 3 crate\n");
    std::string why3;
    check(bp && !bp->canMirror(&why3) && !why3.empty(), "a bumper: it can't be flipped either");
}

void order() {
    auto mk = [](const char* id, int f, int t) { Chunk c; c.id = id; c.ranges = {{f, t}}; return c; };
    check(chunkBefore(mk("a", 1, 5), mk("b", 6, 10)) && !chunkBefore(mk("b", 6, 10), mk("a", 1, 5)), "the list: band 1-5 before 6-10");
    check(chunkBefore(mk("a", 2, 9), mk("b", 4, 5)), "...inside a band by where the first range starts");
    check(chunkBefore(mk("a", 3, 4), mk("b", 3, 8)) && chunkBefore(mk("a", 3, 8), mk("b", 3, 8)), "...then by its end, then its id");
    check(chunkBefore(mk("z", 5, 5), mk("a", 6, 6)), "...the bands are of five: 5 is in the first");
}

void several() {
    const auto a = parseChunk("alpha", wellText());
    auto b = parseChunk("beta", wellText());
    if (!a || !b) { check(false, "chunks read"); return; }
    b->weight = 9;
    const std::string both = chunksText({*a, *b});
    std::vector<Chunk> out;
    std::string why;
    check(parseChunks(both, out, &why) && out.size() == 2 && out[0].id == "alpha" && out[1].id == "beta" && out[1].weight == 9, "several chunks in one text, each under '=== id'");
    out.clear();
    check(!parseChunks(both + "=== broken\nchunk course\n", out, &why) && out.size() == 2 && !why.empty(), "...one that doesn't read: reported, the others kept");
    check(chunkUsesLabel({{"a", 0, 1, false}, {"b", 1, 2, true}, {"a", 2, 3, false}}) == "a \xC3\x97" "2, b" && chunkUsesLabel({}).empty(), "the history label: 'a \xC3\x97" "2, b'");
}

} // namespace

// what the editors lean on: the header read apart from the body, the body's problem and where, the header alone, a new chunk
void forEditors() {
    // splitChunk: a body that breaks the rules is kept as text, the header read
    std::string t = wellText();
    t.replace(t.find("saws\n9.6"), 8, "saws\n3.0");
    Chunk c;
    std::string body, why;
    check(!parseChunk("well", t).has_value(), "(a saw in the run-in: parseChunk turns it down)");
    check(splitChunk("well", t, c, body, &why) && c.weight == 4 && c.uses == 2 && c.mirror && c.ranges.size() == 3 && c.notes.size() == 2 && c.id == "well" && body.find("saws\n3.0") != std::string::npos,
          "splitChunk: the same file's header read and its body kept as text, unread");
    check(!splitChunk("well", "chunk course\nweight 3\n---\nx\n", c, body, &why) && why.find("ranges") != std::string::npos, "...a header that doesn't read: why");
    check(!splitChunk("Well", wellText(), c, body, &why), "...a bad name");
    check(!splitChunk("well", "chunk course\nranges 1\nlength 4\n", c, body, &why), "...no --- line");

    // chunkBodyProblem: ok, a hazard in a pad (where), a barrier, too few walls, a maze with no rows
    const auto ok = parseChunk("well", wellText());
    double at = -1;
    check(ok && chunkBodyProblem(*ok, &at).empty() && at == 0, "chunkBodyProblem: a chunk that reads has none");
    Chunk broken = *ok;
    broken.body.items[0].v[0] = 3.0; // (the saw)
    const std::string problem = chunkBodyProblem(broken, &at);
    check(problem.find("run-in") != std::string::npos && std::abs(at - 3.0) < 1e-9, "...a saw in the run-in: why, and where (3 m)");
    broken = *ok;
    broken.body.course.front().ceil = 4;
    check(chunkBodyProblem(broken).find("flat run-in") != std::string::npos, "...a barrier over the run-in");
    broken = *ok;
    broken.body.course.resize(2);
    check(chunkBodyProblem(broken).find("run-in, something between") != std::string::npos, "...fewer than three walls");
    Chunk maze;
    maze.maze = true;
    check(!chunkBodyProblem(maze).empty(), "...a maze chunk with no rows");
    maze.rows = {"##"};
    check(chunkBodyProblem(maze).empty(), "...and with some");

    // chunkHeaderText: everything above the body, the --- line last; chunkText is it and the body
    const std::string head = chunkHeaderText(*ok);
    check(head == std::string(kHeader) && chunkText(*ok) == head + levelText(ok->body), "chunkHeaderText: the notes, kind, ranges, weight, uses and mirror lines, then ---; chunkText is it and the body");

    // blankCourseChunk: a new chunk the editors start from
    const Chunk blank = blankCourseChunk("hurdles", 20);
    std::string err;
    const auto again = parseChunk("hurdles", chunkText(blank), &err);
    check(again.has_value(), "blankCourseChunk: reads as a file");
    if (!again) { std::printf("  %s\n", err.c_str()); return; }
    check(again->ranges.size() == 1 && again->ranges[0] == ChunkRange{20, 20} && again->weight == 3 && again->uses == 0 && !again->mirror && again->notes.empty(), "...level 20 only, weight 3, uses any, no mirror");
    check(again->body.course.size() == 3 && again->runIn() == 13 && again->runOut() == 13 && again->body.items.empty() && again->entryHeight() == 0 && again->exitHeight() == 0, "...a run-in, a wall, a run-out, flat, nothing set in it");
    check(again->body.length <= again->reach() + 1e-6 && again->body.length > again->reach() - 0.01, "...its finish line where its walls end");
    check(again->body.diffOffset == blankCourseChunk("x", 20).body.diffOffset && blankCourseChunk("x", 60).body.diffOffset > blank.body.diffOffset, "...its difficulty that of the level (a later level is harder)");
}

// a maze chunk (stage 4): its rows mirror, a spring keeps it from flipping, and its block must read as the maze's cells
void mazeChunks() {
    check(mirroredCell('(') == ')' && mirroredCell(')') == '(' && mirroredCell('R') == 'L' && mirroredCell('L') == 'R' && mirroredCell('#') == '#' && mirroredCell('S') == 'S' && mirroredCell('^') == '^', "mirroredCell: lasers and gates face the other way, the rest as is");
    const auto c = parseChunk("well", "chunk maze\nranges 21-30\nweight 2\nuses 1\nmirror yes\n---\n.(R..\n#..)#\n##S~#\n");
    check(c.has_value() && c->maze && c->canMirror() && c->mirrors(), "a maze chunk with Mirror on and no spring can be flipped");
    if (!c) return;
    const Chunk m = mirrored(*c);
    check(m.rows == std::vector<std::string>{"..L).", "#(..#", "#~S##"}, "mirrored: columns reversed, lasers and gates turned");
    check(mirrored(m).rows == c->rows && m.id == c->id && m.ranges == c->ranges && m.weight == c->weight && m.uses == c->uses && m.mirror == c->mirror && m.maze, "...flipped twice is the chunk, its settings kept");
    Chunk spring = *c;
    spring.rows[2] = "##^##";
    std::string why;
    check(!spring.canMirror(&why) && !spring.mirrors() && why.find("spring") != std::string::npos, "a spring in it: it can't be flipped, and why");
    spring.mirror = false;
    check(!spring.mirrors(), "...Mirror off: it isn't");
    Chunk off = *c;
    off.mirror = false;
    check(off.canMirror() && !off.mirrors(), "Mirror off: it can be, but isn't");
    std::string err;
    check(!parseChunk("a", "chunk maze\nranges 1\n---\n##.\n#..#\n", &err) && err.find("as long") != std::string::npos, "a maze chunk whose rows differ in length is turned down");
    check(!parseChunk("a", "chunk maze\nranges 1\n---\n.>.\n###\n", &err) && err.find("no doors") != std::string::npos, "...a door in it");
    check(!parseChunk("a", "chunk maze\nranges 1\n---\n.?.\n###\n", &err), "...a cell the maze hasn't");
    check(parseChunk("a", "chunk maze\nranges 1\n---\n.R.\n#L#\n").has_value(), "...gates are the maze's");
    check(chunkText(*c) == chunkText(*parseChunk("well", chunkText(*c))), "a maze chunk's text writes and reads back the same");
    const Chunk blank = blankMazeChunk("fresh", 12);
    const auto back = parseChunk("fresh", chunkText(blank));
    check(back && back->maze && back->rows.size() == 5 && back->rows.back() == "####S####" && back->ranges.size() == 1 && back->ranges[0] == ChunkRange{12, 12} && back->weight == 3 && back->uses == 0 && !back->mirror && back->canMirror(),
          "blankMazeChunk: a 9 x 5 block with a saw in its floor, level 12 only, that reads as a file");
}

// the notice for chunks a generator gave up on
void leftOut() {
    check(chunksLeftOutNote({}, "maze", 20).empty() && chunksLeftOutNote({}, "course", 20, true).empty(), "chunksLeftOutNote: nothing given up, nothing said");
    check(chunksLeftOutNote({"three-saws"}, "maze", 20) == "Left out: three-saws \xE2\x80\x94 no maze passed with it at level 20", "one chunk left out: 'it'");
    check(chunksLeftOutNote({"a", "b"}, "course", 7) == "Left out: a, b \xE2\x80\x94 no course passed with them at level 7", "several: 'them'");
    check(chunksLeftOutNote({"a", "b"}, "maze", 31, true) == "Fewer of: a, b \xE2\x80\x94 no maze passed with all of them at level 31", "laid in smaller numbers: 'Fewer of'");
}

// a maze chunk in its plain corridor, and the stamp
void corridor() {
    Chunk well;
    well.maze = true;
    well.rows = {"..#..", ".....", "##...", "##S##"};
    const std::vector<std::string> rows = chunkCorridor(well);
    check(rows.size() == 9 && rows[0].size() == 31, "chunkCorridor: 7 rows of air, the street and the roof, the chunk's width and 26 cells of run-in and run-out");
    check(rows.back() == std::string(13, '#') + "##S##" + std::string(13, '#'), "...the chunk's bottom row on the street, 13 cells in");
    check(rows[7][0] == '>' && rows[7][30] == 'X', "...a door at one end on the first air row and the exit at the other");
    bool stoneUp = true;
    for (int i = 0; i <= 4; ++i) stoneUp = stoneUp && rows[size_t(i)][15] == '#'; // (rows 8 to 4: over the chunk's top row)
    check(stoneUp && rows[0][14] == '#' && rows[1][14] == '.' && rows[1][16] == '.', "...the stone in the chunk's top row reaches up to the roof, the air beside it open");
    check(rows[5].substr(13, 5) == "..#.." && rows[6].substr(13, 5) == "....." && rows[7].substr(13, 5) == "##...", "...the chunk's rows stand as drawn over the street");
    Chunk tall = well;
    tall.rows.assign(10, ".....");
    tall.rows.back() = "#####";
    check(chunkCorridor(tall).size() == 12, "a chunk taller than 7 rows: the roof goes up with it");
    check(chunkCorridor(Chunk{}).empty(), "a chunk with no rows: no corridor");
    // the stamp: cells set row 0 at the floor going up, the top row's stone to the roof; a hay cell counts as stone
    std::vector<std::string> seen(6, "......");
    Chunk hay;
    hay.maze = true;
    hay.rows = {"H.", "##"};
    stampMazeChunk(hay, 2, 1, 4, [&](int c, int r, char ch) { seen[size_t(r)][size_t(c)] = ch; });
    check(seen[1] == "..##.." && seen[2] == "..H..." && seen[3] == "..#..." && seen[4] == "..#..." && seen[5] == "......" && seen[0] == "......", "stampMazeChunk: the chunk from its column and floor row, stone up from a top-row H to the roof row");
}

int main() {
    corridor();
    leftOut();
    mazeChunks();
    forEditors();
    reads();
    turnsDown();
    flips();
    order();
    several();
    return test::finish("Chunk");
}
