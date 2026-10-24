// What the editor opens on: a start screen with two big buttons, Edit Levels and Edit Chunks, and the two places they go
// to (each a list beside an editor). A Home button in each comes back. Launch arguments: --open N goes straight to the
// levels (that one open), --chunks [id] to the chunks (that one open).
import SwiftUI

/// a Home button's action, for the editors' toolbars
private struct GoHomeKey: EnvironmentKey { static let defaultValue: (() -> Void)? = nil }
extension EnvironmentValues {
    var goHome: (() -> Void)? {
        get { self[GoHomeKey.self] }
        set { self[GoHomeKey.self] = newValue }
    }
}

/// the Home button: back to the start screen
struct HomeButton: View {
    @Environment(\.goHome) private var goHome
    var body: some View {
        if let goHome {
            Button { Haptic.tap(); goHome() } label: { Label("Home", systemImage: "house.fill").labelStyle(.titleAndIcon) }
                .keyboardShortcut("h", modifiers: [.command, .control])
                .accessibilityLabel("Home: the start screen")
                .accessibilityIdentifier("home")
        }
    }
}

struct RootView: View {
    @ObservedObject var store: LevelStore
    @ObservedObject var chunks: ChunkStore
    @ObservedObject var sync: SyncEngine
    @StateObject private var layout: AppLayout
    @State private var mode: AppMode
    private let launch: LaunchRequest

    init(store: LevelStore, chunks: ChunkStore, sync: SyncEngine, launch: LaunchRequest = LaunchRequest(ProcessInfo.processInfo.arguments)) {
        self.store = store
        self.chunks = chunks
        self.sync = sync
        self.launch = launch
        _mode = State(initialValue: launch.mode)
        // (--detail: the editor alone from the first frame, not folded away after it has shown the list)
        _layout = StateObject(wrappedValue: AppLayout(detailOnly: launch.detailOnly && launch.mode != .home))
    }

    var body: some View {
        ZStack {
            switch mode {
            case .home:
                StartScreen(store: store, chunks: chunks) { go($0) }
                    .transition(.asymmetric(insertion: .opacity.combined(with: .scale(scale: 1.04)), removal: .opacity.combined(with: .scale(scale: 0.96))))
            case .levels:
                LevelsRoot(store: store, sync: sync, launch: launch)
                    .transition(.asymmetric(insertion: .opacity.combined(with: .scale(scale: 0.97)), removal: .opacity))
            case .chunks:
                ChunksRoot(store: chunks, levels: store, sync: sync, launch: launch)
                    .transition(.asymmetric(insertion: .opacity.combined(with: .scale(scale: 0.97)), removal: .opacity))
            }
        }
        .tint(Neon.cyan)
        .preferredColorScheme(.dark)
        .environmentObject(layout)
        .environment(\.goHome, mode == .home ? nil : { go(.home) })
    }

    private func go(_ next: AppMode) {
        // (the list showing again, as an iPad opens it)
        layout.columns = UIDevice.current.userInterfaceIdiom == .pad ? .all : .automatic
        withAnimation(.spring(response: 0.5, dampingFraction: 0.86)) { mode = next }
    }
}

/// the levels: the list beside the editor (this was the app's whole window before there were chunks)
struct LevelsRoot: View {
    @ObservedObject var store: LevelStore
    @ObservedObject var sync: SyncEngine
    let launch: LaunchRequest
    @EnvironmentObject private var layout: AppLayout
    @State private var selection: Int?

    var body: some View {
        NavigationSplitView(columnVisibility: $layout.columns) {
            LevelList(store: store, selection: $selection, sync: sync)
                .navigationSplitViewColumnWidth(min: 260, ideal: 300, max: 360)
                .toolbar(removing: .sidebarToggle) // (the editor's own Levels button instead: never two)
        } detail: {
            if let n = selection {
                EditorScreen(number: n).id(n)
            } else {
                ZStack {
                    NightBackdrop(zone: "city")
                    VStack(spacing: 14) {
                        Image(systemName: "pencil.and.ruler.fill").font(.system(size: 54)).foregroundStyle(Neon.cyan).shadow(color: Neon.cyan, radius: 14)
                            .symbolEffect(.bounce, options: .nonRepeating, value: selection)
                        Text("Pick a level, or make a new one").font(.system(size: 20, weight: .bold, design: .rounded)).foregroundStyle(.white)
                    }
                }
            }
        }
        .onChange(of: selection) { _, n in
            // (an iPad: a level picked, the list folds away and the canvas gets the room; the sidebar button or a
            // swipe from the left brings it back)
            if n != nil && UIDevice.current.userInterfaceIdiom == .pad {
                withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) { layout.columns = .detailOnly }
            }
        }
        .onAppear {
            if let n = launch.level {
                selection = n
                if launch.detailOnly { layout.columns = .detailOnly }
            }
        }
    }
}

