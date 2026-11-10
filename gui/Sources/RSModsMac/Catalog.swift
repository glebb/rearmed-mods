import Foundation

// What the app shows: the mods rearmed-mods.dylib (../src) has, with the labels of the Windows
// RSMods GUI (GUI/UI.Designer.cs). When a mod is ported, move it from `notYetOnMac` to `mods`, add its keys to
// `defaults`, its key to one of `categories`, and its settings (if any) to ModSettings.

enum Section {
    static let toggles = "Toggle Switches"
    static let settings = "Mod Settings"
    static let keybinds = "Keybinds"
    static let audioKeybinds = "Audio Keybindings"
    static let stringColors = "String Colors"
    static let highwayColors = "Highway Colors"
    static let guitarSpeak = "Guitar Speak"
    static let guiSettings = "GUI Settings"
    static let songListTitles = "SongListTitles"
    static let customTunings = "Custom Tunings"
    static let defaultTones = "Default Tones"
    static let sevenString = "Seven String"
}

struct ModToggle: Identifiable {
    let label: String
    let key: String  // in [Toggle Switches], unless `section` says otherwise
    let help: String
    var note: String? = nil  // shown under the checkbox, e.g. a known problem on macOS
    var section = Section.toggles
    var id: String { key }
}

// A page of the sidebar: the mods that change one part of the game
struct ModCategory: Identifiable {
    let name: String
    let icon: String  // an SF Symbol
    let keys: [String]  // of `Catalog.mods`, in the page's order
    var footnote: String? = nil
    var id: String { name }
    var mods: [ModToggle] { keys.compactMap { key in Catalog.mods.first { $0.key == key } } }
}

struct Keybind: Identifiable {
    let label: String
    let section: String
    let key: String
    let mod: String?  // the toggle it belongs to, to dim it while that mod is off
    var id: String { section + "/" + key }
}

