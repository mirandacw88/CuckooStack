// What the stores keep about a text, apart from their files (Foundation only: the chunk store's tests build it on its own):
// its hash, the bot's last word on it, and its history as the device keeps it.
import CryptoKit
import Foundation

func textHash(_ text: String) -> String {
    SHA256.hash(data: Data(text.utf8)).map { String(format: "%02x", $0) }.joined()
}

/// the bot's last word on a level (its text's hash: stale once it's changed)
struct Verdict: Codable, Hashable {
    var cleared: Bool
    var seconds: Double
    var hash: String
}

/// a level's history as the device keeps it between launches (EditorDoc::historyExport): each step with the level's
/// text on its far side
struct HistoryFile: Codable {
    struct Step: Codable { let label: String; let time: Double; let at: Double; let text: String }
    let current: String
    let undo: [Step]
    let redo: [Step]
}
