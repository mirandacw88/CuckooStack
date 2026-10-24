// The chunks on this device: in bands of five levels (1-5, 6-10, ...) by where each one's first range starts, each row with its
// name, its kind, its ranges, its weight, its uses, whether it flips, and the bot's (a maze chunk's: the checker's) last word on it;
// a new course chunk or maze chunk, a copy, one taken out; and the backup of every chunk to one file, and its restore.
import SwiftUI
import UniformTypeIdentifiers

/// one chunk in the list
struct ChunkRow: View {
    let entry: ChunkEntry
    let verdict: Verdict?
    let selected: Bool
    var body: some View {
        let look = ZoneLook.of(ChunkWords.zone(forLevel: entry.header.first))
        let h = entry.header
        HStack(spacing: 12) {
            ZStack {
                RoundedRectangle(cornerRadius: 11, style: .continuous)
                    .fill(LinearGradient(colors: look.sky + [Neon.magenta.opacity(0.3)], startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 11, style: .continuous).strokeBorder(Neon.magenta.opacity(selected ? 1 : 0.5), lineWidth: selected ? 1.6 : 1))
                    .shadow(color: Neon.magenta.opacity(selected ? 0.7 : 0.2), radius: selected ? 8 : 3)
                Image(systemName: entry.maze ? "square.grid.3x3.fill" : "square.stack.3d.up.fill")
                    .font(.system(size: 18, weight: .semibold)).foregroundStyle(.white)
                    .symbolEffect(.bounce, value: selected)
            }
            .frame(width: 44, height: 44)
            .accessibilityLabel(entry.maze ? "A maze chunk" : "A course chunk")
            VStack(alignment: .leading, spacing: 5) {
                Text(entry.title).font(.system(size: 15, weight: .semibold, design: .rounded)).foregroundStyle(.white).lineLimit(1)
                FlowRow(spacing: 4) {
                    ForEach(Array(h.ranges.enumerated()), id: \.offset) { _, r in chip(r.shown, "flag.fill", look.accent) }
                    chip("\(h.weight)", "scalemass.fill", Neon.magenta)
                    chip(ChunkWords.uses(h.uses), "number.circle.fill", Neon.amber)
                    if h.mirror { chip(entry.canMirror ? "flips" : "can't flip", "arrow.left.arrow.right", entry.canMirror ? Neon.cyan : Neon.red.opacity(0.8)) }
                }
                if !entry.problem.isEmpty {
                    Label(entry.problem, systemImage: "exclamationmark.triangle.fill").font(.system(size: 11, weight: .semibold)).foregroundStyle(Neon.red).lineLimit(2)
                }
            }
            Spacer(minLength: 0)
            if entry.dirty {
                Image(systemName: "arrow.triangle.2.circlepath.circle.fill").font(.system(size: 13)).foregroundStyle(Neon.amber)
                    .accessibilityLabel("Not synced yet")
            }
            if let v = verdict {
                Image(systemName: v.cleared ? "checkmark.seal.fill" : "xmark.octagon.fill")
                    .foregroundStyle(v.cleared ? Neon.lime : Neon.red)
                    .shadow(color: v.cleared ? Neon.lime : Neon.red, radius: 4)
                    .accessibilityLabel(entry.maze ? (v.cleared ? "The checker is satisfied" : "The checker has something to fix") : (v.cleared ? "The bot clears it" : "The bot crashes"))
            }
        }
        .padding(.vertical, 4)
        .contentShape(Rectangle())
        // (one element a row, reading "A course chunk, hurdles, 20, 8, 2 a level, flips": the UI tests read it)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("chunk.row.\(entry.id)")
    }

    private func chip(_ text: String, _ symbol: String, _ tint: Color) -> some View {
        Label(text, systemImage: symbol)
            .font(.system(size: 10, weight: .bold, design: .rounded)).labelStyle(.titleAndIcon)
            .foregroundStyle(tint)
            .padding(.horizontal, 6).padding(.vertical, 2.5)
            .background(Capsule().fill(tint.opacity(0.13)))
            .overlay(Capsule().strokeBorder(tint.opacity(0.4), lineWidth: 0.8))
    }
}