enum Catalog {
    // In the Windows GUI's order of its "Enabled Mods" box; `categories` puts them on the app's pages
    static let mods: [ModToggle] = [
        ModToggle(label: "Toggle Loft", key: "ToggleLoft",
                  help: "The venue behind the note highway goes black, e.g. to key it out in OBS."),
        ModToggle(label: "Show Song Timer", key: "ShowSongTimer",
                  help: "The song position at the top right of the screen."),
        ModToggle(label: "Force Enumeration", key: "ForceReEnumeration",
                  help: "The game finds songs you added without a restart: with a key or automatically."),
        ModToggle(label: "Remove Headstock", key: "Headstock",
                  help: "Hides the guitar's headstock and tuning pegs."),
        ModToggle(label: "Remove Skyline", key: "Skyline",
                  help: "Hides the phrase bars (dynamic difficulty) at the top of the screen."),
        ModToggle(label: "Autoload Last Used Profile", key: "ForceProfileLoad",
                  help: "Goes straight into the last used profile at startup."),
        ModToggle(label: "Allow Rewinding", key: "AllowRewind",
                  help: "The rewind key moves the song back, and the notes from there count again. The notes need RS Re-ARMed 1.11.0."),
        ModToggle(label: "Enable Looping", key: "AllowLooping",
                  help: "Set a loop with the loop keys in Learn a Song and Non-Stop Play. Pausing in a loop makes its notes count again (needs RS Re-ARMed 1.11.0)."),
        ModToggle(label: "Linear Riff Repeater", key: "LinearRiffRepeater",
                  help: "The Riff Repeater slider's percent is the song speed (normally 68% plays at about 50%)."),
        ModToggle(label: "Bypass 2+ RTC MessageBox", key: "BypassTwoRTCMessageBox",
                  help: "No popup when two Real Tone cables are connected."),
        ModToggle(label: "Audio In Background", key: "AllowAudioInBackground",
                  help: "The song keeps playing, with sound, while another app is active."),
        ModToggle(label: "Override Input Volume", key: "OverrideInputVolumeEnabled",
                  help: "Keeps your input device at a set volume."),
        ModToggle(label: "Remove Previews", key: "SongPreviews",
                  help: "No song previews while you browse the song lists."),
        ModToggle(label: "Allow RR Speed Above 100", key: "RRSpeedAboveOneHundred",
                  help: "The RR speed key raises Riff Repeater speed past 100%; Control + the key lowers it."),
        ModToggle(label: "Screenshot Scores", key: "ScreenShotScores",
                  help: "Saves each score screen as a picture in Pictures › RS Re-ARMed."),
        ModToggle(label: "Control Game Volume", key: "VolumeControl",
                  help: "The audio keys raise each channel's volume; Control + the key lowers it."),
        ModToggle(label: "Remove Lyrics", key: "Lyrics",
                  help: "Hides the lyrics."),
        ModToggle(label: "Fretless Mode", key: "Fretless",
                  help: "Hides the fret wires on the neck."),
        ModToggle(label: "Remove Inlays", key: "Inlays",
                  help: "Hides the position dots on the neck."),
        ModToggle(label: "Remove Lane Markers", key: "LaneMarkers",
                  help: "Hides the lane lines on the note highway."),
        ModToggle(label: "Custom String Colors", key: "CustomStringColors",
                  help: "The strings, and the notes unless they have their own colors, in your own colors."),
        ModToggle(label: "Separate Note Colors", key: "SeparateNoteColors",
                  help: "The notes get their own colors."),
        ModToggle(label: "Custom Highway Colors", key: "CustomHighwayColors",
                  help: "The lanes, gutters and fret numbers in your own colors.",
                  section: Section.highwayColors),
        ModToggle(label: "Rainbow Strings", key: "RainbowStrings",
                  help: "The rainbow strings key cycles the strings through the colors."),
        ModToggle(label: "Rainbow Notes", key: "RainbowNotes",
                  help: "The rainbow notes key cycles the notes through the colors, in a song."),
        ModToggle(label: "Extended Range Support", key: "ExtendedRange",
                  help: "Extended range colors in songs tuned low enough for a 7-string guitar or 5-string bass."),
        ModToggle(label: "Greenscreen Wall", key: "GreenScreenWall",
                  help: "Only the wall behind the stage goes black; the amps stay. Toggle Loft, when off, wins."),
        ModToggle(label: "Remove Fingerprints", key: "RemoveFingerprints",
                  help: "Hides the hand indicators (the fingerprints and their finger numbers) of chords on the neck."),
        ModToggle(label: "Prevent Pause Mid Song", key: "PreventMidSongPause",
                  help: "In a song, another app coming to the front doesn't pause the game."),
        ModToggle(label: "Prevent Buggy Tones", key: "FixBrokenTones",
                  help: "A tone change that never ends doesn't leave the game playing the clean tone. Needs RS Re-ARMed 1.11.0."),
        ModToggle(label: "Custom NSP Timer", key: "UseCustomNSPTimer",
                  help: "Your own time between songs in Non-Stop Play (10.99 seconds in the game)."),
        ModToggle(label: "Start On Secondary Monitor", key: "SecondaryMonitor",
                  help: "Moves the game's window to another screen when the game starts."),
        ModToggle(label: "Show Current Note", key: "ShowCurrentNoteOnScreen",
                  help: "The note you play, as the game hears it: in a song left of the middle, elsewhere at the top. Needs RS Re-ARMed 1.11.0."),
        ModToggle(label: "Guitar Speak", key: "GuitarSpeak",
                  help: "Play notes on your guitar to press keys in the game's menus."),
        ModToggle(label: "Show Current Accuracy", key: "DisplayCurrentAccuracy",
                  help: "In a song, the notes you hit out of the notes played so far, under the song timer. Needs RS Re-ARMed 1.11.0; in Learn & Play not in Score Attack."),
        ModToggle(label: "Custom Song List Titles", key: "CustomSongListTitles",
                  help: "Your own names for the song lists. Needs RS Re-ARMed 1.11.0."),
        ModToggle(label: "More Song Lists", key: "MoreSongLists",
                  help: "Your profiles get up to 20 song lists. When the game starts."),
        ModToggle(label: "Custom Tunings", key: "CustomTunings",
                  help: "The game names your tunings where it showed CUSTOM TUNING. From the game's next start."),
        ModToggle(label: "Change Default Tones", key: "DefaultTones",
                  help: "Tones saved in your profiles become the game's default tones. From the game's next start."),
        ModToggle(label: "Backup Profile", key: "BackupProfile",
                  help: "Backs up your profiles when this app opens, and every 10 minutes while the game runs (when they changed).",
                  section: Section.guiSettings),
        ModToggle(label: "Camera Director", key: "CameraDirector",
                  help: "Your own view of the note highway: zoom, height, side and tilt, with keys in a song."),
        ModToggle(label: "Sunlight", key: "Sunlight",
                  help: "For a screen outdoors: the sunlight key goes through off, bright and daylight."),
        ModToggle(label: "Your Own Plug-in (ToneHost)", key: "ToneHost",
                  help: "rs-tonehost plays the guitar through your Audio Unit and follows the game's tones."),
        ModToggle(label: "Song Stems", key: "SongStems",
                  help: "Takes the guitar or the bass out of the song you play, at a level of your own: a line of the volume mixer, with its own key. rs-stems separates each song on the Neural Engine. Needs RS Re-ARMed 1.12.0."),
        ModToggle(label: "Seven-String Guitar", key: "SevenString",
                  help: "A low B string for 7-string charts (DotCharter's), after rs7string. From the game's next start.",
                  note: "Learn & Play only, with the RS Re-ARMed after 1.11.0"),
        ModToggle(label: "Plain Packages", key: "PlainPackages",
                  help: "Unencrypted song archives, as DotCharter writes its 7-string charts, load as the encrypted ones do. From the game's next start.",
                  note: "Learn & Play only, with the RS Re-ARMed after 1.11.0"),
    ]

