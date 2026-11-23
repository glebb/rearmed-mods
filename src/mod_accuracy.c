// RSMods: Show Current Accuracy (DisplayCurrentAccuracy; D3DOverlay.cpp GameOverlay::DisplaySongAccuracy). In a song,
// once it plays, the notes hit out of the notes played so far, as a percentage with two decimals, at the top right
// under the song timer (100% before the first note). The counts are the game's own (rsmods_note_stats.c).
//
// Different: RSMods also shows 0.00% in the song's other menus (Riff Repeater, the tuner, the mixer), where it reads
// no counts; here nothing shows there.
#include <stdio.h>

#include "rsmods.h"

#define TEXT_SIZE 0.028f

static const char *const PAUSED[] = {"LearnASong_Pause", "NonStopPlay_Pause", "ScoreAttack_Pause", NULL};

// Why it shows or doesn't, in the log when that changes (RS_MODS_LOG)
static void say(int state, double t, int hits, int misses) {
  static int said = -1;
  if (state == said) return;
  said = state;
  static const char *const WHY[] = {"shown", "the song's position isn't read", "the song is at 0", "no note counts"};
  rsm_log("accuracy: %s (%s, position %g, %d hit, %d missed)", WHY[state], rsm_menu(), t, hits, misses);
}

static void tick(void) {
  if (!rsm_on("DisplayCurrentAccuracy") || !rsm_menu_in(RSM_SONG_MODES)) return;
  double t = rsm_song_time();  // -1 while paused: the song's position isn't read then
  if (t == 0 || (t < 0 && !rsm_menu_in(PAUSED))) {
    say(t == 0 ? 2 : 1, t, 0, 0);
    return;
  }
  int hits, misses;
  if (!rsm_note_counts(&hits, &misses)) {
    say(3, t, 0, 0);
    return;
  }
  say(0, t, hits, misses);
  int total = hits + misses;
  double pct = total > 0 ? 100.0 * hits / total : 100.0;
  char txt[16];
  snprintf(txt, sizeof txt, "%.2f%%", pct);
  // RSMods' rectangle is the song timer's, a line and two quarter lines lower
  rsm_text(txt, 1.f - 1.f / 96, 1.f / 54 + TEXT_SIZE * 1.5f, TEXT_SIZE, RSM_ALIGN_RIGHT, 0xffffffff);
}

const rsm_mod rsm_mod_accuracy = {"accuracy", tick, NULL, NULL, NULL};
