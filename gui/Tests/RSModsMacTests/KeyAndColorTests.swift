import SwiftUI
import XCTest
@testable import RSModsMac

final class KeyAndColorTests: XCTestCase {
    func testAColorSurvivesTheRoundTrip() {
        for hex in ["FF4F5A", "000000", "FFFFFF", "1DACF9", "7648A8"] { XCTAssertEqual(Color(hex: hex).hex, hex) }
    }

    func testAPressedKeyIsWrittenAsWindowsWrites() {
        XCTAssertEqual(KeyNames.iniValue(forKeyCode: 0x0f), "R")
        XCTAssertEqual(KeyNames.iniValue(forKeyCode: 0x60), "VK_F5")
        XCTAssertEqual(KeyNames.iniValue(forKeyCode: 0x72), "VK_INSERT", "not its other name")
    }

    func testKeyNamesIgnoreCaseAndThePrefix() {
        XCTAssertEqual(KeyNames.code(forIniValue: "vk_f5"), KeyNames.code(forIniValue: "F5"))
        XCTAssertTrue(KeyNames.isKnown("VK_ESCAPE"))
        XCTAssertFalse(KeyNames.isKnown("VK_NOPE"))
    }

    func testDisplayNames() {
        XCTAssertEqual(KeyNames.display("VK_PRIOR"), "Page Up")
        XCTAssertEqual(KeyNames.display("VK_NUMPAD3"), "Keypad 3")
    }
}
