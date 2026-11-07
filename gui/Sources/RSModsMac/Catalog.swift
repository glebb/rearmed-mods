import Foundation

// What the app shows: the mods rearmed-mods.dylib (../src) has, with the labels of the Windows
// RSMods GUI (GUI/UI.Designer.cs). When a mod is ported, move it from `notYetOnMac` to `mods`, add its keys to
// `defaults`, and its settings (if any) to ModSettingsView.

enum Section {
    static let toggles = "Toggle Switches"
    static let settings = "Mod Settings"
    static let keybinds = "Keybinds"
    static let audioKeybinds = "Audio Keybindings"
    static let stringColors = "String Colors"
    static let highwayColors = "Highway Colors"
}

struct ModToggle: Identifiable {
    let label: String
    let key: String  // in [Toggle Switches], unless `section` says otherwise
    let help: String
    var note: String? = nil  // shown under the checkbox, e.g. a known problem on macOS
    var section = Section.toggles
    var id: String { key }
}

struct Keybind: Identifiable {
    let label: String
    let section: String
    let key: String
    let mod: String?  // the toggle it belongs to, to dim it while that mod is off
    var id: String { section + "/" + key }
}

enum Catalog {
    // In the Windows GUI's order of its "Enabled Mods" box
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
                  help: "The rewind key moves the song back."),
        ModToggle(label: "Enable Looping", key: "AllowLooping",
                  help: "Set a loop with the loop keys in Learn a Song and Non-Stop Play."),
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
                  help: "The strings, and the notes unless they have their own colors, in the colors of the Colors tab."),
        ModToggle(label: "Separate Note Colors", key: "SeparateNoteColors",
                  help: "The notes get their own colors, from the Colors tab."),
        ModToggle(label: "Custom Highway Colors", key: "CustomHighwayColors",
                  help: "The lanes, gutters and fret numbers in the colors of the Colors tab.",
                  section: Section.highwayColors),
        ModToggle(label: "Rainbow Strings", key: "RainbowStrings",
                  help: "The rainbow strings key cycles the strings through the colors."),
        ModToggle(label: "Rainbow Notes", key: "RainbowNotes",
                  help: "The rainbow notes key cycles the notes through the colors, in a song."),
        ModToggle(label: "Camera Director", key: "CameraDirector",
                  help: "Your own view of the note highway: zoom, height, side and tilt, with keys in a song."),
        ModToggle(label: "Sunlight", key: "Sunlight",
                  help: "For a screen outdoors: the sunlight key goes through off, bright and daylight."),
        ModToggle(label: "Your Own Plug-in (ToneHost)", key: "ToneHost",
                  help: "rs-tonehost plays the guitar through your Audio Unit and follows the game's tones."),
    ]

    // Mods of the Windows GUI that the Mac library doesn't have yet
    static let notYetOnMac = [
        "Extended Range Support", "Greenscreen Wall", "Remove Fingerprints", "Guitar Speak", "Prevent Pause Mid Song",
        "Show Current Accuracy", "Show Current Note", "Custom NSP Timer", "Prevent Buggy Tones", "Auto Tune Song",
        "Backup Profile", "Fast Song Scan", "Fast Profile Load/Save",
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
    ]

    static let audioKeybinds: [Keybind] = [
        Keybind(label: "Master Volume", section: Section.audioKeybinds, key: "MasterVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Song Volume", section: Section.audioKeybinds, key: "SongVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Player 1 Volume", section: Section.audioKeybinds, key: "Player1VolumeKey", mod: "VolumeControl"),
        Keybind(label: "Player 2 Volume", section: Section.audioKeybinds, key: "Player2VolumeKey", mod: "VolumeControl"),
        Keybind(label: "Microphone Volume", section: Section.audioKeybinds, key: "MicrophoneVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Voice-Over Volume", section: Section.audioKeybinds, key: "VoiceOverVolumeKey", mod: "VolumeControl"),
        Keybind(label: "SFX Volume", section: Section.audioKeybinds, key: "SFXVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Change Selected Volume", section: Section.audioKeybinds, key: "ChangedSelectedVolumeKey", mod: "VolumeControl"),
        Keybind(label: "Show Volume Mixer", section: Section.audioKeybinds, key: "DisplayMixerKey", mod: "VolumeControl"),
        Keybind(label: "Mute Player 1", section: Section.audioKeybinds, key: "MutePlayer1Key", mod: nil),
        Keybind(label: "Mute Player 2", section: Section.audioKeybinds, key: "MutePlayer2Key", mod: nil),
    ]

    // The colors of the game's strings, for rows of the Colors tab with no color set (DEFAULT_N, DEFAULT_CB in
    // mod_colors.c), low E first
    static let stringNames = ["Low E", "A", "D", "G", "B", "High E"]
    static let defaultColors = ["FF4F5A", "E2C102", "1DACF9", "FF9216", "3FCC0C", "C825ED"]
    static let defaultColorblindColors = ["C12A2A", "A3F400", "1DACF9", "DB7F41", "00C68E", "7648A8"]

    static let highwayColors: [(label: String, key: String)] = [
        ("Numbered Lanes", "CustomHighwayNumbered"),
        ("Unnumbered Lanes", "CustomHighwayUnNumbered"),
        ("Gutters", "CustomHighwayGutter"),
        ("Fret Numbers", "CustomFretNubmers"),  // spelled so on Windows too
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
            "CameraDirector": "off", "ToneHost": "off", "Sunlight": "off", "CustomStringColors": "0",
            "SeparateNoteColors": "off", "RainbowNotes": "off", "RainbowStrings": "off",
        ],
        Section.settings: [
            "RRSpeedInterval": "2", "LoopingLeadUp": "0", "RewindBy": "5000", "RewindLeadup": "2000",
            "VolumeControlInterval": "5", "CheckForNewSongsInterval": "5000", "OverrideInputVolume": "17",
            "CameraZoom": "100", "CameraHeight": "0", "CameraSide": "0", "CameraTilt": "0", "SunlightMode": "1",
            "SeparateNoteColorsMode": "0",
        ],
        Section.keybinds: [
            "RRSpeedKey": "R", "LoopStartKey": "Y", "LoopEndKey": "U", "RewindKey": "Z", "ShowSongTimerKey": "I",
            "ForceReEnumerationKey": "F", "ToggleLoftKey": "T", "RemoveLyricsKey": "L", "CameraZoomKey": "K",
            "CameraHeightKey": "H", "CameraSideKey": "D", "CameraTiltKey": "M", "CameraResetKey": "J",
            "SunlightKey": "G", "RainbowNotesKey": "N", "RainbowStringsKey": "V",
        ],
        Section.audioKeybinds: [
            "MasterVolumeKey": "5", "SongVolumeKey": "6", "Player1VolumeKey": "7", "Player2VolumeKey": "8",
            "MicrophoneVolumeKey": "9", "VoiceOverVolumeKey": "0", "SFXVolumeKey": "S", "DisplayMixerKey": "P",
            "MutePlayer1Key": "X", "MutePlayer2Key": "C", "ChangedSelectedVolumeKey": "B",
        ],
        Section.highwayColors: [
            "CustomHighwayColors": "off", "CustomHighwayNumbered": "", "CustomHighwayUnNumbered": "",
            "CustomHighwayGutter": "", "CustomFretNubmers": "",
        ],
    ]
}
