// The base of the editor's UI tests: launches CuckooEditor on its own empty data (--ui-test-reset: a fresh folder in the app's
// temporary directory, the game's levels seeded, no chunks), so a test never reads or writes the real levels and chunks.
import XCTest

class EditorUITestCase: XCTestCase {
    var app: XCUIApplication!

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    override func tearDownWithError() throws {
        app?.terminate()
    }

    /// launch with the isolated data folder and any more arguments; `chunks` are fixture names put in it (Fixtures/<name>.txt)
    @discardableResult
    func launch(_ args: [String] = [], chunks: [String] = [], files: URL? = nil) -> XCUIApplication {
        let a = XCUIApplication()
        var all = ["--ui-test-reset"]
        for name in chunks { all += ["--ui-test-chunk", fixture(name)] }
        if let files { all += ["--ui-test-files", files.path] }
        a.launchArguments = all + args
        a.launch()
        app = a
        return a
    }

    /// a fixture chunk's path in the test bundle
    func fixture(_ name: String) -> String {
        let b = Bundle(for: EditorUITestCase.self)
        if let p = b.path(forResource: name, ofType: "txt") { return p }
        if let p = b.path(forResource: name, ofType: "txt", inDirectory: "Resources") { return p }
        XCTFail("fixture \(name).txt isn't in the test bundle")
        return ""
    }

    /// a folder of the test's own, emptied (the backup file goes through it)
    func scratchFolder(_ name: String) throws -> URL {
        let d = FileManager.default.temporaryDirectory.appendingPathComponent("CuckooUITests-\(name)", isDirectory: true)
        try? FileManager.default.removeItem(at: d)
        try FileManager.default.createDirectory(at: d, withIntermediateDirectories: true)
        addTeardownBlock { try? FileManager.default.removeItem(at: d) }
        return d
    }

    // MARK: finding things

    /// anything with this accessibility identifier
    func el(_ id: String) -> XCUIElement { app.descendants(matching: .any).matching(identifier: id).firstMatch }

    /// anything whose identifier starts with this
    func el(prefix: String) -> XCUIElement {
        app.descendants(matching: .any).matching(NSPredicate(format: "identifier BEGINSWITH %@", prefix)).firstMatch
    }

    /// static text containing these words
    func text(_ words: String) -> XCUIElement {
        app.staticTexts.matching(NSPredicate(format: "label CONTAINS[c] %@", words)).firstMatch
    }

    func wait(_ e: XCUIElement, _ seconds: TimeInterval = 10, _ message: String? = nil, file: StaticString = #filePath, line: UInt = #line) {
        if !e.waitForExistence(timeout: seconds) { XCTFail(message ?? "didn't appear: \(e)", file: file, line: line) }
    }

    func gone(_ e: XCUIElement, _ seconds: TimeInterval = 10, file: StaticString = #filePath, line: UInt = #line) {
        let ok = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: e)
        if XCTWaiter().wait(for: [ok], timeout: seconds) != .completed { XCTFail("still there: \(e)", file: file, line: line) }
    }

    /// tap a button that may need a moment to be hittable (a simulator drops the first tap after some transitions)
    func tap(_ e: XCUIElement, _ seconds: TimeInterval = 10, file: StaticString = #filePath, line: UInt = #line) {
        wait(e, seconds, "no \(e) to tap", file: file, line: line)
        // (a panel still sliding in isn't hittable yet)
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate(format: "isHittable == true"), object: e)
        _ = XCTWaiter().wait(for: [ready], timeout: seconds)
        e.tap()
    }

    // MARK: the screens

    /// from the start screen into the chunks (or the levels)
    func openChunks() { tap(el("start.chunks")); wait(el("chunks.list")) }
    func openLevels() { tap(el("start.levels")); wait(el("levels.list")) }

    /// name a new chunk in the alert and make it
    func makeChunk(named name: String, maze: Bool = false) {
        let empty = el("chunks.newCourse").exists
        if empty { tap(maze ? el("chunks.newMaze") : el("chunks.newCourse")) }
        else {
            tap(el("chunks.new"))
            tap(el(maze ? "chunks.menu.newMaze" : "chunks.menu.newCourse"))
        }
        let field = app.alerts.textFields.firstMatch
        wait(field)
        field.tap()
        field.typeText(name)
        app.alerts.buttons["Make it"].tap()
    }

    /// the editor is showing a chunk (the settings card is up)
    func waitForChunkEditor() {
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in self.text("Chunk \u{00B7} ").exists }, object: nil)
        _ = XCTWaiter().wait(for: [ready], timeout: 20)
        showInspector()
        wait(text("Chunk settings"), 20)
    }

    /// the chunk list, brought back over an editor on an iPad (the editor's own sidebar button)
    func showSidebar() {
        if el(prefix: "chunk.row.").exists { return }
        tap(el("editor.sidebar"))
    }

    /// a number stepper's field, set by typing the number over what is there (for a field that holds a short number, as a new chunk's 1: the caret lands after it)
    func setField(_ id: String, to value: Int) {
        let f = el(id)
        tap(f)
        f.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: 5) + String(value))
        XCTAssertEqual(f.value as? String, String(value), "\(id) after typing")
        // (the number pad is a popover with no Return: a tap on the bar's title, which does nothing, puts it away)
        app.navigationBars.firstMatch.staticTexts.firstMatch.tap()
        let away = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: app.keyboards.firstMatch)
        _ = XCTWaiter().wait(for: [away], timeout: 4)
    }

    /// the inspector open beside the canvas (an iPad's button), if it is not
    func showInspector() {
        if el("inspector.rollCourse").exists || text("Chunk settings").exists { return }
        let b = app.buttons["Inspector"]
        if b.waitForExistence(timeout: 5) { b.tap() }
    }

    /// Home, from an editor (the tap can be dropped while a screen is still settling: again, up to three times)
    func goHome() {
        for _ in 0..<3 {
            if el("start.levels").exists { return }
            tap(el("home"))
            if el("start.levels").waitForExistence(timeout: 4) { return }
        }
        wait(el("start.levels"))
    }
}
