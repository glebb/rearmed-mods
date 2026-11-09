import SwiftUI

// The tabs of the Windows GUI that apply on macOS: "Enable / Disable Mods" (the mods and their settings side by
// side), "Colors" and "Keybindings".
struct ContentView: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        VStack(spacing: 0) {
            LibraryBar()
            if let problem = store.problem {
                HStack {
                    Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange)
                    Text(problem)
                    Spacer()
                    Button("Choose Game Folder…", action: store.chooseGameFolder)
                }
                .padding(10)
                .background(Color.orange.opacity(0.12))
            }
            TabView {
                ModTogglesTab().tabItem { Text("Enable / Disable Mods") }
                ColorsTab().tabItem { Text("Colors") }
                KeybindingsTab().tabItem { Text("Keybindings") }
            }
            .padding(12)
            .disabled(!store.folderExists)
            Divider()
            FooterView()
        }
        .frame(minWidth: 860, minHeight: 600)
        .task { store.backUpProfiles() }  // Backup Profile: when the app opens, as RSMods' app does
    }
}

// The mods library: installed? up to date? Install, update or remove it.
struct LibraryBar: View {
    @EnvironmentObject var store: SettingsStore
    @EnvironmentObject var library: ModsLibrary

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: icon).foregroundStyle(color)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).fontWeight(.medium)
                Text(library.message ?? detail).font(.callout).foregroundStyle(.secondary)
            }
            Spacer()
            switch library.state {
            case .notInstalled:
                Button("Install Mods") { library.install(gameFolder: store.gameFolder) }.keyboardShortcut(.defaultAction)
            case .updateAvailable:
                Button("Update Mods") { library.install(gameFolder: store.gameFolder) }.keyboardShortcut(.defaultAction)
                Button("Remove") { library.uninstall(gameFolder: store.gameFolder) }
            case .installed, .newerInstalled:
                Button("Remove Mods") { library.uninstall(gameFolder: store.gameFolder) }
            case .noRearmed:
                Link("Get RS Re-ARMed", destination: URL(string: "https://github.com/bbelgg/rs-rearmed")!)
            case .rearmedTooOld:
                Link("Update RS Re-ARMed", destination: URL(string: "https://github.com/bbelgg/rs-rearmed/releases")!)
            case .noBundledLibrary:
                EmptyView()
            }
        }
        .padding(10)
        .background(color.opacity(0.10))
        .onAppear { library.refresh(gameFolder: store.gameFolder) }
        .onChange(of: store.gameFolder) { _ in library.refresh(gameFolder: store.gameFolder) }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            library.refresh(gameFolder: store.gameFolder)
        }
    }

    private var version: String { library.bundledVersion ?? "" }

    private var title: String {
        switch library.state {
        case .noRearmed: return "RS Re-ARMed is not installed"
        case .rearmedTooOld: return "RS Re-ARMed 1.5.1 or later is needed"
        case .notInstalled: return "The mods are not installed"
        case .installed(let v), .newerInstalled(let v): return "Mods \(v) installed"
        case .updateAvailable: return "Mods update: \(version)"
        case .noBundledLibrary: return "This build of the app carries no mods library"
        }
    }

    private var detail: String {
        switch library.state {
        case .noRearmed: return "Install RS Re-ARMed first; the mods run in it. (Or choose its game folder below.)"
        case .rearmedTooOld: return "Your RS Re-ARMed can't load these mods. Install its latest version, then come back."
        case .notInstalled: return "Install them into RS Re-ARMed's mods folders. The settings below work either way."
        case .installed: return "RS Re-ARMed loads them at startup. Settings take effect while the game runs."
        case .newerInstalled: return "That's newer than this app's \(version), so the app leaves it in place."
        case .updateAvailable(let v): return "Installed: \(v). Update to this app's version."
        case .noBundledLibrary: return "Build the app with build-app.sh, or install with ../build.sh --install."
        }
    }

    private var icon: String {
        switch library.state {
        case .installed, .newerInstalled: return "checkmark.circle.fill"
        case .notInstalled, .updateAvailable: return "arrow.down.circle.fill"
        default: return "exclamationmark.triangle.fill"
        }
    }

    private var color: Color {
        switch library.state {
        case .installed, .newerInstalled: return .green
        case .notInstalled, .updateAvailable: return .accentColor
        default: return .orange
        }
    }
}

