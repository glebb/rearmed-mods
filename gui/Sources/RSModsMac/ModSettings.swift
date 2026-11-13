import SwiftUI

// A mod's settings, shown under its switch while the mod is on
struct ModSettings: View {
    @EnvironmentObject var store: SettingsStore
    let mod: String

    // The mods with a case below
    static let mods: Set<String> = [
        "ToggleLoft", "Headstock", "Skyline", "Lyrics", "ShowSongTimer", "CameraDirector", "Sunlight", "ExtendedRange",
        "ForceReEnumeration", "ForceProfileLoad", "UseCustomNSPTimer", "RRSpeedAboveOneHundred", "VolumeControl",
        "AllowRewind", "AllowLooping", "OverrideInputVolumeEnabled", "ToneHost", "SongStems", "SecondaryMonitor",
        "BackupProfile", "GuitarSpeak", "MoreSongLists", "CustomSongListTitles", "CustomTunings", "DefaultTones",
        "CustomStringColors", "SeparateNoteColors", "CustomHighwayColors", "SevenString",
    ]

    var body: some View {
        switch mod {
        case "ToggleLoft":
            WhenPicker(key: "ToggleLoftWhen", options: [
                ("startup", "As soon as the game starts"),
                ("song", "Only when in a song"),
                ("manual", "Manually, with the Toggle Loft key"),
            ])
        case "Headstock":
            WhenPicker(key: "RemoveHeadstockWhen", options: [
                ("startup", "As soon as the game starts"),
                ("song", "Only when in a song"),
            ])
        case "Skyline":
            WhenPicker(key: "ToggleSkylineWhen", options: [
                ("startup", "As soon as the game starts"),
                ("song", "Only when in a song"),
            ])
        case "Lyrics":
            WhenPicker(key: "RemoveLyricsWhen", options: [
                ("startup", "As soon as the game starts"),
                ("manual", "Manually, with the Remove Lyrics key"),
            ])
        case "ShowSongTimer":
            WhenPicker(key: "ShowSongTimerWhen", options: [
                ("automatic", "Always, in every song"),
                ("manual", "Manually, with the Show Song Timer key"),
            ])
        case "CameraDirector": CameraDirectorSettings()
        case "Sunlight": SunlightSettings()
        case "ExtendedRange": ExtendedRangeSettings()
        case "ForceReEnumeration": EnumerationSettings()
        case "ForceProfileLoad": ProfileLoadSettings()
        case "UseCustomNSPTimer":
                NumberField(label: "Time between songs in Non-Stop Play",
                            value: store.number("CustomNSPTimeLimit", scale: 1000),
                            range: 2...60, step: 0.1, unit: "seconds")
        case "RRSpeedAboveOneHundred":
                NumberField(label: "Each press changes the speed by", value: store.number("RRSpeedInterval"),
                            range: 1...100, unit: "%")
        case "VolumeControl":
                NumberField(label: "Each press changes the volume by", value: store.number("VolumeControlInterval"),
                            range: 1...100, unit: "%")
        case "AllowRewind":
                NumberField(label: "Rewind By", value: store.number("RewindBy", scale: 1000),
                            range: 0...120, step: 0.5, unit: "seconds")
                NumberField(label: "Rewind Lead Up", value: store.number("RewindLeadup", scale: 1000),
                            range: 0...60, step: 0.5, unit: "seconds")
        case "AllowLooping":
                NumberField(label: "Lead before the loop starts", value: store.number("LoopingLeadUp", scale: 1000),
                            range: 0...60, step: 0.5, unit: "seconds")
        case "OverrideInputVolumeEnabled": OverrideInputSettings()
        case "ToneHost": ToneHostSettings()
        case "SongStems": SongStemsSettings()
        case "SecondaryMonitor": SecondaryMonitorSettings()
        case "BackupProfile": BackupSettings()
        case "GuitarSpeak": GuitarSpeakSettings()
        case "MoreSongLists": MoreSongListsSettings()
        case "CustomSongListTitles": SongListTitleSettings()
        case "CustomTunings": TuningSettings()
        case "DefaultTones": ToneSettings()
        case "CustomStringColors": StringColorGrid(prefix: "string")
        case "SeparateNoteColors": StringColorGrid(prefix: "note")
        case "CustomHighwayColors": HighwayColorSettings()
        case "SevenString": SevenStringSettings()
        default: EmptyView()
        }
    }
}

