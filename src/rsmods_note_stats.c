// The player's note counts and grey-note time, where RSMods reads them on Windows (D3DOverlay.cpp ReadAccuracy,
// SongTimer.cpp GetGreyNoteTimer / SetGreyNoteTimer): for Show Current Accuracy, and for looping and rewinding,
// which reset the grey-note time.
//
// A song's menu, playing or paused, holds the song's game object (song_menu_game), which holds Player 1's
// (song_game_player), which holds the note counts (player_note_stats): hits and misses (note_stats_hits,
// note_stats_misses), the same counts in Learn a Song and in Score Attack. The counts hold the player's track
// (note_stats_track), which keeps the song position it is at (track_song_time) and the grey-note time
// (track_grey_time): notes before it are grey and not detected. The game raises the grey-note time to the song
// position when the song pauses, so after a pause the song resumes a little earlier with the notes before the pause
// grey. These names are in RS Re-ARMed from 1.11.0, on the Mac; Learn & Play's are not matched yet, so there both
// stay off, with a line in the log.
//
// The path starts at the current menu, so it is followed only in these menus, and each pointer is read with a
// check that the memory is there; what it ends at must look like counts and a track.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <math.h>
#include <stdio.h>

#include "rsmods.h"

static const char *const MENUS[] = {"LearnASong_Game", "NonStopPlay_Game", "ScoreAttack_Game", "LearnASong_Pause",
                                    "NonStopPlay_Pause", "ScoreAttack_Pause", NULL};

#define MAX_COUNT 100000  // more notes than any song has
#define MAX_TIME 36000.f  // seconds

static int known(void) {
  static int k = -1;
  if (k < 0) {
    k = RC_ADDR(ui_manager) && RC_HAS_OFF(ui_current_menu) && RC_HAS_OFF(song_menu_game) &&
        RC_HAS_OFF(song_game_player) && RC_HAS_OFF(player_note_stats) && RC_HAS_OFF(note_stats_hits) &&
        RC_HAS_OFF(note_stats_misses) && RC_HAS_OFF(note_stats_track) && RC_HAS_OFF(track_song_time) &&
        RC_HAS_OFF(track_grey_time);
    if (!k)
      fprintf(stderr, "[rsmods] the note counts aren't known on %s: Show Current Accuracy and the grey-note reset need "
                      "RS Re-ARMed 1.11.0 on the Mac\n", rc_target());
  }
  return k;
}

// The guest pointer at host address p as a host pointer, or NULL if p can't be read
static const char *safe_ptr(const char *p) {
  if (!p) return NULL;
  uint64_t v = 0;
  mach_vm_size_t got = 0;
  size_t n = (size_t)rc_guest_ptr_size;
  if (mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)p, n, (mach_vm_address_t)&v, &got) ||
      got != n)
    return NULL;
  if (!v) return NULL;
  return n == 8 ? (const char *)(uintptr_t)v : RC_HOST((uint32_t)v);
}

static int readable(const char *p, size_t n) {
  uint8_t b[16];
  mach_vm_size_t got = 0;
  return p && n <= sizeof b &&
         !mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)p, n, (mach_vm_address_t)b, &got) &&
         got == n;
}

static int plausible_time(float t) { return isfinite(t) && t >= -1.f && t <= MAX_TIME; }

// The counts and the track, or 0
static int find(const char **stats, const char **track) {
  if (!rsm_menu_in(MENUS) || !known()) return 0;
  const char *mgr = safe_ptr((const char *)(uintptr_t)RC_A(RC_ADDR(ui_manager)));
  const char *menu = mgr ? safe_ptr(mgr + RC_OFF(ui_current_menu)) : NULL;
  const char *game = menu ? safe_ptr(menu + RC_OFF(song_menu_game)) : NULL;
  const char *player = game ? safe_ptr(game + RC_OFF(song_game_player)) : NULL;
  const char *s = player ? safe_ptr(player + RC_OFF(player_note_stats)) : NULL;
  const char *t = s ? safe_ptr(s + RC_OFF(note_stats_track)) : NULL;
  if (!t || !readable(s + RC_OFF(note_stats_hits), 4) || !readable(s + RC_OFF(note_stats_misses), 4) ||
      !readable(t + RC_OFF(track_song_time), 4) || !readable(t + RC_OFF(track_grey_time), 4))
    return 0;
  int32_t hits = RC_LD(int32_t, s + RC_OFF(note_stats_hits)), misses = RC_LD(int32_t, s + RC_OFF(note_stats_misses));
  float now = RC_LD(float, t + RC_OFF(track_song_time)), grey = RC_LD(float, t + RC_OFF(track_grey_time));
  if (hits < 0 || hits > MAX_COUNT || misses < 0 || misses > MAX_COUNT || !plausible_time(now) ||
      !plausible_time(grey))
    return 0;
  if (stats) *stats = s;
  if (track) *track = t;
  return 1;
}

int rsm_note_counts(int *hits, int *misses) {
  const char *s;
  if (!find(&s, NULL)) return 0;
  *hits = RC_LD(int32_t, s + RC_OFF(note_stats_hits));
  *misses = RC_LD(int32_t, s + RC_OFF(note_stats_misses));
  return 1;
}

int rsm_grey_time(float *seconds) {
  const char *t;
  if (!find(NULL, &t)) return 0;
  *seconds = RC_LD(float, t + RC_OFF(track_grey_time));
  return 1;
}

int rsm_set_grey_time(float seconds) {
  const char *t;
  if (!find(NULL, &t)) return 0;
  RC_ST(float, t + RC_OFF(track_grey_time), seconds);
  return 1;
}
