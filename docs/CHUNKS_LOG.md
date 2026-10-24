# Chunks: verification log

Plan: `~/.claude/plans/we-now-need-to-whimsical-shannon.md`, "Chunks: your own set pieces, placed by the generators".

Each stage is done by a Sonnet 5.5 sub-agent. After it reports, I check the stage myself:
- read the diff;
- run all the tests (C++, web, Python);
- confirm that levels 1–30 and `assets/gauntlets` are unchanged (checksum `cd879dc5…`);
- build the iPad editor.

What I find goes below, so we can iterate on it. Nothing is installed on the iPad until you ask.

**Your test requirement (2026-10-24):** every new piece of code, and every existing file a stage touches, gets tests.
Each stage's brief requires this. A final pass at the end checks coverage: every changed function has a test that
would fail without it.

| Stage | Status |
|---|---|
| 1. Core, course chunks | done, verified 2026-10-24 |
| 2. iPad, course chunks | done, verified 2026-10-24 (not yet seen on screen) |
| 3. Sync and the game | done, verified 2026-10-24 |
| 4. Maze chunks | done, verified 2026-10-24 |
| 4b. Follow-ups and the final test pass | done, verified 2026-10-24 |

---

## Stage 1: core, course chunks (verified 2026-10-24)

**What it added:**
- **`src/core/Chunk.h/.cpp`:**
  - the chunk format, with several chunks in one text under `=== <id>` lines;
  - ranges, where `7` is read as `7-7`;
  - `uses any`;
  - mirroring;
  - `canMirror()`, which refuses springs and bounce boxes;
  - the list order.
- **`LevelBeats`:** the `Q` piece. It goes in the same weighted order as the other pieces, never in the teach group and
  never on a floating floor. Each chunk's uses are counted. The random stream isn't touched when no chunk is in range.
- **The editor:** `ed_regenerate_chunks` and `ed_chunks_used_json` (where each chunk went). The history label names the
  chunks, e.g. "chunks: hurdle ×2, steps".

**My checks:**
- All C++ tests pass: 71 of 71 (new: `Chunk_test`).
- `assets/` is unchanged (checksum `cd879dc5…`).
- **The iPad build failed to link at first.** The new `Chunk.cpp` wasn't in the Xcode project yet. Fixed by running
  `platforms/ios/generate_xcode.sh B4HLN9759H`, and it now builds. From now on, every brief says to rerun that script
  whenever a C++ file is added.
- No existing test was weakened. I checked for leftover `|| true`.
- Behaviour change: only the history label of a roll, and only when chunks were placed.

**Coverage:**

| File | Tests |
|---|---|
| `Chunk.*` | `Chunk_test`: round trip, ranges, bad files turned down, mirror maths, `canMirror` |
| `LevelBake` / `LevelBeats` | `LevelBeats_test`: a 31–35 chunk at 33, never at 36; uses 1/2/any; flipped placement; byte-identical with no chunks; placed chunks reported; every course with chunks passes the checker and is cleared by the bot |
| `EditorDoc` | `rollWithChunks`, `cApi` |

**Numbers.** A 3-wall test chunk, levels 31–40, 10 seeds:
- 2.9 to 5.5 placements per level, more at higher weight and density;
- 98–100% of levels get at least one;
- about 47% are placed flipped;
- no fallbacks and no density notched down;
- compose is faster with chunks: 0.08–0.17 s against 0.21–0.26 s.