struct FooterView: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        HStack(spacing: 10) {
            if store.hasLearnPlay {
                Picker("", selection: Binding(get: { store.isLearnPlay }, set: store.showLearnPlay)) {
                    Text("Mac").tag(false)
                    Text("Learn & Play").tag(true)
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                .help("Each edition has its own RSMods.ini.")
            }
            Image(systemName: "folder")
            Text(store.gameFolder.path.replacingOccurrences(of: NSHomeDirectory(), with: "~"))
                .lineLimit(1).truncationMode(.middle)
                .foregroundStyle(.secondary)
            Button("Change…", action: store.chooseGameFolder)
            Button("Show RSMods.ini", action: store.revealInFinder).disabled(!store.folderExists)
            Spacer()
            Text(store.status).foregroundStyle(.secondary).lineLimit(1)
        }
        .font(.callout)
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
    }
}

// MARK: - Enable / Disable Mods

struct ModTogglesTab: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            GroupBox("Enabled Mods") {
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        LazyVGrid(columns: [GridItem(.flexible(), alignment: .topLeading),
                                            GridItem(.flexible(), alignment: .topLeading)],
                                  alignment: .leading, spacing: 8) {
                            ForEach(Catalog.mods) { mod in
                                VStack(alignment: .leading, spacing: 1) {
                                    Toggle(mod.label, isOn: store.toggle(mod))
                                    if let note = mod.note {
                                        Text(note).font(.caption).foregroundStyle(.orange).padding(.leading, 20)
                                    }
                                }
                                .help(mod.help)
                            }
                        }
                        DisclosureGroup("Not on macOS yet") {
                            Text(Catalog.notYetOnMac.joined(separator: " · "))
                                .font(.callout)
                                .foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                                .padding(.top, 4)
                        }
                        .foregroundStyle(.secondary)
                    }
                    .toggleStyle(.checkbox)
                    .padding(6)
                }
            }
            .frame(width: 380)

            TabView {
                DisableUISettings().tabItem { Text("Disable UI Elements") }
                HighwaySettings().tabItem { Text("Highway") }
                AutomationSettings().tabItem { Text("Automation") }
                MiscSettings().tabItem { Text("Misc") }
                GuitarSpeakSettings().tabItem { Text("Guitar Speak") }
                SongListSettings().tabItem { Text("Song Lists") }
                TuningSettings().tabItem { Text("Tunings") }
                ToneSettings().tabItem { Text("Tones") }
                SevenStringSettings().tabItem { Text("7-String") }
            }
        }
    }
}

// A group of a mod's settings, dimmed while the mod is off
struct ModSettingsGroup<Content: View>: View {
    @EnvironmentObject var store: SettingsStore
    let title: String
    let mod: String
    @ViewBuilder var content: Content

    var body: some View {
        GroupBox(title) {
            VStack(alignment: .leading, spacing: 6) { content }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(4)
        }
        .disabled(!store.isOn(mod))
        .opacity(store.isOn(mod) ? 1 : 0.55)
    }
}

// Radio buttons for a "…When" value in [Toggle Switches]
struct WhenPicker: View {
    @EnvironmentObject var store: SettingsStore
    let key: String
    let options: [(value: String, label: String)]

    var body: some View {
        Picker("", selection: Binding(
            get: { store.value(Section.toggles, key).lowercased() },
            set: { store.set(Section.toggles, key, $0) })) {
            ForEach(options, id: \.value) { Text($0.label).tag($0.value) }
        }
        .pickerStyle(.radioGroup)
        .labelsHidden()
    }
}

// A number with a stepper and its unit
struct NumberField: View {
    let label: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var step: Double = 1
    var unit = ""

    var body: some View {
        HStack {
            Text(label)
            Spacer()
            TextField("", value: $value, format: .number.precision(.fractionLength(0...2)))
                .frame(width: 64)
                .multilineTextAlignment(.trailing)
            Stepper("", value: Binding(get: { value }, set: { value = min(max($0, range.lowerBound), range.upperBound) }),
                    in: range, step: step)
                .labelsHidden()
            Text(unit).foregroundStyle(.secondary).frame(width: 52, alignment: .leading)
        }
    }
}

