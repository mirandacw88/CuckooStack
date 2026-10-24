// Sync with the level editor's server on the Mac (tools/level_editor/server.py --lan), on the same Wi-Fi, found by
// Bonjour (_cuckooedit._tcp) and paired once with the code it prints. Edits made anywhere, the Mac or here, away
// from it or not, come together level by level against the text both last agreed on (LevelStore's base):
//   changed here only: pushed (the Mac takes it only if it's still at that base: compare-and-swap)
//   changed there only: pulled
//   changed on both: merged (LevelMerge, the game's own C++); a clean merge is pushed, a conflict asks: keep mine,
//                    take theirs, or keep mine as a new level
// A level made here whose number the Mac already has asks to be renumbered. A maze's grid edited here is pushed the
// same way (changed on both: no merge, a choice: keep mine, take the Mac's, keep mine as a copy); a grid that grew or
// shrank refits the levels that lay it, on the Mac as here. A chunk (ChunkStore, ChunkSyncLogic.swift) goes the same way as
// a maze: changed on both is a choice, and a chunk renamed here is, to the Mac, one taken out and one new.
import Foundation
import Network
import SwiftUI

struct FoundServer: Identifiable, Hashable {
    let name: String
    let host: String
    let port: Int
    var id: String { name }
}

@MainActor
final class SyncEngine: ObservableObject {
    static let shared = SyncEngine(store: .shared, chunks: .shared)

    @Published private(set) var status = "Not paired with a Mac"
    @Published private(set) var syncing = false
    @Published private(set) var conflicts: [SyncConflict] = []
    @Published private(set) var found: [FoundServer] = []
    @Published private(set) var reachable = false
    @Published var lastError: String?

    let store: LevelStore
    let chunks: ChunkStore
    private var browser: NWBrowser?
    private let monitor = NWPathMonitor(requiredInterfaceType: .wifi)

    init(store: LevelStore, chunks: ChunkStore) {
        self.store = store
        self.chunks = chunks
        if let s = store.state.server { status = "Paired with \(s.name)" }
        monitor.pathUpdateHandler = { [weak self] path in
            Task { @MainActor in
                let up = path.status == .satisfied
                if up && !(self?.reachable ?? true) { await self?.sync(quietly: true) } // (back on Wi-Fi: catch up)
                self?.reachable = up
            }
        }
        monitor.start(queue: .main)
    }

    var paired: Bool { store.state.server != nil }

    // MARK: finding the Mac

    func browse() {
        browser?.cancel()
        let b = NWBrowser(for: .bonjour(type: "_cuckooedit._tcp", domain: nil), using: .tcp)
        b.browseResultsChangedHandler = { [weak self] results, _ in
            MainActor.assumeIsolated { // (its queue is the main one)
                for r in results { if case let .service(name, _, _, _) = r.endpoint { self?.resolve(name: name, endpoint: r.endpoint) } }
            }
        }
        b.start(queue: .main)
        browser = b
    }

    func stopBrowsing() { browser?.cancel(); browser = nil }

    private func resolve(name: String, endpoint: NWEndpoint) {
        let params = NWParameters.tcp // (resolved to IPv4: a link-local IPv6 address needs its interface, which a URL can't carry)
        if let ip = params.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options { ip.version = .v4 }
        let c = NWConnection(to: endpoint, using: params)
        c.stateUpdateHandler = { [weak self] state in
            if case .ready = state, let remote = c.currentPath?.remoteEndpoint, case let .hostPort(host, port) = remote {
                var h = "\(host)"
                if let pct = h.firstIndex(of: "%") { h = String(h[..<pct]) } // (an IPv6 link-local's interface)
                let s = FoundServer(name: name, host: h, port: Int(port.rawValue))
                Task { @MainActor in if !(self?.found.contains(s) ?? true) { self?.found.append(s) } }
                c.cancel()
            }
        }
        c.start(queue: .main)
    }

