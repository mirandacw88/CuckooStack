// The iPhone and iPad editor's rules apart from its looks (ToolGrouping.swift, MazeCells.swift, GridNames.swift,
// ChunkLogic.swift, LaunchRequest.swift): the course's rail and its groups, the maze's, what a group's button wears and
// whether it's lit (none while erasing), what each group last had (kept between launches), the generators' switches as
// the C API takes them, the maze's cells by family, and the chunks' names, ranges, words, master backup and restore plan,
// and what the launch arguments open.
import Foundation

var failures = 0
func check(_ ok: Bool, _ what: String) {
    print((ok ? "ok    " : "FAIL  ") + what)
    if !ok { failures += 1 }
}

// ---- the course's rail
let registry: [(id: String, group: String)] = [("saw", "hazards"), ("disco", "pickups"), ("laser", "hazards"), ("lava", "hazards")]
let rail = ToolGrouping.courseRail(hazards: registry)
check(rail == [.tool("pan"),
               .group(.init(id: "build", label: "Build", members: ["wall", "kind", "barrier", "block"])),
               .group(.init(id: "hazards", label: "Hazards", members: ["saw", "laser", "lava"])),
               .tool("disco"),
               .group(.init(id: "edit", label: "Edit", members: ["cols", "select"])),
               .tool("gauntlet"), .tool("start")],
      "the course's rail: Pan, Build, Hazards (the registry's, in its order), the disco ball its own, Edit, Gauntlet, Start")
check(ToolGrouping.courseRail(hazards: registry + [("star", "pickups")]).contains(.group(.init(id: "pickups", label: "Pickups", members: ["disco", "star"]))),
      "...a second pickup: a Pickups group")
check(ToolGrouping.courseRail(hazards: registry + [("spike", "hazards")]).contains(.group(.init(id: "hazards", label: "Hazards", members: ["saw", "laser", "lava", "spike"]))),
      "...a new zone's hazard joins the Hazards flyout with nothing else written")
check(!ToolGrouping.courseRail(hazards: []).contains { if case .group(let g) = $0 { return g.id == "hazards" } else { return false } },
      "...no hazards: no Hazards button")
check(ToolGrouping.group(of: "laser", in: rail)?.id == "hazards" && ToolGrouping.group(of: "kind", in: rail)?.id == "build" && ToolGrouping.group(of: "pan", in: rail) == nil,
      "a tool's group (its button lit for it); a tool of its own: none")

// ---- the maze's rail
let maze = ToolGrouping.mazeRail(tools: ["pan", "brush", "box", "door", "cols", "rows", "start"])
check(maze == [.tool("pan"), .tool("brush"), .tool("box"), .tool("door"), .group(.init(id: "maze.lines", label: "Lines", members: ["cols", "rows"])), .tool("start")],
      "the maze's rail: Columns and Rows as Lines, where Columns was")

// ---- a group's button
let members = ["saw", "laser", "lava"]
var f = ToolGrouping.face(members: members, current: "laser", erasing: false, remembered: "lava")
check(f.shown == "laser" && f.lit, "a group's button: its tool picked, it wears it, lit")
f = ToolGrouping.face(members: members, current: "wall", erasing: false, remembered: "lava")
check(f.shown == "lava" && !f.lit, "...another tool picked: it wears the one it last had, not lit")
f = ToolGrouping.face(members: members, current: "laser", erasing: true, remembered: "lava")
check(f.shown == "lava" && !f.lit, "...erasing: not lit (a tap picks its tool, putting the eraser away)")
f = ToolGrouping.face(members: members, current: "wall", erasing: false, remembered: "gone")
check(f.shown == "saw" && !f.lit, "...one it last had that's gone (or none): its first")
check(ToolGrouping.lit("wall", current: "wall", erasing: false) && !ToolGrouping.lit("wall", current: "wall", erasing: true) && !ToolGrouping.lit("pan", current: "wall", erasing: false),
      "a tool's own button: lit when it's the tool, never while erasing")