/// the chunks: the list beside the editor, as the levels have
struct ChunksRoot: View {
    @ObservedObject var store: ChunkStore
    @ObservedObject var levels: LevelStore
    @ObservedObject var sync: SyncEngine
    let launch: LaunchRequest
    @EnvironmentObject private var layout: AppLayout
    @State private var selection: String?

    var body: some View {
        NavigationSplitView(columnVisibility: $layout.columns) {
            ChunkList(store: store, levels: levels, sync: sync, selection: $selection)
                .navigationSplitViewColumnWidth(min: 260, ideal: 300, max: 360)
                .toolbar(removing: .sidebarToggle)
        } detail: {
            if let id = selection, store.exists(id) {
                ChunkEditorScreen(id: id).id(id)
            } else {
                ZStack {
                    NightBackdrop(zone: "city")
                    VStack(spacing: 14) {
                        Image(systemName: "puzzlepiece.extension.fill").font(.system(size: 54)).foregroundStyle(Neon.magenta).shadow(color: Neon.magenta, radius: 14)
                            .symbolEffect(.bounce, options: .nonRepeating, value: selection)
                        Text(store.chunks.isEmpty ? "Make a chunk to start" : "Pick a chunk, or make a new one").font(.system(size: 20, weight: .bold, design: .rounded)).foregroundStyle(.white)
                    }
                }
            }
        }
        .onChange(of: selection) { _, id in
            if id != nil && UIDevice.current.userInterfaceIdiom == .pad {
                withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) { layout.columns = .detailOnly }
            }
        }
        .onReceive(store.renames) { r in if selection == r.from { selection = r.to } } // (renamed: the editor follows it)
        .onChange(of: store.chunks.map(\.id)) { _, ids in
            if let s = selection, !ids.contains(s) { selection = nil } // (taken out, or an undone restore: nothing to show)
        }
        .onAppear {
            if let id = launch.chunk {
                selection = id
                if launch.detailOnly { layout.columns = .detailOnly }
            }
        }
    }
}

// MARK: the start screen

/// two big buttons: Edit Levels and Edit Chunks, each with a little moving picture of what it is
struct StartScreen: View {
    @ObservedObject var store: LevelStore
    @ObservedObject var chunks: ChunkStore
    let open: (AppMode) -> Void
    @State private var shown = false

    var body: some View {
        ZStack {
            NightBackdrop(zone: "city")
            VStack(spacing: 26) {
                VStack(spacing: 6) {
                    Image(systemName: "pencil.and.ruler.fill").font(.system(size: 34, weight: .semibold)).foregroundStyle(Neon.cyan)
                        .shadow(color: Neon.cyan, radius: 12).symbolEffect(.bounce, options: .nonRepeating, value: shown)
                    Text("CUCKOO EDITOR").font(.system(size: 30, weight: .heavy, design: .rounded)).tracking(5).foregroundStyle(.white)
                        .shadow(color: Neon.cyan.opacity(0.8), radius: 14)
                    Text("Build the courses, and the set pieces the generators place").font(.system(size: 14, design: .rounded)).foregroundStyle(.white.opacity(0.6))
                }
                .opacity(shown ? 1 : 0).offset(y: shown ? 0 : -14)
                ViewThatFits(in: .horizontal) {
                    HStack(spacing: 22) { levelsCard; chunksCard }
                    VStack(spacing: 18) { levelsCard; chunksCard }
                }
                .frame(maxWidth: 760)
            }
            .padding(24)
        }
        .onAppear { withAnimation(.spring(response: 0.6, dampingFraction: 0.8)) { shown = true } }
    }

    private var levelsCard: some View {
        StartCard(title: "Edit Levels", detail: levelsDetail, accent: Neon.cyan, symbol: "road.lanes", index: 0, shown: shown, id: "start.levels") {
            Haptic.tap(); open(.levels)
        } art: { t in StartArt.course(t) }
    }

