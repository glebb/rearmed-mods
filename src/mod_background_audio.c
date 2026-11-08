// RSMods: keep playing, with sound, when the game is not the active app (AllowAudioInBackgroundMod.cpp,
// VolumeControl::AllowAltTabbingWithAudio). RSMods patches two places on Windows, what the game does with WM_ACTIVATE;
// here the point of the focus (set_app_focus) is told "focused", so the game doesn't hear of the loss. On the Mac:
//   - SetAppFocus(bool) (pauses the audio): always told "focused";
//   - the pause the shell sends on focus loss, UI event 0x98 through ui_event, right after SetAppFocus (from
//     applicationWillResignActive, while the app still counts as active): the next one after a focus loss is dropped;
//   - the focus byte app_focus the shell clears just before SetAppFocus: set again there (and every frame).
// In Learn & Play the point is where the window's focus reaches the game (the input layer) and WM_ACTIVATE carries
// what it is told: the game's pause and Wwise's sink going away (0x149ee1c) come from that message, so neither happens.
// There is no such byte in that game: app_focus has no address there, which the log says once.
//
// RSMods: prevent the pause mid song (PreventMidSongPause; dllmain.cpp's WndProc): in a song the game is told it keeps
// the focus, the same way, so another app coming to the front doesn't pause it. (RSMods' other half, the window kept on
// top, is switched off in RSMods itself: ensureForcedTopMode.)
#include <stdatomic.h>

#include "rsmods.h"

#define UI_EVENT_PAUSE 0x98

static atomic_int g_on;
static int g_lost;  // a focus loss was ignored: drop the shell's pause that follows it (same thread, same call)

static void hold_focus_byte(void) {
  uint64_t a = RC_ADDR(app_focus);
  if (a) RC_ST(uint8_t, RC_A(a), 1);
}

static void tick(void) {
  int on = rsm_on("AllowAudioInBackground") || (rsm_on("PreventMidSongPause") && rsm_menu_in(RSM_SONG_MODES));
  atomic_store(&g_on, on);
  if (on) hold_focus_byte();
  g_lost = 0;  // the pause comes in the same call as the focus loss, never a frame later
}

static int before_set_focus(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  if (atomic_load(&g_on) && !(OV_I32(a) & 0xff)) {
    rc_mod_set_arg(c, 0, 1);  // focused
    // the shell has just cleared the focus byte; the game reads it in the next frame, before our tick
    hold_focus_byte();
    g_lost = 1;
    rsm_log("background audio: focus loss ignored");
  }
  return 0;
}

static int before_ui_event(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  OV_PTR(a, void);  // the menu
  if (!g_lost || OV_I32(a) != UI_EVENT_PAUSE) return 0;
  g_lost = 0;
  rsm_log("background audio: pause on focus loss dropped");
  ov_ret_void(c);
  return 1;
}

__attribute__((constructor)) static void init(void) {
  rsm_hook("set_app_focus", before_set_focus, NULL);
  rsm_hook("ui_event", before_ui_event, NULL);
}

const rsm_mod rsm_mod_background_audio = {"background_audio", tick, NULL, NULL, NULL};
