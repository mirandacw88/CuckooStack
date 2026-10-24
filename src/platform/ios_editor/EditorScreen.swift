// A level in the editor, laid out for the device it's on and the way it's held, the same pieces either way:
//   iPhone upright: the canvas edge to edge, the tools along the bottom, the inspector a sheet, play full screen
//   iPhone on its side: the tools a rail at the side, the canvas the rest (all its height for the course)
//   iPad mini (regular both ways), upright or on its side: the tool rail beside the canvas, the minimap over it, the
//   inspector a column on the right (open by default when there's room for it: on its side), play a modal over the
//   editor, the game in an iPhone's frame (the real perspective)
import SwiftUI

enum PlayMode: String, Identifiable { case manual, bot; var id: String { rawValue } }

/// the window's split: the levels list beside the editor, or the editor alone (watching the bot: the left side out of
/// the way)
final class AppLayout: ObservableObject {
    // an iPad opens with the levels list showing (upright too, where it would start hidden): pick one to start
    @Published var columns: NavigationSplitViewVisibility = UIDevice.current.userInterfaceIdiom == .pad ? .all : .automatic
    init(detailOnly: Bool = false) { if detailOnly { columns = .detailOnly } }
}

struct EditorScreen: View {
    let number: Int
    @State private var model: EditorModel?
    @State private var failed = false

    var body: some View {
        Group {
            if let model { EditorLayout(model: model) }
            else if failed { ContentUnavailableView("Level \(number) doesn't read", systemImage: "exclamationmark.triangle", description: Text("Its file on this device is broken; sync takes the Mac's.")) }
            else { ProgressView().tint(Neon.cyan) }
        }
        .task(id: number) {
            if let m = EditorModel(number: number) { model = m } else { failed = true }
        }
    }
}

/// a chunk in the editor: a course chunk's body (the same canvas, tools, checker and bot as a level's) or a maze chunk's block of
/// cells (the maze editor), with the chunk's settings in the inspector
struct ChunkEditorScreen: View {
    let id: String
    @State private var model: EditorModel?
    @State private var failed = false
    @ObservedObject private var store = ChunkStore.shared

    var body: some View {
        if store.entry(id)?.maze == true { MazeChunkScreen(id: id) } else { course }
    }

    private var course: some View {
        Group {
            if let model { EditorLayout(model: model) }
            else if failed {
                ContentUnavailableView("\(ChunkNames.shown(id).capitalized) doesn't read", systemImage: "exclamationmark.triangle",
                                       description: Text(store.entry(id)?.problem ?? "Its file on this device is broken."))
            } else { ProgressView().tint(Neon.magenta) }
        }
        .task(id: id) {
            if let m = EditorModel(chunk: id) { model = m } else { failed = true }
        }
    }
}

struct EditorLayout: View {
    @ObservedObject var model: EditorModel
    @Environment(\.horizontalSizeClass) private var hsize
    @Environment(\.verticalSizeClass) private var vsize
    @State private var inspector = false
    private var isPad: Bool { hsize == .regular && vsize == .regular } // (a big phone on its side is regular across: still a phone)
    @State private var playing: PlayMode?
    @State private var inline: PlayMode?
    @EnvironmentObject private var layout: AppLayout
    @State private var columnsBefore: NavigationSplitViewVisibility? // (the levels list as it was, before watching)

    var body: some View {
        Group {
            if isPad { regular } else if vsize == .compact { phoneLandscape } else { compact }
        }
        .background(NightBackdrop(zone: model.zone))
        .overlay(alignment: .top) {
            if let n = model.notice {
                Label(n, systemImage: "arrow.down.circle.fill")
                    .font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(.white)
                    .padding(.horizontal, 14).padding(.vertical, 9)
                    .glass(Neon.cyan, radius: 20)
                    .accessibilityIdentifier("editor.notice")
                    .padding(.top, 6)
                    .transition(.move(edge: .top).combined(with: .opacity))
                    .task(id: n) {
                        try? await Task.sleep(for: .seconds(2.5))
                        withAnimation(.easeOut(duration: 0.3)) { model.notice = nil }
                    }
            }
        }
        .animation(.spring(response: 0.4, dampingFraction: 0.8), value: model.notice)
        .animation(.spring(response: 0.4, dampingFraction: 0.85), value: model.maze?.name)
        .sheet(isPresented: $model.historyOpen) {
            HistoryWindow(model: model).presentationDetents([.medium, .large]).presentationDragIndicator(.visible)
        }
        .navigationTitle(model.chunk != nil ? "Chunk · \(model.title)" : model.maze.map { "The \(Builtin.gauntletShown($0.name)) · level \(model.number)" } ?? "\(model.number) · \(model.title)")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar { toolbar }
        .alert("Not that", isPresented: Binding(get: { model.message != nil }, set: { if !$0 { model.message = nil } })) {
            Button("OK") { model.message = nil }
        } message: { Text(model.message ?? "") }
        .onAppear {
            inspector = isPad && UIScreen.main.bounds.width > UIScreen.main.bounds.height
            let args = ProcessInfo.processInfo.arguments
            if args.contains("--inspector") { inspector = true }
            if args.contains("--play-bot") { play(true) }
            if args.contains("--history") { model.historyOpen = true }
            if args.contains("--pencil-palette") { model.palette = CGPoint(x: 360, y: 900) } // (the simulator has no Pencil to squeeze)
            if args.contains("--maze") { model.openMaze() }
        }
    }