    // The sidebar's pages. Every mod is on exactly one.
    static let categories: [ModCategory] = [
        ModCategory(name: "Screen", icon: "tv", keys: [
            "ToggleLoft", "GreenScreenWall", "Skyline", "Lyrics", "ShowSongTimer", "DisplayCurrentAccuracy",
            "ShowCurrentNoteOnScreen", "Sunlight", "SecondaryMonitor", "ScreenShotScores",
        ]),
        ModCategory(name: "Highway & Neck", icon: "guitars", keys: [
            "CameraDirector", "Headstock", "Fretless", "Inlays", "LaneMarkers", "RemoveFingerprints", "ExtendedRange",
        ]),
        ModCategory(name: "Colors", icon: "paintpalette", keys: [
            "CustomStringColors", "SeparateNoteColors", "CustomHighwayColors", "RainbowStrings", "RainbowNotes",
        ], footnote: "The notes follow the strings unless Separate Note Colors is on. Rainbow Strings and Rainbow Notes turn on with their keys. These need RS Re-ARMed 1.9.0; the note and highway colors need its Metal renderer too."),
        ModCategory(name: "Practice", icon: "repeat", keys: [
            "AllowRewind", "AllowLooping", "RRSpeedAboveOneHundred", "LinearRiffRepeater", "PreventMidSongPause",
        ]),
        ModCategory(name: "Sound & Tones", icon: "speaker.wave.2", keys: [
            "VolumeControl", "OverrideInputVolumeEnabled", "AllowAudioInBackground", "BypassTwoRTCMessageBox",
            "FixBrokenTones", "DefaultTones", "ToneHost", "SongStems",
        ]),
        ModCategory(name: "Songs & Lists", icon: "music.note.list", keys: [
            "ForceReEnumeration", "SongPreviews", "UseCustomNSPTimer", "MoreSongLists", "CustomSongListTitles",
            "CustomTunings",
        ]),
        ModCategory(name: "Profiles & Menus", icon: "person.crop.circle", keys: [
            "ForceProfileLoad", "BackupProfile", "GuitarSpeak",
        ]),
        ModCategory(name: "Seven-String", icon: "7.circle", keys: ["SevenString", "PlainPackages"]),
    ]

