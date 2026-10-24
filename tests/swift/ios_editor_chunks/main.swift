// The chunk editor's store and its link to the core (ChunkStore.swift, ChunkCore.swift, CoreBridge.swift, StoreBasics.swift),
// built with the editor's C++ core linked in: the chunk files read, written and ordered by the core; the store's files in a
// scratch folder (new, copy, rename, delete, histories, the bot's words), what a roll is given, the master backup and its
// restore as one undoable step with a "before restore" copy, and the automatic backup's chunks; and chunk sync (ChunkSyncLogic.swift,
// the store's side of SyncEngine.syncChunks): the sync state read from old and new files, the plan for each chunk (nothing changed,
// the Mac's pulled, mine pushed, changed on both a choice, taken out, a chunk renamed), what the store does with it against a
// pretend Mac, and the game's own chunks seeded on a fresh device.
import Combine
import Foundation

var failures = 0
func check(_ ok: Bool, _ what: String) {
    print((ok ? "ok    " : "FAIL  ") + what)
    if !ok { failures += 1 }
}

/// the bot's words, kept in memory (the app keeps them in the levels' sync state)
@MainActor
final class MemoryVerdicts: VerdictBook {
    var verdicts: [String: Verdict] = [:]
    var chunkEntries: [String: SyncState.Entry] = [:]
    var chunksSeeded = false
    var saves = 0
    func saveState() { saves += 1 }
}

func scratch() -> URL {
    let u = FileManager.default.temporaryDirectory.appendingPathComponent("chunk-store-test-\(UUID().uuidString)")
    try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
    return u
}

