import XCTest
@testable import RSModsMac

final class IniDocumentTests: XCTestCase {
    private let text = "; a comment\n[Toggle Switches]\nToggleLoft = on\n\n[Keybinds]\nRewindKey = Z\n"

    func testARoundTripChangesNothing() {
        XCTAssertEqual(IniDocument(text: text).text, text)
    }

    func testReadsIgnoreCaseAndTheLastWins() {
        let d = IniDocument(text: "[A]\nkey = 1\nKEY = 2\n")
        XCTAssertEqual(d.value("a", "Key"), "2")
        XCTAssertNil(d.value("a", "other"))
    }

    func testSetChangesTheLineInPlace() {
        var d = IniDocument(text: text)
        d.set("Toggle Switches", "ToggleLoft", "off")
        XCTAssertEqual(d.text, text.replacingOccurrences(of: "ToggleLoft = on", with: "ToggleLoft = off"))
    }

    func testSetChangesEveryOccurrence() {
        var d = IniDocument(text: "[A]\nk = 1\nk = 2\n")
        d.set("A", "k", "3")
        XCTAssertEqual(d.value("A", "k"), "3")
        XCTAssertFalse(d.text.contains("= 1"))
    }

    func testANewKeyGoesInItsSection() {
        var d = IniDocument(text: text)
        d.set("Toggle Switches", "Sunlight", "on")
        XCTAssertLessThan(d.lines.firstIndex(of: "Sunlight = on")!, d.lines.firstIndex(of: "[Keybinds]")!)
    }

    func testANewSectionGoesAtTheEnd() {
        var d = IniDocument(text: text)
        d.set("Highway Colors", "CustomHighwayColors", "on")
        XCTAssertTrue(d.text.hasSuffix("[Highway Colors]\nCustomHighwayColors = on\n"))
        XCTAssertTrue(d.text.contains("; a comment"))
    }

    func testAnEmptyFile() {
        var d = IniDocument(text: "")
        d.set("A", "k", "1")
        XCTAssertEqual(d.text, "[A]\nk = 1\n")
    }
}
