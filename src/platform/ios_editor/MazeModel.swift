// A maze open in the editor: a level's gauntlet (the Gauntlet tool) or a maze chunk's block of cells. Its grid (GdDoc), the maze's
// tools and cells, its history, the checker in the background after every edit (GdJob: the solver's way through, where she gets,
// the near misses, the egg meter, then the game's bot playing it; a chunk's: the solver's word on a plain corridor round it, the
// flipped one too with Mirror on), and the play-test's start cell. Saved to the device as it's edited (a MazeStorage: LevelStore's
// grids, or a chunk's file); the level that lays it takes up its width (refitted, as the Mac does); sync sends both. What the
// maze is part of (a level, a chunk) is its MazeHost.
import Combine
import SwiftUI

/// what a maze is part of: the level that lays it, or the chunk that is the maze. The screens ask it for the zone's look, show its
/// messages, and it says what the maze is checked against
@MainActor
protocol MazeHost: AnyObject {
    var zone: String { get }
    var accent: Color { get }
    var message: String? { get set }    // something turned away: an alert
    var notice: String? { get set }     // something that happened: a notice that fades
    /// the level the checker judges it at (a level's number; a chunk's the start of its first range) and that level's text as
    /// edited (nil: the catalog's)
    var number: Int { get }
    var courseText: String? { get }
    /// the maze's rows changed (a level's: refitted to the maze's width)
    func gridChanged(_ name: String, rows: [String])
    /// the maze is to be called `new`: false (and why in `message`) if the host won't have it
    func renamedMaze(_ old: String, to new: String) -> Bool
    /// back to the course (a level's); a chunk has nowhere to go
    func closeMaze()
    /// where the maze sits on its host's course: the first column and the cell size (nil: it doesn't)
    func mazePlace(_ name: String) -> (c0: Int, cell: Double)?
    /// the chunk's Mirror switch (a chunk's maze is judged flipped too when on)
    var mirrorChecked: Bool { get }
}

extension MazeHost {
    var mirrorChecked: Bool { false }
}

/// a maze tool (GridTools)
struct MazeTool: Identifiable, Hashable {
    let id: String
    let label: String
    let symbol: String
    let key: String
    let tint: Color
    var tip = ""
    static let doorTools: Set<String> = ["door", "start"]   // (a chunk has no doors, and no play-test)
    static let all: [MazeTool] = [
        MazeTool(id: "pan", label: "Pan", symbol: "hand.raised.fill", key: "h", tint: .white),
        MazeTool(id: "brush", label: "Brush", symbol: "paintbrush.pointed.fill", key: "b", tint: Neon.straw),
        MazeTool(id: "box", label: "Box", symbol: "rectangle.dashed", key: "r", tint: Neon.cyan),
        MazeTool(id: "door", label: "Doors", symbol: "door.left.hand.open", key: "d", tint: Neon.lime),
        MazeTool(id: "cols", label: "Columns", symbol: "rectangle.split.3x1.fill", key: "c", tint: Neon.lime,
                 tip: "Drag across columns to copy them in; erasing, to take them out"),
        MazeTool(id: "rows", label: "Rows", symbol: "rectangle.split.1x2.fill", key: "w", tint: Neon.lime,
                 tip: "Drag across rows to copy them in; erasing, to take them out"),
        MazeTool(id: "start", label: "Start", symbol: "flag.fill", key: "t", tint: Neon.lime),
    ]
    /// the rail (ToolGrouping.mazeRail): the tools, Columns and Rows sharing a button (Lines, where Columns is)
    static let lines = ToolGrouping.mazeLines
    static func rail(chunk: Bool) -> [MazeTool] {
        ToolGrouping.mazeRail(tools: all.map(\.id).filter { !chunk || !doorTools.contains($0) }).compactMap { item in
            switch item {
            case .tool(let id): return all.first { $0.id == id }
            case .group(let g): return all.first { $0.id == g.members[0] }
            }
        }
    }
    static var rail: [MazeTool] { rail(chunk: false) }
}

@MainActor
final class MazeModel: ObservableObject {
    @Published private(set) var name: String
    let doc: GdDoc
    unowned let host: MazeHost
    let storage: MazeStorage
    private let job = GdJob()
    /// a maze chunk's block (chunk mode)
    var isChunk: Bool { storage.isChunk }
    /// the chunks the last roll laid in it, outlined on the canvas until the next edit
    @Published private(set) var placed: [MazePlaced] = []
    private var placedRevision = -1

