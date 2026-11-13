import XCTest
@testable import RSModsMac

final class ToneHostProfilesTests: XCTestCase {
    private var folder: URL!
    private let helix: ToneHostProfiles.Lookup = { type, subtype, manufacturer in
        [type, subtype, manufacturer] == ["aumf", "6HPA", "LSIX"] ? .init(name: "Line 6: Helix Native", version: 0x030802) : nil
    }

    override func setUpWithError() throws {
        folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    }

    override func tearDown() { try? FileManager.default.removeItem(at: folder) }

    private func profile(_ edit: (inout [String: Any]) -> Void = { _ in }) -> [String: Any] {
        var p: [String: Any] = [
            "name": "Helix Native", "plugin": ["type": "aumf", "subtype": "6HPA", "manufacturer": "LSIX"],
            "plugin_version": 0x030802,
            "templates": ["clean": ["state": "states/clean.plist"], "driven": ["state": "states/driven.plist"]],
            "map": ["clean": "clean", "driven": "driven", "by_prefix": ["Amp_X": "driven"]],
        ]
        edit(&p)
        return p
    }

    private func save(_ name: String, in home: URL, codes: [String] = ["aumf", "6HPA", "LSIX"]) throws {
        let states = home.appendingPathComponent("states", isDirectory: true)
        try FileManager.default.createDirectory(at: states, withIntermediateDirectories: true)
        let state: NSDictionary = ["type": ToneHostProfiles.fourCC(codes[0]), "subtype": ToneHostProfiles.fourCC(codes[1]),
                                   "manufacturer": ToneHostProfiles.fourCC(codes[2]), "data": Data([1])]
        try state.write(to: states.appendingPathComponent(name + ".plist"))
    }

    private func install(_ id: String, _ p: [String: Any], states: [String] = ["clean", "driven"]) throws -> URL {
        let home = folder.appendingPathComponent("plugins/\(id)", isDirectory: true)
        try FileManager.default.createDirectory(at: home, withIntermediateDirectories: true)
        XCTAssertTrue(ToneHostConfig.write(p, to: home.appendingPathComponent("profile.json")))
        for s in states { try save(s, in: home) }
        return home
    }

    func testNothingIsOfferedBeforeASetup() {
        XCTAssertEqual(ToneHostProfiles.all(in: folder, config: ["input": ["device": "X"]], installed: helix), [])
    }

    func testAFinishedProfileCanBeChosen() throws {
        _ = try install("helix-native", profile())
        XCTAssertEqual(ToneHostProfiles.all(in: folder, config: [:], installed: helix),
                       [ToneHostPlugin(id: "helix-native", name: "Helix Native")])
    }

    func testASetupUnderWayIsntListed() throws {
        _ = try install(".helix-native.new", profile())
        XCTAssertEqual(ToneHostProfiles.all(in: folder, config: [:], installed: helix), [])
    }

    func testWhatKeepsAProfileFromPlaying() throws {
        func problem(_ id: String, _ p: [String: Any], states: [String] = ["clean", "driven"],
                     installed: ToneHostProfiles.Lookup? = nil) throws -> String? {
            _ = try install(id, p, states: states)
            return ToneHostProfiles.all(in: folder, config: [:], installed: installed ?? helix).first { $0.id == id }?.problem
        }
        XCTAssertEqual(try problem("a", profile(), installed: { _, _, _ in nil }), "The plug-in isn't installed.")
        XCTAssertEqual(try problem("b", profile { $0["plugin"] = ["type": "aumf"] }), "It names no plug-in.")
        XCTAssertEqual(try problem("c", profile { $0["templates"] = [String: Any]() }), "It has no sounds.")
        XCTAssertEqual(try problem("d", profile { $0["map"] = ["clean": "clean", "driven": "heavy"] }),
                       "Its rules name a sound that isn't there.")
        XCTAssertEqual(try problem("e", profile { $0["map"] = ["clean": "clean"] }), "Its rules name a sound that isn't there.")
        XCTAssertEqual(try problem("f", profile(), states: ["clean"]), "1 of its 2 sounds aren't saved.")
        XCTAssertEqual(try problem("g", profile(), states: []), "Its sounds aren't saved.")
        let home = try install("h", profile(), states: ["clean"])
        try save("driven", in: home, codes: ["aufx", "SND1", "Shed"])
        XCTAssertEqual(ToneHostProfiles.all(in: folder, config: [:], installed: helix).first { $0.id == "h" }?.problem,
                       "Its sounds were saved from another plug-in.")
        try "{".write(to: home.appendingPathComponent("profile.json"), atomically: true, encoding: .utf8)
        XCTAssertEqual(ToneHostProfiles.all(in: folder, config: [:], installed: helix).first { $0.id == "h" }?.problem,
                       "Its profile can't be read.")
    }

    func testAnUpdatedPlugInStillPlaysWithANote() throws {
        _ = try install("helix-native", profile { $0["plugin_version"] = 0x030701 })
        let found = ToneHostProfiles.all(in: folder, config: [:], installed: helix)
        XCTAssertNil(found[0].problem)
        XCTAssertEqual(found[0].note, "Set up with version 3.7.1 of the plug-in, and 3.8.2 is installed. If tones sound wrong, set it up again.")
    }

    func testAConfigFromBeforeProfilesIsItsOwnPlugIn() throws {
        var config = profile { $0["name"] = nil }
        try save("clean", in: folder)
        try save("driven", in: folder)
        _ = try install("other", profile { $0["name"] = "Other" })
        XCTAssertEqual(ToneHostProfiles.all(in: folder, config: config, installed: helix).map(\.name),
                       ["Line 6: Helix Native", "Other"])
        XCTAssertEqual(ToneHostProfiles.chosen(config), "")
        ToneHostProfiles.choose("other", in: &config)
        XCTAssertEqual(ToneHostProfiles.chosen(config), "other")
        ToneHostProfiles.choose("", in: &config)
        XCTAssertNil(config["profile"])
    }
}
