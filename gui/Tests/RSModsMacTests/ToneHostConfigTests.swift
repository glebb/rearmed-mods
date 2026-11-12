import XCTest
@testable import RSModsMac

final class ToneHostConfigTests: XCTestCase {
    private let stomp: [String: Any] = ["input": ["device": "HX Stomp", "channel": 4, "gain_db": 6, "other": 1],
                                        "output": ["device": "HX Stomp"], "buffer": 64]

    func testReadsTheChosenInterface() {
        XCTAssertEqual(ToneHostConfig.chosen(stomp),
                       ToneHostSetup(device: "HX Stomp", channel: 5, gainDb: 6, output: "HX Stomp"))
        XCTAssertNil(ToneHostConfig.chosen(["input": ["device": ""]]))
    }

    func testChoosingKeepsEachInterface() {
        var c = stomp
        ToneHostConfig.choose(ToneHostConfig.chosen(c)!, in: &c)
        ToneHostConfig.choose(ToneHostSetup(device: "Trio", channel: 1, gainDb: 15, output: nil), in: &c)
        XCTAssertEqual(ToneHostConfig.chosen(c), ToneHostSetup(device: "Trio", channel: 1, gainDb: 15, output: nil))
        XCTAssertNil(c["output"], "none: the system's output")
        XCTAssertEqual((c["input"] as? [String: Any])?["other"] as? Int, 1)
        XCTAssertEqual(c["buffer"] as? Int, 64)
        XCTAssertEqual(ToneHostConfig.remembered("HX Stomp", in: c)?.channel, 5)
        ToneHostConfig.choose(ToneHostSetup(device: "Trio", channel: 2, gainDb: 12, output: "Trio"), in: &c)
        XCTAssertEqual((c["setups"] as? [[String: Any]])?.count, 2, "one entry for each interface")
        XCTAssertEqual(ToneHostConfig.remembered("Trio", in: c)?.channel, 2)
        XCTAssertNil(ToneHostConfig.remembered("Other", in: c))
    }

    func testAFileComesBackTheSame() throws {
        let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".json")
        defer { try? FileManager.default.removeItem(at: file) }
        XCTAssertTrue(ToneHostConfig.write(stomp, to: file))
        XCTAssertEqual(ToneHostConfig.chosen(ToneHostConfig.read(file)!), ToneHostConfig.chosen(stomp))
    }
}
