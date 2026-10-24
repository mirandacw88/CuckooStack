// The chunks on this device (Documents), editable with no network: chunks/<id>.txt, as assets/chunks/<id>.txt will have
// them (Chunk.h's file: a header, then the body), each one's history in history/chunk_<id>.json, the bot's last word on it,
// and the copies a restore leaves (chunks_before_restore/). First launch: the game's own chunks (assets/chunks as the app was
// built), if there are any. Sync (SyncEngine.syncChunks, ChunkSyncLogic.swift) keeps what the Mac and this device last agreed
// on for each chunk (the sync state's chunks, sync/base/chunk_<id>.txt). The files' rules and their order are the
// core's (Chunks in Core.swift); the names, words, the master backup and the restore's plan are ChunkLogic.swift's.
import Combine
import Foundation

/// a chunk in the list: what its file says in the header, whether it reads as the generator needs it
struct ChunkEntry: Identifiable, Hashable {
    let id: String
    var header: ChunkHeader
    var canMirror: Bool
    var problem: String              // why the generator can't place it ("": it reads)
    var verdictKey: String           // what the bot's word on it is for (its body and the level it's judged at)
    var dirty = false                // changed here since the last sync (or never synced)
    var maze: Bool { header.maze }
    var title: String { header.summary.isEmpty ? ChunkNames.shown(id).capitalized : header.summary }
}

/// what a restore changed, kept until the next one so it can be undone: the old text of the chunks it replaced, the ones
/// it added, and where the copy of the replaced ones went
struct ChunkRestoreRecord: Equatable {
    var replaced: [String: String]
    var added: [String]
    var copy: URL?
    var skipped = 0
    var summary: String {
        var parts: [String] = []
        if !added.isEmpty { parts.append("\(added.count) new") }
        if !replaced.isEmpty { parts.append("\(replaced.count) changed") }
        return "Restored " + (parts.isEmpty ? "nothing" : parts.joined(separator: ", ")) + (skipped > 0 ? " (\(skipped) in the file didn't read)" : "")
    }
}

/// where the bot's words are kept (the levels' sync state; a chunk's under "chunk:<id>"): the store asks for little
@MainActor
protocol VerdictBook: AnyObject {
    var verdicts: [String: Verdict] { get set }
    /// what sync last agreed on for each chunk (the levels' sync state: its chunks), and whether the game's own chunks were put in
    /// Documents yet
    var chunkEntries: [String: SyncState.Entry] { get set }
    var chunksSeeded: Bool { get set }
    func saveState()
}

@MainActor
final class ChunkStore: ObservableObject {

    @Published private(set) var chunks: [ChunkEntry] = []        // in the list's order
    @Published private(set) var lastRestore: ChunkRestoreRecord?
    /// a chunk's file changed by something other than its editor (a restore, an undone restore): an editor that has it open
    /// takes it up. `text` nil: the chunk is gone
    let changes = PassthroughSubject<(id: String, text: String?), Never>()
    /// a chunk renamed here: what's open on it follows it to its new name
    let renames = PassthroughSubject<(from: String, to: String), Never>()

    private let root: URL
    private let levels: VerdictBook
    private let fm = FileManager.default
    private var dir: URL { root.appendingPathComponent("chunks") }
    private var historyDir: URL { root.appendingPathComponent("history") }
    private var copiesDir: URL { root.appendingPathComponent("chunks_before_restore") }
    private var baseDir: URL { root.appendingPathComponent("sync").appendingPathComponent("base") }

    /// the chunks under `root` (its chunks/, history/ and sync/base/ folders); ChunkStore.shared is the app's, in Documents.
    /// The first time (an empty folder, nothing synced, nothing seeded before), the game's own chunks go in as what the Mac has
    /// (`builtin`: id to file text; the app's are Builtin.chunks)
    init(root: URL, verdicts: VerdictBook, builtin: [String: String]? = nil) {
        levels = verdicts
        self.root = root
        for d in [dir, historyDir, baseDir] { try? fm.createDirectory(at: d, withIntermediateDirectories: true) }
        if !levels.chunksSeeded {
            if levels.chunkEntries.isEmpty, ((try? fm.contentsOfDirectory(atPath: dir.path)) ?? []).isEmpty { seed(builtin ?? Builtin.chunks) }
            levels.chunksSeeded = true
            levels.saveState()
        }
        reload()
    }

    private func seed(_ files: [String: String]) {
        for (id, text) in files where ChunkNames.valid(id) {
            write(text, to: url(id))
            write(text, to: baseURL(id))
            levels.chunkEntries[id] = .init(base: textHash(text))
        }
    }

    // MARK: files

