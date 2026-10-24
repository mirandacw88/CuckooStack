import XCTest

/// three small bugs found on screen, each covered
final class FixesUITests: EditorUITestCase {
    /// "New course chunk" and "New maze chunk" wrapped onto three lines in the narrow sidebar
    func testNewChunkButtonsFitOnOneLine() {
        launch(["--chunks"])
        let course = el("chunks.newCourse"), maze = el("chunks.newMaze")
        wait(course); wait(maze)
        XCTAssertLessThan(course.frame.height, 56, "the course button wraps: \(course.frame)")
        XCTAssertLessThan(maze.frame.height, 56, "the maze button wraps: \(maze.frame)")
        // side by side, inside the list
        XCTAssertEqual(course.frame.midY, maze.frame.midY, accuracy: 4)
        XCTAssertLessThan(course.frame.maxX, maze.frame.minX + 1)
        XCTAssertLessThanOrEqual(maze.frame.maxX, el("chunks.list").frame.maxX)
    }

    /// --detail opens a level with the list out of the way
    func testDetailLaunchHidesTheLevelList() {
        launch(["--open", "20", "--detail"])
        wait(el("course.canvas"), 20)
        wait(el("editor.sidebar"))
        XCTAssertFalse(el("level.row.1").exists && el("level.row.1").isHittable, "the level list is showing beside the editor")
        // the editor's own button brings it back
        el("editor.sidebar").tap()
        wait(el("level.row.1"))
    }

    func testDetailLaunchHidesTheChunkList() {
        launch(["--chunks", "hurdles", "--detail"], chunks: ["hurdles"])
        wait(el("course.canvas"), 20)
        wait(el("editor.sidebar"))
        XCTAssertFalse(el("chunk.row.hurdles").exists && el("chunk.row.hurdles").isHittable, "the chunk list is showing beside the editor")
        el("editor.sidebar").tap()
        wait(el("chunk.row.hurdles"))
    }

    /// a tap on the overview strip jumps the canvas there
    func testTappingTheOverviewJumpsTheCanvas() {
        launch(["--open", "20", "--detail"])
        let map = el("course.minimap")
        wait(map, 20)
        let (from0, to0, len) = view(map)
        XCTAssertGreaterThan(len, to0 - from0, "the whole course should be longer than the canvas shows")
        for f in [0.85, 0.5, 0.15] {
            map.coordinate(withNormalizedOffset: CGVector(dx: f, dy: 0.5)).tap()
            let target = f * len
            let ok = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
                let (a, b, _) = self.view(map)
                return abs((a + b) / 2 - target) < len * 0.06
            }, object: nil)
            XCTAssertEqual(XCTWaiter().wait(for: [ok], timeout: 5), .completed, "the canvas didn't go to \(Int(f * 100))%: \(self.view(map)) from \(from0)-\(to0)")
        }
    }

    /// a drag along the strip carries the canvas with it
    func testDraggingAlongTheOverviewCarriesTheCanvas() {
        launch(["--open", "20", "--detail"])
        let map = el("course.minimap")
        wait(map, 20)
        let (_, _, len) = view(map)
        map.coordinate(withNormalizedOffset: CGVector(dx: 0.2, dy: 0.5))
            .press(forDuration: 0.1, thenDragTo: map.coordinate(withNormalizedOffset: CGVector(dx: 0.7, dy: 0.5)))
        let (a, b, _) = view(map)
        XCTAssertEqual((a + b) / 2, 0.7 * len, accuracy: len * 0.08, "the canvas should follow the drag to 70%: \(a)-\(b) of \(len)")
    }

    /// the minimap's value: "view 12.0 to 30.0 of 240.0 m"
    private func view(_ map: XCUIElement) -> (Double, Double, Double) {
        let n = (map.value as? String ?? "").split(whereSeparator: { !"0123456789.-".contains($0) }).compactMap { Double($0) }
        return n.count == 3 ? (n[0], n[1], n[2]) : (0, 0, 0)
    }
}
