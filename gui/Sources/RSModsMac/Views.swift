import SwiftUI

// A sidebar of the mods by what they change, and the keybindings. Each mod is a row with its switch; its settings and
// keys open under it while it is on.
enum Page: Hashable {
    case category(String)
    case keybindings
}

struct ContentView: View {
    @EnvironmentObject var store: SettingsStore
    @State private var page = Page.category(Catalog.categories[0].name)
    @State private var search = ""

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
            Divider()
            HStack(spacing: 0) {
                Sidebar(page: $page, search: $search).frame(width: 200)
                Divider()
                VStack(spacing: 0) {
                    HStack {
                        Text(title).font(.title2.weight(.semibold)).lineLimit(1)
                        Spacer()
                        SearchField(text: $search).frame(width: 200)
                    }
                    .padding(.horizontal, 16)
                    .padding(.vertical, 12)
                    detail.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                }
            }
            .disabled(!store.folderExists)
            Divider()
            FooterView()
        }
        .frame(minWidth: 860, minHeight: 600)
        .task { store.backUpProfiles() }  // Backup Profile: when the app opens, as RSMods' app does
    }

    private var query: String { search.trimmingCharacters(in: .whitespaces) }

    private var title: String {
        if !query.isEmpty { return "Mods with “\(query)”" }
        switch page {
        case .category(let name): return name
        case .keybindings: return "Keybindings"
        }
    }

    @ViewBuilder private var detail: some View {
        if !query.isEmpty {
            let found = Catalog.mods.filter {
                $0.label.localizedCaseInsensitiveContains(query) || $0.help.localizedCaseInsensitiveContains(query)
            }
            ModsPage(mods: found, footnote: (found.isEmpty ? "No mod matches. " : "")
                     + "Not on macOS yet: " + Catalog.notYetOnMac.joined(separator: ", ") + ".")
        } else {
            switch page {
            case .category(let name):
                let category = Catalog.categories.first { $0.name == name } ?? Catalog.categories[0]
                ModsPage(mods: category.mods, footnote: category.footnote).id(name)
            case .keybindings:
                KeybindingsTab()
            }
        }
    }
}

struct Sidebar: View {
    @EnvironmentObject var store: SettingsStore
    @Binding var page: Page
    @Binding var search: String

    var body: some View {
        // Choosing a page ends a search
        List(selection: Binding<Page?>(get: { page }, set: { if let new = $0 { page = new; search = "" } })) {
            SwiftUI.Section("Mods") {
                ForEach(Catalog.categories) { category in
                    Label(category.name, systemImage: category.icon)
                        .badge(category.keys.filter(store.isOn).count)  // the mods that are on
                        .tag(Page.category(category.name))
                }
            }
            SwiftUI.Section("Keys") {
                Label("Keybindings", systemImage: "keyboard").tag(Page.keybindings)
            }
        }
        .listStyle(.sidebar)
    }
}

struct SearchField: View {
    @Binding var text: String

    var body: some View {
        HStack(spacing: 5) {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("Search Mods", text: $text).textFieldStyle(.plain)
            if !text.isEmpty {
                Button {
                    text = ""
                } label: {
                    Image(systemName: "xmark.circle.fill")
                }
                .buttonStyle(.borderless)
            }
        }
        .padding(.horizontal, 7)
        .padding(.vertical, 5)
        .background(RoundedRectangle(cornerRadius: 6).fill(Color.primary.opacity(0.06)))
        .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.primary.opacity(0.10)))
    }
}

// MARK: - Mods

// A list of mods in one box, as System Settings lists its switches
struct ModsPage: View {
    let mods: [ModToggle]
    var footnote: String? = nil

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                if !mods.isEmpty {
                    VStack(spacing: 0) {
                        ForEach(Array(mods.enumerated()), id: \.element.id) { i, mod in
                            if i > 0 { Divider() }
                            ModRow(mod: mod)
                        }
                    }
                    .background(Color(nsColor: .controlBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color(nsColor: .separatorColor)))
                }
                if let footnote {
                    Text(footnote)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(.horizontal, 4)
                }
            }
            .frame(maxWidth: 760, alignment: .leading)
            .padding(.horizontal, 16)
            .padding(.bottom, 16)
            .frame(maxWidth: .infinity)
        }
    }
}

// A mod: what it does and its switch, and while it is on its settings and keys
struct ModRow: View {
    @EnvironmentObject var store: SettingsStore
    let mod: ModToggle

    var body: some View {
        let keys = Catalog.keybinds(of: mod.key)
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .center, spacing: 12) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(mod.label).fontWeight(.medium)
                    Text(mod.help).font(.callout).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    if let note = mod.note {
                        Text(note).font(.caption).foregroundStyle(.orange)
                    }
                }
                Spacer(minLength: 0)
                Toggle(mod.label, isOn: store.toggle(mod)).toggleStyle(.switch).controlSize(.small).labelsHidden()
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 9)
            if store.isOn(mod.key) && (ModSettings.mods.contains(mod.key) || !keys.isEmpty) {
                Divider().padding(.leading, 12)
                VStack(alignment: .leading, spacing: 8) {
                    ModSettings(mod: mod.key)
                    if !keys.isEmpty {
                        if ModSettings.mods.contains(mod.key) { Divider() }
                        KeyRows(binds: keys)
                    }
                }
                .toggleStyle(.checkbox)
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Color.primary.opacity(0.035))
            }
        }
    }
}

// A mod's keys under its settings; the Keybindings page has them all
struct KeyRows: View {
    @EnvironmentObject var store: SettingsStore
    let binds: [Keybind]

    var body: some View {
        let duplicates = store.duplicateKeys
        ForEach(binds) { kb in
            HStack {
                Text(kb.label + " key")
                Spacer()
                KeyRecorder(value: store.binding(kb.section, kb.key), conflict: duplicates.contains(kb.id))
            }
        }
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
                .layoutPriority(1)  // the status gives way first
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

enum Screens {
    // A screen's top left corner as Windows has it (RSMods' SecondaryMonitorX/YPosition): from the main screen's top
    // left, y down, in points
    static func topLeft(of screen: NSScreen) -> (x: Int, y: Int) {
        let mainHeight = NSScreen.screens.first?.frame.height ?? screen.frame.height
        return (Int(screen.frame.minX), Int(mainHeight - screen.frame.maxY))
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

// MARK: - Keybindings

struct KeybindingsTab: View {
    @EnvironmentObject var store: SettingsStore

    var body: some View {
        let duplicates = store.duplicateKeys
        HStack(alignment: .top, spacing: 10) {
            KeybindList(title: "Mod Key Bindings", binds: Catalog.modKeybinds, duplicates: duplicates)
            KeybindList(title: "Audio Key Bindings", binds: Catalog.audioKeybinds, duplicates: duplicates,
                        footer: "Each volume key raises its channel; Control + the key lowers it. Hold the mixer key to see every channel.")
        }
        .padding(.horizontal, 16)
        .padding(.bottom, 16)
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
                        Text(kb.label).foregroundStyle(off ? .secondary : .primary).lineLimit(1)
                            .help(off ? "Its mod is off" : "")
                        Spacer(minLength: 4)
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
