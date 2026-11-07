import Foundation

// Key names in RSMods.ini: letters and digits as themselves, other keys as Windows VK_ names (what the
// Windows GUI writes), and the macOS key codes the mods map them to (KEYS in rsmods_ini.c).
enum KeyNames {
    private static let table: [(name: String, code: UInt16)] = [
        ("A", 0x00), ("S", 0x01), ("D", 0x02), ("F", 0x03), ("H", 0x04), ("G", 0x05), ("Z", 0x06), ("X", 0x07),
        ("C", 0x08), ("V", 0x09), ("B", 0x0b), ("Q", 0x0c), ("W", 0x0d), ("E", 0x0e), ("R", 0x0f), ("Y", 0x10),
        ("T", 0x11), ("1", 0x12), ("2", 0x13), ("3", 0x14), ("4", 0x15), ("6", 0x16), ("5", 0x17), ("9", 0x19),
        ("7", 0x1a), ("8", 0x1c), ("0", 0x1d), ("O", 0x1f), ("U", 0x20), ("I", 0x22), ("P", 0x23), ("L", 0x25),
        ("J", 0x26), ("K", 0x28), ("N", 0x2d), ("M", 0x2e),
        ("OEM_PLUS", 0x18), ("OEM_MINUS", 0x1b), ("OEM_6", 0x1e), ("OEM_4", 0x21), ("OEM_7", 0x27),
        ("OEM_1", 0x29), ("OEM_5", 0x2a), ("OEM_COMMA", 0x2b), ("OEM_2", 0x2c), ("OEM_PERIOD", 0x2f),
        ("OEM_3", 0x32),
        ("RETURN", 0x24), ("TAB", 0x30), ("SPACE", 0x31), ("BACK", 0x33),
        ("F1", 0x7a), ("F2", 0x78), ("F3", 0x63), ("F4", 0x76), ("F5", 0x60), ("F6", 0x61), ("F7", 0x62),
        ("F8", 0x64), ("F9", 0x65), ("F10", 0x6d), ("F11", 0x67), ("F12", 0x6f), ("F13", 0x69), ("F14", 0x6b),
        ("F15", 0x71), ("F16", 0x6a), ("F17", 0x40), ("F18", 0x4f), ("F19", 0x50), ("F20", 0x5a),
        ("INSERT", 0x72), ("HOME", 0x73), ("PRIOR", 0x74), ("DELETE", 0x75), ("END", 0x77),
        ("NEXT", 0x79), ("LEFT", 0x7b), ("RIGHT", 0x7c), ("DOWN", 0x7d), ("UP", 0x7e),
        ("DECIMAL", 0x41), ("MULTIPLY", 0x43), ("ADD", 0x45), ("NUMLOCK", 0x47),
        ("DIVIDE", 0x4b), ("SUBTRACT", 0x4e),
        ("NUMPAD0", 0x52), ("NUMPAD1", 0x53), ("NUMPAD2", 0x54), ("NUMPAD3", 0x55), ("NUMPAD4", 0x56),
        ("NUMPAD5", 0x57), ("NUMPAD6", 0x58), ("NUMPAD7", 0x59), ("NUMPAD8", 0x5b), ("NUMPAD9", 0x5c),
        // Other names the mods read. Last: a pressed key is written with the first name of its code
        ("ESCAPE", 0x35), ("HELP", 0x72), ("CLEAR", 0x47),
    ]

    private static let friendly: [String: String] = [
        "OEM_PLUS": "=", "OEM_MINUS": "-", "OEM_6": "]", "OEM_4": "[", "OEM_7": "'", "OEM_1": ";",
        "OEM_5": "\\", "OEM_COMMA": ",", "OEM_2": "/", "OEM_PERIOD": ".", "OEM_3": "`",
        "RETURN": "Return", "TAB": "Tab", "SPACE": "Space", "BACK": "Delete", "DELETE": "Forward Delete",
        "INSERT": "Help", "HOME": "Home", "END": "End", "PRIOR": "Page Up", "NEXT": "Page Down",
        "LEFT": "←", "RIGHT": "→", "UP": "↑", "DOWN": "↓",
        "DECIMAL": "Keypad .", "MULTIPLY": "Keypad *", "ADD": "Keypad +", "NUMLOCK": "Clear",
        "DIVIDE": "Keypad /", "SUBTRACT": "Keypad -", "ESCAPE": "Esc", "HELP": "Help", "CLEAR": "Clear",
    ]

    private static func bare(_ value: String) -> String {
        let v = value.uppercased()
        return v.hasPrefix("VK_") ? String(v.dropFirst(3)) : v
    }

    // The value to write for a pressed key, or nil if the mods can't use it
    static func iniValue(forKeyCode code: UInt16) -> String? {
        guard let name = table.first(where: { $0.code == code })?.name else { return nil }
        return name.count == 1 ? name : "VK_" + name
    }

    static func code(forIniValue value: String) -> UInt16? {
        let name = bare(value)
        return table.first(where: { $0.name == name })?.code
    }

    // How a key shows in the app: "R", "F5", "Page Up", "Keypad 3"
    static func display(_ value: String) -> String {
        let name = bare(value)
        if name.isEmpty { return "" }
        if let f = friendly[name] { return f }
        if name.hasPrefix("NUMPAD") { return "Keypad " + name.dropFirst(6) }
        return name
    }

    static func isKnown(_ value: String) -> Bool { value.isEmpty || code(forIniValue: value) != nil }
}
