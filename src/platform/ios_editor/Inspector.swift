// The inspector: the bot's verdict, what's picked (a hazard's numbers as sliders, their ranges the registry's), the
// gauntlet, the checker's notes (tap one: the canvas goes to it), the level's settings, and its history in words. A
// column beside the canvas on the iPad, a sheet from the bottom on the iPhone.
import SwiftUI

struct Inspector: View {
    @ObservedObject var model: EditorModel
    var onPlay: (Bool) -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                if let session = model.chunk {
                    // a chunk: its settings, what's picked, the checker (flipped too), the bot (alone, flipped too), the history
                    ChunkSettingsCard(model: model, session: session)
                    picked
                    NotesCard(model: model)
                    BotCard(model: model, onPlay: onPlay)
                    HistoryCard(model: model)
                } else {
                    BotCard(model: model, onPlay: onPlay)
                    GeneratorCard(model: model)
                    picked
                    if model.tool == "gauntlet" || model.meta?.gauntlet != nil { GauntletCard(model: model) }
                    NotesCard(model: model)
                    SettingsCard(model: model)
                    HistoryCard(model: model)
                }
            }
            .padding(14)
            .animation(.spring(response: 0.4, dampingFraction: 0.8), value: model.picked?.i)
        }
        .scrollContentBackground(.hidden)
        .background(NightBackdrop(zone: model.zone).opacity(0.9))
    }
}

extension Inspector {
    @ViewBuilder fileprivate var picked: some View {
        if let hit = model.picked, let i = hit.i, let type = model.registry.hazard(hit.type ?? "") {
            PickedCard(model: model, index: i, type: type, values: hit.v ?? [])
                .transition(.asymmetric(insertion: .scale(scale: 0.9).combined(with: .opacity), removal: .opacity))
        }
    }
}

struct Card<Content: View>: View {
    let title: String
    let symbol: String
    let accent: Color
    @ViewBuilder var content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title.uppercased(), systemImage: symbol)
                .font(.system(size: 11, weight: .heavy, design: .rounded))
                .tracking(1.2)
                .foregroundStyle(accent)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .glass(accent, radius: 16)
    }
}

struct BotCard: View {
    @ObservedObject var model: EditorModel
    var onPlay: (Bool) -> Void
    var body: some View {
        let v = model.botVerdict
        Card(title: model.chunk != nil ? "The bot, on the chunk alone" : "The bot", symbol: "cpu", accent: v.tint) {
            HStack {
                StatusPill(text: v.text, symbol: v.symbol, tint: v.tint, busy: model.botBusy || model.botStale)
                Spacer()
                if let g = model.botReport?.game, !(model.botBusy || model.botStale) {
                    VStack(alignment: .trailing, spacing: 2) {
                        Text("\(g.perfects) PERFECT").foregroundStyle(Neon.magenta)
                        Text("\(g.closeCalls) close calls").foregroundStyle(Neon.cyan)
                    }
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .transition(.opacity)
                }
            }
            if let g = model.botReport?.game, !g.cleared, !(model.botBusy || model.botStale) {
                Button { model.jumpToCrash() } label: { Label("Show where it crashed", systemImage: "scope") }
                    .buttonStyle(.bordered).tint(Neon.red)
            }
            if let f = model.botFlipVerdict { // (a chunk with Mirror on: the flipped chunk played too)
                StatusPill(text: f.text, symbol: f.symbol, tint: f.tint).transition(.scale.combined(with: .opacity))
            }
            if let e = model.botReport?.error, !e.isEmpty { Text(e).font(.footnote).foregroundStyle(Neon.red) }
            HStack {
                Button { onPlay(false) } label: { Label("Play", systemImage: "play.fill").frame(maxWidth: .infinity) }
                    .buttonStyle(.borderedProminent).tint(model.accent)
                Button { onPlay(true) } label: { Label("Watch", systemImage: "eye.fill").frame(maxWidth: .infinity) }
                    .buttonStyle(.bordered).tint(Neon.cyan)
            }
            .font(.system(size: 14, weight: .bold, design: .rounded))
            Toggle(isOn: $model.showBot) { Text("The bot's way on the course").font(.footnote) }.tint(Neon.cyan)
            Toggle(isOn: $model.showWalk) { Text("The egg meter").font(.footnote) }.tint(Neon.egg)
        }
    }
}

