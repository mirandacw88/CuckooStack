// The chunk editor's rules apart from its looks (Foundation only: tests/swift/ios_editor builds and checks them on their
// own): a chunk's name as typed and as its file has it, how its ranges, uses and bands read in words, the ranges the
// settings card edits, the master backup file (every chunk's id and file text) and what restoring one would change.
// The chunk files themselves, their order and their rules are the core's (Chunk.h: Chunks in Core.swift).
import Foundation

/// a run of consecutive levels a chunk may appear in (1...9999; `to` is never before `from`)
struct LevelRange: Codable, Hashable {
    var from: Int
    var to: Int
    static let most = 9999
    init(_ from: Int, _ to: Int) {
        self.from = min(max(from, 1), Self.most)
        self.to = min(max(to, self.from), Self.most)
    }
    /// "1-5", or "7" for one level: as the chunk file has it
    var fileText: String { to == from ? "\(from)" : "\(from)-\(to)" }
    /// "1–5", or "7": as the screens show it
    var shown: String { to == from ? "\(from)" : "\(from)\u{2013}\(to)" }
}

/// what the settings card edits: the header of a chunk file (its body is the course or the maze)
struct ChunkHeader: Hashable {
    var maze = false
    var notes: [String] = []
    var ranges: [LevelRange] = [LevelRange(1, 1)]
    var weight = 3
    var uses = 0          // the most a course holds; 0: no limit
    var mirror = false

    /// the level the chunk is judged at: its first range's start
    var first: Int { ranges.first?.from ?? 1 }
    /// the file's `ranges` argument ("1-5 7")
    var rangesText: String { ranges.map(\.fileText).joined(separator: " ") }
    /// a name for the list: its first note, if it has one
    var summary: String { notes.first { !$0.trimmingCharacters(in: .whitespaces).isEmpty }?.trimmingCharacters(in: .whitespaces) ?? "" }
}

enum ChunkRanges {
    /// a range added after the others: the five levels after the last one's end (1-5 then 6-10), or 1-5 if there are none
    static func added(to ranges: [LevelRange]) -> [LevelRange] {
        guard let last = ranges.last else { return [LevelRange(1, 5)] }
        let from = min(last.to + 1, LevelRange.most)
        return ranges + [LevelRange(from, from + 4)]
    }
    /// range `k` taken out; a chunk keeps one at least
    static func removed(_ k: Int, from ranges: [LevelRange]) -> [LevelRange] {
        guard ranges.count > 1, ranges.indices.contains(k) else { return ranges }
        var out = ranges
        out.remove(at: k)
        return out
    }
    /// range `k` set to run from..to (to pushed up to from when it would be before it)
    static func set(_ k: Int, from: Int? = nil, to: Int? = nil, in ranges: [LevelRange]) -> [LevelRange] {
        guard ranges.indices.contains(k) else { return ranges }
        var out = ranges
        let f = from ?? out[k].from
        out[k] = LevelRange(f, to ?? out[k].to)
        return out
    }
    /// whether a chunk may appear in `level`
    static func covers(_ ranges: [LevelRange], level: Int) -> Bool { ranges.contains { level >= $0.from && level <= $0.to } }
}