    // MARK: iPad

    private var regular: some View {
        Group {
            if let maze = model.maze { MazeLayout(maze: maze, isPad: true, landscapePhone: false, inspector: $inspector) }
            else { courseRegular }
        }
        .inspector(isPresented: $inspector) {
            Group {
                if let maze = model.maze { MazeInspector(maze: maze, onPlay: play) } else { Inspector(model: model, onPlay: play) }
            }
            .inspectorColumnWidth(min: 290, ideal: 330, max: 420)
        }
        // play: a modal over the editor, the game in an iPhone's frame (the real perspective: PlayPanel)
        .fullScreenCover(item: $inline, onDismiss: restoreColumns) { mode in
            PlayPanel(model: model, bot: mode == .bot, fullScreen: false) { closeInline() }
                .presentationBackground(.clear)
        }
    }

    private var courseRegular: some View {
        HStack(alignment: .top, spacing: 10) {
            if inline != .bot { // (watching the bot, the tools are out of the way: nothing to edit with them)
                ToolPalette(model: model, vertical: true).transition(.move(edge: .leading).combined(with: .opacity))
            }
            VStack(spacing: 8) {
                HStack(spacing: 8) {
                    Minimap(model: model, viewport: model.viewport)
                    if !inspector { pills.transition(.move(edge: .trailing).combined(with: .opacity)) }
                }
                CourseCanvasArea(model: model)
                    .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).strokeBorder(model.accent.opacity(0.35), lineWidth: 1))
                    .frame(maxHeight: .infinity)
            }
        }
        .padding(10)
    }

    // MARK: iPhone

    /// the bot's verdict and the checker's notes, a tap from the inspector
    private var pills: some View {
        HStack {
            let v = model.botVerdict
            Button { inspector = true } label: { StatusPill(text: v.text, symbol: v.symbol, tint: v.tint, busy: model.botBusy || model.botStale) }
                .buttonStyle(.plain)
            if isPad { Spacer(minLength: 0) } else { Spacer() }
            if !isPad { // (the iPhone's top bar has room for its title only: the history here)
                Button { model.historyOpen = true } label: {
                    Image(systemName: "clock.arrow.circlepath").font(.system(size: 14, weight: .bold)).foregroundStyle(.white)
                        .frame(width: 34, height: 32).background(Capsule().fill(.white.opacity(0.1)))
                        .overlay(Capsule().strokeBorder(.white.opacity(0.25)))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("History")
            }
            if !model.notes.isEmpty {
                Button { inspector = true; model.focusedNote = model.notes.first } label: {
                    StatusPill(text: "\(model.notes.count)", symbol: "exclamationmark.triangle.fill", tint: Neon.amber)
                }
                .buttonStyle(.plain)
                .transition(.scale.combined(with: .opacity))
            }
        }
        .animation(.spring(response: 0.4), value: model.notes.count)
        .fixedSize(horizontal: isPad, vertical: false)
    }

    private var compact: some View {
        Group {
            if let maze = model.maze { MazeLayout(maze: maze, isPad: false, landscapePhone: false, inspector: $inspector) } else { courseCompact }
        }
        .modifier(PhoneSheets(model: model, inspector: $inspector, playing: $playing, play: play))
    }

    private var courseCompact: some View {
        ZStack(alignment: .bottom) {
            CourseCanvasArea(model: model, insets: UIEdgeInsets(top: 52, left: 0, bottom: 150, right: 0)).ignoresSafeArea(edges: [.bottom, .horizontal])
            VStack {
                pills.padding(.horizontal, 12)
                Spacer()
                ToolPalette(model: model, vertical: false).padding(.horizontal, 8).padding(.bottom, 4)
            }
        }
    }

    /// on its side: the tools a rail at the side, so the course gets the screen's whole height
    private var phoneLandscape: some View {
        Group {
            if let maze = model.maze { MazeLayout(maze: maze, isPad: false, landscapePhone: true, inspector: $inspector) } else { coursePhoneLandscape }
        }
        .modifier(PhoneSheets(model: model, inspector: $inspector, playing: $playing, play: play))
    }

    private var coursePhoneLandscape: some View {
        HStack(spacing: 6) {
            ToolPalette(model: model, vertical: true).padding(.vertical, 4)
            ZStack(alignment: .top) {
                CourseCanvasArea(model: model, insets: UIEdgeInsets(top: 40, left: 0, bottom: 0, right: 0))
                    .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                pills.padding(.horizontal, 8).padding(.top, 4)
            }
        }
        .padding(.horizontal, 4)
    }

    private func play(_ bot: Bool) {
        guard isPad else { playing = bot ? .bot : .manual; return }
        withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) {
            if bot && columnsBefore == nil { // watching: the levels list out of the way too
                columnsBefore = layout.columns
                layout.columns = .detailOnly
            }
            inline = bot ? .bot : .manual
        }
    }

    private func closeInline() { inline = nil }

    private func restoreColumns() {
        withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) {
            if let before = columnsBefore { layout.columns = before; columnsBefore = nil } // (back as it was)
        }
    }

    @ToolbarContentBuilder private var toolbar: some ToolbarContent {
        if let maze = model.maze { mazeToolbar(maze) } else { courseToolbar }
    }

    /// the maze open: back to the course, its undo and redo, play from inside it
    @ToolbarContentBuilder private func mazeToolbar(_ maze: MazeModel) -> some ToolbarContent {
        ToolbarItem(placement: .topBarLeading) {
            Button { withAnimation(.spring(response: 0.4, dampingFraction: 0.85)) { model.closeMaze() } } label: {
                Label("Done", systemImage: "checkmark.circle.fill").labelStyle(.titleAndIcon)
            }
            .tint(Neon.straw)
            .accessibilityLabel("Done: back to the course")
        }
        ToolbarItemGroup(placement: .topBarTrailing) {
            Button { maze.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                .disabled(!(maze.history?.canUndo ?? false)).accessibilityLabel("Undo")
            Button { maze.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                .disabled(!(maze.history?.canRedo ?? false)).accessibilityLabel("Redo")
            Button { model.generateMaze() } label: { Image(systemName: model.generating ? "hourglass" : "dice.fill") }
                .disabled(model.generating).accessibilityLabel("Roll a new maze")
            Menu {
                Button { play(false) } label: { Label("Play the maze", systemImage: "play.fill") }
                Button { play(true) } label: { Label("Watch the bot", systemImage: "cpu") }
                if maze.scene?.start != nil { Button { maze.clearStart() } label: { Label("From the maze's door", systemImage: "flag.slash") } }
            } label: { Image(systemName: "play.fill") } primaryAction: { play(false) }
                .keyboardShortcut(.space, modifiers: [])
                .accessibilityLabel("Play")
            if isPad {
                Button { withAnimation { inspector.toggle() } } label: { Image(systemName: "sidebar.trailing") }
                    .keyboardShortcut("i", modifiers: [.command, .option]).accessibilityLabel("Inspector")
            }
        }
    }

    @ToolbarContentBuilder private var courseToolbar: some ToolbarContent {
        if isPad { // Home, and the levels list (or the chunks'), in and out (our own: iPadOS 26 doesn't always show the split view's)
            ToolbarItemGroup(placement: .topBarLeading) {
                HomeButton()
                Button {
                    withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) { layout.columns = layout.columns == .detailOnly ? .all : .detailOnly }
                    Haptic.tap()
                } label: { Label(model.chunk != nil ? "Chunks" : "Levels", systemImage: "sidebar.leading").labelStyle(.titleAndIcon) }
                .keyboardShortcut("s", modifiers: [.command, .control])
                .accessibilityLabel(layout.columns == .detailOnly ? "Show the \(model.chunk != nil ? "chunks" : "levels")" : "Hide the \(model.chunk != nil ? "chunks" : "levels")")
                .accessibilityIdentifier("editor.sidebar")
            }
        }
        ToolbarItemGroup(placement: .topBarTrailing) {
            Button { model.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                .disabled(!(model.history?.canUndo ?? false)).accessibilityLabel("Undo")
            Button { model.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                .disabled(!(model.history?.canRedo ?? false)).accessibilityLabel("Redo")
            if isPad { Menu {
                Button { model.historyOpen = true } label: { Label("History", systemImage: "clock.arrow.circlepath") }
                Divider()
                if model.chunk == nil {
                    Button { model.revertToSynced() } label: { Label("Back to the synced version", systemImage: "arrow.uturn.backward.circle") }
                        .disabled(!model.canRevertToSynced)
                    Button { model.revertToOriginal() } label: { Label("Back to the game's original", systemImage: "gamecontroller") }
                        .disabled(!model.canRevertToOriginal)
                }
            } label: { Image(systemName: "clock.arrow.circlepath") } primaryAction: { model.historyOpen = true }
                .accessibilityLabel("History").accessibilityIdentifier("history.open") }
            Button("") { model.historyOpen = true }.keyboardShortcut("h", modifiers: [.command, .shift]).opacity(0).frame(width: 0)
            if model.chunk == nil { generatorsMenu }
            Menu {
                Button { play(false) } label: { Label("Play it", systemImage: "play.fill") }
                Button { play(true) } label: { Label("Watch the bot", systemImage: "cpu") }
                if model.doc.start != nil { Button { model.setStart(nil) } label: { Label("From the level's start", systemImage: "flag.slash") } }
            } label: { Image(systemName: "play.fill") } primaryAction: { play(false) }
                .keyboardShortcut(.space, modifiers: [])
                .accessibilityLabel("Play")
            if isPad {
                Button { withAnimation { inspector.toggle() } } label: { Image(systemName: "sidebar.trailing") }
                    .keyboardShortcut("i", modifiers: [.command, .option]).accessibilityLabel("Inspector")
            }
        }
    }

    /// the generators' menu (a level's): roll the course, add a maze, and what a roll may lay
    private var generatorsMenu: some View {
        Menu {
            Button { model.rollLevel() } label: { Label("Roll a new course", systemImage: "dice.fill") }
                .accessibilityIdentifier("roll.course")
            Button { model.generateMaze() } label: { Label(model.meta?.gauntlet == nil ? "Add a generated maze" : "Generate a new maze", systemImage: "square.grid.3x3.fill") }
                .disabled(model.generating)
            Section("A course roll may lay") {
                ForEach(model.generatedHazards) { h in
                    Toggle(isOn: Binding(get: { !model.genOff.contains(h.id) }, set: { on in if on { model.genOff.remove(h.id) } else { model.genOff.insert(h.id) } })) {
                        Label(h.plural.capitalized, systemImage: UIImage(systemName: h.icon) != nil ? h.icon : "diamond.fill")
                    }
                }
            }
            Section("A maze roll may have") {
                ForEach(model.registry.hazards.filter { model.mazeHazardIDs.contains($0.id) }) { h in
                    Toggle(isOn: Binding(get: { !model.mazeGenOff.contains(h.id) }, set: { on in if on { model.mazeGenOff.remove(h.id) } else { model.mazeGenOff.insert(h.id) } })) {
                        Label(h.plural.capitalized, systemImage: UIImage(systemName: h.icon) != nil ? h.icon : "diamond.fill")
                    }
                }
            }
        } label: { Image(systemName: model.generating ? "hourglass" : "dice.fill") } primaryAction: { model.rollLevel() }
            .accessibilityLabel("Generators")
    }
}

/// the iPhone's inspector (a sheet from the bottom) and play (full screen), upright or on its side
struct PhoneSheets: ViewModifier {
    @ObservedObject var model: EditorModel
    @Binding var inspector: Bool
    @Binding var playing: PlayMode?
    let play: (Bool) -> Void
    func body(content: Content) -> some View {
        content
            .inspector(isPresented: $inspector) {
                Group {
                    if let maze = model.maze { MazeInspector(maze: maze, onPlay: { bot in inspector = false; play(bot) }) }
                    else { Inspector(model: model, onPlay: { bot in inspector = false; play(bot) }) }
                }
                    .presentationDetents([.fraction(0.45), .large])
                    .presentationBackgroundInteraction(.enabled(upThrough: .fraction(0.45)))
            }
            .fullScreenCover(item: $playing) { mode in
                PlayPanel(model: model, bot: mode == .bot, fullScreen: true) { playing = nil }
            }
    }
}

@main
struct CuckooEditorApp: App {
    @StateObject private var store = LevelStore.shared
    @StateObject private var chunks = ChunkStore.shared
    @StateObject private var sync = SyncEngine.shared
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            RootView(store: store, chunks: chunks, sync: sync)
        }
        .onChange(of: phase) { _, p in if p == .active { Task { await sync.sync(quietly: true) } } }
    }
}
