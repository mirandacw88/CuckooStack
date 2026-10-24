// The tools: the course's own (walls, kinds, barriers, blocks, columns, select, the gauntlet, the start) and one for
// each hazard in the game's registry (Hazards.h), so a new hazard brings its tool. A strip along the bottom on the
// iPhone, a rail beside the canvas on the iPad; the wall kinds and the eraser under them; each its keyboard key.
// Related tools share a button (Build, Hazards, Edit; the registry's pickups when there's more than one): it wears
// the one last used, and its flyout holds the rest.
import SwiftUI
import UIKit

struct ToolDef: Identifiable, Hashable {
    let id: String
    let label: String
    let symbol: String
    let key: String
    let tint: Color
    let usesKind: Bool
    var tip = ""
}

/// tools sharing a button in the rail
struct ToolGroup: Identifiable, Hashable {
    let id: String
    let label: String
    let members: [ToolDef]
}

/// what the rail holds: a tool, or a group of them
enum RailItem: Identifiable {
    case tool(ToolDef)
    case group(ToolGroup)
    var id: String { switch self { case .tool(let t): return t.id; case .group(let g): return "group." + g.id } }
}

enum Tools {
    static func all(_ r: Registry) -> [ToolDef] {
        var t: [ToolDef] = [
            ToolDef(id: "pan", label: "Pan", symbol: "hand.raised.fill", key: "h", tint: .white, usesKind: false),
            ToolDef(id: "wall", label: "Walls", symbol: "square.stack.3d.up.fill", key: "w", tint: Neon.cyan, usesKind: true,
                    tip: "Drag up to raise walls, down to lower them, in the kind picked under the rail"),
            ToolDef(id: "kind", label: "Kind", symbol: "paintbrush.pointed.fill", key: "k", tint: Neon.straw, usesKind: true,
                    tip: "Paint walls another kind (crates, hay, mixed) without changing their height"),
            ToolDef(id: "barrier", label: "Barrier", symbol: "rectangle.topthird.inset.filled", key: "b", tint: Neon.magenta, usesKind: false,
                    tip: "A ceiling over the walls: drag its underside to set how many eggs fit under it"),
            ToolDef(id: "block", label: "Block", symbol: "cube.fill", key: "o", tint: Neon.magenta, usesKind: true,
                    tip: "Drop a block of walls in one stroke, the kind picked under the rail"),
        ]
        for h in r.hazards {
            let symbol = UIImage(systemName: h.icon) != nil ? h.icon : "diamond.fill" // (a symbol this iOS lacks: a diamond)
            t.append(ToolDef(id: h.id, label: h.label, symbol: symbol, key: h.key, tint: Color(hex: h.color), usesKind: false, tip: h.tip))
        }
        for k in r.bouncerKinds { // bumper and gate walls: drag as high as the wall should be
            let symbol = k.id == "bumper" ? "arrow.uturn.backward.circle.fill" : k.id == "gate>" ? "arrow.right.to.line" : "arrow.left.to.line"
            t.append(ToolDef(id: k.id, label: k.label, symbol: symbol, key: k.key, tint: Color(hex: k.color), usesKind: false, tip: k.tip))
        }
        t += [
            ToolDef(id: "cols", label: "Columns", symbol: "rectangle.split.3x1.fill", key: "c", tint: Neon.lime, usesKind: false,
                    tip: "Drag across columns to copy them in; erasing, to take them out"),
            ToolDef(id: "select", label: "Select", symbol: "selection.pin.in.out", key: "v", tint: .white, usesKind: false,
                    tip: "Pick a stretch of columns to copy, cut, paste or delete"),
            ToolDef(id: "gauntlet", label: "Gauntlet", symbol: "square.grid.3x3.fill", key: "g", tint: Neon.straw, usesKind: false),
            ToolDef(id: "start", label: "Start", symbol: "flag.fill", key: "t", tint: Neon.lime, usesKind: false),
        ]
        return t
    }

    /// the rail (ToolGrouping.courseRail): Pan, Build, Hazards, the pickups, Edit, Gauntlet, Start
    static func rail(_ r: Registry, gauntlet: Bool = true) -> [RailItem] {
        let all = Tools.all(r)
        func def(_ id: String) -> ToolDef? { all.first { $0.id == id } }
        let pieces = r.hazards.map { ($0.id, $0.toolGroup) } + r.bouncerKinds.map { ($0.id, "bouncers") } // (the Bouncers: the spring, bumpers, gates)
        return ToolGrouping.courseRail(hazards: pieces, gauntlet: gauntlet).compactMap { item in
            switch item {
            case .tool(let id): return def(id).map { RailItem.tool($0) }
            case .group(let g): return .group(ToolGroup(id: g.id, label: g.label, members: g.members.compactMap(def)))
            }
        }
    }

