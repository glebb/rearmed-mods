import Foundation

// Tone Host set up from here, with what the app carries in Resources/tonehost (gui/build-app.sh): the host, and
// tonehost/helix's makers of Helix Native's presets. Installing the host is what tonehost/install.sh does, and Helix
// Native's sounds what tonehost/helix/setup.sh does: nothing by hand. The makers are Python, which macOS has with its
// command line tools
enum ToneHostInstaller {
    static let helixID = "helix-native"
    private static let helix = ["aumf", "6HPA", "LSIX"]

    // What the app carries, or nil in an app built without it
    static var carried: URL? {
        guard let folder = Bundle.main.resourceURL?.appendingPathComponent("tonehost", isDirectory: true),
              FileManager.default.fileExists(atPath: folder.appendingPathComponent("RS Tone Host.app").path) else { return nil }
        return folder
    }

    static var helixInstalled: Bool {
        ToneHostProfiles.component(type: helix[0], subtype: helix[1], manufacturer: helix[2]) != nil
    }

    // Whether the host in the tonehost folder is the one the app carries, byte for byte: its program and the two scripts.
    // false for one from another version of the app or built from source, and where there is none
    static func hostIsCarried(_ carried: URL, in folder: URL) -> Bool {
        ["RS Tone Host.app/Contents/MacOS/rs-tonehost", "start.sh", "stop.sh"].allSatisfy {
            FileManager.default.contentsEqual(atPath: carried.appendingPathComponent($0).path, andPath: folder.appendingPathComponent($0).path)
        }
    }

    // The host into the tonehost folder, over one that is there, and a config if there is none. nil, or what went wrong
    static func installHost(from carried: URL, to folder: URL) -> String? {
        let fm = FileManager.default
        do {
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            ToneHostConfig.restartHost(commands: folder.appendingPathComponent("commands"))  // a running one ends: the game starts the new
            for name in ["RS Tone Host.app", "start.sh", "stop.sh"] {
                let to = folder.appendingPathComponent(name)
                try? fm.removeItem(at: to)
                try fm.copyItem(at: carried.appendingPathComponent(name), to: to)
                ModsLibrary.unquarantine(to)  // start.sh opens the host as an app, which macOS refuses with a download's flag on it
            }
            let file = folder.appendingPathComponent("config.json")
            if !fm.fileExists(atPath: file.path) {
                guard var config = ToneHostConfig.read(carried.appendingPathComponent("config.default.json")) else {
                    return "The app's own copy of the host's config can't be read."
                }
                config["fifo"] = folder.appendingPathComponent("commands").path
                guard ToneHostConfig.write(config, to: file) else { return "The host's config couldn't be written." }
            }
            return nil
        } catch {
            return "The host couldn't be installed: \(error.localizedDescription)"
        }
    }

    // A program run to its end: whether it ended well, and what it said
    private static func run(_ program: URL, _ arguments: [String]) -> (ok: Bool, said: String) {
        let p = Process(), said = Pipe()
        p.executableURL = program
        p.arguments = arguments
        p.standardOutput = said
        p.standardError = said
        guard (try? p.run()) != nil else { return (false, "") }
        let text = String(decoding: said.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        p.waitUntilExit()
        return (p.terminationStatus == 0, text.trimmingCharacters(in: .whitespacesAndNewlines))
    }

    private static func lastLine(_ said: String) -> String { said.components(separatedBy: "\n").last ?? "" }

    // The host installed, Helix Native's presets and profile made from the Helix on this Mac, and every preset played
    // through the plug-in, heard by nobody. nil when Helix Native can be chosen, or what kept it from that
    static func setUpHelix(from carried: URL, in folder: URL, python: URL = URL(fileURLWithPath: "/usr/bin/python3")) -> String? {
        let fm = FileManager.default
        if ModsLibrary.gameRunning() { return "Quit the game first." }
        guard helixInstalled else { return "Helix Native isn't installed on this Mac." }
        guard fm.fileExists(atPath: folder.deletingLastPathComponent().appendingPathComponent("libnetguard.dylib").path) else {
            return "RS Re-ARMed's network guard isn't there: install RS Re-ARMed first."
        }
        guard run(python, ["-c", "pass"]).ok else {
            return "This needs macOS's command line tools. If macOS offers to install them now, accept, and click this again when it has finished."
        }
        if let problem = installHost(from: carried, to: folder) { return problem }
        let work = fm.temporaryDirectory.appendingPathComponent("rs-helix-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: work) }
        let made = work.appendingPathComponent("new", isDirectory: true), scripts = carried.appendingPathComponent("helix", isDirectory: true)
        guard (try? fm.createDirectory(at: made, withIntermediateDirectories: true)) != nil else { return "No room to work in." }
        // -B: nothing is written beside the scripts, which are inside the app
        let presets = run(python, ["-B", scripts.appendingPathComponent("make_presets.py").path, work.path])
        guard presets.ok else { return "Helix Native's presets couldn't be made: \(lastLine(presets.said))" }
        let host = folder.appendingPathComponent("RS Tone Host.app/Contents/MacOS/rs-tonehost")
        let version = run(host, ["--plugin"] + helix)
        let config = run(python, ["-B", scripts.appendingPathComponent("make_config.py").path, work.appendingPathComponent("tones.json").path,
                                  folder.appendingPathComponent("config.json").path, "--out", made.path, "--id", helixID,
                                  "--name", "Helix Native"] + (version.ok ? ["--version", lastLine(version.said)] : []))
        guard config.ok else { return "Helix Native's profile couldn't be made: \(lastLine(config.said))" }
        // The profile whole, or not at all: a folder whose name starts with a dot is a setup under way
        let plugins = folder.appendingPathComponent("plugins", isDirectory: true)
        let stage = plugins.appendingPathComponent(".\(helixID).new", isDirectory: true), home = plugins.appendingPathComponent(helixID, isDirectory: true)
        do {
            try? fm.removeItem(at: stage)
            try fm.createDirectory(at: stage, withIntermediateDirectories: true)
            try fm.copyItem(at: work.appendingPathComponent("presets"), to: stage.appendingPathComponent("presets"))
            try fm.copyItem(at: made.appendingPathComponent("profile.json"), to: stage.appendingPathComponent("profile.json"))
            try? fm.removeItem(at: home)  // and its levels, measured with the presets before these
            try fm.moveItem(at: stage, to: home)
            guard let new = ToneHostConfig.read(made.appendingPathComponent("config.json")),
                  ToneHostConfig.write(new, to: folder.appendingPathComponent("config.json")) else { return "The host's config couldn't be written." }
        } catch {
            return "Helix Native's profile couldn't be put in place: \(error.localizedDescription)"
        }
        return ToneHostTools.checkSound(profile: folder.appendingPathComponent("config.json"), in: folder)
    }
}
