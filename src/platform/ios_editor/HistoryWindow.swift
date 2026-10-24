// A level's history, its own window: every edit in words, newest first, when and where; what's been undone dimmed
// above "Now" (tap one: back to just after it, and the canvas goes there); SYNCED where it last matched the Mac's;
// and the reverts, each a step of its own: back to the synced version, or to the game's original. Kept on the device
// between launches (LevelStore), so a level's history is there whenever it's opened.
import SwiftUI

struct HistoryWindow: View {
    @ObservedObject var model: EditorModel
    @Environment(\.dismiss) private var dismiss
    @State private var confirm: Revert?
    @State private var hashes: (start: String, undo: [String], redo: [String]) = ("", [], [])

    enum Revert: Identifiable { case synced, original; var id: Int { self == .synced ? 0 : 1 } }

    var body: some View {
        NavigationStack {
            List {
                if model.chunk == nil { Section { // (a chunk has no game original, and no synced version yet)
                    revertButton("Back to the synced version", "arrow.uturn.backward.circle.fill", "What this device and the Mac last agreed on",
                                 enabled: model.canRevertToSynced, tint: Neon.cyan) { confirm = .synced }
                    revertButton("Back to the game's original", "gamecontroller.fill", "Level \(model.number) as the game ships it",
                                 enabled: model.canRevertToOriginal, tint: Neon.amber) { confirm = .original }
                } footer: { Text("A revert is a step of its own: undo takes it back.") } }

                Section(header: Text("\(undo.count) \(undo.count == 1 ? "edit" : "edits")")) {
                    ForEach(Array(redo.enumerated().reversed()), id: \.offset) { j, s in // (undone: the farthest first)
                        row(s, after: undo.count + j + 1, undone: true, synced: hashes.redo.indices.contains(j) && hashes.redo[j] == model.syncedHash)
                    }
                    HStack(spacing: 10) {
                        Image(systemName: "location.fill").foregroundStyle(model.accent)
                        Text("Now").font(.system(size: 14, weight: .heavy, design: .rounded)).foregroundStyle(model.accent)
                        Rectangle().fill(model.accent.opacity(0.5)).frame(height: 1)
                    }
                    .listRowBackground(model.accent.opacity(0.08))
                    ForEach(Array(undo.enumerated().reversed()), id: \.offset) { i, s in // (newest first)
                        row(s, after: i + 1, undone: false, synced: hashes.undo.indices.contains(i) && hashes.undo[i] == model.syncedHash)
                    }
                    Button { model.goTo(0) } label: {
                        HStack {
                            Label(undo.isEmpty ? "As it was opened (no edits yet)" : "Before all of these", systemImage: "circle.dashed")
                                .font(.footnote).foregroundStyle(.white.opacity(0.6))
                            Spacer()
                            if hashes.start == model.syncedHash { syncedBadge }
                        }
                    }
                    .disabled(undo.isEmpty)
                }
            }
            .scrollContentBackground(.hidden)
            .background(NightBackdrop(zone: model.zone))
            .navigationTitle(model.chunk != nil ? "History · \(model.title)" : "History · Level \(model.number)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() }.keyboardShortcut(.escape, modifiers: []) }
                ToolbarItemGroup(placement: .bottomBar) {
                    Button { model.undo() } label: { Label("Undo", systemImage: "arrow.uturn.backward") }.disabled(!(model.history?.canUndo ?? false))
                    Spacer()
                    Button { model.redo() } label: { Label("Redo", systemImage: "arrow.uturn.forward") }.disabled(!(model.history?.canRedo ?? false))
                }
            }
            .confirmationDialog(confirm == .synced ? "Back to the synced version?" : "Back to the game's original?", isPresented: Binding(get: { confirm != nil }, set: { if !$0 { confirm = nil } }), titleVisibility: .visible) {
                Button(confirm == .synced ? "Revert to the synced version" : "Revert to the original", role: .destructive) {
                    withAnimation(.spring(response: 0.4)) { confirm == .synced ? model.revertToSynced() : model.revertToOriginal() }
                    confirm = nil
                }
            } message: { Text("Your edits stay in the history: undo brings them back.") }
            .onAppear { hashes = model.stepHashes() }
            .onChange(of: model.history?.undo.count) { _, _ in hashes = model.stepHashes() }
            .onChange(of: model.history?.redo.count) { _, _ in hashes = model.stepHashes() }
            .animation(.spring(response: 0.35, dampingFraction: 0.85), value: model.history?.undo.count)
        }
    }

    private var undo: [HistoryStep] { model.history?.undo ?? [] }
    private var redo: [HistoryStep] { model.history?.redo ?? [] }

    private func row(_ s: HistoryStep, after k: Int, undone: Bool, synced: Bool) -> some View {
        Button {
            model.goTo(k)
            if s.at >= 0 { model.canvas?.center(on: s.at) }
        } label: {
            HStack(alignment: .top, spacing: 10) {
                Image(systemName: symbol(s.label))
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(undone ? .white.opacity(0.35) : model.accent)
                    .frame(width: 24)
                VStack(alignment: .leading, spacing: 3) {
                    Text(s.label).font(.system(size: 15, weight: .semibold)).foregroundStyle(.white.opacity(undone ? 0.45 : 1))
                        .strikethrough(undone, color: .white.opacity(0.3))
                    HStack(spacing: 8) {
                        Text(s.date, format: .relative(presentation: .named))
                        if s.at >= 0 { Label(String(format: "%.0f m", s.at), systemImage: "scope") }
                    }
                    .font(.system(size: 12)).foregroundStyle(.white.opacity(0.5))
                }
                Spacer(minLength: 0)
                if synced { syncedBadge }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityHint(undone ? "Redo up to here" : "Back to just after this")
    }

    private var syncedBadge: some View {
        Text("SYNCED").font(.system(size: 10, weight: .heavy, design: .rounded)).foregroundStyle(.black)
            .padding(.horizontal, 6).padding(.vertical, 3).background(Capsule().fill(Neon.cyan))
            .accessibilityLabel("As the Mac has it")
    }

    private func revertButton(_ title: String, _ symbol: String, _ detail: String, enabled: Bool, tint: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 12) {
                Image(systemName: symbol).font(.system(size: 22)).foregroundStyle(enabled ? tint : .gray)
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.system(size: 15, weight: .semibold)).foregroundStyle(enabled ? .white : .gray)
                    Text(enabled ? detail : "It's that already").font(.system(size: 12)).foregroundStyle(.white.opacity(0.5))
                }
            }
        }
        .disabled(!enabled)
    }

    /// the step's kind, from its words (EditorTools' labels)
    private func symbol(_ label: String) -> String {
        let l = label.lowercased()
        if l.hasPrefix("back to") || l.hasPrefix("updated") { return "arrow.uturn.backward.circle" }
        if l.contains("saw") { return "gearshape.fill" }
        if l.contains("disco") { return "circle.hexagongrid.fill" }
        if l.contains("barrier") { return "rectangle.topthird.inset.filled" }
        if l.contains("block") { return "cube.fill" }
        if l.contains("column") || l.contains("pasted") { return "rectangle.split.3x1.fill" }
        if l.contains("gauntlet") || l.contains("the ") && (l.hasPrefix("placed") || l.hasPrefix("moved the") || l.hasPrefix("took out the")) { return "square.grid.3x3.fill" }
        if l.contains("finish") || l.contains("difficulty") || l.contains("raids") || l.contains("notes") { return "slider.horizontal.3" }
        if l.contains("street") || l.contains("walls") || l.contains("crates") || l.contains("hay") { return "square.stack.3d.up.fill" }
        return "pencil"
    }
}