// ---- what each group last had
let suite = "cuckoo.tests.\(ProcessInfo.processInfo.processIdentifier)"
GroupMemory.store = UserDefaults(suiteName: suite)!
check(GroupMemory.get("hazards") == nil, "a group's memory: nothing yet")
GroupMemory.set("hazards", "lava")
GroupMemory.set("build", "block")
check(GroupMemory.get("hazards") == "lava" && GroupMemory.get("build") == "block", "...each group its own, kept")
check(UserDefaults(suiteName: suite)?.string(forKey: "toolGroup.hazards") == "lava", "...in the app's defaults (between launches)")
UserDefaults.standard.removePersistentDomain(forName: suite)

// ---- the generators' switches
check(HazardSwitches.course([("saw", true), ("disco", true), ("laser", true), ("beacon", false), ("lava", true)]) == ["saw", "disco", "laser", "lava"],
      "a course roll's switches: the hazards the generator lays, in the registry's order")
check(HazardSwitches.maze(registry: ["saw", "disco", "laser", "lava"]) == ["saw", "laser", "lava"] && HazardSwitches.maze(registry: ["saw"]) == ["saw"],
      "a maze roll's: those a maze has cells for")
check(HazardSwitches.rolled(["saw", "laser", "lava"], off: ["laser"]) == ["saw", "lava"], "a roll: only those switched on")
check(HazardSwitches.argument(["saw", "lava"]) == "saw,lava" && HazardSwitches.argument([]) == "none", "...as the C API takes them (none: \"none\", not \"\": that's all)")

// ---- the maze's cells
let keys = MazeCell.all.map(\.key), chars = MazeCell.all.map(\.ch)
check(Set(keys).count == keys.count && Set(chars).count == chars.count, "the maze's cells: each its own key and character")
check(MazeCellFamily.all.map(\.id) == ["terrain", "bouncers", "hazards", "doors"] && MazeCell.all.allSatisfy { c in MazeCellFamily.all.contains { $0.id == c.family } },
      "...each in one of four families: Terrain, Bouncers, Hazards, Doors")
check(MazeCellFamily.all.first { $0.id == "hazards" }?.cells.map(\.ch) == ["S", "C", "(", ")", "~"], "the Hazards family: saws, the wall lasers (left, right), lava")
check(MazeCellFamily.all.first { $0.id == "bouncers" }?.cells.map(\.ch) == ["B", "R", "L", "^"] && MazeCell.named("^")?.name == "Spring" && MazeCell.named("^")?.key == "\\",
      "the Bouncers family: the bumper, the gates and the spring (on the backslash)")
check(MazeCell.named("(")?.key == "[" && MazeCell.named(")")?.key == "]" && MazeCell.named("=") == nil, "...the lasers on [ and ]; the old beam cell gone")
check(MazeCellFamily.all.first { $0.id == "doors" }?.cells.allSatisfy(\.isDoor) == true, "the Doors family: the entry and the exit")

// ---- a maze's name, and the renames the Mac is told of
check(GridNames.typed("  Hall of Saws ") == "hall_of_saws" && GridNames.typed("sand-tomb") == "sand_tomb", "a name as typed: the file's (lower case, spaces as _)")
check(GridNames.shown("hall_of_saws") == "hall of saws", "...shown with spaces")
var renames = GridNames.pending([], from: "tomb", to: "crypt", known: true)
check(renames == [GridRename(from: "tomb", to: "crypt")], "a maze the Mac has, renamed: the Mac told")
renames = GridNames.pending(renames, from: "crypt", to: "vault", known: true)
check(renames == [GridRename(from: "tomb", to: "vault")], "...renamed again before a sync: one rename")
check(GridNames.pending(renames, from: "vault", to: "tomb", known: true).isEmpty, "...renamed back: nothing to tell")
check(GridNames.pending([], from: "maze_20", to: "my_maze", known: false).isEmpty, "a maze the Mac never had: nothing to tell (it's new there)")

// ---- a chunk has no maze door
let chunkRail = ToolGrouping.courseRail(hazards: registry, gauntlet: false)
check(chunkRail == rail.filter { $0 != .tool("gauntlet") } && chunkRail.last == .tool("start"), "a chunk's rail: the course's without the Gauntlet tool, Start last")
check(ToolGrouping.courseRail(hazards: registry, gauntlet: true) == rail, "...a level's rail as it was")

