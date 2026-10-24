// A backup of everything made on this device, as data: every level's file, every maze's and every chunk's, as the game
// reads them, and each one's history (Backup.swift saves it to a file and brings it back). Brought back: what's in it replaces
// what's here (a level, maze or chunk not in it is kept), and syncs as changed. The Mac's pairing code isn't in it.
import Foundation
#if canImport(UIKit)
import UIKit
#endif

struct EditorBackup: Codable {
    var format = "cuckoo-editor-backup"
    var version = 1
    var made: Date
    var device: String
    var levels: [String: String]                 // its number: its file
    var grids: [String: String]                  // its name: its file
    var levelHistory: [String: HistoryFile]
    var gridHistory: [String: GridHistoryFile]
    var chunks: [String: String]? = nil           // its id: its file (backups from before chunks don't have it)
    var chunkHistory: [String: HistoryFile]? = nil

    static func decode(_ data: Data) throws -> EditorBackup {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        let b = try d.decode(EditorBackup.self, from: data)
        guard b.format == "cuckoo-editor-backup" else { throw CocoaError(.fileReadCorruptFile) }
        return b
    }

    func encoded() throws -> Data {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return try e.encode(self)
    }

    /// what the device is called (the backup says where it came from)
    static var deviceName: String {
        #if canImport(UIKit)
        UIDevice.current.name
        #else
        Host.current().localizedName ?? "a Mac"
        #endif
    }

    var fileName: String {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd HH.mm"
        return "Cuckoo Editor backup \(f.string(from: made))"
    }
}

extension LevelStore {
    /// everything here, as a backup
    func makeBackup(chunks: ChunkStore? = nil) -> EditorBackup {
        let store = chunks ?? ChunkStore.shared
        var levelsOut: [String: String] = [:], gridsOut: [String: String] = [:]
        var lh: [String: HistoryFile] = [:], gh: [String: GridHistoryFile] = [:]
        for e in levels {
            if let t = text(of: e.number) { levelsOut[String(e.number)] = t }
            if let h = history(of: e.number) { lh[String(e.number)] = h }
        }
        for name in grids.keys {
            if let t = gridText(name) { gridsOut[name] = t }
            if let h = gridHistory(of: name) { gh[name] = h }
        }
        var chunksOut: [String: String] = [:], ch: [String: HistoryFile] = [:]
        for e in store.chunks {
            if let t = store.text(of: e.id) { chunksOut[e.id] = t }
            if let h = store.history(of: e.id) { ch[e.id] = h }
        }
        return EditorBackup(made: Date(), device: EditorBackup.deviceName, levels: levelsOut, grids: gridsOut, levelHistory: lh, gridHistory: gh, chunks: chunksOut, chunkHistory: ch)
    }

    /// a backup brought back: its levels, mazes and chunks in place of these (each a change to sync), their histories with
    /// them; what's here and not in it, kept. How many of each it brought back
    @discardableResult func restore(_ b: EditorBackup, chunks: ChunkStore? = nil) -> (levels: Int, grids: Int, chunks: Int) {
        let store = chunks ?? ChunkStore.shared
        var nl = 0, ng = 0
        for (name, text) in b.grids where !name.isEmpty && !LevelStore.rows(of: text).isEmpty {
            saveGrid(name, text: text)
            if let h = b.gridHistory[name] { saveGridHistory(name, h) }
            gridChanges.send((name, text))
            ng += 1
        }
        for (key, text) in b.levels {
            guard let n = Int(key), n > 0 else { continue }
            save(n, text: text)
            if let h = b.levelHistory[key] { saveHistory(n, h) }
            changes.send((n, text))
            nl += 1
        }
        let nc = store.restore(texts: b.chunks ?? [:], histories: b.chunkHistory)
        saveState()
        reload()
        return (nl, ng, nc)
    }
}