struct ChunkList: View {
    @ObservedObject var store: ChunkStore
    @ObservedObject var levels: LevelStore
    @ObservedObject var sync: SyncEngine
    @Binding var selection: String?
    @State private var confirmDelete: String?
    @State private var naming = false
    @State private var newMaze = false   // (the name asked for is a maze chunk's)
    @State private var newName = ""
    @StateObject private var backup = ChunkBackupState()

    var body: some View {
        List(selection: $selection) {
            Section { SyncBanner(sync: sync, store: levels) }
                .listRowBackground(Color.clear)
            if let r = store.lastRestore {
                Section { RestoreBanner(record: r, undo: { withAnimation { store.undoRestore() } }, dismiss: { withAnimation { store.dismissRestore() } }) }
                    .listRowBackground(Color.clear)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
            if store.chunks.isEmpty {
                Section {
                    VStack(spacing: 10) {
                        Image(systemName: "puzzlepiece.extension.fill").font(.system(size: 34)).foregroundStyle(Neon.magenta).shadow(color: Neon.magenta, radius: 10)
                            .symbolEffect(.pulse)
                        Text("No chunks yet").font(.system(size: 16, weight: .bold, design: .rounded)).foregroundStyle(.white)
                        Text("A chunk is a set piece you build once and the generators place in the courses they roll.")
                            .font(.footnote).foregroundStyle(.white.opacity(0.6)).multilineTextAlignment(.center)
                        // (short labels under a heading: the sidebar is narrow, and "New course chunk" wrapped onto three lines)
                        VStack(spacing: 6) {
                            Text("NEW CHUNK").font(.system(size: 10, weight: .heavy, design: .rounded)).tracking(1.5).foregroundStyle(Neon.magenta.opacity(0.85))
                            HStack(spacing: 8) {
                                Button { newName = ""; newMaze = false; naming = true } label: { Label("Course", systemImage: "plus").lineLimit(1).frame(maxWidth: .infinity) }
                                    .buttonStyle(.borderedProminent).tint(Neon.magenta)
                                    .accessibilityLabel("New course chunk").accessibilityIdentifier("chunks.newCourse")
                                Button { newName = ""; newMaze = true; naming = true } label: { Label("Maze", systemImage: "square.grid.3x3.fill").lineLimit(1).frame(maxWidth: .infinity) }
                                    .buttonStyle(.bordered).tint(Neon.magenta)
                                    .accessibilityLabel("New maze chunk").accessibilityIdentifier("chunks.newMaze")
                            }
                        }
                    }
                    .frame(maxWidth: .infinity).padding(.vertical, 14)
                }
                .listRowBackground(Color.clear)
            }
            ForEach(bands, id: \.band) { group in
                Section(ChunkWords.bandTitle(group.band)) {
                    ForEach(group.entries) { e in
                        ChunkRow(entry: e, verdict: store.verdict(e), selected: selection == e.id)
                            .tag(e.id)
                            .listRowBackground(selection == e.id ? Neon.magenta.opacity(0.14) : Color.white.opacity(0.03))
                            .contextMenu {
                                Button { if let c = store.duplicate(e.id) { selection = c } } label: { Label("Copy as a new chunk", systemImage: "plus.square.on.square") }
                                Button(role: .destructive) { confirmDelete = e.id } label: { Label("Take it out", systemImage: "trash") }
                            }
                            .swipeActions(edge: .trailing) { Button(role: .destructive) { confirmDelete = e.id } label: { Label("Delete", systemImage: "trash") } }
                            .swipeActions(edge: .leading) { Button { if let c = store.duplicate(e.id) { selection = c } } label: { Label("Copy", systemImage: "plus.square.on.square") }.tint(Neon.cyan) }
                    }
                }
            }
        }
        .animation(.spring(response: 0.4, dampingFraction: 0.85), value: store.chunks.map(\.id))
        .animation(.spring(response: 0.4, dampingFraction: 0.85), value: store.lastRestore)
        .scrollContentBackground(.hidden)
        .background(NightBackdrop(zone: "city"))
        .accessibilityIdentifier("chunks.list")
        .navigationTitle("Chunks")
        .toolbar {
            ToolbarItem(placement: .topBarLeading) { HomeButton() }
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button { newName = ""; newMaze = false; naming = true } label: { Label("New course chunk", systemImage: "square.stack.3d.up.fill") }
                        .accessibilityIdentifier("chunks.menu.newCourse")
                    Button { newName = ""; newMaze = true; naming = true } label: { Label("New maze chunk", systemImage: "square.grid.3x3.fill") }
                        .accessibilityIdentifier("chunks.menu.newMaze")
                } label: { Label("New chunk", systemImage: "plus") } primaryAction: { newName = ""; newMaze = false; naming = true }
                    .keyboardShortcut("n", modifiers: .command)
                    .accessibilityIdentifier("chunks.new")
            }
            ToolbarItem(placement: .primaryAction) { ChunkBackupMenu(store: store, state: backup) }
        }
        .modifier(ChunkBackupSheets(store: store, state: backup))
        .alert(newMaze ? "New maze chunk" : "New course chunk", isPresented: $naming) {
            TextField(newMaze ? "Name, like sacred well" : "Name, like hurdles", text: $newName).textInputAutocapitalization(.never).autocorrectionDisabled()
            Button("Make it") {
                withAnimation { selection = newMaze ? store.newMaze(named: newName, first: nextFirst) : store.newCourse(named: newName, first: nextFirst) }
                Haptic.success()
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(newMaze ? "A block of cells with a floor under it, a saw in it. Build the piece, then set its levels; the maze generator stamps it into a floor."
                         : "A flat run-in, a wall, a flat run-out. Build the piece between the pads, then set its levels.")
        }
        .confirmationDialog("Take \(confirmDelete.map { ChunkNames.shown($0) } ?? "it") out?", isPresented: Binding(get: { confirmDelete != nil }, set: { if !$0 { confirmDelete = nil } }), titleVisibility: .visible) {
            Button("Take it out", role: .destructive) {
                if let id = confirmDelete { if selection == id { selection = nil }; store.delete(id) }
                confirmDelete = nil
            }
        } message: { Text("Back up your chunks first if you may want it again.") }
    }

