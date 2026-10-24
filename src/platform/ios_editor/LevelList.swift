// The levels on this device: each one's number, zone, description, whether it's changed since the last sync, and the
// bot's last verdict on it; a new one (the generator's course), a copy, one taken out. And the minimap: the whole
// course in a strip, the canvas's window on it, dragged to look along it.
import SwiftUI

struct LevelRow: View {
    let entry: LevelEntry
    let verdict: Verdict?
    let selected: Bool
    var body: some View {
        let look = ZoneLook.of(entry.zone)
        HStack(spacing: 12) {
            ZStack {
                RoundedRectangle(cornerRadius: 11, style: .continuous)
                    .fill(LinearGradient(colors: look.sky + [look.accent.opacity(0.35)], startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 11, style: .continuous).strokeBorder(look.accent.opacity(selected ? 1 : 0.5), lineWidth: selected ? 1.6 : 1))
                    .shadow(color: look.accent.opacity(selected ? 0.7 : 0.2), radius: selected ? 8 : 3)
                Text("\(entry.number)").font(.system(size: 17, weight: .heavy, design: .rounded)).foregroundStyle(.white)
            }
            .frame(width: 44, height: 44)
            VStack(alignment: .leading, spacing: 3) {
                Text(entry.title).font(.system(size: 15, weight: .semibold, design: .rounded)).foregroundStyle(.white).lineLimit(1)
                HStack(spacing: 6) {
                    Text(ZoneLook.zoneName(entry.zone)).foregroundStyle(look.accent)
                    if entry.dirty { Label("Not synced", systemImage: "icloud.and.arrow.up").labelStyle(.titleAndIcon).foregroundStyle(Neon.amber) }
                }
                .font(.system(size: 11, weight: .semibold))
            }
            Spacer(minLength: 0)
            if let v = verdict {
                Image(systemName: v.cleared ? "checkmark.seal.fill" : "xmark.octagon.fill")
                    .foregroundStyle(v.cleared ? Neon.lime : Neon.red)
                    .shadow(color: v.cleared ? Neon.lime : Neon.red, radius: 4)
                    .accessibilityLabel(v.cleared ? "The bot clears it" : "The bot crashes")
            }
        }
        .padding(.vertical, 4)
        .contentShape(Rectangle())
    }
}

struct LevelList: View {
    @ObservedObject var store: LevelStore
    @Binding var selection: Int?
    @ObservedObject var sync: SyncEngine
    @State private var confirmDelete: Int?
    @StateObject private var backup = BackupState()

    var body: some View {
        List(selection: $selection) {
            Section {
                SyncBanner(sync: sync, store: store)
            }
            .listRowBackground(Color.clear)
            Section("Levels") {
                ForEach(store.levels) { e in
                    LevelRow(entry: e, verdict: store.verdict(e.number), selected: selection == e.number)
                        .tag(e.number)
                        .accessibilityIdentifier("level.row.\(e.number)")
                        .listRowBackground(selection == e.number ? ZoneLook.of(e.zone).accent.opacity(0.14) : Color.white.opacity(0.03))
                        .contextMenu {
                            Button { if let n = store.duplicate(e.number) { selection = n } } label: { Label("Copy as a new level", systemImage: "plus.square.on.square") }
                            Button(role: .destructive) { confirmDelete = e.number } label: { Label("Take it out", systemImage: "trash") }
                        }
                        .swipeActions { Button(role: .destructive) { confirmDelete = e.number } label: { Label("Delete", systemImage: "trash") } }
                }
            }
        }
        .scrollContentBackground(.hidden)
        .background(NightBackdrop(zone: "city"))
        .accessibilityIdentifier("levels.list")
        .navigationTitle("Levels")
        .toolbar {
            ToolbarItem(placement: .topBarLeading) { HomeButton() }
            ToolbarItem(placement: .primaryAction) {
                Button { withAnimation { selection = store.newLevel() } } label: { Label("New level", systemImage: "plus") }
                    .keyboardShortcut("n", modifiers: .command)
            }
            ToolbarItem(placement: .primaryAction) { BackupMenu(store: store, state: backup) }
        }
        .modifier(BackupSheets(store: store, state: backup))
        .confirmationDialog("Take level \(confirmDelete ?? 0) out?", isPresented: Binding(get: { confirmDelete != nil }, set: { if !$0 { confirmDelete = nil } }), titleVisibility: .visible) {
            Button("Take it out", role: .destructive) {
                if let n = confirmDelete { if selection == n { selection = nil }; store.delete(n) }
                confirmDelete = nil
            }
        } message: { Text("On the Mac too, at the next sync.") }
    }
}