enum ChunkWords {
    /// "2 a level" or "No limit"
    static func uses(_ n: Int) -> String { n <= 0 ? "No limit" : "\(n) a level" }
    /// the list's band of five levels a chunk sits in, by where its first range starts: 0 is 1-5, 1 is 6-10
    static func band(of first: Int) -> Int { (max(first, 1) - 1) / 5 }
    /// "Levels 1–5"
    static func bandTitle(_ band: Int) -> String { "Levels \(band * 5 + 1)\u{2013}\(band * 5 + 5)" }
    /// the zone a level is in (the look of its row): 11-20 Egypt, 21-30 Japan, else the city
    static func zone(forLevel n: Int) -> String { n >= 11 && n <= 20 ? "egypt" : n >= 21 && n <= 30 ? "japan" : "city" }
    /// the chunks a roll placed, in the order they came, with how many times each: "hurdle ×2, steps"
    static func used(_ ids: [String]) -> String {
        var order: [String] = [], counts: [String: Int] = [:]
        for id in ids { if counts[id] == nil { order.append(id) }; counts[id, default: 0] += 1 }
        return order.map { (counts[$0] ?? 0) > 1 ? "\($0) \u{00D7}\(counts[$0] ?? 0)" : $0 }.joined(separator: ", ")
    }
    /// what a roll tells the designer, or nil for nothing: the chunks that don't read and were left out, the generator's note on
    /// chunks it gave up on (no course or maze passed with them), the chunks placed
    static func rollNotice(skipped: [String], note: String, placed: [String]) -> String? {
        var parts: [String] = []
        if !skipped.isEmpty { parts.append("Left out: \(skipped.joined(separator: ", ")) (they don't read)") }
        if !note.isEmpty { parts.append(note) }
        if !placed.isEmpty { parts.append("Chunks placed: " + used(placed)) }
        return parts.isEmpty ? nil : parts.joined(separator: " \u{00B7} ")
    }
    /// a new maze's history label: "A new maze (seed 7, chunks: a \u{00D7}2; left out: b)"
    static func mazeLabel(seed: UInt32, placed: [String], dropped: [String], reduced: [String]) -> String {
        var words: [String] = []
        if !placed.isEmpty { words.append("chunks: \(used(placed))") }
        if !dropped.isEmpty { words.append("left out: \(dropped.joined(separator: ", "))") }
        if !reduced.isEmpty { words.append("fewer of: \(reduced.joined(separator: ", "))") }
        return "A new maze (seed \(seed)" + (words.isEmpty ? ")" : ", " + words.joined(separator: "; ") + ")")
    }
    /// 1...10 as "light" to "heavy": what the weight slider says it does
    static func weight(_ w: Int) -> String {
        switch w {
        case ...2: return "rare"
        case 3...4: return "now and then"
        case 5...7: return "often"
        default: return "everywhere"
        }
    }
}

/// the bot's word on a chunk with Mirror on
enum ChunkVerdict {
    /// cleared only if it clears the chunk as built and, when it was played flipped too, flipped (nil: not played flipped)
    static func cleared(plain: Bool, flipped: Bool?) -> Bool { plain && (flipped ?? true) }
    /// "Flipped: cleared 9 s" or "Flipped: crashed at 7 m"
    static func flippedWords(cleared: Bool, seconds: Double, x: Double) -> String {
        cleared ? String(format: "Flipped: cleared %.0f s", seconds) : String(format: "Flipped: crashed at %.0f m", x)
    }
}

/// a chunk's pads, as bands over its course: the run-in from the start, the run-out to its end (metres; nothing for a pad
/// that isn't there)
enum ChunkPads {
    static func bands(reach: Double, runIn: Int, runOut: Int, cell: Double) -> [(x0: Double, x1: Double, label: String)] {
        var out: [(x0: Double, x1: Double, label: String)] = []
        if runIn > 0 { out.append((0, Double(runIn) * cell, "RUN-IN")) }
        if runOut > 0 { out.append((reach - Double(runOut) * cell, reach, "RUN-OUT")) }
        return out
    }
}