    /// the group a tool's in (its button lit when it's the tool)
    static func group(of tool: String, in rail: [RailItem]) -> ToolGroup? {
        for i in rail { if case .group(let g) = i, g.members.contains(where: { $0.id == tool }) { return g } }
        return nil
    }
}

struct ToolPalette: View {
    @ObservedObject var model: EditorModel
    let vertical: Bool
    @Namespace private var glow

    var body: some View {
        let tools = Tools.all(model.registry).filter { model.chunk == nil || $0.id != "gauntlet" } // (a chunk lays no maze door)
        let rail = Tools.rail(model.registry, gauntlet: model.chunk == nil)
        let current = tools.first { $0.id == model.tool }
        Group {
            if vertical {
                // a column when it fits (nothing to scroll, nothing to drift); scrolling only when it doesn't (an iPad
                // mini on its side), and then no bounce
                let column = VStack(spacing: 6) {
                    ForEach(rail) { i in item(i, vertical: true) }
                    Divider().overlay(model.accent.opacity(0.3)).padding(.vertical, 4)
                    extras(current, vertical: true)
                }
                .padding(.vertical, 10).padding(.horizontal, 6)
                ViewThatFits(in: .vertical) {
                    column.frame(maxHeight: .infinity, alignment: .top)
                    ScrollView(.vertical, showsIndicators: false) { column }
                        .scrollBounceBehavior(.basedOnSize)
                }
                .frame(width: 66)
                .glass(model.accent, radius: 22)
            } else {
                VStack(spacing: 6) {
                    if current?.usesKind == true || model.erase { extras(current, vertical: false).transition(.move(edge: .bottom).combined(with: .opacity)) }
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 4) { ForEach(rail) { i in item(i, vertical: false) } }
                            .padding(.horizontal, 10).padding(.vertical, 6)
                    }
                    .scrollBounceBehavior(.basedOnSize)
                    .glass(model.accent, radius: 20)
                }
                .animation(.spring(response: 0.35, dampingFraction: 0.8), value: model.tool)
            }
        }
        .background(shortcuts(tools))
        .onChange(of: model.tool) { _, t in if let g = Tools.group(of: t, in: rail) { GroupMemory.set(g.id, t) } } // (a key's pick too)
    }

    @ViewBuilder private func item(_ i: RailItem, vertical: Bool) -> some View {
        switch i {
        case .tool(let t): button(t, compact: !vertical)
        case .group(let g): group(g, vertical: vertical)
        }
    }

    /// a group's button: the tool it last had; its flyout, the group's tools with their keys and what they do
    private func group(_ g: ToolGroup, vertical: Bool) -> some View {
        let f = ToolGrouping.face(members: g.members.map(\.id), current: model.tool, erasing: model.erase, remembered: GroupMemory.get(g.id))
        let shown = g.members.first { $0.id == f.shown } ?? g.members[0]
        return ToolGroupButton(label: g.label, selected: f.lit, vertical: vertical,
                               face: ToolFace(symbol: shown.symbol, label: g.label, tint: shown.tint, selected: f.lit, compact: !vertical, more: true, namespace: glow),
                               pick: { withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = shown.id } }) { close in
            FlyoutPanel(title: g.label, accent: model.accent) {
                ForEach(Array(g.members.enumerated()), id: \.element.id) { n, t in
                    FlyoutRow(index: n, name: t.label, key: t.key, tip: t.tip, tint: t.tint, selected: ToolGrouping.lit(t.id, current: model.tool, erasing: model.erase)) {
                        Image(systemName: t.symbol).font(.system(size: 20, weight: .semibold)).symbolRenderingMode(.hierarchical)
                            .foregroundStyle(t.tint).shadow(color: t.tint.opacity(0.6), radius: 5)
                    } action: {
                        withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = t.id }
                        close()
                    }
                }
            }
        }
    }

    @ViewBuilder private func button(_ t: ToolDef, compact: Bool = false) -> some View {
        let b = ToolButton(symbol: t.symbol, label: t.label, key: t.key, tint: t.tint, selected: ToolGrouping.lit(t.id, current: model.tool, erasing: model.erase), compact: compact, namespace: glow) {
            if t.id == "gauntlet" { gauntletTapped(); return }
            withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = t.id }
        }
        if t.id == "start" { b.contextMenu { startMenu } } // (hold it: the start flag's choices)
        else if t.id == "gauntlet" { b.contextMenu { gauntletMenu } } // (hold it: the maze's)
        else { b }
    }

    /// the Gauntlet tool: the level's maze opened for editing (with none yet: placing one)
    private func gauntletTapped() {
        model.erase = false
        if model.meta?.gauntlet != nil { withAnimation(.spring(response: 0.4, dampingFraction: 0.85)) { model.openMaze() } }
        else { withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = "gauntlet" } }
    }

    /// held down on the Gauntlet tool: the maze edited, found, moved along, swapped, taken out
    @ViewBuilder private var gauntletMenu: some View {
        let name = model.meta?.gauntlet
        Button { withAnimation(.spring(response: 0.4, dampingFraction: 0.85)) { model.openMaze() } } label: {
            Label(name.map { "Edit the \($0)" } ?? "Edit the maze", systemImage: "square.grid.3x3.square")
        }
        .disabled(name == nil)
        Button {
            if let n = name, let s = model.scene, let g = s.gauntlets.first(where: { $0.name == n }) { model.canvas?.center(on: Double(g.c0 + g.c1) / 2 * s.cell) }
        } label: { Label("Go to it", systemImage: "scope") }
        .disabled(name == nil)
        Button { model.generateMaze() } label: {
            Label(name == nil ? "Add a generated maze" : "Generate a new maze", systemImage: "dice.fill")
        }
        .disabled(model.generating)
        Button { withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = "gauntlet" } } label: {
            Label(name == nil ? "Place a maze" : "Move it along, or swap it", systemImage: "arrow.left.and.right.square")
        }
        if let n = name {
            Button(role: .destructive) { model.removeGauntlet(n) } label: { Label("Take the \(n) out", systemImage: "trash") }
        }
    }

    /// the start flag's choices, held down on the Start tool: back to the level's start, the flag shown, placed
    @ViewBuilder private var startMenu: some View {
        let start = model.scene?.start
        Button { withAnimation(.spring(response: 0.35)) { model.setStart(nil) }; Haptic.success() } label: {
            Label("Back to the level's start", systemImage: "flag.slash")
        }
        .disabled(start == nil)
        Button { if let s = start { model.canvas?.center(on: s) } } label: {
            Label(start.map { String(format: "Show the flag (%.1f m)", $0) } ?? "Show the flag", systemImage: "scope")
        }
        .disabled(start == nil)
        Button { withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = "start" } } label: {
            Label("Place the flag", systemImage: "flag.fill")
        }
    }

    @ViewBuilder private func extras(_ current: ToolDef?, vertical: Bool) -> some View {
        let layout = vertical ? AnyLayout(VStackLayout(spacing: 6)) : AnyLayout(HStackLayout(spacing: 8))
        layout {
            if current?.usesKind == true {
                ForEach(model.registry.buildKinds) { k in
                    Button { withAnimation(.spring(response: 0.3)) { model.kind = k.id; model.erase = false }; Haptic.tap() } label: {
                        KindSwatch(kind: k, selected: model.kind == k.id, accent: model.accent)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(k.label)
                }
            }
            Button { withAnimation(.spring(response: 0.3)) { model.erase.toggle() }; Haptic.tap() } label: {
                Image(systemName: model.erase ? "eraser.fill" : "eraser")
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundStyle(model.erase ? Color(hex: "#ff6a80") : .white.opacity(0.7))
                    .frame(width: 42, height: 38)
                    .background(RoundedRectangle(cornerRadius: 11).fill(model.erase ? Color(hex: "#ff6a80").opacity(0.2) : .clear))
                    .overlay(RoundedRectangle(cornerRadius: 11).strokeBorder(model.erase ? Color(hex: "#ff6a80") : .white.opacity(0.15)))
                    .symbolEffect(.bounce, value: model.erase)
            }
            .buttonStyle(.plain)
            .accessibilityLabel(model.erase ? "Erasing" : "Erase")
        }
        .padding(vertical ? 0 : 8)
        .modifier(OptionalGlass(on: !vertical, accent: model.accent))
    }

    /// the tools' keys and the editor's commands, for a hardware keyboard (iPad)
    private func shortcuts(_ tools: [ToolDef]) -> some View {
        ZStack {
            ForEach(tools.filter { !$0.key.isEmpty }) { t in
                Button("") { if t.id == "gauntlet" { gauntletTapped() } else { model.tool = t.id } }.keyboardShortcut(KeyEquivalent(Character(t.key)), modifiers: [])
            }
            ForEach(Array(model.registry.buildKinds.enumerated()), id: \.offset) { i, k in
                Button("") { model.kind = k.id }.keyboardShortcut(KeyEquivalent(Character(k.key)), modifiers: [])
            }
            Button("") { model.undo() }.keyboardShortcut("z", modifiers: .command)
            Button("") { model.redo() }.keyboardShortcut("z", modifiers: [.command, .shift])
            Button("") { model.copy() }.keyboardShortcut("c", modifiers: .command)
            Button("") { model.cut() }.keyboardShortcut("x", modifiers: .command)
            Button("") { model.paste() }.keyboardShortcut("v", modifiers: .command)
            Button("") { model.deleteSelection(); model.removePicked() }.keyboardShortcut(.delete, modifiers: [])
            Button("") { model.erase.toggle() }.keyboardShortcut("e", modifiers: [])
            Button("") { model.setStart(nil) }.keyboardShortcut("t", modifiers: .shift) // (the start flag: back to the level's start)
            Button("") { model.canvas?.zoom(by: 1.25) }.keyboardShortcut("=", modifiers: .command)
            Button("") { model.canvas?.zoom(by: 0.8) }.keyboardShortcut("-", modifiers: .command)
        }
        .opacity(0)
        .accessibilityHidden(true)
    }
}

