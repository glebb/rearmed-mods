// rearmed-mods: RSMods (github.com/Lovrom8/RSMods) features for RS Re-ARMed, as a library the game loads at
// startup (rearmed-mods.dylib in RS Re-ARMed's mods folder; RS Re-ARMed 1.5.1 and later). One binary serves both
// editions, the Mac game (mac-x64) and Learn & Play (win-x86): it is built with rc_mod.h, and everything of a game,
// a hook point, a function, a global or a field, it reaches by the name in RS Re-ARMed's tools/targets. Comments may
// cite addresses as evidence; the code has none.
//
// The Windows mods patch the 32-bit Windows executable. Here the game is translated code, so the mods hook
// RS Re-ARMed's hook points (include/rs-rearmed/rc_hook.h):
//   game_frame          once per frame on the game thread, after the game's own frame (the mods' tick)
//   input_sink          (code, a, b): key events, before the game sees them (hotkeys)
//   ak_post_event_name  AK::SoundEngine::PostEvent(const char *, ...): song keys from the preview events
// and call game functions and read game memory through the app's runtime (OV_GUEST, RC_OFF, rc_mod_ptr).
// A guest pointer is 8 bytes on one game and 4 on the other: it is read by rc_mod_ptr, never as a C pointer, and
// what the mods keep (uint64_t obj, a const char *) is the host's address of it. What a game call is given has to
// be memory the game can see (rsmods.c: the Wwise calls).
// Settings come from RSMods.ini in the game folder, the same file, sections and keys as RSMods on Windows;
// it is re-read when it changes.
#ifndef RS_RSMODS_H
#define RS_RSMODS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rc_mod.h"

// ------------------------------------------------------------------------------------------------
// Settings (rsmods_ini.c)
// ------------------------------------------------------------------------------------------------
// "on" in [Toggle Switches]
int rsm_on(const char *key);
int rsm_on_section(const char *section, const char *key);  // in another section ([Highway Colors])
// A number in [Mod Settings]
long rsm_long(const char *key, long dflt);
// A key from [Keybinds]: the macOS virtual key code, or -1 if unset or unknown
int rsm_key(const char *key);
int rsm_key_in(const char *section, const char *key);  // from another section ([Audio Keybindings])
// Any value; NULL if absent. Valid until the next reload.
const char *rsm_get(const char *section, const char *key);
// The i-th line (from 0) of a section, in the file's order: 1 and its key and value, 0 past the last. Valid until the
// next reload.
int rsm_ini_entry(const char *section, int i, const char **key, const char **value);
// Writes a value into RSMods.ini (it is read back with the next poll)
void rsm_set(const char *section, const char *key, const char *value);
// Re-reads RSMods.ini if it changed (at most once a second); returns 1 if it did.
int rsm_ini_poll(void);
// Strips leading and trailing white space in place; returns the start
char *rsm_trim(char *s);

// ------------------------------------------------------------------------------------------------
// Game state (rsmods.c)
// ------------------------------------------------------------------------------------------------
// The current menu's name ("LearnASong_Game", ...), or "" before the UI exists
const char *rsm_menu(void);
int rsm_menu_in(const char *const *list);  // NULL-terminated list
// The key of the last song whose preview played ("" if none yet)
const char *rsm_song_key(void);
// The position in the playing song, in seconds (Wwise: the source position of "Play_<SongKey>"); -1 if
// that song has not been started
double rsm_song_time(void);
// Moves the playing song to ms (AK::SoundEngine::SeekOnEvent on "Play_<SongKey>")
void rsm_song_seek_ms(int32_t ms);
// A song position as RSMods shows it: "mm:ss", or "hh:mm:ss" from an hour, with unit letters ("03m:07s")
void rsm_format_time(char *out, size_t n, double seconds);

// Seconds on a monotonic clock
double rsm_now(void);

extern const char *const RSM_SONG_MODES[], *const RSM_FAST_RR_MODES[], *const RSM_SCORE_SCREENS[],
    *const RSM_LAS_PLAYING[], *const RSM_LAS_MODES[], *const RSM_LAS_PAUSE[];

// Registers hooks on the hook point `name`; 0, or -1 when this game or build hasn't it, which is logged: the mod then
// stays off. Call from a constructor.
int rsm_hook(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post);

// The Gamebryo scene graph and render steps (rsmods_scene.c): an object's name, and hiding by name. Hide (on) or
// show the objects with these names (NULL-terminated; "name*" a prefix) for owner, how: the culled bit (objects in
// the scene tree), scale 0 (objects drawn directly), scale 0 for objects with a parent only (ones the game culls and
// shows itself, whose parentless templates it may copy), or a render step off. Call every tick.
enum { RSM_CULL, RSM_COLLAPSE, RSM_STEP_OFF, RSM_COLLAPSE_PLACED };
int rsm_scene_name(uint64_t obj, char *out, size_t n);
void rsm_scene_hide(const char *owner, const char *const *names, int how, int on);
void rsm_scene_tick(void);  // the core, after the mods' ticks
// Calls fn for every NiAVObject and NiRenderStep in memory, with its class name (on the calling thread, not the
// game's; slow: a second or more)
enum { RSM_KIND_AV = 1, RSM_KIND_STEP = 2 };
void rsm_scene_each(void (*fn)(uint64_t obj, int kind, const char *cls, void *ctx), void *ctx);

// Presses and releases a key, as the shell delivers it (macOS key code, the character it types)
void rsm_send_key(int keycode, uint16_t ch);

// Modifier keys held now
int rsm_ctrl_down(void);
int rsm_shift_down(void);

void rsm_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// ------------------------------------------------------------------------------------------------
// Calls into translated code (from the game thread only: tick and key handlers)
// ------------------------------------------------------------------------------------------------
static inline uint64_t rsm_f32bits(float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  return u;
}