struct PickedCard: View {
    @ObservedObject var model: EditorModel
    let index: Int
    let type: HazardType
    let values: [Double]
    @State private var draft: [Double] = []

    var body: some View {
        Card(title: type.label, symbol: "slider.horizontal.3", accent: Color(hex: type.color)) {
            Text(type.tip).font(.footnote).foregroundStyle(.white.opacity(0.75)).fixedSize(horizontal: false, vertical: true)
            ForEach(Array(type.params.enumerated()), id: \.offset) { k, p in
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text(p.label).font(.system(size: 13, weight: .semibold))
                        Spacer()
                        Text(String(format: p.step < 0.1 ? "%.2f %@" : "%.1f %@", value(k), p.unit)).monospacedDigit()
                            .font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(Color(hex: type.color))
                    }
                    Slider(value: Binding(get: { value(k) }, set: { v in set(k, v) }),
                           in: p.min...max(p.min + p.step, k == 0 ? min(p.max, model.scene?.courseLength ?? p.max) : p.max), step: p.step,
                           onEditingChanged: { editing in if !editing { commit(k) } })
                        .tint(Color(hex: type.color))
                    HStack {
                        Button { nudge(k, -p.step) } label: { Image(systemName: "minus") }
                        Button { nudge(k, p.step) } label: { Image(systemName: "plus") }
                    }
                    .buttonStyle(.bordered).controlSize(.mini)
                }
            }
            Button(role: .destructive) { model.removePicked() } label: { Label("Take it out", systemImage: "trash") }
                .buttonStyle(.bordered)
        }
        .onAppear { draft = values }
        .onChange(of: values) { _, v in draft = v }
    }

    private func value(_ k: Int) -> Double { k < draft.count ? draft[k] : (k < values.count ? values[k] : 0) }
    private func set(_ k: Int, _ v: Double) { if draft.count != values.count { draft = values }; if k < draft.count { draft[k] = v } }
    private func commit(_ k: Int) { model.setPickedParam(k, value(k)) }
    private func nudge(_ k: Int, _ d: Double) { set(k, value(k) + d); commit(k) }
}

struct GauntletCard: View {
    @ObservedObject var model: EditorModel
    var body: some View {
        Card(title: "Gauntlet", symbol: "square.grid.3x3.fill", accent: Neon.straw) {
            if let g = model.meta?.gauntlet {
                HStack {
                    Text("The \(Builtin.gauntletShown(g))").font(.system(size: 15, weight: .bold, design: .rounded))
                    Spacer()
                    Button(role: .destructive) { model.removeGauntlet(g) } label: { Image(systemName: "trash") }.buttonStyle(.bordered)
                }
                Text("Drag it along with the Gauntlet tool: its forecourt, lintel and porch come with it.").font(.footnote).foregroundStyle(.white.opacity(0.7))
            } else {
                Text("Pick one, then tap the course where its door goes.").font(.footnote).foregroundStyle(.white.opacity(0.7))
            }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    ForEach(model.meta?.gauntlets ?? [], id: \.self) { name in
                        Button {
                            model.gauntletPick = name
                            model.tool = "gauntlet"
                            Haptic.tap()
                        } label: {
                            Text(Builtin.gauntletShown(name).capitalized).font(.system(size: 13, weight: .semibold, design: .rounded))
                                .padding(.horizontal, 10).padding(.vertical, 6)
                                .background(Capsule().fill(model.gauntletPick == name ? Neon.straw.opacity(0.3) : .white.opacity(0.06)))
                                .overlay(Capsule().strokeBorder(model.gauntletPick == name ? Neon.straw : .white.opacity(0.15)))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
    }
}

struct NotesCard: View {
    @ObservedObject var model: EditorModel
    var body: some View {
        let notes = model.notes
        Card(title: notes.isEmpty ? "The checker: all fair" : "The checker: \(notes.count) to look at", symbol: notes.isEmpty ? "checkmark.shield.fill" : "exclamationmark.triangle.fill",
             accent: notes.isEmpty ? Neon.lime : Neon.amber) {
            if notes.isEmpty {
                Text("Every climb and blade affordable, every landing long enough.").font(.footnote).foregroundStyle(.white.opacity(0.7))
            }
            ForEach(notes) { n in
                Button { model.focusedNote = n; Haptic.tap() } label: {
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: n.kind == "note" ? "exclamationmark.triangle.fill" : "xmark.octagon.fill")
                            .foregroundStyle(n.kind == "note" ? Neon.amber : Neon.red)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(String(format: "%.1f m", n.x)).font(.system(size: 11, weight: .heavy, design: .rounded)).foregroundStyle(.white.opacity(0.6))
                            Text(n.text).font(.footnote).foregroundStyle(.white).multilineTextAlignment(.leading)
                        }
                        Spacer(minLength: 0)
                    }
                    .padding(8)
                    .background(RoundedRectangle(cornerRadius: 10).fill(model.focusedNote == n ? Neon.amber.opacity(0.18) : .white.opacity(0.04)))
                }
                .buttonStyle(.plain)
            }
        }
    }
}