struct OptionalGlass: ViewModifier {
    let on: Bool
    let accent: Color
    func body(content: Content) -> some View { if on { content.glass(accent, radius: 16) } else { content } }
}

/// a wall kind as a little stack of what it's made of
struct KindSwatch: View {
    let kind: WallKindInfo
    let selected: Bool
    let accent: Color
    var body: some View {
        let hay = kind.id == "hay", mix = kind.id == "mix"
        VStack(spacing: 1.5) {
            ForEach(0..<3) { i in
                RoundedRectangle(cornerRadius: 2)
                    .fill(hay || (mix && i == 0) ? Neon.hayLit : Color(hex: "#13243c"))
                    .overlay(RoundedRectangle(cornerRadius: 2).strokeBorder(hay || (mix && i == 0) ? Neon.straw.opacity(0.6) : accent.opacity(0.6), lineWidth: 0.8))
                    .frame(width: 22, height: 8)
            }
        }
        .padding(6)
        .background(RoundedRectangle(cornerRadius: 10).fill(selected ? accent.opacity(0.2) : .clear))
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(selected ? accent : .white.opacity(0.12), lineWidth: selected ? 1.4 : 1))
        .shadow(color: selected ? accent.opacity(0.6) : .clear, radius: 6)
        .scaleEffect(selected ? 1.06 : 1)
    }
}

