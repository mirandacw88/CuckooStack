// A maze's name as the editors show it and as its file has it, and the renames waiting to be told to the Mac (pure:
// tests/swift/ios_editor). The rules themselves are the game's (Gauntlet.h's validGauntletName: Builtin.validGauntletName).
import Foundation

/// a maze renamed here that the Mac has by its old name
struct GridRename: Codable, Hashable { var from: String; var to: String }

enum GridNames {
    /// as typed ("Hall of Saws"): the file's ("hall_of_saws")
    static func typed(_ text: String) -> String {
        text.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
            .replacingOccurrences(of: #"[\s-]+"#, with: "_", options: .regularExpression)
    }
    /// as the editors show it ("hall of saws")
    static func shown(_ name: String) -> String { name.replacingOccurrences(of: "_", with: " ") }
    /// the renames to tell the Mac, with `from` renamed `to` (`known`: the Mac has `from`). A chain is one rename (a→b,
    /// then b→c: a→c); renamed back, nothing to tell
    static func pending(_ list: [GridRename], from: String, to: String, known: Bool) -> [GridRename] {
        var out = list
        if let k = out.firstIndex(where: { $0.to == from }) { out[k].to = to } else if known { out.append(.init(from: from, to: to)) }
        out.removeAll { $0.from == $0.to }
        return out
    }
}