enum ChunkNames {
    /// a name as typed ("Sacred wells"): the file's ("sacred-wells"): lower case, spaces as '-', anything else but digits,
    /// '-' and '_' dropped
    static func typed(_ text: String) -> String {
        let lower = text.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
            .replacingOccurrences(of: #"\s+"#, with: "-", options: .regularExpression)
        let kept = lower.unicodeScalars.filter { ($0 >= "a" && $0 <= "z") || ($0 >= "0" && $0 <= "9") || $0 == "-" || $0 == "_" }
        return String(String.UnicodeScalarView(kept)).replacingOccurrences(of: #"-{2,}"#, with: "-", options: .regularExpression)
            .trimmingCharacters(in: CharacterSet(charactersIn: "-"))
    }
    /// whether it is a chunk's file name (Chunk.cpp's validId: 1 to 60 of lower case letters, digits, '-' and '_')
    static func valid(_ id: String) -> Bool {
        !id.isEmpty && id.count <= 60 && id.unicodeScalars.allSatisfy { ($0 >= "a" && $0 <= "z") || ($0 >= "0" && $0 <= "9") || $0 == "-" || $0 == "_" }
    }
    /// as the screens show it ("sacred wells")
    static func shown(_ id: String) -> String { id.replacingOccurrences(of: "-", with: " ").replacingOccurrences(of: "_", with: " ") }
    /// `base` if no chunk has it, else base-2, base-3, ...
    static func unique(_ base: String, taken: Set<String>) -> String {
        let stem = valid(base) ? base : "chunk"
        if !taken.contains(stem) { return stem }
        var k = 2
        while taken.contains("\(stem)-\(k)") { k += 1 }
        return "\(stem)-\(k)"
    }
    /// the id of a copy: copy, copy-2, ...
    static func copy(of id: String, taken: Set<String>) -> String { unique(String(id.prefix(54)) + "-copy", taken: taken) }
}

/// the master backup of every chunk: one file (Cuckoo Chunks 2026-10-24.cuckoochunks), JSON, each chunk's id and its
/// whole file text (header and body, course or maze)
struct ChunkBackup: Codable, Equatable {
    struct Item: Codable, Equatable { var id: String; var text: String }
    static let formatName = "cuckoo-chunks-backup"
    static let fileExtension = "cuckoochunks"
    var format = ChunkBackup.formatName
    var version = 1
    var made: Date
    var app: String
    var chunks: [Item]

    static func decode(_ data: Data) throws -> ChunkBackup {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        let b = try d.decode(ChunkBackup.self, from: data)
        guard b.format == formatName, b.chunks.allSatisfy({ ChunkNames.valid($0.id) }), Set(b.chunks.map(\.id)).count == b.chunks.count else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return b
    }

    func encoded() throws -> Data {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return try e.encode(self)
    }

    /// "Cuckoo Chunks 2026-10-24" (the file's extension is the type's)
    static func fileName(_ date: Date) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd"
        return "Cuckoo Chunks \(f.string(from: date))"
    }
}

/// what restoring a backup would do to the chunks here: new ones, ones it changes, ones it leaves as they are; and
/// the ones here that it doesn't have (kept)
struct ChunkRestorePlan: Equatable {
    var new: [String] = []
    var changed: [String] = []
    var unchanged: [String] = []
    var kept: [String] = []
    var isEmpty: Bool { new.isEmpty && changed.isEmpty }

    /// `current`: the chunks here, id to file text. Line endings and a missing last newline don't count as a change
    init(backup: ChunkBackup, current: [String: String]) {
        func plain(_ s: String) -> String { s.replacingOccurrences(of: "\r\n", with: "\n").trimmingCharacters(in: .whitespacesAndNewlines) }
        for item in backup.chunks {
            guard let mine = current[item.id] else { new.append(item.id); continue }
            if plain(mine) == plain(item.text) { unchanged.append(item.id) } else { changed.append(item.id) }
        }
        let theirs = Set(backup.chunks.map(\.id))
        kept = current.keys.filter { !theirs.contains($0) }.sorted()
    }

    /// "2 new, 1 changed, 3 unchanged"
    var summary: String {
        var parts: [String] = []
        if !new.isEmpty { parts.append("\(new.count) new") }
        if !changed.isEmpty { parts.append("\(changed.count) changed") }
        if !unchanged.isEmpty { parts.append("\(unchanged.count) unchanged") }
        return parts.isEmpty ? "Nothing in it" : parts.joined(separator: ", ")
    }
}
