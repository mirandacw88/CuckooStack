import XCTest

final class StartScreenUITests: EditorUITestCase {
    func testStartScreenOffersLevelsAndChunksAndHomeComesBack() {
        launch()
        wait(el("start.levels")); wait(el("start.chunks"))
        XCTAssertTrue(app.buttons["Edit Levels"].exists)
        XCTAssertTrue(app.buttons["Edit Chunks"].exists)

        openLevels()
        tap(el("home")); wait(el("start.levels"))
        gone(el("levels.list"))

        openChunks()
        tap(el("home")); wait(el("start.chunks"))
        gone(el("chunks.list"))
    }
}