    /// the level a new chunk starts at: where the one picked starts (else 1)
    private var nextFirst: Int { selection.flatMap { store.entry($0)?.header.first } ?? 1 }

    private var bands: [(band: Int, entries: [ChunkEntry])] {
        var out: [(band: Int, entries: [ChunkEntry])] = []
        for e in store.chunks {
            let b = ChunkWords.band(of: e.header.first)
            if let i = out.indices.last, out[i].band == b { out[i].entries.append(e) } else { out.append((b, [e])) }
        }
        return out
    }
}

/// what a restore did, with the way back
struct RestoreBanner: View {
    let record: ChunkRestoreRecord
    let undo: () -> Void
    let dismiss: () -> Void
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Image(systemName: "externaldrive.badge.checkmark").font(.system(size: 20)).foregroundStyle(Neon.lime).shadow(color: Neon.lime, radius: 6)
                    .symbolEffect(.bounce, options: .nonRepeating, value: record.added.count + record.replaced.count)
                Text(record.summary).font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(.white)
                Spacer(minLength: 0)
                Button(action: dismiss) { Image(systemName: "xmark.circle.fill").foregroundStyle(.white.opacity(0.45)) }.buttonStyle(.plain).accessibilityLabel("Dismiss")
            }
            if record.copy != nil {
                Text("The chunks it replaced are kept in a \"before restore\" file in this app's folder in Files.").font(.caption).foregroundStyle(.white.opacity(0.6))
            }
            Button(action: undo) { Label("Undo the restore", systemImage: "arrow.uturn.backward") }
                .buttonStyle(.borderedProminent).tint(Neon.amber).controlSize(.small)
                .accessibilityIdentifier("restore.undo")
        }
        .padding(12)
        .glass(Neon.lime, radius: 16)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("restore.banner")
    }
}

