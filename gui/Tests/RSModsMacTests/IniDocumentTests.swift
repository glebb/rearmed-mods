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

    // A section that is a list ([Custom Tunings]): read in order, written all at once in its place
    func testAListSection() {
        let list = "[A]\nk = 1\n\n[Custom Tunings]\n; how to write one\nB Standard = -5 -5 -5 -5 -5 -5\n"
            + "C# Drop B = -5 -3 -3 -3 -3 -3\n\n[Z]\nz = 2\n"
        var d = IniDocument(text: list)
        let e = d.entries("custom tunings")
        XCTAssertEqual(e.map(\.key), ["B Standard", "C# Drop B"])
        XCTAssertEqual(e.first?.value, "-5 -5 -5 -5 -5 -5")
        d.setEntries("Custom Tunings", [("Open C", "-4 -2 -2 0 1 0"), ("B Standard", "-5 -5 -5 -5 -5 -5")])
        XCTAssertEqual(d.text, "[A]\nk = 1\n\n[Custom Tunings]\n; how to write one\nOpen C = -4 -2 -2 0 1 0\n"
            + "B Standard = -5 -5 -5 -5 -5 -5\n\n[Z]\nz = 2\n")
        d.setEntries("Custom Tunings", [])
        XCTAssertEqual(d.text, "[A]\nk = 1\n\n[Custom Tunings]\n; how to write one\n\n[Z]\nz = 2\n")
        d.setEntries("Custom Tunings", [("F Standard", "1 1 1 1 1 1")])  // after the section's comment
        XCTAssertEqual(d.entries("Custom Tunings").map(\.key), ["F Standard"])
        XCTAssertLessThan(d.lines.firstIndex(of: "; how to write one")!, d.lines.firstIndex(of: "F Standard = 1 1 1 1 1 1")!)
        var n = IniDocument(text: "[A]\nk = 1\n")
        n.setEntries("Custom Tunings", [("X", "0 0 0 0 0 0")])
        XCTAssertEqual(n.text, "[A]\nk = 1\n\n[Custom Tunings]\nX = 0 0 0 0 0 0\n")
    }
}
