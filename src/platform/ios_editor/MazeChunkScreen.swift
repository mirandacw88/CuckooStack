// A maze chunk in the editor: the maze editor (MazeModel, MazeLayout: its tools, cells, canvas, checker and history) on a doorless
// block of cells, with the chunk's settings in the inspector (the same card a course chunk has). MazeChunkModel is what the maze is
// part of here, as a level is for a gauntlet (MazeHost): it keeps the chunk's header, says which level the checker judges at (the
// start of the first range), whether the chunk can be flipped, and tells the chunk list what the checker made of it.
import Combine
import SwiftUI

@MainActor
final class MazeChunkModel: ObservableObject, MazeHost, ChunkSettingsHost {
    let session: ChunkSession
    private let chunkStore: ChunkStore
    private(set) var maze: MazeModel!
    @Published var message: String?
    @Published var notice: String?
    @Published private(set) var number: Int
    @Published private(set) var canMirror = true
    @Published private(set) var whyNot = ""
    private var sink: AnyCancellable?

    init?(chunk id: String, chunks: ChunkStore? = nil) {
        let chunks = chunks ?? ChunkStore.shared
        guard let text = chunks.text(of: id) else { return nil }
        let file = Chunks.parse(id, text)
        guard file.ok, file.isMaze, file.body != nil else {
            NSLog("[CuckooEditor] chunk %@ doesn't open: %@", id, file.error ?? "it isn't a maze chunk")
            return nil
        }
        number = file.first ?? 1
        chunkStore = chunks
        session = ChunkSession(id: id, header: file.header)
        guard let m = MazeModel(name: id, host: self, storage: ChunkMazeStorage(session: session, chunks: chunks)) else { return nil }
        maze = m
        m.chunkChecked = { [weak self] ok in self?.checked(ok) }
        m.onEdited = { [weak self] in self?.edited() }
        refreshMirror()
        sink = chunks.changes.receive(on: DispatchQueue.main).sink { [weak self] change in self?.chunkChanged(change.id, change.text) }
    }

    // MARK: MazeHost

    var zone: String { ChunkWords.zone(forLevel: number) }
    var accent: Color { ZoneLook.of(zone).accent }
    var courseText: String? { nil }
    func gridChanged(_ name: String, rows: [String]) {}
    func renamedMaze(_ old: String, to new: String) -> Bool { true }
    func closeMaze() {}
    func mazePlace(_ name: String) -> (c0: Int, cell: Double)? { nil }
    var mirrorChecked: Bool { session.header.mirror && canMirror }

    // MARK: ChunkSettingsHost

    var chunkCanMirror: Bool { canMirror }
    var chunkWhyNot: String { whyNot }
    var chunkIsMaze: Bool { true }
    var chunkReach: (reach: Double, pad: Double)? { nil }
    var title: String { session.title }

    /// a chunk's settings changed (ranges, weight, uses, mirror, description): its file saved. A first range that moved changes the
    /// level the checker judges it at; Mirror on or off asks the checker to judge it flipped (or not)
    func setHeader(_ change: (inout ChunkHeader) -> Void) {
        var h = session.header
        change(&h)
        guard h != session.header else { return }
        let mirrorChanged = h.mirror != session.header.mirror, firstChanged = h.first != number
        session.header = h
        if firstChanged { number = h.first }
        session.save(body: maze.doc.text, to: chunkStore)
        refreshMirror()
        if mirrorChanged || firstChanged { maze.mirrorCheckChanged() }
    }

    func renameChunk(_ typed: String) -> Bool { maze.rename(typed) }

    // MARK: following the maze and the store

    /// whether the block can be flipped now (a spring in it can't), and why not
    private func refreshMirror() {
        let f = Chunks.parse(session.id, Chunks.text(session.header, body: maze.doc.text))
        let can = f.canMirror ?? true
        if can != canMirror { canMirror = can; maze.mirrorCheckChanged() }
        whyNot = f.whyNot ?? ""
    }

    private func edited() { refreshMirror() }

    /// the checker's word, kept for the chunk list
    private func checked(_ ok: Bool) {
        chunkStore.setVerdict(session.id, cleared: ok, seconds: 0, key: ChunkStore.verdictKey(first: number, flipped: mirrorChecked, body: maze.doc.text))
    }

    /// a restore (or its undo) changed this chunk's file: the settings and the ranges are taken up (the block itself the maze
    /// takes, as a step of its history)
    private func chunkChanged(_ id: String, _ text: String?) {
        guard id == session.id, let text else { return }
        let f = Chunks.parse(id, text)
        guard f.ok, f.isMaze else { return }
        session.header = f.header
        if let first = f.first, first != number { number = first }
        refreshMirror()
        maze.mirrorCheckChanged()
        notice = "Restored from a backup"
    }
}