@MainActor
func run() {
    // ---- the core's chunk files
    let blank = Chunks.blank("hurdles", first: 20)
    let f = Chunks.parse("hurdles", blank)
    check(f.ok && f.reads && !f.isMaze && f.first == 20 && f.canMirror == true && (f.body ?? "").hasPrefix("length "), "Chunks.blank: a new course chunk for level 20 that reads, its body a level")
    check(f.header == ChunkHeader(maze: false, notes: [], ranges: [LevelRange(20, 20)], weight: 3, uses: 0, mirror: false), "...its settings: level 20 only, weight 3, uses any, no mirror")
    var h = f.header
    h.notes = ["Hurdles.", "Two lines."]
    h.ranges = [LevelRange(31, 40), LevelRange(2, 3)]
    h.weight = 7; h.uses = 2; h.mirror = true
    let text = Chunks.text(h, body: f.body ?? "")
    let g = Chunks.parse("hurdles", text)
    check(g.reads && g.header == h && g.first == 31 && g.body == f.body, "Chunks.text: a header and a body make a file that reads back the same")
    var broken = text
    broken += "saws\n3\n"
    let b = Chunks.parse("hurdles", broken)
    check(b.ok && !b.reads && (b.error ?? "").contains("run-in") && b.header == h && (b.body ?? "").contains("saws"), "a body that breaks the rules: the header and body still come back, the error says why")
    check(!Chunks.parse("hurdles", "nonsense").ok && !Chunks.parse("Bad Name", blank).ok, "a file that isn't one, or a bad name: not ok")
    check(Chunks.isValid("sacred-wells") && !Chunks.isValid("Sacred Wells") && !Chunks.isValid(""), "Chunks.isValid: the core's rule for a name")
    check(Chunks.parse("a", Chunks.text(ChunkHeader(), body: "length 1\n")).header == ChunkHeader(), "(the default settings round trip)")
    let order = Chunks.sorted(["late": Chunks.text(ChunkHeader(ranges: [LevelRange(16, 20)]), body: "x\n"),
                               "wide": Chunks.text(ChunkHeader(ranges: [LevelRange(2, 9)]), body: "x\n"),
                               "early": Chunks.text(ChunkHeader(ranges: [LevelRange(1, 5)]), body: "x\n"),
                               "junk": "nonsense"])
    check(order == ["early", "wide", "late", "junk"], "Chunks.sorted: the core's list order (bands of five by the first range's start), a file that doesn't read last")

    // ---- the store
    let root = scratch()
    defer { try? FileManager.default.removeItem(at: root) }
    let book = MemoryVerdicts()
    let store = ChunkStore(root: root, verdicts: book)
    check(store.chunks.isEmpty && FileManager.default.fileExists(atPath: root.appendingPathComponent("chunks").path), "a new store: no chunks, a chunks folder")
    let id = store.newCourse(named: "Hurdles", first: 20)
    check(id == "hurdles" && store.exists("hurdles") && store.chunks.map(\.id) == ["hurdles"], "newCourse: a file under the name typed, in the list")
    let e = store.entry("hurdles")
    check(e?.header.first == 20 && e?.problem == "" && e?.canMirror == true && e?.maze == false && e?.title == "Hurdles", "...its entry: the first range, no problem, can be flipped, a course chunk, titled by its name")
    check(store.newCourse(named: "hurdles") == "hurdles-2" && store.newCourse(named: "  ") == "new-chunk" && store.newCourse(named: "!!") == "new-chunk-2", "a name taken: numbered; none, or none left after cleaning: 'new-chunk'")
    check(store.chunks.map(\.id) == ["hurdles-2", "new-chunk", "new-chunk-2", "hurdles"], "the list: bands of five by the first range (level 1 first, level 20 after)")
    // saving changes its place and its title
    var hh = store.entry("hurdles")!.header
    hh.ranges = [LevelRange(1, 1)]
    hh.notes = ["The hurdles."]
    store.save("hurdles", text: Chunks.text(hh, body: Chunks.parse("hurdles", store.text(of: "hurdles") ?? "").body ?? ""))
    check(store.chunks.map(\.id) == ["hurdles", "hurdles-2", "new-chunk", "new-chunk-2"] && store.entry("hurdles")?.title == "The hurdles." && store.entry("hurdles")?.header.ranges == [LevelRange(1, 1)],
          "a save that moves its first range: it moves in the list; its first description line is its title")
    hh.weight = 9
    let before = store.chunks.map(\.id)
    store.save("hurdles", text: Chunks.text(hh, body: Chunks.parse("hurdles", store.text(of: "hurdles") ?? "").body ?? ""))
    check(store.chunks.map(\.id) == before && store.entry("hurdles")?.header.weight == 9, "...a save that doesn't: the entry changes in place")
    let same = store.text(of: "hurdles")
    store.save("hurdles", text: same ?? "")
    check(store.text(of: "hurdles") == same, "...a save of the same text changes nothing")
    // a file that doesn't read still has its row
    store.save("junk", text: "nonsense")
    check(store.entry("junk")?.problem.isEmpty == false && store.chunks.last?.id == "junk", "a file that doesn't read: a row, with why, last in the list")
    store.delete("junk")
    // histories, copies
    store.saveHistory("hurdles", HistoryFile(current: "c", undo: [.init(label: "x", time: 1, at: 2, text: "t")], redo: []))
    check(store.history(of: "hurdles")?.undo.first?.label == "x", "a history: kept and read back")
    let copy = store.duplicate("hurdles")
    check(copy == "hurdles-copy" && store.text(of: "hurdles-copy") == store.text(of: "hurdles") && store.history(of: "hurdles-copy")?.current == "c", "duplicate: the file and its history under a new name")
    check(store.duplicate("nothing") == nil, "...none to copy")
    // verdicts
    let he = store.entry("hurdles")!
    store.setVerdict("hurdles", cleared: true, seconds: 12, key: he.verdictKey)
    check(store.verdict(he)?.cleared == true && book.saves > 0, "the bot's word: kept in the book, read back for the same chunk")
    var heavier = he.header
    heavier.weight = 2; heavier.uses = 1
    store.save("hurdles", text: Chunks.text(heavier, body: Chunks.parse("hurdles", store.text(of: "hurdles") ?? "").body ?? ""))
    check(store.verdict(store.entry("hurdles")!)?.cleared == true, "...a weight or uses changed: the bot's word stands (it doesn't change how it plays)")
    var moved = heavier
    moved.ranges = [LevelRange(8, 9)]
    store.save("hurdles", text: Chunks.text(moved, body: Chunks.parse("hurdles", store.text(of: "hurdles") ?? "").body ?? ""))
    check(store.verdict(store.entry("hurdles")!) == nil, "...its first level moved (a new difficulty): stale")
    check(ChunkStore.verdictKey(first: 3, flipped: true, body: "b") != ChunkStore.verdictKey(first: 3, flipped: false, body: "b") && ChunkStore.verdictKey(first: 3, flipped: false, body: "b") != ChunkStore.verdictKey(first: 3, flipped: false, body: "c"),
          "(the key: the level, whether it was played flipped, the body)")
    // rename
    store.setVerdict("hurdles-copy", cleared: false, seconds: 3, key: store.entry("hurdles-copy")!.verdictKey)
    var renamed: [String] = []
    let renameSink = store.renames.sink { renamed.append($0.from + ">" + $0.to) }
    check(store.rename("hurdles-copy", to: "steps") && renamed == ["hurdles-copy>steps"] && !store.exists("hurdles-copy") && store.exists("steps") && store.history(of: "steps")?.current == "c" && store.history(of: "hurdles-copy") == nil, "rename: the file and its history go with it")
    check(store.verdict(store.entry("steps")!)?.cleared == false && book.verdicts[ChunkStore.verdictSlot("hurdles-copy")] == nil, "...and the bot's word")
    check(!store.rename("steps", to: "hurdles") && !store.rename("steps", to: "Bad Name") && !store.rename("steps", to: "steps") && !store.rename("nothing", to: "x") && renamed.count == 1, "...a name taken, one that isn't a name, the same, a chunk that isn't there: refused, no rename told")
    renameSink.cancel()
    store.delete("steps")
    check(!store.exists("steps") && store.history(of: "steps") == nil && book.verdicts[ChunkStore.verdictSlot("steps")] == nil && store.entry("steps") == nil, "delete: the file, its history and the bot's word gone")
    // reopening reads the same
    let again = ChunkStore(root: root, verdicts: book)
    check(again.chunks.map(\.id) == store.chunks.map(\.id) && again.entry("hurdles")?.header == store.entry("hurdles")?.header, "a store opened again: the same chunks, in the same order")

    // ---- what a roll is given
    store.save("well", text: "chunk maze\nranges 31-40\nweight 3\nuses 1\nmirror yes\n---\n##.##\n#...#\n")
    store.save("bad", text: Chunks.text(ChunkHeader(), body: Chunks.parse("hurdles", store.text(of: "hurdles") ?? "").body ?? "") + "saws\n3\n")
    let roll = store.rollBundle()
    check(roll.text.contains("=== hurdles\n; The hurdles.\nchunk course\n") && roll.text.contains("=== hurdles-2\n") && !roll.text.contains("=== well") && !roll.text.contains("=== bad"), "rollBundle: every course chunk that reads, each under '=== id'; not a maze chunk (not yet), not one that breaks the rules")
    check(roll.skipped == ["bad"], "...the one it left out is named")
    check(Chunks.sorted([:]).isEmpty, "(sorted of none: none)")
    store.delete("well")
    store.delete("bad")

    // ---- the master backup
    let now = Date(timeIntervalSince1970: 1_792_843_200)
    let master = store.makeBackup(now: now)
    check(master.chunks.map(\.id) == store.chunks.map(\.id) && master.chunks.allSatisfy { $0.text == store.text(of: $0.id) } && master.made == now && master.format == ChunkBackup.formatName,
          "makeBackup: every chunk's id and whole file, in the list's order")
    check(store.plan(for: master).isEmpty && store.plan(for: master).unchanged.count == master.chunks.count, "...restoring it as it is: nothing to do")
    // change the store, then restore the master
    let hurdlesBefore = store.text(of: "hurdles") ?? ""
    var other = store.entry("hurdles")!.header
    other.weight = 1
    other.notes = ["Changed after the backup."]
    store.save("hurdles", text: Chunks.text(other, body: Chunks.parse("hurdles", hurdlesBefore).body ?? ""))
    store.delete("new-chunk-2")
    store.save("extra", text: Chunks.blank("extra", first: 2))
    let plan = store.plan(for: master)
    check(plan.changed == ["hurdles"] && plan.new == ["new-chunk-2"] && plan.kept == ["extra"] && plan.unchanged.count == master.chunks.count - 2, "a restore's plan: the edited chunk changed, the deleted one new, the one made since kept")
    var changes: [String] = []
    let sink = store.changes.sink { changes.append($0.id + ($0.text == nil ? "-" : "+")) }
    let rec = store.restore(master, now: now)
    check(rec.replaced.keys.sorted() == ["hurdles"] && rec.added == ["new-chunk-2"] && rec.skipped == 0 && store.lastRestore == rec, "restore: the new chunk added, the changed one replaced; it keeps a record")
    check(store.text(of: "hurdles") == hurdlesBefore && store.exists("new-chunk-2") && store.exists("extra"), "...the backup's files in place, the chunk made since still there")
    check(changes.sorted() == ["hurdles+", "new-chunk-2+"], "...the open editors told which files changed")
    check(rec.replaced["hurdles"]?.contains("Changed after the backup.") == true, "...the old text of the one it replaced is in the record")
    if let u = rec.copy, let data = try? Data(contentsOf: u), let copy = try? ChunkBackup.decode(data) {
        check(u.pathExtension == "cuckoochunks" && u.lastPathComponent.hasPrefix("Cuckoo Chunks before restore ") && u.deletingLastPathComponent().lastPathComponent == "chunks_before_restore" && copy.chunks.map(\.id) == ["hurdles"] && copy.chunks[0].text.contains("Changed after the backup."),
              "...the replaced chunk kept in a 'before restore' copy, itself a chunks backup")
    } else { check(false, "...the replaced chunk kept in a 'before restore' copy") }
    check(rec.summary == "Restored 1 new, 1 changed", "...in words")
    changes.removeAll()
    store.undoRestore()
    check(store.lastRestore == nil && (store.text(of: "hurdles") ?? "").contains("Changed after the backup.") && !store.exists("new-chunk-2") && store.exists("extra"), "undo the restore: the replaced chunk as it was, the added one gone, the rest left")
    check(changes.sorted() == ["hurdles+", "new-chunk-2-"], "...the editors told")
    store.undoRestore()
    check(store.lastRestore == nil, "(nothing to undo twice)")
    _ = store.restore(master)
    store.dismissRestore()
    check(store.lastRestore == nil && store.text(of: "hurdles") == hurdlesBefore, "dismissed: the banner goes, the restore stays")
    // a chunk in the file that doesn't read is skipped
    var withBad = master
    withBad.chunks.append(.init(id: "junk", text: "nonsense"))
    let rec2 = store.restore(withBad)
    check(rec2.skipped == 1 && !store.exists("junk") && rec2.summary.contains("1 in the file didn't read"), "a restore with a chunk that doesn't read: it's skipped, and the summary says so")
    sink.cancel()

    // ---- the automatic backup
    let n = store.restore(texts: ["steps": Chunks.blank("steps", first: 4), "Bad Name": Chunks.blank("x", first: 1), "junk": "nonsense"],
                          histories: ["steps": HistoryFile(current: "c", undo: [], redo: [])])
    check(n == 1 && store.exists("steps") && !store.exists("junk") && store.history(of: "steps")?.current == "c", "the automatic backup's chunks: each that reads put back (and its history); a bad name or a file that doesn't read, not")
    check(store.restore(texts: [:], histories: nil) == 0, "...none in it (a backup from before chunks): nothing")
}

