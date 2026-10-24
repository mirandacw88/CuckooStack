// Play-testing a level as it's edited, in the game itself (CSEngineView: the real game, the editor's text and grids):
// played by hand, or the bot playing, fast-forwarded; from the level's start or the start flag. A run over says how it
// went; back to the editor, her way is drawn on the course and the canvas goes to where she crashed. Full screen on
// the iPhone; on the iPad, a modal over the editor with the game in an iPhone's frame, as big as an iPhone's screen
// and laid out as one (the real perspective).
import SwiftUI

struct EngineView: UIViewRepresentable {
    func makeUIView(context: Context) -> CSEngineView { CSEngineView.shared() }
    func updateUIView(_ v: CSEngineView, context: Context) {}
    static func dismantleUIView(_ v: CSEngineView, coordinator: ()) { v.pause() }
}

@MainActor
final class PlaySession: ObservableObject {
    @Published var bot: Bool { didSet { engine.bot = bot } }
    @Published var speed = 1 { didSet { engine.speed = speed } }
    @Published private(set) var ended: (cleared: Bool, x: Double, y: Double)?
    @Published private(set) var seconds: Double = 0
    @Published private(set) var x: Double = 0
    @Published private(set) var eggs = 0
    @Published private(set) var error: String?
    private let engine = CSEngineView.shared()
    private var timer: Timer?

    init(model: EditorModel, bot: Bool) {
        self.bot = bot
        var err: NSString?
        var grids: [String: String] = [:]
        for (name, rows) in LevelStore.shared.grids { grids[name] = rows.joined(separator: "\n") }
        let started: Bool
        if let maze = model.maze { // (from inside the maze: its start flag, or its door)
            let s = maze.playStart
            started = engine.playMaze(Int32(model.number), text: model.doc.text, grids: grids, col: Int32(s.col), row: Int32(s.row), dir: Int32(s.dir), bot: bot, error: &err)
        } else {
            started = engine.playLevel(Int32(model.number), text: model.doc.text, grids: grids, startX: model.doc.start ?? -1, bot: bot, error: &err)
        }
        if !started {
            error = (err as String?) ?? "It didn't start"
        }
        engine.speed = 1
        engine.onRunEnded = { [weak self] cleared, x, y in
            Task { @MainActor in
                withAnimation(.spring(response: 0.45, dampingFraction: 0.75)) { self?.ended = (cleared, x, y) }
                cleared ? Haptic.success() : Haptic.warning()
            }
        }
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.seconds = self.engine.seconds
                self.x = self.engine.henX
                self.eggs = self.engine.eggs
            }
        }
    }

    deinit { timer?.invalidate() }

    func restart() {
        withAnimation { ended = nil }
        engine.restart()
    }

    /// her way this run, for the canvas
    var trace: [CGPoint] { engine.trace.map { $0.cgPointValue } }

    func stop() {
        timer?.invalidate()
        engine.onRunEnded = nil
        engine.pause()
    }
}

struct PlayPanel: View {
    @ObservedObject var model: EditorModel
    @StateObject var session: PlaySession
    var fullScreen: Bool
    var onClose: () -> Void

    init(model: EditorModel, bot: Bool, fullScreen: Bool, onClose: @escaping () -> Void) {
        self.model = model
        _session = StateObject(wrappedValue: PlaySession(model: model, bot: bot))
        self.fullScreen = fullScreen
        self.onClose = onClose
    }

    var body: some View {
        Group { if fullScreen { phoneScreen } else { phoneOnIPad } }
            .onDisappear {
                session.stop()
                let engine = CSEngineView.shared()
                engine.phoneInsets = .zero
                engine.contentScaleFactor = UIScreen.main.nativeScale
            }
    }

    /// the game, or why it didn't start
    private var screen: some View {
        ZStack {
            Color.black
            if let e = session.error {
                VStack(spacing: 12) {
                    Image(systemName: "exclamationmark.triangle.fill").font(.largeTitle).foregroundStyle(Neon.red)
                    Text(e).multilineTextAlignment(.center).foregroundStyle(.white).padding()
                    Button("Back to the editor", action: close).buttonStyle(.borderedProminent).tint(model.accent)
                }
            } else {
                EngineView()
            }
        }
    }

