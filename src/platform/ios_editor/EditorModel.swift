// A level open in the editor, for the screens: the core's document (EdDoc), the scene it draws, its history and
// header, the tool and its options, the bot's verdict in the background (EdBot, after every edit), and the play-test's
// way. Saved to the device as it's edited (LevelStore); sync takes it from there.
import Combine
import SwiftUI

/// where the canvas is looking (the minimap's window): its own object, so panning redraws only what shows it
final class ViewportState: ObservableObject {
    @Published var from: Double = 0
    @Published var to: Double = 40
}

@MainActor
final class EditorModel: ObservableObject {
    /// the level's number; a chunk's body is judged as the level its first range starts at
    private(set) var number: Int
    /// a chunk's body open instead of a level (the chunk editor: ChunkSession), else nil
    let chunk: ChunkSession?
    private(set) var doc: EdDoc
    let registry = Builtin.registry
    private let bot = EdBot()
    private let store: LevelStore
    private let chunkStore: ChunkStore

    @Published private(set) var scene: CourseScene?
    @Published private(set) var history: History?
    @Published private(set) var meta: LevelMeta?
    @Published private(set) var botReport: BotReport?
    @Published private(set) var botBusy = false
    @Published private(set) var botStale = true
    // (a tool picked puts the eraser away: the eraser is a tool of its own, taking whatever's under the finger)
    @Published var tool = "wall" { didSet { if tool != oldValue { previousTool = oldValue }; doc.setTool(tool); canvas?.tool = tool; if erase { erase = false } } }
    private var previousTool = "wall"
    @Published var palette: CGPoint?            // the Pencil Pro's squeeze: a palette of the tools there (canvas points)
    @Published var kind = "crate" { didSet { doc.setKind(kind) } }
    @Published var erase = false { didSet { doc.setErase(erase) } }
    @Published var gauntletPick = "" { didSet { doc.setGauntletPick(gauntletPick) } }
    @Published var message: String?            // something turned away: an alert
    @Published var notice: String?             // something that happened (sync): a notice that fades
    @Published var focusedNote: CourseNote? { didSet { if let n = focusedNote { canvas?.center(on: n.x) } } }
    @Published var playTrace: [CGPoint] = []
    @Published var showWalk = true
    @Published var showBot = true
    @Published var showNotes = true
    @Published private(set) var picked: Hit?
    let viewport = ViewportState()
    weak var canvas: CourseCanvasView?
    private var timer: Timer?
    private var sync: AnyCancellable?
    @Published private(set) var chunkInfo: ChunkInfo?           // (a chunk's body: its pads, whether it can be flipped, what breaks its rules)
    @Published private(set) var chunksUsed: [ChunkUse] = []     // (a level: the chunks its last roll placed, where)

