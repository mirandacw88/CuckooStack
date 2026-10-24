// The maze editor: a level's gauntlet's grid, full screen in place of the course (the Gauntlet tool opens it; Done
// goes back to the course). Laid out like the course editor: on the iPad the maze's tool rail (its tools, the cells to
// paint with, the eraser), the overlays' chips over the grid, its inspector on the right (the maze and its
// description, the checklist the tests want, the egg meter's runs, the game's bot, its history); on the iPhone the
// tools along the bottom (or a rail on its side) and the inspector a sheet.
import SwiftUI

/// a maze cell as a little tile, drawn as the canvas draws it
struct MazeCellTile: View {
    let cell: MazeCell
    let zone: String
    var size: CGFloat = 26
    var body: some View {
        Canvas { ctx, sz in
            let look = ZoneLook.of(zone)
            ctx.withCGContext { cg in
                let f = CGRect(origin: .zero, size: sz).insetBy(dx: 2, dy: 2)
                if cell.ch == "." {
                    cg.setStrokeColor(UIColor(white: 1, alpha: 0.25).cgColor)
                    cg.setLineDash(phase: 0, lengths: [3, 3])
                    cg.stroke(f)
                } else {
                    MazePaint.cell(cg, cell.ch, cell.ch == "S" || cell.ch == "C" ? f.insetBy(dx: 0, dy: f.height * 0.25).offsetBy(dx: 0, dy: f.height * 0.25)
                                   : cell.ch == "^" ? CGRect(x: f.minX, y: f.minY + f.height * 0.6, width: f.width, height: f.height * 0.4) : f, look)
                }
            }
        }
        .frame(width: size, height: size)
    }
}

/// the maze's tools and cells: a rail beside the grid, or a strip along the bottom
struct MazeRail: View {
    @ObservedObject var maze: MazeModel
    let vertical: Bool
    @Namespace private var glow

