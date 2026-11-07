import SwiftUI
import XCTest
@testable import RSModsMac

@MainActor
final class SettingsStoreTests: XCTestCase {
    private var folder: URL!
    private var ini: URL { folder.appendingPathComponent("RSMods.ini") }

    override func setUp() async throws {
        folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    }

    override func tearDown() async throws { try? FileManager.default.removeItem(at: folder) }

    private func store(_ text: String = "") throws -> SettingsStore {
        try text.write(to: ini, atomically: true, encoding: .utf8)
        return SettingsStore(gameFolder: folder)
    }

    private func mod(_ key: String) -> ModToggle { Catalog.mods.first { $0.key == key }! }

    func testCustomStringColorsIsTwoOrZero() throws {
        let s = try store()
        XCTAssertFalse(s.isOn("CustomStringColors"), "the default, 0, is off")
        s.toggle(mod("CustomStringColors")).wrappedValue = true
        XCTAssertEqual(s.value(Section.toggles, "CustomStringColors"), "2")
        XCTAssertTrue(s.isOn("CustomStringColors"))
        s.toggle(mod("CustomStringColors")).wrappedValue = false
        XCTAssertEqual(s.value(Section.toggles, "CustomStringColors"), "0")
    }

    func testSeparateNoteColorsSetsBothKeys() throws {
        let s = try store()
        s.toggle(mod("SeparateNoteColors")).wrappedValue = true
        XCTAssertEqual(s.value(Section.toggles, "SeparateNoteColors"), "on")
        XCTAssertEqual(s.value(Section.settings, "SeparateNoteColorsMode"), "2")
        XCTAssertTrue(s.isOn("SeparateNoteColors"))
        s.toggle(mod("SeparateNoteColors")).wrappedValue = false
        XCTAssertEqual(s.value(Section.settings, "SeparateNoteColorsMode"), "0")
    }

    func testSeparateNoteColorsNeedsItsMode() throws {
        XCTAssertFalse(try store("[Toggle Switches]\nSeparateNoteColors = on\n").isOn("SeparateNoteColors"))
    }

    func testHighwayColorsLiveInTheirOwnSection() throws {
        let s = try store()
        s.toggle(mod("CustomHighwayColors")).wrappedValue = true
        XCTAssertEqual(s.ini.value(Section.highwayColors, "CustomHighwayColors"), "on")
        XCTAssertNil(s.ini.value(Section.toggles, "CustomHighwayColors"))
    }

    func testAColorIsSavedAsHexAndClearedToEmpty() throws {
        let s = try store()
        XCTAssertNil(s.colorValue(Section.stringColors, "string0_N"))
        s.color(Section.stringColors, "string0_N", fallback: "FF4F5A").wrappedValue = Color(hex: "112233")
        XCTAssertEqual(s.colorValue(Section.stringColors, "string0_N"), "112233")
        s.set(Section.stringColors, "string0_N", "")
        XCTAssertNil(s.colorValue(Section.stringColors, "string0_N"))
    }

    func testAKeyTheGameJustWroteIsNotLost() throws {
        let s = try store("[Mod Settings]\nSunlightMode = 1\n")
        // what rsm_set does: a new file over the old one
        let game = folder.appendingPathComponent("game.new")
        try "[Mod Settings]\nSunlightMode = 2\n".write(to: game, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.modificationDate: Date().addingTimeInterval(5)], ofItemAtPath: game.path)
        _ = try FileManager.default.replaceItemAt(ini, withItemAt: game)
        s.set(Section.toggles, "ToggleLoft", "on")
        let saved = try String(contentsOf: ini, encoding: .utf8)
        XCTAssertTrue(saved.contains("SunlightMode = 2"), saved)
    }

    func testAKeyOfAModThatIsOffIsNoConflict() throws {
        let both = "[Toggle Switches]\nCameraDirector = on\nRainbowNotes = on\n[Keybinds]\nCameraSideKey = N\nRainbowNotesKey = N\n"
        XCTAssertTrue(try store(both).duplicateKeys.contains("Keybinds/CameraSideKey"))
        let one = both.replacingOccurrences(of: "RainbowNotes = on", with: "RainbowNotes = off")
        XCTAssertFalse(try store(one).duplicateKeys.contains("Keybinds/CameraSideKey"))
    }

    func testEditions() throws {
        let home = folder!
        let game = home.appendingPathComponent("game", isDirectory: true)
        let lp = home.appendingPathComponent("learn-play/game", isDirectory: true)
        for f in [game, lp] { try FileManager.default.createDirectory(at: f, withIntermediateDirectories: true) }
        XCTAssertTrue(SettingsStore(gameFolder: game).hasLearnPlay)
        XCTAssertFalse(SettingsStore(gameFolder: game).isLearnPlay)
        let s = SettingsStore(gameFolder: lp)
        XCTAssertTrue(s.isLearnPlay)
        XCTAssertEqual(s.home.standardizedFileURL.path, home.standardizedFileURL.path)
    }
}
