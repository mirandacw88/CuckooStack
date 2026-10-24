// The editor's C++ core (cs_editor.h) as Swift: a level open for editing (EdDoc), the bot in the background (EdBot),
// and the game's own levels, grids and registry. Strings come back from C as the caller's (ed_free).
import Foundation

enum Phase: Int32 { case begin = 0, move = 1, end = 2, cancel = 3 }

/// the game's own: its levels, grids, the hazards' registry and the course's rules
enum Builtin {
    static let registry: Registry = decodeJSON(Registry.self, takeString(ed_registry_json())) ?? Registry.empty
    static var numbers: [Int] { decodeJSON([Int].self, takeString(ed_builtin_numbers_json())) ?? [] }
    static func level(_ n: Int) -> String? { takeString(ed_builtin_level(Int32(n))) }
    static var grids: [String: [String]] { decodeJSON([String: [String]].self, takeString(ed_builtin_grids_json())) ?? [:] }
    /// the game's own chunks (assets/chunks as the app was built): each id and its file's text
    static var chunks: [String: String] { decodeJSON([String: String].self, takeString(ck_builtin_json())) ?? [:] }
    static func bake(_ n: Int) -> String { takeString(ed_bake_level(Int32(n))) ?? "" }
    static func merge(_ number: Int, base: String, mine: String, theirs: String) -> MergeResult {
        decodeJSON(MergeResult.self, takeString(ed_merge3(Int32(number), base, mine, theirs))) ?? MergeResult(ok: false, text: mine, conflicts: ["merge"], error: "")
    }
    static func check(_ number: Int, _ text: String) -> (ok: Bool, error: String) {
        let r = decodeJSON(CheckResult.self, takeString(ed_check_text(Int32(number), text)))
        return (r?.ok ?? false, r?.error ?? "")
    }
    static func useGrid(_ name: String, rows: [String]) { ed_set_dev_grid(name, rows.joined(separator: "\n")) }
    /// a gauntlet's name: lower case letters, digits and _, a letter first (Gauntlet.h's validGauntletName)
    static func validGauntletName(_ name: String) -> Bool { ed_valid_gauntlet_name(name) != 0 }
    static func gauntletName(typed: String) -> String { GridNames.typed(typed) }
    static func gauntletShown(_ name: String) -> String { GridNames.shown(name) }
    /// a level file's text laying gauntlet `from` as `to` (renameGauntletIn)
    static func renameGauntlet(in text: String, from: String, to: String) -> String { takeString(ed_rename_in_text(text, from, to)) ?? text }
    /// the Gauntlet generator: a maze that passes the checklist (its file's text), or why not (MazeGen.h; a second or
    /// so at the most: off the main thread)
    static func generateMaze(level: Int, levelText: String?, width: Int, floors: Int, seed: UInt32, hazards: [String], density: Int = 0) -> MazeGenOut {
        decodeJSON(MazeGenOut.self, takeString(gd_generate(Int32(level), levelText, Int32(width), Int32(floors), seed, HazardSwitches.argument(hazards), Int32(density))))
            ?? MazeGenOut(ok: false, text: "", seed: seed, tries: 0, why: "the generator didn't answer")
    }
    /// ...with the designer's maze chunks (`chunks`: their files under "=== id" lines, ChunkStore.mazeBundle): the ones whose
    /// ranges cover the level are laid, and the maze says which went where
    static func generateMaze(level: Int, levelText: String?, width: Int, floors: Int, seed: UInt32, hazards: [String], density: Int = 0, chunks: String) -> MazeGenOut {
        decodeJSON(MazeGenOut.self, takeString(gd_generate_chunks(Int32(level), levelText, Int32(width), Int32(floors), seed, HazardSwitches.argument(hazards), Int32(density), chunks)))
            ?? MazeGenOut(ok: false, text: "", seed: seed, tries: 0, why: "the generator didn't answer")
    }
}

