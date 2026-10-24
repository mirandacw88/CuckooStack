// Where a maze open in the maze editor lives, and what its checker says. The maze editor (MazeModel) edits two kinds of
// grid: a level's gauntlet (a file in gauntlets/, its history in history/grid_<name>.json) and a maze chunk's block of
// cells (the body of a file in chunks/, its history in history/chunk_<id>.json). Each is a MazeStorage; the editor asks it
// for the text, the history, the names' rules and the changes sync brings. Plus the words of a maze chunk's checklist (a
// plain corridor round the chunk, not a level: no near misses asked, the flipped chunk judged as well). Foundation and Combine
// only: tests/swift/ios_editor_chunks builds it on its own.
import Combine
import Foundation

/// a maze's file store, as the maze editor uses it
@MainActor
protocol MazeStorage: AnyObject {
    /// a maze chunk's block (its checker is a corridor round it, there are no doors, it has no play-test)
    var isChunk: Bool { get }
    /// the text of the maze called `name` as it is saved here (a chunk's: its rows), nil if there is none
    func load(_ name: String) -> String?
    func history(of name: String) -> GridHistoryFile?
    func save(_ name: String, text: String)
    func saveHistory(_ name: String, _ history: GridHistoryFile)
    /// the other mazes here (name to rows): the checker says which this one is the same as
    func otherGrids(except name: String) -> [String: [String]]
    /// a maze's file changed by something other than its editor (sync, a restore): an editor open on it takes it up
    var changes: AnyPublisher<(name: String, text: String), Never> { get }
    /// the text sync last agreed on (nil: never synced), and whether the maze changed here since
    func base(of name: String) -> String?
    func dirty(_ name: String) -> Bool
    // names: as typed, the file's, as shown, what makes one fit, and the rename itself (the file, the history)
    func typed(_ text: String) -> String
    func valid(_ name: String) -> Bool
    func taken(_ name: String) -> Bool
    func shown(_ name: String) -> String
    var nameRule: String { get }
    func rename(_ old: String, to new: String) -> Bool
}

/// the gauntlets of the levels (LevelStore's grids)
@MainActor
final class GauntletStorage: MazeStorage {
    private let store: LevelStore
    init(_ store: LevelStore) { self.store = store }

    var isChunk: Bool { false }
    func load(_ name: String) -> String? {
        store.gridText(name) ?? (store.grids[name] ?? Builtin.grids[name]).map { $0.joined(separator: "\n") + "\n" }
    }
    func history(of name: String) -> GridHistoryFile? { store.gridHistory(of: name) }
    func save(_ name: String, text: String) { store.saveGrid(name, text: text) }
    func saveHistory(_ name: String, _ history: GridHistoryFile) { store.saveGridHistory(name, history) }
    func otherGrids(except name: String) -> [String: [String]] { store.grids.filter { $0.key != name } }
    var changes: AnyPublisher<(name: String, text: String), Never> { store.gridChanges.eraseToAnyPublisher() }
    func base(of name: String) -> String? { store.gridBase(name) }
    func dirty(_ name: String) -> Bool { store.gridDirty(name) }
    func typed(_ text: String) -> String { Builtin.gauntletName(typed: text) }
    func valid(_ name: String) -> Bool { Builtin.validGauntletName(name) }
    func taken(_ name: String) -> Bool { store.grids[name] != nil || store.gridText(name) != nil } // (a built-in name no grid here has: free)
    func shown(_ name: String) -> String { Builtin.gauntletShown(name) }
    var nameRule: String { "A maze's name: letters, digits and spaces, a letter first (32 at most)" }
    func rename(_ old: String, to new: String) -> Bool { store.renameGrid(old, to: new); return true }
}

/// the block of cells of a maze chunk (ChunkStore's file: the header is the session's, the body the rows)
@MainActor
final class ChunkMazeStorage: MazeStorage {
    private let session: ChunkSession
    private let chunks: ChunkStore
    init(session: ChunkSession, chunks: ChunkStore) {
        self.session = session
        self.chunks = chunks
    }

    var isChunk: Bool { true }
    static func rows(of file: String?, id: String) -> String? {
        guard let file else { return nil }
        let f = Chunks.parse(id, file)
        return f.ok ? f.body : nil
    }
    func load(_ name: String) -> String? { Self.rows(of: chunks.text(of: name), id: name) }
    func history(of name: String) -> GridHistoryFile? { chunks.gridHistory(of: name) }
    /// the chunk's file: its header as the session has it, these rows
    func save(_ name: String, text: String) { session.save(body: text, to: chunks) }
    func saveHistory(_ name: String, _ history: GridHistoryFile) { if chunks.exists(name) { chunks.saveGridHistory(name, history) } } // (not for one taken out meanwhile)
    func otherGrids(except name: String) -> [String: [String]] { [:] }
    var changes: AnyPublisher<(name: String, text: String), Never> {
        chunks.changes.compactMap { change in change.text.flatMap { Self.rows(of: $0, id: change.id) }.map { (name: change.id, text: $0) } }.eraseToAnyPublisher()
    }
    func base(of name: String) -> String? { Self.rows(of: chunks.baseText(of: name), id: name) }
    func dirty(_ name: String) -> Bool { chunks.dirty(name) }
    func typed(_ text: String) -> String { ChunkNames.typed(text) }
    func valid(_ name: String) -> Bool { ChunkNames.valid(name) }
    func taken(_ name: String) -> Bool { chunks.exists(name) }
    func shown(_ name: String) -> String { ChunkNames.shown(name).capitalized }
    var nameRule: String { "A chunk's name: lower case letters, digits, '-' and '_'." }
    func rename(_ old: String, to new: String) -> Bool {
        guard chunks.rename(old, to: new) else { return false }
        session.id = new
        return true
    }
}

