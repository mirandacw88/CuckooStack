// A backup of everything made on this device, in one file the person saves where they like (Files: iCloud Drive, On
// My iPad, a USB drive), so the levels outlive the app: every level's file, every maze's and every chunk's, as the game reads
// them, and each one's history. Brought back from the same file: what's in it replaces what's here (a level or maze not in
// it is kept), and syncs as changed. The Mac's pairing code isn't in it.
import SwiftUI
import UniformTypeIdentifiers

// (the backup itself, and what makes and restores it: BackupModel.swift)

/// the backup as a file to save (fileExporter)
struct BackupDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.json] }
    let backup: EditorBackup
    init(_ backup: EditorBackup) { self.backup = backup }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        backup = try EditorBackup.decode(data)
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: try backup.encoded()) }
}

/// what the backup menu has under way: a file to save, one to bring back, the answer (the sheets hang off the levels
/// list, not off the menu: a menu's view is gone once it closes)
@MainActor
final class BackupState: ObservableObject {
    @Published var exporting: BackupDocument?
    @Published var importing = false
    @Published var pending: EditorBackup?
    @Published var result: String?
}

/// the levels list's backup button: back everything up to a file, or bring a backup back
struct BackupMenu: View {
    @ObservedObject var store: LevelStore
    @ObservedObject var state: BackupState
    var body: some View {
        Menu {
            Button { state.exporting = BackupDocument(store.makeBackup()) } label: {
                Label("Back up all levels…", systemImage: "externaldrive.badge.plus")
            }
            Button { state.importing = true } label: { Label("Restore from a backup…", systemImage: "externaldrive.badge.timemachine") }
            if let last = store.state.lastBackup {
                Text("Last backed up \(last.formatted(.relative(presentation: .named)))")
            }
        } label: { Label("Backup", systemImage: "externaldrive") }
        .accessibilityLabel("Backup")
    }
}

/// the backup's sheets: save the file (Files), pick one to bring back, confirm, say how it went
struct BackupSheets: ViewModifier {
    @ObservedObject var store: LevelStore
    @ObservedObject var state: BackupState
    func body(content: Content) -> some View {
        content
            .fileExporter(isPresented: Binding(get: { state.exporting != nil }, set: { if !$0 { state.exporting = nil } }), document: state.exporting,
                          contentType: .json, defaultFilename: state.exporting?.backup.fileName ?? "Cuckoo Editor backup") { [counts = (store.levels.count, store.grids.count, ChunkStore.shared.chunks.count)] r in
                switch r {
                case .success:
                    store.state.lastBackup = Date()
                    store.saveState()
                    state.result = "Backed up \(counts.0) levels, \(counts.1) mazes and \(counts.2) chunks."
                    Haptic.success()
                case .failure(let e): state.result = "Not backed up: \(e.localizedDescription)"
                }
                state.exporting = nil
            }
            .fileImporter(isPresented: $state.importing, allowedContentTypes: [.json]) { r in
                guard case let .success(url) = r else { return }
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                do { state.pending = try EditorBackup.decode(Data(contentsOf: url)) }
                catch { state.result = "That file isn't a Cuckoo Editor backup." }
            }
            .confirmationDialog(state.pending.map { "Restore the backup of \($0.made.formatted(date: .abbreviated, time: .shortened))?" } ?? "",
                                isPresented: Binding(get: { state.pending != nil }, set: { if !$0 { state.pending = nil } }), titleVisibility: .visible) {
                Button("Restore \(state.pending?.levels.count ?? 0) levels and \(state.pending?.grids.count ?? 0) mazes" + ((state.pending?.chunks?.count ?? 0) > 0 ? " and \(state.pending?.chunks?.count ?? 0) chunks" : ""), role: .destructive) {
                    if let b = state.pending {
                        let n = store.restore(b)
                        state.result = "Restored \(n.levels) levels, \(n.grids) mazes" + (n.chunks > 0 ? " and \(n.chunks) chunks" : "") + ". Any that differ from the Mac's sync to it as changes."
                        Haptic.success()
                    }
                    state.pending = nil
                }
            } message: {
                Text("From \(state.pending?.device ?? "a device"). Each level and maze in it replaces the one here (its history too); the others here stay.")
            }
            .alert(state.result ?? "", isPresented: Binding(get: { state.result != nil }, set: { if !$0 { state.result = nil } })) { Button("OK") { state.result = nil } }
    }
}
