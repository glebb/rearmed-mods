import XCTest
@testable import RSModsMac

@MainActor
final class ModsLibraryTests: XCTestCase {
    private var root: URL!
    private var running = false
    private var game: URL { root.appendingPathComponent("home/game") }
    private var macLibrary: URL { root.appendingPathComponent("home/mods/rearmed-mods.dylib") }
    private var lpLibrary: URL { root.appendingPathComponent("home/learn-play/mods/rearmed-mods.dylib") }
    private let fm = FileManager.default

    private func library(version: String = "0.7.0") throws -> ModsLibrary {
        let bundled = root.appendingPathComponent("bundled.dylib")
        try tagged(version).write(to: bundled)
        return ModsLibrary(bundled: bundled, gameIsRunning: { [unowned self] in running })
    }

    // With Song Stems' helper in the app: rs-stems, a model folder and the licence
    private var warmed: [String] = []
    private var installedStems: URL { root.appendingPathComponent("home/stems") }

    private func withStems(supported: Bool = true, helper: String = "helper 1") throws -> ModsLibrary {
        let bundled = root.appendingPathComponent("bundled.dylib")
        try tagged("0.7.0").write(to: bundled)
        let stems = root.appendingPathComponent("bundled-stems", isDirectory: true)
        try put(Data(helper.utf8), at: stems.appendingPathComponent("rs-stems"))
        try put(Data("weights".utf8), at: stems.appendingPathComponent("htdemucs_6s.mlmodelc/weights/weight.bin"))
        try put(Data("MIT".utf8), at: stems.appendingPathComponent("LICENSE-demucs"))
        return ModsLibrary(bundled: bundled, bundledStems: stems, stemsSupported: supported,
                           warmStems: { [unowned self] in warmed.append($0.path) }, gameIsRunning: { [unowned self] in running })
    }

    private func tagged(_ version: String) -> Data { Data("xx rearmed-mods-version:\(version)\0yy".utf8) }

