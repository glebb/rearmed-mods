// The player's note counts and grey-note time, where RSMods reads them on Windows (D3DOverlay.cpp ReadAccuracy,
// SongTimer.cpp GetGreyNoteTimer / SetGreyNoteTimer): for Show Current Accuracy, and for looping and rewinding,
// which reset the grey-note time.
//
// The counts are hits and misses (note_stats_hits, note_stats_misses) in an object that holds the song's game object
// (note_stats_track), which keeps the song position it is at (track_song_time) and the grey-note time
// (track_grey_time): notes before it are grey and not detected. The game raises the grey-note time to the song
// position when the song pauses, so after a pause the song resumes a little earlier with the notes before the pause
// grey.
//
// On the Mac the counts are reached as the game's own code reaches them. The game root (game_root) has a state
// (game_root_state) with the game that runs now (game_state_current): the song's game object while a song plays
// (GRGame_PlaySong_GE). A game object has components, in a vector of entries (game_components, components_list,
// component_entry_size, component_entry_component), and the game finds one by its type: the counts are the component
// (GameComponentRepetitionLAS) whose type number (component_type) is note_stats_type and whose owner is that game
// object. One set of counts serves Learn a Song and Score Attack. These names are in the RS Re-ARMed after 1.18.1.
// Before them the counts were reached from the song's menu (song_menu_game, song_game_player, player_note_stats), a
// path read in runs: its second step is a place in a list whose order differs from run to run, so it reached the
// counts in some runs and nothing, or another object, in others (found 2026-11-23). It is not followed any more:
// with an RS Re-ARMed that hasn't the new names nothing is read on the Mac.
//
// In Learn & Play the counts are reached as RSMods reaches them, from the UI manager (ui_note_stats_0 to _3:
// D3DOverlay.cpp's ptr_noteDataOffsets), and the rest is the same: their track and its times. Score Attack counts in
// another object there (RSMods' ScoreAttackNoteData), reached by RSMods' path with another last step
// (ui_score_attack_stats: score_attack_hits, score_attack_misses; from RS Re-ARMed 1.14.0; seen in a run). Only the
// counts are read from it: the grey-note time isn't found in Score Attack in Learn & Play.
//
// The counts are read only in a song's own menus, and each pointer is read with a check that the memory is there;
// what a path ends at must look like counts and a track.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <math.h>
#include <stdio.h>

#include "rsmods.h"

static const char *const MENUS[] = {"LearnASong_Game", "NonStopPlay_Game", "ScoreAttack_Game", "LearnASong_Pause",
                                    "NonStopPlay_Pause", "ScoreAttack_Pause", NULL};

#define MAX_COUNT 100000  // more notes than any song has
#define MAX_TIME 36000.f  // seconds

// The counts' path: 1 the game's own, from the game root; 2 RSMods', from the UI manager; 0 none
static int g_path;
#define MAX_COMPONENTS 256  // of a game object: the song's has 16

