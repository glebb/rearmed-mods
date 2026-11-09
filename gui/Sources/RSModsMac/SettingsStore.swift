import AppKit
import SwiftUI

// RSMods.ini in the game folder. Every change is saved at once, as in the Windows GUI; the mods pick it up
// while the game runs. Changes made elsewhere (a text editor, the game writing its default file) are read back.
@MainActor
final class SettingsStore: ObservableObject {
    static let defaultGameFolder = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/RS Re-ARMed/game", isDirectory: true)
    private static let folderDefaultsKey = "GameFolder"
    private static let enumerationModeKey = "ForceReEnumerationMode"

    @Published private(set) var ini = IniDocument()
    @Published private(set) var gameFolder: URL
    @Published private(set) var status = ""
    @Published private(set) var problem: String?

    private var lastModified: Date?
    private var timer: Timer?

    var iniURL: URL { gameFolder.appendingPathComponent("RSMods.ini") }
    var folderExists: Bool {
        var dir: ObjCBool = false
        return FileManager.default.fileExists(atPath: gameFolder.path, isDirectory: &dir) && dir.boolValue
    }

    init(gameFolder folder: URL? = nil) {
        if let folder {
            gameFolder = folder
        } else if let path = UserDefaults.standard.string(forKey: Self.folderDefaultsKey) {
            gameFolder = URL(fileURLWithPath: path, isDirectory: true)
        } else {
            gameFolder = Self.defaultGameFolder
        }
        reload()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.reloadIfChanged() }
        }
    }

    // MARK: Values

    func value(_ section: String, _ key: String) -> String {
        ini.value(section, key) ?? Catalog.defaults[section]?[key] ?? ""
    }

    func set(_ section: String, _ key: String, _ value: String) {
        reloadIfChanged()  // the game writes the file too: a key press in the last second is not to be lost
        guard self.value(section, key) != value || ini.value(section, key) == nil else { return }
        ini.set(section, key, value)
        save()
    }

    // A section that is a list ([Custom Tunings]): its lines, and all of them at once
    func entries(_ section: String) -> [(key: String, value: String)] { ini.entries(section) }

    func setEntries(_ section: String, _ entries: [(key: String, value: String)]) {
        reloadIfChanged()
        guard ini.entries(section).map({ [$0.key, $0.value] }) != entries.map({ [$0.key, $0.value] }) else { return }
        ini.setEntries(section, entries)
        save()
    }

    // The tones saved in the profiles, as the library lists them when the game starts (rearmed-mods/tones.txt in the
    // game folder: "guitar" or "bass", a tab, the name)
    var profileTones: [(name: String, bass: Bool)] {
        let url = gameFolder.appendingPathComponent("rearmed-mods/tones.txt")
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return [] }
        return text.split(separator: "\n").compactMap { line in
            let parts = line.split(separator: "\t", maxSplits: 1)
            return parts.count == 2 ? (String(parts[1]), parts[0] == "bass") : nil
        }
    }

    func binding(_ section: String, _ key: String) -> Binding<String> {
        Binding(get: { self.value(section, key) }, set: { self.set(section, key, $0) })
    }

    func isOn(_ key: String) -> Bool {
        let section = Catalog.mods.first { $0.key == key }?.section ?? Section.toggles
        let v = value(section, key).lowercased()
        switch key {
        case "CustomStringColors": return v == "2"  // 0 is off, and on Windows other values are other modes
        case "SeparateNoteColors": return v == "on" && value(Section.settings, "SeparateNoteColorsMode") == "2"
        default: return v != "off" && !v.isEmpty
        }
    }

    // A mod's checkbox. ForceReEnumeration holds its mode instead of "on": turning it on brings back the mode last
    // chosen. CustomStringColors is 2 or 0, and separate note colors take two keys.
    func toggle(_ mod: ModToggle) -> Binding<Bool> {
        let key = mod.key
        return Binding(get: { self.isOn(key) }, set: { on in
            switch key {
            case "ForceReEnumeration": self.set(mod.section, key, on ? self.enumerationMode : "off")
            case "CustomStringColors": self.set(mod.section, key, on ? "2" : "0")
            case "SeparateNoteColors":
                self.set(Section.settings, "SeparateNoteColorsMode", on ? "2" : "0")
                self.set(mod.section, key, on ? "on" : "off")
            default: self.set(mod.section, key, on ? "on" : "off")
            }
        })
    }

    // A color in the file (RRGGBB), or `fallback` while it has none. An empty value is the game's own color.
    func color(_ section: String, _ key: String, fallback: String) -> Binding<Color> {
        Binding(get: { Color(hex: self.colorValue(section, key) ?? fallback) },
                set: { self.set(section, key, $0.hex) })
    }

    func colorValue(_ section: String, _ key: String) -> String? {
        let v = value(section, key)
        return v.isEmpty ? nil : v
    }

    var enumerationMode: String {
        get {
            let v = value(Section.toggles, "ForceReEnumeration").lowercased()
            if v == "manual" || v == "automatic" { return v }
            return UserDefaults.standard.string(forKey: Self.enumerationModeKey) ?? "manual"
        }
        set {
            UserDefaults.standard.set(newValue, forKey: Self.enumerationModeKey)
            if isOn("ForceReEnumeration") { set(Section.toggles, "ForceReEnumeration", newValue) }
            objectWillChange.send()
        }
    }

    // A whole number in [Mod Settings], or another section
    func number(_ key: String, scale: Double = 1, section: String = Section.settings) -> Binding<Double> {
        Binding(get: {
            (Double(self.value(section, key)) ?? 0) / scale
        }, set: { v in
            self.set(section, key, String(Int((v * scale).rounded())))
        })
    }

    // MARK: Profile backups

    var backupsFolder: URL { gameFolder.appendingPathComponent(ProfileBackups.rootName, isDirectory: true) }

    // Backs up the profiles of both editions (each into its own game folder), off the main thread: when the app opens
    // with Backup Profile on, and with Back Up Now
    func backUpProfiles(evenIfOff: Bool = false) {
        guard evenIfOff || isOn("BackupProfile") else { return }
        let folders = [home.appendingPathComponent("game", isDirectory: true), learnPlayFolder]
            .filter { FileManager.default.fileExists(atPath: $0.path) }
        let keep = Int(value(Section.guiSettings, "NumberOfBackups")) ?? 50
        DispatchQueue.global(qos: .utility).async {
            let made = folders.flatMap { ProfileBackups.backUp(gameFolder: $0, keep: keep) }
            DispatchQueue.main.async {
                self.status = made.isEmpty ? "The profiles haven't changed since their last backup"
                                           : "Backed up the profiles (\(made.count) save folder\(made.count == 1 ? "" : "s"))"
            }
        }
    }

    // Keys bound to more than one action
    // of the mods that are on: the mods' default keys share some (N: camera side and rainbow notes)
    var duplicateKeys: Set<String> {
        let binds = (Catalog.modKeybinds + Catalog.audioKeybinds).filter { $0.mod.map(isOn) ?? true }
        var seen: [UInt16: Int] = [:]
        for kb in binds {
            if let code = KeyNames.code(forIniValue: value(kb.section, kb.key)) { seen[code, default: 0] += 1 }
        }
        return Set(binds.filter {
            guard let code = KeyNames.code(forIniValue: value($0.section, $0.key)) else { return false }
            return seen[code, default: 0] > 1
        }.map(\.id))
    }

    // MARK: Game folder

    // RS Re-ARMed's folder: the Mac edition's game is in game/, Learn & Play's in learn-play/game/
    var home: URL {
        let parent = gameFolder.deletingLastPathComponent()
        return parent.lastPathComponent == "learn-play" ? parent.deletingLastPathComponent() : parent
    }
    var learnPlayFolder: URL { home.appendingPathComponent("learn-play/game", isDirectory: true) }
    var isLearnPlay: Bool { gameFolder.deletingLastPathComponent().lastPathComponent == "learn-play" }
    var hasLearnPlay: Bool { FileManager.default.fileExists(atPath: learnPlayFolder.path) }
    // What the mod runs: start.sh in RS Re-ARMed's own folder, whichever game folder is shown here
    // rs-stems, where stems/install.sh puts it (beside tonehost)
    var stemsInstalled: Bool {
        FileManager.default.isExecutableFile(atPath: Self.defaultGameFolder.deletingLastPathComponent()
            .appendingPathComponent("stems/rs-stems").path)
    }

    var toneHostInstalled: Bool {
        FileManager.default.isExecutableFile(atPath: Self.defaultGameFolder.deletingLastPathComponent()
            .appendingPathComponent("tonehost/start.sh").path)
    }

    func showLearnPlay(_ on: Bool) {
        use(on ? learnPlayFolder : home.appendingPathComponent("game", isDirectory: true))
    }

    func chooseGameFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.directoryURL = folderExists ? gameFolder : FileManager.default.homeDirectoryForCurrentUser
        panel.message = "Choose the RS Re-ARMed game folder (the one with Rocksmith2014.app and RSMods.ini)."
        panel.prompt = "Choose"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        use(url)
    }

    private func use(_ url: URL) {
        gameFolder = url
        UserDefaults.standard.set(url.path, forKey: Self.folderDefaultsKey)
        reload()
    }

    func revealInFinder() {
        if FileManager.default.fileExists(atPath: iniURL.path) {
            NSWorkspace.shared.activateFileViewerSelecting([iniURL])
        } else if folderExists {
            NSWorkspace.shared.open(gameFolder)
        }
    }

    // MARK: File

    private func modificationDate() -> Date? {
        (try? FileManager.default.attributesOfItem(atPath: iniURL.path))?[.modificationDate] as? Date
    }

    func reload() {
        problem = nil
        guard folderExists else {
            ini = IniDocument()
            lastModified = nil
            problem = "The game folder wasn't found. Install RS Re-ARMed, or choose its folder."
            return
        }
        if let text = try? String(contentsOf: iniURL, encoding: .utf8) {
            ini = IniDocument(text: text)
            status = "Loaded RSMods.ini"
        } else {
            ini = IniDocument()
            status = "No RSMods.ini yet: it is created when you change a setting"
        }
        lastModified = modificationDate()
    }

    func reloadIfChanged() {
        let date = modificationDate()
        guard date != lastModified else { return }
        reload()
        if date != nil { status = "RSMods.ini changed outside the app; reloaded" }
    }

    private func save() {
        guard folderExists else {
            problem = "Not saved: the game folder wasn't found."
            return
        }
        do {
            var text = ini.text
            if !text.hasSuffix("\n") { text += "\n" }
            try text.write(to: iniURL, atomically: true, encoding: .utf8)
            lastModified = modificationDate()
            problem = nil
            status = "Saved. The game picks up changes while it runs."
        } catch {
            problem = "Couldn't save RSMods.ini: \(error.localizedDescription)"
        }
    }
}