    private func put(_ data: Data, at url: URL) throws {
        try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url)
    }

    override func setUp() async throws {
        root = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        running = false
        warmed = []
        try put(Data("x rc_hook_add_name y".utf8), at: game.appendingPathComponent("Rocksmith2014.app/Contents/MacOS/Rocksmith2014"))
        try fm.createDirectory(at: root.appendingPathComponent("home/learn-play/game"), withIntermediateDirectories: true)
    }

    override func tearDown() async throws { try? fm.removeItem(at: root) }

    func testNoRearmed() throws {
        let l = try library()
        l.refresh(gameFolder: root.appendingPathComponent("elsewhere/game"))
        XCTAssertEqual(l.state, .noRearmed)
    }

    func testAGameTooOld() throws {
        try put(Data("old".utf8), at: game.appendingPathComponent("Rocksmith2014.app/Contents/MacOS/Rocksmith2014"))
        let l = try library()
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .rearmedTooOld)
    }

    func testAnAppWithoutTheLibrary() {
        let l = ModsLibrary(bundled: nil, gameIsRunning: { false })
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .noBundledLibrary)
    }

    func testInstallAndRemoveCoverBothEditions() throws {
        let l = try library()
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .notInstalled)
        l.install(gameFolder: game)
        XCTAssertEqual(l.state, .installed("0.7.0"))
        XCTAssertTrue(fm.fileExists(atPath: macLibrary.path) && fm.fileExists(atPath: lpLibrary.path))
        l.refresh(gameFolder: root.appendingPathComponent("home/learn-play/game"))
        XCTAssertEqual(l.state, .installed("0.7.0"), "the same from Learn & Play's folder")
        l.uninstall(gameFolder: game)
        XCTAssertEqual(l.state, .notInstalled)
        XCTAssertFalse(fm.fileExists(atPath: lpLibrary.path))
    }

    func testOnlyTheMacEditionInstalled() throws {
        try fm.removeItem(at: root.appendingPathComponent("home/learn-play"))
        let l = try library()
        l.install(gameFolder: game)
        XCTAssertEqual(l.state, .installed("0.7.0"))
    }

    func testAnEditionWithoutItIsAnUpdate() throws {
        let l = try library()
        try put(tagged("0.7.0"), at: macLibrary)
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .updateAvailable("0.7.0"))
    }

    func testAnOlderLibraryIsAnUpdate() throws {
        let l = try library()
        try put(tagged("0.6.0"), at: macLibrary)
        try put(tagged("0.6.0"), at: lpLibrary)
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .updateAvailable("0.6.0"))
    }

    func testANewerLibraryIsNeverOfferedAsAnUpdate() throws {
        let l = try library()
        try put(tagged("0.8.0"), at: macLibrary)  // and none in Learn & Play
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .newerInstalled("0.8.0"))
    }

    func testNothingChangesWhileTheGameRuns() throws {
        let l = try library()
        running = true
        l.install(gameFolder: game)
        XCTAssertFalse(fm.fileExists(atPath: macLibrary.path))
        XCTAssertEqual(l.message, "Quit RS Re-ARMed first.")
        running = false
        l.refresh(gameFolder: game)
        XCTAssertNil(l.message, "gone once the app looks again")
    }

    func testTheQuarantineFlagIsNotCopied() throws {
        let l = try library()
        XCTAssertEqual(setxattr(l.bundled!.path, "com.apple.quarantine", "0081;0;Safari;", 14, 0, 0), 0)
        l.install(gameFolder: game)
        XCTAssertEqual(getxattr(macLibrary.path, "com.apple.quarantine", nil, 0, 0, 0), -1)
    }

    func testTheStemsHelperGoesWithTheLibrary() throws {
        let l = try withStems()
        l.install(gameFolder: game)
        XCTAssertEqual(l.state, .installed("0.7.0"))
        XCTAssertEqual(try Data(contentsOf: installedStems.appendingPathComponent("rs-stems")), Data("helper 1".utf8))
        XCTAssertTrue(fm.fileExists(atPath: installedStems.appendingPathComponent("htdemucs_6s.mlmodelc/weights/weight.bin").path))
        XCTAssertTrue(fm.fileExists(atPath: installedStems.appendingPathComponent("LICENSE-demucs").path))
        XCTAssertEqual(warmed, [installedStems.appendingPathComponent("rs-stems").path], "its model compiled once")
    }

    func testAnotherHelperIsAnUpdate() throws {
        try withStems(helper: "helper 1").install(gameFolder: game)
        let l = try withStems(helper: "helper 2")
        l.refresh(gameFolder: game)
        XCTAssertEqual(l.state, .updateAvailable("0.7.0"))
        l.install(gameFolder: game)
        XCTAssertEqual(l.state, .installed("0.7.0"))
        XCTAssertEqual(try Data(contentsOf: installedStems.appendingPathComponent("rs-stems")), Data("helper 2".utf8))
    }

    func testNoHelperBeforeMacOS15() throws {
        let l = try withStems(supported: false)
        l.install(gameFolder: game)
        XCTAssertEqual(l.state, .installed("0.7.0"), "the library alone is up to date there")
        XCTAssertFalse(fm.fileExists(atPath: installedStems.path))
        XCTAssertTrue(warmed.isEmpty)
    }

    func testRemovingTheModsRemovesTheHelper() throws {
        let l = try withStems()
        l.install(gameFolder: game)
        l.uninstall(gameFolder: game)
        XCTAssertFalse(fm.fileExists(atPath: installedStems.path))
        XCTAssertFalse(fm.fileExists(atPath: macLibrary.path))
    }

    func testTheHelperLosesTheQuarantineFlag() throws {
        let l = try withStems()
        let helper = l.bundledStems!.appendingPathComponent("rs-stems")
        XCTAssertEqual(setxattr(helper.path, "com.apple.quarantine", "0081;0;Safari;", 14, 0, 0), 0)
        l.install(gameFolder: game)
        XCTAssertEqual(getxattr(installedStems.appendingPathComponent("rs-stems").path, "com.apple.quarantine", nil, 0, 0, 0), -1)
    }

    func testVersionOrder() {
        XCTAssertTrue(ModsLibrary.isNewer("0.10.0", than: "0.9.0"))
        XCTAssertFalse(ModsLibrary.isNewer("0.7.0", than: "0.7.0"))
        XCTAssertFalse(ModsLibrary.isNewer("unknown", than: "0.7.0"))
    }
}