    var body: some View {
        Group {
            if vertical {
                let column = VStack(spacing: 6) {
                    ForEach(MazeTool.rail(chunk: maze.isChunk)) { t in item(t, vertical: true) }
                    Divider().overlay(maze.accent.opacity(0.3)).padding(.vertical, 4)
                    cells(vertical: true)
                }
                .padding(.vertical, 10).padding(.horizontal, 6)
                ViewThatFits(in: .vertical) {
                    column.frame(maxHeight: .infinity, alignment: .top)
                    ScrollView(.vertical, showsIndicators: false) { column }.scrollBounceBehavior(.basedOnSize)
                }
                .frame(width: 76)
                .glass(maze.accent, radius: 22)
            } else {
                VStack(spacing: 6) {
                    ScrollView(.horizontal, showsIndicators: false) { cells(vertical: false).padding(.horizontal, 10).padding(.vertical, 6) }
                        .scrollBounceBehavior(.basedOnSize)
                        .glass(maze.accent, radius: 16)
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 4) { ForEach(MazeTool.rail(chunk: maze.isChunk)) { t in item(t, vertical: false) } }.padding(.horizontal, 10).padding(.vertical, 6)
                    }
                    .scrollBounceBehavior(.basedOnSize)
                    .glass(maze.accent, radius: 20)
                }
            }
        }
        .background(shortcuts)
        .onChange(of: maze.tool) { _, t in if MazeTool.lines.contains(t) { GroupMemory.set("maze.lines", t) } }
        .onChange(of: maze.primary) { _, ch in if let c = MazeCell.named(ch) { GroupMemory.set("maze.cells." + c.family, String(ch)) } }
    }

    @ViewBuilder private func item(_ t: MazeTool, vertical: Bool) -> some View {
        if t.id == MazeTool.lines[0] { lines(vertical: vertical) } else { button(t, compact: !vertical) }
    }

    /// Columns and Rows behind one button (Lines): each copies lines in, or (erasing) takes them out
    private func lines(vertical: Bool) -> some View {
        let members = MazeTool.all.filter { MazeTool.lines.contains($0.id) }
        let f = ToolGrouping.face(members: members.map(\.id), current: maze.tool, erasing: false, remembered: GroupMemory.get("maze.lines"))
        let shown = members.first { $0.id == f.shown } ?? members[0]
        return ToolGroupButton(label: "Lines", selected: f.lit, vertical: vertical,
                               face: ToolFace(symbol: shown.symbol, label: "Lines", tint: shown.tint, selected: f.lit, compact: !vertical, more: true, namespace: glow),
                               pick: { withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { maze.tool = shown.id } }) { close in
            FlyoutPanel(title: "Lines", accent: maze.accent) {
                ForEach(Array(members.enumerated()), id: \.element.id) { n, t in
                    FlyoutRow(index: n, name: t.label, key: t.key, tip: t.tip, tint: t.tint, selected: maze.tool == t.id) {
                        Image(systemName: t.symbol).font(.system(size: 20, weight: .semibold)).symbolRenderingMode(.hierarchical)
                            .foregroundStyle(t.tint).shadow(color: t.tint.opacity(0.6), radius: 5)
                    } trailing: {
                        HStack(spacing: 4) { // (copy in, take out)
                            lineMode(t, erase: false, symbol: "plus.square.on.square", close: close)
                            lineMode(t, erase: true, symbol: "minus.square", close: close)
                        }
                    } action: {
                        withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { maze.tool = t.id }
                        close()
                    }
                }
            }
        }
    }

    private func lineMode(_ t: MazeTool, erase: Bool, symbol: String, close: @escaping () -> Void) -> some View {
        let on = maze.tool == t.id && maze.erase == erase
        let tint = erase ? Color(hex: "#ff6a80") : t.tint
        return Button { maze.tool = t.id; maze.erase = erase; Haptic.tap(); close() } label: {
            Image(systemName: symbol).font(.system(size: 15, weight: .semibold)).foregroundStyle(on ? .white : tint)
                .frame(width: 34, height: 30)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? tint.opacity(0.35) : .white.opacity(0.05)))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(tint.opacity(on ? 1 : 0.35), lineWidth: 1))
        }
        .buttonStyle(PressSpring())
        .accessibilityLabel(erase ? "Take \(t.label.lowercased()) out" : "Copy \(t.label.lowercased()) in")
    }

    private func button(_ t: MazeTool, compact: Bool = false) -> some View {
        ToolButton(symbol: t.symbol, label: t.label, key: t.key, tint: t.tint, selected: maze.tool == t.id, compact: compact, namespace: glow) {
            withAnimation(.spring(response: 0.32, dampingFraction: 0.72)) { maze.tool = t.id }
        }
        .contextMenu { menu(t) }
    }

    /// held down: what each tool does the other way
    @ViewBuilder private func menu(_ t: MazeTool) -> some View {
        switch t.id {
        case "cols", "rows":
            let what = t.id == "cols" ? "columns" : "rows"
            Button { maze.tool = t.id; maze.erase = false } label: { Label("Copy \(what) in", systemImage: "plus.square.on.square") }
            Button(role: .destructive) { maze.tool = t.id; maze.erase = true } label: { Label("Take \(what) out", systemImage: "minus.square") }
        case "start":
            Button { maze.clearStart(); Haptic.success() } label: { Label("From the maze's door", systemImage: "flag.slash") }
                .disabled(maze.scene?.start == nil)
            Button { maze.tool = "start" } label: { Label("Place the flag", systemImage: "flag.fill") }
        case "pan":
            Button { maze.canvas?.fit() } label: { Label("Fit the maze", systemImage: "arrow.up.left.and.down.right.magnifyingglass") }
        default: EmptyView()
        }
    }

    /// the cells to paint with, by family (Terrain, Bouncers, Hazards, Doors: each button the cell last painted with,
    /// its flyout the rest; the first cell: what a finger paints, the second: the eraser's), and the eraser
    @ViewBuilder private func cells(vertical: Bool) -> some View {
        let fams = MazeCellFamily.all.filter { !maze.isChunk || $0.id != "doors" } // (a chunk has no doors)
        if vertical { // (two a row)
            Grid(horizontalSpacing: 3, verticalSpacing: 3) {
                ForEach(0..<((fams.count + 1) / 2), id: \.self) { r in
                    GridRow {
                        family(fams[2 * r], vertical: true)
                        if 2 * r + 1 < fams.count { family(fams[2 * r + 1], vertical: true) }
                    }
                }
            }
            eraser
        } else {
            HStack(spacing: 6) {
                ForEach(fams) { f in family(f, vertical: false) }
                eraser
            }
        }
    }

    private func family(_ f: MazeCellFamily, vertical: Bool) -> some View {
        let cells = f.cells
        let picked = ToolGrouping.face(members: cells.map { String($0.ch) }, current: String(maze.primary), erasing: false, remembered: GroupMemory.get("maze.cells." + f.id))
        let current = picked.lit ? cells.first { String($0.ch) == picked.shown } : nil
        let shown = cells.first { String($0.ch) == picked.shown } ?? cells[0]
        let erasing = cells.contains { $0.ch == maze.secondary }
        let size: CGFloat = vertical ? 24 : 28
        let face = VStack(spacing: 2) {
            MazeCellTile(cell: shown, zone: maze.host.zone, size: size)
                .padding(4)
                .background(RoundedRectangle(cornerRadius: 9).fill(current != nil ? maze.accent.opacity(0.28) : .clear))
                .overlay(RoundedRectangle(cornerRadius: 9).strokeBorder(current != nil ? maze.accent : erasing ? Color(hex: "#ff6a80").opacity(0.8) : .white.opacity(0.08),
                                                                      style: StrokeStyle(lineWidth: current != nil ? 1.6 : 1, dash: current == nil && erasing ? [3, 2] : [])))
                .shadow(color: current != nil ? maze.accent.opacity(0.5) : .clear, radius: 6)
                .overlay(alignment: .bottomTrailing) { GroupMark(tint: current != nil ? .white : maze.accent) }
                .scaleEffect(current != nil ? 1.05 : 1)
                .animation(.spring(response: 0.3, dampingFraction: 0.7), value: current?.ch)
            if vertical { Text(f.label).font(.system(size: 8, weight: .semibold, design: .rounded)).foregroundStyle(.white.opacity(current != nil ? 0.9 : 0.55)).lineLimit(1).minimumScaleFactor(0.7) }
        }
        return ToolGroupButton(label: f.label, selected: current != nil, vertical: vertical, face: face,
                               pick: { withAnimation(.spring(response: 0.3)) { maze.pick(shown.ch) }; Haptic.tap() }) { close in
            FlyoutPanel(title: f.label, accent: maze.accent) {
                ForEach(Array(cells.enumerated()), id: \.element.id) { n, c in
                    FlyoutRow(index: n, name: c.name, key: c.key, tip: c.tip, tint: maze.accent, selected: maze.primary == c.ch) {
                        MazeCellTile(cell: c, zone: maze.host.zone, size: 32)
                    } trailing: {
                        if !c.isDoor { eraseWith(c, close: close) }
                    } action: {
                        withAnimation(.spring(response: 0.3)) { maze.pick(c.ch) }
                        close()
                    }
                }
            }
        }
    }

    /// in a cell's row: the eraser paints with this cell
    private func eraseWith(_ c: MazeCell, close: @escaping () -> Void) -> some View {
        let on = maze.secondary == c.ch
        let red = Color(hex: "#ff6a80")
        return Button { maze.secondary = c.ch; Haptic.tap(); close() } label: {
            Image(systemName: on ? "eraser.fill" : "eraser").font(.system(size: 15, weight: .semibold)).foregroundStyle(on ? .white : red)
                .frame(width: 34, height: 30)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? red.opacity(0.35) : .white.opacity(0.05)))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(red.opacity(on ? 1 : 0.35), style: StrokeStyle(lineWidth: 1, dash: on ? [] : [3, 2])))
        }
        .buttonStyle(PressSpring())
        .accessibilityLabel("Erase with \(c.name)")
    }

    private var eraser: some View {
        Button { withAnimation(.spring(response: 0.3)) { maze.erase.toggle() }; Haptic.tap() } label: {
            Image(systemName: maze.erase ? "eraser.fill" : "eraser")
                .font(.system(size: 18, weight: .semibold))
                .foregroundStyle(maze.erase ? Color(hex: "#ff6a80") : .white.opacity(0.7))
                .frame(width: 42, height: 38)
                .background(RoundedRectangle(cornerRadius: 11).fill(maze.erase ? Color(hex: "#ff6a80").opacity(0.2) : .clear))
                .overlay(RoundedRectangle(cornerRadius: 11).strokeBorder(maze.erase ? Color(hex: "#ff6a80") : .white.opacity(0.15)))
                .symbolEffect(.bounce, value: maze.erase)
        }
        .buttonStyle(.plain)
        .accessibilityLabel(maze.erase ? "Erasing \(MazeCell.named(maze.secondary)?.name ?? "")" : "Erase")
    }

    private var shortcuts: some View {
        ZStack {
            ForEach(MazeTool.all.filter { !maze.isChunk || !MazeTool.doorTools.contains($0.id) }) { t in Button("") { maze.tool = t.id }.keyboardShortcut(KeyEquivalent(Character(t.key)), modifiers: []) }
            ForEach(MazeCell.all.filter { !maze.isChunk || !$0.isDoor }) { c in Button("") { maze.pick(c.ch) }.keyboardShortcut(KeyEquivalent(Character(c.key)), modifiers: []) }
            Button("") { maze.swapCells() }.keyboardShortcut("x", modifiers: [])
            Button("") { maze.erase.toggle() }.keyboardShortcut("e", modifiers: [])
            Button("") { maze.canvas?.fit() }.keyboardShortcut("f", modifiers: [])
            Button("") { maze.undo() }.keyboardShortcut("z", modifiers: .command)
            Button("") { maze.redo() }.keyboardShortcut("z", modifiers: [.command, .shift])
            Button("") { maze.canvas?.zoom(by: 1.25) }.keyboardShortcut("=", modifiers: .command)
            Button("") { maze.canvas?.zoom(by: 0.8) }.keyboardShortcut("-", modifiers: .command)
            Button("") { maze.clearStart() }.keyboardShortcut("t", modifiers: .shift)
            Button("") { withAnimation(.spring(response: 0.4, dampingFraction: 0.85)) { maze.host.closeMaze() } }.keyboardShortcut(.escape, modifiers: [])
        }
        .opacity(0)
        .accessibilityHidden(true)
    }
}

