// RSMods: show the current note (GameOverlay::DisplayCurrentNote; ShowCurrentNoteOnScreen = on): the note the game hears
// the player play (rsmods_game_note.c), as "E2". In a song it shows left of the middle, a little below it, as RSMods
// places it; elsewhere at the top, as "Current Note: E2".
#include <stdio.h>

#include "rsmods.h"

static void tick(void) {
  if (!rsm_on("ShowCurrentNoteOnScreen")) return;
  int note = rsm_guitar_note(NULL);
  if (note < 0) return;
  char name[8], text[32];
  rsm_note_name(note, name, sizeof name);
  if (rsm_menu_in(RSM_SONG_MODES)) {
    rsm_text(name, 0.5f - 349.f / 1920, 617.f / 1080, 0.04f, RSM_ALIGN_CENTER, 0xffffffff);
  } else {
    snprintf(text, sizeof text, "Current Note: %s", name);
    rsm_text(text, 0.5f, 35.f / 1080, 0.028f, RSM_ALIGN_CENTER, 0xffffffff);
  }
}

const rsm_mod rsm_mod_current_note = {"current_note", tick, NULL, NULL, NULL};
