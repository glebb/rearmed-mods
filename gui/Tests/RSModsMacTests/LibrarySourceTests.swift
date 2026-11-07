import XCTest
@testable import RSModsMac

// The app and the library each have a table of the settings and the keys: these keep the two in step
final class LibrarySourceTests: XCTestCase {
    private var source: String {
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        return try! String(contentsOf: root.appendingPathComponent("src/rsmods_ini.c"), encoding: .utf8)
    }

    private func matches(_ pattern: String, in text: String) -> [[String]] {
        let regex = try! NSRegularExpression(pattern: pattern)
        return regex.matches(in: text, range: NSRange(text.startIndex..., in: text)).map { m in
            (1..<m.numberOfRanges).map { String(text[Range(m.range(at: $0), in: text)!]) }
        }
    }

    // {"Section", "Key", "value"} of DEFAULTS
    private var libraryDefaults: [(section: String, key: String, value: String)] {
        matches(#"\{"([^"]+)", "([^"]+)", "([^"]*)"\}"#, in: source).map { ($0[0], $0[1], $0[2]) }
    }

    func testDefaultsMatchTheLibrary() {
        let library = libraryDefaults
        XCTAssertGreaterThan(library.count, 50)
        for d in library {
            XCTAssertEqual(Catalog.defaults[d.section]?[d.key], d.value, "[\(d.section)] \(d.key)")
        }
        let count = Catalog.defaults.values.reduce(0) { $0 + $1.count }
        XCTAssertEqual(count, library.count, "a default in the app that the library lacks")
    }

    func testEveryKeybindHasADefault() {
        for kb in Catalog.modKeybinds + Catalog.audioKeybinds {
            XCTAssertNotNil(Catalog.defaults[kb.section]?[kb.key], kb.key)
        }
    }

    // A key press goes to every mod: two actions on a key would both happen. The mods' keys all, on or off.
    func testNoTwoActionsShareADefaultKey() {
        var seen: [String: String] = [:]
        for d in libraryDefaults where d.section == Section.keybinds || d.section == Section.audioKeybinds {
            let key = d.value.uppercased()
            XCTAssertNil(seen[key], "\(d.key) and \(seen[key] ?? "") are both \(key)")
            seen[key] = d.key
        }
        XCTAssertGreaterThan(seen.count, 20)
    }

    func testEveryDefaultKeyIsAKeyTheLibraryKnows() {
        for kb in Catalog.modKeybinds + Catalog.audioKeybinds {
            XCTAssertNotNil(KeyNames.code(forIniValue: Catalog.defaults[kb.section]![kb.key]!), kb.key)
        }
    }

    func testKeyNamesMatchTheLibrary() {
        let table = source[source.range(of: "} KEYS[] = {")!.upperBound...]
        let keys = matches(#"\{"([A-Z0-9_]+)", 0x([0-9a-f]+)\}"#, in: String(table))
        XCTAssertGreaterThan(keys.count, 90)
        for k in keys {
            XCTAssertEqual(KeyNames.code(forIniValue: k[0]), UInt16(k[1], radix: 16), k[0])
        }
    }

    func testEveryModCheckboxIsASettingOfTheLibrary() {
        let library = Set(libraryDefaults.map { $0.section + "/" + $0.key })
        for mod in Catalog.mods {
            XCTAssertTrue(library.contains(mod.section + "/" + mod.key), mod.key)
        }
    }
}