/// the overlays over the grid: the bot's way, where she gets, the near misses; and the checker's word
struct MazeChips: View {
    @ObservedObject var maze: MazeModel
    var onVerdict: () -> Void = {}
    var body: some View {
        HStack(spacing: 8) {
            let v = maze.verdictState
            Button(action: onVerdict) { StatusPill(text: v.text, symbol: v.symbol, tint: v.tint, busy: maze.checking) }.buttonStyle(.plain)
            Spacer(minLength: 0)
            chip("The way", "point.topleft.down.to.point.bottomright.curvepath.fill", $maze.showWay, Neon.cyan)
            chip("Reach", "circle.dotted.and.circle", $maze.showReach, Neon.lime)
            chip("Near misses", "sparkles", $maze.showMisses, Neon.straw)
        }
    }
    private func chip(_ label: String, _ symbol: String, _ on: Binding<Bool>, _ tint: Color) -> some View {
        Button { withAnimation(.spring(response: 0.3)) { on.wrappedValue.toggle() }; Haptic.tap() } label: {
            Label(label, systemImage: symbol)
                .font(.system(size: 12, weight: .semibold, design: .rounded))
                .labelStyle(.titleAndIcon)
                .foregroundStyle(on.wrappedValue ? tint : .white.opacity(0.5))
                .padding(.horizontal, 10).padding(.vertical, 6)
                .background(Capsule().fill(on.wrappedValue ? tint.opacity(0.16) : .white.opacity(0.05)))
                .overlay(Capsule().strokeBorder(on.wrappedValue ? tint.opacity(0.7) : .white.opacity(0.15)))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(on.wrappedValue ? .isSelected : [])
    }
}

/// the maze's inspector: the maze, its checklist, the egg meter, the game's bot, its history
struct MazeInspector: View {
    @ObservedObject var maze: MazeModel
    var onPlay: (Bool) -> Void
    @State private var description = ""
    @FocusState private var editing: Bool
    @State private var nameText = ""
    @FocusState private var naming: Bool

    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                mazeCard
                if let level = maze.host as? EditorModel { // (a level's maze: its generator; a chunk's is made by hand)
                    Card(title: "Generator", symbol: "dice.fill", accent: Neon.straw) {
                        MazeGenButton(model: level, title: "Roll a new maze")
                        HazardChips(model: level, hazards: level.registry.hazards.filter { level.mazeHazardIDs.contains($0.id) }, off: Binding(get: { level.mazeGenOff }, set: { level.mazeGenOff = $0 }))
                        MazeSizeControls(model: level)
                        Text("Each press: a new maze that passes the checklist, in place of this one (undo brings it back).")
                            .font(.caption).foregroundStyle(.white.opacity(0.6))
                    }
                }
                MazeChecklistCard(maze: maze)
                MazeMeterCard(maze: maze)
                MazeHistoryCard(maze: maze)
            }
            .padding(12)
        }
        .scrollIndicators(.hidden)
        .background(NightBackdrop(zone: maze.host.zone))
        .onAppear { description = (maze.scene?.description ?? []).joined(separator: "\n"); nameText = maze.storage.shown(maze.name) }
        .onChange(of: maze.name) { _, n in if !naming { nameText = maze.storage.shown(n) } }
        .onChange(of: maze.scene?.description ?? []) { _, d in if !editing { description = d.joined(separator: "\n") } }
    }

    private var mazeCard: some View {
        Card(title: "The \(maze.storage.shown(maze.name))", symbol: "square.grid.3x3.fill", accent: Neon.straw) {
            nameField
            HStack {
                Text("\(maze.scene?.cols ?? 0) × \(maze.rows.count)").font(.system(size: 22, weight: .heavy, design: .rounded)).monospacedDigit()
                    .contentTransition(.numericText())
                Text("cells").font(.footnote).foregroundStyle(.white.opacity(0.6))
                Spacer()
                if maze.storage.dirty(maze.name) {
                    Label("Not synced", systemImage: "icloud.and.arrow.up").font(.caption.weight(.semibold)).foregroundStyle(Neon.cyan)
                }
            }
            HStack(spacing: 8) {
                Button { onPlay(false) } label: { Label("Play", systemImage: "play.fill").frame(maxWidth: .infinity) }
                    .buttonStyle(.borderedProminent).tint(Neon.straw)
                Button { onPlay(true) } label: { Label("Watch", systemImage: "eye.fill").frame(maxWidth: .infinity) }
                    .buttonStyle(.bordered).tint(Neon.cyan)
            }
            Text(maze.scene?.start != nil ? "From the start flag (hold Start: from the door)" : "From the maze's door (the Start tool: from a cell)")
                .font(.caption).foregroundStyle(.white.opacity(0.6))
            Text("Its description").font(.caption).foregroundStyle(.white.opacity(0.6))
            TextEditor(text: $description)
                .focused($editing)
                .font(.system(size: 13, design: .rounded))
                .scrollContentBackground(.hidden)
                .frame(minHeight: 70, maxHeight: 160)
                .padding(6)
                .background(RoundedRectangle(cornerRadius: 10).fill(.white.opacity(0.06)))
            if description != (maze.scene?.description ?? []).joined(separator: "\n") {
                Button { maze.setDescription(description); editing = false; Haptic.success() } label: { Text("Save the description") }
                    .buttonStyle(.bordered).tint(Neon.cyan)
            }
        }
    }

    /// its name: typed with spaces, renamed on Return (its file, the levels that lay it, the Mac at the next sync)
    private var nameField: some View {
        let typed = maze.storage.typed(nameText)
        let changed = typed != maze.name && !typed.isEmpty
        let ok = maze.storage.valid(typed)
        return VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 8) {
                Image(systemName: "character.cursor.ibeam").foregroundStyle(Neon.straw)
                TextField("Its name", text: $nameText)
                    .focused($naming)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .submitLabel(.done)
                    .font(.system(size: 16, weight: .semibold, design: .rounded))
                    .onSubmit { commitName() }
                    .accessibilityLabel("The maze's name")
                if changed {
                    Button { commitName() } label: { Image(systemName: "checkmark.circle.fill").font(.title3) }
                        .buttonStyle(.plain).foregroundStyle(ok ? Neon.lime : .white.opacity(0.3)).disabled(!ok)
                        .transition(.scale.combined(with: .opacity))
                        .accessibilityLabel("Rename")
                }
            }
            .padding(.horizontal, 10).padding(.vertical, 8)
            .background(RoundedRectangle(cornerRadius: 10).fill(.white.opacity(naming ? 0.1 : 0.06)))
            .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(changed && !ok ? Neon.red.opacity(0.7) : naming ? Neon.straw.opacity(0.6) : .clear, lineWidth: 1.5))
            .animation(.spring(response: 0.3, dampingFraction: 0.8), value: changed)
            .animation(.easeOut(duration: 0.15), value: naming)
            if changed {
                Text(ok ? "Return renames it: its file, the levels that lay it, and the Mac at the next sync" : "Letters, digits and spaces, a letter first")
                    .font(.caption).foregroundStyle(ok ? .white.opacity(0.6) : Neon.red)
            }
        }
    }

    private func commitName() {
        maze.rename(nameText)
        nameText = maze.storage.shown(maze.name) // (as the file has it; turned away: the name it had)
        naming = false
    }

}

