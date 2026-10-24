// The levels and grids on this device (Documents), editable with no network: levels/level_NN.txt and
// gauntlets/<name>.txt, as the game's files are; and what sync needs (sync/state.json: for each, the text the Mac and
// this device last agreed on, its hash; sync/base/: those texts, a merge's base). First launch: the game's own.
import Combine
import CryptoKit
import Foundation
import SwiftUI

struct LevelEntry: Identifiable, Hashable {
    let number: Int
    var title: String
    var dirty: Bool          // changed here since the last sync
    var id: Int { number }
    var zone: String { number >= 11 && number <= 20 ? "egypt" : number >= 21 && number <= 30 ? "japan" : "city" }
}

struct SyncState: Codable {
    struct Entry: Codable, Hashable {
        var base: String          // the hash of the text both sides last agreed on ("": none, a new one here)
        var deleted = false       // taken out here, not yet on the Mac
    }
    var levels: [String: Entry] = [:]
    var grids: [String: Entry] = [:]
    var server: ServerInfo? = nil
    var lastSync: Date? = nil
    var verdicts: [String: Verdict] = [:]
    var lastBackup: Date? = nil       // (the levels list's backup menu says when)
    /// mazes renamed here that the Mac has by their old names: told at the next sync, in order
    typealias Rename = GridRename
    var renames: [Rename]? = nil
    /// the chunks (ChunkStore): as `levels` and `grids`, the text both sides last agreed on, a hash of it. Optional: a state file
    /// from before chunks has no such key, and reads as no chunks yet
    var chunks: [String: Entry]? = nil
    /// the game's own chunks were put in Documents once (a fresh device gets the repo's; later a deleted chunk stays deleted)
    var chunksSeeded: Bool? = nil
}

struct ServerInfo: Codable, Hashable {
    var host: String
    var port: Int
    var token: String
    var name: String
}

@MainActor
final class LevelStore: ObservableObject {
    static let shared = LevelStore(root: LaunchRequest.current.dataRoot()) // (a UI test's own folder, or Documents)

    @Published private(set) var levels: [LevelEntry] = []
    @Published private(set) var grids: [String: [String]] = [:]
    @Published var state = SyncState()
    /// a level's file changed by sync (pulled or merged): an editor that has it open takes it up
    let changes = PassthroughSubject<(number: Int, text: String), Never>()

    let root: URL
    private var levelsDir: URL { root.appendingPathComponent("levels") }
    private var gridsDir: URL { root.appendingPathComponent("gauntlets") }
    private var syncDir: URL { root.appendingPathComponent("sync") }
    private var baseDir: URL { syncDir.appendingPathComponent("base") }
    private var historyDir: URL { root.appendingPathComponent("history") }
    private let fm = FileManager.default

    init(root: URL? = nil) {
        self.root = root ?? fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        for d in [levelsDir, gridsDir, syncDir, baseDir, historyDir] { try? fm.createDirectory(at: d, withIntermediateDirectories: true) }
        loadState()
        if (try? fm.contentsOfDirectory(atPath: levelsDir.path).isEmpty) ?? true { seed() }
        reload()
    }

    // MARK: files

    private func levelURL(_ n: Int) -> URL { levelsDir.appendingPathComponent(String(format: "level_%02d.txt", n)) }
    private func baseURL(_ n: Int) -> URL { baseDir.appendingPathComponent(String(format: "level_%02d.txt", n)) }
    private func gridURL(_ name: String) -> URL { gridsDir.appendingPathComponent(name + ".txt") }
    private func gridBaseURL(_ name: String) -> URL { baseDir.appendingPathComponent("grid_" + name + ".txt") }

    /// the game's own levels and grids, as the Mac's checkout had them when this app was built: both sides' base
    private func seed() {
        for n in Builtin.numbers {
            guard let text = Builtin.level(n) else { continue }
            write(text, to: levelURL(n))
            write(text, to: baseURL(n))
            state.levels[String(n)] = .init(base: textHash(text))
        }
        for (name, rows) in Builtin.grids {
            let text = rows.joined(separator: "\n") + "\n"
            write(text, to: gridURL(name))
            write(text, to: gridBaseURL(name))
            state.grids[name] = .init(base: textHash(text))
        }
        saveState()
    }

    private func write(_ text: String, to url: URL) { try? text.write(to: url, atomically: true, encoding: .utf8) }
    private func read(_ url: URL) -> String? { try? String(contentsOf: url, encoding: .utf8) }

    func reload() {
        var out: [LevelEntry] = []
        for f in (try? fm.contentsOfDirectory(atPath: levelsDir.path)) ?? [] where f.hasPrefix("level_") && f.hasSuffix(".txt") {
            guard let n = Int(f.dropFirst(6).dropLast(4)), let text = read(levelsDir.appendingPathComponent(f)) else { continue }
            out.append(LevelEntry(number: n, title: Self.title(of: text, number: n), dirty: textHash(text) != (state.levels[String(n)]?.base ?? "")))
        }
        levels = out.sorted { $0.number < $1.number }
        var g: [String: [String]] = [:]
        for f in (try? fm.contentsOfDirectory(atPath: gridsDir.path)) ?? [] where f.hasSuffix(".txt") {
            guard let text = read(gridsDir.appendingPathComponent(f)) else { continue }
            g[String(f.dropLast(4))] = Self.rows(of: text)
        }
        grids = g
        for (name, rows) in g { Builtin.useGrid(name, rows: rows) } // (merges and checks read them: LevelCatalog::dev())
    }