/// the chunk calls of EdDoc (Core.swift) and the JSON they bring back (Models.swift, ChunkCore.swift)
@MainActor
func core() {
    let well = "length 26\ndifficulty 1085 0.35\nraids auto\ncourse\n13 0 crate\n8 3 hay\n10 1 crate\n13 1 crate\nsaws\n9.6\ndiscos\nlasers\n12.6 1 1\nlava\n14.4 3\n"
    let blank = Chunks.parse("hurdles", Chunks.blank("hurdles", first: 20)).body ?? ""
    var why = ""
    guard let doc = EdDoc(number: 20, text: blank, error: &why) else { check(false, "a chunk's body opens as a level: \(why)"); return }
    doc.setChunk(1)
    let info = doc.chunkInfo
    check(info?.canMirror == true && info?.problem == "" && info?.runIn == 13 && info?.runOut == 13 && abs((info?.reach ?? 0) - 21.6) < 0.01 && info?.why == "", "EdDoc.setChunk, chunkInfo: it can be flipped, no problem, its pads 13 cells each, its reach")
    check(doc.meta?.length == 21.6 && doc.setMeta("length", "5") && doc.meta?.length == 12, "a chunk's finish line: never under 12 m (a level's: 30)")
    let text = doc.text
    doc.setTool("saw")
    doc.pointer(.begin, x: 3.0, y: 0.3); doc.pointer(.end, x: 3.0, y: 0.3)
    check(doc.text == text && doc.lastError.contains("run-in"), "a saw dropped into the run-in: turned away, why in lastError")
    doc.pointer(.begin, x: 9.6, y: 0.3); doc.pointer(.end, x: 9.6, y: 0.3)
    check(doc.text.contains("saws\n9.6") || doc.text.contains("saws\n9.5") || doc.text.contains("saws\n9.7"), "...between the pads: placed")
    check(doc.scene?.zone == "egypt" && doc.setNumber(25) && doc.scene?.zone == "japan" && !doc.setNumber(25) && doc.number == 25, "setNumber: the body judged as another level (20: Egypt, then 25: Japan); the same number: nothing")
    check(doc.chunksUsed.isEmpty, "a body has no roll's chunks")
    // flipped
    guard let w = EdDoc(number: 31, text: well, error: &why) else { check(false, "the well opens: \(why)"); return }
    w.setChunk(1)
    check(!(w.scene?.notes ?? []).contains { $0.text.hasPrefix("Flipped:") }, "Mirror off: no note says flipped")
    w.setChunk(2)
    check((w.scene?.notes ?? []).contains { $0.text.hasPrefix("Flipped:") && $0.kind == "note" }, "Mirror on: the checker's notes say what is unfair flipped")
    // a roll with chunks
    let bundle = "=== hurdles\n" + Chunks.text(ChunkHeader(ranges: [LevelRange(31, 40)], weight: 10, uses: 0), body: Chunks.parse("hurdles", Chunks.blank("hurdles", first: 31)).body ?? "")
    guard let level = EdDoc(number: 33, text: Builtin.bake(33), error: &why) else { check(false, "level 33 opens: \(why)"); return }
    var seed: UInt32 = 1
    while seed < 12 && !(level.regenerate(seed: seed, hazards: ["saw", "laser", "lava"], density: 0, chunks: bundle) && !level.chunksUsed.isEmpty) { seed += 1 }
    let used = level.chunksUsed
    check(used.first?.id == "hurdles" && used.allSatisfy { $0.x1 > $0.x0 } && level.history?.undo.last?.label.contains("chunks: hurdles") == true,
          "EdDoc.regenerate(chunks:): a roll places the chunk, chunksUsed says where, its history step names it")
    check(level.regenerate(seed: 99, hazards: ["saw"], density: 0, chunks: "=== broken\nnonsense\n") == false && !level.lastError.isEmpty, "...a chunk that doesn't read stops the roll, why in lastError")
    level.setTool("wall")
    level.pointer(.begin, x: 2.1, y: 1.8); level.pointer(.end, x: 2.1, y: 1.8)
    check(level.chunksUsed.isEmpty, "...none once the course has been edited since")
    // what the bot says
    let report = decodeJSON(BotReport.self, """
        {"ok":true,"level":20,"game":{"cleared":true,"seconds":9.5,"x":21.6,"y":0,"perfects":1,"closeCalls":0,"trace":[[0,0,0]],"events":[]},
         "flipped":{"ok":true,"game":{"cleared":false,"seconds":4,"x":7.5,"y":0,"perfects":0,"closeCalls":0,"trace":[],"events":[]}}}
        """)
    check(report?.game?.cleared == true && report?.flipped?.game?.cleared == false && report?.flipped?.game?.x == 7.5, "a bot report with the chunk flipped inside it decodes: both plays")
    check(decodeJSON(BotReport.self, "{\"ok\":true,\"level\":1}")?.flipped == nil, "...one without it: no flipped")
    check(decodeJSON([ChunkUse].self, "[{\"id\":\"a\",\"x0\":1.5,\"x1\":9,\"mirrored\":true}]") == [ChunkUse(id: "a", x0: 1.5, x1: 9, mirrored: true)], "where a roll placed chunks decodes")
}

/// the levels' store keeping the chunks' bot words (LevelStore, ChunkStoreApp) and the automatic backup (BackupModel)
@MainActor
func backups() {
    let root = scratch()
    defer { try? FileManager.default.removeItem(at: root) }
    let levels = LevelStore(root: root)
    let chunks = ChunkStore(root: root, verdicts: levels)
    check(levels.levels.count >= 30 && chunks.chunks.isEmpty, "the levels' store seeds the game's levels; the chunks start with none")
    chunks.newCourse(named: "hurdles", first: 4)
    chunks.saveHistory("hurdles", HistoryFile(current: "c", undo: [.init(label: "A step", time: 1, at: 2, text: "t")], redo: []))
    chunks.setVerdict("hurdles", cleared: true, seconds: 3, key: chunks.entry("hurdles")?.verdictKey ?? "")
    check(levels.state.verdicts["chunk:hurdles"]?.cleared == true && levels.verdict(1) == nil, "a chunk's bot word is kept in the levels' sync state, under 'chunk:<id>' (the levels' own are unmoved)")
    let reopened = LevelStore(root: root)
    let chunksAgain = ChunkStore(root: root, verdicts: reopened)
    check(reopened.state.verdicts["chunk:hurdles"]?.cleared == true && chunksAgain.entry("hurdles").flatMap { chunksAgain.verdict($0) }?.cleared == true, "...saved with the state: still there when the app is opened again")

    // the automatic backup holds the chunks
    let b = levels.makeBackup(chunks: chunks)
    check(b.chunks == ["hurdles": chunks.text(of: "hurdles") ?? ""] && b.chunkHistory?["hurdles"]?.current == "c" && b.levels.count == levels.levels.count, "makeBackup: every chunk's file and history beside the levels and mazes")
    guard let data = try? b.encoded(), let back = try? EditorBackup.decode(data) else { check(false, "a backup written and read back"); return }
    check(back.chunks == b.chunks && back.chunkHistory?["hurdles"]?.undo.first?.label == "A step" && back.format == "cuckoo-editor-backup", "...written and read back, chunks and their histories with it")
    // a backup from before chunks: no such keys
    guard var json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { check(false, "the backup is JSON"); return }
    json["chunks"] = nil
    json["chunkHistory"] = nil
    let old = try? JSONSerialization.data(withJSONObject: json)
    let oldBackup = old.flatMap { try? EditorBackup.decode($0) }
    check(oldBackup != nil && oldBackup?.chunks == nil && oldBackup?.chunkHistory == nil, "a backup from before chunks (no chunks key) reads as it did")
    // restore into a fresh device
    let root2 = scratch()
    defer { try? FileManager.default.removeItem(at: root2) }
    let levels2 = LevelStore(root: root2)
    let chunks2 = ChunkStore(root: root2, verdicts: levels2)
    let n = levels2.restore(back, chunks: chunks2)
    check(n.chunks == 1 && chunks2.exists("hurdles") && chunks2.text(of: "hurdles") == chunks.text(of: "hurdles") && chunks2.history(of: "hurdles")?.current == "c" && n.levels == levels.levels.count,
          "restore: the chunks put back with their histories, the levels as before")
    if let oldBackup {
        chunks2.save("mine", text: Chunks.blank("mine", first: 2))
        let m = levels2.restore(oldBackup, chunks: chunks2)
        check(m.chunks == 0 && chunks2.exists("mine") && chunks2.exists("hurdles"), "a backup with no chunks restored: the chunks here are left alone")
    }
    check(!EditorBackup.deviceName.isEmpty && back.fileName.hasPrefix("Cuckoo Editor backup "), "(the backup's device name and file name)")
}

