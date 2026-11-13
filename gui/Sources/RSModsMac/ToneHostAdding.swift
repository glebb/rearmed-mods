import AudioToolbox
import Foundation

// An Audio Unit effect installed here, which Tone Host could play through
struct ToneHostEffect: Equatable, Hashable, Identifiable {
    let name: String
    let type: String
    let subtype: String
    let manufacturer: String
    let version: UInt32
    var id: String { type + subtype + manufacturer }
    // "Line 6: Helix Native" is the maker and the plug-in: a profile is named for the plug-in
    var shortName: String { name.components(separatedBy: ": ").last ?? name }
}

// Making a profile here, from sounds the player sets in the plug-in's own window (tonehost/README.md): two of them, a
// clean one and a driven one, and the host picks by the game's tone. The profile is made in a folder nobody reads,
// plugins/.adding.new, and moves into place once the host's sound check has played both
extension ToneHostProfiles {
    static let sounds = ["clean", "driven"]
    static let madeHere = "Re-ARMed Mods"
    // The game's gear that makes any amp a driven one, as tonehost/helix/gear.json has it
    static let drivePedals = [
        "Bass_Pedal_BassDistortion", "Bass_Pedal_BassFuzz", "Bass_Pedal_BassOverdrive", "Pedal_BuzzOne", "Pedal_BuzzToo",
        "Pedal_CaptFuzzle", "Pedal_CustomDrive", "Pedal_Distortion", "Pedal_FuzzWasHe", "Pedal_GermaniumDrive",
        "Pedal_LineDrive", "Pedal_MarshallGuvnorPlus", "Pedal_MetalDistortion", "Pedal_RangeBooster", "Pedal_ShredZone",
        "Pedal_SuperDrive", "Pedal_VintageDistortion",
    ]

    // Effects and effects that take MIDI, other than Apple's own, by name. Nothing is opened
    static func effects() -> [ToneHostEffect] {
        var found: [ToneHostEffect] = []
        for type in [kAudioUnitType_Effect, kAudioUnitType_MusicEffect] {
            var wanted = AudioComponentDescription(componentType: type, componentSubType: 0, componentManufacturer: 0,
                                                   componentFlags: 0, componentFlagsMask: 0)
            var component: AudioComponent?
            while let next = AudioComponentFindNext(component, &wanted) {
                component = next
                var d = AudioComponentDescription()
                var name: Unmanaged<CFString>?
                var version: UInt32 = 0
                guard AudioComponentGetDescription(next, &d) == noErr, d.componentManufacturer != kAudioUnitManufacturer_Apple
                else { continue }
                AudioComponentCopyName(next, &name)
                AudioComponentGetVersion(next, &version)
                let codes = [d.componentType, d.componentSubType, d.componentManufacturer].map(fourCC)
                found.append(ToneHostEffect(name: (name?.takeRetainedValue() as String?) ?? codes.joined(separator: " "),
                                            type: codes[0], subtype: codes[1], manufacturer: codes[2], version: version))
            }
        }
        return found.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    static func fourCC(_ code: UInt32) -> String {
        String(bytes: [24, 16, 8, 0].map { UInt8(code >> $0 & 0xff) }, encoding: .macOSRoman) ?? ""
    }

    // A folder's name from the plug-in's: "Archetype: Gojira X" is archetype-gojira-x, and -2 if that is taken
    static func newID(for name: String, taken: Set<String>) -> String {
        let words = name.lowercased().unicodeScalars.split { !("a"..."z" ~= $0 || "0"..."9" ~= $0) }
        let slug = words.map { String(String.UnicodeScalarView($0)) }.joined(separator: "-")
        let base = slug.isEmpty ? "plug-in" : slug
        if !taken.contains(base) { return base }
        return (2...).lazy.map { "\(base)-\($0)" }.first { !taken.contains($0) } ?? base
    }

    static func profile(for effect: ToneHostEffect) -> [String: Any] {
        var templates: [String: Any] = [:]
        for sound in sounds { templates[sound] = ["state": "states/\(sound).plist", "output_db": 0] }
        return [
            "name": effect.shortName, "made_by": madeHere, "plugin_version": effect.version,
            "plugin": ["type": effect.type, "subtype": effect.subtype, "manufacturer": effect.manufacturer],
            "templates": templates,
            "map": ["gain_knobs": ["_Gain", "_Loudness1", "_Loudness2", "_Drive"], "drive_pedals": drivePedals,
                    "clean": "clean", "driven": "driven", "driven_from_gain": 0.3] as [String: Any],
        ]
    }

    static func effect(of profile: [String: Any], installed: Lookup = component) -> ToneHostEffect? {
        guard let codes = profile["plugin"] as? [String: Any], let type = codes["type"] as? String,
              let subtype = codes["subtype"] as? String, let manufacturer = codes["manufacturer"] as? String,
              let found = installed(type, subtype, manufacturer) else { return nil }
        return ToneHostEffect(name: profile["name"] as? String ?? found.name, type: type, subtype: subtype,
                              manufacturer: manufacturer, version: found.version)
    }

    static func stage(in folder: URL) -> URL { folder.appendingPathComponent("plugins/.adding.new", isDirectory: true) }

    // An empty stage, or a copy of the profile whose sounds are to change. Its levels were measured with the sounds
    // before, and its check played them
    static func startStage(in folder: URL, from id: String? = nil) -> Bool {
        let fm = FileManager.default, stage = stage(in: folder)
        try? fm.removeItem(at: stage)
        do {
            if let id {
                try fm.copyItem(at: folder.appendingPathComponent("plugins/\(id)", isDirectory: true), to: stage)
                for stale in ["levels.plist", "checked.json"] { try? fm.removeItem(at: stage.appendingPathComponent(stale)) }
            }
            try fm.createDirectory(at: stage.appendingPathComponent("states", isDirectory: true), withIntermediateDirectories: true)
            return true
        } catch { return false }
    }

    static func stateFile(_ sound: String, in folder: URL) -> URL {
        stage(in: folder).appendingPathComponent("states/\(sound).plist")
    }

    // The stage, checked by then, becomes plugins/ID and the plug-in chosen
    static func finishStage(in folder: URL, as id: String) -> Bool {
        let fm = FileManager.default, home = folder.appendingPathComponent("plugins/\(id)", isDirectory: true)
        let file = folder.appendingPathComponent("config.json")
        guard var config = ToneHostConfig.read(file) else { return false }
        try? fm.removeItem(at: home)
        guard (try? fm.moveItem(at: stage(in: folder), to: home)) != nil else { return false }
        choose(id, in: &config)
        return ToneHostConfig.write(config, to: file)
    }
}

// rs-tonehost run from here, always with RS Re-ARMed's network guard as start.sh has it
enum ToneHostTools {
    private static func guardEnvironment(_ folder: URL, log: String) -> [String: String] {
        ["DYLD_INSERT_LIBRARIES": folder.deletingLastPathComponent().appendingPathComponent("libnetguard.dylib").path,
         "RS_NETGUARD_LOG": folder.appendingPathComponent(log).path]
    }