/// a maze chunk the generator laid: the grid's columns c0...c1 and rows r0...r1 (from the top), the floor it is on (0 at the street),
/// and whether the grid shows it mirrored
struct MazePlaced: Decodable, Hashable {
    let id: String
    let floor, c0, c1, r0, r1: Int
    let mirrored: Bool
}

struct MazeGenOut: Decodable {
    let ok: Bool; let text: String; let seed: UInt32; let tries: Int; let why: String
    var chunks: [MazePlaced]? = nil
    /// chunks the generator gave up on because no maze passed with them: the notice, the ids left out and the ids laid in
    /// smaller numbers (MazeGenResult; absent from an older core's answer)
    var chunkNote: String? = nil
    var dropped: [String]? = nil
    var reduced: [String]? = nil
}

/// a level open for editing: its course, tools, history and checker (EditorDoc, EditorTools)
final class EdDoc {
    let ptr: OpaquePointer

    init?(number: Int, text: String, grids: [String: [String]] = [:], error: inout String) {
        var why: UnsafeMutablePointer<CChar>? = nil
        let gridText = grids.map { ([$0.key] + $0.value).joined(separator: "\n") }.joined(separator: "\n\n")
        guard let p = ed_open_grids(Int32(number), text, gridText, &why) else {
            error = takeString(why) ?? "doesn't read"
            return nil
        }
        ptr = p
    }
    deinit { ed_close(ptr) }

    var text: String { takeString(ed_text(ptr)) ?? "" }
    var number: Int { Int(ed_number(ptr)) }
    var revision: Int { Int(ed_revision(ptr)) }
    var lastError: String { takeString(ed_last_error(ptr)) ?? "" }
    @discardableResult func setGrid(_ name: String, rows: [String]) -> Bool { ed_set_grid(ptr, name, rows.joined(separator: "\n")) != 0 }

