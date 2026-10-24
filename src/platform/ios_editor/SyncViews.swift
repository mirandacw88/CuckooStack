// Sync's screens: a banner over the levels (paired or not, when it last synced, a spinner while it does, what's left
// to settle), pairing with the Mac (the servers Bonjour finds, the code the server printed, or its address typed),
// and a conflict settled side by side.
import SwiftUI

struct SyncBanner: View {
    @ObservedObject var sync: SyncEngine
    @ObservedObject var store: LevelStore
    @State private var pairing = false
    @State private var settling: SyncConflict?
    @State private var spin = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                ZStack {
                    Circle().fill(Neon.cyan.opacity(0.15)).frame(width: 36, height: 36)
                    Image(systemName: sync.paired ? "desktopcomputer" : "link.badge.plus")
                        .font(.system(size: 16, weight: .semibold)).foregroundStyle(Neon.cyan)
                    if sync.syncing {
                        Circle().trim(from: 0, to: 0.7).stroke(Neon.cyan, style: StrokeStyle(lineWidth: 2, lineCap: .round))
                            .frame(width: 36, height: 36).rotationEffect(.degrees(spin ? 360 : 0))
                            .onAppear { withAnimation(.linear(duration: 0.9).repeatForever(autoreverses: false)) { spin = true } }
                            .onDisappear { spin = false }
                    }
                }
                VStack(alignment: .leading, spacing: 2) {
                    Text(sync.status).font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(.white).contentTransition(.opacity)
                    if let d = store.state.lastSync {
                        Text("Last synced \(d.formatted(.relative(presentation: .named)))").font(.system(size: 11)).foregroundStyle(.white.opacity(0.55))
                    } else {
                        Text("Edits stay on this device till you sync").font(.system(size: 11)).foregroundStyle(.white.opacity(0.55))
                    }
                }
                Spacer()
                if sync.paired {
                    Button { Task { await sync.sync() } } label: { Image(systemName: "arrow.triangle.2.circlepath").font(.system(size: 15, weight: .bold)) }
                        .buttonStyle(.borderedProminent).tint(Neon.cyan).disabled(sync.syncing)
                        .keyboardShortcut("s", modifiers: [.command, .shift])
                        .accessibilityLabel("Sync now")
                } else {
                    Button("Pair") { pairing = true }.buttonStyle(.borderedProminent).tint(Neon.cyan)
                }
            }
            if let e = sync.lastError {
                Text(e).font(.footnote).foregroundStyle(Neon.red).transition(.opacity)
            }
            ForEach(sync.conflicts) { c in
                Button { settling = c } label: {
                    Label(conflictTitle(c), systemImage: "arrow.triangle.merge")
                        .font(.system(size: 13, weight: .semibold)).foregroundStyle(Neon.amber)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(8).background(RoundedRectangle(cornerRadius: 10).fill(Neon.amber.opacity(0.12)))
                }
                .buttonStyle(.plain)
                .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
        .padding(12)
        .glass(Neon.cyan, radius: 16)
        .contextMenu { if sync.paired { Button("Pair with another Mac") { pairing = true }; Button("Unpair", role: .destructive) { sync.unpair() } } }
        .sheet(isPresented: $pairing) { PairSheet(sync: sync) }
        .sheet(item: $settling) { c in ConflictSheet(sync: sync, conflict: c) }
    }

    private func conflictTitle(_ c: SyncConflict) -> String {
        switch c.kind {
        case .number: return "Level \(c.number): the Mac has one by that number"
        case .both(let what): return "Level \(c.number): both changed \(what.joined(separator: ", "))"
        case .grid(let name): return "The \(Builtin.gauntletShown(name)): both changed its maze"
        case .chunk(let id): return "The \(ChunkNames.shown(id)) chunk: both changed it"
        }
    }
}