    /// on an iPhone: the game edge to edge, its controls inside the safe area
    private var phoneScreen: some View {
        ZStack {
            screen.ignoresSafeArea()
            VStack {
                controls
                Spacer()
                if let end = session.ended { result(end).transition(.move(edge: .bottom).combined(with: .opacity)) }
            }
            .padding(16)
        }
    }

    /// on an iPad: the game in an iPhone's frame over the editor, as big as an iPhone's screen (393 × 852 points, its
    /// notch and home bar the game's safe area) and shrunk to fit; the controls under it, off the game's HUD
    static let phoneSize = CGSize(width: 393, height: 852)
    static let phoneInsets = UIEdgeInsets(top: 59, left: 0, bottom: 34, right: 0)

    private var phoneOnIPad: some View {
        GeometryReader { geo in
            let bezel: CGFloat = 12
            let outer = CGSize(width: Self.phoneSize.width + 2 * bezel, height: Self.phoneSize.height + 2 * bezel)
            let k = min(1, (geo.size.height - 96) / outer.height, (geo.size.width - 32) / outer.width)
            ZStack {
                Color.black.opacity(0.62).ignoresSafeArea()
                    .contentShape(Rectangle())
                    .onTapGesture { close() }
                VStack(spacing: 14) {
                    PhoneFrame(accent: model.accent, bezel: bezel) {
                        ZStack(alignment: .bottom) {
                            screen
                            if let end = session.ended {
                                result(end).padding(.horizontal, 12).padding(.bottom, Self.phoneInsets.bottom + 8)
                                    .transition(.move(edge: .bottom).combined(with: .opacity))
                            }
                        }
                        .frame(width: Self.phoneSize.width, height: Self.phoneSize.height)
                    }
                    .scaleEffect(k)
                    .frame(width: outer.width * k, height: outer.height * k)
                    controls.frame(maxWidth: max(outer.width * k, 420))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .onAppear {
                let engine = CSEngineView.shared()
                engine.phoneInsets = Self.phoneInsets
                engine.contentScaleFactor = UIScreen.main.nativeScale * max(0.5, k) // (no more pixels than it shows)
            }
        }
    }

    private var controls: some View {
        HStack(spacing: 8) {
            Button(action: close) {
                Image(systemName: "chevron.backward").font(.system(size: 15, weight: .bold))
                    .frame(width: 34, height: 34).background(Circle().fill(model.accent)).foregroundStyle(.black)
            }
            .buttonStyle(.plain)
            .keyboardShortcut(.escape, modifiers: [])
            .accessibilityLabel("Back to editing")
            Text(String(format: "%.0f m · %.1f s", session.x, session.seconds))
                .monospacedDigit().lineLimit(1).fixedSize()
                .font(.system(size: 14, weight: .bold, design: .rounded)).foregroundStyle(.white)
                .contentTransition(.numericText())
            Spacer(minLength: 4)
            Button { session.bot.toggle(); Haptic.tap() } label: {
                Image(systemName: session.bot ? "cpu.fill" : "hand.tap.fill").font(.system(size: 15, weight: .bold))
                    .frame(width: 38, height: 34)
                    .background(RoundedRectangle(cornerRadius: 10).fill(session.bot ? Neon.cyan.opacity(0.3) : .white.opacity(0.08)))
                    .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(session.bot ? Neon.cyan : .white.opacity(0.2)))
                    .foregroundStyle(session.bot ? Neon.cyan : .white)
                    .contentTransition(.symbolEffect(.replace))
            }
            .buttonStyle(.plain)
            .keyboardShortcut("b", modifiers: [])
            .accessibilityLabel(session.bot ? "The bot plays" : "You play")
            Button { session.speed = session.speed >= 4 ? 1 : session.speed * 2; Haptic.tap() } label: {
                Text("\(session.speed)×").font(.system(size: 14, weight: .heavy, design: .rounded)).monospacedDigit()
                    .frame(width: 40, height: 34)
                    .background(RoundedRectangle(cornerRadius: 10).fill(session.speed > 1 ? Neon.magenta.opacity(0.3) : .white.opacity(0.08)))
                    .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(session.speed > 1 ? Neon.magenta : .white.opacity(0.2)))
                    .foregroundStyle(.white)
            }
            .buttonStyle(.plain)
            .keyboardShortcut("f", modifiers: [])
            .accessibilityLabel("Speed \(session.speed) times")
            Button { session.restart() } label: {
                Image(systemName: "arrow.counterclockwise").font(.system(size: 15, weight: .bold))
                    .frame(width: 38, height: 34)
                    .background(RoundedRectangle(cornerRadius: 10).fill(.white.opacity(0.08)))
                    .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(.white.opacity(0.2)))
                    .foregroundStyle(.white)
            }
            .buttonStyle(.plain)
            .keyboardShortcut("r", modifiers: [])
            .accessibilityLabel("Again")
        }
        .padding(8)
        .glass(model.accent, radius: 18)
    }

    private func result(_ end: (cleared: Bool, x: Double, y: Double)) -> some View {
        HStack(spacing: 10) {
            Image(systemName: end.cleared ? "flag.checkered" : "xmark.octagon.fill")
                .font(.system(size: 26, weight: .bold))
                .foregroundStyle(end.cleared ? Neon.lime : Neon.red)
                .symbolEffect(.bounce, value: end.cleared)
            VStack(alignment: .leading) {
                Text(end.cleared ? "Cleared" : String(format: "Crashed at %.0f m", end.x)).font(.system(size: 17, weight: .heavy, design: .rounded)).foregroundStyle(.white)
                    .lineLimit(1).minimumScaleFactor(0.8)
                Text(String(format: "%.1f s", session.seconds)).font(.footnote).foregroundStyle(.white.opacity(0.7))
            }
            Spacer()
            Button { session.restart() } label: { Image(systemName: "arrow.counterclockwise") }.buttonStyle(.bordered).tint(.white).accessibilityLabel("Again")
            if !end.cleared {
                Button { close(); if let maze = model.maze { maze.focus(x: end.x, y: end.y) } else { model.canvas?.center(on: end.x) } } label: { Label("Fix it", systemImage: "scope").lineLimit(1) }
                    .buttonStyle(.borderedProminent).tint(Neon.red)
            }
        }
        .padding(14)
        .glass(end.cleared ? Neon.lime : Neon.red, radius: 18)
    }

    private func close() {
        model.playTrace = session.trace
        session.stop()
        onClose()
    }
}

