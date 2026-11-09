import AppKit
import Foundation

// Installs the mods library the app carries (Contents/Resources/rearmed-mods.dylib, built by build-app.sh) into RS
// Re-ARMed's mods folders: <home>/mods for the Mac edition, <home>/learn-play/mods for Learn & Play, the same file in
// both. RS Re-ARMed loads them at startup; this library needs 1.5.1 or later.
//
// With it goes Song Stems' helper, rs-stems (stems/), when the app carries it (Contents/Resources/stems: the helper, its
// model compiled, Demucs' licence) and macOS is 15 or later, which its model needs: into <home>/stems, one for both
// editions. A model is compiled for the Neural Engine the first time it loads, 20 s or more, so the install starts that
// (rs-stems warm) for a song not to wait for it. A helper or model other than the app's is an update, as the library is.
@MainActor
final class ModsLibrary: ObservableObject {
    enum State: Equatable {
        case noRearmed                // neither edition is installed next to the game folder
        case rearmedTooOld            // its game can't load this library (before 1.5.1)
        case notInstalled
        case installed(String)        // up to date (version)
        case updateAvailable(String)  // installed version (another than the app's), or an edition lacks it
        case newerInstalled(String)   // a newer library than the app's (a newer release or a local build)
        case noBundledLibrary         // the app was built without it (swift run)
    }

    @Published private(set) var state: State = .notInstalled
    @Published private(set) var message: String?

    static let fileName = "rearmed-mods.dylib"
    private static let versionMarker = Data("rearmed-mods-version:".utf8)
    // RS Re-ARMed's hook by name, which the library is built on, is from 1.5.1
    private static let runtimeMarker = Data("rc_hook_add_name".utf8)

    let bundled: URL?
    let bundledStems: URL?
    private let stemsSupported: Bool
    private let warmStems: (URL) -> Void
    private let gameIsRunning: () -> Bool
    private var supportChecked: (path: String, date: Date?, supported: Bool)?

    init(bundled: URL? = Bundle.main.url(forResource: "rearmed-mods", withExtension: "dylib"),
         bundledStems: URL? = Bundle.main.url(forResource: "stems", withExtension: nil),
         stemsSupported: Bool = ModsLibrary.macOSRunsStems,
         warmStems: @escaping (URL) -> Void = ModsLibrary.warm,
         gameIsRunning: @escaping () -> Bool = ModsLibrary.gameRunning) {
        self.bundled = bundled
        self.bundledStems = bundledStems
        self.stemsSupported = stemsSupported
        self.warmStems = warmStems
        self.gameIsRunning = gameIsRunning
    }

    var bundledVersion: String? { bundled.flatMap(Self.version(of:)) }

    private var home: URL = URL(fileURLWithPath: NSHomeDirectory())

    private var gameExecutable: URL {
        home.appendingPathComponent("game/Rocksmith2014.app/Contents/MacOS/Rocksmith2014")
    }

    // The installed editions' folders, each with its game/ and mods/
    private var editions: [URL] {
        [home, home.appendingPathComponent("learn-play")].filter {
            FileManager.default.fileExists(atPath: $0.appendingPathComponent("game").path)
        }
    }

    private var stemsFolder: URL { home.appendingPathComponent("stems", isDirectory: true) }

    // The helper the app carries, if this Mac can run it
    private var stemsToInstall: URL? { stemsSupported ? bundledStems : nil }

    // Every file of the app's helper is installed as it is (a model is a folder: compared through)
    private func stemsCurrent() -> Bool {
        guard let source = stemsToInstall else { return true }
        let fm = FileManager.default
        guard let names = try? fm.contentsOfDirectory(atPath: source.path) else { return true }
        return names.allSatisfy {
            fm.contentsEqual(atPath: source.appendingPathComponent($0).path, andPath: stemsFolder.appendingPathComponent($0).path)
        }
    }

    private func installStems() throws {
        guard let source = stemsToInstall else { return }
        let fm = FileManager.default
        let temp = home.appendingPathComponent("stems.new", isDirectory: true)
        try? fm.removeItem(at: temp)
        try fm.copyItem(at: source, to: temp)
        Self.unquarantine(temp)
        if fm.fileExists(atPath: stemsFolder.path) {
            _ = try fm.replaceItemAt(stemsFolder, withItemAt: temp)
        } else {
            try fm.moveItem(at: temp, to: stemsFolder)
        }
        warmStems(stemsFolder.appendingPathComponent("rs-stems"))
    }

    private func installedURL(_ edition: URL) -> URL {
        edition.appendingPathComponent("mods").appendingPathComponent(Self.fileName)
    }

    private func setHome(gameFolder: URL) {
        let parent = gameFolder.deletingLastPathComponent()
        home = parent.lastPathComponent == "learn-play" ? parent.deletingLastPathComponent() : parent
    }

    // Runs each time the app comes forward: the executable, near 100 MB, is searched once for each build of it
    private func supportsMods() -> Bool {
        let path = gameExecutable.path
        let date = (try? FileManager.default.attributesOfItem(atPath: path))?[.modificationDate] as? Date
        if let known = supportChecked, known.path == path, known.date == date { return known.supported }
        let supported = Self.supportsMods(gameExecutable)
        supportChecked = (path, date, supported)
        return supported
    }