/// the checklist the tests want of a maze (a chunk's: of the corridor round it, and flipped too with Mirror on)
struct MazeChecklistCard: View {
    @ObservedObject var maze: MazeModel
    var body: some View {
        let items = maze.verdict
        let v = maze.verdictState
        return Card(title: "The checker: \(v.text)", symbol: v.symbol, accent: v.tint) {
            ForEach(items) { it in
                Button {
                    if let c = it.cell { maze.canvas?.center(on: c[0], maze.rows.count - 1 - c[1]) }
                } label: {
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: it.ok == nil ? "hourglass" : it.ok! ? "checkmark.circle.fill" : "xmark.circle.fill")
                            .foregroundStyle(it.ok == nil ? Neon.cyan : it.ok! ? Neon.lime : Neon.red)
                            .symbolEffect(.pulse, isActive: it.ok == nil)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(it.what).font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(.white)
                            Text(it.why).font(.system(size: 12)).foregroundStyle(.white.opacity(0.65)).fixedSize(horizontal: false, vertical: true)
                        }
                        Spacer(minLength: 0)
                        if it.cell != nil { Image(systemName: "scope").font(.caption).foregroundStyle(.white.opacity(0.5)) }
                    }
                    .multilineTextAlignment(.leading)
                }
                .buttonStyle(.plain)
                .disabled(it.cell == nil)
            }
            if maze.isChunk {
                Text("A chunk is judged in a plain corridor: a door, a run-in, the chunk on the floor, a run-out, the exit. Its bottom row is the floor.")
                    .font(.caption).foregroundStyle(.white.opacity(0.55))
            }
        }
        .animation(.spring(response: 0.4), value: items.map(\.id))
    }
}