    static func title(of text: String, number: Int) -> String {
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            guard line.hasPrefix(";") else { break }
            let note = line.dropFirst().trimmingCharacters(in: .whitespaces)
            if !note.isEmpty { return note }
        }
        return "Level \(number)"
    }

    static func rows(of gridText: String) -> [String] {
        gridText.split(separator: "\n").map(String.init).filter { !$0.isEmpty && !$0.hasPrefix(";") }
    }

    // MARK: levels

    func text(of n: Int) -> String? { read(levelURL(n)) }
    func baseText(of n: Int) -> String? { read(baseURL(n)) }

    func save(_ n: Int, text: String) {
        guard read(levelURL(n)) != text else { return }
        write(text, to: levelURL(n))
        if state.levels[String(n)] == nil { state.levels[String(n)] = .init(base: "") }
        state.levels[String(n)]?.deleted = false
        saveState()
        if let i = levels.firstIndex(where: { $0.number == n }) {
            levels[i].dirty = textHash(text) != (state.levels[String(n)]?.base ?? "")
            levels[i].title = Self.title(of: text, number: n)
        } else { reload() }
    }

    /// a new level: the next number, the generator's course for it
    func newLevel() -> Int {
        let n = (levels.map(\.number).max() ?? 0) + 1
        save(n, text: Builtin.bake(n))
        return n
    }

    func duplicate(_ n: Int) -> Int? {
        guard let text = text(of: n) else { return nil }
        let m = (levels.map(\.number).max() ?? 0) + 1
        save(m, text: text.replacingOccurrences(of: "; ", with: "; (Copy of \(n)) ", options: [], range: text.range(of: "; ")))
        return m
    }

    func delete(_ n: Int) {
        try? fm.removeItem(at: levelURL(n))
        forgetHistory(n)
        if state.levels[String(n)]?.base.isEmpty ?? true { state.levels[String(n)] = nil } // (never on the Mac: just gone)
        else { state.levels[String(n)]?.deleted = true }
        saveState()
        reload()
    }

    /// what sync agreed on: the text both sides have now. `unless`: only if the file here is still what sync read at
    /// its start (an edit made meanwhile is never overwritten: the base moves on, the edit syncs next time)
    func agreed(_ n: Int, text: String, ifStill expected: String?? = .none) {
        write(text, to: baseURL(n))
        state.levels[String(n)] = .init(base: textHash(text))
        if case let .some(want) = expected, read(levelURL(n)) != want { return }
        if read(levelURL(n)) != text {
            write(text, to: levelURL(n))
            changes.send((n, text))
        }
    }

    func forget(_ n: Int) {
        try? fm.removeItem(at: levelURL(n))
        forgetHistory(n)
        try? fm.removeItem(at: baseURL(n))
        state.levels[String(n)] = nil
    }

    // MARK: each level's history (kept between launches)

    private func historyURL(_ n: Int) -> URL { historyDir.appendingPathComponent(String(format: "level_%02d.json", n)) }
    func history(of n: Int) -> HistoryFile? {
        guard let data = try? Data(contentsOf: historyURL(n)) else { return nil }
        return try? JSONDecoder().decode(HistoryFile.self, from: data)
    }
    func saveHistory(_ n: Int, _ h: HistoryFile) {
        if let data = try? JSONEncoder().encode(h) { try? data.write(to: historyURL(n), options: .atomic) }
    }
    func forgetHistory(_ n: Int) { try? fm.removeItem(at: historyURL(n)) }
    /// the version the Mac and this device last agreed on, and its hash (nil: never synced)
    var baseHashes: [Int: String] { Dictionary(uniqueKeysWithValues: state.levels.compactMap { k, v in Int(k).map { ($0, v.base) } }) }

    // MARK: grids

    func gridText(_ name: String) -> String? { read(gridURL(name)) }
    func gridBase(_ name: String) -> String? { read(gridBaseURL(name)) }
    /// a maze edited here: its file, and the grid every level and play-test reads
    func saveGrid(_ name: String, text: String) {
        guard read(gridURL(name)) != text else { return }
        write(text, to: gridURL(name))
        let rows = Self.rows(of: text)
        grids[name] = rows
        Builtin.useGrid(name, rows: rows)
        if state.grids[name] == nil { state.grids[name] = .init(base: ""); saveState() }
    }
    /// changed here since the last sync
    func gridDirty(_ name: String) -> Bool { (read(gridURL(name)).map(textHash) ?? "") != (state.grids[name]?.base ?? "") }
    /// what sync agreed on for a grid (see agreed): an open maze takes it up
    func agreedGrid(_ name: String, text: String, ifStill expected: String?? = .none) {
        write(text, to: gridBaseURL(name))
        state.grids[name] = .init(base: textHash(text))
        if case let .some(want) = expected, read(gridURL(name)) != want { return }
        if read(gridURL(name)) != text {
            write(text, to: gridURL(name))
            gridChanges.send((name, text))
        }
    }
    /// a grid's file changed by sync: a maze open on it takes it up
    let gridChanges = PassthroughSubject<(name: String, text: String), Never>()

    /// a maze renamed here (the name checked: Builtin.validGauntletName, no grid has it): its file, history and synced
    /// base go under the new name; every level here that lays it, and the history kept for it, lays it by the new one;
    /// the Mac is told at the next sync (a maze it never had: nothing to tell, it's new there anyway)
    func renameGrid(_ from: String, to: String) {
        guard from != to, let text = read(gridURL(from)), read(gridURL(to)) == nil else { return }
        write(text, to: gridURL(to))
        try? fm.removeItem(at: gridURL(from))
        for (a, b) in [(gridBaseURL(from), gridBaseURL(to)), (gridHistoryURL(from), gridHistoryURL(to))] where fm.fileExists(atPath: a.path) {
            try? fm.moveItem(at: a, to: b)
        }
        let entry = state.grids.removeValue(forKey: from)
        state.grids[to] = entry ?? .init(base: "")
        state.renames = GridNames.pending(state.renames ?? [], from: from, to: to, known: !(entry?.base.isEmpty ?? true))
        for l in levels {
            if let t = self.text(of: l.number) {
                let r = Builtin.renameGauntlet(in: t, from: from, to: to)
                if r != t { save(l.number, text: r) }
            }
            if let h = history(of: l.number), ([h.current] + h.undo.map(\.text) + h.redo.map(\.text)).contains(where: { $0.contains(" gauntlet \(from)") }) {
                let r = { (t: String) in Builtin.renameGauntlet(in: t, from: from, to: to) }
                saveHistory(l.number, HistoryFile(current: r(h.current), undo: h.undo.map { .init(label: $0.label, time: $0.time, at: $0.at, text: r($0.text)) },
                                                  redo: h.redo.map { .init(label: $0.label, time: $0.time, at: $0.at, text: r($0.text)) }))
            }
        }
        if let rows = grids.removeValue(forKey: from) { grids[to] = rows; Builtin.useGrid(to, rows: rows) }
        saveState()
    }
    /// the Mac renamed it too (sync): the level texts it rewrote are the base both sides agree on, if renaming ours
    /// gives the same (else the merge settles them as ever); the rename told
    func renameSynced(_ r: SyncState.Rename, levels: [Int: String]) {
        for (n, t) in levels {
            guard let base = baseText(of: n), Builtin.renameGauntlet(in: base, from: r.from, to: r.to) == t else { continue }
            write(t, to: baseURL(n))
            state.levels[String(n)] = .init(base: textHash(t))
        }
        state.renames = (state.renames ?? []).filter { $0 != r }
        saveState()
    }
    /// a grid the Mac no longer has (renamed or taken out there), unchanged here and laid by no level here: gone
    func forgetGrid(_ name: String) {
        guard levels.allSatisfy({ !(text(of: $0.number) ?? "").contains(" gauntlet \(name)\n") }) else { return }
        for u in [gridURL(name), gridBaseURL(name), gridHistoryURL(name)] { try? fm.removeItem(at: u) }
        state.grids[name] = nil
        grids[name] = nil
    }

    private func gridHistoryURL(_ name: String) -> URL { historyDir.appendingPathComponent("grid_" + name + ".json") }
    func gridHistory(of name: String) -> GridHistoryFile? {
        guard let data = try? Data(contentsOf: gridHistoryURL(name)) else { return nil }
        return try? JSONDecoder().decode(GridHistoryFile.self, from: data)
    }
    func saveGridHistory(_ name: String, _ h: GridHistoryFile) {
        if let data = try? JSONEncoder().encode(h) { try? data.write(to: gridHistoryURL(name), options: .atomic) }
    }

    // MARK: verdicts and state

    func verdict(_ n: Int) -> Verdict? {
        guard let v = state.verdicts[String(n)], let text = text(of: n), v.hash == textHash(text) else { return nil }
        return v
    }

    func setVerdict(_ n: Int, cleared: Bool, seconds: Double, text: String) {
        state.verdicts[String(n)] = Verdict(cleared: cleared, seconds: seconds, hash: textHash(text))
        saveState()
    }

    private var stateURL: URL { syncDir.appendingPathComponent("state.json") }
    private func loadState() {
        if let data = try? Data(contentsOf: stateURL), let s = try? JSONDecoder().decode(SyncState.self, from: data) { state = s }
    }
    func saveState() {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        if let data = try? e.encode(state) { try? data.write(to: stateURL, options: .atomic) }
    }
}
