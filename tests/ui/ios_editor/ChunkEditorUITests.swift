import XCTest

/// the chunk list and the chunk editor: a new chunk, its settings, and what the list says of them
final class ChunkEditorUITests: EditorUITestCase {
    func testEmptyListOffersANewCourseChunkThatOpensItsEditor() {
        launch(["--chunks"])
        wait(text("No chunks yet"))
        wait(el("chunks.newCourse")); wait(el("chunks.newMaze"))

        makeChunk(named: "first try")
        waitForChunkEditor()
        // the pads either side of the hazards, the checker's word, the kind
        wait(el("canvas.band.RUN-IN"))
        // (the run-out is at the far end: the overview strip takes the canvas there)
        el("course.minimap").coordinate(withNormalizedOffset: CGVector(dx: 0.95, dy: 0.5)).tap()
        wait(el("canvas.band.RUN-OUT"))
        wait(text("The checker: all fair"), 30)
        XCTAssertTrue(text("Course chunk").exists)

        // back in the list: the chunk is in the band its first level is in (levels 1-5)
        showSidebar()
        wait(el("chunk.row.first-try"))
        wait(app.staticTexts["Levels 1\u{2013}5"])
    }

    func testSettingsReachTheListRow() {
        launch(["--chunks"])
        makeChunk(named: "tuned")
        waitForChunkEditor()

        // levels 20 to 20
        setField("chunk.range.0.from.field", to: 20)
        // (a from past the to pushes the to up with it)
        XCTAssertEqual(el("chunk.range.0.from").value as? String, "20")
        XCTAssertEqual(el("chunk.range.0.to").value as? String, "20")
        // a second range: the five levels after the last (21-25)
        tap(el("chunk.range.add"))
        wait(el("chunk.range.1.from"))
        XCTAssertEqual(el("chunk.range.1.from").value as? String, "21")
        XCTAssertEqual(el("chunk.range.1.to").value as? String, "25")
        // weight 8
        let w = el("chunk.weight")
        wait(w)
        for p in [0.79, 0.8, 0.82, 0.85, 0.88] where w.value as? String != "8" { w.adjust(toNormalizedSliderPosition: p) }
        XCTAssertEqual(w.value as? String, "8")
        // uses: the stepper up by 2 (from the 1 it has when it gets a limit) 
        tap(el("chunk.noLimit"))   // (it was "No limit": now a limit, 2)
        XCTAssertEqual(el("chunk.uses").value as? String, "2")
        tap(el("chunk.uses.more")); tap(el("chunk.uses.more"))
        XCTAssertEqual(el("chunk.uses").value as? String, "4")
        // Mirror on
        tap(el("chunk.mirror"))
        XCTAssertEqual(el("chunk.mirror").value as? String, "1")

        // the list row says all of it, and the chunk is in the band of its first range (levels 16-20)
        showSidebar()
        let row = el("chunk.row.tuned")
        wait(row)
        let l = row.label
        XCTAssertTrue(l.contains("20") && l.contains("21\u{2013}25") && l.contains("8") && l.contains("4 a level") && l.contains("flips"), "row: \(l)")
        wait(app.staticTexts["Levels 16\u{2013}20"])
    }

    func testNoLimitUsesAndMirrorOff() {
        launch(["--chunks"], chunks: ["hurdles"])
        wait(el("chunk.row.hurdles"))
        let before = el("chunk.row.hurdles").label
        XCTAssertTrue(before.contains("2 a level") && before.contains("flips"), "row: \(before)")
        el("chunk.row.hurdles").tap()
        waitForChunkEditor()
        XCTAssertEqual(el("chunk.noLimit").value as? String, "0")
        tap(el("chunk.noLimit"))
        XCTAssertEqual(el("chunk.noLimit").value as? String, "1")
        tap(el("chunk.mirror"))
        showSidebar()
        wait(el("chunk.row.hurdles"))
        let after = el("chunk.row.hurdles").label
        XCTAssertFalse(after.contains("2 a level") || after.contains("flips"), "row: \(after)")
    }

    func testMazeChunkOpensItsEditorAndChecker() {
        launch(["--chunks"])
        makeChunk(named: "sacred well", maze: true)
        waitForChunkEditor()
        XCTAssertTrue(text("Maze chunk").exists)
        // the maze checker's panel: the way through, the reach, the near misses
        wait(text("The way")); wait(text("Reach")); wait(text("Near misses"))
        wait(text("Ready"), 30)
        showSidebar()
        wait(el("chunk.row.sacred-well"))
    }
}
