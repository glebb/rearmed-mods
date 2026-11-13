import AudioToolbox
import Foundation

// A plug-in rs-tonehost can play (tonehost/README.md): a profile in plugins/ID, or the one a config from before
// profiles has in itself, whose id is "". Only one with no problem can be chosen
struct ToneHostPlugin: Equatable, Identifiable {
    let id: String
    let name: String
    var problem: String?  // why it can't play
    var note: String?  // something to know about one that can
}

enum ToneHostProfiles {
    struct Installed: Equatable {
        let name: String
        let version: UInt32
    }

    typealias Lookup = (_ type: String, _ subtype: String, _ manufacturer: String) -> Installed?

    static func chosen(_ config: [String: Any]) -> String { config["profile"] as? String ?? "" }

    static func choose(_ id: String, in config: inout [String: Any]) { config["profile"] = id.isEmpty ? nil : id }

    // The config's own plug-in first, then the profiles by name. A folder starting with a dot is a setup under way
    static func all(in folder: URL, config: [String: Any], installed: Lookup = component) -> [ToneHostPlugin] {
        var found: [ToneHostPlugin] = []
        if config["plugin"] != nil {
            found.append(plugin(id: "", profile: config, states: folder, installed: installed))
        }
        let plugins = folder.appendingPathComponent("plugins", isDirectory: true)
        let ids = (try? FileManager.default.contentsOfDirectory(atPath: plugins.path)) ?? []
        for id in ids.sorted() where !id.hasPrefix(".") {
            let home = plugins.appendingPathComponent(id, isDirectory: true)
            var isFolder: ObjCBool = false
            guard FileManager.default.fileExists(atPath: home.path, isDirectory: &isFolder), isFolder.boolValue else { continue }
            guard let profile = ToneHostConfig.read(home.appendingPathComponent("profile.json")) else {
                found.append(ToneHostPlugin(id: id, name: id, problem: "Its profile can't be read."))
                continue
            }
            found.append(plugin(id: id, profile: profile, states: home, installed: installed))
        }
        return found
    }

    // The same checks as rs-tonehost's config_problem, none of which opens the plug-in
    static func plugin(id: String, profile: [String: Any], states: URL, installed: Lookup) -> ToneHostPlugin {
        let codes = profile["plugin"] as? [String: Any] ?? [:]
        let type = codes["type"] as? String ?? "", subtype = codes["subtype"] as? String ?? ""
        let manufacturer = codes["manufacturer"] as? String ?? ""
        let component = [type, subtype, manufacturer].allSatisfy { $0.utf8.count == 4 }
            ? installed(type, subtype, manufacturer) : nil
        let name = profile["name"] as? String ?? component?.name ?? (id.isEmpty ? "The plug-in in config.json" : id)
        var plugin = ToneHostPlugin(id: id, name: name)
        guard [type, subtype, manufacturer].allSatisfy({ $0.utf8.count == 4 }) else {
            plugin.problem = "It names no plug-in."
            return plugin
        }
        guard let component else {
            plugin.problem = "The plug-in isn't installed."
            return plugin
        }
        guard let templates = profile["templates"] as? [String: Any], !templates.isEmpty else {
            plugin.problem = "It has no sounds."
            return plugin
        }
        let map = profile["map"] as? [String: Any] ?? [:]
        var named = (map["by_prefix"] as? [String: Any] ?? [:]).values.map { $0 as? String }
        named += [map["clean"] as? String, map["driven"] as? String]
        guard named.allSatisfy({ $0.map { templates[$0] != nil } ?? false }) else {
            plugin.problem = "Its rules name a sound that isn't there."
            return plugin
        }
        let want = [fourCC(type), fourCC(subtype), fourCC(manufacturer)]
        var missing = 0, foreign = 0
        for template in templates.values {
            let path = (template as? [String: Any])?["state"] as? String ?? ""
            let file = path.hasPrefix("/") ? URL(fileURLWithPath: path) : states.appendingPathComponent(path)
            guard !path.isEmpty, let state = NSDictionary(contentsOf: file) else {
                missing += 1
                continue
            }
            let has = ["type", "subtype", "manufacturer"].map { (state[$0] as? NSNumber)?.uint32Value }
            if zip(has, want).contains(where: { $0 != nil && $0 != $1 }) { foreign += 1 }
        }
        if missing > 0 {
            plugin.problem = missing == templates.count ? "Its sounds aren't saved."
                : "\(missing) of its \(templates.count) sounds aren't saved."
        } else if foreign > 0 {
            plugin.problem = "Its sounds were saved from another plug-in."
        } else if let made = (profile["plugin_version"] as? NSNumber)?.uint32Value, made != component.version {
            plugin.note = "Set up with version \(version(made)) of the plug-in, and \(version(component.version)) is installed. "
                + "If tones sound wrong, set it up again."
        }
        return plugin
    }

    // An Audio Unit's version as its maker writes it: 0x00030802 is 3.8.2
    static func version(_ v: UInt32) -> String { "\(v >> 16).\(v >> 8 & 0xff).\(v & 0xff)" }

    static func fourCC(_ s: String) -> UInt32 { s.utf8.reduce(0) { $0 << 8 | UInt32($1) } }

    // Asks the system's list of Audio Units: nothing is opened, so no licence check and no window
    static func component(type: String, subtype: String, manufacturer: String) -> Installed? {
        var d = AudioComponentDescription(componentType: fourCC(type), componentSubType: fourCC(subtype),
                                          componentManufacturer: fourCC(manufacturer), componentFlags: 0,
                                          componentFlagsMask: 0)
        guard let found = AudioComponentFindNext(nil, &d) else { return nil }
        var name: Unmanaged<CFString>?
        var version: UInt32 = 0
        AudioComponentCopyName(found, &name)
        AudioComponentGetVersion(found, &version)
        return Installed(name: (name?.takeRetainedValue() as String?) ?? "\(manufacturer) \(subtype)", version: version)
    }
}
