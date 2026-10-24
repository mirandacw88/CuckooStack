// What the editor's core says, as Swift values (the JSON's shapes: EditorScene.h, Hazards.h, ReportJson.h)
import Foundation
import SwiftUI

struct ParamSpec: Decodable, Hashable {
    let key, label, unit: String
    let min, max, step, `default`: Double
}

struct HazardType: Decodable, Identifiable, Hashable {
    let id, section, label, plural, key, tip, icon, glyph, color, anchor: String
    let params: [ParamSpec]
    let snapGap, minGap: Double
    let spanParam: Int?       // a stretch (lava): the param that's its length in cells
    let generated: Bool?      // the course generator lays it (a switch on the Level generator)
    let group: String?        // its tool group: "hazards" (the Hazards flyout) or "pickups" (the disco ball)
    var toolGroup: String { group ?? "hazards" }
    var isFree: Bool { anchor == "free" }
    var isFace: Bool { anchor == "face" }   // (set into a wall's or a block's side: a wall laser)
}

struct WallKindInfo: Decodable, Identifiable, Hashable {
    let id, label, key, tip, color: String
    let bouncer: Bool?        // a bumper or a gate (the Bouncers tools, not the kinds under the rail)
}

struct Registry: Decodable {
    let cell: Double
    let lintel, porch, bodyRows: Int
    let budgetCap: Double
    let forecourt: Int
    let hazards: [HazardType]
    let kinds: [WallKindInfo]
    static let empty = Registry(cell: 0.6, lintel: 3, porch: 8, bodyRows: 2, budgetCap: 8, forecourt: 23, hazards: [], kinds: [])
    func hazard(_ id: String) -> HazardType? { hazards.first { $0.id == id } }
    /// the kinds a wall's built of (under the rail); the bouncers are tools of their own
    var buildKinds: [WallKindInfo] { kinds.filter { $0.bouncer != true } }
    var bouncerKinds: [WallKindInfo] { kinds.filter { $0.bouncer == true } }
}

/// a run of like columns: [c0, c1, h, kind, ceil, top]
struct WallRun: Decodable, Hashable {
    let c0, c1, h: Int
    let kind: String
    let ceil, top: Int
    init(from decoder: Decoder) throws {
        var c = try decoder.unkeyedContainer()
        c0 = try c.decode(Int.self); c1 = try c.decode(Int.self); h = try c.decode(Int.self)
        kind = try c.decode(String.self); ceil = try c.decode(Int.self); top = try c.decode(Int.self)
    }
}

struct GauntletSpan: Decodable, Hashable {
    let name: String
    let c0, c1: Int
    let entryRow, exitRow: Int?
    let rows: [String]?
}

struct PlacedItem: Decodable, Hashable, Identifiable {
    let i: Int
    let type: String
    let x, y: Double
    let x1: Double?           // a stretch's end
    let reach: [Double]?      // one in a wall's side: its beam's reach along the course, from .. to (metres)
    let arc: [[Double]]?      // a spring's flight (its feet, (x, y) metres, every 0.05 s) and how it ends
    let end: String?          // ...landed, wall (into a wall's face), barrier, off
    let v: [Double]
    var id: Int { i }
}

struct CourseNote: Decodable, Hashable, Identifiable {
    let x, y: Double
    let text, kind: String
    var id: String { "\(kind)\(x)\(y)\(text)" }
}

struct ToolPreview: Decodable, Hashable {
    let kind: String
    let c0, c1, r0, r1: Int
    let erase: Bool
}

struct CourseScene: Decodable {
    let number: Int
    let zone: String
    let cell, length, courseLength: Double
    let revision: Int
    let walls: [WallRun]
    let gauntlets: [GauntletSpan]
    let items: [PlacedItem]
    let notes: [CourseNote]
    let walk: [[Double]]
    let lowest: Double
    let rows: Int
    let start: Double?
    let selection: [Int]?
    let picked: Int?
    let preview: ToolPreview?
}

struct Hit: Decodable {
    let what: String
    let i: Int?
    let type: String?
    let v: [Double]?
    let name: String?
    let col, h, ceil, top: Int?
    let kind: String?
}

/// a step of a level's history: its words, when (seconds since 1970), where (metres along; -1: its settings)
struct HistoryStep: Decodable, Hashable {
    let label: String
    let revision: Int
    let time: Double
    let at: Double
    var date: Date { Date(timeIntervalSince1970: time) }
}
struct History: Decodable {
    let undo: [HistoryStep]   // oldest first
    let redo: [HistoryStep]   // next first
    let canUndo, canRedo: Bool
}

struct LevelMeta: Decodable {
    let number: Int
    let notes: [String]
    let length, diffOffset, diffScale: Double
    let raids: String
    let gauntlet: String?
    let gauntlets: [String]
    let error: String
    enum CodingKeys: String, CodingKey { case number, notes, length, diffOffset, diffScale, raids, gauntlet, gauntlets, error }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        number = try c.decode(Int.self, forKey: .number)
        notes = try c.decode([String].self, forKey: .notes)
        length = try c.decode(Double.self, forKey: .length)
        diffOffset = try c.decode(Double.self, forKey: .diffOffset)
        diffScale = try c.decode(Double.self, forKey: .diffScale)
        if let n = try? c.decode(Int.self, forKey: .raids) { raids = String(n) } else { raids = try c.decode(String.self, forKey: .raids) }
        gauntlet = try c.decodeIfPresent(String.self, forKey: .gauntlet)
        gauntlets = try c.decode([String].self, forKey: .gauntlets)
        error = try c.decode(String.self, forKey: .error)
    }
}