/// the Pencil Pro's squeeze: the tools in a ring around its tip, springing open; one picked, or a tap away, closes it
struct PencilPalette: View {
    @ObservedObject var model: EditorModel
    let at: CGPoint
    @State private var open = false

    var body: some View {
        let tools = Tools.all(model.registry)
        let r: CGFloat = 92
        ZStack {
            Color.black.opacity(open ? 0.25 : 0).ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture { close() }
            Circle().fill(.ultraThinMaterial).frame(width: r * 2 + 64, height: r * 2 + 64)
                .overlay(Circle().strokeBorder(model.accent.opacity(0.6), lineWidth: 1))
                .shadow(color: model.accent.opacity(0.5), radius: 18)
                .scaleEffect(open ? 1 : 0.3).opacity(open ? 1 : 0)
                .position(at)
            Button { withAnimation(.spring(response: 0.3)) { model.erase.toggle() }; close() } label: {
                Image(systemName: model.erase ? "eraser.fill" : "eraser").font(.system(size: 20, weight: .bold))
                    .foregroundStyle(model.erase ? Color(hex: "#ff6a80") : .white).frame(width: 46, height: 46)
                    .background(Circle().fill(.black.opacity(0.4)))
            }
            .buttonStyle(.plain)
            .position(at)
            .scaleEffect(open ? 1 : 0.2)
            ForEach(Array(tools.enumerated()), id: \.element.id) { i, t in
                let a = -Double.pi / 2 + Double(i) * 2 * .pi / Double(tools.count)
                Button { withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { model.tool = t.id }; Haptic.tap(); close() } label: {
                    Image(systemName: t.symbol).font(.system(size: 19, weight: .semibold)).symbolRenderingMode(.hierarchical)
                        .foregroundStyle(model.tool == t.id ? .white : t.tint)
                        .frame(width: 44, height: 44)
                        .background(Circle().fill(model.tool == t.id ? t.tint.opacity(0.45) : .black.opacity(0.35)))
                        .overlay(Circle().strokeBorder(t.tint.opacity(model.tool == t.id ? 1 : 0.4), lineWidth: 1))
                        .shadow(color: model.tool == t.id ? t.tint : .clear, radius: 8)
                }
                .buttonStyle(.plain)
                .accessibilityLabel(t.label)
                .position(x: at.x + (open ? CGFloat(cos(a)) * r : 0), y: at.y + (open ? CGFloat(sin(a)) * r : 0))
                .opacity(open ? 1 : 0)
                .animation(.spring(response: 0.38, dampingFraction: 0.7).delay(Double(i) * 0.015), value: open)
            }
        }
        .onAppear { withAnimation(.spring(response: 0.35, dampingFraction: 0.75)) { open = true } }
    }

    private func close() {
        withAnimation(.easeOut(duration: 0.18)) { open = false }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.18) { model.palette = nil }
    }
}
