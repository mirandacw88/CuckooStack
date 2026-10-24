/* The level editor's core, for the iPhone and iPad editor's Swift (and anything else that speaks C): a level open for
 * editing, its tools, its scene, its history, the checker, the bot in the background, and the merge and refit sync
 * needs. Coordinates are metres: x along the course, y up. Strings returned are the caller's: free them with ed_free.
 * What's JSON is described where it's made (EditorScene.h, ReportJson.h, Hazards.h). Not thread-safe per document:
 * one thread drives a document (the bot runs on its own, with its own copy). */
#ifndef CS_EDITOR_H
#define CS_EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ed_doc ed_doc;
typedef struct ed_bot ed_bot;

void ed_free(char* s);

/* the game's own: the course's rules, the hazards' registry and the walls' kinds (level_check --registry-json); its
 * levels' numbers ([1,2,...]); a level's text (null: none); its grids ({"name":["row top", ...]}); and the course the
 * generator lays for a new level n */
char* ed_registry_json(void);
char* ed_builtin_numbers_json(void);
char* ed_builtin_level(int number);
char* ed_builtin_grids_json(void);
char* ed_bake_level(int number);

/* a level open: null, and why in *error (if error isn't null: free it), if its text doesn't read */
ed_doc* ed_open(int number, const char* text, char** error);
/* ...with grids standing in for theirs first (a level laying a maze edited here reads at its width): each grid its
 * name on a line, then its rows, grids apart by a blank line */
ed_doc* ed_open_grids(int number, const char* text, const char* grids, char** error);
void ed_close(ed_doc* doc);
char* ed_text(ed_doc* doc);
int ed_number(ed_doc* doc);
int ed_revision(ed_doc* doc);
char* ed_last_error(ed_doc* doc);
/* a grid standing in for its gauntlet (rows top first, one a line): 0 if it doesn't parse (ed_last_error says why) */
int ed_set_grid(ed_doc* doc, const char* name, const char* rows);

/* tools (EditorTools.h): "wall", "kind", "barrier", "block", "cols", "select", "gauntlet", "start", or a hazard's id */
void ed_set_tool(ed_doc* doc, const char* tool);
void ed_set_kind(ed_doc* doc, const char* kind);
void ed_set_erase(ed_doc* doc, int erase);
void ed_set_gauntlet_pick(ed_doc* doc, const char* name);
/* phase: 0 begin, 1 move, 2 end, 3 cancel; 1 if the course changed */
int ed_pointer(ed_doc* doc, int phase, double x, double y);
int ed_undo(ed_doc* doc);
int ed_redo(ed_doc* doc);
int ed_copy(ed_doc* doc);
int ed_cut(ed_doc* doc);
int ed_paste(ed_doc* doc, int col);
int ed_delete_selection(ed_doc* doc);
void ed_select(ed_doc* doc, int c0, int c1);     /* -1: none */
int ed_picked(ed_doc* doc);                        /* the picked hazard's index, or -1 */
void ed_pick(ed_doc* doc, int i);
int ed_set_picked_param(ed_doc* doc, int param, double value);
int ed_remove_picked(ed_doc* doc);
/* the Level generator: a new course from `seed` (the game's generator: its length, difficulty and hazards as the level
 * number has them; no gauntlet), the notes kept; one step in the history. `hazards`: the ids it may lay, comma
 * separated ("saw,laser,lava,disco"; null or "": all of them) */
int ed_regenerate(ed_doc* doc, unsigned seed, const char* hazards, int density); /* density: 0 (as designed) to 10 (super hard) */
/* ...with the designer's chunks (Chunk.h) it may place: `chunks` is all their file texts in one string, each under a line
 * "=== <id>" (chunk files as assets/chunks/<id>.txt holds them); the ones whose ranges cover this level are placed, course
 * chunks only, and the history step names them ("A new course (seed 5, density 6): chunks: hurdle x2, steps"). 0 if a
 * chunk doesn't read (ed_last_error says which and why); null or "": as ed_regenerate */
int ed_regenerate_chunks(ed_doc* doc, unsigned seed, const char* hazards, int density, const char* chunks);
/* the chunks the last roll placed, as JSON [{"id","x0","x1","mirrored"}] (metres along); [] once anything has changed since */
char* ed_chunks_used_json(ed_doc* doc);
/* the notice for chunks the last roll left out because no course passed with them ("Left out: x - no course passed with it at level 20"), "" for none */
char* ed_chunks_note(ed_doc* doc);
/* a course chunk's body open (the chunk editor; Chunk.h): `mode` 0 a level, 1 a chunk's body (it may be as short as 12 m; an edit
 * that leaves it breaking a chunk's rules is turned away unless it already does; the checker's notes say what breaks them), 2
 * the same with the chunk's Mirror switch on, the checker judging it flipped too (a note "Flipped: ..."; the bot plays it
 * flipped, its report has "flipped"). Not an edit */
