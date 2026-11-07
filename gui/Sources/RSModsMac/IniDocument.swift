import Foundation

// RSMods.ini as lines, so that saving a value keeps the file's comments, order and keys this app doesn't know.
// Sections and keys match case-insensitively and the last occurrence wins, as in the mods' reader
// (../src/rsmods_ini.c).
struct IniDocument: Equatable {
    var lines: [String]

    init(text: String = "") {
        lines = text.isEmpty ? [] : text.replacingOccurrences(of: "\r\n", with: "\n").components(separatedBy: "\n")
    }

    var text: String { lines.joined(separator: "\n") }

    private enum Line {
        case section(String)
        case entry(key: String, value: String)
        case other
    }

    private static func parse(_ line: String) -> Line {
        let s = line.trimmingCharacters(in: .whitespaces)
        if s.isEmpty || s.hasPrefix(";") || s.hasPrefix("#") { return .other }
        if s.hasPrefix("[") {
            let name = s.dropFirst().split(separator: "]", maxSplits: 1, omittingEmptySubsequences: false).first ?? ""
            return .section(name.trimmingCharacters(in: .whitespaces))
        }
        guard let eq = s.firstIndex(of: "=") else { return .other }
        return .entry(key: s[..<eq].trimmingCharacters(in: .whitespaces),
                      value: s[s.index(after: eq)...].trimmingCharacters(in: .whitespaces))
    }

    private static func same(_ a: String, _ b: String) -> Bool { a.caseInsensitiveCompare(b) == .orderedSame }

    func value(_ section: String, _ key: String) -> String? {
        var current = ""
        var found: String?
        for line in lines {
            switch Self.parse(line) {
            case .section(let name): current = name
            case .entry(let k, let v) where Self.same(current, section) && Self.same(k, key): found = v
            default: break
            }
        }
        return found
    }

    mutating func set(_ section: String, _ key: String, _ value: String) {
        var current = ""
        var header: Int?
        var lastInSection: Int?
        var matched = false
        for (i, line) in lines.enumerated() {
            switch Self.parse(line) {
            case .section(let name):
                current = name
                if Self.same(name, section) {
                    header = i
                    lastInSection = i
                }
            case .entry(let k, _) where Self.same(current, section):
                lastInSection = i
                if Self.same(k, key) {
                    lines[i] = "\(k) = \(value)"
                    matched = true
                }
            case .other where Self.same(current, section) && !line.trimmingCharacters(in: .whitespaces).isEmpty:
                lastInSection = i  // a comment in the section
            default: break
            }
        }
        if matched { return }
        if header != nil, let last = lastInSection {
            lines.insert("\(key) = \(value)", at: last + 1)
            return
        }
        // A new section at the end, before the file's final newline
        let trailingNewline = lines.last == ""
        if trailingNewline { lines.removeLast() }
        if let last = lines.last, !last.trimmingCharacters(in: .whitespaces).isEmpty { lines.append("") }
        lines.append("[\(section)]")
        lines.append("\(key) = \(value)")
        lines.append("")
    }
}