/// level_check --json --game's report (the bot's run in the real game)
struct BotGame: Decodable, Hashable {
    let cleared: Bool
    let seconds, x, y: Double
    let perfects, closeCalls: Int
    let trace: [[Double]]
    let events: [[Double]]
}

/// a chunk's body played flipped, inside its report
struct FlippedReport: Decodable {
    let ok: Bool
    let error: String?
    let game: BotGame?
}

struct BotReport: Decodable {
    let ok: Bool
    let level: Int?
    let error: String?
    let lowest: Double?
    let game: BotGame?
    let flipped: FlippedReport?   // a chunk with Mirror on: the flipped body played too
}

extension Color {
    /// "#rrggbb"
    init(hex: String, opacity: Double = 1) {
        var v: UInt64 = 0
        Scanner(string: hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))).scanHexInt64(&v)
        self.init(.sRGB, red: Double((v >> 16) & 0xff) / 255, green: Double((v >> 8) & 0xff) / 255, blue: Double(v & 0xff) / 255, opacity: opacity)
    }
}

#if canImport(UIKit)
extension UIColor {
    convenience init(hex: String, alpha: CGFloat = 1) {
        var v: UInt64 = 0
        Scanner(string: hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))).scanHexInt64(&v)
        self.init(red: CGFloat((v >> 16) & 0xff) / 255, green: CGFloat((v >> 8) & 0xff) / 255, blue: CGFloat(v & 0xff) / 255, alpha: alpha)
    }
}
#endif

// MARK: a maze (GridDoc, GridJob: cs_editor.h's gd_ calls)

struct GridPreview: Decodable, Hashable {
    let kind: String
    let c0, i0, c1, i1: Int
    let erase: Bool
}

/// a maze as its document has it: its rows top first, its doors, the tool's preview, the play-test's start cell
struct GridScene: Decodable {
    let name: String
    let cols: Int
    let rows: [String]
    let description: [String]
    let revision: Int
    let canUndo, canRedo: Bool
    let entry, exit: [Int]?
    let preview: GridPreview?
    let start: [Int]?
    var chunk: Bool? = nil   // a maze chunk's block (chunk mode)
    let error: String
}

/// a maze's history as the device keeps it (GridDoc::historyExport)
struct GridHistoryFile: Codable {
    struct Step: Codable { let label: String; let time: Double; let c: Int; let i: Int; let text: String }
    let current: String
    let undo: [Step]
    let redo: [Step]
}

/// gauntlet_check --json's report (GauntletReport.h), with the other grids it's the same as and whether the game ran
struct GridReport: Decodable {
    struct Solution: Decodable { let reachable: Bool; let routes, routeCells, deadEndCells, softLocks, minEggs, moves, perfects, shaves, squeezes, nearMisses: Int }
    /// a step of the bot's way: [c, b, n, d, lay, move (0 ok, 1 bounce, 2 crash, 3 out), climbed, fell, flags(, meter×100)]
    struct Meter: Decodable { let rate: Double; let out: Bool; let cells, waiting: Int; let seconds: Double; let crash: [Int]?; let nearMisses: [[Int]] }
    struct Course: Decodable { let placed, authored: Bool; let x0, x1, finish: Double; let saws, sawsWanted: Int }
    struct Design: Decodable, Hashable { let what: String; let ok: Bool; let why: String }
    struct Game: Decodable { let entered, out, cleared: Bool; let seconds, inside: Double; let perfects, closeCalls: Int; let crash: [Double]? }
    let ok: Bool
    let error: String?
    let sameAs: [String]?
    let gameRun: Bool?
    let name: String?
    let level: Int?
    let cols, rows, entryRow, exitRow: Int?
    let hard, wantMisses: Int?   // its "hard N" (the density it was made at) and the near misses that asks
    let solution: Solution?
    let cellMap: [String]?
    let way: [[Int]]?
    let wayNearMisses: [[Int]]?
    let meter: [Meter]?
    let course: Course?
    let designs: [Design]?
    let game: Game?
    /// a maze chunk's checker with its Mirror switch on: the same corridor round the chunk mirrored, judged too
    struct Flipped: Decodable { let ok: Bool; let error: String?; let solution: Solution?; let meter: [Meter]? }
    var flipped: Flipped? = nil

    /// a chunk's report is still without its flipped half (Mirror on)
    func mirrorPending(_ mirror: Bool) -> Bool { mirror && flipped == nil }

    /// a chunk's report seen from the chunk (the corridor round it cut away): the report's columns counted from the chunk's first, its
    /// map the chunk's own rows, so the canvas draws the bot's way and the near misses over the chunk. `c0`: the chunk's first column
    /// in the corridor
    func windowed(c0: Int, cols: Int, rows: Int) -> GridReport {
        func shift(_ steps: [[Int]]?) -> [[Int]]? { steps?.map { s in var t = s; if !t.isEmpty { t[0] -= c0 }; return t } }
        let map = cellMap.map { m in m.suffix(rows).map { line in String(Array(line).dropFirst(c0).prefix(cols)) } }
        let meters = meter?.map { m in Meter(rate: m.rate, out: m.out, cells: m.cells, waiting: m.waiting, seconds: m.seconds, crash: m.crash.map { c in var t = c; if !t.isEmpty { t[0] -= c0 }; return t }, nearMisses: m.nearMisses) }
        var out = GridReport(ok: ok, error: error, sameAs: sameAs, gameRun: gameRun, name: name, level: level, cols: cols, rows: rows, entryRow: entryRow, exitRow: exitRow,
                             hard: hard, wantMisses: wantMisses, solution: solution, cellMap: map, way: shift(way), wayNearMisses: shift(wayNearMisses), meter: meters,
                             course: course, designs: designs, game: game)
        out.flipped = flipped
        return out
    }
}