struct SettingsCard: View {
    @ObservedObject var model: EditorModel
    @State private var length = ""
    @State private var offset = ""
    @State private var scale = ""
    @State private var notes = ""
    var body: some View {
        Card(title: "Level \(model.number)", symbol: "slider.vertical.3", accent: model.accent) {
            if let m = model.meta {
                LabeledContent("Zone") { Text(ZoneLook.zoneName(model.zone)).foregroundStyle(model.accent) }
                LabeledContent("Finish line") {
                    TextField("m", text: $length).keyboardType(.decimalPad).multilineTextAlignment(.trailing).frame(width: 80)
                        .onSubmit { model.setMeta("length", length) }
                    Text("m")
                }
                LabeledContent("Difficulty") {
                    TextField("start", text: $offset).keyboardType(.decimalPad).multilineTextAlignment(.trailing).frame(width: 64)
                    TextField("rate", text: $scale).keyboardType(.decimalPad).multilineTextAlignment(.trailing).frame(width: 52)
                }
                .onSubmit { model.setMeta("difficulty", "\(offset) \(scale)") }
                LabeledContent("Droid raids") {
                    Picker("Droid raids", selection: Binding(get: { m.raids }, set: { model.setMeta("raids", $0) })) {
                        Text("Auto").tag("auto"); Text("None").tag("none")
                        ForEach(1...6, id: \.self) { Text("\($0)").tag(String($0)) }
                    }
                    .labelsHidden()
                }
                VStack(alignment: .leading) {
                    Text("Its description").font(.footnote).foregroundStyle(.white.opacity(0.6))
                    TextEditor(text: $notes).frame(minHeight: 60).scrollContentBackground(.hidden)
                        .background(RoundedRectangle(cornerRadius: 8).fill(.white.opacity(0.05)))
                    Button("Save the description") { model.setMeta("notes", notes) }.buttonStyle(.bordered).controlSize(.small)
                }
                .onAppear { load(m) }
            }
        }
        .font(.system(size: 14))
        .onChange(of: model.meta?.length) { _, _ in if let m = model.meta { load(m) } }
    }
    private func load(_ m: LevelMeta) {
        length = String(format: "%g", m.length); offset = String(format: "%g", m.diffOffset); scale = String(format: "%g", m.diffScale)
        notes = m.notes.joined(separator: "\n")
    }
}

