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
                    Text("At startup, the game goes into the last used profile (the top of the profile list).")
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
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
                         : "Until it is, the game keeps its own tones. Set it up with RS Re-ARMed's tools/tonehost.")
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
                Text("The notes follow the strings unless Separate Note Colors is on. Rainbow Strings and Rainbow Notes turn on with their keys. These need RS Re-ARMed after 1.8.0; the note and highway colors need its Metal renderer too.")
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
            .help("Back to the game's color")
        }
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
