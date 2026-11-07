import AppKit
import SwiftUI

// RSMods.ini's colors are RRGGBB, in sRGB
extension Color {
    init(hex: String) {
        let v = UInt32(hex.prefix(6), radix: 16) ?? 0x808080
        self.init(.sRGB, red: Double(v >> 16 & 0xff) / 255, green: Double(v >> 8 & 0xff) / 255,
                  blue: Double(v & 0xff) / 255)
    }

    var hex: String {
        guard let c = NSColor(self).usingColorSpace(.sRGB) else { return "808080" }
        func byte(_ x: CGFloat) -> Int { Int((min(max(x, 0), 1) * 255).rounded()) }
        return String(format: "%02X%02X%02X", byte(c.redComponent), byte(c.greenComponent), byte(c.blueComponent))
    }
}