/// a maze chunk's inspector: its settings, then the checker, the egg meter and the history
struct MazeChunkInspector: View {
    @ObservedObject var host: MazeChunkModel
    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                ChunkSettingsCard(model: host, session: host.session)
                MazeChecklistCard(maze: host.maze)
                MazeMeterCard(maze: host.maze)
                MazeHistoryCard(maze: host.maze)
            }
            .padding(12)
        }
        .scrollIndicators(.hidden)
        .background(NightBackdrop(zone: host.zone))
    }
}

/// a maze chunk's screen: the maze editor's layout for the device, the inspector beside it (a sheet on an iPhone)
struct MazeChunkScreen: View {
    let id: String
    @State private var model: MazeChunkModel?
    @State private var failed = false
    @ObservedObject private var store = ChunkStore.shared

    var body: some View {
        Group {
            if let model { MazeChunkLayout(host: model) }
            else if failed {
                ContentUnavailableView("\(ChunkNames.shown(id).capitalized) doesn't read", systemImage: "exclamationmark.triangle",
                                       description: Text(store.entry(id)?.problem.isEmpty == false ? store.entry(id)?.problem ?? "" : "Its file on this device is broken."))
            } else { ProgressView().tint(Neon.magenta) }
        }
        .task(id: id) {
            if let m = MazeChunkModel(chunk: id) { model = m } else { failed = true }
        }
    }
}

struct MazeChunkLayout: View {
    @ObservedObject var host: MazeChunkModel
    @ObservedObject private var maze: MazeModel
    @Environment(\.horizontalSizeClass) private var hsize
    @Environment(\.verticalSizeClass) private var vsize
    @EnvironmentObject private var layout: AppLayout
    @State private var inspector = false
    private var isPad: Bool { hsize == .regular && vsize == .regular }

    init(host: MazeChunkModel) {
        self.host = host
        _maze = ObservedObject(wrappedValue: host.maze)
    }

    var body: some View {
        MazeLayout(maze: maze, isPad: isPad, landscapePhone: !isPad && vsize == .compact, inspector: $inspector)
            .inspector(isPresented: $inspector) {
                MazeChunkInspector(host: host)
                    .inspectorColumnWidth(min: 290, ideal: 330, max: 420)
                    .presentationDetents([.fraction(0.45), .large])
                    .presentationBackgroundInteraction(.enabled(upThrough: .fraction(0.45)))
            }
            .background(NightBackdrop(zone: host.zone))
            .overlay(alignment: .top) {
                if let n = host.notice {
                    Label(n, systemImage: "arrow.down.circle.fill")
                        .font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(.white)
                        .padding(.horizontal, 14).padding(.vertical, 9)
                        .glass(Neon.cyan, radius: 20)
                        .padding(.top, 6)
                        .transition(.move(edge: .top).combined(with: .opacity))
                        .task(id: n) {
                            try? await Task.sleep(for: .seconds(2.5))
                            withAnimation(.easeOut(duration: 0.3)) { host.notice = nil }
                        }
                }
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.8), value: host.notice)
            .navigationTitle("Chunk · \(host.title)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                if isPad {
                    ToolbarItemGroup(placement: .topBarLeading) {
                        HomeButton()
                        Button {
                            withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) { layout.columns = layout.columns == .detailOnly ? .all : .detailOnly }
                            Haptic.tap()
                        } label: { Label("Chunks", systemImage: "sidebar.leading").labelStyle(.titleAndIcon) }
                        .accessibilityLabel(layout.columns == .detailOnly ? "Show the chunks" : "Hide the chunks")
                        .accessibilityIdentifier("editor.sidebar")
                    }
                }
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button { maze.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                        .disabled(!(maze.history?.canUndo ?? false)).accessibilityLabel("Undo")
                    Button { maze.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                        .disabled(!(maze.history?.canRedo ?? false)).accessibilityLabel("Redo")
                    Button { withAnimation { inspector.toggle() } } label: { Image(systemName: "sidebar.trailing") }
                        .keyboardShortcut("i", modifiers: [.command, .option]).accessibilityLabel("Inspector")
                }
            }
            .alert("Not that", isPresented: Binding(get: { host.message != nil }, set: { if !$0 { host.message = nil } })) {
                Button("OK") { host.message = nil }
            } message: { Text(host.message ?? "") }
            .onAppear {
                inspector = isPad && UIScreen.main.bounds.width > UIScreen.main.bounds.height
                if ProcessInfo.processInfo.arguments.contains("--inspector") { inspector = true }
            }
    }
}