    // The keys of a mod, for its row
    static func keybinds(of mod: String) -> [Keybind] { (modKeybinds + audioKeybinds).filter { $0.mod == mod } }

    // Mods of the Windows GUI that the Mac library doesn't have yet
    static let notYetOnMac = [
        "Auto Tune Song", "Fast Song Scan", "Fast Profile Load/Save",
    ]

    static let modKeybinds: [Keybind] = [
        Keybind(label: "Toggle Loft", section: Section.keybinds, key: "ToggleLoftKey", mod: "ToggleLoft"),
        Keybind(label: "Show Song Timer", section: Section.keybinds, key: "ShowSongTimerKey", mod: "ShowSongTimer"),
        Keybind(label: "Force ReEnumeration", section: Section.keybinds, key: "ForceReEnumerationKey", mod: "ForceReEnumeration"),
        Keybind(label: "Riff Repeater Speed", section: Section.keybinds, key: "RRSpeedKey", mod: "RRSpeedAboveOneHundred"),
        Keybind(label: "Loop Start", section: Section.keybinds, key: "LoopStartKey", mod: "AllowLooping"),
        Keybind(label: "Loop End", section: Section.keybinds, key: "LoopEndKey", mod: "AllowLooping"),
        Keybind(label: "Rewind", section: Section.keybinds, key: "RewindKey", mod: "AllowRewind"),
        Keybind(label: "Remove Lyrics", section: Section.keybinds, key: "RemoveLyricsKey", mod: "Lyrics"),
        Keybind(label: "Camera Zoom", section: Section.keybinds, key: "CameraZoomKey", mod: "CameraDirector"),
        Keybind(label: "Camera Height", section: Section.keybinds, key: "CameraHeightKey", mod: "CameraDirector"),
        Keybind(label: "Camera Side", section: Section.keybinds, key: "CameraSideKey", mod: "CameraDirector"),
        Keybind(label: "Camera Tilt", section: Section.keybinds, key: "CameraTiltKey", mod: "CameraDirector"),
        Keybind(label: "Camera Reset", section: Section.keybinds, key: "CameraResetKey", mod: "CameraDirector"),
        Keybind(label: "Sunlight", section: Section.keybinds, key: "SunlightKey", mod: "Sunlight"),
        Keybind(label: "Rainbow Strings", section: Section.keybinds, key: "RainbowStringsKey", mod: "RainbowStrings"),
        Keybind(label: "Rainbow Notes", section: Section.keybinds, key: "RainbowNotesKey", mod: "RainbowNotes"),
        Keybind(label: "Toggle Extended Range", section: Section.keybinds, key: "ToggleExtendedRangeKey",
                mod: "ExtendedRange"),
    ]

    // GuitarSpeak's keys, each pressed by a note ([Guitar Speak], a MIDI note; 0 none), in the Windows GUI's order
    static let guitarSpeakKeys: [(label: String, key: String)] = [
        ("Delete", "GuitarSpeakDeleteWhen"), ("Space", "GuitarSpeakSpaceWhen"), ("Enter", "GuitarSpeakEnterWhen"),
        ("Tab", "GuitarSpeakTabWhen"), ("Page Up", "GuitarSpeakPGUPWhen"), ("Page Down", "GuitarSpeakPGDNWhen"),
        ("Up Arrow", "GuitarSpeakUPWhen"), ("Down Arrow", "GuitarSpeakDNWhen"), ("Escape", "GuitarSpeakESCWhen"),
        ("Open Bracket", "GuitarSpeakOBracketWhen"), ("Close Bracket", "GuitarSpeakCBracketWhen"),
        ("Tilde", "GuitarSpeakTildeaWhen"), ("Forward Slash", "GuitarSpeakForSlashWhen"), ("Option", "GuitarSpeakAltWhen"),
        ("Stop / Start Guitar Speak", "GuitarSpeakCloseWhen"),
    ]