private struct CameraDirectorSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        CameraSlider(label: "Zoom", key: "CameraZoom", range: 60...160)
        CameraSlider(label: "Height", key: "CameraHeight", range: -10...20)
        CameraSlider(label: "Side", key: "CameraSide", range: -20...20)
        CameraSlider(label: "Tilt", key: "CameraTilt", range: -15...15)
        HStack {
            Text("In a song the keys change these; Control + a key goes the other way.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Spacer()
            Button("Game's View") {
                for (key, value) in [("CameraZoom", "100"), ("CameraHeight", "0"), ("CameraSide", "0"),
                                     ("CameraTilt", "0")] {
                    store.set(Section.settings, key, value)
                }
            }
        }
    }
}

private struct SunlightSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Picker("", selection: store.binding(Section.settings, "SunlightMode")) {
            Text("Off").tag("0")
            Text("Bright: lifts the picture, every color at one luminance").tag("1")
            Text("Daylight: in a song, a white highway with dark notes").tag("2")
        }
        .pickerStyle(.radioGroup)
        .labelsHidden()
        Text("The sunlight key goes through these in the game. In a song the venue goes too. Needs the Metal renderer.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct ExtendedRangeSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Picker("Lowest string at or below", selection: store.binding(Section.settings, "ExtendedRangeModeAt")) {
            ForEach(Catalog.extendedRangeTunings, id: \.value) { Text($0.label).tag($0.value) }
        }
        Picker("Colors", selection: store.binding(Section.toggles, "CustomStringColors")) {
            Text("The game's colorblind colors").tag("0")
            Text("ZZ's colors").tag("1")
            Text("Your colorblind string colors (Colors)").tag("2")
        }
        Toggle("Drop tunings count from their tuning (C# Drop B at B)",
               isOn: store.toggle(ModToggle(label: "", key: "ExtendedRangeDropTuning", help: "")))
        Toggle("A bass charted with its top two strings at E counts by its four",
               isOn: store.toggle(ModToggle(label: "", key: "ExtendedRangeFixBassTuning", help: "")))
        Text("In a song tuned this low, and in its tuner. The tuning is read from the song's archive. The Toggle Extended Range key turns it on or off for a song.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct EnumerationSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Picker("", selection: Binding(get: { store.enumerationMode },
                                      set: { store.enumerationMode = $0 })) {
            Text("Manually, with the Force ReEnumeration key").tag("manual")
            Text("Automatically, when songs are added to dlc").tag("automatic")
        }
        .pickerStyle(.radioGroup)
        .labelsHidden()
        NumberField(label: "Check for new songs every",
                    value: store.number("CheckForNewSongsInterval", scale: 1000),
                    range: 1...600, unit: "seconds")
            .disabled(store.enumerationMode != "automatic")
    }
}