struct DisableUISettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Toggle Loft Off When", mod: "ToggleLoft") {
                    WhenPicker(key: "ToggleLoftWhen", options: [
                        ("startup", "As soon as the game starts"),
                        ("song", "Only when in a song"),
                        ("manual", "Manually, with the Toggle Loft key"),
                    ])
                }
                ModSettingsGroup(title: "Toggle Headstock Off When", mod: "Headstock") {
                    WhenPicker(key: "RemoveHeadstockWhen", options: [
                        ("startup", "As soon as the game starts"),
                        ("song", "Only when in a song"),
                    ])
                }
                ModSettingsGroup(title: "Toggle Skyline Off When", mod: "Skyline") {
                    WhenPicker(key: "ToggleSkylineWhen", options: [
                        ("startup", "As soon as the game starts"),
                        ("song", "Only when in a song"),
                    ])
                }
                ModSettingsGroup(title: "Toggle Lyrics Off When", mod: "Lyrics") {
                    WhenPicker(key: "RemoveLyricsWhen", options: [
                        ("startup", "As soon as the game starts"),
                        ("manual", "Manually, with the Remove Lyrics key"),
                    ])
                }
                ModSettingsGroup(title: "Toggle Song Timer When", mod: "ShowSongTimer") {
                    WhenPicker(key: "ShowSongTimerWhen", options: [
                        ("automatic", "Always, in every song"),
                        ("manual", "Manually, with the Show Song Timer key"),
                    ])
                }
            }
            .padding(8)
        }
    }
}

struct HighwaySettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Camera Director", mod: "CameraDirector") {
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
                ModSettingsGroup(title: "Sunlight", mod: "Sunlight") {
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
                ModSettingsGroup(title: "Extended Range", mod: "ExtendedRange") {
                    Picker("Lowest string at or below", selection: store.binding(Section.settings, "ExtendedRangeModeAt")) {
                        ForEach(Catalog.extendedRangeTunings, id: \.value) { Text($0.label).tag($0.value) }
                    }
                    Picker("Colors", selection: store.binding(Section.toggles, "CustomStringColors")) {
                        Text("The game's colorblind colors").tag("0")
                        Text("ZZ's colors").tag("1")
                        Text("Your colorblind string colors (Colors tab)").tag("2")
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
                .toggleStyle(.checkbox)
            }
            .padding(8)
        }
    }
}

struct CameraSlider: View {
    @EnvironmentObject var store: SettingsStore
    let label: String
    let key: String
    let range: ClosedRange<Double>

    var body: some View {
        HStack {
            Text(label).frame(width: 48, alignment: .leading)
            Slider(value: store.number(key), in: range, step: 1)
            Text("\(Int(store.number(key).wrappedValue))").monospacedDigit().frame(width: 36, alignment: .trailing)
        }
    }
}

struct AutomationSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "How Do You Want To Enumerate?", mod: "ForceReEnumeration") {
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
                ModSettingsGroup(title: "Auto Load Profile", mod: "ForceProfileLoad") {
                    HStack {
                        Text("Profile")
                        TextField("The last used one", text: store.binding(Section.toggles, "ProfileToLoad"))
                    }
                    Text("At startup, the game goes into this profile, by its name in the game; left empty, into the last used one (the top of the profile list).")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                ModSettingsGroup(title: "Custom NSP Timer", mod: "UseCustomNSPTimer") {
                    NumberField(label: "Time between songs in Non-Stop Play",
                                value: store.number("CustomNSPTimeLimit", scale: 1000),
                                range: 2...60, step: 0.1, unit: "seconds")
                }
            }
            .padding(8)
        }
    }
}