    // A MIDI note's name, as the library shows it: E2 (C C# D Eb E F F# G Ab A Bb B; 0 is C-1)
    static func noteName(_ midi: Int) -> String {
        let names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
        return midi < 0 ? "" : "\(names[midi % 12])\(midi / 12 - 1)"
    }

    // Extended Range's tunings: semitones from E standard, by the lowest string's note
    static let extendedRangeTunings: [(value: String, label: String)] = [
        ("-1", "Eb (-1)"), ("-2", "D (-2)"), ("-3", "C# (-3)"), ("-4", "C (-4)"), ("-5", "B (-5)"), ("-6", "Bb (-6)"),
        ("-7", "A (-7)"), ("-8", "Ab (-8)"), ("-9", "G (-9)"), ("-10", "F# (-10)"), ("-11", "F (-11)"), ("-12", "E (-12)"),
    ]

    static let audioKeybinds: [Keybind] = [
        Keybind(label: "Master Volume", section: Section.audioKeybinds, key: "MasterVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Song Volume", section: Section.audioKeybinds, key: "SongVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Player 1 Volume", section: Section.audioKeybinds, key: "Player1VolumeKey", mod: "VolumeControl"),
        Keybind(label: "Player 2 Volume", section: Section.audioKeybinds, key: "Player2VolumeKey", mod: "VolumeControl"),
        Keybind(label: "Microphone Volume", section: Section.audioKeybinds, key: "MicrophoneVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Voice-Over Volume", section: Section.audioKeybinds, key: "VoiceOverVolumeKey", mod: "VolumeControl"),
        Keybind(label: "SFX Volume", section: Section.audioKeybinds, key: "SFXVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Guitar or Bass in Song", section: Section.audioKeybinds, key: "SongStemVolumeKey", mod: "SongStems"),
        Keybind(label: "Change Selected Volume", section: Section.audioKeybinds, key: "ChangedSelectedVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Show Volume Mixer", section: Section.audioKeybinds, key: "DisplayMixerKey", mod: "VolumeControl"),
        Keybind(label: "Mute Player 1", section: Section.audioKeybinds, key: "MutePlayer1Key", mod: nil),
        Keybind(label: "Mute Player 2", section: Section.audioKeybinds, key: "MutePlayer2Key", mod: nil),
    ]

    // The colors of the game's strings, for rows of the Colors page with no color set (DEFAULT_N, DEFAULT_CB in
    // mod_colors.c), low E first
    static let stringNames = ["Low E", "A", "D", "G", "B", "High E"]
    static let defaultColors = ["FF4F5A", "E2C102", "1DACF9", "FF9216", "3FCC0C", "C825ED"]
    static let defaultColorblindColors = ["C12A2A", "A3F400", "1DACF9", "DB7F41", "00C68E", "7648A8"]
    // Seven-String Guitar's low B while [Seven String] string6_N is empty (DEFAULT_RGB in seven_colours.c)
    static let lowBColor = "C8C8C8"

    static let highwayColors: [(label: String, key: String)] = [
        ("Numbered Lanes", "CustomHighwayNumbered"),
        ("Unnumbered Lanes", "CustomHighwayUnNumbered"),
        ("Gutters", "CustomHighwayGutter"),
        ("Fret Numbers", "CustomFretNubmers"),  // spelled so on Windows too
    ]

    // [Custom Tunings]: the tunings the library's default RSMods.ini has (DEFAULT_INI in rsmods_ini.c), for Add Common
    // Tunings. The strings' semitones from E standard, low E first.
    static let commonTunings: [(name: String, strings: [Int])] = [
        ("F Standard", [1, 1, 1, 1, 1, 1]), ("C# Drop B", [-5, -3, -3, -3, -3, -3]),
        ("C Drop Bb", [-6, -4, -4, -4, -4, -4]), ("B Standard", [-5, -5, -5, -5, -5, -5]),
        ("B Drop A", [-7, -5, -5, -5, -5, -5]), ("Bb Standard", [-6, -6, -6, -6, -6, -6]),
        ("A Standard", [-7, -7, -7, -7, -7, -7]), ("Double Drop D", [-2, 0, 0, 0, 0, -2]),
        ("Open C", [-4, -2, -2, 0, 1, 0]),
    ]
    static let openStrings = [40, 45, 50, 55, 59, 64]  // E standard, MIDI: E2 A2 D3 G3 B3 E4