// ---- a chunk's ranges
check(LevelRange(3, 1) == LevelRange(3, 3) && LevelRange(0, 4).from == 1 && LevelRange(5, 99999).to == 9999, "a range: never before its start, from level 1, to 9999")
check(LevelRange(1, 5).fileText == "1-5" && LevelRange(7, 7).fileText == "7" && LevelRange(1, 5).shown == "1\u{2013}5" && LevelRange(7, 7).shown == "7", "...as the file has it (1-5, 7) and as the screens show it (1\u{2013}5)")
check(ChunkRanges.added(to: []) == [LevelRange(1, 5)] && ChunkRanges.added(to: [LevelRange(1, 5)]) == [LevelRange(1, 5), LevelRange(6, 10)] && ChunkRanges.added(to: [LevelRange(31, 40), LevelRange(2, 3)]).last == LevelRange(4, 8),
      "a range added: the five levels after the last one's end")
check(ChunkRanges.added(to: [LevelRange(9990, 9999)]).last == LevelRange(9999, 9999), "...none past level 9999")
check(ChunkRanges.removed(0, from: [LevelRange(1, 5), LevelRange(6, 10)]) == [LevelRange(6, 10)] && ChunkRanges.removed(0, from: [LevelRange(1, 5)]) == [LevelRange(1, 5)] && ChunkRanges.removed(5, from: [LevelRange(1, 5)]) == [LevelRange(1, 5)],
      "a range taken out; a chunk keeps one")
check(ChunkRanges.set(0, from: 8, in: [LevelRange(1, 5)]) == [LevelRange(8, 8)] && ChunkRanges.set(0, to: 9, in: [LevelRange(1, 5)]) == [LevelRange(1, 9)] && ChunkRanges.set(0, to: 0, in: [LevelRange(3, 5)]) == [LevelRange(3, 3)] && ChunkRanges.set(3, from: 2, in: [LevelRange(1, 5)]) == [LevelRange(1, 5)],
      "a range's ends set: to is pushed up to from; a range that isn't there: nothing")
check(ChunkRanges.covers([LevelRange(1, 5), LevelRange(31, 40)], level: 33) && !ChunkRanges.covers([LevelRange(1, 5), LevelRange(31, 40)], level: 6) && ChunkRanges.covers([LevelRange(2, 3), LevelRange(3, 9)], level: 3), "a level covered: by any range, overlapping ones too")
var header = ChunkHeader()
header.ranges = [LevelRange(31, 40), LevelRange(2, 3), LevelRange(7, 7)]
header.notes = ["", "  Sacred wells.  ", "more"]
check(header.first == 31 && header.rangesText == "31-40 2-3 7" && header.summary == "Sacred wells." && ChunkHeader().summary.isEmpty && ChunkHeader(ranges: []).first == 1,
      "a chunk's settings: judged at its first range's start, its ranges as the file has them, its name its first description line")

