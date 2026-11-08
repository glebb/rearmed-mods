import XCTest
@testable import RSModsMac

// The app's profile backups (Before GUI): a copy of each changed save folder, the newest kept
final class ProfileBackupsTests: XCTestCase {
    private var home: URL!
    private var game: URL { home.appendingPathComponent("game", isDirectory: true) }
    private var save: URL { home.appendingPathComponent("steam/123/221680/remote", isDirectory: true) }
    private var tier: URL { game.appendingPathComponent("Profile_AutoBackups/123/Before GUI", isDirectory: true) }

    override func setUpWithError() throws {
        home = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: save, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: game, withIntermediateDirectories: true)
        try Data("list".utf8).write(to: save.appendingPathComponent("LocalProfiles.json"))
        try Data("profile".utf8).write(to: save.appendingPathComponent("ABCD_PRFLDB"))
    }

    override func tearDownWithError() throws { try? FileManager.default.removeItem(at: home) }

    private func at(_ seconds: TimeInterval) -> Date { Date(timeIntervalSince1970: 1_800_000_000 + seconds) }

    func testCopiesChangedFoldersAndKeepsTheNewest() throws {
        let first = ProfileBackups.backUp(gameFolder: game, keep: 2, now: at(0))
        XCTAssertEqual(first.map(\.lastPathComponent), ["2027-01-15_08-00-00"])
        XCTAssertEqual(try String(contentsOf: first[0].appendingPathComponent("ABCD_PRFLDB"), encoding: .utf8), "profile")
        XCTAssertEqual(ProfileBackups.state(of: first[0]), ProfileBackups.state(of: save), "the files' times are kept")

        XCTAssertTrue(ProfileBackups.backUp(gameFolder: game, keep: 2, now: at(60)).isEmpty, "nothing changed")

        for (i, text) in ["two", "three"].enumerated() {
            try Data(text.utf8).write(to: save.appendingPathComponent("ABCD_PRFLDB"))
            XCTAssertEqual(ProfileBackups.backUp(gameFolder: game, keep: 2, now: at(Double(120 * (i + 1)))).count, 1)
        }
        let kept = ProfileBackups.backups(in: tier)
        XCTAssertEqual(kept.map(\.lastPathComponent), ["2027-01-15_08-02-00", "2027-01-15_08-04-00"])
        XCTAssertEqual(try String(contentsOf: kept[1].appendingPathComponent("ABCD_PRFLDB"), encoding: .utf8), "three")
    }

    func testOnlyAccountFoldersWithAProfileList() throws {
        try FileManager.default.createDirectory(at: home.appendingPathComponent("steam/notanaccount/221680/remote"),
                                                withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: home.appendingPathComponent("steam/456/221680/remote"),
                                                withIntermediateDirectories: true)
        let folders = ProfileBackups.saveFolders(steamFolder: home.appendingPathComponent("steam"))
        XCTAssertEqual(folders.map(\.account), ["123"])
    }

    func testOtherFoldersInTheTierAreLeftAlone() throws {
        try FileManager.default.createDirectory(at: tier.appendingPathComponent("my notes"), withIntermediateDirectories: true)
        ProfileBackups.backUp(gameFolder: game, keep: 1, now: at(0))
        try Data("changed".utf8).write(to: save.appendingPathComponent("ABCD_PRFLDB"))
        ProfileBackups.backUp(gameFolder: game, keep: 1, now: at(600))
        XCTAssertTrue(FileManager.default.fileExists(atPath: tier.appendingPathComponent("my notes").path))
        XCTAssertEqual(ProfileBackups.backups(in: tier).count, 1)
    }
}