private struct ProfileLoadSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        HStack {
            Text("Profile")
            TextField("The last used one", text: store.binding(Section.toggles, "ProfileToLoad"))
        }
        Text("At startup, the game goes into this profile, by its name in the game; left empty, into the last used one (the top of the profile list).")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct OverrideInputSettings: View {
    @EnvironmentObject var store: SettingsStore
    @State private var devices: [String] = []

    var body: some View {
        Group {
            HStack {
                Text("Device")
                TextField("Default input", text: store.binding(Section.toggles, "OverrideInputVolumeDevice"))
                Menu("Choose") {
                    ForEach(devices, id: \.self) { name in
                        Button(name) { store.set(Section.toggles, "OverrideInputVolumeDevice", name) }
                    }
                    Divider()
                    Button("Default input") { store.set(Section.toggles, "OverrideInputVolumeDevice", "") }
                }
                .fixedSize()
            }
            HStack {
                Text("Volume")
                Slider(value: store.number("OverrideInputVolume"), in: 0...100, step: 1)
                Text("\(Int(store.number("OverrideInputVolume").wrappedValue))%")
                    .monospacedDigit().frame(width: 40, alignment: .trailing)
            }
            Text("17% is the game's default. The device can be part of its name, e.g. Rocksmith. Several devices: separate them with ;, each optionally with its own percent (Rocksmith; OBS = 100).")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .onAppear { devices = AudioDevices.inputNames() }
    }
}

private struct ToneHostSettings: View {
    @EnvironmentObject var store: SettingsStore
    @State private var interfaces: [AudioDevices.Interface] = []
    @State private var setup: ToneHostSetup?
    @State private var plugins: [ToneHostPlugin] = []
    @State private var plugin = ""
    @State private var failed = false

    private var file: URL { store.toneHostFolder.appendingPathComponent("config.json") }

    var body: some View {
        Label(store.toneHostInstalled ? "rs-tonehost is installed." : "rs-tonehost isn't installed.",
              systemImage: store.toneHostInstalled ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
            .foregroundStyle(store.toneHostInstalled ? Color.green : Color.orange)
        if store.toneHostInstalled {
            let ready = plugins.filter { $0.problem == nil }
            let playing = plugins.first { $0.id == plugin }
            if ready.isEmpty {
                Label("No plug-in is set up, so the game keeps its own tones.", systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(Color.orange)
            } else {
                Picker("Plug-in", selection: Binding(get: { plugin }, set: choosePlugin)) {
                    if playing?.problem != nil || playing == nil {
                        Text(playing.map { "\($0.name) (not set up)" } ?? "None").tag(plugin)
                    }
                    ForEach(ready) { Text($0.name).tag($0.id) }
                }
            }
            if let note = playing?.note {
                Text(note).font(.caption).foregroundStyle(.orange).fixedSize(horizontal: false, vertical: true)
            }
            ForEach(plugins.filter { $0.problem != nil }) { p in
                Text("\(p.name) can't be chosen. \(p.problem ?? "")")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
        }
        if store.toneHostInstalled, let setup {
            let connected = interfaces.first { $0.name == setup.device }
            Picker("Interface", selection: Binding(get: { setup.device }, set: choose)) {
                if connected == nil { Text("\(setup.device) (not connected)").tag(setup.device) }
                ForEach(interfaces.filter { $0.inputs > 0 }, id: \.name) { Text($0.name).tag($0.name) }
            }
            Picker("Guitar on channel", selection: Binding(get: { setup.channel }, set: { n in
                change { $0.channel = n }
            })) {
                ForEach(1...max(connected?.inputs ?? 1, setup.channel), id: \.self) { Text(String($0)).tag($0) }
            }
            HStack {
                Text("Gain")
                Slider(value: Binding(get: { setup.gainDb }, set: { self.setup?.gainDb = $0 }), in: -24...36, step: 1,
                       onEditingChanged: { editing in if !editing { save() } })
                Text("\(Int(setup.gainDb)) dB").monospacedDigit().frame(width: 48, alignment: .trailing)
            }
            Picker("Sound to", selection: Binding(get: { setup.output }, set: { name in
                change { $0.output = name }
            })) {
                Text("The system's output").tag(String?.none)
                if let output = setup.output, !interfaces.contains(where: { $0.name == output }) {
                    Text("\(output) (not connected)").tag(String?.some(output))
                }
                ForEach(interfaces.filter { $0.outputs > 0 }, id: \.name) { Text($0.name).tag(String?.some($0.name)) }
            }
            if failed {
                Text("The host's config couldn't be written.").font(.caption).foregroundStyle(.red)
            }
        }
        Text(store.toneHostInstalled
             ? "The game starts it, and it plays the guitar through the plug-in chosen here, following the game's tones. A plug-in appears here once its setup has finished: tonehost in rearmed-mods' source has the steps. The game's own guitar is silent while it runs; the guitar fader and the volume keys still work. The sound comes soonest with the guitar and the sound on one interface. With this interface unplugged the host takes another you've chosen here before that is connected. A change restarts a running host: the game's own tones play until it's back, within half a minute."
             : "Until it is, the game keeps its own tones. Set it up with tonehost in rearmed-mods' source.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
            .onAppear {
                interfaces = AudioDevices.interfaces()
                let config = ToneHostConfig.read(file) ?? [:]
                setup = ToneHostConfig.chosen(config)
                plugins = ToneHostProfiles.all(in: store.toneHostFolder, config: config)
                plugin = ToneHostProfiles.chosen(config)
            }
    }

    private func choosePlugin(_ id: String) {
        guard var config = ToneHostConfig.read(file) else { failed = true; return }
        ToneHostProfiles.choose(id, in: &config)
        write(config)
        if !failed { plugin = id }
    }

    // An interface chosen before comes back as it was; a new one starts on channel 1, with its own output if it has one
    private func choose(_ device: String) {
        let config = ToneHostConfig.read(file) ?? [:]
        let own = interfaces.contains { $0.name == device && $0.outputs > 0 } ? device : nil
        setup = ToneHostConfig.remembered(device, in: config) ?? ToneHostSetup(device: device, output: own)
        save()
    }

    private func change(_ edit: (inout ToneHostSetup) -> Void) {
        guard var s = setup else { return }
        edit(&s)
        setup = s
        save()
    }

    private func save() {
        guard let setup, var config = ToneHostConfig.read(file) else { failed = true; return }
        ToneHostConfig.choose(setup, in: &config)
        write(config)
    }

    private func write(_ config: [String: Any]) {
        failed = !ToneHostConfig.write(config, to: file)
        if !failed { ToneHostConfig.restartHost(commands: store.toneHostFolder.appendingPathComponent("commands")) }
    }
}

private struct SongStemsSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Label(store.stemsInstalled ? "rs-stems is installed." : "rs-stems isn't installed.",
              systemImage: store.stemsInstalled ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
            .foregroundStyle(store.stemsInstalled ? Color.green : Color.orange)
        HStack {
            Text("Guitar in the song")
            Slider(value: store.number("SongStemsGuitarLevel"), in: 0...100, step: 5)
            Text("\(Int(store.number("SongStemsGuitarLevel").wrappedValue))%")
                .monospacedDigit().frame(width: 40, alignment: .trailing)
        }
        HStack {
            Text("Bass in the song")
            Slider(value: store.number("SongStemsBassLevel"), in: 0...100, step: 5)
            Text("\(Int(store.number("SongStemsBassLevel").wrappedValue))%")
                .monospacedDigit().frame(width: 40, alignment: .trailing)
        }
        NumberField(label: "Keep songs up to", value: store.number("SongStemsCacheMB"),
                    range: 50...20000, step: 50, unit: "MB")
        Text(store.stemsInstalled
             ? "0% takes the part out, 100% is the song as it is. A bass tone takes out the bass, any other tone the guitar. The level is also a line of the volume mixer in the game, with its own key, and stays for the next songs. Each song is separated once, a few seconds before it starts, and kept; the songs you play most stay when the space runs out."
             : ModsLibrary.macOSRunsStems
                ? "Until it is, the songs stay as they are. Installing the mods installs it too, when this app carries it."
                : "It needs macOS 15 or later; until then the songs stay as they are.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct SecondaryMonitorSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        HStack {
            Menu("Choose Screen") {
                ForEach(Array(NSScreen.screens.enumerated()), id: \.offset) { _, screen in
                    Button(screen.localizedName) {
                        let corner = Screens.topLeft(of: screen)
                        store.set(Section.settings, "SecondaryMonitorXPosition", String(corner.x))
                        store.set(Section.settings, "SecondaryMonitorYPosition", String(corner.y))
                    }
                }
            }
            .fixedSize()
            Spacer()
        }
        NumberField(label: "Left", value: store.number("SecondaryMonitorXPosition"), range: -20000...20000,
                    unit: "points")
        NumberField(label: "Top", value: store.number("SecondaryMonitorYPosition"), range: -20000...20000,
                    unit: "points")
        Text("The screen's top left corner, from the top left of the main screen. The game's window moves there when the game starts; best with the game in a window, at that screen's size.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct BackupSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        NumberField(label: "Backups kept from opening this app", value: store.number(
            "NumberOfBackups", section: Section.guiSettings), range: 1...500, unit: "")
        HStack {
            Button("Back Up Now") { store.backUpProfiles(evenIfOff: true) }
            Button("Show Backups") {
                NSWorkspace.shared.open(FileManager.default.fileExists(atPath: store.backupsFolder.path)
                                        ? store.backupsFolder : store.gameFolder)
            }
        }
        Text("In Profile_AutoBackups in the game folder: one when this app opens, and while the game runs one every 10 minutes when the profiles changed (the last hour, 6 hourly, 4 quarter-daily and 30 daily are kept). To restore one, quit the game and copy its files back into the save folder (howto.txt says where).")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

private struct GuitarSpeakSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
            ForEach(Catalog.guitarSpeakKeys, id: \.key) { row in
                GridRow {
                    Text(row.label)
                    Picker("", selection: store.binding(Section.guitarSpeak, row.key)) {
                        Text("None").tag("0")
                        ForEach(24...96, id: \.self) { midi in
                            Text("\(Catalog.noteName(midi)) (\(midi))").tag(String(midi))
                        }
                    }
                    .labelsHidden()
                    .frame(width: 120)
                }
            }
        }
        Toggle("Also in the tuners (wrong notes can get in the way of tuning)",
               isOn: Binding(get: { store.value(Section.guitarSpeak, "GuitarSpeakWhileTuning").lowercased() == "on" },
                             set: { store.set(Section.guitarSpeak, "GuitarSpeakWhileTuning", $0 ? "on" : "off") }))
            .toggleStyle(.checkbox)
        Text("In the menus, each new note you play presses the key set to it (E3, the low E's 12th fret, is 52). Off in songs, lessons and calibration. The notes are the ones the game hears; needs RS Re-ARMed 1.11.0.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

// More Song Lists (RSMods' app's Profile Edits: Add New SongList, Remove Newest SongList), which the library applies
// to the profiles
private struct MoreSongListsSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        NumberField(label: "Song lists in each profile", value: store.number("NumberOfSongLists"),
                    range: 6...20, unit: "")
        Text("When the game starts, each of your profiles gets this many song lists: empty ones after the last, or with a lower number the last ones go while they are empty (a list with songs stays: take its songs out in the game first). Learn a Song and the song list editor show them; Non-Stop Play offers the first six.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

// [SongListTitles]: RSMods' song list names, SongListTitle_1 to _20 (its app's "Song Lists" box)
private struct SongListTitleSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        titles(1...6)
        DisclosureGroup("Song lists 7 to 20") {
            titles(7...20).padding(.top, 4)
        }
        Text("The names the game's menus give your song lists: such as Learn a Song's song list filter and Non-Stop Play's choice of list. A list left empty keeps the game's name. Lists 7 to 20 are in a profile when More Song Lists (or RSMods' app on Windows) added them. Needs RS Re-ARMed 1.11.0.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func titles(_ lists: ClosedRange<Int>) -> some View {
        Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
            ForEach(Array(lists), id: \.self) { n in
                GridRow {
                    Text("Song List \(n)")
                    TextField("SONG LIST \(n)", text: store.binding(Section.songListTitles, "SongListTitle_\(n)"))
                }
            }
        }
    }
}

// [Custom Tunings]: the tunings the game names, a name and six strings each (RSMods' app's Custom Tunings tab)
private struct TuningSettings: View {
    @EnvironmentObject var store: SettingsStore

    private var tunings: [(key: String, value: String)] { store.entries(Section.customTunings) }
    private var missingCommon: [(name: String, strings: [Int])] {
        let have = Set(tunings.map { $0.key.lowercased() })
        return Catalog.commonTunings.filter { !have.contains($0.name.lowercased()) }
    }

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 6, verticalSpacing: 4) {
            GridRow {
                Text("Name")
                ForEach(Catalog.stringNames, id: \.self) { Text($0) }
                Text("Notes")
                Text("")
            }
            .font(.caption)
            .foregroundStyle(.secondary)
            ForEach(Array(tunings.enumerated()), id: \.offset) { i, t in
                GridRow {
                    TextField("Name", text: name(i)).frame(width: 120)
                    ForEach(0..<6, id: \.self) { s in
                        TextField("", value: string(i, s), format: .number)
                            .multilineTextAlignment(.trailing)
                            .frame(width: 34)
                    }
                    Text(Catalog.tuningStrings(t.value).map(Catalog.tuningNotes) ?? "not six numbers")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Button {
                        var all = tunings
                        all.remove(at: i)
                        store.setEntries(Section.customTunings, all)
                    } label: {
                        Image(systemName: "minus.circle")
                    }
                    .buttonStyle(.borderless)
                    .help("Remove this tuning")
                }
            }
        }
        HStack {
            Button("Add Tuning") {
                store.setEntries(Section.customTunings, tunings + [("New Tuning", "0 0 0 0 0 0")])
            }
            Button("Add Common Tunings") {
                store.setEntries(Section.customTunings,
                                 tunings + missingCommon.map { ($0.name, Catalog.tuningValue($0.strings)) })
            }
            .disabled(missingCommon.isEmpty)
            .help(missingCommon.map(\.name).joined(separator: ", "))
        }
        Text("The name the game shows for a song's arrangement in that tuning, where it showed CUSTOM TUNING: in the song lists, the song's details and the tuner. Each string is its semitones from E standard, low E first: B Standard is -5 on all six. A tuning with the same strings as one of the game's own takes its place. From the game's next start.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }

    // A name can't hold '=', nor start like a comment or a section
    private func name(_ i: Int) -> Binding<String> {
        Binding(get: { i < tunings.count ? tunings[i].key : "" }, set: { new in
            guard i < tunings.count else { return }
            var all = tunings
            var clean = new.replacingOccurrences(of: "=", with: "")
            while let c = clean.first, c == ";" || c == "#" || c == "[" || c == " " { clean.removeFirst() }
            all[i].key = clean
            store.setEntries(Section.customTunings, all)
        })
    }

    private func string(_ i: Int, _ s: Int) -> Binding<Int> {
        Binding(get: {
            guard i < tunings.count, let strings = Catalog.tuningStrings(tunings[i].value) else { return 0 }
            return strings[s]
        }, set: { v in
            guard i < tunings.count else { return }
            var all = tunings
            var strings = Catalog.tuningStrings(all[i].value) ?? [0, 0, 0, 0, 0, 0]
            strings[s] = max(-24, min(24, v))
            all[i].value = Catalog.tuningValue(strings)
            store.setEntries(Section.customTunings, all)
        })
    }
}

// [Default Tones]: the game's default tones, from the tones saved in the profiles (RSMods' app's Change Default Tones
// and Change Default Guitarcade Tones)
private struct ToneSettings: View {
    @EnvironmentObject var store: SettingsStore

    private var tones: [(name: String, bass: Bool)] { store.profileTones }

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
            ForEach(Catalog.toneSlots, id: \.key) { slot in
                GridRow {
                    Text(slot.label)
                    picker(slot.key, tones.filter { $0.bass == slot.bass }.map(\.name))
                }
            }
        }
        DisclosureGroup("Guitarcade") {
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
                ForEach(Catalog.guitarcadeTones, id: \.key) { game in
                    GridRow {
                        Text(game.label)
                        picker(game.key, tones.filter { !$0.bass }.map(\.name))
                    }
                }
            }
            .padding(.top, 4)
        }
        Text((tones.isEmpty ? "No saved tones are listed yet: the list is made from your profiles when the game starts with the mods. " : "")
             + "A tone you saved in one of your profiles (the tone designer's Save) for each of the game's: Rhythm, Lead and Bass are the tones on slot 1 of the tone stick when the game loads up. A tone saved since the game last started is listed after its next start. From the game's next start.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func picker(_ key: String, _ names: [String]) -> some View {
        let current = store.value(Section.defaultTones, key)
        return Picker("", selection: store.binding(Section.defaultTones, key)) {
            Text("The game's tone").tag("")
            ForEach(names, id: \.self) { Text($0).tag($0) }
            if !current.isEmpty && !names.contains(current) {
                Text("\(current) (not in the profiles)").tag(current)
            }
        }
        .labelsHidden()
        .frame(width: 220)
    }
}

private struct HighwayColorSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 6) {
            ForEach(Catalog.highwayColors, id: \.key) { row in
                GridRow {
                    Text(row.label)
                    ColorCell(section: Section.highwayColors, key: row.key, fallback: "808080")
                }
            }
        }
        Text("A color left at Game's stays the game's. Numbered and unnumbered lanes go together: set both.")
            .font(.caption)
            .foregroundStyle(.secondary)
    }
}