struct MiscSettings: View {
    @EnvironmentObject var store: SettingsStore
    @State private var devices: [String] = []

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Riff Repeater Speed Increment", mod: "RRSpeedAboveOneHundred") {
                    NumberField(label: "Each press changes the speed by", value: store.number("RRSpeedInterval"),
                                range: 1...100, unit: "%")
                }
                ModSettingsGroup(title: "Control Volume Increment", mod: "VolumeControl") {
                    NumberField(label: "Each press changes the volume by", value: store.number("VolumeControlInterval"),
                                range: 1...100, unit: "%")
                }
                ModSettingsGroup(title: "Rewind", mod: "AllowRewind") {
                    NumberField(label: "Rewind By", value: store.number("RewindBy", scale: 1000),
                                range: 0...120, step: 0.5, unit: "seconds")
                    NumberField(label: "Rewind Lead Up", value: store.number("RewindLeadup", scale: 1000),
                                range: 0...60, step: 0.5, unit: "seconds")
                }
                ModSettingsGroup(title: "Looping Lead Up", mod: "AllowLooping") {
                    NumberField(label: "Lead before the loop starts", value: store.number("LoopingLeadUp", scale: 1000),
                                range: 0...60, step: 0.5, unit: "seconds")
                }
                ModSettingsGroup(title: "Override Input Volume", mod: "OverrideInputVolumeEnabled") {
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
                ModSettingsGroup(title: "Your Own Plug-in (ToneHost)", mod: "ToneHost") {
                    Label(store.toneHostInstalled ? "rs-tonehost is set up." : "rs-tonehost isn't set up.",
                          systemImage: store.toneHostInstalled ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                        .foregroundStyle(store.toneHostInstalled ? Color.green : Color.orange)
                    Text(store.toneHostInstalled
                         ? "The game starts it, and it plays the guitar through your plug-in, following the game's tones. The game's own guitar is silent while it runs; the guitar fader and the volume keys still work."
                         : "Until it is, the game keeps its own tones. Set it up with tonehost in rearmed-mods' source.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                ModSettingsGroup(title: "Start On Secondary Monitor", mod: "SecondaryMonitor") {
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
                ModSettingsGroup(title: "Show Current Note", mod: "ShowCurrentNoteOnScreen") {
                    Text("The note you play, as the game hears it: in a song left of the middle, elsewhere at the top. Needs RS Re-ARMed 1.11.0.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                ModSettingsGroup(title: "Backup Profile", mod: "BackupProfile") {
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
            .padding(8)
        }
        .onAppear { devices = AudioDevices.inputNames() }
    }
}

enum Screens {
    // A screen's top left corner as Windows has it (RSMods' SecondaryMonitorX/YPosition): from the main screen's top
    // left, y down, in points
    static func topLeft(of screen: NSScreen) -> (x: Int, y: Int) {
        let mainHeight = NSScreen.screens.first?.frame.height ?? screen.frame.height
        return (Int(screen.frame.minX), Int(mainHeight - screen.frame.maxY))
    }
}

struct GuitarSpeakSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Guitar Speak", mod: "GuitarSpeak") {
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
            .padding(8)
        }
    }
}

// [SongListTitles]: RSMods' song list names, SongListTitle_1 to _20 (its app's "Song Lists" box), and More Song Lists
// (its app's Profile Edits: Add New SongList, Remove Newest SongList), which the library applies to the profiles
struct SongListSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "More Song Lists", mod: "MoreSongLists") {
                    NumberField(label: "Song lists in each profile", value: store.number("NumberOfSongLists"),
                                range: 6...20, unit: "")
                    Text("When the game starts, each of your profiles gets this many song lists: empty ones after the last, or with a lower number the last ones go while they are empty (a list with songs stays: take its songs out in the game first). Learn a Song and the song list editor show them; Non-Stop Play offers the first six.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                ModSettingsGroup(title: "Custom Song List Titles", mod: "CustomSongListTitles") {
                    titles(1...6)
                    DisclosureGroup("Song lists 7 to 20") {
                        titles(7...20).padding(.top, 4)
                    }
                    Text("The names the game's menus give your song lists: such as Learn a Song's song list filter and Non-Stop Play's choice of list. A list left empty keeps the game's name. Lists 7 to 20 are in a profile when More Song Lists (or RSMods' app on Windows) added them. Needs RS Re-ARMed 1.11.0.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .padding(8)
        }
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
struct TuningSettings: View {
    @EnvironmentObject var store: SettingsStore

    private var tunings: [(key: String, value: String)] { store.entries(Section.customTunings) }
    private var missingCommon: [(name: String, strings: [Int])] {
        let have = Set(tunings.map { $0.key.lowercased() })
        return Catalog.commonTunings.filter { !have.contains($0.name.lowercased()) }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Custom Tunings", mod: "CustomTunings") {
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
            }
            .padding(8)
        }
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
struct ToneSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        let tones = store.profileTones
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Default Tones", mod: "DefaultTones") {
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
            }
            .padding(8)
        }
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