    private var chunksCard: some View {
        StartCard(title: "Edit Chunks", detail: chunksDetail, accent: Neon.magenta, symbol: "puzzlepiece.extension.fill", index: 1, shown: shown, id: "start.chunks") {
            Haptic.tap(); open(.chunks)
        } art: { t in StartArt.chunk(t) }
    }

    private var levelsDetail: String {
        let n = store.levels.count, dirty = store.levels.filter(\.dirty).count
        return "\(n) \(n == 1 ? "level" : "levels")" + (dirty > 0 ? " · \(dirty) not synced" : "")
    }
    private var chunksDetail: String {
        let n = chunks.chunks.count, dirty = chunks.chunks.filter(\.dirty).count
        return n == 0 ? "None yet: make the first" : "\(n) \(n == 1 ? "chunk" : "chunks")" + (dirty > 0 ? " · \(dirty) not synced" : "")
    }
}

/// a start button: a glass card, a moving picture, its name and what's in it; it springs in, and dips as it's pressed
struct StartCard<Art: View>: View {
    let title: String
    let detail: String
    let accent: Color
    let symbol: String
    let index: Int
    let shown: Bool
    let id: String
    let action: () -> Void
    @ViewBuilder let art: (Double) -> Art

    var body: some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 14) {
                TimelineView(.animation(minimumInterval: 1.0 / 30)) { tl in
                    art(tl.date.timeIntervalSinceReferenceDate)
                }
                .frame(height: 130)
                .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(accent.opacity(0.35), lineWidth: 1))
                HStack(spacing: 12) {
                    Image(systemName: symbol).font(.system(size: 22, weight: .semibold)).foregroundStyle(accent).shadow(color: accent, radius: 6)
                        .frame(width: 34)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(title).font(.system(size: 22, weight: .heavy, design: .rounded)).foregroundStyle(.white)
                        Text(detail).font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(accent.opacity(0.9))
                    }
                    Spacer(minLength: 0)
                    Image(systemName: "chevron.right.circle.fill").font(.system(size: 26)).foregroundStyle(accent).shadow(color: accent.opacity(0.8), radius: 8)
                }
            }
            .padding(14)
            .frame(maxWidth: .infinity, minHeight: 230)
            .glass(accent, radius: 24)
        }
        .buttonStyle(PressSpring())
        .scaleEffect(shown ? 1 : 0.88)
        .opacity(shown ? 1 : 0)
        .offset(y: shown ? 0 : 30)
        .animation(.spring(response: 0.6, dampingFraction: 0.78).delay(0.12 * Double(index)), value: shown)
        .accessibilityIdentifier(id)
        .accessibilityLabel(title)
        .accessibilityHint(detail)
    }
}

/// the start buttons' pictures, drawn each frame from the time: the neon course scrolling by under a bouncing egg (levels);
/// a gap in the course that a magenta piece slides into, over and over (chunks)
enum StartArt {
    private static func profile(_ x: Double) -> Double { // (the course's height at x: steps up and down)
        let k = Int(floor(x / 56))
        return [0, 1, 1, 2, 0, 0, 3, 3, 1, 0, 2, 2][((k % 12) + 12) % 12] * 14
    }