    // The plug-in's window with the guitar through it; returns when the window has closed, which saves the state.
    // Through open, so that a question about the microphone is the host's own
    static func setSound(_ effect: ToneHostEffect, state: URL, in folder: URL) {
        let log = folder.appendingPathComponent("edit.log").path
        FileManager.default.createFile(atPath: log, contents: nil)
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        p.arguments = ["-n", "-W"] + guardEnvironment(folder, log: "netguard.edit.log").flatMap { ["--env", "\($0)=\($1)"] }
            + ["--stdout", log, "--stderr", log, folder.appendingPathComponent("RS Tone Host.app").path, "--args",
               "--config", folder.appendingPathComponent("config.json").path,
               "--edit", effect.type, effect.subtype, effect.manufacturer, state.path]
        guard (try? p.run()) != nil else { return }
        p.waitUntilExit()
    }

    // The host's --check-sound on a profile: nil when every sound played, or what it said
    static func checkSound(profile: URL, in folder: URL) -> String? {
        let p = Process(), said = Pipe()
        p.executableURL = folder.appendingPathComponent("RS Tone Host.app/Contents/MacOS/rs-tonehost")
        p.arguments = ["--config", profile.path, "--check-sound"]
        p.environment = ProcessInfo.processInfo.environment.merging(guardEnvironment(folder, log: "netguard.check.log")) { $1 }
        p.standardError = said
        p.standardOutput = FileHandle.nullDevice
        guard (try? p.run()) != nil else { return "The host couldn't be run." }
        let text = String(decoding: said.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        p.waitUntilExit()
        return p.terminationStatus == 0 ? nil : problem(in: text)
    }

    static func problem(in said: String) -> String {
        guard let line = said.components(separatedBy: "\n").last(where: { $0.contains("not set up: ") }),
              let reason = line.components(separatedBy: "not set up: ").last else { return "The check didn't finish." }
        let silent = reason.hasSuffix(" is silent")
        return "The check said: \(reason)." + (silent ? " Is the plug-in authorized, and does this sound play when you set it?" : "")
    }
}
