// What a sync does with each chunk (SyncEngine.syncChunks), decided from three things alone: the chunk files here (a hash of
// each), what sync last agreed on for each (the base: SyncState.chunks), and the Mac's (the manifest's hashes). No chunk is
// merged, as no maze is: one changed on both sides is a choice (keep mine, take the Mac's, keep mine as a copy). Foundation
// only: the sync's tests build it on its own.
import Foundation

struct ChunkSyncPlan: Equatable {
    /// changed on the Mac only: take its file
    var pull: [String] = []
    /// changed here only (or new here, or gone from the Mac and changed here): send it, against the Mac's hash ("" if it has none)
    var push: [String] = []
    /// taken out here and unchanged on the Mac: tell it
    var deletes: [String] = []
    /// changed on both: fetch the Mac's file; the same text agrees, a different one is a choice
    var both: [String] = []
    /// gone from the Mac and unchanged here (or gone on both): drop it here too
    var forget: [String] = []

    var isEmpty: Bool { pull.isEmpty && push.isEmpty && deletes.isEmpty && both.isEmpty && forget.isEmpty }
    /// the ones whose Mac file sync needs to read
    var wanted: [String] { pull + both }

    /// `mine`: each chunk file here, id to the hash of its text; `entries`: what was agreed (and the ones taken out here);
    /// `theirs`: the Mac's manifest, id to hash
    static func make(mine: [String: String], entries: [String: SyncState.Entry], theirs: [String: String]) -> ChunkSyncPlan {
        var plan = ChunkSyncPlan()
        for id in Set(mine.keys).union(entries.keys).union(theirs.keys).sorted() {
            let base = entries[id]?.base ?? ""
            let deleted = entries[id]?.deleted == true
            let here = mine[id], there = theirs[id]
            // (a file that has gone without being taken out here is not a change: it comes back when the Mac's changes)
            let mineChanged = deleted || (here != nil && here! != base)
            let theirsChanged = (there ?? "") != base
            switch (mineChanged, theirsChanged) {
            case (false, false): continue
            case (false, true):
                if there == nil { plan.forget.append(id) } else { plan.pull.append(id) }
            case (true, false):
                if deleted { plan.deletes.append(id) } else { plan.push.append(id) }
            case (true, true):
                if deleted { if there == nil { plan.forget.append(id) } else { plan.pull.append(id) } } // (changed there, taken out here: the change wins)
                else if there == nil { plan.push.append(id) } // (gone from the Mac, changed here: the change wins)
                else { plan.both.append(id) }
            }
        }
        return plan
    }
}

/// what the chunks add to a push: the chunks to send (id: {base, text}), the ones taken out here (id: base), and each file as sync
/// read it (never overwritten if it's changed since)
struct ChunkPush {
    var push: [String: Any] = [:]
    var deletes: [String: String] = [:]
    var atStart: [String: String?] = [:]
    var isEmpty: Bool { push.isEmpty && deletes.isEmpty }
}

/// a chunk changed on both sides, to a choice
struct ChunkClash: Equatable {
    let id: String
    let mine: String
    let theirs: String
}

@MainActor
extension ChunkStore {
    /// the part of a chunk sync that needs no network once the Mac's files are in (`texts`: the ones `plan.wanted` asked for): the
    /// Mac's pulled in, the ones it dropped dropped, the same edit on both sides agreed; what's left to push, and the clashes
    func syncStep(_ plan: ChunkSyncPlan, theirs: [String: String], texts: [String: String]) -> (push: ChunkPush, clashes: [ChunkClash]) {
        var out = ChunkPush()
        var clashes: [ChunkClash] = []
        for id in Set(plan.wanted + plan.push) { out.atStart[id] = text(of: id) }
        for id in plan.forget { forget(id) }
        for id in plan.pull { if let t = texts[id] { agreed(id, text: t, ifStill: out.atStart[id] ?? nil) } }
        for id in plan.both {
            guard let t = texts[id], let mine = text(of: id) else { continue }
            if t == mine { agreed(id, text: t) } // (the same edit both sides)
            else { clashes.append(ChunkClash(id: id, mine: mine, theirs: t)) }
        }
        for id in plan.push { if let mine = text(of: id) { out.push[id] = ["base": theirs[id] ?? "", "text": mine] } }
        for id in plan.deletes { out.deletes[id] = syncEntries[id]?.base ?? "" }
        return (out, clashes)
    }

    /// the Mac took these (`applied`: the ids it wrote or took out): what was sent is agreed, what was taken out is gone here too
    func pushed(_ sent: ChunkPush, applied: [String]) {
        for id in applied {
            if sent.deletes[id] != nil { forget(id) }
            else if let p = sent.push[id] as? [String: String], let t = p["text"] { agreed(id, text: t, ifStill: sent.atStart[id] ?? nil) }
        }
    }

    /// "keep mine as a copy, take the Mac's": mine saved as a new chunk, the Mac's text agreed under the name; the copy's id
    @discardableResult func keepMineAsCopy(_ id: String, mine: String, theirs: String) -> String {
        let copy = ChunkNames.copy(of: id, taken: ids)
        save(copy, text: mine)
        agreed(id, text: theirs)
        return copy
    }
}
