// The player's note counts and grey-note time, where RSMods reads them on Windows (D3DOverlay.cpp ReadAccuracy,
// SongTimer.cpp GetGreyNoteTimer / SetGreyNoteTimer): for Show Current Accuracy, and for looping and rewinding,
// which reset the grey-note time.
//
// A song's menu, playing or paused, holds the song's game object (song_menu_game), which holds Player 1's
// (song_game_player), which holds the note counts (player_note_stats): hits and misses (note_stats_hits,
// note_stats_misses). The counts hold the player's track
// (note_stats_track), which keeps the song position it is at (track_song_time) and the grey-note time
// (track_grey_time): notes before it are grey and not detected. The game raises the grey-note time to the song
// position when the song pauses, so after a pause the song resumes a little earlier with the notes before the pause
// grey. These names are in RS Re-ARMed from 1.11.0. On the Mac one set of counts serves Learn a Song and Score Attack.
// In Learn & Play the menu's path reached the counts in two runs and not in a third, so there the counts are reached
// as RSMods reaches them, from the UI manager (ui_note_stats_0 to _3: D3DOverlay.cpp's ptr_noteDataOffsets), and
// the rest is the same: their track and its times. Score Attack counts in another object there (RSMods'
// ScoreAttackNoteData), which that path doesn't reach, so the mods find nothing in Score Attack in Learn & Play.
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

// The counts' path: 1 from the song's menu, 2 RSMods' from the UI manager, 0 none
static int g_path;

static int known(void) {
  static int k = -1;
  if (k < 0) {
    g_path = RC_HAS_OFF(ui_current_menu) && RC_HAS_OFF(song_menu_game) && RC_HAS_OFF(song_game_player) &&
                     RC_HAS_OFF(player_note_stats)
                 ? 1
             : RC_HAS_OFF(ui_note_stats_0) && RC_HAS_OFF(ui_note_stats_1) && RC_HAS_OFF(ui_note_stats_2) &&
                     RC_HAS_OFF(ui_note_stats_3)
                 ? 2
                 : 0;
    k = RC_ADDR(ui_manager) && g_path && RC_HAS_OFF(note_stats_hits) && RC_HAS_OFF(note_stats_misses) &&
        RC_HAS_OFF(note_stats_track) && RC_HAS_OFF(track_song_time) && RC_HAS_OFF(track_grey_time);
    if (!k)
      fprintf(stderr, "[rsmods] the note counts aren't known on %s: Show Current Accuracy and the grey-note reset need "
                      "RS Re-ARMed 1.11.0\n", rc_target());
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

// Where the path stopped, in the log at most every 5 seconds (RS_MODS_LOG)
static void not_found(const char *why, const void *at, double a, double b) {
  static double last = -10;
  double now = rsm_now();
  if (now - last < 5) return;
  last = now;
  rsm_log("note counts: none in %s: %s (%p, %g, %g)", rsm_menu(), why, at, a, b);
}

// The counts and the track, or 0
static int find(const char **stats, const char **track) {
  if (!rsm_menu_in(MENUS) || !known()) return 0;
  const char *p[5] = {safe_ptr((const char *)(uintptr_t)RC_A(RC_ADDR(ui_manager)))};
  int64_t offs[4];
  if (g_path == 1) {
    offs[0] = RC_OFF(ui_current_menu), offs[1] = RC_OFF(song_menu_game), offs[2] = RC_OFF(song_game_player);
    offs[3] = RC_OFF(player_note_stats);
  } else {
    offs[0] = RC_OFF(ui_note_stats_0), offs[1] = RC_OFF(ui_note_stats_1), offs[2] = RC_OFF(ui_note_stats_2);
    offs[3] = RC_OFF(ui_note_stats_3);
  }
  for (int i = 0; i < 4; i++) p[i + 1] = p[i] ? safe_ptr(p[i] + offs[i]) : NULL;
  const char *s = p[4];
  const char *t = s ? safe_ptr(s + RC_OFF(note_stats_track)) : NULL;
  if (!t) {
    int step = 0;
    while (step < 4 && p[step + 1]) step++;
    not_found(step < 4 ? "a step of the path is empty" : "no track", step < 4 ? (const void *)p[step] : s, step, 0);
    return 0;
  }
  if (!readable(s + RC_OFF(note_stats_hits), 4) || !readable(s + RC_OFF(note_stats_misses), 4) ||
      !readable(t + RC_OFF(track_song_time), 4) || !readable(t + RC_OFF(track_grey_time), 4)) {
    not_found("not readable", t, 0, 0);
    return 0;
  }
  int32_t hits = RC_LD(int32_t, s + RC_OFF(note_stats_hits)), misses = RC_LD(int32_t, s + RC_OFF(note_stats_misses));
  float now = RC_LD(float, t + RC_OFF(track_song_time)), grey = RC_LD(float, t + RC_OFF(track_grey_time));
  if (hits < 0 || hits > MAX_COUNT || misses < 0 || misses > MAX_COUNT) {
    not_found("not counts", s, hits, misses);
    return 0;
  }
  if (!plausible_time(now) || !plausible_time(grey)) {
    not_found("not times", t, now, grey);
    return 0;
  }
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

// Not before the song's start: a rewind there would otherwise ask for up to RewindLeadup before it, a time find() takes
// for no track (plausible_time), which would hide the counts until the game set the time again at the next pause
int rsm_set_grey_time(float seconds) {
  const char *t;
  if (!isfinite(seconds) || seconds > MAX_TIME) return 0;
  if (seconds < 0) seconds = 0;
  if (!find(NULL, &t)) return 0;
  RC_ST(float, t + RC_OFF(track_grey_time), seconds);
  return 1;
}