/// a chunk open in the editor (ChunkModel): its title, its file saved with its header and a body
@MainActor
func session() {
    let root = scratch()
    defer { try? FileManager.default.removeItem(at: root) }
    let store = ChunkStore(root: root, verdicts: MemoryVerdicts())
    let id = store.newCourse(named: "steps", first: 6)
    var header = store.entry(id)?.header ?? ChunkHeader()
    let s = ChunkSession(id: id, header: header)
    check(s.title == "Steps", "a chunk open: titled by its name until it has a description")
    header.notes = ["Three steps up.", "More."]
    header.weight = 8
    header.mirror = true
    s.header = header
    check(s.title == "Three steps up.", "...its first description line once it has one")
    let body = Chunks.parse(id, store.text(of: id) ?? "").body ?? ""
    s.save(body: body, to: store)
    let f = Chunks.parse(id, store.text(of: id) ?? "")
    check(f.reads && f.header == header && f.body == body && store.entry(id)?.title == "Three steps up.", "save: the chunk's file is its header and the body, in the list at once")
    s.id = "renamed"
    s.save(body: body, to: store)
    check(store.exists("renamed") && store.exists(id), "(a session saves under the id it has: the store renames the file first)")
    check(CanvasBand(x0: 0, x1: 1, label: "A", hex: "#fff", stripped: true) != CanvasBand(x0: 0, x1: 1, label: "A", hex: "#fff", stripped: false), "(a band is told from another by whether it's a pad)")
}

// ---- sync

/// a chunk file's text that reads, with a weight to tell versions apart
func chunkFile(_ id: String, weight: Int = 3, first: Int = 20) -> String {
    Chunks.blank(id, first: first).replacingOccurrences(of: "weight 3", with: "weight \(weight)")
}

/// the Mac, pretended: its files, and the manifest it would send
struct PretendMac {
    var files: [String: String] = [:]
    var manifest: [String: String] { files.mapValues(textHash) }
}

/// one sync's chunk half against a pretend Mac, as SyncEngine.syncChunks and its push do it; the clashes it found
@MainActor
func syncOnce(_ store: ChunkStore, _ mac: inout PretendMac) -> [ChunkClash] {
    let plan = ChunkSyncPlan.make(mine: store.hashes(), entries: store.syncEntries, theirs: mac.manifest)
    var texts: [String: String] = [:]
    for id in plan.wanted { texts[id] = mac.files[id] }
    let step = store.syncStep(plan, theirs: mac.manifest, texts: texts)
    var applied: [String] = []
    for (id, item) in step.push.push {
        guard let p = item as? [String: String] else { continue }
        if (mac.files[id].map(textHash) ?? "") == p["base"] { mac.files[id] = p["text"]; applied.append(id) } // (compare-and-swap)
    }
    for (id, base) in step.push.deletes where (mac.files[id].map(textHash) ?? "") == base { mac.files[id] = nil; applied.append(id) }
    store.pushed(step.push, applied: applied)
    return step.clashes
}