    // A [Custom Tunings] value: six whole numbers, spaces or commas between (rsm_tuning_parse); nil otherwise
    static func tuningStrings(_ value: String) -> [Int]? {
        let parts = value.split(whereSeparator: { $0 == " " || $0 == "," || $0 == "\t" })
        let numbers = parts.compactMap { Int($0) }
        guard parts.count == 6, numbers.count == 6, numbers.allSatisfy({ (-24...24).contains($0) }) else { return nil }
        return numbers
    }

    static func tuningValue(_ strings: [Int]) -> String { strings.map(String.init).joined(separator: " ") }

    // The notes of a tuning, low string first: "B1 E2 A2 D3 F#3 B3"
    static func tuningNotes(_ strings: [Int]) -> String {
        zip(openStrings, strings).map { noteName($0 + $1) }.joined(separator: " ")
    }

    // [Default Tones]: the game's tones the mod sets (DefaultTones in mod_cache.c), and which a bass tone suits
    static let toneSlots: [(label: String, key: String, bass: Bool)] = [
        ("Rhythm", "Rhythm", false), ("Lead", "Lead", false), ("Bass", "Bass", true),
    ]
    // Each Guitarcade game's tone, by the name the game gives it
    static let guitarcadeTones: [(label: String, key: String)] = [
        ("Temple of Bends", "TempleOfBends"), ("Scale Warriors", "ScaleWarriors"),
        ("String Skip Saloon", "StringSkipSaloon"), ("Scale Racer", "ScaleRacer"),
        ("Star Chords Laser", "StarChordsLaser"), ("Ninja Slide N", "NinjaSlideN"),
        ("Hurtlin' Hurdles", "HurtlinHurdles"), ("Harmonic Heist", "HarmonicHeist"), ("Ducks Redux", "DucksRedux"),
        ("Rainbow Laser", "RainbowLaser"), ("Gone Wailin'", "GoneWailin"),
    ]

