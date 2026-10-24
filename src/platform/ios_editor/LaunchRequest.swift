// What the editor opens on, from its launch arguments (Foundation only: tests/swift/ios_editor checks it on its own).
import Foundation

enum AppMode: Equatable { case home, levels, chunks }

/// what the launch arguments ask for: the mode, the level (--open N) and the chunk (--chunks [id]) to open; and, for the UI
/// tests (tests/ui/ios_editor), --ui-test-reset (its own empty data folder), --ui-test-chunk <file> (a chunk put in it) and
/// --ui-test-files <folder> (the backup and restore through that folder, not the Files sheets)
struct LaunchRequest: Equatable {
    var mode: AppMode = .home
    var level: Int?
    var chunk: String?
    var detailOnly = false
    var uiTestReset = false
    var uiTestChunks: [String] = []
    var uiTestFiles: String?

    init(_ args: [String]) {
        if let i = args.firstIndex(of: "--open"), i + 1 < args.count, let n = Int(args[i + 1]) { mode = .levels; level = n }
        else if let i = args.firstIndex(of: "--chunks") {
            mode = .chunks
            if i + 1 < args.count, !args[i + 1].hasPrefix("--") { chunk = args[i + 1] }
        }
        detailOnly = args.contains("--detail")
        uiTestReset = args.contains("--ui-test-reset")
        for (i, a) in args.enumerated() where a == "--ui-test-chunk" && i + 1 < args.count && !args[i + 1].hasPrefix("--") { uiTestChunks.append(args[i + 1]) }
        if let i = args.firstIndex(of: "--ui-test-files"), i + 1 < args.count, !args[i + 1].hasPrefix("--") { uiTestFiles = args[i + 1] }
    }

    /// the arguments this process was launched with
    static let current = LaunchRequest(ProcessInfo.processInfo.arguments)

    /// the folder the stores live in: nil (Documents) normally; with --ui-test-reset a fresh one in the temporary folder,
    /// so a test never reads or writes the real levels and chunks
    func dataRoot(temp: URL = FileManager.default.temporaryDirectory) -> URL? {
        guard uiTestReset else { return nil }
        let dir = temp.appendingPathComponent("CuckooUITestData", isDirectory: true)
        try? FileManager.default.removeItem(at: dir)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }
}
