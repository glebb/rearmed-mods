// RSMods: looping and rewinding in Learn a Song and Non-Stop Play (RiffRepeaterMod.cpp, D3DOverlay.cpp
// HandleLooping).
//
// Looping (AllowLooping): the loop start key sets the start at the song position, the loop end key the end
// (Ctrl + loop start clears both, Ctrl + loop end clears the end). While the song plays past the end, it is
// moved back to the start, LoopingLeadUp ms early. The loop shows at the top of the screen. Leaving the song,
// or opening Riff Repeater, clears it.
// Rewinding (AllowRewind): the rewind key moves the song back RewindBy + RewindLeadup ms.
//
// Both reset the game's grey-note time, as RSMods does: notes before it are grey and not detected, and the game sets
// it to where the song pauses. While paused with a loop it is the loop's start, so the notes of the loop count again
// when the song resumes before them; a rewind sets it RewindLeadup ms before where the song goes back to, or to the
// song's start (rsmods_note_stats.c).
#include <stdio.h>

#include "rsmods.h"

static double g_start = -1, g_end = -1;  // seconds; -1 = unset
static int g_cooldown;                   // frames to wait after a seek, while the position catches up

static void clear_loop(const char *why) {
  if (g_start < 0 && g_end < 0) return;
  g_start = g_end = -1;
  rsm_log("looping: loop cleared (%s)", why);
}

static void tick(void) {
  if (!rsm_menu_in(RSM_SONG_MODES) && !rsm_menu_in(RSM_SCORE_SCREENS)) {
    clear_loop("left the song");
    return;
  }
  if (!rsm_on("AllowLooping") || (g_start < 0 && g_end < 0)) return;
  if (!rsm_menu_in(RSM_LAS_MODES)) {
    if (rsm_menu_in(RSM_FAST_RR_MODES)) clear_loop("Riff Repeater");
    return;
  }
  char a[24], b[24], t[64];
  rsm_format_time(a, sizeof a, g_start);
  rsm_format_time(b, sizeof b, g_end);
  snprintf(t, sizeof t, "Loop: %s - %s", a, b);
  rsm_text(t, 0.5f, 50.f / 1080, 0.028f, RSM_ALIGN_CENTER, 0xffffffff);

  if (rsm_menu_in(RSM_LAS_PAUSE)) {
    float want = g_start < 0 ? 0.f : (float)g_start, grey;
    if (rsm_grey_time(&grey) && grey != want && rsm_set_grey_time(want))
      rsm_log("looping: notes from %.2f s count again (the grey-note time was %.2f s)", want, grey);
  }
  if (g_cooldown > 0) {
    g_cooldown--;
    return;
  }
  if (g_start < 0 || g_end < 0 || rsm_menu_in(RSM_LAS_PAUSE)) return;
  double now = rsm_song_time();
  if (now < 0 || now < g_end) return;
  double lead = rsm_long("LoopingLeadUp", 0) / 1000.0;
  double to = lead >= g_start ? 0 : g_start - lead;
  rsm_song_seek_ms((int32_t)(to * 1000));
  g_cooldown = 15;
  rsm_log("looping: %.2f s -> %.2f s", now, to);
}

static void key(const rsm_key_event *e) {
  if (e->down && rsm_on("AllowLooping") && rsm_menu_in(RSM_FAST_RR_MODES)) {
    if (e->keycode == rsm_key("LoopStartKey")) {
      if (e->ctrl) {
        clear_loop("Ctrl + loop start key");
        return;
      }
      double now = rsm_song_time();
      if (now < 0) return;
      g_start = now;
      if (g_end >= 0 && g_end <= g_start) g_end = -1;
      rsm_log("looping: start %.2f s", g_start);
    } else if (e->keycode == rsm_key("LoopEndKey")) {
      if (e->ctrl) {
        g_end = -1;
        rsm_log("looping: end cleared");
        return;
      }
      double now = rsm_song_time();
      if (now < 0) return;
      g_end = now;
      if (g_end <= g_start) g_end = -1;
      rsm_log("looping: end %.2f s", g_end);
    }
  }
  // rewind on key up, as RSMods
  if (!e->down && e->keycode == rsm_key("RewindKey") && rsm_on("AllowRewind") && rsm_menu_in(RSM_LAS_PLAYING)) {
    double now = rsm_song_time();
    if (now < 0) return;
    long by = rsm_long("RewindBy", 5000), leadup = rsm_long("RewindLeadup", 2000);
    long to = (long)(now * 1000) - by - leadup;
    if (to < 0) to = 0;
    rsm_song_seek_ms((int32_t)to);
    g_cooldown = 15;
    float grey = (float)(to - leadup) / 1000.f;
    int reset = rsm_set_grey_time(grey);
    rsm_log("rewind: %.2f s -> %.2f s%s", now, to / 1000.0, reset ? ", grey-note time reset" : "");
  }
}

static void song_changed(const char *key) {
  (void)key;
  clear_loop("another song");
}

const rsm_mod rsm_mod_looping = {"looping", tick, key, song_changed, NULL};