    private func url(_ id: String) -> URL { dir.appendingPathComponent(id + ".txt") }
    private func historyURL(_ id: String) -> URL { historyDir.appendingPathComponent("chunk_" + id + ".json") }
    private func baseURL(_ id: String) -> URL { baseDir.appendingPathComponent("chunk_" + id + ".txt") }
    private func read(_ u: URL) -> String? { try? String(contentsOf: u, encoding: .utf8) }
    private func write(_ text: String, to u: URL) { try? text.write(to: u, atomically: true, encoding: .utf8) }

    func text(of id: String) -> String? { read(url(id)) }
    func exists(_ id: String) -> Bool { fm.fileExists(atPath: url(id).path) }
    var ids: Set<String> { Set(chunks.map(\.id)) }
    func entry(_ id: String) -> ChunkEntry? { chunks.first { $0.id == id } }

    func reload() {
        var files: [String: String] = [:]
        for f in (try? fm.contentsOfDirectory(atPath: dir.path)) ?? [] where f.hasSuffix(".txt") {
            let id = String(f.dropLast(4))
            if ChunkNames.valid(id), let t = read(dir.appendingPathComponent(f)) { files[id] = t }
        }
        var out: [ChunkEntry] = []
        for id in Chunks.sorted(files) {
            guard let t = files[id] else { continue }
            out.append(synced(Self.entry(id, t), text: t))
        }
        chunks = out
    }

    /// a file as the list shows it. One whose header doesn't read still has a row (to open and mend, or take out)
    static func entry(_ id: String, _ text: String) -> ChunkEntry {
        let f = Chunks.parse(id, text)
        guard f.ok else { return ChunkEntry(id: id, header: ChunkHeader(), canMirror: false, problem: f.error ?? "it doesn't read", verdictKey: "") }
        let h = f.header
        return ChunkEntry(id: id, header: h, canMirror: f.canMirror ?? true, problem: f.error ?? "", verdictKey: verdictKey(first: h.first, flipped: h.mirror && (f.canMirror ?? true), body: f.body ?? ""))
    }

    private func synced(_ e: ChunkEntry, text: String) -> ChunkEntry {
        var e = e
        e.dirty = textHash(text) != (levels.chunkEntries[e.id]?.base ?? "")
        return e
    }

    /// what a bot verdict is for: the level it's judged at, whether it played the flip too, the body
    static func verdictKey(first: Int, flipped: Bool, body: String) -> String { "\(first)|\(flipped ? "flip" : "plain")|" + body }

    func save(_ id: String, text: String) {
        guard read(url(id)) != text else { return }
        put(id, text)
        let e = synced(Self.entry(id, text), text: text)
        if let i = chunks.firstIndex(where: { $0.id == id }) {
            let before = chunks[i]
            chunks[i] = e
            if before.header.ranges != e.header.ranges { reload() } // (its place in the list moved)
        } else { reload() }
    }

    /// a new course chunk (its file from the core: a run-in, a wall, a run-out), under `name` if free, else numbered
    @discardableResult func newCourse(named name: String, first: Int = 1) -> String {
        let id = ChunkNames.unique(ChunkNames.typed(name).isEmpty ? "new-chunk" : ChunkNames.typed(name), taken: ids)
        save(id, text: Chunks.blank(id, first: first))
        return id
    }

    /// a new maze chunk (its file from the core: a 9 × 5 block with a saw in its floor), named as a course chunk is
    @discardableResult func newMaze(named name: String, first: Int = 1) -> String {
        let id = ChunkNames.unique(ChunkNames.typed(name).isEmpty ? "new-maze-chunk" : ChunkNames.typed(name), taken: ids)
        save(id, text: Chunks.blankMaze(id, first: first))
        return id
    }

    func duplicate(_ id: String) -> String? {
        guard let t = text(of: id) else { return nil }
        let copy = ChunkNames.copy(of: id, taken: ids)
        save(copy, text: t)
        if let h = history(of: id) { saveHistory(copy, h) }
        return copy
    }

    /// the chunk under another name (its history and the bot's word go with it); false if the name is taken or isn't one
    @discardableResult func rename(_ id: String, to new: String) -> Bool {
        guard new != id, ChunkNames.valid(new), !exists(new), let t = text(of: id) else { return false }
        put(new, t)
        try? fm.removeItem(at: url(id))
        noteGone(id) // (to the Mac it's one taken out and one new: told at the next sync)
        if fm.fileExists(atPath: historyURL(id).path) { try? fm.moveItem(at: historyURL(id), to: historyURL(new)) }
        if let v = levels.verdicts[Self.verdictSlot(id)] { levels.verdicts[Self.verdictSlot(new)] = v; levels.verdicts[Self.verdictSlot(id)] = nil; levels.saveState() }
        reload()
        renames.send((id, new))
        return true
    }