struct PairSheet: View {
    @ObservedObject var sync: SyncEngine
    @Environment(\.dismiss) private var dismiss
    @State private var chosen: FoundServer?
    @State private var code = ""
    @State private var manualHost = ""
    @State private var busy = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text("On the Mac: `python3 tools/level_editor/server.py --lan`. It prints a pairing code; the Mac and this device on the same Wi-Fi.")
                        .font(.footnote)
                }
                Section("Macs nearby") {
                    if sync.found.isEmpty {
                        HStack { ProgressView(); Text("Looking…").foregroundStyle(.secondary) }
                    }
                    ForEach(sync.found) { s in
                        Button { chosen = s } label: {
                            HStack {
                                Image(systemName: "desktopcomputer")
                                Text(s.name)
                                Spacer()
                                if chosen == s { Image(systemName: "checkmark").foregroundStyle(Neon.cyan) }
                            }
                        }
                    }
                }
                Section("Or its address") {
                    TextField("192.168.1.20:8766", text: $manualHost).keyboardType(.URL).textInputAutocapitalization(.never).autocorrectionDisabled()
                }
                Section("Pairing code") {
                    TextField("the code the server printed", text: $code).textInputAutocapitalization(.never).autocorrectionDisabled().font(.system(.body, design: .monospaced))
                }
                if let e = sync.lastError { Text(e).foregroundStyle(Neon.red).font(.footnote) }
            }
            .navigationTitle("Pair with your Mac")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button(busy ? "Pairing…" : "Pair") {
                        guard let server = target else { return }
                        busy = true
                        Task {
                            if await sync.pair(server, code: code) { dismiss(); await sync.sync() }
                            busy = false
                        }
                    }
                    .disabled(target == nil || code.isEmpty || busy)
                }
            }
            .onAppear { sync.browse() }
            .onDisappear { sync.stopBrowsing() }
        }
    }

    private var target: FoundServer? {
        if !manualHost.isEmpty {
            let parts = manualHost.split(separator: ":")
            return FoundServer(name: manualHost, host: String(parts.first ?? ""), port: parts.count > 1 ? Int(parts[1]) ?? 8766 : 8766)
        }
        return chosen
    }
}

struct ConflictSheet: View {
    @ObservedObject var sync: SyncEngine
    let conflict: SyncConflict
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView { VStack(spacing: 12) {
                if let name = conflict.gridName {
                    Text("Both changed the \(name)'s maze: grids aren't merged, so pick one.").font(.callout).padding(.horizontal)
                } else if let id = conflict.chunkId {
                    Text("Both changed the \(ChunkNames.shown(id)) chunk: chunks aren't merged, so pick one.").font(.callout).padding(.horizontal)
                } else if case .number = conflict.kind {
                    Text("The Mac has a level \(conflict.number) of its own; this one was made here.").font(.callout).padding(.horizontal)
                } else {
                    Text("Both changed level \(conflict.number) where the merge can't tell which to keep.").font(.callout).padding(.horizontal)
                }
                if conflict.gridName != nil { // (a maze: both drawn, where they differ outlined)
                    let mine = LevelStore.rows(of: conflict.mine), theirs = LevelStore.rows(of: conflict.theirs)
                    VStack(spacing: 10) {
                        MazeDiffSide(title: "This device", rows: mine, other: theirs, tint: Neon.lime)
                        MazeDiffSide(title: "The Mac", rows: theirs, other: mine, tint: Neon.cyan)
                    }
                    .padding(.horizontal)
                } else {
                    HStack(alignment: .top, spacing: 10) {
                        side("This device", conflict.mine, Neon.lime)
                        if conflict.chunkId != nil { side("The Mac", conflict.theirs, Neon.cyan) }
                        else if case .both = conflict.kind { side("The Mac", conflict.theirs, Neon.cyan) }
                    }
                    .padding(.horizontal)
                }
                VStack(spacing: 8) {
                    if let name = conflict.gridName {
                        choice("Keep mine", "iphone", Neon.lime) { await sync.settle(conflict, .keepMine) }
                        choice("Take the Mac's", "desktopcomputer", Neon.cyan) { await sync.settle(conflict, .takeTheirs) }
                        choice("Keep mine as \(name)_copy, take the Mac's", "plus.square.on.square", Neon.amber) { await sync.settle(conflict, .mineAsNew) }
                    } else if let id = conflict.chunkId {
                        choice("Keep mine", "iphone", Neon.lime) { await sync.settle(conflict, .keepMine) }
                        choice("Take the Mac's", "desktopcomputer", Neon.cyan) { await sync.settle(conflict, .takeTheirs) }
                        choice("Keep mine as \(ChunkNames.copy(of: id, taken: sync.chunks.ids)), take the Mac's", "plus.square.on.square", Neon.magenta) { await sync.settle(conflict, .mineAsNew) }
                    } else if case .both = conflict.kind {
                        choice("Keep mine", "iphone", Neon.lime) { await sync.settle(conflict, .keepMine) }
                        choice("Take the Mac's", "desktopcomputer", Neon.cyan) { await sync.settle(conflict, .takeTheirs) }
                        choice("Keep mine as a new level, take the Mac's", "plus.square.on.square", Neon.amber) { await sync.settle(conflict, .mineAsNew) }
                    } else {
                        choice("Renumber mine (a new level)", "number", Neon.lime) { await sync.settle(conflict, .mineAsNew) }
                        choice("Take the Mac's, drop mine", "desktopcomputer", Neon.red) { await sync.settle(conflict, .takeTheirs) }
                    }
                }
                .padding()
            } }
            .navigationTitle(conflict.gridName.map { "The \($0)" } ?? conflict.chunkId.map { "The \(ChunkNames.shown($0)) chunk" } ?? "Level \(conflict.number)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Later") { dismiss() } } }
            .background(NightBackdrop(zone: "city"))
        }
    }

    private func side(_ title: String, _ text: String, _ tint: Color) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title.uppercased()).font(.system(size: 11, weight: .heavy, design: .rounded)).foregroundStyle(tint)
            ScrollView { Text(text).font(.system(size: 11, design: .monospaced)).foregroundStyle(.white.opacity(0.85)).frame(maxWidth: .infinity, alignment: .leading) }
                .frame(maxHeight: 320)
        }
        .padding(10)
        .glass(tint, radius: 12)
    }

    private func choice(_ label: String, _ symbol: String, _ tint: Color, _ act: @escaping () async -> Void) -> some View {
        Button { Task { await act(); dismiss() } } label: { Label(label, systemImage: symbol).frame(maxWidth: .infinity) }
            .buttonStyle(.borderedProminent).tint(tint.opacity(0.85)).controlSize(.large)
    }
}