// Wwise (statically linked into the game; ak_* in the tables). A game object is 8 bytes on the Mac, 4 in Learn & Play:
// the invalid one cuts down to the same -1.
enum { AK_SUCCESS = 1, AK_FAIL = 2, AK_PARTIAL_SUCCESS = 3, AK_CURVE_LINEAR = 4 };
#define AK_INVALID_GAME_OBJECT 0xffffffffffffffffull
#define RS_SONG_GAME_OBJECT 0x1234ull  // the game object the game plays songs on
int ak_ready(void);  // Wwise is initialized (and not yet terminated); the ak_ calls do nothing otherwise
int ak_set_rtpc(const char *name, float value, uint64_t obj, int ms, int curve);
int ak_get_rtpc(const char *name, uint64_t obj, float *out);  // type: game object
int ak_set_actor_mixer_effect(uint32_t node, uint32_t slot, uint32_t fx);
int ak_seek_on_event(const char *event, uint64_t obj, int32_t ms, int seek_to_nearest_marker);
int ak_get_source_play_position(uint32_t playing_id, int32_t *ms, int extrapolate);
uint32_t ak_query_first_object(const char *event);  // 0 if none

// ------------------------------------------------------------------------------------------------
// Text over the game (rsmods_overlay.c). Queue it every frame it should show, from a tick.
// ------------------------------------------------------------------------------------------------
enum { RSM_ALIGN_LEFT, RSM_ALIGN_CENTER, RSM_ALIGN_RIGHT };
// x, y: the anchor (the text's top), as fractions of the window from the top left; size: font size as a
// fraction of the window height; rgba: 0xRRGGBBAA
void rsm_text(const char *text, float x, float y, float size, int align, uint32_t rgba);
void rsm_overlay_begin(void);  // before the mods' tick (rsmods.c)
void rsm_overlay_draw(void);   // after it
// Saves this frame, as shown (with the overlay), as a PNG at path (written on another thread)
void rsm_screenshot(const char *path);

// ------------------------------------------------------------------------------------------------
// Mods
// ------------------------------------------------------------------------------------------------
typedef struct {
  int keycode;  // macOS virtual key code
  int down;     // 1 key down (also auto-repeat), 0 key up
  int ctrl, shift;
} rsm_key_event;

typedef struct {
  const char *name;
  void (*tick)(void);                          // every frame
  void (*key)(const rsm_key_event *e);         // every key event
  void (*song_changed)(const char *song_key);  // a different song's preview played
  // Before Wwise posts an event by name; 1 = drop it (PostEvent returns no playing ID). Can run on any thread:
  // read only state the mod keeps for it, not the settings.
  int (*post_event)(const char *name);
} rsm_mod;

extern const rsm_mod rsm_mod_riff_repeater, rsm_mod_looping, rsm_mod_song_previews, rsm_mod_song_timer,
    rsm_mod_volume, rsm_mod_autoload, rsm_mod_enumeration,
    rsm_mod_input_volume, rsm_mod_screenshot_scores, rsm_mod_two_rtc, rsm_mod_background_audio, rsm_mod_linear_rr,
    rsm_mod_loft, rsm_mod_scene_dump, rsm_mod_hide_nodes, rsm_mod_headstock, rsm_mod_lyrics, rsm_mod_skyline,
    rsm_mod_fretboard, rsm_mod_camera, rsm_mod_colors, rsm_mod_tone_host, rsm_mod_sunlight, rsm_mod_nsp_timer,
    rsm_mod_secondary_monitor, rsm_mod_profile_backups, rsm_mod_extended_range, rsm_mod_game_note,
    rsm_mod_current_note, rsm_mod_guitar_speak, rsm_mod_buggy_tones, rsm_mod_accuracy, rsm_mod_song_list_titles;

// The guitar fader while ToneHost holds the game's own at 0: 1 and the value; otherwise 0 (mod_tone_host.c)
int rsm_guitar_fader(float *value);

// In a song with Sunlight on: the venue goes, as with the loft off (mod_sunlight.c)
int rsm_sunlight_hides_venue(void);

// The extended range colors are on now (mod_extended_range.c; mod_colors.c applies them)
int rsm_extended_range(void);

// The note the game hears the player play, while Show Current Note or GuitarSpeak is on (rsmods_game_note.c): MIDI, or
// -1; *plucks (may be NULL) a count that goes up with each new note, as RSMods' GuitarSpeak counts them
int rsm_guitar_note(unsigned *plucks);
// The player's note counts and grey-note time, in a song's own menus (playing or paused) (rsmods_note_stats.c): 1 and
// the values, or 0 where they aren't read. Notes before the grey-note time (seconds) are grey and not detected.
int rsm_note_counts(int *hits, int *misses);
int rsm_grey_time(float *seconds);
int rsm_set_grey_time(float seconds);
// "E2", with RSMods' names (C C# D Eb E F F# G Ab A Bb B; MIDI 0 is C-1)
void rsm_note_name(int midi, char *out, size_t n);

// The player's profiles (rsmods_profiles.c). The save folder in use (Steam Cloud's <account>/221680/remote) and its
// account; 0 if none is found.
int rsm_save_folder(char *out, size_t n, unsigned long long *account);
// LocalProfiles.json of a save folder, decrypted with the game's key: malloc'ed JSON, or NULL
char *rsm_profiles_json(const char *folder);
char *rsm_profiles_decode(const uint8_t *data, size_t n, const uint8_t key[32]);
// The profiles' names (PlayerName), in the file's order: last used first, as the game lists them; how many
int rsm_profiles_names(const char *json, char names[][64], int max);

#endif
