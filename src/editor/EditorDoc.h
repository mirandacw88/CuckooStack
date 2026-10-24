// A level open in the iPhone and iPad editor: its course (EditCourse.h), its own gauntlets (a catalog over the built-in
// ones: the grids synced from the Mac), its undo history in words, and what the checker makes of it (the course laid
// and judged: LevelJudge.h), worked out when it's asked for after an edit. Every edit must write back and read again
// as the game will read it (levelText, parseLevel), or it's turned away with the reason.
#pragma once

#include "EditCourse.h"
#include "core/LevelCatalog.h"
#include "core/Chunk.h"
#include "core/LevelBake.h"
#include "core/LevelJudge.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cs::ed {

class EditorDoc {
public:
    // null, and why in *error, if the text doesn't read (with these grids standing in for theirs: a level that lays a
    // maze edited on the device reads at its width)
    struct Grid { std::string name; std::vector<std::string> rows; };
    static std::unique_ptr<EditorDoc> open(int number, const std::string& text, std::string* error = nullptr, const std::vector<Grid>& grids = {});

    int number() const { return course_.number; }
    const Course& course() const { return course_; }
    std::string text() const;
    int revision() const { return revision_; }        // up one with every change (an edit, an undo, a grid)
    const std::string& lastError() const { return error_; }
    void setError(const std::string& e) { error_ = e; }
    LevelCatalog& catalog() { return *cat_; }
    const LevelCatalog& catalog() const { return *cat_; }
    // a grid standing in for its gauntlet (synced): false, and why, if it doesn't parse
    bool setGrid(const std::string& name, const std::vector<std::string>& rowsTopFirst);

    // an edit, in words (the history's): false if it changed nothing or was turned away (lastError says why)
    bool edit(const std::string& label, const std::function<bool(Course&)>& change);
    // a gesture (a drag): its edits one step in the history, under its first one's words
    void beginGesture();
    void endGesture();
    bool inGesture() const { return gesture_; }
    // the course as it was at the gesture's start (a preview redrawn from it, every move)
    const Course& gestureBase() const { return gestureBase_; }
    void restoreGestureBase();

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    // a step of the history: its words, when (seconds since 1970), where (metres along the course; -1: its settings),
    // and the level's text on its far side (before it, for one undo takes back; after it, for one redo puts back)
    struct Step { std::string label; int revision; long long time; double at; };
    std::vector<Step> history() const;                // oldest first, what undo takes back last
    std::vector<Step> redoSteps() const;              // what redo puts back next, first
    std::vector<std::string> redoLabels() const;
    // to just after the k-th step (0: before them all), undoing or redoing to it
    bool goTo(size_t k);
    // the level as `text` reads, as one edit (a revert: back to the synced or the game's own version; sync's update)
    bool restore(const std::string& text, const std::string& label);
    // the history kept on the device between launches: as JSON ({"current","undo":[{label,time,at,text}],"redo":[...]})
    // and back, a step at a time (cleared first; false for a text that doesn't read)
    std::string historyExport() const;
    void clearHistory();
    bool addHistory(bool redo, const std::string& label, long long time, double at, const std::string& text);

    // the Level generator: a new course for this level, as the game's generator lays one from `seed` (its length,
    // difficulty and hazards as the level number has them, only those `options` allows; no gauntlet: the Gauntlet
    // generator puts one in), its notes kept; one step in the history
    // (options.chunks: the designer's chunks it may place, Chunk.h; the history step names them: "A new course (seed 5):
    // chunks: hurdle Ã2, steps")
    bool regenerate(uint32_t seed, const GenOptions& options = {});
    // the chunks the last roll placed and where (empty once anything has changed since: they're the roll's, not the
    // course's)
    const std::vector<ChunkUse>& chunksUsed() const { return rollRevision_ == revision_ ? rolled_ : none_; }
    // the ids of chunks the last roll left out because no course passed with them (chunksLeftOutNote says so), empty once
    // anything has changed since; the roll never comes back plain or less dense for a chunk
    const std::vector<std::string>& chunksDropped() const { return rollRevision_ == revision_ ? rolledDropped_ : noIds_; }
    std::string chunksNote() const { return chunksLeftOutNote(chunksDropped(), "course", rolledLevel_); }
    // a header field: notes (lines), length, difficulty "offset scale", raids (auto, none, or a number)
    bool setMeta(const std::string& key, const std::string& value);
    // a level's gauntlet, placed or moved (its door at column `at`), or taken out; its grid's width refitted
    bool placeGauntlet(const std::string& name, int at);
    bool removeGauntlet(const std::string& name);
    // its gauntlet's maze renamed (its grid is the same): the course and every step of the history lay it by its new
    // name. Not a step itself (an undo never brings back a name that's gone). False, and why, if `to` isn't a
    // gauntlet's name. (No other grid has it: the caller's to see, it knows the grids; this catalog keeps old names)
    bool renameGauntlet(const std::string& from, const std::string& to);
    std::optional<GridInfo> gridInfo(const std::string& name) const;
    int forecourtAt(double x) const;                  // Level::forecourtAt, at this level's difficulty
    std::optional<double> start;                       // the play-test's start (metres along; not an edit)

    // a course chunk's body open (Chunk.h): shorter courses are fine (kMinChunkLength), an edit that leaves it breaking a
    // chunk's rules (a hazard in a pad, a barrier over a pad...) is turned away with the reason, unless it already did (the
    // way to mend it stays open), and the checker judges it flipped too (Flip, its Mirror switch on and it can be). Not an
    // edit: no step
    enum class ChunkMode { None, Chunk, Flip };
    void setChunkMode(ChunkMode m);
    ChunkMode chunkMode() const { return chunk_; }
    double minLength() const { return chunk_ == ChunkMode::None ? kMinLength : kMinChunkLength; }
    // the body as a chunk (numbered as the doc is, ranges that one level)
    Chunk asChunk() const;
    // why the body breaks a chunk's rules ("" none) and where along; "" outside chunk mode
    std::string chunkProblem(double* at = nullptr) const;
    // the course renumbered (a chunk's first range moved: its zone, difficulty and bot go with it), the history too; not
    // an edit. False if it's that number already
    bool setNumber(int n);
    // flipped (chunk mode Flip, a chunk that can be): the body as the generator would place it flipped, judged, and its
    // text (for the bot); null and "" otherwise
    const Report* flippedReport() const;
    std::string flippedText() const;

    // the course laid and judged (cached till the next change)
    const Level& level() const;
    const Report& report() const;
    std::string reportJson() const;                   // level_check --json's, without the game run

private:
    EditorDoc() = default;
    void changed();
    Course course_;
    std::unique_ptr<LevelCatalog> cat_;
    int revision_ = 0;
    std::string error_;
    std::vector<ChunkUse> rolled_, none_;
    std::vector<std::string> rolledDropped_, noIds_;
    int rolledLevel_ = 0;
    int rollRevision_ = -1;
    struct Snapshot { Course course; std::string label; int revision; long long time = 0; double at = -1; };
    std::vector<Snapshot> undo_, redo_;
    bool gesture_ = false, gestureEdited_ = false;
    Course gestureBase_;
    ChunkMode chunk_ = ChunkMode::None;
    mutable std::unique_ptr<Level> level_;
    mutable std::unique_ptr<Report> report_;
    mutable std::unique_ptr<Report> flipReport_;
    mutable bool flipJudged_ = false;
};

} // namespace cs::ed
