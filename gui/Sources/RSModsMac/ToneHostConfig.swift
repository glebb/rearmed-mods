import Foundation

// rs-tonehost's interface in its config.json (tonehost/README.md): "input" and "output" are the one chosen, and
// "setups" keeps every one chosen before, which the host falls back to when the chosen one is unplugged
struct ToneHostSetup: Equatable {
    var device: String
    var channel = 1  // from 1 here, from 0 in the file
    var gainDb = 6.0
    var output: String?  // nil: the system's output
}

enum ToneHostConfig {
    static func chosen(_ config: [String: Any]) -> ToneHostSetup? { setup(config) }

    static func remembered(_ device: String, in config: [String: Any]) -> ToneHostSetup? {
        (config["setups"] as? [[String: Any]] ?? []).compactMap(setup).first { $0.device == device }
    }

    // Other keys of "input" stay; the setup takes the place of the one kept for its device, or joins the end
    static func choose(_ s: ToneHostSetup, in config: inout [String: Any]) {
        var input = config["input"] as? [String: Any] ?? [:]
        input["device"] = s.device
        input["channel"] = s.channel - 1
        input["gain_db"] = s.gainDb
        config["input"] = input
        config["output"] = s.output.map { ["device": $0] }
        var entry: [String: Any] = ["input": input]
        if let output = s.output { entry["output"] = ["device": output] }
        var setups = config["setups"] as? [[String: Any]] ?? []
        if let i = setups.firstIndex(where: { setup($0)?.device == s.device }) { setups[i] = entry }
        else { setups.append(entry) }
        config["setups"] = setups
    }

    private static func setup(_ entry: [String: Any]) -> ToneHostSetup? {
        guard let input = entry["input"] as? [String: Any], let device = input["device"] as? String,
              !device.isEmpty else { return nil }
        return ToneHostSetup(device: device, channel: max(0, (input["channel"] as? NSNumber)?.intValue ?? 0) + 1,
                             gainDb: (input["gain_db"] as? NSNumber)?.doubleValue ?? 0,
                             output: (entry["output"] as? [String: Any])?["device"] as? String)
    }

    static func read(_ file: URL) -> [String: Any]? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }

    static func write(_ config: [String: Any], to file: URL) -> Bool {
        guard let data = try? JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys])
        else { return false }
        return (try? data.write(to: file, options: .atomic)) != nil
    }

    // Ends a running host, which then starts again with the new config: the game starts one whenever it has none.
    // Nobody reading the FIFO means no host, and the open fails instead of waiting
    static func restartHost(commands: URL) {
        let fd = open(commands.path, O_WRONLY | O_NONBLOCK)
        guard fd >= 0 else { return }
        _ = "quit\n".withCString { Darwin.write(fd, $0, 5) }
        close(fd)
    }
}