struct HistoryCard: View {
    @ObservedObject var model: EditorModel
    var body: some View {
        Card(title: "History", symbol: "clock.arrow.circlepath", accent: .white) {
            HStack {
                Button { model.undo() } label: { Label("Undo", systemImage: "arrow.uturn.backward") }.disabled(!(model.history?.canUndo ?? false))
                Button { model.redo() } label: { Label("Redo", systemImage: "arrow.uturn.forward") }.disabled(!(model.history?.canRedo ?? false))
            }
            .buttonStyle(.bordered).controlSize(.small)
            let all = model.history?.undo ?? []
            let steps = Array(all.reversed().prefix(8))
            if steps.isEmpty { Text("No edits yet").font(.footnote).foregroundStyle(.white.opacity(0.5)) }
            ForEach(Array(steps.enumerated()), id: \.offset) { k, s in
                Button {
                    model.goTo(all.count - k - 1) // (back to before it)
                } label: {
                    HStack {
                        Circle().fill(k == 0 ? model.accent : .white.opacity(0.3)).frame(width: 6, height: 6)
                        Text(s.label).font(.footnote).foregroundStyle(.white.opacity(k == 0 ? 1 : 0.7))
                        Spacer()
                        Text(s.date, format: .relative(presentation: .numeric)).font(.system(size: 10)).foregroundStyle(.white.opacity(0.4))
                    }
                }
                .buttonStyle(.plain)
            }
            Button { model.historyOpen = true } label: { Label("The whole history, and revert", systemImage: "clock.arrow.circlepath") }
                .font(.footnote.weight(.semibold)).tint(model.accent)
        }
    }
}

/// the generators: the course rolled anew, and a maze made for it (each press another; undo goes back a roll)
struct GeneratorCard: View {
    @ObservedObject var model: EditorModel
    var body: some View {
        Card(title: "Generators", symbol: "dice.fill", accent: Neon.magenta) {
            Button { withAnimation(.spring(response: 0.4, dampingFraction: 0.8)) { model.rollLevel() } } label: {
                Label("Roll a new course", systemImage: "dice.fill").frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent).tint(Neon.magenta)
            .keyboardShortcut("l", modifiers: [.command, .shift])
            .accessibilityIdentifier("inspector.rollCourse")
            HazardChips(model: model, hazards: model.generatedHazards, off: $model.genOff)
            DensitySlider(value: $model.genDensity)
            Text(model.meta?.gauntlet != nil ? "The rhythm-beat generator, a new seed each press, with what's switched on, as dense as the slider. Its maze comes out: the maze generator puts a new one in."
                                               : "The rhythm-beat generator, a new seed each press, with what's switched on, as dense as the slider; undo goes back a roll.")
                .font(.caption).foregroundStyle(.white.opacity(0.6))
            Divider().overlay(.white.opacity(0.1))
            MazeGenButton(model: model, title: model.meta?.gauntlet == nil ? "Add a generated maze" : "Generate a new maze")
            HazardChips(model: model, hazards: model.registry.hazards.filter { model.mazeHazardIDs.contains($0.id) }, off: $model.mazeGenOff)
            MazeSizeControls(model: model)
        }
    }
}

/// a generator's switches: a chip a hazard (its icon and colour, from the registry), lit when the roll may use it
struct HazardChips: View {
    @ObservedObject var model: EditorModel
    let hazards: [HazardType]
    @Binding var off: Set<String>
    var body: some View {
        FlowRow(spacing: 6) {
            ForEach(hazards) { h in
                let on = !off.contains(h.id)
                Button {
                    withAnimation(.spring(response: 0.3)) { if on { off.insert(h.id) } else { off.remove(h.id) } }
                    Haptic.tap()
                } label: {
                    Label(h.plural.capitalized, systemImage: UIImage(systemName: h.icon) != nil ? h.icon : "diamond.fill")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(on ? Color(hex: h.color) : .white.opacity(0.4))
                        .padding(.horizontal, 9).padding(.vertical, 6)
                        .background(Capsule().fill(on ? Color(hex: h.color).opacity(0.16) : .white.opacity(0.04)))
                        .overlay(Capsule().strokeBorder(on ? Color(hex: h.color).opacity(0.75) : .white.opacity(0.15), style: StrokeStyle(lineWidth: 1, dash: on ? [] : [3, 2])))
                        .symbolEffect(.bounce, value: on)
                }
                .buttonStyle(.plain)
                .accessibilityLabel("\(h.plural.capitalized): \(on ? "on" : "off")")
            }
        }
    }
}

