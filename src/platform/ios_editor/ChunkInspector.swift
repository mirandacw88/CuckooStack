// A chunk's settings in the inspector: its name, the levels it may appear in (any number of ranges, overlapping if you
// like), how much the generators want it (weight), the most a course holds (or no limit), whether they may flip it
// (Mirror: off, with the reason, when it holds a spring or a bounce box) and its description.
import SwiftUI

/// a number with a minus and a plus either side, and typing into it: for a range's ends
struct NumberStepper: View {
    let label: String
    var id = ""
    @Binding var value: Int
    var range: ClosedRange<Int> = 1...LevelRange.most
    var tint: Color = Neon.magenta
    @State private var draft = ""
    @FocusState private var typing: Bool

    var body: some View {
        HStack(spacing: 0) {
            stepButton("minus", -1)
            VStack(spacing: 0) {
                Text(label.uppercased()).font(.system(size: 8, weight: .heavy, design: .rounded)).tracking(0.8).foregroundStyle(tint.opacity(0.8))
                TextField("", text: $draft)
                    .keyboardType(.numberPad)
                    .multilineTextAlignment(.center)
                    .font(.system(size: 17, weight: .bold, design: .rounded)).monospacedDigit()
                    .foregroundStyle(.white)
                    .focused($typing)
                    .accessibilityIdentifier("\(id).field")
                    .frame(minWidth: 40)
                    .onChange(of: draft) { _, t in if typing, let n = Int(t), range.contains(n), n != value { value = n } }
                    .onChange(of: typing) { _, on in if !on { draft = String(value) } }
            }
            .frame(maxWidth: .infinity)
            stepButton("plus", 1)
        }
        .padding(.vertical, 3)
        .background(Capsule().fill(tint.opacity(0.1)))
        .overlay(Capsule().strokeBorder(tint.opacity(typing ? 0.95 : 0.4), lineWidth: typing ? 1.5 : 1))
        .shadow(color: tint.opacity(typing ? 0.5 : 0), radius: 6)
        .animation(.spring(response: 0.25), value: typing)
        .onAppear { draft = String(value) }
        .onChange(of: value) { _, v in if !typing { draft = String(v) } }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(label)
        .accessibilityValue("\(value)")
        .accessibilityIdentifier(id)
    }

    private func stepButton(_ symbol: String, _ d: Int) -> some View {
        Button {
            let next = min(max(value + d, range.lowerBound), range.upperBound)
            if next != value { value = next; Haptic.tap() }
        } label: {
            Image(systemName: symbol).font(.system(size: 12, weight: .heavy)).foregroundStyle(tint).frame(width: 30, height: 34).contentShape(Rectangle())
        }
        .buttonStyle(PressSpring())
        .accessibilityLabel(d < 0 ? "Less \(label)" : "More \(label)")
        .accessibilityIdentifier("\(id).\(d < 0 ? "less" : "more")")
    }
}

/// what the settings card needs of the editor it sits in: a course chunk's (EditorModel) or a maze chunk's (MazeChunkModel)
@MainActor
protocol ChunkSettingsHost: ObservableObject {
    var chunkCanMirror: Bool { get }
    var chunkWhyNot: String { get }
    var chunkIsMaze: Bool { get }
    /// a course chunk's reach and the length of a pad, metres (a maze chunk has none)
    var chunkReach: (reach: Double, pad: Double)? { get }
    func setHeader(_ change: (inout ChunkHeader) -> Void)
    func renameChunk(_ typed: String) -> Bool
}

extension EditorModel: ChunkSettingsHost {
    var chunkCanMirror: Bool { chunkInfo?.canMirror ?? true }
    var chunkWhyNot: String { chunkInfo?.why ?? "" }
    var chunkIsMaze: Bool { false }
    var chunkReach: (reach: Double, pad: Double)? { chunkInfo.map { ($0.reach, Double($0.runIn) * (scene?.cell ?? 0.6)) } }
}

struct ChunkSettingsCard<Host: ChunkSettingsHost>: View {
    @ObservedObject var model: Host
    @ObservedObject var session: ChunkSession
    @State private var name = ""
    @State private var notes = ""
    @FocusState private var naming: Bool