**For iteration (things you'll feel when authoring chunks):**
1. **A mirrored chunk must also be fair flipped.** Flipping turns climbs into drops, which need longer landings. A
   chunk that is only fair one way is never placed flipped. Stage 2's chunk checker tests both ways, and says so.
2. **A chunk carries its own room.** The checker measures the room after a hazard, and a drop's landing, within the
   wall they're on. So leave room inside the chunk before its run-out.
3. **A chunk that's unfair at a level's speed makes the whole candidate course fail.** The generator then picks
   another candidate. Chunks that are only fair at slow speeds will rarely appear at high levels.
4. **No hazards in the pads.** The first and last walls are the run-in and run-out.

**Speed:** `BEATS_QUICK=1` (every 5th level in the slow sweeps) and `BEATS_CHUNKS=1` (only the chunk checks, about
11 s) for iterating.

## Stage 2: iPad, course chunks (verified 2026-10-24)

**What it added:**
- **The start screen.** Animated **Edit Levels** and **Edit Chunks** cards, and a Home button in both modes. `--open N`
  still opens a level directly, and `--chunks [id]` opens Chunks.
- **The chunk list.** Bands of five levels. Each row shows the kind icon, the range chips, the weight, the uses, a flip
  mark and the bot's verdict. **New course chunk** asks for a name. You can copy a chunk or delete it with a confirm.
  **New maze chunk** is shown disabled ("coming soon") until stage 4.
- **The chunk editor.**
  - It's the level canvas and tools on the chunk's body. The pads are drawn hatched.
  - An edit that puts a hazard or barrier into a pad is turned away, with the reason.
  - With Mirror on, the checker judges the **flipped** chunk too ("Flipped: …" notes) and the bot plays it.
  - The Gauntlet tool and the generator cards are hidden.
  - The finish line can be as short as 12 m.
- **The Chunk settings card.** The name (renaming moves the file, its history and its verdict), ranges, the weight
  slider, max uses with **No limit**, Mirror (greyed out with the reason when the chunk can't flip), and a description.
- **Rolls.** **Roll a new course** passes every chunk that reads. A notice names the chunks placed, and they're outlined
  in magenta on the canvas. A chunk that doesn't read is left out and named in the notice.
- **Backups.** **Back up chunks…** saves one `.cuckoochunks` master file. **Restore chunks…** shows what's new, changed
  and unchanged, restores as one undoable step, and keeps the chunks it replaced in a "before restore" copy. The
  automatic backups include chunks.
- **The wiki.** A Chunks section, a live figure and a changelog line.

**My checks:**
- All C++ tests pass: 72 of 72 (new: `ios_editor_chunks`, a Swift test linked against the real core).
- Web: 42/42. Python: OK.
- The iPad build succeeds; I regenerated the Xcode project.
- `assets/` is unchanged.

I also read the roll path and the start screen:
- With no chunks, **Roll a new course** calls exactly the old `regenerate`.
- `--open N` goes straight to Levels.

**Behaviour that changed** (you asked for these):
- The app now opens on the start screen.
- Rolls include chunks in range.
- There's a Home button in the toolbar.

**Choices you may want to reverse:**
1. **Chunk settings changes save at once.** They aren't undo steps; course edits still are.
2. **New maze chunk** is shown disabled until stage 4.
3. **A chunk's bot verdict** is kept in the sync state under `chunk:<id>`.

**Not seen yet:** none of these screens has been run on a simulator, since you said wait for the install. Their
logic is unit-tested, but the views are build-checked only. The first time you say install, I'll go through each
screen and log what I see.

**Coverage:**

| Files | Tests |
|---|---|
| `Chunk.*` | `Chunk_test` (`forEditors`) |
| `EditCourse`, `EditorDoc`, `EditorTools`, `EditorScene`, `CEditor` | `EditorDoc_test` (`chunkBody`, `chunkFlipped`, `chunkFiles`) |
| `BotJob` | `BotJob_test` (`chunkFlipped`) |
| `ChunkLogic`, `LaunchRequest`, `ToolGrouping` | Swift `ios_editor_logic` |
| `ChunkStore`, `ChunkCore`, `CoreBridge`, `StoreBasics`, `Core`, `Models`, `LevelStore`, `ChunkStoreApp`, `BackupModel`, `ChunkModel` | Swift `ios_editor_chunks` |
| Views (`AppRoot`, `ChunkList`, `ChunkInspector`, `EditorScreen`, `Inspector`, `CourseCanvas`, `ToolPalette`, `HistoryWindow`, `LevelList`, `Backup`) | no unit tests (views); they'll be checked on screen at install |

## Stage 3: sync and the game (verified 2026-10-24)

**What it added:**
- **The Mac server.**
  - A `chunks` kind, each chunk its own file at `assets/chunks/<id>.txt`. The folder is now in git, empty apart from
    `.gitkeep`.
  - Pushes are compare-and-swap on each chunk's base.
  - Each chunk is checked on push by `level_check --chunk`. A bad chunk is rejected while the others in the push are
    still written.
  - A deleted chunk goes to the editor trash.
  - After any chunk change it runs `scripts/chunks/build_chunks.py`, which writes `src/core/ChunkLayouts.inc`.
- **The game.**
  - `LevelCatalog::chunks()`.
  - `Campaign` passes in-range chunks to generated levels 31+ only, never to levels that have a file.
  - With no chunks, the campaign is byte-identical to before (tested).
- **The iPad.**
  - Chunks sync: no change, pull, push, and delete.
  - When both sides changed a chunk, you choose: keep mine, take the Mac's, or keep mine as a `-copy`.
  - An older Mac server without chunks is skipped safely, so your chunks are never treated as deleted.
  - A fresh device is seeded once with the repo's chunks.

**My checks:**
- All C++ tests pass: 73 of 73 (new: `ReportJson_test`).
- Web: 42/42. Python (editor): 35 OK. Python (scripts): 17 OK.
- The iOS editor **and** the iOS game both build. `Chunk.cpp` moved to the `cs_level` library, so I built the game too.
- `assets/levels` and `assets/gauntlets` are unchanged.
- The level and gauntlet bake scripts were not changed in this stage.

**What you'll notice in sync:**
- the Sync banner at the top of the chunk list;
- an amber arrow on chunks the Mac doesn't have yet;
- "N not synced" on the Edit Chunks card;
- conflicts read like "The sacred wells chunk: both changed it", with a side-by-side sheet.

**Behaviour that changed:**
- **A grid the Mac rejects is now named differently.** The message reads "The tomb: …" instead of "Level grid tomb: …".
- **Renaming a chunk isn't tracked by sync.** To the Mac, it's one chunk deleted and a new one added. Its history moves
  with it on the iPad.

**Coverage:**

| Files | Tests |
|---|---|
| `server.py` | `test_sync.py` chunk tests: pull, push, CAS conflict, bad chunk rejected, delete to trash, bake after push, merge lock |
| `build_chunks.py` | `scripts/tests` `BuildChunks` |
| `level_check --chunk`, `ReportJson` | `ReportJson_test` |
| `LevelCatalog` | `LevelCatalog_test` |
| `Campaign` | `Campaign_test`: a 31–40 chunk in generated 35, not 41; level 30 and saved levels unchanged |
| Swift store and sync logic | `ios_editor_chunks`: old and new sync-state decoding, 14 plan cases, store against a pretend Mac, seeding |
| `SyncEngine`, `SyncViews`, `ChunkList`, `AppRoot` | views and network, build-checked; their decisions are in the tested `ChunkSyncLogic` |

**Gap:** `ck_builtin_json` is only tested with zero built-in chunks, because the repo has none yet. It will be
exercised once you sync your first chunk.

## Stage 4: maze chunks (verified 2026-10-24)

**What it added:**
- **Maze chunks in the generator.** **Generate a new maze** stamps maze chunks into a floor's stretches, where there's
  head room and length and no ledge or pad, with meter-refill gaps either side.
  - Uses are counted per maze.
  - With Mirror on, a chunk is also flipped as she runs it, on any floor. With Mirror off, or with a spring `^` in it,
    it's placed only on right-running floors.
  - The description lists the chunks, the notice and history name them, and the maze canvas outlines them in pulsing
    magenta.
- **The maze-chunk editor.** It's the maze canvas and tools on a doorless block, as small as 3 × 3.
  - The checker plays the chunk inside a plain test corridor: reachable, one route, no traps, the meter. Near misses
    are shown but not required.
  - With Mirror on, it checks the flipped chunk too.
  - **New maze chunk** is now enabled.
- **Refactor.** `MazeModel` now works through a `MazeHost` protocol and pluggable storage. Levels and chunks share it.

**My checks:**
- All C++ tests pass: 73 of 73. Two tests, the replay-clip timing and `level_editor`, failed once under the
  sub-agent's load and passed for it alone. In my run they all passed.
- Web: 43/43. Python: 35 and 17 OK.
- The iPad build succeeds.
- `assets/` is unchanged, and `assets/chunks` is still empty.

**Problems I found (being fixed in 4b):**
1. **A demanding maze chunk can make Generate a new maze fail completely.** With three saws in five cells at weight 10,
   none of 10 seven-floor attempts made a maze, after 120 tries and 10 s each. A roll must never come back empty
   because of a chunk. The fix: when tries with chunks fail, place fewer chunks or leave them out, say so in the
   notice, and still give you a maze. The course generator gets the same treatment if a chunk ever blocks every
   candidate.
2. **The Mac doesn't judge a maze chunk's playability on push**, only that it reads. A course chunk is judged. The fix:
   `level_check --chunk` runs the maze chunk's corridor check, flipped too.

**Decisions it made (yours to confirm):**
- A maze chunk's bottom row is the ground, with floor hazards set into it.
- Stone in its top row reaches up to the roof.
- A chunk is drawn running right; on a left-running floor it's placed mirrored, so she still runs it as drawn.
- The chunk checker doesn't require near misses.

**Numbers.** A 5 × 4 sacred-well chunk, level 20, 10 seeds:
- every maze was ready (10/10) at every weight and floor count;
- 2 to 11 placements per maze;
- tries and time no worse than with no chunk.

The demanding three-saw chunk is what fails, as above.

**Coverage:**

| Tests | What they cover |
|---|---|
| `MazeGen_test` (`chunks`, `chunkKinds`) | ranges 20 vs 21; uses; mirror on and off; spring and laser chunks; bot-cleared; byte-identical with none |
| `GridDoc_test` (`chunkMode`, `chunkChecked`) | chunk mode and the corridor check |
| `EditorDoc_test` | the C API |
| `Chunk_test` | maze mirror rules |
| Swift `ios_editor_chunks` (`mazeChunks`, `mazeChecker`, `mazeRoll`) | the store and model logic of maze chunks |

The views are build-checked only.

## Stage 4b: follow-ups and the final test pass (verified 2026-10-24)

**1. A roll never comes back empty because of a chunk.**
- **Maze.** After 12 tries that fail for want of a way through, the generator places fewer chunks: half the weight,
  one use each. After 6 more, it places none. The notice says which, e.g. "Left out: three-saws — no maze passed with
  it at level 20" or "Fewer of: …".
  - The demanding three-saw chunk on 7 floors made 0 of 4 mazes before, at about 10 s each. It now makes a maze every
    time, in about 1.3 s.
  - The slowest case is now 2.2 s, down from about 10 s.
- **Course.** If no candidate passes with the chunks, the generator retries **at the same density** without them. With
  several chunks it first tries leaving out each one on its own. It steps the density down only if that fails too.
  The history step says "left out: <id>". Before, a spoiling chunk silently dropped the roll to density 0.
- **A limit:** if a good chunk sits beside a bad one, both can be left out. Blame isn't worked out chunk by chunk.

**2. The Mac checks maze chunks on push.** `level_check --chunk` plays a maze chunk in the same test corridor the iPad
uses, flipped too when Mirror is on. An unplayable chunk is rejected on push, with the reason.

**3. The final test pass** (your request: tests for all new code and touched files).
- The sub-agent temporarily broke about 45 branches across the chunks work, and made sure a test catches each one.
- It added tests where none did:
  - the floating-floor guard;
  - the job's no-game mode;
  - the checklist's route, trap and meter lines;
  - the tries cap;
  - loading the built-in chunks, now tested with a stand-in.

| File | Tests | Remaining gap |
|---|---|---|
| `Chunk.*` | `Chunk_test` | none |
| `LevelBeats` | `LevelBeats_test` | the classic-generator fallback dropping chunks is hard to trigger |
| `MazeGen` | `MazeGen_test` | none |
| `ReportJson`, `level_check` | `ReportJson_test` | none |
| `LevelCatalog` | `LevelCatalog_test` | the baked chunk list is empty until you sync a chunk |
| `LevelBake`, `EditorDoc`, `CEditor`, `EditCourse`, `EditorTools`, `EditorScene`, `GridJob` | `EditorDoc_test` | none |
| `GridDoc` | `GridDoc_test` | none |
| `BotJob` | `BotJob_test` | none |
| `Campaign` | `Campaign_test` | two guards duplicated by others |
| `server.py` | `test_sync.py` | none |
| `build_chunks.py` | `test_scripts.py` | none |
| Wiki | `wiki.test.mjs` | none |
| Swift logic | `ios_editor_logic`, `ios_editor_chunks` | none |
| Views | build-checked only | checked on screen at install |

**My final checks (everything, after 4b):**
- All C++ tests pass: 73 of 73. Web: 44/44. Python: editor 36 OK, scripts 17 OK.
- The iOS editor and the iOS game both build.
- `assets/levels` and `assets/gauntlets` are unchanged (checksum `cd879dc5…`). `assets/chunks/` holds only `.gitkeep`.
- I stopped a background task the sub-agent left running after it reported, so nothing changes after these checks.
- Nothing is committed.

**Behaviour that changed in 4b:**
- A course roll with a spoiling chunk keeps its density and drops the chunk, rather than stepping the density down.
- Maze rolls place fewer chunks or leave them out, as above.
- **One test was rewritten, because the behaviour changed as you asked.** The old test said a maze chunk only gets read
  on push. The new one requires a playable chunk to be checked in the corridor, and an unplayable one to be turned
  down.

---

## Waiting for you

1. **Say "install"** and I'll put the editor on the iPad simulator, with your Documents backed up first and restored
   after. Then I'll go through every screen and log what I see:
   - the start screen;
   - the chunk list;
   - the course and maze chunk editors;
   - the Chunk settings card;
   - rolls with chunks outlined;
   - backup and restore;
   - the sync banner.
2. **Decisions to confirm or reverse:**
   - Chunk settings changes save at once and aren't undo steps.
   - A maze chunk's bottom row is the ground, and stone in its top row reaches the roof.
   - The maze chunk checker shows near misses but doesn't require them.
   - Renaming a chunk reaches the Mac as one chunk deleted and a new one added.
   - When a roll can't pass with a chunk, the chunk is left out, and a good chunk beside a bad one can be left out too.

---

## On screen: iPad Pro 13 simulator (2026-10-24, after "install")

Your Documents were backed up first, to `scratchpad/ipad-docs-backup6`.

**What worked:**
- **The start screen.** Animated **Edit Levels** ("30 levels · 1 not synced") and **Edit Chunks** ("None yet: make the
  first") cards. The previews move: a chunk block slides in, and the hen runs.
- **The empty chunk list.** It explains what a chunk is and offers **New course chunk** / **New maze chunk**, with the
  sync card ("Not paired with a Mac — Pair") and Home, + and backup buttons.
- **New course chunk.** A name prompt ("a flat run-in, a wall, a flat run-out…") opens the chunk editor.
- **The chunk editor.**
  - Hatched **RUN-IN** and **RUN-OUT** pads and a finish line, with the level tools (no Gauntlet tool).
  - **Chunk settings:** name, kind, reach ("21.6 m · pads 7.8 m"), the level range with from and to steppers, the
    weight slider ("3 · now and then"), max uses with **No limit**, Mirror ("Off: it is always placed as you built it"),
    and a description.
  - The checker says **All fair**, and the bot plays the chunk alone ("Cleared 8 s").
  - The file is written correctly, and changes to Mirror and the other settings save to it at once.
- **Rolls with a chunk.** I made a "hurdles" chunk (two saws and a step, range 20, uses 2, mirror on).
  - First, the Mac's `level_check --chunk` caught my first design: each saw sat too close to the next wall, and one came
    too soon after a drop when flipped. After a revision, it's fair both ways.
  - **Roll a new course** on level 20 at density 10 gave the history entry "A new course (seed 3479264799, density 10):
    chunks: hurdles ×2". The checker says all fair, and the bot clears it in 53 s.
  - The two placements: **flipped at 77.4 m on the street**, and **as built at 120.6 m up on floor 5**. So mirroring
    works, and the heights shift to whichever floor she's on.
  - **The canvas outline:** a magenta band labelled "HURDLES ⇄" (⇄ = flipped) over the placed chunk, its step and saw
    drawn inside.

**Issues found (small):**
1. **Cramped buttons.** In the narrow chunk sidebar, "New course chunk" and "New maze chunk" wrap onto three lines. They
   could be shorter ("Course" / "Maze" under a "New chunk" heading) or stacked full-width.
2. **`--detail` no longer hides the level list on launch**, so the sidebar stays open beside level 20. Probably a side
   effect of the new start-screen routing. Low priority; it only affects launch arguments.
3. **Tapping the overview strip didn't jump the canvas** to that point. I'm not sure whether it ever did; check against
   the old build if it matters.
4. **A new chunk's default range is "1"** (levels 1–1). Defaulting to the level you last had open, or 31–35, might be
   handier.
5. **Simulator only, not app bugs:** switches need a short hold from the simulator tool, and the first tap after an
   install is often dropped.

**Not yet exercised on screen:**
- **Back up chunks… / Restore chunks…** (these go through the Files sheet);
- the maze-chunk editor;
- **Generate a new maze** with a maze chunk;
- the sync banner with a paired Mac.

They're next if you want them walked through. Their logic is unit-tested.

**State now:** level 20 on the simulator has the rolled course, and `hurdles` is in the chunk list. Restore from
`ipad-docs-backup6` when you're done looking.

---

## iPad UI tests (2026-10-24)

**What was added:**
- **A UI test target, `CuckooEditorUITests`.** It holds 14 XCUITests in `tests/ui/ios_editor/`.
- **Its own simulator,** "Cuckoo UI Tests iPad" (C0A6218B…). Your iPad Pro 13 and iPhone 17 Pro Max are never used.
- **Clean data for every test,** through launch arguments the tests use:
  - `--ui-test-reset` gives the app a fresh data folder, with the built-in levels and no chunks;
  - `--ui-test-chunk <file>` loads a chunk into it;
  - `--ui-test-files <dir>` makes backup and restore use a folder instead of the Files sheets, which a test can't
    drive.
- **How to run them** (also in the README):

  ```
  platforms/ios/generate_xcode.sh B4HLN9759H
  xcodebuild test -project build-ios/CuckooStack.xcodeproj -scheme CuckooEditorUITests -destination 'id=C0A6218B-AE10-4347-A69A-A7DA606ED857'
  ```

  The suite takes about 6 minutes.

**The tests (my run: 14/14 passed, 356 s):**
- **Start screen:** both cards, and Home from both modes.
- **Chunk editor:**
  - a new course chunk opens with its pads, checker and band;
  - settings (two ranges, weight, uses, Mirror) reach the list row;
  - No limit and Mirror off clear the row's chips;
  - a maze chunk opens with its checker.
- **Rolls:** level 20 places the hurdles fixture and outlines it; level 21 places nothing; **Add a generated maze**
  places a maze chunk.
- **Backup:** back up, delete, restore (summary sheet, banner), undo. It also checks the backup file's contents.
- **Fixes:**
  - the new-chunk buttons stay on one line;
  - `--detail` hides the level list and the chunk list;
  - tapping and dragging the overview strip moves the canvas.

**The three issues from the walkthrough:**
1. **Cramped buttons: fixed.** The sidebar now has a "NEW CHUNK" heading with short **Course** and **Maze** buttons.
2. **`--detail`: made dependable.** The sub-agent couldn't reproduce the level list staying open. The split view now
   starts detail-only when `--detail` is passed, instead of folding after the first frame, and two tests cover it.
3. **The overview strip: not a bug.** It already jumps and drags the canvas, and tests cover it now. My earlier miss
   was the first tap after an install being dropped.

**A problem I found and fixed myself:** the project had **lost its `CuckooEditor` scheme.**
- **Why:** Xcode makes no scheme for an app that a shared scheme already names, and the new UI-test scheme names the
  editor. You couldn't have built or run the editor from Xcode, or with `-scheme CuckooEditor`.
- **The fix:** `platforms/ios/make_ui_test_scheme.py` now writes a shared `CuckooEditor` scheme as well. Run launches
  the editor, and Test runs the UI tests.
- Both the `CuckooEditor` and `CuckooStack` (game) schemes build again.
- Tested by `UITestScheme` in `scripts/tests/test_scripts.py`: both schemes are written, and none when there's no test
  target.

**Final state:**
- All C++ tests pass: 73 of 73. Web: 44. Python: editor 36, scripts 19.
- The UI tests pass 14/14.
- The editor and game build.
- `assets/` is unchanged.
- Nothing is committed.