static int known(void) {
  static int k = -1;
  if (k < 0) {
    int rsmods = RC_HAS_OFF(ui_note_stats_0) && RC_HAS_OFF(ui_note_stats_1) && RC_HAS_OFF(ui_note_stats_2) &&
                 RC_HAS_OFF(ui_note_stats_3) && RC_ADDR(ui_manager);
    // asked for only where RSMods' path isn't: a name a game hasn't is a line in its log
    int own = !rsmods && RC_ADDR(game_root) && RC_ADDR(note_stats_type) && RC_HAS_OFF(game_root_state) &&
              RC_HAS_OFF(game_state_current) && RC_HAS_OFF(game_components) && RC_HAS_OFF(components_list) &&
              RC_HAS_OFF(component_entry_size) && RC_HAS_OFF(component_entry_component) && RC_HAS_OFF(component_type) &&
              RC_HAS_OFF(std_vector_begin) && RC_HAS_OFF(std_vector_end) && RC_OFF(component_entry_size) > 0;
    g_path = own ? 1 : rsmods ? 2 : 0;
    k = g_path && RC_HAS_OFF(note_stats_hits) && RC_HAS_OFF(note_stats_misses) && RC_HAS_OFF(note_stats_track) &&
        RC_HAS_OFF(track_song_time) && RC_HAS_OFF(track_grey_time);
    if (!k)
      fprintf(stderr, "[rsmods] the note counts aren't known on %s: Show Current Accuracy and the grey-note reset need "
                      "the RS Re-ARMed after 1.18.1 on the Mac, 1.11.0 in Learn & Play\n", rc_target());
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

// The game's own way to the counts: the game that runs now, and its component of the counts' type. *game: that game
// object, for what is logged when it has none.
static const char *own_counts(const char **game) {
  const char *root = safe_ptr((const char *)(uintptr_t)RC_A(RC_ADDR(game_root)));
  const char *state = root ? safe_ptr(root + RC_OFF(game_root_state)) : NULL;
  const char *g = *game = state ? safe_ptr(state + RC_OFF(game_state_current)) : NULL;
  const char *list = g ? safe_ptr(g + RC_OFF(game_components)) : NULL;
  if (!list || !readable((const char *)(uintptr_t)RC_A(RC_ADDR(note_stats_type)), 4)) return NULL;
  uint32_t type = RC_LD(uint32_t, RC_A(RC_ADDR(note_stats_type)));
  list += RC_OFF(components_list);
  const char *at = safe_ptr(list + RC_OFF(std_vector_begin)), *end = safe_ptr(list + RC_OFF(std_vector_end));
  size_t size = (size_t)RC_OFF(component_entry_size);
  for (int n = 0; at && end && at < end && n < MAX_COMPONENTS; at += size, n++) {
    const char *c = safe_ptr(at + RC_OFF(component_entry_component));
    if (c && readable(c + RC_OFF(component_type), 4) && RC_LD(uint32_t, c + RC_OFF(component_type)) == type &&
        safe_ptr(c + RC_OFF(note_stats_track)) == g)
      return c;
  }
  return NULL;
}

// The counts and the track, or 0
static int find(const char **stats, const char **track) {
  if (!rsm_menu_in(MENUS) || !known()) return 0;
  const char *s, *t;
  if (g_path == 1) {
    const char *game;
    s = own_counts(&game);
    t = s ? game : NULL;
    if (!t) {
      not_found(game ? "the game that runs has no counts" : "no game runs", game, 0, 0);
      return 0;
    }
  } else {
    const char *p[5] = {safe_ptr((const char *)(uintptr_t)RC_A(RC_ADDR(ui_manager)))};
    const int64_t offs[4] = {RC_OFF(ui_note_stats_0), RC_OFF(ui_note_stats_1), RC_OFF(ui_note_stats_2),
                             RC_OFF(ui_note_stats_3)};
    for (int i = 0; i < 4; i++) p[i + 1] = p[i] ? safe_ptr(p[i] + offs[i]) : NULL;
    s = p[4];
    t = s ? safe_ptr(s + RC_OFF(note_stats_track)) : NULL;
    if (!t) {
      int step = 0;
      while (step < 4 && p[step + 1]) step++;
      not_found(step < 4 ? "a step of the path is empty" : "no track", step < 4 ? (const void *)p[step] : s, step, 0);
      return 0;
    }
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

// Learn & Play's Score Attack counts, or 0: in Score Attack's menus where the counts' path is RSMods'
static const char *const SA_MENUS[] = {"ScoreAttack_Game", "ScoreAttack_Pause", NULL};

static int score_attack_counts(int *hits, int *misses) {
  if (!rsm_menu_in(SA_MENUS) || !known() || g_path != 2) return 0;
  if (!RC_HAS_OFF(ui_score_attack_stats) || !RC_HAS_OFF(score_attack_hits) || !RC_HAS_OFF(score_attack_misses))
    return 0;
  const int64_t offs[4] = {RC_OFF(ui_note_stats_0), RC_OFF(ui_note_stats_1), RC_OFF(ui_note_stats_2),
                           RC_OFF(ui_score_attack_stats)};
  const char *p = safe_ptr((const char *)(uintptr_t)RC_A(RC_ADDR(ui_manager)));
  int step = 0;
  for (; step < 4 && p; step++) p = safe_ptr(p + offs[step]);
  if (!p) {
    not_found("a step of Score Attack's path is empty", NULL, step, 0);
    return 0;
  }
  if (!readable(p + RC_OFF(score_attack_hits), 4) || !readable(p + RC_OFF(score_attack_misses), 4)) {
    not_found("Score Attack's counts aren't readable", p, 0, 0);
    return 0;
  }
  int32_t h = RC_LD(int32_t, p + RC_OFF(score_attack_hits)), m = RC_LD(int32_t, p + RC_OFF(score_attack_misses));
  if (h < 0 || h > MAX_COUNT || m < 0 || m > MAX_COUNT) {
    not_found("not Score Attack's counts", p, h, m);
    return 0;
  }
  static int logged;
  if (!logged) logged = 1, rsm_log("note counts: Score Attack's at %p (%d hit, %d missed)", (const void *)p, h, m);
  *hits = h, *misses = m;
  return 1;
}

int rsm_note_counts(int *hits, int *misses) {
  if (score_attack_counts(hits, misses)) return 1;
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