struct Minimap: View {
    @ObservedObject var model: EditorModel
    @ObservedObject var viewport: ViewportState
    var body: some View {
        GeometryReader { geo in
            let s = model.scene
            let len = max(1, s?.courseLength ?? 1)
            let k = Double(geo.size.width) / len                                   // (points a metre, along)
            let rows = Double(max(8, (s?.rows ?? 8) + 2))
            let hk = Double(geo.size.height) / (rows * (s?.cell ?? 0.6))         // (...and up)
            ZStack(alignment: .topLeading) {
                Canvas { ctx, size in
                    guard let s else { return }
                    let H = Double(size.height)
                    func box(_ x: Double, _ y: Double, _ w: Double, _ h: Double) -> Path { Path(CGRect(x: x, y: y, width: w, height: h)) }
                    for w in s.walls {
                        let x0 = Double(w.c0) * s.cell * k, x1 = Double(w.c1) * s.cell * k
                        if w.h > 0 {
                            let h = Double(w.h) * s.cell * hk
                            ctx.fill(box(x0, H - h, x1 - x0, h), with: .color(w.kind == "hay" ? Neon.straw.opacity(0.6) : model.accent.opacity(0.55)))
                        }
                        if w.ceil > 0 { ctx.fill(box(x0, H - Double(w.top > 0 ? w.top : w.ceil + 1) * s.cell * hk, x1 - x0, 2), with: .color(Neon.magenta)) }
                    }
                    for g in s.gauntlets { ctx.fill(box(Double(g.c0) * s.cell * k, 0, Double(g.c1 - g.c0) * s.cell * k, H), with: .color(Neon.straw.opacity(0.2))) }
                    for it in s.items {
                        ctx.fill(Path(ellipseIn: CGRect(x: it.x * k - 1.5, y: H - it.y * hk - 3, width: 3, height: 3)), with: .color(Color(hex: model.registry.hazard(it.type)?.color ?? "#ffffff")))
                    }
                    for n in s.notes { ctx.fill(box(n.x * k - 1, 0, 2, 5), with: .color(n.kind == "note" ? Neon.amber : Neon.red)) }
                    if let g = model.botReport?.game, model.showBot, g.trace.count > 1 {
                        var p = Path()
                        p.addLines(g.trace.map { CGPoint(x: $0[0] * k, y: H - ($0[1] + $0[2] * s.cell) * hk) })
                        ctx.stroke(p, with: .color(Neon.cyan.opacity(0.8)), lineWidth: 1)
                    }
                    ctx.fill(box(s.length * k - 1, 0, 2, H), with: .color(.white))
                }
                RoundedRectangle(cornerRadius: 4)
                    .strokeBorder(.white.opacity(0.9), lineWidth: 1.2)
                    .background(RoundedRectangle(cornerRadius: 4).fill(.white.opacity(0.08)))
                    .frame(width: max(8, (viewport.to - viewport.from) * k), height: geo.size.height)
                    .offset(x: max(0, viewport.from * k))
                    .shadow(color: model.accent, radius: 4)
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { v in model.canvas?.center(on: Double(v.location.x) / k, animated: false) })
        }
        .frame(height: 46)
        .padding(6)
        .glass(model.accent, radius: 12)
        .accessibilityLabel("The whole course")
        .accessibilityIdentifier("course.minimap")
        .accessibilityValue(String(format: "view %.1f to %.1f of %.1f m", viewport.from, viewport.to, model.scene?.courseLength ?? 0))
    }
}