    @Published private(set) var scene: GridScene?
    @Published private(set) var history: History?
    @Published private(set) var report: GridReport?
    @Published private(set) var checking = true
    @Published var tool = "brush" { didSet { doc.setTool(tool); if erase { erase = false } } } // (a tool picked: the eraser put away)
    @Published var primary: Character = "#" { didSet { applyCells() } }
    @Published var secondary: Character = "." { didSet { applyCells() } }
    @Published var erase = false { didSet { doc.setErase(erase) } }
    @Published var showWay = true
    @Published var showReach = false
    @Published var showMisses = true
    @Published var historyOpen = false
    weak var canvas: MazeCanvasView? { didSet { canvas?.placed = placed } }
    /// a chunk's checker has a word (satisfied, or not): the chunk list's verdict
    var chunkChecked: ((Bool) -> Void)?
    /// the maze was edited (saved): a chunk's host looks again at whether it can be flipped
    var onEdited: (() -> Void)?
    private var timer: Timer?
    private var sync: AnyCancellable?

    init?(name: String, host: MazeHost, storage: MazeStorage) {
        let text = storage.load(name) ?? ""
        var why = ""
        guard let d = GdDoc(name: name, text: text, chunk: storage.isChunk, error: &why) else {
            host.message = "The \(storage.shown(name)) doesn't read: \(why)"
            return nil
        }
        self.name = name
        self.host = host
        self.storage = storage
        doc = d
        if let h = storage.history(of: name) {
            d.load(h)
            if h.current != text { d.addHistory(label: "Updated from the Mac", time: Date().timeIntervalSince1970, text: h.current) }
            if d.text != text { d.restore(text, label: "Updated from the Mac") } // (the file wins: the history leads to it)
        }
        d.setTool(tool)
        d.setMirrorCheck(host.mirrorChecked)
        applyCells()
        for (n, rows) in storage.otherGrids(except: name) { job.setGrid(n, rows: rows) }
        refresh(edited: false)
        timer = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in Task { @MainActor in self?.poll() } }
        sync = storage.changes.receive(on: DispatchQueue.main).sink { [weak self] change in self?.synced(change.name, change.text) }
        kick()
    }

    /// a chunk's Mirror switch changed: the checker judges it flipped too (or no longer)
    func mirrorCheckChanged() {
        doc.setMirrorCheck(host.mirrorChecked)
        kick()
    }

    deinit { timer?.invalidate() }

    var accent: Color { host.accent }
    var rows: [String] { scene?.rows ?? [] }
    private func applyCells() { doc.setCells(primary, secondary) }

    /// a cell picked for painting: the first cell, the old first the second (a door is only ever first: it moves)
    func pick(_ ch: Character) {
        erase = false // (a cell picked: the eraser put away)
        guard ch != primary else { return }
        if !(MazeCell.named(primary)?.isDoor ?? false) && !(MazeCell.named(ch)?.isDoor ?? false) { secondary = primary }
        primary = ch
        if tool == "pan" || tool == "start" || tool == "door" { tool = "brush" }
    }

    func swapCells() {
        guard !(MazeCell.named(primary)?.isDoor ?? false), !(MazeCell.named(secondary)?.isDoor ?? false) else { return }
        (primary, secondary) = (secondary, primary)
    }

    // MARK: editing

    func pointer(_ phase: Phase, col: Int, row: Int) {
        let changed = doc.pointer(phase, col: col, row: row)
        if phase == .begin || phase == .end || phase == .cancel || changed { refresh(edited: changed || (phase == .end && scene?.revision != doc.revision)) }
        if changed && phase == .end { Haptic.tap() }
        if phase == .end, tool == "start" { scene = doc.scene }
        let err = doc.lastError
        if !err.isEmpty && phase == .end { host.message = err }
    }

    func undo() { if doc.undo() { refresh(edited: true); Haptic.tap() } }
    func redo() { if doc.redo() { refresh(edited: true); Haptic.tap() } }
    func goTo(_ k: Int) { if doc.historyGo(to: k) { refresh(edited: true); Haptic.tap() } }
    func setDescription(_ text: String) {
        if doc.setDescription(text.components(separatedBy: "\n")) { refresh(edited: true) }
    }
    /// a maze the generator made, in place of this one: a step of its history (undo brings the last one back); `chunks`: the ones it
    /// laid, outlined until the maze is edited
    func applyGenerated(_ text: String, label: String, chunks: [MazePlaced] = []) {
        if doc.restore(text, label: label) { refresh(edited: true); canvas?.fit(); showPlaced(chunks) } else { host.message = doc.lastError }
    }

    /// outline the chunks a roll laid (until the next edit)
    func showPlaced(_ chunks: [MazePlaced]) {
        placed = chunks
        placedRevision = doc.revision
        canvas?.placed = chunks
    }

    /// the maze renamed (as typed: "Hall of Saws" is hall_of_saws; a chunk's "sacred wells" is sacred-wells): its file, the level
    /// that lays it and the others here, the history; the Mac at the next sync. False, and why (host.message), if the name won't do
    @discardableResult func rename(_ typed: String) -> Bool {
        let new = storage.typed(typed)
        guard new != name else { return false }
        guard storage.valid(new) else {
            host.message = storage.nameRule
            return false
        }
        guard !storage.taken(new) else {
            host.message = "There's already a \(isChunk ? "chunk" : "maze") called \(storage.shown(new))"
            return false
        }
        let old = name
        keeping?.cancel()
        if let h = doc.historyFile { storage.saveHistory(old, h) } // (kept: it moves with the file)
        guard host.renamedMaze(old, to: new) else { return false }
        guard storage.rename(old, to: new) else { host.message = "There's already a \(isChunk ? "chunk" : "maze") called \(storage.shown(new))"; return false }
        doc.rename(new)
        name = new
        scene = doc.scene
        kick()
        Haptic.success()
        return true
    }

    func clearStart() { doc.setStart(col: -1, row: -1); scene = doc.scene }

    var syncedText: String? { storage.base(of: name) }
    var canRevertToSynced: Bool { syncedText.map { $0 != doc.text } ?? false }
    func revertToSynced() { if let t = syncedText, doc.restore(t, label: "Back to the synced version") { refresh(edited: true); Haptic.success() } }

    /// the report as the canvas draws it: a chunk's seen from the chunk, the corridor round it cut away
    func canvasReport(_ r: GridReport) -> GridReport {
        guard isChunk, let s = scene else { return r }
        return r.windowed(c0: MazeChunkCheck.corridorStart, cols: s.cols, rows: s.rows.count)
    }

    private var lastRows: [String] = []
    private func refresh(edited: Bool) {
        scene = doc.scene
        history = doc.history
        if !placed.isEmpty && doc.revision != placedRevision { placed = []; canvas?.placed = [] } // (edited since: the outlines would lie)
        guard edited, let s = scene else { return }
        storage.save(name, text: doc.text)
        onEdited?()
        if s.rows != lastRows { host.gridChanged(name, rows: s.rows) } // (the level that lays it: refitted to its width)
        lastRows = s.rows
        kick()
        keepHistory()
    }

    private var keeping: Task<Void, Never>?
    private func keepHistory() {
        keeping?.cancel()
        keeping = Task { @MainActor [weak self] in
            try? await Task.sleep(for: .milliseconds(400))
            guard let self, !Task.isCancelled, let h = self.doc.historyFile else { return }
            self.storage.saveHistory(self.name, h)
        }
    }

    /// sync brought this grid a new text: a step of its history
    private func synced(_ n: String, _ text: String) {
        guard n == name, text != doc.text, doc.restore(text, label: "Updated from the Mac") else { return }
        refresh(edited: true)
        host.notice = "The \(storage.shown(name)) updated from the Mac"
    }

    // MARK: the checker

    private func kick() {
        job.kick(doc, level: host.number, levelText: host.courseText)
        withAnimation(.easeOut(duration: 0.2)) { checking = true }
    }

    private func poll() {
        guard let r = job.poll(), r.revision == doc.revision else { return }
        withAnimation(.spring(response: 0.4, dampingFraction: 0.8)) {
            report = r.report
            checking = isChunk ? r.report.mirrorPending(host.mirrorChecked) : r.report.gameRun != true && r.report.ok
        }
        if r.report.gameRun == true, let g = r.report.game { g.out ? Haptic.success() : Haptic.warning() }
        canvas?.report = canvasReport(r.report)
        if isChunk, MazeChunkCheck.satisfied(verdict) != nil { chunkChecked?(MazeChunkCheck.satisfied(verdict) ?? false) }
    }

    /// what the tests want of a maze (the web editor's checklist: gauntlets.js verdict)
    typealias Item = MazeCheckItem
    static let wantMisses = 15   // (more from "hard 6": the report's wantMisses)

    var verdict: [Item] {
        if isChunk { return MazeChunkCheck.items(report, size: (scene?.cols ?? 0, rows.count), mirror: host.mirrorChecked) }
        guard let r = report else { return [Item(ok: nil, what: "A valid grid", why: "checking…")] }
        guard r.ok, let s = r.solution, let meter = r.meter, meter.count > 1 else {
            return [Item(ok: false, what: "A valid grid", why: r.error ?? "it doesn't read")]
        }
        var out: [Item] = [Item(ok: true, what: "A valid grid", why: "\(r.cols ?? 0) × \(r.rows ?? 0), closed, a door each side")]
        out.append(Item(ok: s.reachable, what: "Out, by stacking alone", why: s.reachable ? "\(s.minEggs) eggs at the least" : "no way to the exit: turn on Reach to see where she's stuck"))
        if s.reachable {
            out.append(Item(ok: s.routes == 1, what: "One way through", why: s.routes == 1 ? "every cell on it unavoidable" : "\(s.routes - 1) cells on other ways through"))
            out.append(Item(ok: s.softLocks == 0, what: "No traps", why: s.softLocks > 0 ? "\(s.softLocks) places she'd bounce forever (a dead end has to end her run)" : s.deadEndCells > 0 ? "\(s.deadEndCells) dead-end cells, each one ending her run" : "no dead ends at all"))
            let hard = meter[1]
            let want = r.wantMisses ?? Self.wantMisses
            out.append(Item(ok: hard.out, what: "Out with the meter at its hardest", why: hard.out ? "refilling \(String(format: "%.2f", hard.rate))× slower, 2 eggs at the door (\(hard.waiting) waits)" : "out of eggs at col \(hard.crash?[0] ?? 0), row \(hard.crash?[1] ?? 0)",
                            cell: hard.out ? nil : hard.crash.map { [$0[0], $0[1] + $0[2]] }))
            out.append(Item(ok: s.nearMisses >= want, what: "\(want)+ near misses", why: "\(s.nearMisses) on the bot's way"))
        }
        let same = r.sameAs ?? []
        out.append(Item(ok: same.isEmpty, what: "Unlike every other gauntlet", why: same.isEmpty ? "mirror images included" : "the same as \(same.joined(separator: ", ")) (mirror images count)"))
        for d in r.designs ?? [] { out.append(Item(ok: d.ok, what: d.what, why: d.why)) }
        if let c = r.course {
            out.append(Item(ok: c.placed && c.x1 < c.finish, what: "On level \(host.number)'s course", why: c.placed ? String(format: "%.0f–%.0f m of %.0f m", c.x0, c.x1, c.finish) : "its course doesn't lay it"))
        }
        if r.gameRun != true { out.append(Item(ok: nil, what: "The game's bot gets out", why: "playing it in the real game…")) }
        else if let g = r.game {
            out.append(Item(ok: g.out, what: "The game's bot gets out", why: g.out ? String(format: "%.1f s inside", g.inside) : g.crash.map { String(format: "it crashed at col %.0f, row %.0f", $0[0], $0[1]) } ?? "it never got in",
                            cell: g.crash.map { [Int($0[0].rounded()), Int($0[1].rounded())] }))
            if g.out {
                let shown = g.perfects + g.closeCalls
                out.append(Item(ok: shown >= (r.wantMisses ?? Self.wantMisses) && abs(shown - s.nearMisses) <= 2, what: "The game shows its near misses", why: "\(shown) shown, the solver's \(s.nearMisses) (within 2)"))
            }
        }
        return out
    }

    var verdictState: (text: String, symbol: String, tint: Color) {
        let items = verdict
        if items.contains(where: { $0.ok == false }) { return ("\(items.filter { $0.ok == false }.count) to fix", "exclamationmark.triangle.fill", Neon.amber) }
        if checking || items.contains(where: { $0.ok == nil }) { return ("Checking…", "figure.run", Neon.cyan) }
        return ("Ready", "checkmark.seal.fill", Neon.lime)
    }

    /// a place on the course (metres along, the floor her stack's on): its cell in the maze, centred (a crash: "Fix it")
    func focus(x: Double, y: Double) {
        guard let place = host.mazePlace(name) else { return }
        let c = Int(((x - Double(place.c0) * place.cell) / place.cell).rounded(.down)), b = Int((y / place.cell).rounded())
        canvas?.center(on: c, rows.count - 1 - b)
    }

    /// the play-test from inside the maze: its start cell (the game's row), else its door
    var playStart: (col: Int, row: Int, dir: Int) {
        guard let s = scene?.start, s.count == 2 else { return (-1, -1, 1) }
        return (s[0], rows.count - 1 - s[1], 1)
    }
}