// ---- words
check(ChunkWords.uses(0) == "No limit" && ChunkWords.uses(2) == "2 a level" && ChunkWords.uses(-1) == "No limit", "uses in words: 2 a level, No limit")
check(ChunkWords.band(of: 1) == 0 && ChunkWords.band(of: 5) == 0 && ChunkWords.band(of: 6) == 1 && ChunkWords.band(of: 31) == 6 && ChunkWords.band(of: 0) == 0, "the list's band of five: 1-5, 6-10, 31-35 ...")
check(ChunkWords.bandTitle(0) == "Levels 1\u{2013}5" && ChunkWords.bandTitle(6) == "Levels 31\u{2013}35", "...its title")
check(ChunkWords.weight(1) == "rare" && ChunkWords.weight(3) == "now and then" && ChunkWords.weight(6) == "often" && ChunkWords.weight(10) == "everywhere", "weight in words")
check(ChunkWords.zone(forLevel: 10) == "city" && ChunkWords.zone(forLevel: 11) == "egypt" && ChunkWords.zone(forLevel: 20) == "egypt" && ChunkWords.zone(forLevel: 21) == "japan" && ChunkWords.zone(forLevel: 31) == "city", "the zone a row looks like")
check(ChunkWords.used(["hurdle", "steps", "hurdle"]) == "hurdle \u{00D7}2, steps" && ChunkWords.used([]).isEmpty && ChunkWords.used(["a"]) == "a", "the chunks a roll placed: 'hurdle \u{00D7}2, steps'")
check(ChunkWords.rollNotice(skipped: [], note: "", placed: []) == nil, "a roll's notice: nothing to say, no notice")
check(ChunkWords.rollNotice(skipped: [], note: "", placed: ["a", "a", "b"]) == "Chunks placed: a \u{00D7}2, b", "...chunks placed")
check(ChunkWords.rollNotice(skipped: ["bad"], note: "", placed: []) == "Left out: bad (they don't read)", "...a chunk that doesn't read")
check(ChunkWords.rollNotice(skipped: [], note: "Left out: three-saws \u{2014} no maze passed with it at level 20", placed: []) == "Left out: three-saws \u{2014} no maze passed with it at level 20", "...the generator's note, as it words it")
check(ChunkWords.rollNotice(skipped: ["bad"], note: "Fewer of: a \u{2014} no maze passed with all of them at level 7", placed: ["a"]) == "Left out: bad (they don't read) \u{00B7} Fewer of: a \u{2014} no maze passed with all of them at level 7 \u{00B7} Chunks placed: a", "...all three, in that order")
check(ChunkWords.mazeLabel(seed: 7, placed: [], dropped: [], reduced: []) == "A new maze (seed 7)", "a new maze's label: plain")
check(ChunkWords.mazeLabel(seed: 7, placed: ["a", "a"], dropped: [], reduced: []) == "A new maze (seed 7, chunks: a \u{00D7}2)", "...with chunks")
check(ChunkWords.mazeLabel(seed: 7, placed: ["a"], dropped: ["b", "c"], reduced: []) == "A new maze (seed 7, chunks: a; left out: b, c)", "...with chunks left out")
check(ChunkWords.mazeLabel(seed: 7, placed: [], dropped: [], reduced: ["b"]) == "A new maze (seed 7, fewer of: b)", "...with chunks laid fewer")

// ---- the bot's word on a chunk, and its pads
check(ChunkVerdict.cleared(plain: true, flipped: nil) && ChunkVerdict.cleared(plain: true, flipped: true) && !ChunkVerdict.cleared(plain: true, flipped: false) && !ChunkVerdict.cleared(plain: false, flipped: true) && !ChunkVerdict.cleared(plain: false, flipped: nil),
      "a chunk is cleared when the bot clears it as built and, if it played it flipped, flipped too")
check(ChunkVerdict.flippedWords(cleared: true, seconds: 8.6, x: 20) == "Flipped: cleared 9 s" && ChunkVerdict.flippedWords(cleared: false, seconds: 3, x: 7.4) == "Flipped: crashed at 7 m", "...the flipped play in words")
let pads = ChunkPads.bands(reach: 21.6, runIn: 13, runOut: 13, cell: 0.6)
check(pads.count == 2 && pads[0].x0 == 0 && abs(pads[0].x1 - 7.8) < 1e-9 && pads[0].label == "RUN-IN" && abs(pads[1].x0 - 13.8) < 1e-9 && pads[1].x1 == 21.6 && pads[1].label == "RUN-OUT", "a chunk's pads as bands: the run-in from the start, the run-out to the end")
check(ChunkPads.bands(reach: 10, runIn: 0, runOut: 0, cell: 0.6).isEmpty && ChunkPads.bands(reach: 10, runIn: 5, runOut: 0, cell: 0.6).count == 1, "...a pad that isn't there: no band")

// ---- names
check(ChunkNames.typed("  Sacred Wells ") == "sacred-wells" && ChunkNames.typed("Hall of  Saws!") == "hall-of-saws" && ChunkNames.typed("a_b-c 9") == "a_b-c-9" && ChunkNames.typed("--x--") == "x" && ChunkNames.typed("???").isEmpty,
      "a name as typed: the file's (lower case, a space as '-', nothing else but digits, '_' and '-')")