/// the egg meter's runs and the game's bot (a chunk: the solver's runs only)
struct MazeMeterCard: View {
    @ObservedObject var maze: MazeModel
    var body: some View {
        Card(title: "The egg meter", symbol: "gauge.with.dots.needle.67percent", accent: Neon.egg) {
            if let s = maze.report?.solution {
                HStack(spacing: 14) {
                    stat("\(s.moves)", "cells")
                    stat("\(s.minEggs)", "eggs")
                    stat("\(s.perfects)", "perfect")
                    stat("\(s.shaves)", "shaves")
                    stat("\(s.squeezes)", "squeezes")
                }
            }
            ForEach(Array((maze.report?.meter ?? []).enumerated()), id: \.offset) { _, m in
                HStack {
                    Text(m.rate == 1 ? "As the level refills" : String(format: "%.3g× slower", m.rate)).font(.system(size: 13, design: .rounded))
                    Spacer()
                    if m.out { Text(String(format: "out, %.1f s", m.seconds)).font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(Neon.lime) }
                    else { Text("runs dry").font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(Neon.red) }
                }
            }
            if let g = maze.report?.game {
                Divider().overlay(.white.opacity(0.1))
                HStack {
                    Label("The game's bot", systemImage: "cpu").font(.system(size: 13, design: .rounded))
                    Spacer()
                    Text(g.out ? String(format: "out in %.1f s · %d near misses", g.inside, g.perfects + g.closeCalls) : "crashed inside")
                        .font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(g.out ? Neon.lime : Neon.red)
                }
            }
        }
    }

