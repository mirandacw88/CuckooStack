import XCTest

/// rolling a course or a maze with a chunk in range
final class RollUITests: EditorUITestCase {
    /// roll until a chunk named `name` shows on the canvas (a roll may leave a chunk out), at most `tries` times
    private func rollUntilPlaced(_ name: String, button: String, tries: Int = 4) -> Bool {
        for _ in 0..<tries {
            tap(el(button), 20)
            if sweepForBand(name) { return true }
        }
        return false
    }

    /// the canvas only offers the bands in view: look along the course with the overview strip until `name`'s outline is there
    private func sweepForBand(_ name: String) -> Bool {
        let busy = el("inspector.rollCourse")
        _ = busy.waitForExistence(timeout: 30)
        sleep(6)   // (a roll is made off the main thread, and the checker and the bot judge it after)
        for f in stride(from: 0.04, through: 0.96, by: 0.06) {
            el("course.minimap").coordinate(withNormalizedOffset: CGVector(dx: f, dy: 0.5)).tap()
            if el(prefix: "canvas.band.\(name)").waitForExistence(timeout: 1.5) { return true }
        }
        return false
    }

    /// the history window (a toolbar menu's own button doesn't open it from a test: its shortcut does)
    private func openHistory() {
        app.typeKey("h", modifierFlags: [.command, .shift])
    }

    func testRollOnLevel20PlacesTheChunk() {
        launch(["--open", "20"], chunks: ["hurdles"])
        wait(el("course.canvas"), 20)
        showInspector()
        let density = el("inspector.density")
        wait(density)
        density.adjust(toNormalizedSliderPosition: 1.0)   // (a denser roll has more places for a set piece)
        XCTAssertTrue(rollUntilPlaced("hurdles", button: "inspector.rollCourse"), "no roll placed hurdles")
        // the history names it
        openHistory()
        wait(text("chunks: hurdles"), 15)
    }

    func testRollOnLevel21PlacesNoChunk() {
        launch(["--open", "21"], chunks: ["hurdles"])
        wait(el("course.canvas"), 20)
        showInspector()
        tap(el("inspector.rollCourse"), 20)
        wait(text("A new course"), 40)
        XCTAssertFalse(el(prefix: "canvas.band.").exists)
        openHistory()
        wait(text("A new course"), 15)
        XCTAssertFalse(text("hurdles").exists)
    }

    func testGenerateAMazeWithAMazeChunkInRange() {
        launch(["--chunks"])
        makeChunk(named: "sacred well", maze: true)
        waitForChunkEditor()
        setField("chunk.range.0.from.field", to: 12)
        goHome()
        openLevels()
        tap(el("level.row.12"))
        wait(el("course.canvas"), 20)
        showInspector()
        var placed = false
        for _ in 0..<4 {
            tap(el("inspector.generateMaze"), 20)
            if text("sacred-well").waitForExistence(timeout: 90) { placed = true; break }
            if el("inspector.generateMaze").waitForExistence(timeout: 90) {}
        }
        XCTAssertTrue(placed, "no maze roll placed the maze chunk")
    }
}
