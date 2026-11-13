import SwiftUI

// "Add a Plug-in": the player picks an Audio Unit and sets a clean and a driven sound in its own window, with the
// guitar playing. With `changing`, the sounds of a profile made here before are set again
struct ToneHostAddSheet: View {
    let folder: URL
    let changing: String?
    let done: (_ added: Bool) -> Void

    @State private var effects: [ToneHostEffect] = []
    @State private var effect: ToneHostEffect?
    @State private var saved: Set<String> = []
    @State private var setting: String?  // the sound whose window is open
    @State private var checking = false
    @State private var message: String?

    private var busy: Bool { setting != nil || checking }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(changing == nil ? "Add a Plug-in" : "Change Its Sounds").font(.headline)
            if changing == nil {
                Picker("Plug-in", selection: Binding(get: { effect }, set: pick)) {
                    if effect == nil { Text("Choose…").tag(ToneHostEffect?.none) }
                    ForEach(effects) { Text($0.name).tag(ToneHostEffect?.some($0)) }
                }
                .disabled(busy)
                if effects.isEmpty {
                    Text("No Audio Unit effect is installed. Only Audio Units work, not plug-ins that are VST only.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            } else if let effect {
                Text(effect.name)
            }
            Text("Set two sounds. Each button opens the plug-in's window with your guitar playing through it: set the sound, then close the window to save it. The host plays the clean one for the game's clean tones and the driven one for the rest.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            row("clean", "Clean sound")
            row("driven", "Driven sound")
            if setting != nil {
                Text("Close the plug-in's window to save the sound. If nothing opens, macOS may be asking for the microphone.")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            if checking {
                HStack { ProgressView().controlSize(.small); Text("Playing both sounds through the plug-in to check them…") }
            }
            if let message {
                Text(message).font(.caption).foregroundStyle(.red).fixedSize(horizontal: false, vertical: true)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { close(false) }.disabled(busy)
                Button(changing == nil ? "Add" : "Save") { finish() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(busy || effect == nil || saved.count < ToneHostProfiles.sounds.count)
            }
        }
        .padding(20)
        .frame(width: 460)
        .onAppear(perform: start)
    }

    private func row(_ sound: String, _ label: String) -> some View {
        HStack {
            Image(systemName: saved.contains(sound) ? "checkmark.circle.fill" : "circle")
                .foregroundStyle(saved.contains(sound) ? Color.green : Color.secondary)
            Text(label)
            Spacer()
            Button(saved.contains(sound) ? "Change…" : "Set…") { set(sound) }.disabled(busy || effect == nil)
        }
    }

    private func start() {
        effects = ToneHostProfiles.effects()
        guard ToneHostProfiles.startStage(in: folder, from: changing) else {
            message = "The tonehost folder couldn't be written."
            return
        }
        if changing != nil {
            let profile = ToneHostConfig.read(ToneHostProfiles.stage(in: folder).appendingPathComponent("profile.json")) ?? [:]
            effect = ToneHostProfiles.effect(of: profile)
            if effect == nil { message = "The plug-in isn't installed." }
        }
        look()
    }

    private func look() {
        saved = Set(ToneHostProfiles.sounds.filter {
            FileManager.default.fileExists(atPath: ToneHostProfiles.stateFile($0, in: folder).path)
        })
    }

    // A sound saved from one plug-in is nothing to another
    private func pick(_ chosen: ToneHostEffect?) {
        guard chosen != effect else { return }
        effect = chosen
        message = nil
        _ = ToneHostProfiles.startStage(in: folder)
        look()
    }

    private func set(_ sound: String) {
        guard let effect else { return }
        let config = ToneHostConfig.read(folder.appendingPathComponent("config.json")) ?? [:]
        guard ToneHostConfig.chosen(config) != nil else {
            message = "Choose the interface your guitar is on first, in the settings behind this."
            return
        }
        message = nil
        setting = sound
        DispatchQueue.global(qos: .userInitiated).async {
            // The game's host has the interface, and the game starts another when it ends
            guard !ModsLibrary.gameRunning() else {
                DispatchQueue.main.async {
                    setting = nil
                    message = "Quit the game first: its host is playing through the interface."
                }
                return
            }
            ToneHostConfig.restartHost(commands: folder.appendingPathComponent("commands"))  // one left running ends
            ToneHostTools.setSound(effect, state: ToneHostProfiles.stateFile(sound, in: folder), in: folder)
            DispatchQueue.main.async {
                setting = nil
                look()
                if !saved.contains(sound) { message = "No sound was saved. The reason may be in edit.log in the tonehost folder." }
            }
        }
    }

    private func finish() {
        guard let effect else { return }
        let stage = ToneHostProfiles.stage(in: folder), file = stage.appendingPathComponent("profile.json")
        var profile = changing == nil ? ToneHostProfiles.profile(for: effect) : ToneHostConfig.read(file) ?? [:]
        profile["plugin_version"] = effect.version
        let taken = (try? FileManager.default.contentsOfDirectory(atPath: folder.appendingPathComponent("plugins").path)) ?? []
        let id = changing ?? ToneHostProfiles.newID(for: effect.shortName, taken: Set(taken))
        // Beside another profile of this plug-in, such as Helix Native's own setup, the name tells them apart
        if changing == nil, id != ToneHostProfiles.newID(for: effect.shortName, taken: []) {
            profile["name"] = "\(effect.shortName) (two sounds)"
        }
        guard ToneHostConfig.write(profile, to: file) else {
            message = "The profile couldn't be written."
            return
        }
        message = nil
        checking = true
        DispatchQueue.global(qos: .userInitiated).async {
            let problem = ToneHostTools.checkSound(profile: file, in: folder)
            DispatchQueue.main.async {
                checking = false
                if let problem {
                    message = problem
                    return
                }
                if ToneHostProfiles.finishStage(in: folder, as: id) { close(true) }
                else { message = "The profile couldn't be put in place." }
            }
        }
    }

    private func close(_ added: Bool) {
        if !added { try? FileManager.default.removeItem(at: ToneHostProfiles.stage(in: folder)) }
        done(added)
    }
}