// MARK: backup

extension UTType {
    /// a chunks backup: JSON, with its own extension (declared in Info.plist)
    static let cuckooChunks = UTType(exportedAs: "com.cuckoostack.editor.chunks", conformingTo: .json)
}

/// the chunks backup as a file to save (fileExporter)
struct ChunkBackupDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.cuckooChunks, .json] }
    static var writableContentTypes: [UTType] { [.cuckooChunks] }
    let backup: ChunkBackup
    init(_ backup: ChunkBackup) { self.backup = backup }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        backup = try ChunkBackup.decode(data)
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: try backup.encoded()) }
}

@MainActor
final class ChunkBackupState: ObservableObject {
    @Published var exporting: ChunkBackupDocument?
    @Published var importing = false
    @Published var pending: (backup: ChunkBackup, plan: ChunkRestorePlan)?
    @Published var result: String?
    static var lastBackupKey: String { "lastChunkBackup" }
    var lastBackup: Date? { UserDefaults.standard.object(forKey: Self.lastBackupKey) as? Date }

    /// the UI tests' file (--ui-test-files <folder>): the backup and its restore go through it, not the Files sheets
    static var testFile: URL? { LaunchRequest.current.uiTestFiles.map { URL(fileURLWithPath: $0).appendingPathComponent("chunks-backup.cuckoochunks") } }

    func backUp(_ store: ChunkStore) {
        guard let f = Self.testFile else { exporting = ChunkBackupDocument(store.makeBackup()); return }
        do {
            try store.makeBackup().encoded().write(to: f, options: .atomic)
            result = "Backed up \(store.chunks.count) \(store.chunks.count == 1 ? "chunk" : "chunks")."
        } catch { result = "Not backed up: \(error.localizedDescription)" }
    }

    func restore(_ store: ChunkStore) {
        guard let f = Self.testFile else { importing = true; return }
        do {
            let b = try ChunkBackup.decode(Data(contentsOf: f))
            pending = (b, store.plan(for: b))
        } catch { result = "That file isn't a Cuckoo chunks backup." }
    }
}

/// the chunk list's backup button: every chunk to one file in Files, or a backup back
struct ChunkBackupMenu: View {
    @ObservedObject var store: ChunkStore
    @ObservedObject var state: ChunkBackupState
    var body: some View {
        Menu {
            Button { state.backUp(store) } label: { Label("Back up chunks…", systemImage: "externaldrive.badge.plus") }
                .disabled(store.chunks.isEmpty).accessibilityIdentifier("chunks.backup")
            Button { state.restore(store) } label: { Label("Restore chunks…", systemImage: "externaldrive.badge.timemachine") }
                .accessibilityIdentifier("chunks.restore")
            if let last = state.lastBackup { Text("Last backed up \(last.formatted(.relative(presentation: .named)))") }
        } label: { Label("Backup", systemImage: "externaldrive") }
        .accessibilityLabel("Backup").accessibilityIdentifier("chunks.backupMenu")
    }
}