@MainActor
func sync() {
    // ---- the sync state read from files
    let old = """
        {"levels":{"1":{"base":"abc","deleted":false}},"grids":{},"verdicts":{},"lastSync":null}
        """.data(using: .utf8)!
    let oldState = try? JSONDecoder().decode(SyncState.self, from: old)
    check(oldState != nil && oldState?.chunks == nil && oldState?.chunksSeeded == nil && oldState?.levels["1"]?.base == "abc", "a sync state from before chunks (no chunks key) reads as it did: no chunks yet")
    var state = SyncState()
    state.chunks = ["hurdles": .init(base: "h1", deleted: true)]
    state.chunksSeeded = true
    let data = try? JSONEncoder().encode(state)
    let back = data.flatMap { try? JSONDecoder().decode(SyncState.self, from: $0) }
    check(back?.chunks?["hurdles"] == .init(base: "h1", deleted: true) && back?.chunksSeeded == true, "...a new one writes its chunks and reads them back")

    // ---- the conflicts the sheet lists, and the words for what the Mac turned away
    let level = SyncConflict(number: 4, kind: .both(["x"]), mine: "a", theirs: "b", theirsHash: "h")
    let grid = SyncConflict(number: 0, kind: .grid("tomb"), mine: "a", theirs: "b", theirsHash: "h")
    let chunkClash = SyncConflict(number: 0, kind: .chunk("sacred-wells"), mine: "a", theirs: "b", theirsHash: "h")
    check(level.id == "level-4" && grid.id == "grid-tomb" && chunkClash.id == "chunk-sacred-wells" && Set([level.id, grid.id, chunkClash.id]).count == 3, "a conflict's id: a level's, a maze's and a chunk's are told apart")
    check(chunkClash.chunkId == "sacred-wells" && chunkClash.gridName == nil && grid.gridName == "tomb" && grid.chunkId == nil && level.chunkId == nil && level.gridName == nil, "...and which kind it is")
    check(SyncConflict.rejection("chunk sacred-wells", "no run-out") == "Chunk sacred wells: no run-out" && SyncConflict.rejection("12", "bad") == "Level 12: bad" && SyncConflict.rejection("grid tomb", "x") == "The tomb: x",
          "what the Mac turned away: a chunk by its name, a maze by its name, a level by its number")

    // ---- the plan for each chunk (mine: hashes of the files here; entries: what was agreed; theirs: the Mac's)
    typealias Plan = ChunkSyncPlan
    func entry(_ base: String, deleted: Bool = false) -> SyncState.Entry { .init(base: base, deleted: deleted) }
    check(Plan.make(mine: ["a": "1"], entries: ["a": entry("1")], theirs: ["a": "1"]).isEmpty, "plan: nothing changed on either side: nothing to do")
    check(Plan.make(mine: [:], entries: [:], theirs: [:]).isEmpty, "...no chunks at all: nothing")
    check(Plan.make(mine: ["a": "1"], entries: ["a": entry("1")], theirs: ["a": "2"]) == Plan(pull: ["a"]), "changed on the Mac only: pulled")
    check(Plan.make(mine: [:], entries: [:], theirs: ["a": "2"]) == Plan(pull: ["a"]), "...a chunk only the Mac has (a fresh device): pulled")
    check(Plan.make(mine: ["a": "2"], entries: ["a": entry("1")], theirs: ["a": "1"]) == Plan(push: ["a"]), "changed here only: pushed")
    check(Plan.make(mine: ["a": "2"], entries: ["a": entry("")], theirs: [:]) == Plan(push: ["a"]), "...a new chunk here: pushed")
    check(Plan.make(mine: ["a": "2"], entries: ["a": entry("1")], theirs: ["a": "3"]) == Plan(both: ["a"]), "changed on both: a choice")
    check(Plan.make(mine: ["a": "2"], entries: ["a": entry("")], theirs: ["a": "3"]) == Plan(both: ["a"]), "...made on both under one name: a choice too")
    check(Plan.make(mine: [:], entries: ["a": entry("1", deleted: true)], theirs: ["a": "1"]) == Plan(deletes: ["a"]), "taken out here, unchanged on the Mac: the Mac is told")
    check(Plan.make(mine: [:], entries: ["a": entry("1", deleted: true)], theirs: ["a": "2"]) == Plan(pull: ["a"]), "...changed on the Mac meanwhile: the change wins, it comes back")
    check(Plan.make(mine: [:], entries: ["a": entry("1", deleted: true)], theirs: [:]) == Plan(forget: ["a"]), "...taken out on both: forgotten")
    check(Plan.make(mine: ["a": "1"], entries: ["a": entry("1")], theirs: [:]) == Plan(forget: ["a"]), "gone from the Mac, unchanged here: gone here too")
    check(Plan.make(mine: ["a": "2"], entries: ["a": entry("1")], theirs: [:]) == Plan(push: ["a"]), "...changed here: the change wins, pushed as new")
    check(Plan.make(mine: [:], entries: ["a": entry("1")], theirs: ["a": "1"]).isEmpty, "a file gone here without being taken out, the Mac unchanged: no change (it isn't a delete)")
    let mixed = Plan.make(mine: ["b": "2", "c": "1", "d": "9"], entries: ["b": entry("1"), "c": entry("1"), "e": entry("1", deleted: true)], theirs: ["a": "5", "c": "1", "d": "8", "e": "1"])
    check(mixed == Plan(pull: ["a"], push: ["b"], deletes: ["e"], both: [], forget: []) || mixed == Plan(pull: ["a"], push: ["b"], deletes: ["e"], both: ["d"], forget: []),
          "several at once: each its own way, in order")
    check(mixed.both == ["d"] && mixed.wanted == ["a", "d"], "...(d, new on both, a choice; the ones to read from the Mac are the pulls and the choices)")

    // ---- the store against a pretend Mac
    let root = scratch()
    defer { try? FileManager.default.removeItem(at: root) }
    let levels = LevelStore(root: root)
    let store = ChunkStore(root: root, verdicts: levels, builtin: [:])
    var mac = PretendMac()
    check(store.chunks.isEmpty && levels.state.chunksSeeded == true, "a fresh device with no built-in chunks: none, and it won't seed later")
    // new here: pushed, then agreed
    store.save("hurdles", text: chunkFile("hurdles"))
    check(levels.state.chunks?["hurdles"]?.base == "" && store.entry("hurdles")?.dirty == true && store.dirty("hurdles"), "a chunk made here: known to sync, not yet agreed (dirty)")
    var clashes = syncOnce(store, &mac)
    check(clashes.isEmpty && mac.files["hurdles"] == chunkFile("hurdles"), "sync: it's pushed to the Mac")
    check(levels.state.chunks?["hurdles"]?.base == textHash(chunkFile("hurdles")) && store.baseText(of: "hurdles") == chunkFile("hurdles") && store.entry("hurdles")?.dirty == false && !store.dirty("hurdles"),
          "...agreed: the base is its hash and its text, no longer dirty")
    check(!(syncOnce(store, &mac).isEmpty == false) && mac.files.count == 1, "a second sync with nothing changed does nothing")
    // changed on the Mac only: pulled
    mac.files["hurdles"] = chunkFile("hurdles", weight: 8)
    clashes = syncOnce(store, &mac)
    check(clashes.isEmpty && store.text(of: "hurdles") == chunkFile("hurdles", weight: 8) && store.entry("hurdles")?.header.weight == 8 && !store.dirty("hurdles"), "changed on the Mac: pulled, and the list shows it")
    // changed here only: pushed
    store.save("hurdles", text: chunkFile("hurdles", weight: 5))
    check(store.entry("hurdles")?.dirty == true, "an edit here: dirty")
    clashes = syncOnce(store, &mac)
    check(clashes.isEmpty && mac.files["hurdles"] == chunkFile("hurdles", weight: 5) && !store.dirty("hurdles"), "...pushed")
    // changed on both: a clash, nothing overwritten; then each choice
    store.save("hurdles", text: chunkFile("hurdles", weight: 2))
    mac.files["hurdles"] = chunkFile("hurdles", weight: 9)
    clashes = syncOnce(store, &mac)
    check(clashes == [ChunkClash(id: "hurdles", mine: chunkFile("hurdles", weight: 2), theirs: chunkFile("hurdles", weight: 9))], "changed on both: a clash with both texts")
    check(store.text(of: "hurdles") == chunkFile("hurdles", weight: 2) && mac.files["hurdles"] == chunkFile("hurdles", weight: 9) && store.dirty("hurdles"), "...neither is overwritten, mine stays dirty")
    store.agreed("hurdles", text: chunkFile("hurdles", weight: 9))
    check(store.text(of: "hurdles") == chunkFile("hurdles", weight: 9) && !store.dirty("hurdles"), "'Take the Mac's': agreed, my file replaced")
    store.save("hurdles", text: chunkFile("hurdles", weight: 1))
    mac.files["hurdles"] = chunkFile("hurdles", weight: 7)
    let c = syncOnce(store, &mac)
    let copy = store.keepMineAsCopy("hurdles", mine: c[0].mine, theirs: c[0].theirs)
    check(copy == "hurdles-copy" && store.text(of: copy) == chunkFile("hurdles", weight: 1) && store.text(of: "hurdles") == chunkFile("hurdles", weight: 7) && store.dirty(copy) && !store.dirty("hurdles"),
          "'Keep mine as a copy': mine becomes a new chunk (dirty: it will go to the Mac), the Mac's agreed under the name")
    clashes = syncOnce(store, &mac)
    check(clashes.isEmpty && mac.files[copy] == chunkFile("hurdles", weight: 1), "...the copy goes to the Mac at the next sync")
    // the same edit on both sides agrees
    store.save("hurdles", text: chunkFile("hurdles", weight: 4))
    mac.files["hurdles"] = chunkFile("hurdles", weight: 4)
    check(syncOnce(store, &mac).isEmpty && !store.dirty("hurdles"), "the same edit made on both sides: agreed, no clash")
    // a push the Mac turns down (it moved on between the manifest and the push) is not marked agreed
    store.save("hurdles", text: chunkFile("hurdles", weight: 6))
    let plan = ChunkSyncPlan.make(mine: store.hashes(), entries: store.syncEntries, theirs: mac.manifest)
    let step = store.syncStep(plan, theirs: mac.manifest, texts: [:])
    store.pushed(step.push, applied: [])
    check(store.dirty("hurdles") && step.push.push["hurdles"] != nil, "a push the Mac didn't apply: the chunk stays dirty, to try again")
    // an edit made while the push was in flight is never overwritten
    store.save("hurdles", text: chunkFile("hurdles", weight: 3))
    store.pushed(step.push, applied: ["hurdles"])
    check(store.text(of: "hurdles") == chunkFile("hurdles", weight: 3) && store.syncEntries["hurdles"]?.base == textHash(chunkFile("hurdles", weight: 6)) && store.dirty("hurdles"),
          "an edit made while the push was in flight: kept (the base moves on, the edit syncs next time)")
    // taken out here: held, then told; one the Mac dropped is dropped here
    mac.files["hurdles"] = chunkFile("hurdles", weight: 6) // (the push did land on the Mac)
    _ = syncOnce(store, &mac)
    store.delete("hurdles")
    check(store.syncEntries["hurdles"]?.deleted == true && store.text(of: "hurdles") == nil && !store.ids.contains("hurdles"), "a chunk taken out here: held as deleted till the Mac is told")
    check(syncOnce(store, &mac).isEmpty && mac.files["hurdles"] == nil && store.syncEntries["hurdles"] == nil && store.baseText(of: "hurdles") == nil, "...sync takes it out of the Mac too, and forgets it")
    mac.files[copy] = nil
    check(syncOnce(store, &mac).isEmpty && store.text(of: copy) == nil && store.syncEntries[copy] == nil, "a chunk the Mac took out (unchanged here): gone from here at the next sync")
    // one never synced, taken out: just gone, nothing to tell
    store.save("scratch", text: chunkFile("scratch"))
    store.delete("scratch")
    check(store.syncEntries["scratch"] == nil && syncOnce(store, &mac).isEmpty && mac.files["scratch"] == nil, "a chunk made and taken out before any sync: nothing is ever sent")
    // a rename is, to the Mac, one taken out and one new
    store.save("old", text: chunkFile("old"))
    _ = syncOnce(store, &mac)
    check(store.rename("old", to: "fresh") && store.syncEntries["old"]?.deleted == true && store.syncEntries["fresh"]?.base == "", "a chunk renamed here: the old name held as taken out, the new one new")
    _ = syncOnce(store, &mac)
    check(mac.files["old"] == nil && mac.files["fresh"] == chunkFile("old") && store.syncEntries["old"] == nil && store.syncEntries["fresh"]?.base == textHash(chunkFile("old")),
          "...sync sends the new and takes out the old")
    // a taken-out chunk the Mac changed meanwhile comes back
    store.delete("fresh")
    mac.files["fresh"] = chunkFile("old", weight: 9)
    _ = syncOnce(store, &mac)
    check(store.text(of: "fresh") == chunkFile("old", weight: 9) && store.syncEntries["fresh"]?.deleted == false && mac.files["fresh"] != nil, "taken out here but changed on the Mac: it comes back")
    // a chunk made again under the name of one taken out (before the Mac was told) is an edit, not a delete
    store.delete("fresh")
    store.save("fresh", text: chunkFile("old", weight: 2))
    check(store.syncEntries["fresh"]?.deleted == false && store.syncEntries["fresh"]?.base == textHash(chunkFile("old", weight: 9)), "a chunk taken out and made again under its name: an edit of what the Mac has")
    check(syncOnce(store, &mac).isEmpty && mac.files["fresh"] == chunkFile("old", weight: 2), "...pushed over it")
    // restores go through sync too
    let backup = ChunkBackup(made: Date(), app: "t", chunks: [.init(id: "restored", text: chunkFile("restored"))])
    store.restore(backup)
    check(store.syncEntries["restored"]?.base == "" && store.dirty("restored"), "a chunk restored from a backup: new to sync")
    store.undoRestore()
    check(store.syncEntries["restored"] == nil, "...and the undo takes it out again: nothing for the Mac to hear of")

    // ---- the game's own chunks on a fresh device
    let seedRoot = scratch()
    defer { try? FileManager.default.removeItem(at: seedRoot) }
    let seedLevels = LevelStore(root: seedRoot)
    let built = ["arch": chunkFile("arch", weight: 6, first: 31), "wells": chunkFile("wells", first: 2), "Bad Name": "x"]
    let seeded = ChunkStore(root: seedRoot, verdicts: seedLevels, builtin: built)
    check(seeded.ids == ["arch", "wells"] && seeded.text(of: "arch") == built["arch"] && seedLevels.state.chunksSeeded == true, "a fresh device is seeded with the game's chunks (a name that isn't a chunk's, left out)")
    check(seeded.syncEntries["arch"]?.base == textHash(built["arch"] ?? "") && seeded.baseText(of: "arch") == built["arch"] && !seeded.dirty("arch") && seeded.chunks.allSatisfy { !$0.dirty }, "...as what the Mac has: agreed, none dirty")
    var seedMac = PretendMac(files: ["arch": built["arch"] ?? "", "wells": built["wells"] ?? ""])
    check(syncOnce(seeded, &seedMac).isEmpty && seedMac.files.count == 2 && seeded.ids == ["arch", "wells"], "...so the first sync has nothing to do")
    seedMac.files["arch"] = chunkFile("arch", weight: 9, first: 31)
    _ = syncOnce(seeded, &seedMac)
    check(seeded.text(of: "arch") == chunkFile("arch", weight: 9, first: 31), "...and takes the repo's newer one when it differs")
    // not again: a chunk taken out stays out, however empty the folder gets
    seeded.delete("wells")
    _ = syncOnce(seeded, &seedMac)
    seeded.delete("arch")
    _ = syncOnce(seeded, &seedMac)
    let again = ChunkStore(root: seedRoot, verdicts: seedLevels, builtin: built)
    check(again.chunks.isEmpty && seedMac.files.isEmpty, "seeded once only: chunks taken out and synced away don't come back at the next launch")
    // not on a device that has chunks, or has synced them
    let busyRoot = scratch()
    defer { try? FileManager.default.removeItem(at: busyRoot) }
    let busyLevels = LevelStore(root: busyRoot)
    let busy = ChunkStore(root: busyRoot, verdicts: busyLevels, builtin: [:])
    busy.save("mine", text: chunkFile("mine"))
    busyLevels.state.chunksSeeded = nil
    let busyAgain = ChunkStore(root: busyRoot, verdicts: busyLevels, builtin: built)
    check(busyAgain.ids == ["mine"], "a device that already has chunks isn't seeded over them")
    let syncedRoot = scratch()
    defer { try? FileManager.default.removeItem(at: syncedRoot) }
    let syncedLevels = LevelStore(root: syncedRoot)
    syncedLevels.state.chunks = ["gone": .init(base: "x")]
    let synced = ChunkStore(root: syncedRoot, verdicts: syncedLevels, builtin: built)
    check(synced.chunks.isEmpty, "...nor one whose sync state already knows chunks (they were synced away)")
}