    func delete(_ id: String) {
        try? fm.removeItem(at: url(id))
        noteGone(id)
        forgetHistory(id)
        if levels.verdicts[Self.verdictSlot(id)] != nil { levels.verdicts[Self.verdictSlot(id)] = nil; levels.saveState() }
        reload()
    }

    // MARK: sync

    /// a chunk file written here: it is a chunk sync knows of now, and not one taken out
    private func put(_ id: String, _ text: String) {
        write(text, to: url(id))
        var e = levels.chunkEntries[id] ?? .init(base: "")
        guard levels.chunkEntries[id] == nil || e.deleted else { return }
        e.deleted = false
        levels.chunkEntries[id] = e
        levels.saveState()
    }

    /// a chunk's file taken out here: one sync never agreed on is simply gone; one it did is held (deleted) till the Mac is told
    private func noteGone(_ id: String) {
        guard let e = levels.chunkEntries[id] else { return }
        if e.base.isEmpty { levels.chunkEntries[id] = nil; try? fm.removeItem(at: baseURL(id)) } else { levels.chunkEntries[id]?.deleted = true }
        levels.saveState()
    }

    /// every chunk file here, id to the hash of its text (what sync plans from; a file whose header doesn't read is one too)
    func hashes() -> [String: String] {
        var out: [String: String] = [:]
        for f in (try? fm.contentsOfDirectory(atPath: dir.path)) ?? [] where f.hasSuffix(".txt") {
            let id = String(f.dropLast(4))
            if ChunkNames.valid(id), let t = read(dir.appendingPathComponent(f)) { out[id] = textHash(t) }
        }
        return out
    }
    var syncEntries: [String: SyncState.Entry] { levels.chunkEntries }
    /// the text sync last agreed on (a merge's base; nil: never synced)
    func baseText(of id: String) -> String? { read(baseURL(id)) }
    /// changed here since the last sync, or never synced
    func dirty(_ id: String) -> Bool { (read(url(id)).map(textHash) ?? "") != (levels.chunkEntries[id]?.base ?? "") }

    /// what sync agreed on: the text both sides have now. `ifStill`: only if the file here is still what sync read at its start
    /// (an edit made meanwhile is never overwritten: the base moves on, the edit syncs next time). A chunk open in an editor takes
    /// a changed file up (`changes`)
    func agreed(_ id: String, text: String, ifStill expected: String?? = .none) {
        write(text, to: baseURL(id))
        levels.chunkEntries[id] = .init(base: textHash(text))
        levels.saveState()
        defer { reload() }
        if case let .some(want) = expected, read(url(id)) != want { return }
        if read(url(id)) != text {
            write(text, to: url(id))
            changes.send((id, text))
        }
    }

    /// a chunk the Mac no longer has (or one taken out here that the Mac has now taken out): gone from here, with its history and word
    func forget(_ id: String) {
        try? fm.removeItem(at: url(id))
        try? fm.removeItem(at: baseURL(id))
        forgetHistory(id)
        levels.chunkEntries[id] = nil
        if levels.verdicts[Self.verdictSlot(id)] != nil { levels.verdicts[Self.verdictSlot(id)] = nil }
        levels.saveState()
        changes.send((id, nil))
        reload()
    }

    // MARK: histories and the bot's word

    func history(of id: String) -> HistoryFile? {
        guard let data = try? Data(contentsOf: historyURL(id)) else { return nil }
        return try? JSONDecoder().decode(HistoryFile.self, from: data)
    }
    func saveHistory(_ id: String, _ h: HistoryFile) {
        if let data = try? JSONEncoder().encode(h) { try? data.write(to: historyURL(id), options: .atomic) }
    }
    /// a maze chunk's history (GridDoc's: a block of cells, not a course) is kept in the same file
    func gridHistory(of id: String) -> GridHistoryFile? {
        guard let data = try? Data(contentsOf: historyURL(id)) else { return nil }
        return try? JSONDecoder().decode(GridHistoryFile.self, from: data)
    }
    func saveGridHistory(_ id: String, _ h: GridHistoryFile) {
        if let data = try? JSONEncoder().encode(h) { try? data.write(to: historyURL(id), options: .atomic) }
    }
    func forgetHistory(_ id: String) { try? fm.removeItem(at: historyURL(id)) }

    /// (kept with the levels' in the sync state's verdicts, under "chunk:<id>")
    static func verdictSlot(_ id: String) -> String { "chunk:" + id }
    func verdict(_ e: ChunkEntry) -> Verdict? {
        guard let v = levels.verdicts[Self.verdictSlot(e.id)], !e.verdictKey.isEmpty, v.hash == textHash(e.verdictKey) else { return nil }
        return v
    }
    func setVerdict(_ id: String, cleared: Bool, seconds: Double, key: String) {
        levels.verdicts[Self.verdictSlot(id)] = Verdict(cleared: cleared, seconds: seconds, hash: textHash(key))
        levels.saveState()
        objectWillChange.send()
    }

