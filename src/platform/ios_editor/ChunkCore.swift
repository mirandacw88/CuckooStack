// The chunk files as the core reads them (Chunk.h through cs_editor.h's ck_ calls), apart from any level: a file's header and
// its body, what a chunk's body is like, where a roll placed one. Foundation only (the chunk store's tests build it on its own).
import Foundation

/// a chunk file as the core reads it (ck_parse): its header, its body as text, why it doesn't read (if not)
struct ChunkFile: Decodable {
    let ok: Bool
    let error: String?
    let kind: String?
    let notes: [String]?
    let ranges: [[Int]]?
    let weight: Int?
    let uses: Int?
    let mirror: Bool?
    let canMirror: Bool?
    let whyNot: String?
    let body: String?
    let first: Int?
    init(ok: Bool, error: String?) { self.ok = ok; self.error = error; kind = nil; notes = nil; ranges = nil; weight = nil; uses = nil; mirror = nil; canMirror = nil; whyNot = nil; body = nil; first = nil }
    /// the whole file reads as the generator needs it
    var reads: Bool { ok && (error ?? "").isEmpty }
    var isMaze: Bool { kind == "maze" }
    var header: ChunkHeader {
        ChunkHeader(maze: isMaze, notes: notes ?? [], ranges: (ranges ?? []).filter { $0.count == 2 }.map { LevelRange($0[0], $0[1]) }, weight: weight ?? 3, uses: uses ?? 0, mirror: mirror ?? false)
    }
}

/// a course chunk's body as the core sees it (ed_chunk_json): whether it can be flipped and why not, what breaks a
/// chunk's rules, how far it reaches and its pads (cells)
struct ChunkInfo: Decodable, Equatable {
    let canMirror: Bool
    let why: String
    let problem: String
    let reach: Double
    let runIn, runOut: Int
}

/// where the last roll placed a chunk (metres along the course)
struct ChunkUse: Decodable, Hashable {
    let id: String
    let x0, x1: Double
    let mirrored: Bool
}

struct MergeResult: Decodable {
    let ok: Bool
    let text: String
    let conflicts: [String]
    let error: String
}

struct CheckResult: Decodable { let ok: Bool; let error: String }

/// the chunk files (Chunk.h) apart from any level
enum Chunks {
    static func parse(_ id: String, _ text: String) -> ChunkFile {
        decodeJSON(ChunkFile.self, takeString(ck_parse(id, text))) ?? ChunkFile(ok: false, error: "it doesn't read")
    }
    /// a file's text from its header and its body
    static func text(_ h: ChunkHeader, body: String) -> String {
        takeString(ck_text(h.maze ? 1 : 0, h.notes.joined(separator: "\n"), h.rangesText, Int32(h.weight), Int32(h.uses), h.mirror ? 1 : 0, body)) ?? ""
    }
    /// a new course chunk's file for level `first`
    static func blank(_ id: String, first: Int) -> String { takeString(ck_blank(id, Int32(first))) ?? "" }
    /// a new maze chunk's file for level `first`: a 9 × 5 block, a saw in its floor
    static func blankMaze(_ id: String, first: Int) -> String { takeString(ck_blank_maze(id, Int32(first))) ?? "" }
    /// the ids of these chunks (id: file text) in the list's order: bands of five levels, then where they start
    static func sorted(_ files: [String: String]) -> [String] {
        let bundle = files.keys.sorted().map { "=== \($0)\n" + (files[$0] ?? "") + ((files[$0] ?? "").hasSuffix("\n") ? "" : "\n") }.joined()
        return decodeJSON([String].self, takeString(ck_sort(bundle))) ?? files.keys.sorted()
    }
    static func isValid(_ id: String) -> Bool { ck_valid_id(id) != 0 }
}