// ---- maze chunks (stage 4): the store's side, the maze editor's storage and checklist, and the core's calls under them
@MainActor
func mazeChunks() {
    let blank = Chunks.blankMaze("well", first: 20)
    let f = Chunks.parse("well", blank)
    check(f.reads && f.isMaze && f.first == 20 && f.canMirror == true && f.body == ".........\n.........\n.........\n.........\n####S####\n", "Chunks.blankMaze: a new maze chunk for level 20 that reads: a 9 x 5 block, a saw in its floor")
    check(f.header == ChunkHeader(maze: true, notes: [], ranges: [LevelRange(20, 20)], weight: 3, uses: 0, mirror: false), "...its settings: level 20 only, weight 3, uses any, no mirror")
    let spring = Chunks.parse("well", Chunks.text(f.header, body: "...\n.^.\n###\n"))
    check(spring.reads && spring.canMirror == false && (spring.whyNot ?? "").contains("spring"), "a maze chunk with a spring can't be flipped, and the core says why")
    check(!Chunks.parse("well", Chunks.text(f.header, body: "...\n..\n")).reads && !Chunks.parse("well", Chunks.text(f.header, body: ".>.\n###\n")).reads, "a ragged block, or one with a door: doesn't read")

    // the store
    let root = scratch()
    defer { try? FileManager.default.removeItem(at: root) }
    let book = MemoryVerdicts()
    let store = ChunkStore(root: root, verdicts: book)
    let id = store.newMaze(named: "Sacred Well", first: 20)
    check(id == "sacred-well" && store.entry(id)?.maze == true && store.entry(id)?.problem == "" && store.entry(id)?.header.first == 20 && store.entry(id)?.canMirror == true, "newMaze: a file under the name typed, a maze chunk that reads, in the list")
    check(store.newMaze(named: "sacred well") == "sacred-well-2" && store.newMaze(named: " ") == "new-maze-chunk", "...a name taken: numbered; none: 'new-maze-chunk'")
    let course = store.newCourse(named: "hurdles", first: 20)
    let mazes = store.mazeBundle(), courses = store.rollBundle()
    check(mazes.text.contains("=== sacred-well\n") && mazes.text.contains("chunk maze\n") && !mazes.text.contains("=== hurdles") && mazes.skipped.isEmpty, "mazeBundle: every maze chunk that reads, each under '=== id'; no course chunk")
    check(courses.text.contains("=== hurdles\n") && !courses.text.contains("=== sacred-well") && course == "hurdles", "...and rollBundle has the course chunks only")
    store.save("ragged", text: Chunks.text(f.header, body: "...\n..\n"))
    check(store.mazeBundle().skipped == ["ragged"] && !store.mazeBundle().text.contains("=== ragged"), "a maze chunk that doesn't read: left out of the bundle, and named")
    store.delete("ragged")
    // its history (a block of cells, not a course) in the same file as a course chunk's
    check(store.gridHistory(of: id) == nil, "a maze chunk's history: none yet")
    store.saveGridHistory(id, GridHistoryFile(current: "c\n", undo: [.init(label: "Saw at col 4, row 0", time: 1, c: 4, i: 4, text: "t\n")], redo: []))
    check(store.gridHistory(of: id)?.undo.first?.label == "Saw at col 4, row 0" && store.gridHistory(of: id)?.undo.first?.c == 4, "...kept and read back")
    let moved = store.duplicate(id)
    check(moved == "sacred-well-copy" && store.gridHistory(of: "sacred-well-copy") == nil && store.text(of: "sacred-well-copy") == store.text(of: id), "a copy: the file (a history of a block can't be read as a course's: none carried)")
    check(store.rename("sacred-well-copy", to: "deep-well") && store.exists("deep-well"), "(rename works for a maze chunk)")
    store.saveGridHistory("deep-well", GridHistoryFile(current: "c\n", undo: [], redo: []))
    check(store.rename("deep-well", to: "wide-well") && store.gridHistory(of: "wide-well")?.current == "c\n" && store.gridHistory(of: "deep-well") == nil, "...and a history goes with it")

    // the maze editor's storage over the chunk
    let session = ChunkSession(id: id, header: store.entry(id)!.header)
    let storage = ChunkMazeStorage(session: session, chunks: store)
    check(storage.isChunk && storage.load(id) == f.body && storage.load("nothing") == nil && storage.otherGrids(except: id).isEmpty, "ChunkMazeStorage: loads the chunk's rows, none for a chunk that isn't there, no other grids")
    storage.save(id, text: "...\n.S.\n###\n")
    check(storage.load(id) == "...\n.S.\n###\n" && Chunks.parse(id, store.text(of: id) ?? "").header == session.header && storage.dirty(id), "save: the rows in the chunk's file under its header; not synced since")
    session.header.weight = 9
    storage.save(id, text: "...\n.S.\n###\n")
    check(store.entry(id)?.header.weight == 9, "...the header is the session's, as it is now")
    check(storage.history(of: id)?.current == "c\n" && storage.history(of: "nothing") == nil, "history: the chunk's, none for one that isn't there")
    storage.saveHistory(id, GridHistoryFile(current: "x", undo: [], redo: []))
    storage.saveHistory("gone", GridHistoryFile(current: "x", undo: [], redo: []))
    check(storage.history(of: id)?.current == "x" && store.gridHistory(of: "gone") == nil, "...kept for a chunk that is there, not for one that isn't")
    check(storage.typed("Deep Well!") == "deep-well" && storage.valid("deep-well") && !storage.valid("Deep Well") && storage.taken(id) && !storage.taken("free") && storage.shown("deep-well") == "Deep Well" && storage.nameRule.contains("lower case"), "names: typed as a file's, valid and taken as the store has them")
    check(storage.base(of: id) == nil, "base: never synced")
    var seen: [String] = []
    let sub = storage.changes.sink { seen.append($0.name + "|" + $0.text) }
    store.agreed(id, text: Chunks.text(session.header, body: "...\n.^.\n###\n"))
    check(seen == [id + "|...\n.^.\n###\n"] && storage.base(of: id) == "...\n.^.\n###\n" && !storage.dirty(id), "a change from sync reaches the editor as the rows; the base is the rows sync agreed on")
    store.changes.send((id, nil))
    check(seen.count == 1, "(a chunk taken out: nothing for the editor to take up)")
    sub.cancel()
    check(storage.rename(id, to: "pit") && session.id == "pit" && store.exists("pit") && !store.exists(id) && storage.load("pit") != nil, "rename: the file under the new name, the session follows")
    check(!storage.rename("pit", to: "hurdles"), "...a name that is taken: refused")

    // the levels' mazes are another storage
    let levels = LevelStore(root: scratch())
    let g = GauntletStorage(levels)
    check(!g.isChunk && g.typed("Hall of Saws") == "hall_of_saws" && g.valid("hall_of_saws") && !g.valid("hall of saws") && g.shown("hall_of_saws") == "hall of saws" && g.nameRule.contains("maze"), "GauntletStorage: a gauntlet's names, the editors' way")
    let room = "##########\n#........#\n..........\n>........X\n###S######\n"
    levels.saveGrid("my_room", text: room)
    check(g.load("my_room") == room && g.taken("my_room") && !g.taken("other") && g.dirty("my_room") && g.otherGrids(except: "my_room")["my_room"] == nil && g.otherGrids(except: "x")["my_room"]?.count == 5, "...loads, knows what's taken, what changed since sync, and the other grids")
    g.save("my_room", text: room.replacingOccurrences(of: "S", with: "#"))
    check(levels.gridText("my_room")?.contains("S") == false, "...saves to the levels' gauntlet files")
    check(g.rename("my_room", to: "my_hall") && levels.gridText("my_hall") != nil && levels.gridText("my_room") == nil, "...renames the file")
}