    // The mods' defaults for keys RSMods.ini lacks (DEFAULTS in rsmods_ini.c)
    static let defaults: [String: [String: String]] = [
        Section.toggles: [
            "RRSpeedAboveOneHundred": "off", "AllowLooping": "off", "AllowRewind": "off", "SongPreviews": "off",
            "ShowSongTimer": "off", "ShowSongTimerWhen": "manual", "VolumeControl": "off", "ForceProfileLoad": "off",
            "ForceReEnumeration": "off", "OverrideInputVolumeEnabled": "off", "OverrideInputVolumeDevice": "",
            "ScreenShotScores": "off", "BypassTwoRTCMessageBox": "off", "AllowAudioInBackground": "off",
            "LinearRiffRepeater": "off", "ToggleLoft": "off", "ToggleLoftWhen": "manual", "Headstock": "off",
            "RemoveHeadstockWhen": "song", "Lyrics": "off", "RemoveLyricsWhen": "manual",
            "Skyline": "off", "ToggleSkylineWhen": "song", "Fretless": "off", "Inlays": "off", "LaneMarkers": "off",
            "CameraDirector": "off", "ToneHost": "off", "SongStems": "off", "Sunlight": "off", "CustomStringColors": "0",
            "SeparateNoteColors": "off", "RainbowNotes": "off", "RainbowStrings": "off", "ProfileToLoad": "",
            "RemoveFingerprints": "off", "GreenScreenWall": "off", "UseCustomNSPTimer": "off", "SecondaryMonitor": "off",
            "ExtendedRange": "off", "ExtendedRangeDropTuning": "off", "ExtendedRangeFixBassTuning": "off",
            "ShowCurrentNoteOnScreen": "off", "GuitarSpeak": "off", "PreventMidSongPause": "off", "FixBrokenTones": "off",
            "DisplayCurrentAccuracy": "off", "CustomSongListTitles": "off", "CustomTunings": "off", "DefaultTones": "off",
            "MoreSongLists": "off", "SevenString": "off", "PlainPackages": "off",
        ],
        Section.sevenString: [
            "string6_N": "", "string6_CB": "", "ExtendedRangeColor": "on", "ChordWideMisses": "2", "TuningNames": "on",
            "TuningNamePrefix": "", "TuningNameSuffix": "7Str", "Headstock7": "on", "Headstock7Wood": "0.6",
        ],
        Section.defaultTones: [
            "Rhythm": "", "Lead": "", "Bass": "", "TempleOfBends": "", "ScaleWarriors": "", "StringSkipSaloon": "",
            "ScaleRacer": "", "StarChordsLaser": "", "NinjaSlideN": "", "HurtlinHurdles": "", "HarmonicHeist": "",
            "DucksRedux": "", "RainbowLaser": "", "GoneWailin": "",
        ],
        Section.settings: [
            "RRSpeedInterval": "2", "LoopingLeadUp": "0", "RewindBy": "5000", "RewindLeadup": "2000",
            "VolumeControlInterval": "5", "CheckForNewSongsInterval": "5000", "OverrideInputVolume": "17",
            "CameraZoom": "100", "CameraHeight": "0", "CameraSide": "0", "CameraTilt": "0", "SunlightMode": "1",
            "SeparateNoteColorsMode": "0", "CustomNSPTimeLimit": "10000", "SecondaryMonitorXPosition": "0",
            "SecondaryMonitorYPosition": "0", "ExtendedRangeModeAt": "-5", "NumberOfSongLists": "6",
            "SongStemsGuitarLevel": "0", "SongStemsBassLevel": "0", "SongStemsCacheMB": "500",
        ],
        Section.keybinds: [
            "RRSpeedKey": "R", "LoopStartKey": "Y", "LoopEndKey": "U", "RewindKey": "Z", "ShowSongTimerKey": "I",
            "ForceReEnumerationKey": "F", "ToggleLoftKey": "T", "RemoveLyricsKey": "L", "CameraZoomKey": "K",
            "CameraHeightKey": "H", "CameraSideKey": "D", "CameraTiltKey": "M", "CameraResetKey": "J",
            "SunlightKey": "G", "RainbowNotesKey": "N", "RainbowStringsKey": "V", "ToggleExtendedRangeKey": "E",
        ],
        Section.guitarSpeak: [
            "GuitarSpeakDeleteWhen": "0", "GuitarSpeakSpaceWhen": "0", "GuitarSpeakEnterWhen": "0",
            "GuitarSpeakTabWhen": "0", "GuitarSpeakPGUPWhen": "0", "GuitarSpeakPGDNWhen": "0", "GuitarSpeakUPWhen": "0",
            "GuitarSpeakDNWhen": "0", "GuitarSpeakESCWhen": "0", "GuitarSpeakCloseWhen": "0",
            "GuitarSpeakOBracketWhen": "0", "GuitarSpeakCBracketWhen": "0", "GuitarSpeakTildeaWhen": "0",
            "GuitarSpeakForSlashWhen": "0", "GuitarSpeakAltWhen": "0", "GuitarSpeakWhileTuning": "off",
        ],
        Section.guiSettings: ["BackupProfile": "on", "NumberOfBackups": "50"],
        Section.audioKeybinds: [
            "MasterVolumeKey": "5", "SongVolumeKey": "6", "Player1VolumeKey": "7", "Player2VolumeKey": "8",
            "MicrophoneVolumeKey": "9", "VoiceOverVolumeKey": "0", "SFXVolumeKey": "S", "DisplayMixerKey": "P",
            "MutePlayer1Key": "X", "MutePlayer2Key": "C", "ChangedSelectedVolumeKey": "B",
            "SongStemVolumeKey": "4",
        ],
        Section.highwayColors: [
            "CustomHighwayColors": "off", "CustomHighwayNumbered": "", "CustomHighwayUnNumbered": "",
            "CustomHighwayGutter": "", "CustomFretNubmers": "",
        ],
    ]
}