/// chips wrapping onto as many lines as they need
struct FlowRow: Layout {
    var spacing: CGFloat = 6
    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? 300
        var x: CGFloat = 0, y: CGFloat = 0, line: CGFloat = 0
        for v in subviews {
            let s = v.sizeThatFits(.unspecified)
            if x > 0 && x + s.width > width { x = 0; y += line + spacing; line = 0 }
            x += s.width + spacing
            line = max(line, s.height)
        }
        return CGSize(width: width, height: y + line)
    }
    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX, y = bounds.minY, line: CGFloat = 0
        for v in subviews {
            let s = v.sizeThatFits(.unspecified)
            if x > bounds.minX && x + s.width > bounds.maxX { x = bounds.minX; y += line + spacing; line = 0 }
            v.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(s))
            x += s.width + spacing
            line = max(line, s.height)
        }
    }
}

/// the Gauntlet generator's button: busy while it makes one
struct MazeGenButton: View {
    @ObservedObject var model: EditorModel
    let title: String
    var body: some View {
        Button { model.generateMaze() } label: {
            HStack(spacing: 8) {
                if model.generating { ProgressView().tint(.black) } else { Image(systemName: "square.grid.3x3.fill") }
                Text(model.generating ? "Making a maze…" : title)
            }
            .frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent).tint(Neon.straw)
        .disabled(model.generating)
        .keyboardShortcut("g", modifiers: [.command, .shift])
        .accessibilityIdentifier("inspector.generateMaze")
    }
}

/// the Gauntlet generator's size: how long, how many floors
/// the Level generator's density: 0 the rhythm-beat generator as designed (its rests, its spacing), 10 the densest it
/// lays (beats close, pieces short, few rests: super hard); the course it rolls stays fair (the checker and the bot pass it)
struct DensitySlider: View {
    @Binding var value: Double
    private var words: String {
        switch Int(value) {
        case 0: return "as designed"
        case 1...3: return "busier"
        case 4...5: return "dense"
        default: return "super hard"
        }
    }
    private var tint: Color { Color(hue: 0.83 - 0.83 * value / 10 * 0.95, saturation: 0.85, brightness: 1) } // (magenta to red)
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Image(systemName: value >= 10 ? "flame.fill" : "metronome.fill")
                    .foregroundStyle(tint)
                    .symbolEffect(.bounce, value: Int(value))
                    .contentTransition(.symbolEffect(.replace))
                Text("Density").font(.system(size: 13, design: .rounded))
                Spacer()
                Text("\(Int(value)) · \(words)").font(.system(size: 13, weight: .semibold, design: .rounded)).monospacedDigit()
                    .foregroundStyle(tint)
                    .contentTransition(.numericText())
                    .animation(.spring(response: 0.3, dampingFraction: 0.8), value: Int(value))
            }
            Slider(value: $value, in: 0...10, step: 1) {
                Text("Density")
            } minimumValueLabel: {
                Text("0").font(.caption2).foregroundStyle(.white.opacity(0.5))
            } maximumValueLabel: {
                Text("10").font(.caption2).foregroundStyle(.white.opacity(0.5))
            }
            .tint(tint)
            .onChange(of: Int(value)) { _, _ in Haptic.tap() }
            .accessibilityValue("\(Int(value)), \(words)")
            .accessibilityIdentifier("inspector.density")
        }
        .padding(.vertical, 2)
    }
}

struct MazeSizeControls: View {
    @ObservedObject var model: EditorModel
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Length").font(.system(size: 13, design: .rounded))
                Spacer()
                Text("\(Int(model.genWidth)) cells").font(.system(size: 13, weight: .semibold, design: .rounded)).monospacedDigit()
            }
            Slider(value: $model.genWidth, in: 80...260, step: 10).tint(Neon.straw)
            DensitySlider(value: $model.mazeDensity) // (the maze's own: its gaps shorter, its hazards back to back; "hard" from 6)
            Picker("Floors", selection: $model.genFloors) {
                Text("3 floors").tag(3)
                Text("5 floors").tag(5)
                Text("7 floors").tag(7)
            }
            .pickerStyle(.segmented)
        }
    }
}