/// the core's chunk mode under the maze editor: the corridor round a block, judged in the background, windowed back onto the block;
/// then the checklist in words
@MainActor
func mazeChecker() {
    var why = ""
    check(GdDoc(name: "well", text: ".>.\n###\n", chunk: true, error: &why) == nil && !why.isEmpty, "a block with a door doesn't open in chunk mode (and why)")
    guard let doc = GdDoc(name: "well", text: ".....\n.....\n##...\n##S##\n", chunk: true, error: &why) else { check(false, "a chunk opens: \(why)"); return }
    check(doc.scene?.chunk == true && doc.scene?.cols == 5 && doc.scene?.entry == nil && doc.scene?.exit == nil, "a chunk's scene: 5 columns, a chunk, no doors")
    // judge it: the corridor, the flipped one too
    let job = GdJob()
    func judged() -> GridReport? {
        job.kick(doc, level: 20, levelText: nil)
        for _ in 0..<200 {
            if let r = job.poll() { return r.report }
            Thread.sleep(forTimeInterval: 0.05)
        }
        return nil
    }
    let plain = judged()
    check(plain?.ok == true && plain?.cols == 5 + 26 && plain?.flipped == nil && plain?.gameRun == false && plain?.solution?.reachable == true, "the job judges a plain corridor round the chunk (26 cells wider), no flip, no game run")
    doc.setMirrorCheck(true)
    let both = judged()
    check(both?.flipped?.ok == true && both?.flipped?.solution?.reachable == true, "with the mirror check on, the flipped chunk's corridor too")
    guard let report = both else { check(false, "a report"); return }
    let win = report.windowed(c0: MazeChunkCheck.corridorStart, cols: 5, rows: 4)
    check(win.cols == 5 && win.rows == 4 && win.cellMap?.count == 4 && win.cellMap?.allSatisfy { $0.count == 5 } == true && win.flipped != nil, "windowed: the map is the chunk's own 5 x 4 cells")
    let steps = win.way ?? []
    check(!steps.isEmpty && steps.count == report.way?.count && steps.first?[0] == (report.way?.first?[0] ?? 0) - 13 && steps.first?[1] == report.way?.first?[1], "...the bot's way counted from the chunk's first column, heights as they were")
    check(win.cellMap?.contains { $0.contains("w") } == true, "...and the map has the way through in it")

    // the checklist in words
    let items = MazeChunkCheck.items(both, size: (5, 4), mirror: true)
    check(items.first?.what == "A valid chunk" && items.contains { $0.what == "One way through" && $0.ok == true } && items.contains { $0.what == "Flipped: One way through" && $0.ok == true }, "the checklist: a valid chunk, one way through, and the same flipped")
    check(items.contains { $0.what == "Near misses" && $0.ok == true && $0.why.contains("asked for any") }, "...near misses shown, none asked of a chunk")
    check(MazeChunkCheck.satisfied(items) == true && MazeChunkCheck.state(items, checking: false).ready && !MazeChunkCheck.state(items, checking: true).ready, "...satisfied; ready unless still checking")
    let waiting = MazeChunkCheck.items(plain, size: (5, 4), mirror: true)
    check(waiting.last?.ok == nil && MazeChunkCheck.satisfied(waiting) == nil && plain?.mirrorPending(true) == true && plain?.mirrorPending(false) == false, "Mirror on but the flipped one not back yet: still working it out")
    check(MazeChunkCheck.items(nil, size: (5, 4), mirror: false).first?.ok == nil, "no report yet: checking")
    // a chunk that fails: stone across the floor's air, the corridor has no way through
    _ = doc.restore("#####\n#####\n#####\n#####\n", label: "Walled")
    let blocked = judged()
    let bad = MazeChunkCheck.items(blocked, size: (5, 4), mirror: false)
    check(bad.contains { $0.ok == false } && MazeChunkCheck.satisfied(bad) == false && MazeChunkCheck.state(bad, checking: false).fix >= 1, "a block of solid stone: something to fix, counted")
    let nope = MazeChunkCheck.items(GridReport(ok: false, error: "it doesn't read", sameAs: nil, gameRun: nil, name: nil, level: nil, cols: nil, rows: nil, entryRow: nil, exitRow: nil, hard: nil, wantMisses: nil, solution: nil, cellMap: nil, way: nil, wayNearMisses: nil, meter: nil, course: nil, designs: nil, game: nil), size: (5, 4), mirror: false)
    check(nope.count == 1 && nope[0].ok == false && nope[0].why == "it doesn't read", "a report that isn't ok: its error")
    // each line of the checklist on its own, from reports as the checker writes them
    func crafted(reachable: Bool = true, routes: Int = 1, softLocks: Int = 0, dead: Int = 0, out: Bool = true) -> GridReport? {
        let sol = "{\"reachable\":\(reachable),\"routes\":\(routes),\"routeCells\":9,\"deadEndCells\":\(dead),\"softLocks\":\(softLocks),\"minEggs\":3,\"moves\":10,\"perfects\":0,\"shaves\":0,\"squeezes\":0,\"nearMisses\":2}"
        let meter = "{\"rate\":1.25,\"out\":\(out),\"cells\":30,\"waiting\":2,\"seconds\":4.0,\"crash\":[7,3,1],\"nearMisses\":[]}"
        return decodeJSON(GridReport.self, "{\"ok\":true,\"solution\":\(sol),\"meter\":[\(meter),\(meter)]}")
    }
    func line(_ r: GridReport?, _ what: String) -> MazeCheckItem? { MazeChunkCheck.items(r, size: (5, 4), mirror: false).first { $0.what == what } }
    check(crafted() != nil && MazeChunkCheck.satisfied(MazeChunkCheck.items(crafted(), size: (5, 4), mirror: false)) == true, "(a report as the checker writes it reads, and passes)")
    check(line(crafted(routes: 3), "One way through")?.ok == false && line(crafted(routes: 3), "One way through")?.why.contains("2 cells") == true, "more than one way through: that line to fix, counting the other ways")
    check(line(crafted(softLocks: 2), "No traps")?.ok == false && line(crafted(softLocks: 2), "No traps")?.why.contains("2 places") == true, "a trap: that line to fix, how many")
    check(line(crafted(dead: 4), "No traps")?.ok == true && line(crafted(dead: 4), "No traps")?.why.contains("4 dead-end cells") == true, "dead ends that end her run are no trap")
    check(line(crafted(out: false), "Out with the meter at its hardest")?.ok == false && line(crafted(out: false), "Out with the meter at its hardest")?.cell == [7, 4], "the meter runs out: that line to fix, where she ran short")
    check(line(crafted(reachable: false), "Out, by stacking alone")?.ok == false && line(crafted(reachable: false), "One way through") == nil, "no way to the exit: that line to fix, and no more lines after it")
}