    init?(number: Int, store: LevelStore? = nil, chunks: ChunkStore? = nil) {
        let store = store ?? LevelStore.shared, chunks = chunks ?? ChunkStore.shared
        guard let text = store.text(of: number) else { return nil }
        var why = ""
        guard let d = EdDoc(number: number, text: text, grids: store.grids, error: &why) else {
            NSLog("[CuckooEditor] level %d doesn't read: %@", number, why)
            return nil
        }
        self.number = number
        self.store = store
        chunkStore = chunks
        chunk = nil
        doc = d
        for (name, rows) in store.grids { d.setGrid(name, rows: rows) }
        d.setTool(tool)
        if let h = store.history(of: number) { // (its history, as it was left: a change made since, by sync, a step on top)
            d.load(h)
            if h.current != text { d.addHistory(redo: false, label: "Updated from the Mac", time: Date().timeIntervalSince1970, at: -1, text: h.current) }
        }
        refresh(edited: false)
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in Task { @MainActor in self?.pollBot() } }
        sync = store.changes.receive(on: DispatchQueue.main).sink { [weak self] change in self?.synced(change.number, change.text) }
        if let v = store.verdict(number) { botStale = false; botBusy = false; _ = v }
        kickBot(delay: 50)
    }

    /// a chunk's body open (the chunk editor): the body is the course, numbered by the start of the chunk's first range,
    /// kept to a chunk's rules (EditorDoc::setChunkMode); the header is the ChunkSession's
    init?(chunk id: String, chunks: ChunkStore? = nil, store: LevelStore? = nil) {
        let store = store ?? LevelStore.shared, chunks = chunks ?? ChunkStore.shared
        guard let text = chunks.text(of: id) else { return nil }
        let file = Chunks.parse(id, text)
        guard file.ok, !file.isMaze, let body = file.body else {
            NSLog("[CuckooEditor] chunk %@ doesn't open: %@", id, file.error ?? "it isn't a course chunk")
            return nil
        }
        let first = file.first ?? 1
        var why = ""
        guard let d = EdDoc(number: first, text: body, error: &why) else {
            NSLog("[CuckooEditor] chunk %@'s body doesn't read: %@", id, why)
            return nil
        }
        number = first
        self.store = store
        chunkStore = chunks
        chunk = ChunkSession(id: id, header: file.header)
        doc = d
        d.setChunk(file.header.mirror ? 2 : 1)
        d.setTool(tool)
        if let h = chunks.history(of: id) { // (its history, as it was left: a change made since, by a restore, a step on top)
            d.load(h)
            if h.current != body { d.addHistory(redo: false, label: "Changed outside the editor", time: Date().timeIntervalSince1970, at: -1, text: h.current) }
        }
        refresh(edited: false)
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in Task { @MainActor in self?.pollBot() } }
        sync = chunks.changes.receive(on: DispatchQueue.main).sink { [weak self] change in self?.chunkChanged(change.id, change.text) }
        botStale = true
        kickBot(delay: 50)
    }

    deinit { timer?.invalidate() }

    var zone: String { scene?.zone ?? "city" }
    var accent: Color { ZoneLook.of(zone).accent }
    var title: String { chunk?.title ?? LevelStore.title(of: doc.text, number: number) }
    var notes: [CourseNote] { scene?.notes ?? [] }

    // MARK: editing

    func pointer(_ phase: Phase, x: Double, y: Double) {
        let changed = doc.pointer(phase, x: x, y: y)
        if phase == .begin || phase == .end || phase == .cancel || changed { refresh(edited: changed || phase == .end) }
        if changed && phase == .end { Haptic.tap() }
        if phase == .end || phase == .begin {
            let p = doc.picked
            picked = p >= 0 ? scene?.items.first { $0.i == p }.map { Hit(what: "item", i: $0.i, type: $0.type, v: $0.v, name: nil, col: nil, h: nil, ceil: nil, top: nil, kind: nil) } : nil
        }
        let err = doc.lastError
        if !err.isEmpty && phase == .end { message = err }
    }

    /// the Pencil's double-tap, as its settings say: the eraser on and off, or back to the tool before
    func pencilTap(_ action: UIPencilPreferredAction) {
        switch action {
        case .switchEraser: withAnimation(.spring(response: 0.3)) { erase.toggle() }
        case .switchPrevious: withAnimation(.spring(response: 0.3)) { tool = previousTool }
        case .showColorPalette, .showInkAttributes, .showContextualPalette: withAnimation(.spring(response: 0.35, dampingFraction: 0.75)) { palette = CGPoint(x: (canvas?.bounds.midX ?? 200), y: (canvas?.bounds.midY ?? 200)) }
        default: return
        }
        Haptic.tap()
    }

    func undo() { if doc.undo() { refresh(edited: true); Haptic.tap() } }
    func redo() { if doc.redo() { refresh(edited: true); Haptic.tap() } }
    func copy() { doc.copy() }
    func cut() { if doc.cut() { refresh(edited: true) } }
    func paste() {
        let col = Int(((viewport.from + viewport.to) / 2) / registry.cell)
        if doc.paste(at: col) { refresh(edited: true) }
    }
    func deleteSelection() { if doc.deleteSelection() { refresh(edited: true) } }
    func setPickedParam(_ i: Int, _ v: Double) {
        if doc.setPickedParam(i, v) { refresh(edited: true); picked = scene?.items.first { $0.i == doc.picked }.map { Hit(what: "item", i: $0.i, type: $0.type, v: $0.v, name: nil, col: nil, h: nil, ceil: nil, top: nil, kind: nil) } }
    }
    func removePicked() { if doc.removePicked() { picked = nil; refresh(edited: true) } }
    func setMeta(_ key: String, _ value: String) {
        if doc.setMeta(key, value) { refresh(edited: true) } else if !doc.lastError.isEmpty { message = doc.lastError }
    }
    func placeGauntlet(_ name: String) {
        let col = Int(((viewport.from + viewport.to) / 2) / registry.cell)
        if doc.placeGauntlet(name, col: col) { refresh(edited: true) } else { message = doc.lastError.isEmpty ? "No room for the \(name) there" : doc.lastError }
    }
    func removeGauntlet(_ name: String) { if doc.removeGauntlet(name) { refresh(edited: true) } }
    /// its maze renamed (MazeModel.rename): the course and its history lay it by the new name
    func renameGauntlet(_ from: String, to: String) -> Bool {
        guard doc.renameGauntlet(from, to: to) else { message = doc.lastError; return false }
        refresh(edited: true)
        return true
    }
    func setStart(_ x: Double?) { doc.setStart(x); refresh(edited: false) }

    // MARK: the generators

    /// the Level generator: the course rolled anew (the game's generator, a new seed each press; one undo step each)
    func rollLevel() {
        guard chunk == nil else { return }
        if maze != nil { maze = nil }
        let seed = UInt32.random(in: 1...UInt32.max - 100)
        // (every chunk goes in: the generator keeps the ones whose ranges cover this level; ones that don't read are left out)
        let bundle = chunkStore.rollBundle()
        let ok = bundle.text.isEmpty ? doc.regenerate(seed: seed, hazards: courseHazards, density: Int(genDensity))
                                     : doc.regenerate(seed: seed, hazards: courseHazards, density: Int(genDensity), chunks: bundle.text)
        if ok {
            picked = nil
            refresh(edited: true)
            Haptic.success()
            canvas?.center(on: 0, animated: true)
            if let words = ChunkWords.rollNotice(skipped: bundle.skipped, note: doc.chunksNote, placed: chunksUsed.map(\.id)) { notice = words }
        } else if !doc.lastError.isEmpty { message = doc.lastError }
    }

    /// the Level generator's density: 0 as designed, 10 super hard (kept between launches)
    @Published var genDensity: Double = UserDefaults.standard.object(forKey: "genDensity") as? Double ?? 0 { didSet { UserDefaults.standard.set(genDensity, forKey: "genDensity") } }
    /// the Gauntlet generator's size (kept between launches)
    @Published var mazeDensity: Double = UserDefaults.standard.object(forKey: "mazeDensity") as? Double ?? 0 { didSet { UserDefaults.standard.set(mazeDensity, forKey: "mazeDensity") } }
    @Published var genWidth: Double = UserDefaults.standard.object(forKey: "genWidth") as? Double ?? 170 { didSet { UserDefaults.standard.set(genWidth, forKey: "genWidth") } }
    @Published var genFloors: Int = UserDefaults.standard.object(forKey: "genFloors") as? Int ?? 3 { didSet { UserDefaults.standard.set(genFloors, forKey: "genFloors") } }
    @Published private(set) var generating = false

    /// the generators' switches: which hazards a roll may lay (the registry's generated ones; all on at first, kept)
    var generatedHazards: [HazardType] { let ids = HazardSwitches.course(registry.hazards.map { ($0.id, $0.generated ?? false) }); return registry.hazards.filter { ids.contains($0.id) } }
    var mazeHazardIDs: [String] { HazardSwitches.maze(registry: registry.hazards.map(\.id)) } // (a maze's cells)
    @Published var genOff: Set<String> = Set(UserDefaults.standard.stringArray(forKey: "genOff") ?? []) { didSet { UserDefaults.standard.set(Array(genOff), forKey: "genOff") } }
    @Published var mazeGenOff: Set<String> = Set(UserDefaults.standard.stringArray(forKey: "mazeGenOff") ?? []) { didSet { UserDefaults.standard.set(Array(mazeGenOff), forKey: "mazeGenOff") } }
    var courseHazards: [String] { HazardSwitches.rolled(generatedHazards.map(\.id), off: genOff) }
    var mazeHazards: [String] { HazardSwitches.rolled(mazeHazardIDs, off: mazeGenOff) }

    /// the Gauntlet generator: a maze that passes the checklist, made off the main thread; the level's maze replaced by
    /// it (a step of the maze's history), or, with none yet, put in at 40% of the way as a new one; the maze editor opens
    func generateMaze() {
        guard !generating else { return }
        generating = true
        let level = number, text = doc.text, width = Int(genWidth), floors = genFloors, hazards = mazeHazards, density = Int(mazeDensity)
        let seed = UInt32.random(in: 1...UInt32.max - 1000)
        // (every maze chunk goes in: the generator keeps the ones whose ranges cover this level; ones that don't read are left out)
        let bundle = chunkStore.mazeBundle()
        Task { [weak self] in
            let out = await Task.detached(priority: .userInitiated) {
                Builtin.generateMaze(level: level, levelText: text, width: width, floors: floors, seed: seed, hazards: hazards, density: density, chunks: bundle.text)
            }.value
            self?.generated(out, skipped: bundle.skipped)
        }
    }

    private func generated(_ out: MazeGenOut, skipped: [String] = []) {
        generating = false
        guard out.ok else {
            message = "No maze passed the checklist in \(out.tries) tries (the last: \(out.why)). Press again, or change its size."
            Haptic.warning()
            return
        }
        let placed = out.chunks ?? []
        let label = ChunkWords.mazeLabel(seed: out.seed, placed: placed.map(\.id), dropped: out.dropped ?? [], reduced: out.reduced ?? [])
        if meta?.gauntlet != nil {
            openMaze()
            maze?.applyGenerated(out.text, label: label, chunks: placed)
        } else {
            var name = "maze_\(number)", k = 2
            let taken = Set(store.grids.keys).union(meta?.gauntlets ?? [])
            while taken.contains(name) { name = "maze_\(number)_\(k)"; k += 1 }
            store.saveGrid(name, text: out.text)
            guard doc.setGrid(name, rows: LevelStore.rows(of: out.text)) else { message = doc.lastError; return }
            let at = Int((scene?.length ?? doc.scene?.length ?? 300) * 0.4 / registry.cell)
            guard doc.placeGauntlet(name, col: at) else { message = doc.lastError.isEmpty ? "No room for a maze there" : doc.lastError; return }
            refresh(edited: true)
            openMaze()
            maze?.showPlaced(placed)
        }
        if let words = ChunkWords.rollNotice(skipped: skipped, note: out.chunkNote ?? "", placed: placed.map(\.id)) { notice = words }
        Haptic.success()
    }

    // MARK: the maze (the Gauntlet tool: the level's gauntlet's grid, edited)

    @Published private(set) var maze: MazeModel?
    /// the level's maze open for editing (its gauntlet's grid); nil: the level lays none
    func openMaze() {
        guard let name = meta?.gauntlet else { return }
        if maze?.name != name { maze = MazeModel(name: name, host: self, storage: GauntletStorage(store)) }
    }
    func closeMaze() {
        maze = nil
        if let name = meta?.gauntlet, let s = scene, let g = s.gauntlets.first(where: { $0.name == name }) { canvas?.center(on: Double(g.c0 + g.c1) / 2 * s.cell) }
    }
    /// its grid edited: the level's span refitted to its width (as the Mac's save does), the course redrawn
    func gridChanged(_ name: String, rows: [String]) {
        let before = doc.text
        guard doc.setGrid(name, rows: rows) else { return }
        refresh(edited: doc.text != before)
        if doc.text == before { botStale = true; kickBot() } // (the same course, a new maze: the bot plays it again)
    }

    private func refresh(edited: Bool) {
        scene = doc.scene
        history = doc.history
        meta = doc.meta
        if chunk != nil { chunkInfo = doc.chunkInfo } else { chunksUsed = doc.chunksUsed }
        if edited {
            if let c = chunk { c.save(body: doc.text, to: chunkStore) } else { store.save(number, text: doc.text) }
            botStale = true
            kickBot()
            keepHistory()
        }
    }

    private var keeping: Task<Void, Never>?
    /// the history saved on the device (a moment after the last change: a drag's moves are one save)
    private func keepHistory() {
        keeping?.cancel()
        keeping = Task { @MainActor [weak self] in
            try? await Task.sleep(for: .milliseconds(400))
            guard let self, !Task.isCancelled, let h = self.doc.historyFile else { return }
            if let c = self.chunk { if self.chunkStore.exists(c.id) { self.chunkStore.saveHistory(c.id, h) } } // (not for one taken out meanwhile)
            else { self.store.saveHistory(self.number, h) }
        }
    }

    // MARK: history and revert

    @Published var historyOpen = false
    /// to just after the k-th step of the history (0: before them all)
    func goTo(_ k: Int) { if doc.historyGo(to: k) { refresh(edited: true); Haptic.tap() } }
    var syncedText: String? { chunk == nil ? store.baseText(of: number) : nil }
    var originalText: String? { chunk == nil ? Builtin.level(number) : nil }
    var canRevertToSynced: Bool { syncedText.map { $0 != doc.text } ?? false }
    var canRevertToOriginal: Bool { originalText.map { $0 != doc.text } ?? false }
    func revertToSynced() { if let t = syncedText, doc.restore(t, label: "Back to the synced version") { refresh(edited: true); Haptic.success() } }
    func revertToOriginal() { if let t = originalText, doc.restore(t, label: "Back to the game's original") { refresh(edited: true); Haptic.success() } }
    /// the hash of the level after each step (undo oldest first, then redo next first): where it was synced
    func stepHashes() -> (start: String, undo: [String], redo: [String]) {
        guard let h = doc.historyFile else { return ("", [], []) }
        let undo = h.undo.indices.map { i in textHash(i + 1 < h.undo.count ? h.undo[i + 1].text : h.current) }
        return (textHash(h.undo.first?.text ?? h.current), undo, h.redo.map { textHash($0.text) })
    }
    var syncedHash: String? { chunk == nil ? store.state.levels[String(number)]?.base : nil }

    /// sync brought this level a new text (the Mac's, or a merge): a step of its history, undoable like any
    private func synced(_ n: Int, _ text: String) {
        guard n == number, text != doc.text, doc.restore(text, label: "Updated from the Mac") else { return }
        picked = nil
        refresh(edited: true)
        notice = "Updated from the Mac"
    }

    // MARK: the bot

    func kickBot(delay: Int = 300) {
        bot.kick(doc, delayMs: delay)
        botBusy = true
    }

    private func pollBot() {
        if let r = bot.poll(), r.revision == doc.revision {
            withAnimation(.spring(response: 0.4, dampingFraction: 0.7)) {
                botReport = r.report
                botStale = false
                botBusy = false
            }
            if let g = r.report.game {
                if let c = chunk { // (a chunk cleared only if its flip is too, when it has one)
                    let cleared = ChunkVerdict.cleared(plain: g.cleared, flipped: r.report.flipped.map { $0.game?.cleared ?? false })
                    chunkStore.setVerdict(c.id, cleared: cleared, seconds: g.seconds, key: ChunkStore.verdictKey(first: number, flipped: r.report.flipped != nil, body: doc.text))
                    cleared ? Haptic.success() : Haptic.warning()
                } else {
                    store.setVerdict(number, cleared: g.cleared, seconds: g.seconds, text: doc.text)
                    g.cleared ? Haptic.success() : Haptic.warning()
                }
            }
        } else if !bot.busy && botBusy && !botStale {
            botBusy = false
        }
    }

    var botVerdict: (text: String, symbol: String, tint: Color) {
        if botBusy || botStale { return ("Bot playing…", "figure.run", Neon.cyan) }
        guard let r = botReport else { return ("Bot", "figure.run", Neon.cyan) }
        guard r.ok, let g = r.game else { return ("Doesn't read", "exclamationmark.triangle.fill", Neon.red) }
        return g.cleared ? (String(format: "Cleared %.0f s", g.seconds), "checkmark.seal.fill", Neon.lime)
                         : (String(format: "Crashed at %.0f m", g.x), "xmark.octagon.fill", Neon.red)
    }

    /// a chunk with Mirror on: what the bot made of it flipped (nil: not flipped, or still playing)
    var botFlipVerdict: (text: String, symbol: String, tint: Color)? {
        guard chunk != nil, !(botBusy || botStale), let f = botReport?.flipped else { return nil }
        guard f.ok, let g = f.game else { return ("Flipped: doesn't read", "exclamationmark.triangle.fill", Neon.red) }
        return (ChunkVerdict.flippedWords(cleared: g.cleared, seconds: g.seconds, x: g.x), g.cleared ? "checkmark.seal.fill" : "xmark.octagon.fill", g.cleared ? Neon.lime : Neon.red)
    }

    func jumpToCrash() { if let g = botReport?.game, !g.cleared { canvas?.center(on: g.x) } }

    // MARK: a chunk's header (the settings card)

    /// a chunk's settings changed (ranges, weight, uses, mirror, description): its file saved. A first range that moved
    /// renumbers the body (its zone, difficulty and bot); Mirror on or off asks the checker and the bot to judge it flipped
    func setHeader(_ change: (inout ChunkHeader) -> Void) {
        guard let c = chunk else { return }
        var h = c.header
        change(&h)
        guard h != c.header else { return }
        let mirrorChanged = h.mirror != c.header.mirror
        c.header = h
        var judge = mirrorChanged
        if h.first != number { number = h.first; doc.setNumber(number); judge = true }
        doc.setChunk(h.mirror ? 2 : 1)
        c.save(body: doc.text, to: chunkStore)
        refresh(edited: false)
        if judge { botStale = true; kickBot() }
    }

    /// the chunk under another name; false (and why) if it's taken or isn't one
    func renameChunk(_ typed: String) -> Bool {
        guard let c = chunk else { return false }
        let id = ChunkNames.typed(typed)
        guard id != c.id else { return true }
        guard Chunks.isValid(id) else { message = "A chunk's name: lower case letters, digits, '-' and '_'."; return false }
        guard chunkStore.rename(c.id, to: id) else { message = "There's a chunk called \(id) already."; return false }
        c.id = id
        if let h = doc.historyFile { chunkStore.saveHistory(id, h) } // (at once: the editor opens afresh under the new name)
        return true
    }

    /// a restore (or its undo) changed this chunk's file: the body, the settings and the ranges are taken up, as a step of
    /// its history
    private func chunkChanged(_ id: String, _ text: String?) {
        guard let c = chunk, id == c.id, let text else { return }
        let f = Chunks.parse(id, text)
        guard f.ok, !f.isMaze, let body = f.body else { return }
        c.header = f.header
        if let first = f.first, first != number { number = first; doc.setNumber(first) }
        doc.setChunk(f.header.mirror ? 2 : 1)
        if body != doc.text { _ = doc.restore(body, label: "Restored from a backup") }
        picked = nil
        refresh(edited: true)
        botStale = true
        kickBot()
        notice = "Restored from a backup"
    }

    /// bands over the course: a level's last roll's chunks (their ids, flipped ones marked), a chunk's pads
    var bands: [CanvasBand] {
        if chunk != nil {
            guard let info = chunkInfo, let cell = scene?.cell else { return [] }
            return ChunkPads.bands(reach: info.reach, runIn: info.runIn, runOut: info.runOut, cell: cell).map { CanvasBand(x0: $0.x0, x1: $0.x1, label: $0.label, hex: "#8fa0c8", stripped: true) }
        }
        return chunksUsed.map { CanvasBand(x0: $0.x0, x1: $0.x1, label: $0.id + ($0.mirrored ? " \u{21C4}" : ""), hex: "#ff2bd6", stripped: false) }
    }
}

/// a level is what its maze is part of: the maze is checked against the level, refits the level to its width, closes back to the course
extension EditorModel: MazeHost {
    var courseText: String? { doc.text }
    func renamedMaze(_ old: String, to new: String) -> Bool { renameGauntlet(old, to: new) }
    func mazePlace(_ name: String) -> (c0: Int, cell: Double)? {
        guard let s = scene, let g = s.gauntlets.first(where: { $0.name == name }) else { return nil }
        return (g.c0, s.cell)
    }
}
