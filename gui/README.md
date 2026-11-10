# Re-ARMed Mods.app

The installer and settings app of rearmed-mods. It carries `rearmed-mods.dylib` (built from `../src`) and installs
it into RS Re-ARMed's mods folders, for both editions (`Sources/RSModsMac/ModsLibrary.swift`). The mods have the
Windows RSMods GUI's labels, on the sidebar's pages by what they change: each mod is a row with its switch, and while it
is on its settings and keys are under it (click a key field, press a key). **Keybindings** lists every key, and the
search field finds a mod on any page.

It edits `RSMods.ini` in the game folder (`~/Library/Application Support/RS Re-ARMed/game/`, `learn-play/game/` with
the Learn & Play switch, or another folder chosen with **Change…**). Every change is saved at once and the mods pick it up while the game runs.
The file's comments and keys the app doesn't know are kept; editing the file elsewhere reloads it in the app.

## Build

```bash
./build-app.sh --open
```

builds `build/Re-ARMed Mods.app` (arm64, ad-hoc signed, with the library inside); `--zip` also makes
`build/Re-ARMed-Mods-<version>.zip` for a release. `swift run` runs it without a bundle. To try it on
another folder without changing the saved one: `open "build/Re-ARMed Mods.app" --args -GameFolder /path/to/folder`.

The icon is `icon/AppIcon.icns`, drawn by `icon/make_icon.py` (with `AppIcon.svg`, its source as a picture): RSMods'
faceted pick, on macOS's rounded square and the traces of an Apple Silicon chip, with the gear of a settings app. Run it
again after changing the drawing; it needs `rsvg-convert` (`brew install librsvg`), the build doesn't.

## Adding a mod

`Sources/RSModsMac/Catalog.swift` lists what the app shows. When a mod is ported to the Mac library, move
it from `notYetOnMac` to `mods`, add its keys to `defaults` (matching `DEFAULTS` in `rsmods_ini.c`), its
key to one of `categories` (its page), its keys to `modKeybinds` / `audioKeybinds`, and any settings to `ModSettings.swift`
(a case in `ModSettings` and its key in `ModSettings.mods`).
A mod whose checkbox isn't plain on and off (`CustomStringColors`, `SeparateNoteColors`) needs its case in `isOn` and
`toggle` in `SettingsStore.swift`.