    // MARK: the generators

    /// every chunk the Level generator may place, as it takes them (`=== id` before each file), in the list's order; and the
    /// ones it can't (a file that doesn't read, as the generator needs it: the roll would stop at it). Maze chunks are the
    /// maze generator's (mazeBundle)
    func rollBundle() -> (text: String, skipped: [String]) { bundle(maze: false) }

    /// the maze chunks the Gauntlet generator may place, the same way
    func mazeBundle() -> (text: String, skipped: [String]) { bundle(maze: true) }

    private func bundle(maze: Bool) -> (text: String, skipped: [String]) {
        var text = "", skipped: [String] = []
        for e in chunks where e.maze == maze {
            guard e.problem.isEmpty, var t = self.text(of: e.id) else { skipped.append(e.id); continue }
            if !t.hasSuffix("\n") { t += "\n" }
            text += "=== \(e.id)\n" + t
        }
        return (text, skipped)
    }

    // MARK: the master backup (Back up chunks…, Restore chunks…)

    func makeBackup(now: Date = Date()) -> ChunkBackup {
        ChunkBackup(made: now, app: Self.appVersion, chunks: chunks.compactMap { e in text(of: e.id).map { .init(id: e.id, text: $0) } })
    }

    static var appVersion: String {
        let b = Bundle.main
        return "Cuckoo Editor \(b.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?") (\(b.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"))"
    }

    var currentTexts: [String: String] { Dictionary(uniqueKeysWithValues: chunks.compactMap { e in text(of: e.id).map { (e.id, $0) } }) }

    func plan(for backup: ChunkBackup) -> ChunkRestorePlan { ChunkRestorePlan(backup: backup, current: currentTexts) }

    /// the backup brought back as one step: its new chunks added, its changed ones put in place of these (the old ones kept in
    /// a "before restore" copy next to the chunks, and in the record, so the step can be undone); the unchanged ones and the
    /// ones here that it hasn't are left alone. A chunk in it whose file doesn't read is skipped
    @discardableResult func restore(_ backup: ChunkBackup, now: Date = Date()) -> ChunkRestoreRecord {
        let plan = self.plan(for: backup)
        let mine = currentTexts
        var record = ChunkRestoreRecord(replaced: [:], added: [])
        let fresh = Set(plan.new), changed = Set(plan.changed)
        for item in backup.chunks where fresh.contains(item.id) || changed.contains(item.id) {
            guard Chunks.parse(item.id, item.text).ok else { record.skipped += 1; continue }
            if changed.contains(item.id), let old = mine[item.id] { record.replaced[item.id] = old } else { record.added.append(item.id) }
        }
        if !record.replaced.isEmpty { record.copy = writeCopy(record.replaced, now: now) }
        for item in backup.chunks where record.replaced[item.id] != nil || record.added.contains(item.id) {
            put(item.id, item.text)
            changes.send((item.id, item.text))
        }
        reload()
        lastRestore = record
        return record
    }

    /// the last restore taken back: the chunks it replaced as they were, the ones it added gone
    func undoRestore() {
        guard let r = lastRestore else { return }
        for (id, old) in r.replaced { put(id, old); changes.send((id, old)) }
        for id in r.added { try? fm.removeItem(at: url(id)); noteGone(id); forgetHistory(id); changes.send((id, nil)) }
        lastRestore = nil
        reload()
    }

    func dismissRestore() { lastRestore = nil }

    private func writeCopy(_ replaced: [String: String], now: Date) -> URL? {
        try? fm.createDirectory(at: copiesDir, withIntermediateDirectories: true)
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd HH.mm.ss"
        let copy = ChunkBackup(made: now, app: Self.appVersion, chunks: replaced.keys.sorted().map { .init(id: $0, text: replaced[$0] ?? "") })
        let u = copiesDir.appendingPathComponent("Cuckoo Chunks before restore \(f.string(from: now)).\(ChunkBackup.fileExtension)")
        guard let data = try? copy.encoded(), (try? data.write(to: u, options: .atomic)) != nil else { return nil }
        return u
    }

    // MARK: the automatic backup (EditorBackup)

    /// chunks from the automatic backup put back: each in place of the one here (a chunk not in it is kept)
    @discardableResult func restore(texts: [String: String], histories: [String: HistoryFile]?) -> Int {
        var n = 0
        for (id, text) in texts where ChunkNames.valid(id) && Chunks.parse(id, text).ok {
            put(id, text)
            if let h = histories?[id] { saveHistory(id, h) }
            changes.send((id, text))
            n += 1
        }
        if n > 0 { reload() }
        return n
    }
}