    func setTool(_ t: String) { ed_set_tool(ptr, t) }
    func setKind(_ k: String) { ed_set_kind(ptr, k) }
    func setErase(_ on: Bool) { ed_set_erase(ptr, on ? 1 : 0) }
    func setGauntletPick(_ name: String) { ed_set_gauntlet_pick(ptr, name) }
    @discardableResult func pointer(_ phase: Phase, x: Double, y: Double) -> Bool { ed_pointer(ptr, phase.rawValue, x, y) != 0 }
    @discardableResult func undo() -> Bool { ed_undo(ptr) != 0 }
    @discardableResult func redo() -> Bool { ed_redo(ptr) != 0 }
    @discardableResult func copy() -> Bool { ed_copy(ptr) != 0 }
    @discardableResult func cut() -> Bool { ed_cut(ptr) != 0 }
    @discardableResult func paste(at col: Int) -> Bool { ed_paste(ptr, Int32(col)) != 0 }
    @discardableResult func deleteSelection() -> Bool { ed_delete_selection(ptr) != 0 }
    func select(_ c0: Int, _ c1: Int) { ed_select(ptr, Int32(c0), Int32(c1)) }
    var picked: Int { Int(ed_picked(ptr)) }
    func pick(_ i: Int) { ed_pick(ptr, Int32(i)) }
    @discardableResult func setPickedParam(_ param: Int, _ value: Double) -> Bool { ed_set_picked_param(ptr, Int32(param), value) != 0 }
    @discardableResult func removePicked() -> Bool { ed_remove_picked(ptr) != 0 }
    @discardableResult func setMeta(_ key: String, _ value: String) -> Bool { ed_set_meta(ptr, key, value) != 0 }
    /// the Level generator: a new course from `seed`, one step in the history
    @discardableResult func regenerate(seed: UInt32, hazards: [String], density: Int = 0) -> Bool { ed_regenerate(ptr, seed, HazardSwitches.argument(hazards), Int32(density)) != 0 }
    /// the Level generator with the designer's chunks (`chunks`: their files under "=== id" lines, ChunkStore.rollBundle): the
    /// ones whose ranges cover this level are placed; false, and why in lastError, if one doesn't read
    @discardableResult func regenerate(seed: UInt32, hazards: [String], density: Int = 0, chunks: String) -> Bool {
        ed_regenerate_chunks(ptr, seed, HazardSwitches.argument(hazards), Int32(density), chunks) != 0
    }
    /// the chunks the last roll placed and where (empty once anything has changed since)
    var chunksUsed: [ChunkUse] { decodeJSON([ChunkUse].self, takeString(ed_chunks_used_json(ptr))) ?? [] }
    /// the notice for chunks the last roll left out because no course passed with them ("" for none)
    var chunksNote: String { takeString(ed_chunks_note(ptr)) ?? "" }
    /// a chunk's body open (0 a level, 1 a chunk, 2 a chunk with its Mirror switch on: judged flipped too)
    func setChunk(_ mode: Int) { ed_set_chunk(ptr, Int32(mode)) }
    /// the body renumbered (its first range moved); not an edit
    @discardableResult func setNumber(_ n: Int) -> Bool { ed_set_number(ptr, Int32(n)) != 0 }
    var chunkInfo: ChunkInfo? { decodeJSON(ChunkInfo.self, takeString(ed_chunk_json(ptr))) }
    /// the level as `text` reads, as one step of its history (a revert, sync's update)
    @discardableResult func restore(_ text: String, label: String) -> Bool { ed_restore(ptr, text, label) != 0 }
    @discardableResult func historyGo(to k: Int) -> Bool { ed_history_goto(ptr, Int32(k)) != 0 }
    var historyFile: HistoryFile? { decodeJSON(HistoryFile.self, takeString(ed_history_export(ptr))) }
    /// a kept history brought back (its redo steps next first)
    func load(_ h: HistoryFile) {
        ed_history_clear(ptr)
        for s in h.undo { ed_history_add(ptr, 0, s.label, Int64(s.time), s.at, s.text) }
        for s in h.redo { ed_history_add(ptr, 1, s.label, Int64(s.time), s.at, s.text) }
    }
    func addHistory(redo: Bool, label: String, time: Double, at: Double, text: String) { ed_history_add(ptr, redo ? 1 : 0, label, Int64(time), at, text) }
    @discardableResult func placeGauntlet(_ name: String, col: Int) -> Bool { ed_place_gauntlet(ptr, name, Int32(col)) != 0 }
    @discardableResult func removeGauntlet(_ name: String) -> Bool { ed_remove_gauntlet(ptr, name) != 0 }
    @discardableResult func renameGauntlet(_ from: String, to: String) -> Bool { ed_rename_gauntlet(ptr, from, to) != 0 }
    var start: Double? { let s = ed_start(ptr); return s < 0 ? nil : s }
    func setStart(_ x: Double?) { ed_set_start(ptr, x ?? -1) }

    var scene: CourseScene? { decodeJSON(CourseScene.self, takeString(ed_scene_json(ptr))) }
    func hit(x: Double, y: Double) -> Hit? { decodeJSON(Hit.self, takeString(ed_hit_json(ptr, x, y))) }
    var history: History? { decodeJSON(History.self, takeString(ed_history_json(ptr))) }
    var meta: LevelMeta? { decodeJSON(LevelMeta.self, takeString(ed_meta_json(ptr))) }
}

/// the bot in the background (BotJob): kicked after an edit, polled for its report
final class EdBot {
    let ptr: OpaquePointer = ed_bot_new()
    deinit { ed_bot_free(ptr) }
    func kick(_ doc: EdDoc, delayMs: Int = 300) { ed_bot_kick(ptr, doc.ptr, Int32(delayMs)) }
    func cancel() { ed_bot_cancel(ptr) }
    var busy: Bool { ed_bot_busy(ptr) != 0 }
    func poll() -> (report: BotReport, revision: Int)? {
        var rev: Int32 = -1
        guard let json = takeString(ed_bot_poll(ptr, &rev)), let r = decodeJSON(BotReport.self, json) else { return nil }
        return (r, Int(rev))
    }
}

/// a gauntlet's maze open for editing: its grid, tools and history (GridDoc, GridTools); cells (column, row index from
/// the top)
final class GdDoc {
    let ptr: OpaquePointer

