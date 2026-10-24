// The rails' rules, apart from their looks (Foundation only: tests/swift builds and checks them on their own):
// - which tools share a button (Build, the registry's groups: Hazards, Pickups; Edit; the maze's Lines), a group of one
//   its tool's own button;
// - which tool a group's button wears and whether it's lit (the tool, or the one it last had; none lit while erasing);
// - what each group last had, kept between launches;
// - the generators' hazard switches: what a roll may lay, as the C API takes it.
import Foundation

enum ToolGrouping {
    struct Group: Equatable {
        let id: String
        let label: String
        let members: [String]
    }

    enum Item: Equatable {
        case tool(String)
        case group(Group)
    }

    /// the course's rail: Pan, Build, the registry's groups in its order (hazards, then pickups), Edit, Gauntlet, Start
    /// (`gauntlet` false: a chunk's, which lays no maze door)
    static func courseRail(hazards: [(id: String, group: String)], gauntlet: Bool = true) -> [Item] {
        func item(_ id: String, _ label: String, _ members: [String]) -> Item? {
            members.isEmpty ? nil : members.count == 1 ? .tool(members[0]) : .group(Group(id: id, label: label, members: members))
        }
        var items: [Item] = [.tool("pan")]
        items += [item("build", "Build", ["wall", "kind", "barrier", "block"])].compactMap { $0 }
        var groups: [String] = []
        for h in hazards where !groups.contains(h.group) { groups.append(h.group) }
        for g in groups { items += [item(g, g.capitalized, hazards.filter { $0.group == g }.map(\.id))].compactMap { $0 } }
        items += [item("edit", "Edit", ["cols", "select"])].compactMap { $0 }
        items += (gauntlet ? [.tool("gauntlet")] : []) + [.tool("start")]
        return items
    }

    /// the maze's Lines: Columns and Rows sharing a button
    static let mazeLines = ["cols", "rows"]

    /// the maze's rail: its tools, Columns and Rows as one (where Columns was)
    static func mazeRail(tools: [String]) -> [Item] {
        tools.compactMap { id in
            if id == mazeLines[0] { return .group(Group(id: "maze.lines", label: "Lines", members: mazeLines)) }
            return mazeLines.contains(id) ? nil : .tool(id)
        }
    }

    /// the group a tool is in, on a rail
    static func group(of tool: String, in rail: [Item]) -> Group? {
        for case .group(let g) in rail where g.members.contains(tool) { return g }
        return nil
    }

    /// a group's button: the tool it wears (the tool, else the one it last had, else its first) and whether it's lit
    /// (its tool picked; while erasing, none lit: a tap picks it, putting the eraser away)
    static func face(members: [String], current: String, erasing: Bool, remembered: String?) -> (shown: String, lit: Bool) {
        let isCurrent = !erasing && members.contains(current)
        let shown = isCurrent ? current : remembered.flatMap { r in members.contains(r) ? r : nil } ?? members.first ?? current
        return (shown, isCurrent)
    }

    /// a tool's own button lit: it's the tool, and the eraser isn't
    static func lit(_ tool: String, current: String, erasing: Bool) -> Bool { !erasing && tool == current }
}

/// which tool each group last had, kept between launches
enum GroupMemory {
    static var store = UserDefaults.standard
    static func get(_ group: String) -> String? { store.string(forKey: "toolGroup." + group) }
    static func set(_ group: String, _ id: String) { store.set(id, forKey: "toolGroup." + group) }
}

/// the generators' switches: a hazard per switch (the registry's `generated` ones for a course; a maze's cells for a
/// maze), those switched off left out of a roll
enum HazardSwitches {
    /// the hazards a course roll may lay: the registry's that the generator places, in its order
    static func course(_ hazards: [(id: String, generated: Bool)]) -> [String] { hazards.filter(\.generated).map(\.id) }
    /// the hazards a maze roll may lay: the ones a maze has cells for, that the registry has
    static func maze(registry ids: [String]) -> [String] { ["saw", "laser", "lava"].filter(ids.contains) }
    /// a roll's: those switched on
    static func rolled(_ ids: [String], off: Set<String>) -> [String] { ids.filter { !off.contains($0) } }
    /// as the C API takes it (ed_regenerate, gd_generate): ids, comma-separated; "none" for none ("" would be all)
    static func argument(_ ids: [String]) -> String { ids.isEmpty ? "none" : ids.joined(separator: ",") }
}