/// one side of a maze changed on both: its grid around where the two differ, those cells outlined, and how many
struct MazeDiffSide: View {
    let title: String
    let rows: [String]
    let other: [String]
    let tint: Color

    private var size: (cols: Int, rows: Int) { (rows.first?.count ?? 0, rows.count) }
    private var sameSize: Bool { rows.count == other.count && rows.first?.count == other.first?.count }
    /// the cells that differ (column, row index), when the sizes match
    private var differ: [(Int, Int)] {
        guard sameSize else { return [] }
        var out: [(Int, Int)] = []
        for (i, (a, b)) in zip(rows, other).enumerated() where a != b {
            for (c, (x, y)) in zip(a, b).enumerated() where x != y { out.append((c, i)) }
        }
        return out
    }

    var body: some View {
        let d = differ
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(title.uppercased()).font(.system(size: 11, weight: .heavy, design: .rounded)).foregroundStyle(tint)
                Spacer()
                Text(sameSize ? "\(d.count) \(d.count == 1 ? "cell differs" : "cells differ")" : "\(size.cols) × \(size.rows)")
                    .font(.system(size: 11, weight: .semibold, design: .rounded)).foregroundStyle(.white.opacity(0.7))
            }
            Canvas { ctx, sz in
                guard !rows.isEmpty else { return }
                // the window: the cells that differ in its middle (or the maze's entry), as many as fit 12 rows high
                let n = min(12, size.rows)
                let z = sz.height / CGFloat(max(n, 1))
                let cols = min(size.cols, Int(sz.width / z))
                let mid = d.isEmpty ? (0, size.rows - n / 2) : ((d.map(\.0).min()! + d.map(\.0).max()!) / 2, (d.map(\.1).min()! + d.map(\.1).max()!) / 2)
                let c0 = max(0, min(size.cols - cols, mid.0 - cols / 2)), i0 = max(0, min(size.rows - n, mid.1 - n / 2))
                let look = ZoneLook.of("egypt")
                ctx.translateBy(x: (sz.width - CGFloat(cols) * z) / 2, y: 0)
                ctx.withCGContext { cg in
                    cg.setFillColor(UIColor(hex: "#0b0716").cgColor)
                    cg.fill(CGRect(x: 0, y: 0, width: CGFloat(cols) * z, height: CGFloat(n) * z))
                    for i in i0..<(i0 + n) {
                        let line = Array(rows[i])
                        for c in c0..<min(c0 + cols, line.count) {
                            let f = CGRect(x: CGFloat(c - c0) * z, y: CGFloat(i - i0) * z, width: z, height: z)
                            MazePaint.cell(cg, line[c], f.insetBy(dx: 0.5, dy: 0.5), look)
                        }
                    }
                    cg.setStrokeColor(UIColor(hex: "#ff2b4a").cgColor)
                    cg.setLineWidth(2)
                    for (c, i) in d where c >= c0 && c < c0 + cols && i >= i0 && i < i0 + n {
                        cg.stroke(CGRect(x: CGFloat(c - c0) * z, y: CGFloat(i - i0) * z, width: z, height: z).insetBy(dx: 1, dy: 1))
                    }
                }
            }
            .frame(height: 112)
        }
        .padding(10)
        .glass(tint, radius: 12)
    }
}