    /// `chunk`: a maze chunk's block of cells (chunk mode: no doors, as small as 3 × 3; its checker a test corridor round it)
    init?(name: String, text: String, chunk: Bool = false, error: inout String) {
        var why: UnsafeMutablePointer<CChar>? = nil
        guard let p = gd_open_mode(name, text, chunk ? 1 : 0, &why) else {
            error = takeString(why) ?? "doesn't read"
            return nil
        }
        ptr = p
    }
    deinit { gd_close(ptr) }

    var text: String { takeString(gd_text(ptr)) ?? "" }
    var revision: Int { Int(gd_revision(ptr)) }
    var lastError: String { takeString(gd_last_error(ptr)) ?? "" }
    @discardableResult func rename(_ name: String) -> Bool { gd_rename(ptr, name) != 0 }
    func setTool(_ t: String) { gd_set_tool(ptr, t) }
    func setCells(_ primary: Character, _ secondary: Character) {
        gd_set_cells(ptr, Int32(primary.asciiValue ?? 46), Int32(secondary.asciiValue ?? 35))
    }
    func setErase(_ on: Bool) { gd_set_erase(ptr, on ? 1 : 0) }
    @discardableResult func pointer(_ phase: Phase, col: Int, row: Int) -> Bool { gd_pointer(ptr, phase.rawValue, Int32(col), Int32(row)) != 0 }
    @discardableResult func undo() -> Bool { gd_undo(ptr) != 0 }
    @discardableResult func redo() -> Bool { gd_redo(ptr) != 0 }
    @discardableResult func setDescription(_ lines: [String]) -> Bool { gd_set_description(ptr, lines.joined(separator: "\n")) != 0 }
    @discardableResult func restore(_ text: String, label: String) -> Bool { gd_restore(ptr, text, label) != 0 }
    func setStart(col: Int, row: Int) { gd_set_start(ptr, Int32(col), Int32(row)) }
    /// a chunk's checker also judges it flipped (its Mirror switch on)
    func setMirrorCheck(_ on: Bool) { gd_set_mirror_check(ptr, on ? 1 : 0) }
    var scene: GridScene? { decodeJSON(GridScene.self, takeString(gd_scene_json(ptr))) }
    var history: History? { decodeJSON(History.self, takeString(gd_history_json(ptr))) }
    @discardableResult func historyGo(to k: Int) -> Bool { gd_history_goto(ptr, Int32(k)) != 0 }
    var historyFile: GridHistoryFile? { decodeJSON(GridHistoryFile.self, takeString(gd_history_export(ptr))) }
    func load(_ h: GridHistoryFile) {
        gd_history_clear(ptr)
        for s in h.undo { gd_history_add(ptr, 0, s.label, Int64(s.time), Int32(s.c), Int32(s.i), s.text) }
        for s in h.redo { gd_history_add(ptr, 1, s.label, Int64(s.time), Int32(s.c), Int32(s.i), s.text) }
    }
    func addHistory(label: String, time: Double, text: String) { gd_history_add(ptr, 0, label, Int64(time), -1, -1, text) }
}

/// the maze's checker in the background (GridJob): the solver's report a moment after an edit, the game's a while after
final class GdJob {
    let ptr: OpaquePointer = gd_job_new()
    deinit { gd_job_free(ptr) }
    func setGrid(_ name: String, rows: [String]) { gd_job_set_grid(ptr, name, rows.joined(separator: "\n")) }
    func kick(_ doc: GdDoc, level: Int, levelText: String?) { gd_job_kick(ptr, doc.ptr, Int32(level), levelText) }
    func cancel() { gd_job_cancel(ptr) }
    var busy: Bool { gd_job_busy(ptr) != 0 }
    func poll() -> (report: GridReport, revision: Int)? {
        var rev: Int32 = -1
        guard let json = takeString(gd_job_poll(ptr, &rev)), let r = decodeJSON(GridReport.self, json) else { return nil }
        return (r, Int(rev))
    }
}