    /// paired with the code the Mac's server printed
    func pair(_ server: FoundServer, code: String) async -> Bool {
        let info = ServerInfo(host: server.host, port: server.port, token: code.trimmingCharacters(in: .whitespaces), name: server.name)
        do {
            _ = try await call(info, "GET", "/api/sync/manifest")
            store.state.server = info
            store.saveState()
            status = "Paired with \(info.name)"
            lastError = nil
            return true
        } catch {
            lastError = "\(error.localizedDescription)"
            return false
        }
    }

    func unpair() { store.state.server = nil; store.saveState(); status = "Not paired with a Mac" }

    // MARK: the protocol

    struct HTTPError: LocalizedError {
        let status: Int
        let message: String
        var errorDescription: String? { status == 401 ? "The Mac didn't take the pairing code" : status == 423 ? message : "The Mac said \(status): \(message)" }
    }

    private func call(_ s: ServerInfo, _ method: String, _ path: String, _ body: [String: Any]? = nil) async throws -> [String: Any] {
        let host = s.host.contains(":") ? "[\(s.host)]" : s.host
        guard let url = URL(string: "http://\(host):\(s.port)\(path)") else { throw HTTPError(status: 0, message: "bad address") }
        var req = URLRequest(url: url, timeoutInterval: path.hasSuffix("/push") ? 180 : 15) // (a push runs the checks; the rest is quick)
        req.httpMethod = method
        req.setValue("Bearer \(s.token)", forHTTPHeaderField: "Authorization")
        if let body {
            req.httpBody = try JSONSerialization.data(withJSONObject: body)
            req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        let (data, resp) = try await URLSession.shared.data(for: req)
        let json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
        let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
        guard code == 200 else { throw HTTPError(status: code, message: json["error"] as? String ?? "") }
        return json
    }

    // MARK: sync

    func sync(quietly: Bool = false) async {
        guard let server = store.state.server, !syncing else { return }
        syncing = true
        status = "Syncing with \(server.name)…"
        defer { syncing = false }
        do {
            let held = try await renames(server)
            let manifest = try await call(server, "GET", "/api/sync/manifest")
            let theirLevels = (manifest["levels"] as? [String: String]) ?? [:]
            let theirGrids = ((manifest["grids"] as? [String: String]) ?? [:]).filter { !held.contains($0.key) }
            var newConflicts: [SyncConflict] = []
            let grids = try await syncGrids(server, theirGrids, &newConflicts)
            // (a Mac that has no chunks key is an older server: the chunks wait, never mistaken for all gone from it)
            let theirChunks = manifest["chunks"] as? [String: String]
            var chunkPush = ChunkPush()
            if let theirChunks { chunkPush = try await syncChunks(server, theirChunks, &newConflicts) }
            var push: [String: Any] = [:], deletes: [String: String] = [:], pull: [Int] = [], merge: [Int] = []
            var atStart: [Int: String?] = [:] // (each level's file as sync read it: never overwritten if it's changed since)
            let numbers = Set(store.levels.map(\.number)).union(theirLevels.keys.compactMap(Int.init)).union(store.state.levels.keys.compactMap(Int.init))
            for n in numbers.sorted() {
                let key = String(n)
                let entry = store.state.levels[key]
                let base = entry?.base ?? ""
                let mine = store.text(of: n)
                atStart[n] = mine
                let theirs = theirLevels[key]
                let mineChanged = entry?.deleted == true || (mine.map(textHash) ?? "") != base
                let theirsChanged = (theirs ?? "") != base
                switch (mineChanged, theirsChanged) {
                case (false, false): continue
                case (false, true): if theirs == nil { store.forget(n) } else { pull.append(n) }
                case (true, false):
                    if entry?.deleted == true { deletes[key] = base } else if let mine { push[key] = ["base": base, "text": mine] }
                case (true, true):
                    if base.isEmpty, theirs != nil, let mine { newConflicts.append(SyncConflict(number: n, kind: .number, mine: mine, theirs: "", theirsHash: theirs!)) }
                    else if entry?.deleted == true || theirs == nil { if let mine { push[key] = ["base": base, "text": mine] } else { store.forget(n) } } // (taken out on one side, changed on the other: the change wins)
                    else { merge.append(n) }
                }
            }
            // theirs, for pulls and merges
            let wanted = pull + merge
            var texts: [String: String] = [:]
            if !wanted.isEmpty {
                let got = try await call(server, "POST", "/api/sync/pull", ["levels": wanted])
                texts = (got["levels"] as? [String: String]) ?? [:]
            }
            for n in pull { if let t = texts[String(n)] { store.agreed(n, text: t, ifStill: atStart[n] ?? nil) } }
            for n in merge {
                guard let t = texts[String(n)], let mine = store.text(of: n) else { continue }
                let r = Builtin.merge(n, base: store.baseText(of: n) ?? "", mine: mine, theirs: t)
                if r.ok { push[String(n)] = ["base": textHash(t), "text": r.text] }
                else { newConflicts.append(SyncConflict(number: n, kind: .both(r.conflicts), mine: mine, theirs: t, theirsHash: textHash(t))) }
            }
            if !push.isEmpty || !deletes.isEmpty || !grids.push.isEmpty || !chunkPush.isEmpty {
                let res = try await call(server, "POST", "/api/sync/push", ["levels": push, "deleted": deletes, "grids": grids.push, "chunks": chunkPush.push, "deletedChunks": chunkPush.deletes])
                chunks.pushed(chunkPush, applied: (res["chunksApplied"] as? [String]) ?? [])
                for (id, v) in (res["chunkConflicts"] as? [String: [String: String]]) ?? [:] { // (the Mac moved on meanwhile; one it no longer has: next time)
                    if let mine = chunks.text(of: id), let t = v["text"], !t.isEmpty {
                        newConflicts.append(SyncConflict(number: 0, kind: .chunk(id), mine: mine, theirs: t, theirsHash: textHash(t)))
                    }
                }
                for name in (res["gridsApplied"] as? [String]) ?? [] {
                    if let p = grids.push[name] as? [String: String], let t = p["text"] { store.agreedGrid(name, text: t, ifStill: grids.atStart[name] ?? nil) }
                }
                for (name, v) in (res["gridConflicts"] as? [String: [String: String]]) ?? [:] {
                    if let mine = store.gridText(name), let t = v["text"] {
                        newConflicts.append(SyncConflict(number: 0, kind: .grid(name), mine: mine, theirs: t, theirsHash: textHash(t)))
                    }
                }
                for (k, t) in (res["refit"] as? [String: String]) ?? [:] { // (a level the Mac refitted to a grid pushed: here already, the same)
                    if let n = Int(k), store.text(of: n) == t { store.agreed(n, text: t) }
                }
                for k in (res["applied"] as? [String]) ?? [] {
                    guard let n = Int(k) else { continue }
                    if deletes[k] != nil { store.forget(n) } else if let p = push[k] as? [String: String], let t = p["text"] { store.agreed(n, text: t, ifStill: atStart[n] ?? nil) }
                }
                for (k, v) in (res["conflicts"] as? [String: [String: String]]) ?? [:] { // (the Mac moved on meanwhile: once more, next time)
                    if let n = Int(k), let mine = store.text(of: n), let t = v["text"] {
                        newConflicts.append(SyncConflict(number: n, kind: .both(["changed on the Mac meanwhile"]), mine: mine, theirs: t, theirsHash: textHash(t)))
                    }
                }
                if let rejected = res["rejected"] as? [String: String], !rejected.isEmpty {
                    lastError = rejected.map { SyncConflict.rejection($0.key, $0.value) }.sorted().joined(separator: "\n")
                }
            }
            store.state.lastSync = Date()
            store.saveState()
            store.reload()
            withAnimation(.spring(response: 0.4, dampingFraction: 0.8)) { conflicts = newConflicts }
            status = newConflicts.isEmpty ? "Synced with \(server.name)" : "\(newConflicts.count) to settle"
            if !quietly { newConflicts.isEmpty ? Haptic.success() : Haptic.warning() }
        } catch {
            status = quietly ? "Not synced (the Mac isn't answering)" : "Not synced"
            if !quietly { lastError = error.localizedDescription }
        }
    }

    /// the mazes renamed here, renamed on the Mac first (its grid file and the levels that lay it): then the rest of
    /// the sync finds them agreed. One the Mac turns away stays to tell next time, its old name held back from the pull
    /// (it would come back); what's held back
    private func renames(_ server: ServerInfo) async throws -> Set<String> {
        var held = Set<String>()
        for r in store.state.renames ?? [] {
            do {
                let res = try await call(server, "POST", "/api/sync/rename", ["from": r.from, "to": r.to])
                let levels = ((res["levels"] as? [String: String]) ?? [:]).reduce(into: [Int: String]()) { if let n = Int($1.key) { $0[n] = $1.value } }
                store.renameSynced(r, levels: levels)
            } catch let e as HTTPError where e.status == 409 {
                held.insert(r.from)
                lastError = "The \(Builtin.gauntletShown(r.from)) wasn't renamed on the Mac: \(e.message)"
            }
        }
        return held
    }

    /// the grids: changed on the Mac only, pulled; changed here only, to push (with the levels); changed on both, a
    /// conflict. What to push, and each grid's file as sync read it (never overwritten if it's changed since)
    private func syncGrids(_ server: ServerInfo, _ theirs: [String: String], _ conflicts: inout [SyncConflict]) async throws
        -> (push: [String: Any], atStart: [String: String?]) {
        var push: [String: Any] = [:], atStart: [String: String?] = [:], pull: [String] = [], both: [String] = []
        for name in Set(theirs.keys).union(store.grids.keys).sorted() {
            let base = store.state.grids[name]?.base ?? ""
            let mine = store.gridText(name)
            atStart[name] = mine
            let mineChanged = (mine.map(textHash) ?? base) != base, theirsChanged = (theirs[name] ?? base) != base
            switch (mineChanged, theirsChanged) {
            case (false, true): pull.append(name)
            case (true, false): if let mine { push[name] = ["base": base, "text": mine] }
            case (true, true): if theirs[name] == nil, let mine { push[name] = ["base": "", "text": mine] } else { both.append(name) }
            case (false, false): // (renamed or taken out on the Mac; never one renamed here the Mac hasn't been told of)
                if theirs[name] == nil, !base.isEmpty, mine != nil, !(store.state.renames ?? []).contains(where: { $0.to == name }) { store.forgetGrid(name) }
            default: continue
            }
        }
        let wanted = pull + both
        guard !wanted.isEmpty else { return (push, atStart) }
        let got = try await call(server, "POST", "/api/sync/pull", ["grids": wanted])
        let texts = ((got["grids"] as? [String: [String: Any]]) ?? [:]).compactMapValues { $0["text"] as? String }
        for name in pull { if let t = texts[name] { store.agreedGrid(name, text: t, ifStill: atStart[name] ?? nil) } } // (its file as the Mac has it: the same hash)
        for name in both {
            guard let t = texts[name], let mine = store.gridText(name) else { continue }
            if t == mine { store.agreedGrid(name, text: t) } // (the same edit both sides)
            else { conflicts.append(SyncConflict(number: 0, kind: .grid(name), mine: mine, theirs: t, theirsHash: textHash(t))) }
        }
        return (push, atStart)
    }

    /// the chunks: changed on the Mac only, pulled; here only, to push; taken out here, told; changed on both, a conflict (the same
    /// text either way agrees)
    private func syncChunks(_ server: ServerInfo, _ theirs: [String: String], _ conflicts: inout [SyncConflict]) async throws -> ChunkPush {
        let plan = ChunkSyncPlan.make(mine: chunks.hashes(), entries: chunks.syncEntries, theirs: theirs)
        var texts: [String: String] = [:]
        if !plan.wanted.isEmpty {
            let got = try await call(server, "POST", "/api/sync/pull", ["chunks": plan.wanted])
            texts = (got["chunks"] as? [String: String]) ?? [:]
        }
        let step = chunks.syncStep(plan, theirs: theirs, texts: texts)
        for c in step.clashes { conflicts.append(SyncConflict(number: 0, kind: .chunk(c.id), mine: c.mine, theirs: c.theirs, theirsHash: textHash(c.theirs))) }
        return step.push
    }

    // MARK: settling a conflict

    /// a maze changed on both sides: mine pushed over the Mac's, the Mac's taken, or mine kept as a copy (a new grid,
    /// in no level yet) and the Mac's taken
    private func settleGrid(_ c: SyncConflict, _ name: String, _ choice: Choice) async {
        switch choice {
        case .takeTheirs: store.agreedGrid(name, text: c.theirs)
        case .keepMine:
            guard let server = store.state.server else { return }
            if let res = try? await call(server, "POST", "/api/sync/push", ["levels": [:], "deleted": [:], "grids": [name: ["base": c.theirsHash, "text": c.mine]]]),
               ((res["gridsApplied"] as? [String]) ?? []).contains(name) {
                store.agreedGrid(name, text: c.mine)
                for (k, t) in (res["refit"] as? [String: String]) ?? [:] { if let n = Int(k) { store.agreed(n, text: t) } }
            }
        case .mineAsNew:
            var copy = name + "_copy", k = 2
            while store.grids[copy] != nil { copy = "\(name)_copy\(k)"; k += 1 }
            store.saveGrid(copy, text: c.mine)
            store.agreedGrid(name, text: c.theirs)
        }
        conflicts.removeAll { $0.id == c.id }
        store.saveState()
        store.reload()
        if conflicts.isEmpty { await sync(quietly: true) }
    }

    /// a chunk changed on both sides: mine pushed over the Mac's, the Mac's taken, or mine kept as a copy (a new chunk) and the Mac's taken
    private func settleChunk(_ c: SyncConflict, _ id: String, _ choice: Choice) async {
        switch choice {
        case .takeTheirs: chunks.agreed(id, text: c.theirs)
        case .keepMine:
            guard let server = store.state.server else { return }
            if let res = try? await call(server, "POST", "/api/sync/push", ["levels": [:], "deleted": [:], "chunks": [id: ["base": c.theirsHash, "text": c.mine]]]),
               ((res["chunksApplied"] as? [String]) ?? []).contains(id) { chunks.agreed(id, text: c.mine) }
        case .mineAsNew:
            chunks.keepMineAsCopy(id, mine: c.mine, theirs: c.theirs)
        }
        conflicts.removeAll { $0.id == c.id }
        store.saveState()
        if conflicts.isEmpty { await sync(quietly: true) }
    }

    enum Choice { case keepMine, takeTheirs, mineAsNew }

    func settle(_ c: SyncConflict, _ choice: Choice) async {
        if let name = c.gridName { await settleGrid(c, name, choice); return }
        if let id = c.chunkId { await settleChunk(c, id, choice); return }
        switch choice {
        case .takeTheirs:
            if case .number = c.kind {
                // (theirs is the Mac's level of that number: take it, mine goes)
                store.forget(c.number)
            } else { store.agreed(c.number, text: c.theirs) }
        case .keepMine:
            // mine over theirs: push it against their text's hash
            guard let server = store.state.server else { return }
            if case .number = c.kind { return }
            if let res = try? await call(server, "POST", "/api/sync/push", ["levels": [String(c.number): ["base": c.theirsHash, "text": c.mine]], "deleted": [:]]),
               ((res["applied"] as? [String]) ?? []).contains(String(c.number)) { store.agreed(c.number, text: c.mine) }
        case .mineAsNew:
            let n = (Set(store.levels.map(\.number)).union(store.state.levels.keys.compactMap(Int.init)).max() ?? 0) + 1
            store.save(n, text: c.mine)
            if case .number = c.kind { store.forget(c.number) } else { store.agreed(c.number, text: c.theirs) }
        }
        conflicts.removeAll { $0.id == c.id }
        store.saveState()
        store.reload()
        if conflicts.isEmpty { await sync(quietly: true) }
    }
}
