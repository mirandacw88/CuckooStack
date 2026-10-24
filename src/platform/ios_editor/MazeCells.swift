// A maze's cells as the editors name, key and group them (grid.js's CELLS; the game's: Gauntlet.h). Foundation only:
// tests/swift builds and checks them on their own.
import Foundation

/// a maze cell's kind, as the editors name and key them (grid.js's CELLS)
struct MazeCell: Identifiable, Hashable {
    let ch: Character
    let name: String
    let key: String
    let tip: String
    let family: String // its group in the rail (MazeCellFamily)
    var id: Character { ch }
    var isDoor: Bool { ch == ">" || ch == "X" }
    static let all: [MazeCell] = [
        MazeCell(ch: ".", name: "Air", key: "1", tip: "Carve rooms: the hen runs through air", family: "terrain"),
        MazeCell(ch: "#", name: "Stone", key: "2", tip: "Floors, roofs and walls: at her body it crashes her", family: "terrain"),
        MazeCell(ch: "H", name: "Cyber cube", key: "3", tip: "Stone in the cubes' look", family: "terrain"),
        MazeCell(ch: "B", name: "Bumper", key: "4", tip: "Turns her back; stacked over, she climbs it", family: "bouncers"),
        MazeCell(ch: "R", name: "Gate →", key: "5", tip: "A one-way gate she passes running right (a bumper running left); never a floor", family: "bouncers"),
        MazeCell(ch: "L", name: "Gate ←", key: "6", tip: "A one-way gate she passes running left", family: "bouncers"),
        MazeCell(ch: "^", name: "Spring", key: "\\", tip: "A pad she is launched off as she runs over it: up 5 rows and on at 8 m/s, no eggs laid in the air; she lands on the first floor or ledge under her", family: "bouncers"),
        MazeCell(ch: "S", name: "Saw", key: "7", tip: "A blade in stone: each crossing takes an egg, or her", family: "hazards"),
        MazeCell(ch: "C", name: "Cube saw", key: "8", tip: "A blade in a cyber cube", family: "hazards"),
        MazeCell(ch: "(", name: "Laser, firing left", key: "[", tip: "Stone with a wall laser in its left side: it charges as she comes near, then fires along the row; an egg in it is cut out, her body in it is cut in half", family: "hazards"),
        MazeCell(ch: ")", name: "Laser, firing right", key: "]", tip: "Stone with a wall laser in its right side, firing right along the row", family: "hazards"),
        MazeCell(ch: "~", name: "Lava", key: "=", tip: "A floor that burns the egg under her each cell she crosses; with none, her", family: "hazards"),
        MazeCell(ch: ">", name: "Entry", key: "9", tip: "The door in, on the left edge: tap a row to move it", family: "doors"),
        MazeCell(ch: "X", name: "Exit", key: "0", tip: "The door out, on the right edge: tap a row to move it", family: "doors"),
    ]
    static func named(_ ch: Character) -> MazeCell? { all.first { $0.ch == ch } }
}

/// the cells' groups in the maze's rail: one button each, wearing the cell last painted with, its flyout the rest
struct MazeCellFamily: Identifiable, Hashable {
    let id: String
    let label: String
    static let all: [MazeCellFamily] = [
        MazeCellFamily(id: "terrain", label: "Terrain"),
        MazeCellFamily(id: "bouncers", label: "Bouncers"),
        MazeCellFamily(id: "hazards", label: "Hazards"),
        MazeCellFamily(id: "doors", label: "Doors"),
    ]
    var cells: [MazeCell] { MazeCell.all.filter { $0.family == id } }
}
