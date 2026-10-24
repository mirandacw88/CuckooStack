# Level editor commands

Every command for editing Cuckoo Stack's levels: the web editor on the Mac, the iPhone and iPad editor app
(Cuckoo Editor), syncing the two, and the checkers and scripts underneath. Run them from the project folder.

## The web editor (Mac)

```bash
python3 tools/level_editor/server.py --open
```

| Flag | What it does |
|---|---|
| `--open` | opens the editor in the browser (http://127.0.0.1:8765) |
| `--port N` | listens on port N instead of 8765 |
| `--no-build` | skips building the checkers and the desktop game first |
| `--lan` | also serves sync to the iPhone and iPad editor on this Wi-Fi (below) |

The editor's own guide: http://127.0.0.1:8765/wiki/ (`tools/level_editor/wiki`).

## The iPhone and iPad editor (Cuckoo Editor)

### Open it in Xcode

```bash
platforms/ios/generate_xcode.sh B4HLN9759H
open build-ios/CuckooStack.xcodeproj
```

Then pick the **CuckooEditor** scheme (CuckooStack is the game), pick a simulator or your device, and press Run (⌘R).

- Always pass the team ID to `generate_xcode.sh`: run without it, the project loses its signing team.
- Re-run `generate_xcode.sh` after adding or removing a Swift file in `src/platform/ios_editor` (the project lists
  them when it's generated).
- First install on a device: trust the developer in Settings → General → VPN & Device Management.

### Build it from the command line

```bash
xcodebuild -project build-ios/CuckooStack.xcodeproj -scheme CuckooEditor -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' build
```

Don't add `CODE_SIGNING_ALLOWED=NO`: the simulator then won't load the unsigned graphics framework (MoltenVK) and
the app quits at launch.

### Run it in a simulator

```bash
xcrun simctl install booted build-ios/Debug-iphonesimulator/CuckooEditor.app
```

```bash
xcrun simctl launch --terminate-running-process booted com.cuckoostack.aerospheregames.staging.editor --open 12
```

Launch arguments (after the app's ID):

| Argument | What it does |
|---|---|
| `--open N` | opens level N |
| `--detail` | with `--open`: the editor alone, the level list hidden (iPad) |
| `--inspector` | opens the inspector |
| `--history` | opens the level's History window |
| `--maze` | with `--open`: opens the level's maze (its gauntlet's grid) in the maze editor |
| `--play-bot` | starts the play-test with the bot playing |
| `--pencil-palette` | opens the Apple Pencil Pro squeeze palette (the simulator has no Pencil) |
| `--vk-validation` | turns on Vulkan's validation layers (checks every graphics call; slow, for debugging the renderer) |

An iPad simulator, if there's none yet:

```bash
xcrun simctl create "iPad mini (A17 Pro)" com.apple.CoreSimulator.SimDeviceType.iPad-mini-A17-Pro com.apple.CoreSimulator.SimRuntime.iOS-26-2
```

### The app icon

```bash
python3 scripts/icon/make_editor_icon.py
```

Renders `platforms/ios_editor/Assets.xcassets` (with Google Chrome, or the system's WebKit without it).

### In the app

What each part of the screen is called: [the labelled screenshot](images/cuckoo-editor-parts.png). Every tool, its
key and its numbers: [the tools sheet](images/cuckoo-editor-tools.png).

| On | Do |
|---|---|
| Canvas | one finger (or the Pencil): the tool; two fingers: pan and zoom; the Pan tool (hand): one finger pans |
| Tool groups (iPhone and iPad) | Build (walls, kind, barrier, block), Hazards (saw, wall laser, lava: the registry's group), Edit (columns, select); the disco ball its own. Tap picks the group's last tool; tap again or hold: its flyout (each tool, key, tip). Maze: Lines (columns, rows), cells as Terrain, Bouncers (bumper, gates, spring), Hazards, Doors. The eraser is its own tool: whatever's under the finger goes (hazard, pickup, block, barrier, walls to the street), whichever tool is picked; picking anything on the rail turns it off |
| iPad keyboard | a tool's letter picks it (H pan, W walls, K kind, B barrier, O block, S saw, D disco ball, Z wall laser, A lava, C columns, V select, G gauntlet, T start); ⇧T back to the level's start (clears the flag); 1–3 wall kinds; E or hold ⌥: erase; ⌘Z / ⇧⌘Z undo / redo; ⌘C ⌘X ⌘V ⌫ columns; Space play; ⌘⇧H history; ⇧⌘S sync; ⌘N new level; ⌘= / ⌘− zoom |
| Play (while playing) | B bot on/off, F speed 1×/2×/4×, R again, Esc back to editing |
| Start tool | hold it: back to the level's start (clears the flag), show the flag, place the flag |
| Backup (the levels list's drive button) | Back up all levels…: one file in Files with every level and maze and their histories (save it outside the app's folder, e.g. iCloud Drive); Restore from a backup…: brings one back, each level and maze in it replacing the one here |
| Generators | the inspector's Generators card, or the dice in the top bar: Roll a new course (⇧⌘L; each press another, one undo step each), Add a generated maze / Generate a new maze (⇧⌘G; its length and 3, 5 or 7 floors under the button); under each button, a switch per hazard the roll may place (saws, wall lasers, lava, disco balls; kept between launches; also in the dice menu); in the maze editor: Roll a new maze, with its switches |
| Gauntlet tool | tap (or G): the level's maze in the maze editor (no maze yet: place one); hold it: edit the maze, go to it, move or swap it, take it out |
| Maze editor keys | H pan, B brush, R box, D doors, C columns, W rows, T start; 1–0 the cells (air, stone, cube, bumper, gate →, gate ←, saw, cube saw, entry, exit; [ laser firing left, ] laser firing right, = lava, \ spring); X swap the two cells; E or hold ⌥: erase (the second cell; columns and rows: taken out); F fit; ⌘Z / ⇧⌘Z; ⌘= / ⌘−; ⇧T from the door; Space play; Esc done |
| Apple Pencil | hover: where it lands; double-tap: eraser or the tool before (as the Pencil's settings say); squeeze (Pencil Pro): the tools in a ring |

## Syncing the app with the Mac

```bash
python3 tools/level_editor/server.py --lan
```

It prints a pairing code and shows up on the Wi-Fi as "Cuckoo Editor on *your Mac*". In the app: the levels list's
banner → Pair → pick the Mac, type the code. After that it syncs when the app opens, when it's back on Wi-Fi, or
with the sync button.

- The code is kept in `build/editor-sync.token`; delete it and restart the server for a new one (re-pair the devices).
- Every sync is logged in `build/editor-sync.log`; a level taken out on the device goes to `build/editor-trash/`.
- Mazes edited on the device are pushed too (their levels refitted to their width); a maze changed on both sides
  asks which to keep, or keeps the device's as a copy (`<name>_copy`, in no level).
- Nothing is committed: the files it writes show in `git status` (`assets/levels`, `assets/gauntlets`, and the game's
  `src/core/LevelLayouts.inc` and `GauntletLayouts.inc`).
- Nothing is written while a git merge or rebase is under way.

## Checkers

Build them first (the web editor's server does this unless `--no-build`):

```bash
cmake --build build -j8
```

### A level

```bash
build/level_check --file assets/levels/level_12.txt --level 12
```

| Flag | What it does |
|---|---|
| `--json` | the whole report as JSON (the editors') |
| `--game` | also plays it in the real game, the bot playing, headless |
| `--gauntlet-file PATH` | a grid standing in for the saved one of its name (repeatable) |
| `--bake N [--gauntlet NAME\|none]` | prints level N as the generator would lay it |
| `--refit NAME WIDTH --file PATH` | prints the level with gauntlet NAME made WIDTH cells wide, what's after it moved along |
| `--registry-json` | the course's rules and the hazards' registry (what the editors read) |

### A gauntlet

```bash
build/gauntlet_check tomb --map --events
```

| Flag | What it does |
|---|---|
| `NAME` | a registered gauntlet, as its level plays it |
| `--file PATH` (`-`: stdin) | a grid file instead; `--name NAME` to name it |
| `--level N` | as level N plays it |
| `--level-file PATH` | with that level file standing in for its level |
| `--map` | draws the bot's way on the grid |
| `--reach` | marks everywhere she can get to |
| `--events` | lists the near misses |
| `--all` | every registered gauntlet |
| `--json [--game]` | the whole report as JSON; `--game` plays it in the real game too |

## Scripts

```bash
python3 scripts/levels/build_levels.py
```

Rebuilds `src/core/LevelLayouts.inc` from `assets/levels/` (`--bake N` writes level N's file as the generator lays
it; `--force` to overwrite one).

```bash
python3 scripts/gauntlets/build_gauntlets.py
```

Rebuilds `src/core/GauntletLayouts.inc` from `assets/gauntlets/` (`--print NAME` prints a grid; `--draft NAME`
writes a grid from its first draft).

## Tests

```bash
ctest --test-dir build -j8
```

Just the editors' tests:

```bash
ctest --test-dir build -R "EditorDoc|GridDoc|MazeGen|LevelMerge|BotJob|Hazards|LevelCatalog|LevelData|level_editor"
```

The pre-commit hook runs every test; failures block the commit. `GameDev` and `GameDroid` (the bot plays every level)
take about two minutes each: close the simulators before running everything, or they can run past their limit.
