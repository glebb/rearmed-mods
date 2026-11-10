# rearmed-mods

[RSMods](https://github.com/Lovrom8/RSMods) for **RS Re-ARMed**, the native Apple Silicon port of Rocksmith 2014, and
some mods of its own. One library for both of RS Re-ARMed's apps, RS Re-ARMed and RS Re-ARMed Learn & Play, and a Mac app
to install and set it up.

RSMods, "the Rocksmith Mods that nobody asked for", is a mod collection for the Windows version of Rocksmith
2014 Remastered, made by LovroM8 and Ffio1 with help from ZagatoZee, Kokolihapihvi and L0fka. rearmed-mods brings
many of its features to [RS Re-ARMed](https://github.com/bbelgg/rs-rearmed) on macOS: the same features, the same
`RSMods.ini` settings and the same default keys, rewritten for the Mac build of the game.

All credit for the ideas, the features and their design goes to the RSMods team. This is an independent port:
not made or endorsed by the RSMods team, and not affiliated with Ubisoft. Rocksmith is a trademark of Ubisoft.

![Re-ARMed Mods.app, with the mods' pages in the sidebar and a mod's settings and keys under its switch](docs/images/app.png)

## Table of Contents

- [Requirements](#requirements)
- [Install](#install)
- [Settings](#settings)
- [Mods](#mods)
- [Not on macOS yet](#not-on-macos-yet)
- [Built into RS Re-ARMed](#built-into-rs-re-armed)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [Building and developing](#building-and-developing)
- [Credits](#credits)
- [License](#license)

## Requirements

- An Apple Silicon Mac with **RS Re-ARMed 1.5.1 or later** installed. The library is built on what 1.5.1 brought, the
  game's functions and data by name, so one file runs on both editions. With an older version it doesn't load.
- The string, note and highway colors need RS Re-ARMed 1.9.0, which has the game's color table by name
  and the render API, and the note and highway colors need its Metal renderer. Without them those mods stay off and the
  rest works. Sunlight needs the Metal renderer too.
- Show Current Note and Guitar Speak need RS Re-ARMed 1.11.0, which names the note the game hears; with an older one
  those two stay off. Show Current Accuracy, and the grey-note reset of looping and rewinding, need it too, for the
  game's note counts (in Learn & Play: Learn a Song and its pause menu, not Score Attack), and so do Prevent Buggy
  Tones and Custom Song List Titles, for its hook points on the tone change and a song list's name.
- Seven-String Guitar and Plain Packages need RS Re-ARMed's edits and mid points: in Learn & Play from 1.12.0, on the
  Mac from 1.13.0; with an older one they stay off.
- Song Stems needs RS Re-ARMed 1.12.0, for its hook point on the audio the game decodes, and macOS 15 or later for
  its helper; without them it stays off and the songs play as they are.
- macOS 13 or later for the app.
- To build from source: the Xcode command line tools (`xcode-select --install`), with Swift 5.9 or later.

rearmed-mods doesn't change the game or RS Re-ARMed: it is one library in each edition's mods folder. Delete it and the game
is as before.

## Install

1. Download `Re-ARMed-Mods-<version>.zip` from the [Releases](https://github.com/glebb/rearmed-mods/releases) page
   and open it; you get **Re-ARMed Mods.app**. Move it to Applications.
2. Open it. The first time, macOS says it can't verify the developer: the app isn't notarized by Apple. Go to
   **System Settings › Privacy & Security**, scroll down, click **Open Anyway** next to Re-ARMed Mods, and
   confirm. (On older macOS: right-click the app, choose **Open**, then **Open** again.)
3. Quit RS Re-ARMed if it's running, and click **Install Mods** at the top of the app's window.
4. Start RS Re-ARMed as usual, and turn mods on in the app.

The app carries the mods library and copies it into `~/Library/Application Support/RS Re-ARMed/mods/`, and
`RS Re-ARMed/learn-play/mods/` if Learn & Play is installed, which the game loads at startup (it removes the download's quarantine flag from that copy, so only the app itself needs
**Open Anyway**). It checks that RS Re-ARMed is installed and new enough. When you download a newer version of
the app, it offers **Update Mods**; **Remove Mods** takes the library out again. A reinstall of RS Re-ARMed keeps
the mods folder.

On its first start with the mods, the game creates `RSMods.ini` in its game folder
(`~/Library/Application Support/RS Re-ARMed/game/`, `learn-play/game/` for Learn & Play) with every mod off but Backup
Profile, which is on as in RSMods.

### From source

```bash
git clone https://github.com/glebb/rearmed-mods.git
cd rearmed-mods
./build.sh --install      # the library only, into the mods folders (quit the game first)
gui/build-app.sh --open   # the app, with the library inside
```

To uninstall, remove the mods in the app (or delete `rearmed-mods.dylib` from the mods folders), then delete the app
and, if you like, `RSMods.ini` in the game folder.

## Settings

The settings are in `RSMods.ini` in the game folder, one for each edition: the same sections (`[Toggle Switches]`,
`[Mod Settings]`, `[Keybinds]`, `[Audio Keybindings]`, `[String Colors]`, `[Highway Colors]`, `[Guitar Speak]`,
`[SongListTitles]`, `[GUI Settings]`) and keys as RSMods on
Windows, so an `RSMods.ini` from Windows works too. Seven-String Guitar, which RSMods hasn't, has `[Seven String]`. Custom Tunings, the default tones and the number of song lists,
which RSMods keeps in its app, have `[Custom Tunings]`, `[Default Tones]` and `NumberOfSongLists` here.
The mods re-read the file while the game runs: a change takes effect within a second, no restart.

**Re-ARMed Mods.app** (in `gui/`) edits it, with the labels of the Windows RSMods GUI: the mods are on the sidebar's pages by what
they change (**Screen**, **Highway & Neck**, **Colors**, **Practice**, **Sound & Tones** and so on), each with a switch, and a mod that is on shows its
settings and its keys under it (click a key field, press a key); **Keybindings** has every key, and the search field finds a mod by name. The switch at the bottom left goes
between the two editions' settings. Or edit the file in any text editor.

Keys are letters, digits, `F1`-`F20` or Windows `VK_` names (as RSMods writes them), mapped to the Mac keys.
Function keys may need **Fn** on a Mac keyboard. Where RSMods uses Ctrl + a key (for example to lower a value),
it's the Control key.

## Mods

What each mod does, its settings, and how it differs from RSMods on Windows.

* **Allow Riff Repeater Speed Above 100%**
  * In a song, the speed key raises the song speed by `RRSpeedInterval` percent; Control + the key lowers it
    (6.25% to 400%). The speed shows at the top of the screen and, after each change, in
    `riff_repeater_speed.txt` in the game folder, for stream overlays.
  * `[Toggle Switches] RRSpeedAboveOneHundred`, `[Mod Settings] RRSpeedInterval` (2), `[Keybinds] RRSpeedKey` (R)

* **Linear Riff Repeater**
  * The Riff Repeater slider's percent is the song speed: 68% plays at 68%, not at about 50%.
  * `LinearRiffRepeater`

* **Enable Looping**
  * In Learn a Song and Non-Stop Play, the loop start key sets the loop's start at the song position and the
    loop end key its end; past the end the song goes back to the start, `LoopingLeadUp` ms early. The loop shows
    at the top. Control + loop start clears the loop, Control + loop end clears its end. Leaving the song or
    opening Riff Repeater clears it.
  * `AllowLooping`, `[Mod Settings] LoopingLeadUp` (0), `[Keybinds] LoopStartKey` (Y), `LoopEndKey` (U)
  * While paused in a loop the game's grey-note time is the loop's start, as RSMods sets it: notes before that time
    are grey and not detected, and the game sets it to where the song paused, so the notes of the loop count again.
    Needs RS Re-ARMed 1.11.0 (in Learn & Play, in Learn a Song). In a run on the Mac the time went back to the loop's
    start at each pause in the loop; how the notes look after it hasn't been watched yet.

* **Allow Rewinding**
  * The rewind key moves the song back `RewindBy` + `RewindLeadup` ms.
  * `AllowRewind`, `[Mod Settings] RewindBy` (5000), `RewindLeadup` (2000), `[Keybinds] RewindKey` (Z)
  * The grey-note time goes back too, `RewindLeadup` ms before where the song goes (near the song's start, to the
    start), as RSMods does, so the notes from there count again. Needs RS Re-ARMed 1.11.0 (in Learn & Play, in Learn a
    Song).

* **Show Song Timer**
  * The song position at the top right: in every song (`ShowSongTimerWhen = automatic`) or shown and hidden with
    the key (`manual`, the key works in both).
  * `ShowSongTimer`, `ShowSongTimerWhen` (manual), `[Keybinds] ShowSongTimerKey` (I)
  * Different: the position comes from the game's audio engine. It appears once the song's audio plays, a few
    seconds in.

* **Control Game Volume**
  * Each channel's key raises its volume by `VolumeControlInterval`, Control + the key lowers it; the changed
    channel shows at the top left. Hold the mixer key to see every channel. The mute keys mute and unmute player
    1 and 2 (they work with this mod off too).
  * `VolumeControl`, `[Mod Settings] VolumeControlInterval` (5), `[Audio Keybindings]`: master 5, song 6,
    player 1 7, player 2 8, microphone 9, voice-over 0, SFX S, mixer P, mute player 1 X, mute player 2 C,
    change selected volume B
  * Different: the changed channel's volume hides again after 3 seconds. With Song Stems on, its part has a line too
    (Guitar in Song or Bass in Song, `SongStemVolumeKey`, 4).

* **Remove Song Previews**
  * No previews while you browse the song lists.
  * `SongPreviews`

* **Autoload Last Used Profile**
  * At startup the game goes straight into the profile at the top of the list (the last used one), or the one named in
    `ProfileToLoad`.
  * `ForceProfileLoad`, `ProfileToLoad` (empty)
  * Different: RSMods presses Down until the game highlights the name. Here the list comes from the profiles' own list
    (`LocalProfiles.json`, read with the game's key), last used first as the game shows it: the mod presses Down as many
    times as the profile is down the list, then Enter. A name that isn't there enters nothing, as in RSMods.

* **Force ReEnumeration**
  * The game finds songs you added without a restart: with the key (`manual`) or automatically when the number of
    files under `dlc/` changes (`automatic`, checked every `CheckForNewSongsInterval` ms).
  * `ForceReEnumeration` (off / manual / automatic), `[Mod Settings] CheckForNewSongsInterval` (5000),
    `[Keybinds] ForceReEnumerationKey` (F)
  * Different: RSMods' "Enumerate" entry in the main menu is not a mod here: [RS Re-ARMed has it](#built-into-rs-re-armed).

* **Override Input Volume**
  * Keeps an input device, such as the Real Tone cable, at `OverrideInputVolume` percent (17 is the game's
    default), checked every 2 seconds.
  * `OverrideInputVolumeEnabled`, `OverrideInputVolumeDevice`, `[Mod Settings] OverrideInputVolume` (17)
  * Different: the device is part of its name as macOS shows it (Audio MIDI Setup), e.g.
    `Rocksmith USB Guitar Adapter`; empty means the default input. Not in RSMods: several devices, separated by
    `;`, each optionally with its own percent:
    `OverrideInputVolumeDevice = Rocksmith USB Guitar Adapter; Rocksmith Game (OBS) = 100`.

* **Screenshot Scores**
  * 8 seconds after a score screen opens (Learn a Song, Score Attack, Duet, Head-to-Head), the screen is saved.
  * `ScreenShotScores`
  * Different: RSMods presses F12 for a Steam screenshot. RS Re-ARMed runs without the Steam client, so the mod
    saves the picture itself, as a PNG in `~/Pictures/RS Re-ARMed/` (`<song key> <date> <time>.png`; a `/` or `:`
    in a custom song's key becomes `_`).

* **Bypass 2+ Real Tone Cable Popup**
  * No popup when two Real Tone cables are connected.
  * `BypassTwoRTCMessageBox`
  * Different: the game may still show its warning as a notification for 10 seconds.

* **Play Audio In Background**
  * The song keeps playing, with sound, while another app is active.
  * `AllowAudioInBackground`

* **Prevent Pause Mid Song**
  * The same in songs only: another app coming to the front doesn't pause the song.
  * `PreventMidSongPause`

* **Prevent Buggy Tones**
  * In some songs the tone system gives up and plays the clean tone whatever you choose, until a restart: a tone change
    that never ends makes every later one wait for it. With this on, a new change goes ahead.
  * `FixBrokenTones`
  * As RSMods, which jumps past the wait in the game's code; here a hook on the tone change clears what makes it wait.
    Needs RS Re-ARMed 1.11.0, which has the hook point. Not yet checked in the game (the bug is rare).

* **Custom Non-Stop Play Timer**
  * Non-Stop Play waits `CustomNSPTimeLimit` ms between songs instead of 10.99 seconds (2 to 60 seconds).
  * `UseCustomNSPTimer`, `[Mod Settings] CustomNSPTimeLimit` (10000)
  * As RSMods, it changes the game's constant the countdown is measured against. The library finds it by its value, which
    each game's image has once.

* **Start On Secondary Monitor**
  * When the game starts, its window moves to the screen whose top left corner is `SecondaryMonitorXPosition`,
    `SecondaryMonitorYPosition`: in points, from the top left of the main screen, down (the app's **Choose Screen** fills
    them in).
  * `SecondaryMonitor`, `[Mod Settings] SecondaryMonitorXPosition` (0), `SecondaryMonitorYPosition` (0)
  * As in RSMods, it's meant for the game in a window, at that screen's size. In full screen the window fills that
    screen, but the game keeps the resolution of the main screen, and its own full screen switch brings it back there.

* **Toggle Loft**
  * The venue behind the note highway goes black, for example to key it out in OBS with a Luma Key, as on
    Windows (see RSMods' FAQ). Turn off Venue Mode in the game's settings too.
  * `ToggleLoft`, `ToggleLoftWhen` (manual: the key; song: in songs; startup: always),
    `[Keybinds] ToggleLoftKey` (T)
  * Different: besides the loft camera that RSMods changes, the Mac build draws the song's backdrop, the venue's
    props and floor, and your tone's amps and cabinets separately; they are all hidden, with the surface under the
    strings (the guitar's fret board and neck) and, in a song, the haze around the strings. In lessons (Guided
    Experience) the loft stays, as in RSMods. Gear the game adds while the loft is off (a tone change) disappears
    within 15 seconds. RSMods' green screen wall is not on macOS yet.

* **Remove Headstock**
  * Hides the headstock and tuning pegs, in songs (`song`) or everywhere (`startup`).
  * `Headstock`, `RemoveHeadstockWhen` (song)

* **Remove Skyline**
  * Hides the phrase bars (dynamic difficulty) at the top of the screen, in songs or everywhere.
  * `Skyline`, `ToggleSkylineWhen` (song)

* **Remove Lyrics**
  * Hides the lyrics, with the key (`manual`) or always (`startup`).
  * `Lyrics`, `RemoveLyricsWhen` (manual), `[Keybinds] RemoveLyricsKey` (L)

* **Fretless Mode / Remove Inlays / Remove Lane Markers**
  * Hide the fret wires, the position dots on the neck, and the lane lines on the highway.
  * `Fretless`, `Inlays`, `LaneMarkers`

* **Remove Fingerprints**
  * Hides a chord's hand indicators on the neck: the fingerprints and their finger numbers.
  * `RemoveFingerprints`
  * Different: the game shows and hides these itself, so they are shrunk to nothing rather than switched off. Ones the game
    makes during a song go within 15 seconds. Not yet checked in the game.

* **Greenscreen Wall**
  * Only the wall behind the stage goes black; the amps and the rest stay. Toggle Loft, when the loft is off, wins. With
    Toggle Loft on `song` or `startup`, lessons (which keep the loft) lose their wall instead, as in RSMods.
  * `GreenScreenWall`
  * Different: the Mac build draws the wall, its trim and its wiring as separate objects (`backwall_*`); all go. Not yet
    checked in the game.

* **Custom String Colors**
  * The strings and what the game makes from them (glow, inactive, tuning pegs, text), for normal and colorblind
    mode. The notes follow unless they have their own colors.
  * `CustomStringColors = 2`, `[String Colors]` `string0_N` to `string5_N`, `string0_CB` to `string5_CB` (RRGGBB)
  * Different: each kind of color is made from your color as the game makes its own, from the game's table.

* **Separate Note Colors**
  * The notes get their own colors.
  * `SeparateNoteColors = on` with `[Mod Settings] SeparateNoteColorsMode = 2`, `[String Colors]` `note0_N` and so on

* **Rainbow Strings / Rainbow Notes**
  * The key turns it on and off: a turn of the colors in about three seconds. Rainbow notes work in a song.
  * `RainbowStrings`, `RainbowNotes`, `[Keybinds] RainbowStringsKey` (V), `RainbowNotesKey` (N)

* **Extended Range Support**
  * In a song tuned low enough for a 7-string guitar or a 5-string bass, and in its tuner, the strings and notes take
    extended range colors: ZZ's (`CustomStringColors = 1`: the game's colors a string down, a green for the low string),
    your colorblind colors (`2`, the Colors page), or the game's colorblind colors (`0`). The key turns it on or off for the
    song.
  * `ExtendedRange`, `ExtendedRangeDropTuning`, `ExtendedRangeFixBassTuning`, `[Mod Settings] ExtendedRangeModeAt` (-5:
    a lowest string at B or lower), `[Keybinds] ToggleExtendedRangeKey` (E)
  * RSMods' rule: a drop tuning counts from two semitones lower unless `ExtendedRangeDropTuning` is on, and a bass tuned an
    octave down (A220) an octave lower.
  * Different: RSMods reads the tuning from the game's tuner in memory. Here it comes from the song's archive (each
    arrangement's manifest has it): the library lists the manifests of `songs.psarc` and every archive in `dlc/` once
    (about two seconds, on its own thread, and again when a song isn't there), reading their encrypted tables with the
    game's key. The tone says guitar or bass; when the song's arrangements for it differ (a low lead, a standard rhythm),
    the colors change only if all call for it, and the key is there for the rest. RSMods turns the game's colorblind mode
    on; here the game's mode stays and both color sets get the extended range colors. Needs what the colors need.

* **Show Current Note**
  * The note you play, as the game hears it, as `E2`: in a song left of the middle, a little below it; elsewhere at the
    top, as `Current Note: E2`.
  * `ShowCurrentNoteOnScreen`
  * As RSMods, it reads the note from the game's pitch detection, which RS Re-ARMed names for mods from 1.11.0. With an
    older RS Re-ARMed it stays off, and the log says so. Checked on the Mac with a guitar (single notes, open low E to the 18th
    fret of the high E); not yet in Learn & Play or with a bass.

* **Guitar Speak**
  * In the menus, each new note you play presses the key set to it: Delete, Space, Enter, Tab, Page Up and Down, the
    arrows, Escape, the brackets, tilde, slash, Option. The close note stops it, and starts it again. Off in songs,
    lessons and calibration, and in the tuners unless `GuitarSpeakWhileTuning` is on.
  * `GuitarSpeak`, `[Guitar Speak]` `GuitarSpeakDeleteWhen` to `GuitarSpeakAltWhen`, `GuitarSpeakCloseWhen` (MIDI notes;
    0 none; E3, the low E's 12th fret, is 52), `GuitarSpeakWhileTuning` (off). The app's Profiles & Menus page sets them.
  * The notes are the game's, as for Show Current Note, and a new note is what RSMods counts as one: another note, or a
    note after the last one ended. Needs RS Re-ARMed 1.11.0. Not yet checked in the game.

* **Show Current Accuracy**
  * In a song, once it plays, the notes you hit out of the notes played so far, as `87.50%`, under the song timer
    (100% before the first note).
  * `DisplayCurrentAccuracy`
  * The counts are the game's own, the ones RSMods reads on Windows; on the Mac one set serves Learn a Song and Score
    Attack. RS Re-ARMed 1.11.0 names them. Seen in Learn a Song on the Mac; the counts were read in Learn a Song and
    Score Attack, playing and paused. In Learn & Play they are reached by RSMods' own path, in Learn a Song, playing
    and paused; its Score Attack counts in another object, which that path doesn't reach, so there nothing shows.
  * Different: RSMods shows 0.00% in the song's other menus (Riff Repeater, the tuner, the mixer); here nothing shows
    there.

* **Custom Song List Titles**
  * Your own names for the song lists in the game's menus, such as Learn a Song's song list filter and Non-Stop Play's
    choice of list. A list without one keeps the game's name (SONG LIST 1 and so on); All Songs and Favorites keep theirs.
  * `CustomSongListTitles`, `[SongListTitles]` `SongListTitle_1` to `SongListTitle_20` (empty). The app's Songs & Lists page
    sets them.
  * As RSMods, the title takes the place of the name the game builds for a list, the localized "SONG LIST" and its number.
    RSMods changes that function's code; here a hook point on it (RS Re-ARMed 1.11.0) puts the title in after it. Lists 7
    to 20 are the ones [More Song Lists](#more-song-lists) (or RSMods' app on Windows) adds to a profile: they get their
    titles too, but Non-Stop Play still offers only the first six ([below](#not-on-macos-yet)). Seen on the Mac in
    Learn a Song's song list filter, the song list chosen there and Non-Stop Play's choice of list, with short titles
    and titles longer than the game's name; not yet in Learn & Play.
  * Different: RSMods has no switch, its titles are always on, and a list without one is called SONG LIST, with no
    number.

* **More Song Lists**
  * Your profiles get more song lists than the game's six, up to 20. The game's menus size themselves from a
    profile's lists (Learn a Song's song list filter, the song list editor, adding a song to a list: RSMods' notes, on
    Windows), and Custom Song List Titles names them. Non-Stop Play still offers the first six
    ([below](#not-on-macos-yet)).
  * `MoreSongLists`, `[Mod Settings] NumberOfSongLists` (6 to 20; 6). The app's Songs & Lists page sets the number.
  * As RSMods' app (Profile Edits, Add New SongList), an empty list is added after the last in the profile's
    `SongListsRoot`; the rest of the game sizes itself from that list. When the game starts, on its first frame
    and before it reads a profile, the library gives every profile in the save folder in use that many lists, and
    writes a profile only when it changes, as the game writes one (checked on four of the Mac's profiles: the same
    bytes up to the data's end), and only once the new file reads back. The game then saves the lists as its own.
    Checked on the Mac: four profiles went from 6 to 8 lists at the start, Learn a Song had them, and the game's own
    save of a profile kept all 8. Not yet in Learn & Play.
  * Different: RSMods' app changes the profile you choose, a list at a time, and removes the newest list whatever is in
    it. Here the number is for every profile; with a lower number the last lists go only while they are empty (a
    list with songs stays: take its songs out in the game first). Off, the profiles stay as they are. A profile
    made while the game runs gets its lists at the next start. With [Backup Profile](#backup-profile) on, the app
    backs the profiles up when it opens, before the game's next start changes them.

* **Custom Tunings**
  * The game knows your tunings by name: a song in B Standard is B Standard in the song list, the song's details and
    the tuner, not CUSTOM TUNING.
  * `CustomTunings`, `[Custom Tunings]`: one line a tuning, its name = the six strings' semitones from E standard, low
    string first (`B Standard = -5 -5 -5 -5 -5 -5`). A new `RSMods.ini` has nine common ones (F Standard, C# Drop B,
    C Drop Bb, B Standard, B Drop A, Bb Standard, A Standard, Double Drop D, Open C); the app's Songs & Lists page edits them,
    and Add Common Tunings adds those to a list. A tuning with the same strings as one of the game's takes its place.
  * As RSMods, it adds the tunings to the game's `manifests/tuning.database.json`, and each name to the localization
    (`localization/maingame.csv`) under an id the file hasn't from 37500 on. RSMods' app writes them into `cache.psarc`;
    here `cache.psarc` stays as it is, and the library gives the game its own copies of the two archives they are in
    when the game opens them ([below](#how-it-works)), made again at every start (about a quarter of a second). From
    the game's next start, as it reads them once. Checked on the Mac: B Standard and C# Drop B songs are named so in
    the song list.
  * Different: RSMods' app has its own list (with a few more tunings) and no switch; here the list is in
    `RSMods.ini`, and the switch turns it off without restoring anything. In Learn & Play the library's archives are in
    its game folder's `Update` folder (`rearmed-mods-cache7.7z`, `rearmed-mods-cache4.7z`), next to RS Re-ARMed's.

* **Change Default Tones**
  * The game's default tones, slot 1 of the tone stick when the game loads up (Rhythm, Lead and Bass) and each
    Guitarcade game's, become tones you saved in your profiles.
  * `DefaultTones`, `[Default Tones]` `Rhythm`, `Lead`, `Bass`, and the Guitarcade games' by their tones' names
    (`TempleOfBends` to `GoneWailin`): a tone's name as you saved it (the tone designer's Save); empty keeps the
    game's. The app's Sound & Tones page lists your saved tones: the library writes their names to `rearmed-mods/tones.txt` in
    the game folder when the game starts.
  * As RSMods, the game's tone (`manifests/tonemanager.database.json`) takes the saved tone's gear; the emulated bass's
    is left alone, as in RSMods. From the game's next start. The library logs the tones it set; not yet checked by ear.
  * Different: the tone's volume comes with it, so it sounds as it does where you saved it. The Guitarcade games'
    tones are found by their keys.

* **Backup Profile**
  * Your profiles are backed up to `Profile_AutoBackups/<account>/` in the game folder: by the game, at most every 10
    minutes and only when they changed (the last hour, 6 hourly, 4 quarter-daily and 30 daily are kept), and by the app
    when it opens (`Before GUI`, the newest `NumberOfBackups`). `howto.txt` there says how to restore one.
  * `[GUI Settings] BackupProfile` (on, as in RSMods), `NumberOfBackups` (50)
  * The same folders and names as RSMods. A backup shares the files that didn't change with the one before it (hard
    links).

* **Custom Highway Colors**
  * The lanes (numbered and unnumbered go together), gutters and fret numbers. A color left empty stays the game's.
  * `[Highway Colors]` `CustomHighwayColors`, `CustomHighwayNumbered`, `CustomHighwayUnNumbered`, `CustomHighwayGutter`,
    `CustomFretNubmers` (spelled so on Windows too)

* **Camera Director** (not in RSMods)
  * Your own view of the note highway. In a song the keys zoom it, raise the camera, move it sideways and tilt it,
    Control + key the other way, and the reset key goes back to the game's view. The game's camera still follows the
    song and the stage stays where it is. Two players are untested.
  * `CameraDirector`, `[Mod Settings]` `CameraZoom` (100; 60 to 160), `CameraHeight` (0; -10 to 20), `CameraSide` (0; -20 to
    20), `CameraTilt` (0; -15 to 15), `[Keybinds]` `CameraZoomKey` (K), `CameraHeightKey` (H), `CameraSideKey` (D),
    `CameraTiltKey` (M), `CameraResetKey` (J)

* **Sunlight** (not in RSMods)
  * For a screen outdoors. The key goes through off, bright and daylight. Bright lifts the picture and brings every
    color up to one luminance. Daylight, in a song, turns it over: a white highway with dark notes, as dark on white
    stays readable in the sun. In the menus daylight is bright. In a song the venue goes too, as with the loft off.
  * `Sunlight`, `[Mod Settings] SunlightMode` (1; 0, 1 or 2), `[Keybinds] SunlightKey` (G)
  * The picture is changed by a filter on the game's window, so it needs no new RS Re-ARMed, and screenshots from the
    mods show the frame before it. Checked in the Mac edition, in a window and in full screen; not yet in Learn & Play.

* **Your Own Plug-in (ToneHost)** (not in RSMods)
  * `rs-tonehost`, an add-on in [`tonehost`](tonehost/README.md), plays the guitar through an Audio Unit, such as Helix
    Native, and follows the game's tones. The game starts the host (again when it has ended: it ends by itself
    while ToneHost is off), and the game's guitar is silent while it runs; the guitar fader and the volume keys still
    work. Without the host the game keeps its tones. It is built from source and
    isn't in the app: set it up from that folder.
  * `ToneHost`

* **Song Stems** (not in RSMods)
  * Takes the guitar or the bass out of the song you play, at a level of your own: 0% takes the part out, 100% is the
    song as it is. A bass tone takes out the bass, any other tone the guitar. The level is a line of the volume mixer
    (Guitar in Song or Bass in Song) with its own key, and stays for the songs after. Riff Repeater, rewinds and the
    game's volume sliders work as before.
  * `rs-stems`, an add-on in [`stems`](stems/README.md), separates each song with Demucs on the Neural Engine, a few
    seconds before it starts, and keeps the songs you play most in a cache of a fixed size. The game starts it, and again when it
    has ended (it ends by itself while Song Stems is off). Until a
    song is ready, and without the add-on, the song plays as it is. The app carries it, with its model and Demucs'
    licence, and installs it with the mods on macOS 15 or later (its model is converted for it); the mod itself runs
    where the game does.
  * `SongStems`, `[Mod Settings] SongStemsGuitarLevel` (0), `SongStemsBassLevel` (0), `SongStemsCacheMB` (500),
    `[Audio Keybindings] SongStemVolumeKey` (4)
  * Needs RS Re-ARMed 1.12.0 (the hook point `ak_src_submit` and the codebook names).

* **Seven-String Guitar** (not in RSMods)
  * For 7-string charts, such as DotCharter's: the guitar gets a seventh string, a low B, and the chart plays on it.
    Its lane and string have their own color; its notes and chords, with their techniques and chord frames, are drawn,
    detected and scored; the fret grid and bars span seven strings; the tuner starts on the low B; the chord book's
    diagrams get a seventh row; the tuning has its name (B STANDARD 7STR) in the song list, on the song's page and in
    the tuner; and the guitar has a seven-peg headstock. Six-string songs and bass play as the game has them.
  * `SevenString`, `[Seven String]` `string6_N` and `string6_CB` (the low B's color, normal and colorblind; empty: light
    grey), `ExtendedRangeColor` (on: with Extended Range Support on, its low string color), `ChordWideMisses`
    (2; 0 to 2: how many notes a chord of 7 or 8 may miss and still count), `TuningNames` (on), `TuningNamePrefix`
    (empty), `TuningNameSuffix` (7Str), `Headstock7` (on), `Headstock7Wood` (0.6; 0.05 to 1, its wood's brightness).
    The app's Seven-String page sets them. The color changes while the game runs; the switch and the rest from the next
    start.
  * A port of [rs7string](https://github.com/SomeeName/rs7string) by SomeeName, which patches the running Windows game.
    Here the same changes are built into RS Re-ARMed as edits and mid points, and the library switches them on when the
    game starts: in Learn & Play from RS Re-ARMed 1.12.0, on the Mac from 1.13.0. The chord diagrams and the headstock are built at
    each start from your own game files by rs7string's recipes and programs (`data/rs7string`, checked against their
    hashes); the three rebuilt menu files go in the game folder's `Update` folder (`rearmed-mods-<name>.gfx`).
  * Checked in Learn & Play with DotCharter's Ave Mujica chart: seven strings, chord frames over the low B, and
    B STANDARD 7STR in the song list, on the song's page and in the tuner; a six-string song plays as before. Played with
    a guitar (2026-11-09): it works, with the song's chord diagrams and the seven-peg headstock shown.
  * On the Mac: seen in scripted runs (2026-11-10) with the same chart as a Mac archive, its tuner and pause-menu pages,
    a six-string song and the chart again in one run, and a bass arrangement, with no fault: seven strings, the low B's
    lane and notes, chord frames over the low B, the seven-peg headstock, B STANDARD 7STR in the tuner. Played with a
    guitar the same day: it works; the tuner's reading of the low B and a seven-row chord diagram were not checked
    one by one. A chart published for Windows (`_p.psarc`) has to be converted to a Mac archive (`_m.psarc`:
    the song files' and audio's folders, and the song files encrypted with the Mac's key) before the Mac game lists it;
    RS Re-ARMed and these mods have no converter for that.
  * Different: all or nothing, as in rs7string: a part that can't start leaves the guitar with six strings, and the log
    says why. rs7string's switches for its other code paths aren't here; its defaults are the behaviour. On the Mac the
    game's list of a chord's expected notes keeps six entries, where Learn & Play's holds eight: a chord of seven or eight
    notes is judged by the mod's own chord detection in both.

* **Plain Packages** (not in RSMods)
  * Unencrypted song archives, as DotCharter writes its 7-string charts, load as the encrypted ones do: an archive
    whose table of contents isn't encrypted, and song files that are compressed but not encrypted. Other archives and
    song files load as before.
  * `PlainPackages`, from the next start
  * After rs7string's plain archive loading: in Learn & Play from RS Re-ARMed 1.12.0, on the Mac from 1.13.0. A plain song file has no signature: RS Re-ARMed skips the check (unless `RS_MODS_BYPASS=0`). Checked in Learn &
    Play with an unencrypted copy of the Ave Mujica chart, which plays as the encrypted one does. On the Mac it is on in
    the runs above, which loaded an encrypted Mac copy of the chart; an unencrypted Mac archive hasn't been run yet. The
    Mac game reads Mac archives only, so a Windows one has to be converted first (see Seven-String Guitar).
  * Off, an unencrypted archive may stop the game once a profile is chosen (as in rs7string's tests).

The hiding mods (loft, headstock, skyline, lyrics, frets, inlays, lane markers) find what they hide in the game's
memory after each scene loads. It takes a second or two after a song starts.

## Not on macOS yet

From RSMods, not ported yet. It needs a hook point of RS Re-ARMed that it doesn't have yet:

- The extra song lists in Non-Stop Play: RSMods lets it offer every song list a profile has, not only the first six.
  Its menu has eight places and its song pool eight cases, fixed inside two larger functions, which no hook point
  reaches. (The lists themselves are added by [More Song Lists](#more-song-lists); the rest of the game shows them.)

Not planned: what only exists on Windows or is about particular hardware (Twitch and Crowd Control integrations, the
Oculus crash fix, ASIO, the in-game settings menu, controllers, ultrawide screens (RS Re-ARMed draws any screen shape),
Fast Song Scan and Fast Profile Load/Save (fixes for the Windows game's loading), Auto Tune For Song (MIDI pedals)).

Alternative Sample Rates isn't needed: it lets the Windows game play on a device without 48 kHz. On the Mac the game
asks the output device for 48 kHz, and when it can't, Core Audio converts. Its guitar input still needs an input
that runs at 48 kHz, as on Windows. In Learn & Play, RS Re-ARMed's audio layer converts both ways.

## Built into RS Re-ARMed

These RSMods features need no mod, as RS Re-ARMed has them itself, on both editions and with no change to your game files:

- **Fast Load**: the game starts with no intro screens.
- **Enumerate**: ENUMERATE in the main menu, in place of SHOP, adds new songs with no restart.
- **Exit Game**: EXIT GAME in the main menu, in place of UPLAY. From RS Re-ARMed 1.9.0, with **Refresh
  Game Files** after updating.
- **Direct Connect Mode**: DIRECT CONNECT in the INPUT setting and on the cable screen, for any audio interface.

## How it works

RSMods on Windows patches the 32-bit Windows executable at fixed addresses and changes Direct3D 9 draws. RS
Re-ARMed has no original machine code at run time: the game is translated to C and compiled for arm64. So rearmed-mods
works from what RS Re-ARMed gives mods, and has no address of a game, which is why one file runs on both editions:

- **Hook points**, by name: the library runs code before and after some game functions (each frame, key events, Wwise
  audio events, the game's focus, the camera frustum, the Riff Repeater speed conversion, the two-cable check, the tone
  change, a song list's name, the opening of a game file).
- **The game's own functions and data**, by name: Wwise for speed, volume, seeking and the song position, the UI manager
  for the current menu, the DLC coordinator for enumeration, the color manager for string colors, the game's allocator
  and string destructor for the song list titles.
- **Hiding parts of the picture**: the game's Gamebryo scene objects and render steps are found by name in memory (from
  their run-time type information) and culled, collapsed or switched off.
- **The overlay API** for text on screen and reading the frame, and **the render API** for note and highway colors.
- **The game's files**, where RSMods reads the game's memory: a song's tunings from its archive (Extended Range), the
  profile list from `LocalProfiles.json` (`ProfileToLoad`), both decrypted with the game's own keys, found by name;
  and, where RSMods' app edits a profile, the profiles' song lists (More Song Lists), written back as the game writes
  them.
- **A value in the game's image**, found by what it is: the Non-Stop Play countdown's 10.99 seconds.
- **The game's archives, given again**: for Custom Tunings and the default tones the library writes its own
  `cache7.7z` and `cache4.7z` (in `rearmed-mods/` in the game folder, in Learn & Play in `Update/`) from the game's, with
  the files they change, and gives them to the game when it opens those archives. `cache.psarc` isn't changed.
- macOS APIs where Windows ones were used: Core Audio (input volume), ImageIO (screenshots), Core Image (Sunlight), AppKit
  (the window on another screen).

More in [docs/how-it-works.md](docs/how-it-works.md).

## Troubleshooting

Logging: before starting the game, `launchctl setenv RS_MODS_LOG 1` (and `launchctl unsetenv RS_MODS_LOG` to stop).
The mods then write what they do to `~/Library/Application Support/RS Re-ARMed/logs/last-run.log`: menus, song
keys, key presses, what each mod changes.

`RS_LOAD_MODS=0` (with `launchctl setenv`) starts RS Re-ARMed without any mods.

For finding what a mod should hook or hide. None of these are in the app; all are off unless set by hand:

| Setting | What it does |
|---|---|
| `[Keybinds] SceneDumpKey` | Writes every scene object and render step, with its class and name, to `scene-dump.txt` in the game folder. |
| `[Toggle Switches] HideNodes` | Scene objects to hide, comma separated (`name*` matches a prefix). |
| `[Toggle Switches] HideSteps` | Render steps to switch off, comma separated. |
| `[Keybinds] HideNodesKey` | Turns `HideNodes` and `HideSteps` off and on. |
| `[Mod Settings] LoftExtraFar` | Camera far planes the loft cuts besides 10000, comma separated. |

## Building and developing

```
build.sh                 builds build/rearmed-mods.dylib (--install: into RS Re-ARMed's mods folders)
VERSION                  the library's and the app's version
src/rsmods.c, .h         the core: hooks, game state, Wwise calls, the mod list
src/rsmods_ini.c         RSMods.ini (defaults, key names)
src/rsmods_overlay.c     text over the game
src/rsmods_scene.c       the scene graph and render steps: finding and hiding by name, the dump
src/rsmods_tuning.c, .h  PSARC archives, the manifests' tunings, the Extended Range rule (no game: tested as is)
src/rsmods_game_note.c   the note the game hears, from its pitch detection
src/rsmods_note_stats.c  the player's note counts and the grey-note time
src/rsmods_profiles.c    the save folder and LocalProfiles.json
src/rsmods_7z.c, .h      the 7z archives of cache.psarc and their LZMA: read a file, write one again with files changed
src/rsmods_cache.c, .h   Custom Tunings' and the default tones' edits of the game's files, a profile's tones
src/mod_cache.c          the archives the game is given for those two
src/mod_*.c              one file per mod
src/seven*.c, seven.h    Seven-String Guitar's parts (mod_seven_string.c turns them on)
src/stems_mix.h          Song Stems' mix of a block, and which .wem a song's bank plays (no game: tested as is)
data/rs7string/          rs7string's chord diagram recipes and headstock programs (MIT), compiled into the library
tests/, test.sh          the library's tests, and the script that runs them and the app's
include/rs-rearmed/      RS Re-ARMed's runtime headers (MIT)
gui/                     Re-ARMed Mods.app: settings and installer (SwiftUI); build-app.sh --zip makes the release
tonehost/                rs-tonehost: the add-on that hosts your own plug-in, and its Helix Native setup
stems/                   rs-stems: the add-on that takes a part out of a song (Song Stems), and how its model is made
docs/how-it-works.md     how the library works with RS Re-ARMed
```

A mod is an `rsm_mod` (`rsmods.h`): a tick each frame, key events, song changes, Wwise event filtering. Key events don't
reach the mods while the player types: in a text entry dialog, or in the song search, until Esc or another screen
(`typing` in `rsmods.c`). Add a mod to `MODS` in `rsmods.c`, its defaults to `DEFAULT_INI` and `DEFAULTS` in
`rsmods_ini.c`, and to the settings app's `gui/Sources/RSModsMac/Catalog.swift`. A mod that needs a new hook point needs it
in RS Re-ARMed's `tools/targets/names/hooks.list` and a new RS Re-ARMed release. So does a new function, global or field:
a name in `tools/targets/names`, and an entry in each game's table.

The library has no address or offset of a game, so the same file runs on both (`rc_mod.h`): `rsm_hook("name", ...)` for a
hook point, `OV_GUEST(name, ...)` for a call, `RC_OFF(name)` for a field, `rc_mod_ptr` for a pointer in the game's memory,
whose size differs, and memory from `rc_guest_alloc` for what a game function reads or fills. What a game lacks is logged
and its mod stays off.

### Tests

```bash
./test.sh
```

runs the library's tests (`tests/`: `RSMods.ini`, a value written into one whose last line has no line end, key names,
the color maths, Sunlight's colors, archives and tunings made
in the test with a key of its own, the profile list and the backups, the song list titles put in the game's strings, with
either game's pointers, LZMA and a 7z archive made by liblzma (`tests/make_7z_vectors.py` makes them again) and written
again, archives laid out in the test for a folder without a file and counts the reader refuses, the tuning,
localization and tone edits, a tone key the file has many times) and the app's
(`gui/Tests`: settings, a section that is a list, install and update for both editions, the app's profile backups). None needs the game. They also check the app against `src/rsmods_ini.c`: the same defaults,
the same key names, and no two actions on one default key. A mod's work in the game is checked by playing it.
`stems/test.sh` has `rs-stems`' own tests, which need game data and the model ([`stems/README.md`](stems/README.md)).

### Files from elsewhere

A custom song is somebody else's file, and the library and its helpers read several kinds of them. What reads one checks
every size and offset against the data it has before it uses it, and refuses the file, with a line in the log, when one
doesn't fit; the game then goes on as without the mod:

- `src/rsmods_tuning.c`: a song's archive (PSARC), for Extended Range's tunings and Song Stems' audio.
- `src/seven_chords.c`: the RS7X block of a 7-string chart.
- `stems/wwise_vorbis.c`: a song's `.wem`, in `rs-stems`, a process of its own. A header that names an offset past the
  data, a sample rate outside 8 to 192 kHz, or more frames than its packets can hold (or than 2^28) is refused before
  any memory is taken for it.
- `src/rsmods_7z.c`, `src/rsmods_cache.c`, `src/rsmods_profiles.c`: the game's own `cache.psarc` and your profiles.
- A song's key goes into file names (the score screenshots) only without `/` and `:`.

`rs-stems` is started with none of the game's open files and without a test's key injector in its environment
(`helper_env` in `src/mod_song_stems.c`). A new reader of such a file is to keep to the same rule; a damaged file is
the test for it (`tests/test_7z.c` has archives the reader refuses).

## Credits

- [RSMods](https://github.com/Lovrom8/RSMods) by LovroM8 and Ffio1, with ZagatoZee, Kokolihapihvi, L0fka and
  everyone who contributed: the features, their design, the settings and the default keys.
- [RS Re-ARMed](https://github.com/bbelgg/rs-rearmed): the Mac build these mods run in, and its runtime headers in
  `include/rs-rearmed` (MIT).
- [rs7string](https://github.com/SomeeName/rs7string) by SomeeName (MIT): Seven-String Guitar and Plain Packages, their
  design, and the chord diagram recipes and headstock programs in `data/rs7string`.
- [Demucs](https://github.com/facebookresearch/demucs) by Alexandre Défossez, Meta AI (MIT): Song Stems' separation, its
  model `htdemucs_6s` and the network `stems/convert` rewrites for the Neural Engine.

## License

[MIT](LICENSE), for this project's own code. `include/rs-rearmed` is RS Re-ARMed's, and `data/rs7string` rs7string's,
each under its own MIT license. RSMods has no
license file, and this license gives no rights to RSMods itself.