    static func course(_ t: Double) -> some View {
        Canvas { ctx, size in
            ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .linearGradient(Gradient(colors: [Color(hex: "#0b0720"), Color(hex: "#1a0f3a")]), startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            let base = size.height - 22, scroll = t * 46
            var wall = Path()
            var x = 0.0
            wall.move(to: CGPoint(x: 0, y: size.height))
            while x <= size.width + 8 {
                let h = profile(x + scroll)
                wall.addLine(to: CGPoint(x: x, y: base - h))
                x += 4
            }
            wall.addLine(to: CGPoint(x: size.width, y: size.height))
            wall.closeSubpath()
            ctx.fill(wall, with: .color(Color(hex: "#0e1a2c")))
            ctx.stroke(wall, with: .color(Neon.cyan.opacity(0.85)), lineWidth: 1.6)
            var grid = Path()
            var gx = -(scroll.truncatingRemainder(dividingBy: 20))
            while gx < size.width { grid.move(to: CGPoint(x: gx, y: base - 50)); grid.addLine(to: CGPoint(x: gx, y: size.height)); gx += 20 }
            ctx.stroke(grid, with: .color(Neon.cyan.opacity(0.07)), lineWidth: 0.5)
            // the egg runs along at a third of the way, riding the profile
            let ex = size.width * 0.3, ey = base - profile(ex + scroll) - 9 - 5 * abs(sin(t * 5))
            ctx.fill(Path(ellipseIn: CGRect(x: ex - 6, y: ey - 8, width: 12, height: 16)), with: .color(Neon.egg))
            ctx.stroke(Path(ellipseIn: CGRect(x: ex - 9, y: ey - 11, width: 18, height: 22)), with: .color(Neon.egg.opacity(0.25)), lineWidth: 3)
            // a saw turning on the wall ahead
            let sx = size.width * 0.78, sy = base - profile(sx + scroll) - 12
            var saw = Path()
            for k in 0..<8 {
                let a = t * 6 + Double(k) * .pi / 4
                saw.move(to: CGPoint(x: sx, y: sy)); saw.addLine(to: CGPoint(x: sx + 10 * cos(a), y: sy + 10 * sin(a)))
            }
            ctx.stroke(saw, with: .color(Neon.red.opacity(0.9)), lineWidth: 2)
            ctx.stroke(Path(ellipseIn: CGRect(x: sx - 6, y: sy - 6, width: 12, height: 12)), with: .color(Neon.red), lineWidth: 1.5)
        }
    }

    static func chunk(_ t: Double) -> some View {
        Canvas { ctx, size in
            ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .linearGradient(Gradient(colors: [Color(hex: "#0b0720"), Color(hex: "#2a0f3a")]), startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            let base = size.height - 22, gapW = 62.0, gapX = size.width / 2 - gapW / 2
            // the course either side of a gap
            for (x0, x1) in [(0.0, gapX), (gapX + gapW, size.width)] {
                let top: CGFloat = base - 14
                let r = CGRect(x: x0, y: top, width: x1 - x0, height: size.height - top)
                ctx.fill(Path(r), with: .color(Color(hex: "#200a1d")))
                var edge = Path()
                edge.move(to: CGPoint(x: x0, y: top)); edge.addLine(to: CGPoint(x: x1, y: top))
                ctx.stroke(edge, with: .color(Neon.magenta.opacity(0.9)), lineWidth: 1.6)
            }
            // the piece: slides in from above, settles with a little bounce, rests, lifts away again
            let cycle = 3.4, p = (t.truncatingRemainder(dividingBy: cycle)) / cycle
            let drop: Double
            if p < 0.35 { let q = p / 0.35; drop = (1 - q * q) * -70 }
            else if p < 0.45 { drop = -5 * sin((p - 0.35) / 0.10 * .pi) }
            else if p < 0.8 { drop = 0 }
            else { let q = (p - 0.8) / 0.2; drop = -q * q * 80 }
            let alpha = p < 0.35 ? p / 0.35 : (p > 0.8 ? 1 - (p - 0.8) / 0.2 : 1)
            let piece = CGRect(x: gapX + 2, y: base - 38 + drop, width: gapW - 4, height: 52)
            var up = Path(roundedRect: piece, cornerRadius: 5)
            up.addPath(Path(ellipseIn: CGRect(x: piece.midX - 8, y: piece.minY - 6, width: 16, height: 12)))
            ctx.drawLayer { layer in
                layer.opacity = alpha
                layer.addFilter(.shadow(color: Neon.magenta.opacity(0.9), radius: 10))
                layer.fill(up, with: .color(Neon.magenta.opacity(0.28)))
                layer.stroke(up, with: .color(Neon.magenta), lineWidth: 2)
                // a saw set into it
                let cx = piece.midX, cy = piece.midY
                var s = Path()
                for k in 0..<6 { let a = t * 5 + Double(k) * .pi / 3; s.move(to: CGPoint(x: cx, y: cy)); s.addLine(to: CGPoint(x: cx + 9 * cos(a), y: cy + 9 * sin(a))) }
                layer.stroke(s, with: .color(Neon.red), lineWidth: 2)
            }
            // the gap's outline pulses while it's empty
            let pulse = 0.35 + 0.25 * sin(t * 4)
            ctx.stroke(Path(roundedRect: CGRect(x: gapX, y: base - 38, width: gapW, height: 52), cornerRadius: 6), with: .color(Neon.magenta.opacity(pulse)), style: StrokeStyle(lineWidth: 1.2, dash: [4, 4]))
        }
    }
}