void ed_set_chunk(ed_doc* doc, int mode);
/* the body renumbered (a chunk's first range moved: its zone and difficulty go with it); not an edit; 0 if it's that already */
int ed_set_number(ed_doc* doc, int number);
/* {"canMirror","why" (if not),"problem" (what breaks a chunk's rules; ""),"reach","runIn","runOut" (the pads, cells)} */
char* ed_chunk_json(ed_doc* doc);
/* its header: "notes" (lines), "length", "difficulty" ("offset scale"), "raids" (auto, none, a number) */
int ed_set_meta(ed_doc* doc, const char* key, const char* value);
/* the level as `text` reads, as one edit in its history (a revert, sync's update): 0 if it doesn't read */
int ed_restore(ed_doc* doc, const char* text, const char* label);
/* the history: to just after its k-th step (0: before them all); kept on the device between launches: exported
 * ({"current","undo":[{label,time,at,text}],"redo":[...]}), and brought back a step at a time (cleared first; redo
 * steps next first) */
int ed_history_goto(ed_doc* doc, int k);
char* ed_history_export(ed_doc* doc);
void ed_history_clear(ed_doc* doc);
int ed_history_add(ed_doc* doc, int redo, const char* label, long long time, double at, const char* text);
int ed_place_gauntlet(ed_doc* doc, const char* name, int col);
int ed_remove_gauntlet(ed_doc* doc, const char* name);
/* its gauntlet's maze renamed (EditorDoc::renameGauntlet: the history too, not a step); 0 and why (ed_last_error) if not */
int ed_rename_gauntlet(ed_doc* doc, const char* from, const char* to);
/* a gauntlet's name: lower case letters, digits and _, a letter first, 32 at most */
int ed_valid_gauntlet_name(const char* name);
/* a level file's text laying gauntlet `from` as `to` (renameGauntletIn) */
char* ed_rename_in_text(const char* text, const char* from, const char* to);
double ed_start(ed_doc* doc);                       /* the play-test's start, metres (-1: the level's start) */
void ed_set_start(ed_doc* doc, double x);           /* (snapped clear of saws; -1: none) */

char* ed_scene_json(ed_doc* doc);
char* ed_hit_json(ed_doc* doc, double x, double y);
char* ed_history_json(ed_doc* doc);
char* ed_meta_json(ed_doc* doc);
char* ed_report_json(ed_doc* doc);                  /* the checker's, without the game run */

/* the bot in the background (BotJob.h) */
ed_bot* ed_bot_new(void);
void ed_bot_free(ed_bot* bot);
void ed_bot_kick(ed_bot* bot, ed_doc* doc, int delay_ms);
void ed_bot_cancel(ed_bot* bot);
int ed_bot_busy(ed_bot* bot);
char* ed_bot_poll(ed_bot* bot, int* revision);      /* null: nothing new */

/* chunk files (Chunk.h), apart from any level: a file's header and body read leniently (the body is kept as text: a chunk
 * that breaks the rules can be opened and mended): {"ok" (the header reads),"error" (why the header, or else the body,
 * doesn't; "" if all does),"id","kind":"course"|"maze","notes":[...],"ranges":[[from,to],...],"weight","uses" (0: any),
 * "mirror","canMirror","whyNot","body" (the text after the ---),"first" (the nominal level: the first range's start)} */
char* ck_parse(const char* id, const char* text);
/* a chunk file's text from its parts: `notes` lines, `ranges` ("1-5 7"), the body as it is (a bad range is left out) */
char* ck_text(int maze, const char* notes, const char* ranges, int weight, int uses, int mirror, const char* body);
/* a new course chunk's file for level `first`: a flat run-in, a hay wall, a flat run-out */
char* ck_blank(const char* id, int first);
/* ...a new maze chunk's file: a 9 x 5 block (stone under four rows of air, a saw in it) */
char* ck_blank_maze(const char* id, int first);
/* the chunks of "=== <id>" texts, their ids as JSON in the list's order (bands of five levels, then start, end, id);
 * one whose header doesn't read goes last */
char* ck_sort(const char* chunks);
/* the game's own chunks (assets/chunks, baked into ChunkLayouts.inc) as {"id":"file text",...}: a first launch's chunks */
char* ck_builtin_json(void);
/* ...the same for the chunks of a bundle ("=== <id>" texts; ones that don't read are left out): what ck_builtin_json makes of the
 * game's own, for any set of them */
char* ck_files_json(const char* chunks);
/* whether `id` is a chunk's file name (lower case letters, digits, '-' and '_') */
int ck_valid_id(const char* id);

/* sync (LevelMerge.h): {"ok","text","conflicts":[...],"error"}; a level refitted to its gauntlet's width; whether a
 * text reads ({"ok","error"}) */