check(ChunkNames.valid("sacred-wells_2") && !ChunkNames.valid("Sacred") && !ChunkNames.valid("") && !ChunkNames.valid("a b") && !ChunkNames.valid(String(repeating: "a", count: 61)) && ChunkNames.valid(String(repeating: "a", count: 60)),
      "...valid: lower case letters, digits, '-' and '_', up to 60")
check(ChunkNames.shown("sacred-wells_2") == "sacred wells 2", "...shown with spaces")
check(ChunkNames.unique("hurdles", taken: []) == "hurdles" && ChunkNames.unique("hurdles", taken: ["hurdles"]) == "hurdles-2" && ChunkNames.unique("hurdles", taken: ["hurdles", "hurdles-2"]) == "hurdles-3" && ChunkNames.unique("Bad Name", taken: []) == "chunk",
      "a free name: numbered if taken; one that isn't a name: 'chunk'")
check(ChunkNames.copy(of: "hurdles", taken: ["hurdles"]) == "hurdles-copy" && ChunkNames.copy(of: "hurdles", taken: ["hurdles", "hurdles-copy"]) == "hurdles-copy-2" && ChunkNames.valid(ChunkNames.copy(of: String(repeating: "a", count: 60), taken: [])),
      "a copy's name: -copy, then numbered; always a name")

// ---- the master backup
let made = Date(timeIntervalSince1970: 1_792_843_200) // 2026-10-24, noon UTC
let backup = ChunkBackup(made: made, app: "Cuckoo Editor 1.0 (1)", chunks: [.init(id: "hurdles", text: "chunk course\nranges 1-5\n---\nlength 20\n"), .init(id: "well", text: "; Well\nchunk maze\n---\n##\n")])
if let data = try? backup.encoded(), let back = try? ChunkBackup.decode(data) {
    check(back == backup && back.chunks.count == 2 && back.chunks[1].text.hasPrefix("; Well"), "a chunks backup: written and read back, every chunk's id and whole file")
    check(String(decoding: data, as: UTF8.self).contains("\"cuckoo-chunks-backup\"") && String(decoding: data, as: UTF8.self).contains("\"version\" : 1"), "...JSON, with its format and version")
} else { check(false, "a chunks backup: written and read back") }
check(ChunkBackup.fileName(made) == "Cuckoo Chunks 2026-10-24" && ChunkBackup.fileExtension == "cuckoochunks", "...named Cuckoo Chunks <date>, in a .cuckoochunks file")
check((try? ChunkBackup.decode(Data("{\"format\":\"something-else\",\"version\":1,\"made\":\"2026-10-24T00:00:00Z\",\"app\":\"x\",\"chunks\":[]}".utf8))) == nil && (try? ChunkBackup.decode(Data("nonsense".utf8))) == nil,
      "...another kind of file, or not JSON: turned down")
var twice = backup
twice.chunks.append(.init(id: "hurdles", text: "x"))
var badName = backup
badName.chunks[0].id = "../etc"
check((try? ChunkBackup.decode(twice.encoded())) == nil && (try? ChunkBackup.decode(badName.encoded())) == nil, "...a name twice, or one that isn't a chunk's: turned down (a file name must not walk out of the folder)")

// ---- what a restore would do
let here = ["hurdles": "chunk course\nranges 1-5\n---\nlength 20\n", "well": "; Well changed\nchunk maze\n---\n##\n", "mine": "x"]
var plan = ChunkRestorePlan(backup: backup, current: here)
check(plan.new.isEmpty && plan.changed == ["well"] && plan.unchanged == ["hurdles"] && plan.kept == ["mine"] && !plan.isEmpty, "a restore's plan: the chunk that differs is changed, the same one unchanged, one only here kept")
check(plan.summary == "1 changed, 1 unchanged", "...in words")
plan = ChunkRestorePlan(backup: backup, current: ["hurdles": "chunk course\r\nranges 1-5\r\n---\r\nlength 20"])
check(plan.unchanged == ["hurdles"] && plan.new == ["well"] && plan.summary == "1 new, 1 unchanged", "...line endings and a last newline aren't a change; a chunk not here is new")
plan = ChunkRestorePlan(backup: backup, current: ["hurdles": backup.chunks[0].text, "well": backup.chunks[1].text])
check(plan.isEmpty && plan.summary == "2 unchanged", "...everything the same: nothing to restore")
check(ChunkRestorePlan(backup: ChunkBackup(made: made, app: "x", chunks: []), current: [:]).summary == "Nothing in it", "...an empty backup")