    private func stat(_ v: String, _ label: String) -> some View {
        VStack(spacing: 0) {
            Text(v).font(.system(size: 17, weight: .heavy, design: .rounded)).monospacedDigit().contentTransition(.numericText())
            Text(label).font(.system(size: 10, weight: .semibold)).foregroundStyle(.white.opacity(0.55))
        }
    }
}

/// undo, redo and the steps of the maze's history
struct MazeHistoryCard: View {
    @ObservedObject var maze: MazeModel
    var body: some View {
        let undo = maze.history?.undo ?? [], redo = maze.history?.redo ?? []
        return Card(title: "History", symbol: "clock.arrow.circlepath", accent: Neon.straw) {
            HStack {
                Button { maze.undo() } label: { Label("Undo", systemImage: "arrow.uturn.backward") }.disabled(undo.isEmpty)
                Button { maze.redo() } label: { Label("Redo", systemImage: "arrow.uturn.forward") }.disabled(redo.isEmpty)
                Spacer()
                Menu {
                    Button { maze.revertToSynced() } label: { Label("Back to the synced version", systemImage: "arrow.uturn.backward.circle") }
                        .disabled(!maze.canRevertToSynced)
                } label: { Image(systemName: "ellipsis.circle") }
            }
            .buttonStyle(.bordered).tint(.white)
            if undo.isEmpty && redo.isEmpty { Text("No edits yet").font(.footnote).foregroundStyle(.white.opacity(0.6)) }
            ForEach(Array(redo.enumerated().reversed()), id: \.offset) { j, s in step(s, after: undo.count + j + 1, undone: true) }
            ForEach(Array(undo.enumerated().reversed()), id: \.offset) { i, s in step(s, after: i + 1, undone: false) }
        }
    }