/// the Gauntlet generator with chunks (Builtin.generateMaze, gd_generate_chunks)
@MainActor
func mazeRoll() {
    let well = Chunks.text(ChunkHeader(maze: true, ranges: [LevelRange(20, 20)], weight: 10, uses: 0, mirror: true), body: ".....\n.....\n##...\n##S##\n")
    let bundle = "=== sacred-well\n" + well
    let out = Builtin.generateMaze(level: 20, levelText: nil, width: 170, floors: 3, seed: 31, hazards: [], density: 0, chunks: bundle)
    check(out.ok && !(out.chunks ?? []).isEmpty && (out.chunks ?? []).allSatisfy { $0.id == "sacred-well" && $0.c1 - $0.c0 == 4 && $0.r1 - $0.r0 == 3 } && out.text.contains("chunks: sacred-well"), "a maze rolled with a chunk in range: it is placed, where it says, named in the text")
    let rows = LevelStore.rows(of: out.text)
    if let u = (out.chunks ?? []).first {
        let cut = String(Array(rows[u.r1])[u.c0...u.c1])
        check(cut == "##S##", "...its floor row is in the grid where it says")
    }
    let far = Builtin.generateMaze(level: 21, levelText: nil, width: 170, floors: 3, seed: 31, hazards: [], density: 0, chunks: bundle)
    let none = Builtin.generateMaze(level: 21, levelText: nil, width: 170, floors: 3, seed: 31, hazards: [], density: 0)
    check(far.ok && (far.chunks ?? []).isEmpty && far.text == none.text, "level 21: no chunk, exactly the maze there is without any")
    check((out.chunkNote ?? "x").isEmpty && (out.dropped ?? ["x"]).isEmpty && (out.reduced ?? ["x"]).isEmpty, "a chunk that fits: nothing given up, no note")
    // a chunk no maze holds: a maze all the same, and the result says what was given up
    let hard = Chunks.text(ChunkHeader(maze: true, ranges: [LevelRange(20, 20)], weight: 10, uses: 0, mirror: true), body: ".....\n..#..\n.###.\nS#S#S\n")
    let gave = Builtin.generateMaze(level: 20, levelText: nil, width: 170, floors: 7, seed: 977, hazards: [], density: 0, chunks: "=== three-saws\n" + hard)
    let said = (gave.dropped ?? []) + (gave.reduced ?? [])
    check(gave.ok && said == ["three-saws"] && (gave.chunkNote ?? "").contains("three-saws") && (gave.chunkNote ?? "").contains("level 20"), "a chunk no maze holds: a maze comes back, the chunk named as given up")
    check(ChunkWords.rollNotice(skipped: [], note: gave.chunkNote ?? "", placed: (gave.chunks ?? []).map(\.id))?.hasPrefix(gave.dropped?.isEmpty == false ? "Left out: three-saws" : "Fewer of: three-saws") == true, "...and the notice carries the generator's words")
}

/// the Level generator with a chunk no course passes with (EdDoc.chunksNote)
@MainActor
func courseRoll() {
    var err = ""
    guard let text = Optional(Builtin.bake(35)), let doc = EdDoc(number: 35, text: text, error: &err) else { check(false, "level 35 opens"); return }
    let poison = Chunks.text(ChunkHeader(maze: false, ranges: [LevelRange(31, 40)], weight: 10, uses: 0, mirror: false), body: "length 33\ncourse\n13 0 crate\n30 0 hay\n13 0 crate\nsaws\n15.6\n16.8\n18\n")
    check(doc.regenerate(seed: 2, hazards: ["saw", "laser", "lava", "disco"], density: 5, chunks: "=== poison\n" + poison) && doc.chunksUsed.isEmpty, "a roll with a chunk that spoils every course goes ahead")
    check(doc.chunksNote == "Left out: poison \u{2014} no course passed with it at level 35", "...and the doc's note says what was left out and why")
    check(ChunkWords.rollNotice(skipped: [], note: doc.chunksNote, placed: doc.chunksUsed.map(\.id)) == doc.chunksNote, "...the notice is that note")
    check(doc.regenerate(seed: 3, hazards: [], density: 0) && doc.chunksNote.isEmpty, "a roll with no chunks: no note")
}

MainActor.assumeIsolated { run(); core(); backups(); session(); sync(); mazeChunks(); mazeChecker(); mazeRoll(); courseRoll() }
print(failures == 0 ? "all ios_editor_chunks checks passed" : "ios_editor_chunks TESTS FAILED")
exit(failures == 0 ? 0 : 1)
