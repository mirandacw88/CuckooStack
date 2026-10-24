// A sync's conflict (SyncEngine, SyncViews): what changed on both sides, or a number the Mac has already, with both texts; and the
// words for what the Mac turned away. Foundation only: the sync's tests build it on its own.
import Foundation

struct SyncConflict: Identifiable, Hashable {
    enum Kind: Hashable { case both([String]), number, grid(String), chunk(String) }
    let number: Int
    let kind: Kind
    let mine: String
    let theirs: String
    let theirsHash: String
    var id: String {
        switch kind {
        case let .grid(name): return "grid-" + name
        case let .chunk(chunk): return "chunk-" + chunk
        default: return "level-\(number)"
        }
    }
    var gridName: String? { if case let .grid(name) = kind { return name }; return nil }
    var chunkId: String? { if case let .chunk(id) = kind { return id }; return nil }

    /// what the Mac turned away, as a line: a level by its number, a maze or a chunk by its kind and name
    static func rejection(_ key: String, _ why: String) -> String {
        if key.hasPrefix("chunk ") { return "Chunk \(ChunkNames.shown(String(key.dropFirst(6)))): \(why)" }
        if key.hasPrefix("grid ") { return "The \(Builtin.gauntletShown(String(key.dropFirst(5)))): \(why)" }
        return "Level \(key): \(why)"
    }
}
