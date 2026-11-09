# How rearmed-mods works

## RS Re-ARMed

RSMods on Windows is a DLL the 32-bit Windows game loads (as `xinput1_3.dll`). It patches the game's x86 code at
known addresses and hooks Direct3D 9. None of that carries over to macOS:

- The Mac game is a different program: a 64-bit x86 Mac build of Rocksmith 2014, with its own code and addresses,
  OpenGL instead of Direct3D, and Cocoa instead of Win32.
- RS Re-ARMed doesn't run that x86 code. It **statically recompiles** it, once, at build time: every function is
  translated to C and compiled for arm64. At run time there is no original machine code to patch, and the
  translated functions call each other directly.

Instead, RS Re-ARMed gives mods these ways in, the same in both its apps (the Mac game, and Learn & Play, the
32-bit Windows game translated the same way):

- **A mods folder.** At startup it loads every `.dylib` in `~/Library/Application Support/RS Re-ARMed/mods/`, or
  `RS Re-ARMed/learn-play/mods/` for Learn & Play (`RS_LOAD_MODS=0` skips them; `RS_MODS_PATH` loads another folder or
  library). A reinstall keeps the folder.
- **Hook points.** Some game functions are built so that a mod can run code before or after them, or replace
  them entirely, by name. The list is RS Re-ARMed's `tools/targets/names/hooks.list`; the API is
  `tools/ws7/recomp/runtime/rc_hook.h` (`rc_hook_add_name`). A hook sees the Mac game's call on either game.
- **The runtime, exported.** The game executable exports what a mod needs to work with the game. A mod built with
  `rc_mod.h` is one binary for both: it has no address of a game. It asks for a function, a global or a field by its
  name in `tools/targets` when it runs (`OV_GUEST`, `RC_ADDR`, `RC_OFF`), reads a game pointer with `rc_mod_ptr`, as it is 8
  bytes on the Mac and 4 in Learn & Play, and gives the game only memory it can see (`rc_guest_alloc`). Learn & Play
  loads no library built without it.
- **An overlay API** (from **1.3.0**). The game executable exports `shims/include/rs_overlay.h`: a callback the
  shell runs after the game's frame, before it is shown, which draws RGBA images over the frame and reads the frame
  back, on whichever renderer the game uses (Metal by default since 1.3.0, or OpenGL). A mod may not call the
  graphics API itself.

- **A render API** (from 1.9.0). libRSOpenGL exports `shims/include/rs_render.h`: a mod makes a rule for a game
  texture by its size and the hash of what the game uploads, and gives pixels to draw in its place, or hides the draws that use it. The game's texture is
  left alone. It is on Metal alone, and needs no hook point.

Consequences for the mods:

- A Mac mod needs **no change to the game** and no rebuild of it, unless it needs a new hook point. A new hook point
  means a new RS Re-ARMed release, which users install by rebuilding the game (the RS Re-ARMed installer does this).
  Hook points are cheap; adding them in batches keeps those releases rare.
- RS Re-ARMed supports one build of each game, so its tables are fixed. If a build ever changes, the tables change
  with it, and the library doesn't.
- A new field or function a mod needs is a line in each game's table in RS Re-ARMed, and so a new RS Re-ARMed release.
  A name a game hasn't is logged, and the mod that needs it stays off.
- RS Re-ARMed is a separate project (MIT licensed); the Mac mods depend on its version, just as the Windows DLL depends on
  the Windows game build.

## The library

The Mac mods are a C library (a `.dylib`) with a core and one source file per mod. It reimplements the RSMods
features for the Mac build, keeping RSMods' settings, names and behaviour:

- **Hooks.** In its constructor it registers its hooks: a tick after each game frame (on the game thread, where it
  can call game functions safely), key events before the game sees them (the hotkeys), and Wwise events (the song
  key from the preview event, the song's playing ID).
- **Game functions.** Wwise is statically linked into the game, so the library calls `SetRTPCValue`,
  `SetActorMixerEffect`, `SeekOnEvent`, `GetSourcePlayPosition` and others by name. That covers song speed, volume,
  seeking and the song position.
- **Game state.** The current menu's name is read from the UI manager (as RSMods does on Windows), and the song
  key from the preview event.
- **String colors.** As on Windows, in the game's own table: `GameColorManager` has pointers to each string's color, six for each kind of color, normal and colorblind. It is the
  same table in both games, with pointers of each game's size. The library writes both banks, every frame, and puts back what the game had when a mod is turned off.
- **Note and highway colors.** Where RSMods gives Direct3D its own texture in the draws of certain meshes, the Mac library replaces the small palette
  textures the game colors those meshes with, through the render API. The palettes were found with RS Re-ARMed's texture log (`RS_GL_TEX_LOG`), and by what changes on screen, as
  the checksums RSMods has for the highway don't match here. Each has a hash per edition. Checked in one song (Cherub Rock) in each edition; a texture other modes use isn't known.
- **Hiding parts of the picture.** Where RSMods skips Direct3D draws, the Mac library works with the game's
  Gamebryo scene: it finds objects and render steps by name in memory (from their run-time type information) and
  culls them, collapses them, or switches the render steps off. The loft also uses the camera hook (the loft camera's
  far plane, as on Windows).
- **Text on screen** (song speed, loop, song timer, volume) is rendered with Core Text and drawn after the game's
  frame through the overlay API, which also reads the frame for score screenshots.
- **macOS APIs** where RSMods uses Windows ones: Core Audio for the input volume, ImageIO for screenshots, AppKit for
  the window's screen.
- **The game's files where RSMods reads its memory.** Extended Range needs the arrangement's tuning, which RSMods reads
  from the game's tuner; the Mac build has no name for that, but each arrangement's manifest in the song's archive has
  it. The library reads the archives' encrypted tables of contents (PSARC, AES-CFB) with the game's own key, found by
  name (`key_psarc_hex`), lists every archive's manifests once on a thread, and reads the song's when its key changes.
  `ProfileToLoad` reads the profile list, `LocalProfiles.json` (AES-ECB and zlib, the game's `key_profile`), where
  RSMods reads the highlighted name from memory.
- **The note the game hears**, as RSMods reads it: the game's DetectionService and its pitch detection, named in RS
  Re-ARMed (`detection_service`, `detection_note`), for Show Current Note and Guitar Speak.
- **The note counts and the grey-note time**, where RSMods reads them: Player 1's counts of notes hit and missed, and the
  track whose grey-note time is how far notes are grey. RSMods follows pointers from the UI manager; on the Mac the
  steady way there is through the song's menu (`song_menu_game`, `player_note_stats`, `track_grey_time`), found by
  scanning the game's memory while a song played and paused, and checked in later runs. In Learn & Play, where such a
  path held in two runs and not in a third, the counts are reached as RSMods reaches them, from the UI manager
  (`ui_note_stats_0` to `_3`), and their track is the one with RSMods' grey-note time. Its Score Attack counts in
  another object, which that path doesn't reach. The library follows it only in a
  song's own menus, reading each pointer with a check that the memory is there. For Show Current Accuracy and the
  grey-note reset of looping and rewinding.
- **A hook point where RSMods changes code.** RSMods' Custom Song List Titles changes three places in the function
  that names a song list for the menus (it builds the localized "SONG LIST", a space and the list's number). RS
  Re-ARMed makes that function a hook point (`song_list_name`), and the library's hook after it puts the title in the
  name, an efd string: in its own buffer when the title fits, else in one from the game's allocator (`efd_alloc`), the
  old one freed by the game's string destructor (`efd_str_destroy`), as the game's strings are. Non-Stop Play's extra
  song lists aren't ported: their limits are constants in the middle of two larger functions.
- **A value found by what it is.** RSMods' Non-Stop Play timer writes a constant at a known address. Both games read it
  from their mapped image at run time, and each image has that value (10.99f as a double) once: the library scans the
  image for it and writes it.
- **The game's archives, given again.** RSMods' app changes three of the game's files for Custom Tunings and the
  default tones by rewriting `cache.psarc`: the tunings (`manifests/tuning.database.json`) and the tones
  (`manifests/tonemanager.database.json`) in `cache7.7z`, and the localization, which names the tunings
  (`localization/maingame.csv`), in `cache4.7z`. The game opens those archives by name through `FrontEnd_OpenFile`, a
  hook point (`frontend_open_file`). The first time, the library writes its own, from RS Re-ARMed's overlay
  (`Update/patcharchives.psarc`, with its menu entries) when that has the archive, else from `cache.psarc`, and the game
  opens those instead. On the Mac they are in `rearmed-mods/` in the game folder: the hook opens one there and gives the
  game that stream (RS Re-ARMed's hook asks for `PatchUpdate/<name>` through the hook point again, and the library's
  hook answers that call), and when it can't, the game opens its own. In Learn & Play a mod can't call the function (it
  takes its path in a register, which the runtime's calls by name refuse), so the hook changes the path the game asked
  for to the library's archive in `Update/`, `c:\rocksmith2014\update\rearmed-mods-<name>`: the form in which the game
  opens RS Re-ARMed's overlay there through the same file system (`C:\Rocksmith2014` is the game folder in RS
  Re-ARMed's drive layout). The archives have one file per folder, each packed with
  LZMA: the library keeps every other file's packed bytes, unpacks the files it changes (an LZMA decoder after the
  LZMA specification's) and packs the changed ones as LZMA literals, which any LZMA decoder reads, with the header
  packed as the game's are. A tuning's name is `$[<id>]<name>` with a localization line of its own: the tuner's list
  shows `$[-1]<name>` as the text, but a song's details look the id up and show `#-1#`.
- **RSMods.ini** is read with the same keys and defaults, and re-read when it changes.

Each mod's source names the game functions and data it uses; their addresses are in RS Re-ARMed's `tools/targets`.


## Approaches chosen

- **A rewrite for the Mac build, not a port of the DLL.** The DLL's code patches and Direct3D hooks have no
  equivalent in a recompiled game; what carries over is RSMods' design: the features, settings and keys. The
  library keeps those, so a Windows player finds the same mods.
- **Hook points built into RS Re-ARMed, not patching at run time.** Translated functions call each other directly,
  so a function can only be intercepted reliably where the build provides for it. Patching the compiled arm64 code
  at run time would miss inlined calls and break on every rebuild.
- **The same `RSMods.ini`.** One settings file, and settings that move between platforms.
- **The game's scene and render steps, not draw calls.** The Windows mods recognise Direct3D draws by their
  geometry and textures. On macOS, hiding Gamebryo objects and switching render steps off by name is simpler and
  survives scene changes.
- **A mods folder outside the game app.** RS Re-ARMed rebuilds and replaces the game app on every update; mods in
  their own folder survive that.