// ---- the launch arguments
check(LaunchRequest(["app"]).mode == .home && LaunchRequest([]).level == nil, "launch: no arguments, the start screen")
check(LaunchRequest(["app", "--open", "12"]) == { var r = LaunchRequest([]); r.mode = .levels; r.level = 12; return r }(), "--open N: the levels, that one open")
check(LaunchRequest(["app", "--open", "x"]).mode == .home && LaunchRequest(["app", "--open"]).mode == .home, "...not a number, or none: the start screen")
check(LaunchRequest(["app", "--chunks"]).mode == .chunks && LaunchRequest(["app", "--chunks"]).chunk == nil, "--chunks: the chunks")
check(LaunchRequest(["app", "--chunks", "hurdles"]).chunk == "hurdles" && LaunchRequest(["app", "--chunks", "--inspector"]).chunk == nil && LaunchRequest(["app", "--chunks", "--inspector"]).mode == .chunks, "--chunks id: that chunk open (a flag after it is not an id)")
check(LaunchRequest(["app", "--open", "3", "--detail"]).detailOnly && !LaunchRequest(["app", "--open", "3"]).detailOnly, "--detail: the editor alone")
check(LaunchRequest(["app", "--open", "3", "--chunks", "x"]).mode == .levels, "--open wins over --chunks")

let ut = LaunchRequest(["app", "--ui-test-reset", "--ui-test-chunk", "/a/hurdles.txt", "--ui-test-chunk", "/b/steps.txt", "--ui-test-files", "/tmp/x", "--chunks"])
check(ut.uiTestReset && ut.uiTestChunks == ["/a/hurdles.txt", "/b/steps.txt"] && ut.uiTestFiles == "/tmp/x" && ut.mode == .chunks, "--ui-test-reset, -chunk (any number) and -files are read")
check(!LaunchRequest(["app", "--open", "3"]).uiTestReset && LaunchRequest(["app"]).uiTestChunks.isEmpty && LaunchRequest(["app"]).uiTestFiles == nil, "...and are off without them")
check(LaunchRequest(["app", "--ui-test-chunk", "--chunks"]).uiTestChunks.isEmpty && LaunchRequest(["app", "--ui-test-files"]).uiTestFiles == nil, "...a flag after one is not its value")
let tmpRoot = FileManager.default.temporaryDirectory.appendingPathComponent("launch-test-\(ProcessInfo.processInfo.processIdentifier)")
try? FileManager.default.createDirectory(at: tmpRoot, withIntermediateDirectories: true)
check(LaunchRequest(["app"]).dataRoot(temp: tmpRoot) == nil, "no reset: the data stays in Documents (no folder)")
let fresh = LaunchRequest(["app", "--ui-test-reset"]).dataRoot(temp: tmpRoot)
check(fresh != nil && fresh!.path.hasPrefix(tmpRoot.path), "a reset: a folder in the temporary one")
if let fresh {
    try? "old".write(to: fresh.appendingPathComponent("left-over"), atomically: true, encoding: .utf8)
    let again = LaunchRequest(["app", "--ui-test-reset"]).dataRoot(temp: tmpRoot)
    check(again == fresh && !FileManager.default.fileExists(atPath: fresh.appendingPathComponent("left-over").path), "...and it starts empty each time")
}
try? FileManager.default.removeItem(at: tmpRoot)

print(failures == 0 ? "all ios_editor_logic checks passed" : "ios_editor_logic TESTS FAILED")
exit(failures == 0 ? 0 : 1)