// Seven-String Guitar's settings ([Seven String]): the low B's color, chord detection, tuning names and the headstock
private struct SevenStringSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 6) {
            GridRow {
                Text("")
                Text("Normal").font(.caption).foregroundStyle(.secondary)
                Text("Colorblind").font(.caption).foregroundStyle(.secondary)
            }
            GridRow {
                Text("Low B string")
                ColorCell(section: Section.sevenString, key: "string6_N", fallback: Catalog.lowBColor,
                          showsGame: false, resetHelp: "Back to light grey")
                ColorCell(section: Section.sevenString, key: "string6_CB",
                          fallback: store.colorValue(Section.sevenString, "string6_N") ?? Catalog.lowBColor,
                          showsGame: false, resetHelp: "Back to the normal color")
            }
        }
        Toggle("With Extended Range on, the low B takes its low string color", isOn: onOff("ExtendedRangeColor"))
        Divider()
        Picker("A 7- or 8-note chord counts as played", selection: store.binding(Section.sevenString, "ChordWideMisses")) {
            Text("Only with every note").tag("0")
            Text("With up to one note missed").tag("1")
            Text("With up to two notes missed").tag("2")
        }
        Divider()
        Toggle("Names for 7-string tunings", isOn: onOff("TuningNames"))
        HStack {
            Text("Before")
            TextField("", text: store.binding(Section.sevenString, "TuningNamePrefix")).frame(width: 80)
            Text("After")
            TextField("", text: store.binding(Section.sevenString, "TuningNameSuffix")).frame(width: 80)
            Text(example).font(.callout.monospaced()).foregroundStyle(.secondary)
        }
        .disabled(!onOff("TuningNames").wrappedValue)
        Divider()
        Toggle("Seven-peg headstock", isOn: onOff("Headstock7"))
        HStack {
            Text("Its wood")
            Slider(value: wood, in: 0.05...1, step: 0.05)
            Text(String(format: "%.2f", wood.wrappedValue)).font(.callout.monospaced()).frame(width: 40)
        }
        .disabled(!onOff("Headstock7").wrappedValue)
        Text("For 7-string charts, such as DotCharter's: a seventh string, its notes and chords, the fret grid, tuner and chord diagrams over seven strings, after rs7string. Six-string songs and bass stay as the game has them. The color changes while the game runs; the rest from its next start. DotCharter's unencrypted charts need Plain Packages too.")
            .font(.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }

    // An on/off key of [Seven String], on unless it says off
    private func onOff(_ key: String) -> Binding<Bool> {
        Binding(get: { store.value(Section.sevenString, key).lowercased() != "off" },
                set: { store.set(Section.sevenString, key, $0 ? "on" : "off") })
    }

    private var wood: Binding<Double> {
        Binding(get: { Double(store.value(Section.sevenString, "Headstock7Wood")) ?? 0.6 },
                set: { store.set(Section.sevenString, "Headstock7Wood", String(format: "%.2f", $0)) })
    }

    // B STANDARD as the game names it with the prefix and suffix
    private var example: String {
        let prefix = store.value(Section.sevenString, "TuningNamePrefix").uppercased()
        let suffix = store.value(Section.sevenString, "TuningNameSuffix").uppercased()
        return (prefix.isEmpty ? "" : prefix + " ") + "B STANDARD" + (suffix.isEmpty ? "" : " " + suffix)
    }
}