// MARK: - Colors

struct ColorsTab: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                HStack(alignment: .top, spacing: 12) {
                    ModSettingsGroup(title: "String Colors", mod: "CustomStringColors") {
                        StringColorGrid(prefix: "string")
                    }
                    ModSettingsGroup(title: "Note Colors", mod: "SeparateNoteColors") {
                        StringColorGrid(prefix: "note")
                    }
                }
                ModSettingsGroup(title: "Highway Colors", mod: "CustomHighwayColors") {
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
                Text("The notes follow the strings unless Separate Note Colors is on. Rainbow Strings and Rainbow Notes turn on with their keys. These need RS Re-ARMed 1.9.0; the note and highway colors need its Metal renderer too.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(8)
        }
    }
}

// The six strings of `prefix` in [String Colors], for normal and colorblind mode
struct StringColorGrid: View {
    let prefix: String

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 6) {
            GridRow {
                Text("")
                Text("Normal").font(.caption).foregroundStyle(.secondary)
                Text("Colorblind").font(.caption).foregroundStyle(.secondary)
            }
            ForEach(0..<6, id: \.self) { i in
                GridRow {
                    Text(Catalog.stringNames[i])
                    ColorCell(section: Section.stringColors, key: "\(prefix)\(i)_N",
                              fallback: Catalog.defaultColors[i], showsGame: false)
                    ColorCell(section: Section.stringColors, key: "\(prefix)\(i)_CB",
                              fallback: Catalog.defaultColorblindColors[i], showsGame: false)
                }
            }
        }
    }
}

// A color well, and what clears the key again
struct ColorCell: View {
    @EnvironmentObject var store: SettingsStore
    let section: String
    let key: String
    let fallback: String
    var showsGame = true
    var resetHelp = "Back to the game's color"

    var body: some View {
        let value = store.colorValue(section, key)
        HStack(spacing: 6) {
            ColorPicker("", selection: store.color(section, key, fallback: fallback), supportsOpacity: false)
                .labelsHidden()
            if showsGame {
                Text(value ?? "Game's").font(.callout.monospaced()).foregroundStyle(.secondary)
                    .frame(width: 60, alignment: .leading)
            }
            Button {
                store.set(section, key, "")
            } label: {
                Image(systemName: "arrow.counterclockwise")
            }
            .buttonStyle(.borderless)
            .disabled(value == nil)
            .help(resetHelp)
        }
    }
}

// Seven-String Guitar's settings ([Seven String]): the low B's color, chord detection, tuning names and the headstock
struct SevenStringSettings: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ModSettingsGroup(title: "Seven-String Guitar", mod: "SevenString") {
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
                .toggleStyle(.checkbox)
            }
            .padding(8)
        }
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

// MARK: - Keybindings

struct KeybindingsTab: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        let duplicates = store.duplicateKeys
        HStack(alignment: .top, spacing: 12) {
            KeybindList(title: "Mod Key Bindings", binds: Catalog.modKeybinds, duplicates: duplicates)
            KeybindList(title: "Audio Key Bindings", binds: Catalog.audioKeybinds, duplicates: duplicates,
                        footer: "Each volume key raises its channel; Control + the key lowers it. Hold the mixer key to see every channel.")
        }
        .padding(.top, 4)
    }
}

struct KeybindList: View {
    @EnvironmentObject var store: SettingsStore
    let title: String
    let binds: [Keybind]
    let duplicates: Set<String>
    var footer: String? = nil

    var body: some View {
        GroupBox(title) {
            ScrollView {
            VStack(alignment: .leading, spacing: 6) {
                ForEach(binds) { kb in
                    let off = kb.mod.map { !store.isOn($0) } ?? false
                    HStack {
                        Text(kb.label).foregroundStyle(off ? .secondary : .primary)
                        if off {
                            Text("mod off").font(.caption).foregroundStyle(.tertiary)
                        }
                        Spacer()
                        KeyRecorder(value: store.binding(kb.section, kb.key), conflict: duplicates.contains(kb.id))
                    }
                }
                if let footer {
                    Text(footer).font(.caption).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(.top, 4)
                }
                Spacer(minLength: 0)
            }
            .padding(6)
            }
        }
        .frame(maxWidth: .infinity)
    }
}
