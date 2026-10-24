import XCTest

/// Back up chunks… and Restore chunks…, through --ui-test-files (the app writes and reads a file in that folder instead of
/// showing the Files sheets, which a test cannot drive); the summary sheet and the undo banner are the real ones
final class BackupUITests: EditorUITestCase {
    func testBackUpDeleteRestoreAndUndo() throws {
        let folder = try scratchFolder("backup")
        launch(["--chunks"], chunks: ["hurdles"], files: folder)
        wait(el("chunk.row.hurdles"))

        // back up: the file is written, the app says so
        tap(el("chunks.backupMenu")); tap(el("chunks.backup"))
        wait(app.alerts.firstMatch)
        XCTAssertTrue(app.alerts.staticTexts["Backed up 1 chunk."].exists)
        app.alerts.buttons["OK"].tap()
        let file = folder.appendingPathComponent("chunks-backup.cuckoochunks")
        let data = try Data(contentsOf: file)
        XCTAssertTrue(String(decoding: data, as: UTF8.self).contains("hurdles"), "the backup file names the chunk")

        // take the chunk out
        el("chunk.row.hurdles").press(forDuration: 1.2)
        tap(app.buttons["Take it out"])
        tap(app.buttons["Take it out"].exists ? app.buttons["Take it out"] : app.sheets.buttons["Take it out"])
        gone(el("chunk.row.hurdles"))
        wait(text("No chunks yet"))

        // restore: the plan says one new chunk; restoring it brings the row back, with the banner
        tap(el("chunks.backupMenu")); tap(el("chunks.restore"))
        wait(el("restore.summary"))
        XCTAssertTrue(el("restore.summary").label.contains("1"), "summary: \(el("restore.summary").label)")
        tap(el("restore.confirm"))
        wait(el("chunk.row.hurdles"))
        wait(el("restore.banner"))

        // undo the restore: the chunk goes again
        tap(el("restore.undo"))
        gone(el("chunk.row.hurdles"))
    }
}
