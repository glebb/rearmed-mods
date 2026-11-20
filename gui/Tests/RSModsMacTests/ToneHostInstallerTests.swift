import XCTest
@testable import RSModsMac

final class ToneHostInstallerTests: XCTestCase {
    private var temp: URL!

    override func setUpWithError() throws {
        temp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: temp, withIntermediateDirectories: true)
    }

    override func tearDown() { try? FileManager.default.removeItem(at: temp) }

    // What the app carries, as gui/build-app.sh lays it out
    private func carried(start: String) throws -> URL {
        let carried = temp.appendingPathComponent("carried", isDirectory: true)
        let host = carried.appendingPathComponent("RS Tone Host.app/Contents/MacOS", isDirectory: true)
        try? FileManager.default.removeItem(at: carried)
        try FileManager.default.createDirectory(at: host, withIntermediateDirectories: true)
        try Data("host".utf8).write(to: host.appendingPathComponent("rs-tonehost"))
        try Data(start.utf8).write(to: carried.appendingPathComponent("start.sh"))
        try Data("stop".utf8).write(to: carried.appendingPathComponent("stop.sh"))
        try Data(#"{"input": {"device": "", "channel": 0}, "buffer": 128}"#.utf8).write(to: carried.appendingPathComponent("config.default.json"))
        return carried
    }

    func testInstallsTheHostAndAConfigAndKeepsAConfigThatIsThere() throws {
        let folder = temp.appendingPathComponent("install/tonehost", isDirectory: true), file = folder.appendingPathComponent("config.json")
        XCTAssertNil(ToneHostInstaller.installHost(from: try carried(start: "one"), to: folder))
        XCTAssertTrue(FileManager.default.fileExists(atPath: folder.appendingPathComponent("RS Tone Host.app/Contents/MacOS/rs-tonehost").path))
        XCTAssertEqual(try String(contentsOf: folder.appendingPathComponent("start.sh"), encoding: .utf8), "one")
        var config = try XCTUnwrap(ToneHostConfig.read(file))
        XCTAssertEqual(config["fifo"] as? String, folder.appendingPathComponent("commands").path)
        XCTAssertEqual(config["buffer"] as? Int, 128)
        // again, with a newer host: the player's config stays
        config["buffer"] = 64
        XCTAssertTrue(ToneHostConfig.write(config, to: file))
        XCTAssertNil(ToneHostInstaller.installHost(from: try carried(start: "two"), to: folder))
        XCTAssertEqual(try String(contentsOf: folder.appendingPathComponent("start.sh"), encoding: .utf8), "two")
        XCTAssertEqual(ToneHostConfig.read(file)?["buffer"] as? Int, 64)
    }

    // A downloaded app's files have the quarantine flag, and a copy keeps it: macOS wouldn't open the host
    func testTheInstalledHostHasNoQuarantineFlag() throws {
        let carried = try carried(start: "one"), folder = temp.appendingPathComponent("install/tonehost", isDirectory: true)
        let flag = "com.apple.quarantine", inside = ["RS Tone Host.app", "RS Tone Host.app/Contents/MacOS/rs-tonehost", "start.sh", "stop.sh"]
        for name in inside {
            let value = Array("0083;673a0000;Safari;".utf8)
            XCTAssertEqual(setxattr(carried.appendingPathComponent(name).path, flag, value, value.count, 0, XATTR_NOFOLLOW), 0, name)
        }
        XCTAssertNil(ToneHostInstaller.installHost(from: carried, to: folder))
        for name in inside {
            XCTAssertEqual(getxattr(folder.appendingPathComponent(name).path, flag, nil, 0, 0, XATTR_NOFOLLOW), -1, name)
        }
    }

    // What Update Tone Host goes by: a host from another version of the app is not the carried one, and is after an install
    func testKnowsAHostThatIsNotTheCarriedOne() throws {
        let folder = temp.appendingPathComponent("install/tonehost", isDirectory: true)
        XCTAssertFalse(ToneHostInstaller.hostIsCarried(try carried(start: "one"), in: folder))  // none installed
        XCTAssertNil(ToneHostInstaller.installHost(from: try carried(start: "one"), to: folder))
        XCTAssertTrue(ToneHostInstaller.hostIsCarried(try carried(start: "one"), in: folder))
        let newer = try carried(start: "two")
        XCTAssertFalse(ToneHostInstaller.hostIsCarried(newer, in: folder))
        try Data("another host".utf8).write(to: newer.appendingPathComponent("RS Tone Host.app/Contents/MacOS/rs-tonehost"))
        XCTAssertNil(ToneHostInstaller.installHost(from: newer, to: folder))
        XCTAssertTrue(ToneHostInstaller.hostIsCarried(newer, in: folder))
    }

    func testSaysWhatIsMissing() throws {
        let nothing = temp.appendingPathComponent("nothing", isDirectory: true)
        XCTAssertNotNil(ToneHostInstaller.installHost(from: nothing, to: temp.appendingPathComponent("install/tonehost")))
    }

    // The whole of it with the Helix Native on this Mac, where RS_HELIX_SETUP says "<the app's tonehost resources>|<a
    // tonehost folder to set up, with libnetguard.dylib beside it>": minutes, and nothing a test should do unasked
    func testSetsUpHelixNativeWhereAsked() throws {
        guard let spec = ProcessInfo.processInfo.environment["RS_HELIX_SETUP"] else { throw XCTSkip("RS_HELIX_SETUP isn't set") }
        let parts = spec.components(separatedBy: "|")
        let folder = URL(fileURLWithPath: parts[1], isDirectory: true)
        XCTAssertNil(ToneHostInstaller.setUpHelix(from: URL(fileURLWithPath: parts[0], isDirectory: true), in: folder))
        let config = try XCTUnwrap(ToneHostConfig.read(folder.appendingPathComponent("config.json")))
        XCTAssertEqual(ToneHostProfiles.chosen(config), ToneHostInstaller.helixID)
        let helix = try XCTUnwrap(ToneHostProfiles.all(in: folder, config: config).first { $0.id == ToneHostInstaller.helixID })
        XCTAssertNil(helix.problem)
        XCTAssertEqual(helix.name, "Helix Native")
    }
}