/// the backup's sheets: save the file, pick one, say what it would change and restore it as one step, say how it went
struct ChunkBackupSheets: ViewModifier {
    @ObservedObject var store: ChunkStore
    @ObservedObject var state: ChunkBackupState
    func body(content: Content) -> some View {
        content
            .fileExporter(isPresented: Binding(get: { state.exporting != nil }, set: { if !$0 { state.exporting = nil } }), document: state.exporting,
                          contentType: .cuckooChunks, defaultFilename: ChunkBackup.fileName(state.exporting?.backup.made ?? Date())) { [count = store.chunks.count] r in
                switch r {
                case .success:
                    UserDefaults.standard.set(Date(), forKey: ChunkBackupState.lastBackupKey)
                    state.result = "Backed up \(count) \(count == 1 ? "chunk" : "chunks")."
                    Haptic.success()
                case .failure(let e): state.result = "Not backed up: \(e.localizedDescription)"
                }
                state.exporting = nil
            }
            .fileImporter(isPresented: $state.importing, allowedContentTypes: [.cuckooChunks, .json]) { r in
                guard case let .success(url) = r else { return }
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                do {
                    let b = try ChunkBackup.decode(Data(contentsOf: url))
                    state.pending = (b, store.plan(for: b))
                } catch { state.result = "That file isn't a Cuckoo chunks backup." }
            }
            .sheet(isPresented: Binding(get: { state.pending != nil }, set: { if !$0 { state.pending = nil } })) {
                if let p = state.pending {
                    RestorePlanSheet(backup: p.backup, plan: p.plan) {
                        let rec = store.restore(p.backup)
                        state.pending = nil
                        Haptic.success()
                        if rec.added.isEmpty && rec.replaced.isEmpty { state.result = rec.summary }
                    } cancel: { state.pending = nil }
                    .presentationDetents([.medium, .large])
                    .presentationDragIndicator(.visible)
                }
            }
            .alert(state.result ?? "", isPresented: Binding(get: { state.result != nil }, set: { if !$0 { state.result = nil } })) { Button("OK") { state.result = nil } }
    }
}

/// what restoring would do: the new chunks, the ones it changes, the ones that are the same; then Restore
struct RestorePlanSheet: View {
    let backup: ChunkBackup
    let plan: ChunkRestorePlan
    let restore: () -> Void
    let cancel: () -> Void

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack(spacing: 12) {
                        Image(systemName: "externaldrive.fill").font(.system(size: 26)).foregroundStyle(Neon.cyan).shadow(color: Neon.cyan, radius: 6)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(plan.summary).font(.system(size: 17, weight: .bold, design: .rounded)).foregroundStyle(.white)
                                .accessibilityIdentifier("restore.summary")
                            Text("Backed up \(backup.made.formatted(date: .abbreviated, time: .shortened)) · \(backup.app)").font(.system(size: 12)).foregroundStyle(.white.opacity(0.55))
                        }
                    }
                } footer: {
                    Text("It is one step: you can undo it right after. The chunks it replaces are first saved in a \"before restore\" file; the ones here that it doesn't have stay.")
                }
                group("New", plan.new, "plus.circle.fill", Neon.lime)
                group("Changed (replaced by the backup's)", plan.changed, "arrow.triangle.2.circlepath.circle.fill", Neon.amber)
                group("Unchanged", plan.unchanged, "equal.circle.fill", .white.opacity(0.4))
                group("Here only (kept)", plan.kept, "pin.circle.fill", Neon.cyan)
            }
            .scrollContentBackground(.hidden)
            .background(NightBackdrop(zone: "city"))
            .navigationTitle("Restore chunks")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel", action: cancel).accessibilityIdentifier("restore.cancel") }
                ToolbarItem(placement: .confirmationAction) {
                    Button { restore() } label: { Text(plan.isEmpty ? "Nothing to restore" : "Restore \(plan.new.count + plan.changed.count)").bold() }
                        .disabled(plan.isEmpty).accessibilityIdentifier("restore.confirm")
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    @ViewBuilder private func group(_ title: String, _ ids: [String], _ symbol: String, _ tint: Color) -> some View {
        if !ids.isEmpty {
            Section("\(title) · \(ids.count)") {
                ForEach(ids, id: \.self) { id in
                    Label(ChunkNames.shown(id).capitalized, systemImage: symbol).foregroundStyle(.white).symbolRenderingMode(.palette).tint(tint)
                        .labelStyle(TintedIconLabel(tint: tint))
                }
            }
        }
    }
}

private struct TintedIconLabel: LabelStyle {
    let tint: Color
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 10) { configuration.icon.foregroundStyle(tint); configuration.title }
    }
}