    func refresh(gameFolder: URL) {
        message = nil
        setHome(gameFolder: gameFolder)
        let fm = FileManager.default
        guard !editions.isEmpty else { state = .noRearmed; return }
        if fm.fileExists(atPath: gameExecutable.path), !supportsMods() {
            state = .rearmedTooOld
            return
        }
        guard let bundledVersion, let bundled else { state = .noBundledLibrary; return }
        let installed = editions.map(installedURL).filter { fm.fileExists(atPath: $0.path) }
        guard let first = installed.first else { state = .notInstalled; return }
        if installed.count == editions.count, installed.allSatisfy({ fm.contentsEqual(atPath: $0.path, andPath: bundled.path) }) {
            state = stemsCurrent() ? .installed(bundledVersion) : .updateAvailable(bundledVersion)
            return
        }
        let versions = installed.map { Self.version(of: $0) ?? "unknown" }
        // Never offered as an update, in any edition: it would put the older library over the newer
        if let newer = versions.first(where: { Self.isNewer($0, than: bundledVersion) }) {
            state = .newerInstalled(newer)
        } else {
            state = .updateAvailable(Self.version(of: first) ?? "unknown")
        }
    }

    func install(gameFolder: URL) {
        setHome(gameFolder: gameFolder)
        guard let bundled else { return }
        guard !gameIsRunning() else { message = "Quit RS Re-ARMed first."; return }
        let fm = FileManager.default
        var result: String
        do {
            for edition in editions {
                let target = installedURL(edition)
                let dir = target.deletingLastPathComponent()
                let temp = dir.appendingPathComponent(Self.fileName + ".new")
                try fm.createDirectory(at: dir, withIntermediateDirectories: true)
                try? fm.removeItem(at: temp)
                try fm.copyItem(at: bundled, to: temp)
                // A file copied out of a downloaded app keeps its quarantine flag, and the game would not load it
                removexattr(temp.path, "com.apple.quarantine", 0)
                // A new file renamed over the old one: overwriting a library in place leaves macOS with the old code
                // signature cached for the file
                if fm.fileExists(atPath: target.path) {
                    _ = try fm.replaceItemAt(target, withItemAt: temp)
                } else {
                    try fm.moveItem(at: temp, to: target)
                }
            }
            let stems = stemsToInstall != nil && !stemsCurrent()
            try installStems()
            result = stems
                ? "Installed. Song Stems' model is being prepared for the Neural Engine (about 20 seconds); then start RS Re-ARMed."
                : "Installed. Start RS Re-ARMed to use the mods."
        } catch {
            result = "Install failed: \(error.localizedDescription)"
        }
        refresh(gameFolder: gameFolder)
        message = result
    }

    func uninstall(gameFolder: URL) {
        setHome(gameFolder: gameFolder)
        guard !gameIsRunning() else { message = "Quit RS Re-ARMed first."; return }
        let fm = FileManager.default
        var result: String
        do {
            for edition in editions where fm.fileExists(atPath: installedURL(edition).path) {
                try fm.removeItem(at: installedURL(edition))
            }
            if fm.fileExists(atPath: stemsFolder.appendingPathComponent("rs-stems").path) {
                try fm.removeItem(at: stemsFolder)
            }
            result = "Removed. RS Re-ARMed starts without the mods."
        } catch {
            result = "Remove failed: \(error.localizedDescription)"
        }
        refresh(gameFolder: gameFolder)
        message = result
    }

    // Versions are major.minor.patch (VERSION); one that doesn't parse is never newer
    static func isNewer(_ version: String, than other: String) -> Bool {
        guard version.first?.isNumber == true else { return false }
        return version.compare(other, options: .numeric) == .orderedDescending
    }

    // The version tag the library carries (rsm_version_tag in src/rsmods.c)
    static func version(of url: URL) -> String? {
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe),
              let r = data.range(of: versionMarker) else { return nil }
        let tail = data[r.upperBound...].prefix(32)
        let bytes = tail.prefix { $0 != 0 }
        return String(decoding: bytes, as: UTF8.self)
    }

    static func supportsMods(_ executable: URL) -> Bool {
        guard let data = try? Data(contentsOf: executable, options: .alwaysMapped) else { return false }
        return data.range(of: runtimeMarker) != nil
    }

    // The helper's model is converted for macOS 15 (its attention op)
    nonisolated static var macOSRunsStems: Bool {
        ProcessInfo.processInfo.isOperatingSystemAtLeast(OperatingSystemVersion(majorVersion: 15, minorVersion: 0, patchVersion: 0))
    }

    // Files copied out of a downloaded app keep its quarantine flag, and macOS would refuse to run the helper
    nonisolated static func unquarantine(_ url: URL) {
        removexattr(url.path, "com.apple.quarantine", XATTR_NOFOLLOW)
        guard let all = FileManager.default.enumerator(at: url, includingPropertiesForKeys: nil) else { return }
        for case let item as URL in all { removexattr(item.path, "com.apple.quarantine", XATTR_NOFOLLOW) }
    }

    // Compiles the model for the Neural Engine once (rs-stems warm), in the background
    nonisolated static func warm(_ helper: URL) {
        let p = Process()
        p.executableURL = helper
        p.arguments = ["warm"]
        p.standardOutput = FileHandle.nullDevice
        p.standardError = FileHandle.nullDevice
        try? p.run()
    }

    nonisolated static func gameRunning() -> Bool {
        ["Rocksmith2014", "rs2014_lp"].contains { name in
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
            p.arguments = ["-x", name]
            p.standardOutput = FileHandle.nullDevice
            try? p.run()
            p.waitUntilExit()
            return p.terminationStatus == 0
        }
    }
}