    var body: some View {
        let h = session.header
        let canMirror = model.chunkCanMirror
        let maze = model.chunkIsMaze
        Card(title: "Chunk settings", symbol: "puzzlepiece.extension.fill", accent: Neon.magenta) {
            // its name (its file's: lower case, '-' for a space)
            LabeledContent("Name") {
                TextField("hurdles", text: $name)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .multilineTextAlignment(.trailing)
                    .focused($naming)
                    .accessibilityIdentifier("chunk.name")
                    .onSubmit { commitName() }
                    .frame(maxWidth: 190)
            }
            LabeledContent("Kind") {
                Label(maze ? "Maze chunk" : "Course chunk", systemImage: maze ? "square.grid.3x3.fill" : "square.stack.3d.up.fill")
                    .font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(Neon.magenta)
            }
            if let reach = model.chunkReach {
                LabeledContent("Reach") {
                    Text(String(format: "%.1f m · pads %.1f m", reach.reach, reach.pad))
                        .font(.system(size: 13, weight: .semibold, design: .rounded)).monospacedDigit().foregroundStyle(.white.opacity(0.8))
                }
            }
            Text(maze ? "Build the block as it sits on a floor running right: its bottom row is the floor, the rows above are air she runs through. The maze generator stamps it into a stretch of a floor with the head room for it; on a floor that runs left it is drawn mirrored."
                      : "Set hazards between the hatched pads on the course: the generators cut the pads off when they place it.")
                .font(.caption).foregroundStyle(.white.opacity(0.55))

            divider
            ranges(h)
            divider
            // weight: how much the generators want it (a plain piece weighs 1)
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Label("Weight", systemImage: "scalemass.fill").font(.system(size: 13, weight: .semibold))
                    Spacer()
                    Text("\(h.weight) · \(ChunkWords.weight(h.weight))").font(.system(size: 13, weight: .bold, design: .rounded)).monospacedDigit()
                        .foregroundStyle(Neon.magenta).contentTransition(.numericText())
                        .animation(.spring(response: 0.3, dampingFraction: 0.8), value: h.weight)
                }
                Slider(value: Binding(get: { Double(h.weight) }, set: { v in model.setHeader { $0.weight = Int(v) } }), in: 1...10, step: 1)
                    .tint(Neon.magenta)
                    .onChange(of: h.weight) { _, _ in Haptic.tap() }
                    .accessibilityValue("\(h.weight)").accessibilityIdentifier("chunk.weight")
            }
            divider
            // uses: the most a course holds, or no limit
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Label(maze ? "Max uses a maze" : "Max uses a level", systemImage: "number.circle.fill").font(.system(size: 13, weight: .semibold))
                    Spacer()
                    Text(ChunkWords.uses(h.uses)).font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(Neon.magenta)
                        .contentTransition(.numericText()).animation(.spring(response: 0.3), value: h.uses)
                }
                HStack(spacing: 10) {
                    NumberStepper(label: "uses", id: "chunk.uses", value: Binding(get: { max(h.uses, 1) }, set: { v in model.setHeader { $0.uses = v } }), range: 1...99)
                        .opacity(h.uses == 0 ? 0.35 : 1).disabled(h.uses == 0)
                    Toggle("No limit", isOn: Binding(get: { h.uses == 0 }, set: { on in model.setHeader { $0.uses = on ? 0 : 2 } }))
                        .toggleStyle(.switch).tint(Neon.magenta).font(.system(size: 13, weight: .semibold)).fixedSize()
                        .accessibilityIdentifier("chunk.noLimit")
                }
            }
            divider
            // mirror: the generators may flip it left to right (off, and why, when it can't be)
            VStack(alignment: .leading, spacing: 4) {
                Toggle(isOn: Binding(get: { h.mirror && canMirror }, set: { on in model.setHeader { $0.mirror = on } })) {
                    Label {
                        Text("Mirror").font(.system(size: 13, weight: .semibold))
                    } icon: {
                        Image(systemName: "arrow.left.arrow.right")
                            .rotation3DEffect(.degrees(h.mirror && canMirror ? 180 : 0), axis: (x: 0, y: 1, z: 0))
                            .animation(.spring(response: 0.5, dampingFraction: 0.6), value: h.mirror)
                            .symbolEffect(.bounce, value: h.mirror)
                    }
                }
                .tint(Neon.magenta)
                .disabled(!canMirror)
                .accessibilityIdentifier("chunk.mirror")
                if !canMirror {
                    Label(!model.chunkWhyNot.isEmpty ? model.chunkWhyNot.prefix(1).uppercased() + model.chunkWhyNot.dropFirst() : "It can't be flipped", systemImage: "exclamationmark.triangle.fill")
                        .font(.caption).foregroundStyle(Neon.amber).transition(.opacity.combined(with: .move(edge: .top)))
                } else {
                    Text(h.mirror ? (maze ? "The generator may flip it, and puts it on floors running either way. The checker judges the flipped chunk too: both ways must be fair."
                                          : "The generators may flip it. The checker and the bot judge the flipped chunk too: both ways must be fair.")
                                  : (maze ? "Off: it only goes on a floor that runs right, as you built it." : "Off: it is always placed as you built it."))
                        .font(.caption).foregroundStyle(.white.opacity(0.55))
                }
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.85), value: canMirror)
            divider
            VStack(alignment: .leading) {
                Text("Its description").font(.footnote).foregroundStyle(.white.opacity(0.6))
                TextEditor(text: $notes).frame(minHeight: 56).scrollContentBackground(.hidden)
                    .background(RoundedRectangle(cornerRadius: 8).fill(.white.opacity(0.05)))
                Button("Save the description") { model.setHeader { $0.notes = notes.split(separator: "\n", omittingEmptySubsequences: false).map(String.init) } }
                    .buttonStyle(.bordered).controlSize(.small)
                    .disabled(notes.split(separator: "\n", omittingEmptySubsequences: false).map(String.init) == h.notes)
            }
        }
        .font(.system(size: 14))
        .onAppear { name = session.id; notes = h.notes.joined(separator: "\n") }
        .onChange(of: session.id) { _, id in if !naming { name = id } }
        .onChange(of: h.notes) { _, n in notes = n.joined(separator: "\n") }
        .onChange(of: naming) { _, on in if !on { commitName() } }
        .animation(.spring(response: 0.4, dampingFraction: 0.82), value: h.ranges)
    }

    private var divider: some View { Divider().overlay(.white.opacity(0.1)) }

    private func commitName() {
        guard name != session.id else { return }
        if !model.renameChunk(name) { name = session.id } else { name = session.id }
    }

    /// the ranges: a from and a to each, any number, overlapping allowed
    private func ranges(_ h: ChunkHeader) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Label("Levels it may appear in", systemImage: "flag.2.crossed.fill").font(.system(size: 13, weight: .semibold))
                Spacer()
                Button {
                    withAnimation(.spring(response: 0.4, dampingFraction: 0.8)) { model.setHeader { $0.ranges = ChunkRanges.added(to: $0.ranges) } }
                    Haptic.tap()
                } label: { Image(systemName: "plus.circle.fill").font(.system(size: 20)).foregroundStyle(Neon.magenta).symbolEffect(.bounce, value: h.ranges.count) }
                    .buttonStyle(PressSpring())
                    .accessibilityLabel("Add a range").accessibilityIdentifier("chunk.range.add")
            }
            ForEach(Array(h.ranges.enumerated()), id: \.offset) { k, r in
                HStack(spacing: 6) {
                    NumberStepper(label: "from", id: "chunk.range.\(k).from", value: Binding(get: { r.from }, set: { v in model.setHeader { $0.ranges = ChunkRanges.set(k, from: v, in: $0.ranges) } }))
                    NumberStepper(label: "to", id: "chunk.range.\(k).to", value: Binding(get: { r.to }, set: { v in model.setHeader { $0.ranges = ChunkRanges.set(k, to: v, in: $0.ranges) } }))
                    Button {
                        withAnimation(.spring(response: 0.4, dampingFraction: 0.8)) { model.setHeader { $0.ranges = ChunkRanges.removed(k, from: $0.ranges) } }
                        Haptic.tap()
                    } label: { Image(systemName: "minus.circle.fill").font(.system(size: 20)).foregroundStyle(h.ranges.count > 1 ? Neon.red.opacity(0.9) : .white.opacity(0.2)) }
                        .buttonStyle(PressSpring()).disabled(h.ranges.count <= 1)
                        .accessibilityLabel("Take out this range").accessibilityIdentifier("chunk.range.\(k).remove")
                }
                .transition(.asymmetric(insertion: .scale(scale: 0.9, anchor: .top).combined(with: .opacity), removal: .opacity.combined(with: .scale(scale: 0.9))))
            }
            Text(model.chunkIsMaze ? "Roll a new maze on a level in a range and the generator may place it. Ranges may overlap. Levels 1–30 never change unless you roll them."
                                   : "Roll a new course on a level in a range and the generators may place it. Ranges may overlap. Levels 1–30 never change unless you roll them.")
                .font(.caption).foregroundStyle(.white.opacity(0.55))
        }
    }
}
