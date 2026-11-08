# Re-ARMed Mods.app

The installer and settings app of rearmed-mods. It carries `rearmed-mods.dylib` (built from `../src`) and installs
it into RS Re-ARMed's mods folders, for both editions (`Sources/RSModsMac/ModsLibrary.swift`). Laid out like the
Windows RSMods GUI: the **Enable / Disable Mods** tab with
the mods and their settings side by side, **Colors**, and **Keybindings** (click a key field, press a key).

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
keys to `modKeybinds` / `audioKeybinds`, and any settings to the sub-tabs in `Views.swift`.
A mod whose checkbox isn't plain on and off (`CustomStringColors`, `SeparateNoteColors`) needs its case in `isOn` and
`toggle` in `SettingsStore.swift`.
