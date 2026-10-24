// The app's chunk store: in Documents beside the levels, keeping the bot's words in their sync state.
import Foundation

extension LevelStore: VerdictBook {
    var verdicts: [String: Verdict] {
        get { state.verdicts }
        set { state.verdicts = newValue }
    }
    var chunkEntries: [String: SyncState.Entry] {
        get { state.chunks ?? [:] }
        set { state.chunks = newValue }
    }
    var chunksSeeded: Bool {
        get { state.chunksSeeded ?? false }
        set { state.chunksSeeded = newValue }
    }
}

extension ChunkStore {
    static let shared: ChunkStore = {
        let launch = LaunchRequest.current
        // (a UI test starts with none of the game's chunks, and with the ones it asks for: --ui-test-chunk <file>)
        let s = ChunkStore(root: LevelStore.shared.root, verdicts: LevelStore.shared, builtin: launch.uiTestReset ? [:] : nil)
        var texts: [String: String] = [:]
        for path in launch.uiTestChunks {
            let id = URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent
            if let t = try? String(contentsOfFile: path, encoding: .utf8) { texts[id] = t }
        }
        if !texts.isEmpty { s.restore(texts: texts, histories: nil) }
        return s
    }()
}