/// an iPhone, drawn: its screen's rounded corners, a bezel lit at its edge, the Dynamic Island over the screen, its side
/// buttons, and the zone's light behind it
struct PhoneFrame<Content: View>: View {
    let accent: Color
    let bezel: CGFloat
    @ViewBuilder var content: Content

    var body: some View {
        let screenRadius: CGFloat = 55
        ZStack(alignment: .top) {
            content.clipShape(RoundedRectangle(cornerRadius: screenRadius, style: .continuous))
            Capsule().fill(.black).frame(width: 126, height: 37).padding(.top, 11) // (the Dynamic Island)
                .overlay(Circle().fill(Color(white: 0.12)).frame(width: 11, height: 11).offset(x: 40, y: 5.5))
        }
        .padding(bezel)
        .background(
            RoundedRectangle(cornerRadius: screenRadius + bezel, style: .continuous)
                .fill(LinearGradient(colors: [Color(white: 0.16), Color(white: 0.04)], startPoint: .top, endPoint: .bottom))
        )
        .overlay(
            RoundedRectangle(cornerRadius: screenRadius + bezel, style: .continuous)
                .strokeBorder(LinearGradient(colors: [Color(white: 0.75), Color(white: 0.3), Color(white: 0.6)], startPoint: .topLeading, endPoint: .bottomTrailing), lineWidth: 2.5)
        )
        .background(alignment: .leading) { // (its buttons: action, volume up and down)
            VStack(spacing: 22) {
                Capsule().frame(width: 4, height: 34)
                Capsule().frame(width: 4, height: 62)
                Capsule().frame(width: 4, height: 62)
            }
            .foregroundStyle(Color(white: 0.45))
            .offset(x: -3, y: -150)
        }
        .background(alignment: .trailing) {
            Capsule().fill(Color(white: 0.45)).frame(width: 4, height: 96).offset(x: 3, y: -110)
        }
        .shadow(color: accent.opacity(0.45), radius: 40)
        .shadow(color: .black.opacity(0.6), radius: 12, y: 8)
    }
}