    private func step(_ s: HistoryStep, after k: Int, undone: Bool) -> some View {
        Button { maze.goTo(k) } label: {
            HStack {
                Text(s.label).font(.system(size: 13, design: .rounded)).foregroundStyle(undone ? .white.opacity(0.4) : .white).strikethrough(undone)
                Spacer()
                Text(s.date, style: .relative).font(.caption2).foregroundStyle(.white.opacity(0.45))
            }
        }
        .buttonStyle(.plain)
    }
}

/// the maze in place of the course, for the device it's on and the way it's held
struct MazeLayout: View {
    @ObservedObject var maze: MazeModel
    let isPad: Bool
    let landscapePhone: Bool
    @Binding var inspector: Bool

    var body: some View {
        if isPad {
            HStack(alignment: .top, spacing: 10) {
                MazeRail(maze: maze, vertical: true).transition(.move(edge: .leading).combined(with: .opacity))
                VStack(spacing: 8) {
                    MazeChips(maze: maze) { withAnimation { inspector = true } }
                    canvas
                }
            }
            .padding(10)
        } else if landscapePhone {
            HStack(spacing: 6) {
                MazeRail(maze: maze, vertical: true).padding(.vertical, 4)
                ZStack(alignment: .top) {
                    canvas
                    MazeChips(maze: maze) { inspector = true }.padding(.horizontal, 8).padding(.top, 4)
                }
            }
            .padding(.horizontal, 4)
        } else {
            ZStack(alignment: .bottom) {
                VStack(spacing: 6) {
                    MazeChips(maze: maze) { inspector = true }.padding(.horizontal, 12)
                    canvas.padding(.horizontal, 6)
                    Color.clear.frame(height: 128)
                }
                MazeRail(maze: maze, vertical: false).padding(.horizontal, 8).padding(.bottom, 4)
            }
        }
    }

    private var canvas: some View {
        MazeCanvas(maze: maze)
            .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).strokeBorder(Neon.straw.opacity(0.4), lineWidth: 1))
            .frame(maxHeight: .infinity)
    }
}