/// one line of the checker's checklist
struct MazeCheckItem: Identifiable, Hashable {
    let ok: Bool?            // nil: still working it out
    let what: String
    let why: String
    var cell: [Int]? = nil   // where to look (column, the game's row)
    var id: String { what }
}

/// a maze chunk's checklist: the corridor round it is crossed by stacking alone, one way through, no traps, out with the egg meter at
/// its hardest; its near misses are shown but not asked for (a fragment has few); with Mirror on, the same for the flipped chunk
enum MazeChunkCheck {
    /// the chunk's first column in the test corridor (MazeGen.h's chunkCorridor: the door, then a run-in of 12 cells)
    static let corridorStart = 13
    static func items(_ r: GridReport?, size: (cols: Int, rows: Int), mirror: Bool) -> [MazeCheckItem] {
        guard let r else { return [MazeCheckItem(ok: nil, what: "A valid chunk", why: "checking…")] }
        guard r.ok, let s = r.solution, let meter = r.meter, meter.count > 1 else {
            return [MazeCheckItem(ok: false, what: "A valid chunk", why: r.error ?? "it doesn't read")]
        }
        var out = [MazeCheckItem(ok: true, what: "A valid chunk", why: "\(size.cols) × \(size.rows) cells, with no doors: judged in a plain corridor")]
        out += variant(s, meter, prefix: "")
        if mirror {
            if let f = r.flipped {
                if f.ok, let fs = f.solution, let fm = f.meter, fm.count > 1 { out += variant(fs, fm, prefix: "Flipped: ") }
                else { out.append(MazeCheckItem(ok: false, what: "Flipped: reads", why: f.error ?? "the flipped chunk doesn't read")) }
            } else { out.append(MazeCheckItem(ok: nil, what: "Flipped: judged", why: "judging it flipped…")) }
        }
        return out
    }

    private static func variant(_ s: GridReport.Solution, _ meter: [GridReport.Meter], prefix p: String) -> [MazeCheckItem] {
        var out = [MazeCheckItem(ok: s.reachable, what: p + "Out, by stacking alone", why: s.reachable ? "\(s.minEggs) eggs at the least" : "no way to the exit: turn on Reach to see where she's stuck")]
        guard s.reachable else { return out }
        out.append(MazeCheckItem(ok: s.routes == 1, what: p + "One way through", why: s.routes == 1 ? "every cell on it unavoidable" : "\(s.routes - 1) cells on other ways through"))
        out.append(MazeCheckItem(ok: s.softLocks == 0, what: p + "No traps", why: s.softLocks > 0 ? "\(s.softLocks) places she'd bounce forever (a dead end has to end her run)" : s.deadEndCells > 0 ? "\(s.deadEndCells) dead-end cells, each one ending her run" : "no dead ends at all"))
        let hard = meter[1]
        out.append(MazeCheckItem(ok: hard.out, what: p + "Out with the meter at its hardest", why: hard.out ? "refilling \(String(format: "%.2f", hard.rate))× slower, 2 eggs at the door (\(hard.waiting) waits)" : "out of eggs at col \(hard.crash?[0] ?? 0), row \(hard.crash?[1] ?? 0)",
                                    cell: hard.out ? nil : hard.crash.map { [$0[0], $0[1] + $0[2]] }))
        out.append(MazeCheckItem(ok: true, what: p + "Near misses", why: "\(s.nearMisses) on the bot's way (shown; a chunk isn't asked for any)"))
        return out
    }

    /// the checklist's word: all of it ok, something to fix, or still working
    static func state(_ items: [MazeCheckItem], checking: Bool) -> (fix: Int, ready: Bool, working: Bool) {
        let fix = items.filter { $0.ok == false }.count
        let working = checking || items.contains { $0.ok == nil }
        return (fix, fix == 0 && !working, fix == 0 && working)
    }

    /// a word for the chunk list: whether the chunk's checker was satisfied, kept as the list's verdict (cleared)
    static func satisfied(_ items: [MazeCheckItem]) -> Bool? {
        if items.contains(where: { $0.ok == false }) { return false }
        if items.contains(where: { $0.ok == nil }) { return nil }
        return true
    }
}