char* ed_merge3(int number, const char* base, const char* mine, const char* theirs);
/* a grid the app has (synced from the Mac) for merging and checking texts (ed_merge3, ed_check_text: the game's
 * catalog for the developer tools, LevelCatalog::dev()); main thread, not while a merge runs */
int ed_set_dev_grid(const char* name, const char* rows);
char* ed_refit(const char* text, const char* name, int width);
char* ed_check_text(int number, const char* text);

/* a gauntlet's maze open for editing (GridDoc.h): its file's text (';' description lines, then the rows top first);
 * null, and why in *error, if it doesn't read. Cells are (column, row index from the top). */
typedef struct gd_doc gd_doc;
typedef struct gd_job gd_job;
gd_doc* gd_open(const char* name, const char* text, char** error);
/* chunk != 0: a maze chunk's block of cells in chunk mode (GridDoc.h: no doors, as small as 3 x 3; its checker is a test corridor
 * round it); the scene has "chunk": true */
gd_doc* gd_open_mode(const char* name, const char* text, int chunk, char** error);
/* chunk mode: also judge the chunk flipped (the job's report then has "flipped": the corridor round the mirrored chunk) */
void gd_set_mirror_check(gd_doc* doc, int on);
void gd_close(gd_doc* doc);
char* gd_text(gd_doc* doc);
int gd_revision(gd_doc* doc);
char* gd_last_error(gd_doc* doc);
/* the maze called by another name (not an edit); 0 if the name isn't a gauntlet's (gd_last_error says why) */
int gd_rename(gd_doc* doc, const char* name);
/* tools: "brush", "box", "door", "cols", "rows", "start"; the cell painted and the second one (the eraser's) */
void gd_set_tool(gd_doc* doc, const char* tool);
void gd_set_cells(gd_doc* doc, int primary, int secondary);
void gd_set_erase(gd_doc* doc, int erase);
/* phase: 0 begin, 1 move, 2 end, 3 cancel; 1 if the grid changed */
int gd_pointer(gd_doc* doc, int phase, int col, int row);
int gd_undo(gd_doc* doc);
int gd_redo(gd_doc* doc);
int gd_set_description(gd_doc* doc, const char* lines);
int gd_restore(gd_doc* doc, const char* text, const char* label);
void gd_set_start(gd_doc* doc, int col, int row); /* (-1: none; snapped down to where she'd stand) */
/* {"name","cols","rows":[top first],"description":[...],"revision","canUndo","canRedo","entry":[c,i],"exit":[c,i],
 * "preview":{"kind","c0","i0","c1","i1","erase"}|null,"start":[c,i]|null,"error"} */
char* gd_scene_json(gd_doc* doc);
/* {"undo":[{"label","revision","time","at" (its column),"row"}...],"redo":[...],"canUndo","canRedo"} */
char* gd_history_json(gd_doc* doc);
int gd_history_goto(gd_doc* doc, int k);
char* gd_history_export(gd_doc* doc);
void gd_history_clear(gd_doc* doc);
int gd_history_add(gd_doc* doc, int redo, const char* label, long long time, int col, int row, const char* text);

/* the Gauntlet generator (MazeGen.h): a maze `width` wide with `floors` floors stacked (3, 5, 7) that passes the checklist,
 * for `level` (as `level_text` reads, or the built-in one), seeds from `seed` on, with the hazards `hazards` names
 * (comma separated: "saw,laser,lava"; null or "": all of them), as dense as `density` (0, as designed, to 10; from 6 its
 * file has a "hard N" line):
 * {"ok","text" (its file: description and rows),"seed","tries","widened" (not ready at the width asked: made wider),"why"} */
char* gd_generate(int level, const char* level_text, int width, int floors, unsigned seed, const char* hazards, int density);
/* ...with the maze chunks in `chunks` (Chunk.h's "=== id" bundle; those in range of `level` are laid): as gd_generate, and
 * "chunks":[{"id","floor","c0","c1","r0","r1" (the grid's cells, rows top first),"mirrored"}] for those it placed */
char* gd_generate_chunks(int level, const char* level_text, int width, int floors, unsigned seed, const char* hazards, int density, const char* chunks);

/* the maze's checker in the background (GridJob.h): the other grids it knows (for "sameAs", and the level's course),
 * then a kick after each edit with the level that plays it (0: none) as edited; poll: gauntlet_check --json's report
 * plus "sameAs" and "gameRun" (null: nothing new) */
gd_job* gd_job_new(void);
void gd_job_free(gd_job* job);
void gd_job_set_grid(gd_job* job, const char* name, const char* rows);
void gd_job_kick(gd_job* job, gd_doc* doc, int level, const char* level_text);
void gd_job_cancel(gd_job* job);
int gd_job_busy(gd_job* job);
char* gd_job_poll(gd_job* job, int* revision);

#ifdef __cplusplus
}
#endif

#endif
