// A chunk: a set piece the level designer builds by hand (assets/chunks/<id>.txt) and the generators place. A course
// chunk is a stretch of course the rhythm-beat generator (LevelBeats.h's 'Q' piece) stamps into the courses it rolls; a
// maze chunk is a block of cells for the maze generator (MazeGen.h): the maze's own cells, top row first, the bottom row the
// floor's stone (a saw, lava or a spring set into it, as in a maze), the rows above it the air she runs through; she runs
// right through it as drawn, and on a floor that runs left the tower draws it mirrored.
//
//   ; Sacred wells, the short version.       notes: the chunk's description (';' lines at the top)
//   chunk course                             course, or maze
//   ranges 1-5 2-3 31-40                     the levels it may appear in (one or more; they may overlap; "7" is 7-7)
//   weight 3                                 1 to 10: how much the generator wants it (a plain piece weighs 1)
//   uses 2                                   the most a course holds (or: uses any)
//   mirror yes                               the generator may flip it left to right
//   ---                                      the body: a level's text (a course chunk) or rows of cells (a maze chunk)
//
// A course chunk's body is an ordinary level (parseLevel), numbered by the start of its first range, with a flat run-in
// for its first wall and a flat run-out for its last (about 8 m each, plain walls: no barrier, block, bouncer or
// gauntlet). Heights are measured from the run-in's floor; the generator strips the pads when it places the chunk and
// shifts the heights to wherever she is. The body is a playable mini-level on its own (the chunk editor's checker and
// bot test it so).
//
// Several chunks in one text (the editors' Roll a new course, a backup): each under a line "=== <id>".
#pragma once

#include "LevelData.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cs {

struct ChunkRange {
    int from = 1, to = 1;
    bool operator==(const ChunkRange& o) const { return from == o.from && to == o.to; }
};

struct Chunk {
    std::string id;                      // its file's name: lower case, digits, '-' and '_'
    bool maze = false;                   // else a course chunk
    std::vector<std::string> notes;      // its description
    std::vector<ChunkRange> ranges;
    int weight = 3;                      // 1 to 10
    int uses = 0;                        // the most a course holds; 0: no limit
    bool mirror = false;                 // the generator may flip it
    LevelData body;                      // a course chunk's level
    std::vector<std::string> rows;       // a maze chunk's cells, top first

    bool inRange(int level) const;
    // may it be flipped: no springs or bounce boxes (springs launch rightwards, boxes depend on direction; a maze chunk: no
    // springs); else why not
    bool canMirror(std::string* why = nullptr) const;
    // will the generator flip it (the Mirror switch on and it can be)
    bool mirrors() const { return mirror && canMirror(); }

    // a course chunk's pads: the run-in's floor (its first wall's height), the run-out's, and their lengths in cells
    int entryHeight() const;
    int exitHeight() const;
    int runIn() const;
    int runOut() const;
    // how far the body's walls reach, metres (pads included)
    double reach() const;
};

// where a chunk was placed in a rolled course (Plan::chunks)
struct ChunkUse {
    std::string id;
    double x0 = 0, x1 = 0; // metres along
    bool mirrored = false;
};

// a chunk file's header read, its body left as text, unread (the editors open a chunk whose body breaks the rules, to fix
// it): `out` has everything but the body (body, rows); false, and why, if the header doesn't read
bool splitChunk(const std::string& id, const std::string& text, Chunk& out, std::string& body, std::string* error = nullptr);
// why a chunk's body breaks the rules parseChunk holds it to (a course chunk's: three walls at least, plain pads, nothing set
// into them; a maze chunk's: some rows), or "" if it keeps them; *at: where along (metres) it goes wrong
std::string chunkBodyProblem(const Chunk& c, double* at = nullptr);
// a chunk file's text: the chunk, or why not (`id`: its file's name)
std::optional<Chunk> parseChunk(const std::string& id, const std::string& text, std::string* error = nullptr);
// ...and back
std::string chunkText(const Chunk& c);
// ...its header alone (the notes, kind, ranges, weight, uses, mirror, then the --- line)
std::string chunkHeaderText(const Chunk& c);
// a new course chunk for the editors: a flat run-in, something between and a flat run-out (about 8 m each, a hay wall
// between), numbered by `first` (the level whose difficulty it is judged at), ranges `first`-`first`, weight 3, uses any,
// no mirror
Chunk blankCourseChunk(const std::string& id, int first);
// a maze cell seen in a mirror: a wall laser and a one-way gate face the other way ('(' and ')', 'R' and 'L'); the rest as is
char mirroredCell(char ch);
// a new maze chunk for the editors: a 9 x 5 block, the floor's stone under four rows of air with a saw in it, ranges
// `first`-`first`, weight 3, uses any, no mirror
Chunk blankMazeChunk(const std::string& id, int first);
// a course chunk flipped: the walls reversed (heights as they are, so the run-out's height is the new entry), the
// hazards at their mirror places (a laser fires the other way); a maze chunk's columns reversed, its lasers and gates turned (mirroredCell). Call it only if canMirror()
Chunk mirrored(const Chunk& c);
// the order of the chunk list: bands of five levels (1-5, 6-10, ...) by where each chunk's first range starts, then by
// that start and end, then by id
bool chunkBefore(const Chunk& a, const Chunk& b);
// several chunks in one text ("=== <id>" before each); false, and why, if one doesn't read (the rest are still in *out)
bool parseChunks(const std::string& text, std::vector<Chunk>& out, std::string* error = nullptr);
std::string chunksText(const std::vector<Chunk>& chunks);
// "sacred-wells ×2, hurdles": the chunks a course was rolled with, for the history
std::string chunkUsesLabel(const std::vector<ChunkUse>& used);
// a maze chunk's cells set by `set(column, row, cell)` (row 0 the floor's base, going up): its bottom row at row g0, its first
// column at c0; the stone in its top row reaches up to row `roof`, as in a maze (the generator's stamp, and the corridor's)
void stampMazeChunk(const Chunk& chunk, int c0, int g0, int roof, const std::function<void(int, int, char)>& set);
// a maze chunk set in a plain corridor to be judged (the chunk editor's checker; level_check --chunk): a door, a run-in of flat
// floor, the chunk on the floor (its bottom row the floor's stone, the air rows over it; stone in its top row reaches up to the
// roof as in a maze), a run-out and the exit, the roof high enough for it (7 rows of air at the least). Rows top first, as
// layMaze makes them; empty for a chunk with no rows
std::vector<std::string> chunkCorridor(const Chunk& chunk);
// the notice for chunks a generator gave up on, "" for none: "Left out: three-saws \xE2\x80\x94 no maze passed with it at level 20" (`what`
// "maze" or "course"), or, with `fewer` (laid in smaller numbers, not left out), "Fewer of: three-saws \xE2\x80\x94 no maze passed with
// all of them at level 20"
std::string chunksLeftOutNote(const std::vector<std::string>& ids, const std::string& what, int level, bool fewer = false);

} // namespace cs
