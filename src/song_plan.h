// Tone Host's view of a song ahead of the game: which tone each of an arrangement's five slots has, from its manifest,
// as the knobs the game sets when the tone starts. With the changes' times (the loaded song has them: sng_song_tones)
// the host has a tone ready before its place and plays it a little early. Without the game (tests/test_song_plan.c).
#ifndef SONG_PLAN_H
#define SONG_PLAN_H

#include <stddef.h>
#include <string.h>

enum { SONG_PLAN_SLOTS = 5, SONG_PLAN_GEAR = 3072 };  // Tone_Base, Tone_A to Tone_D; a tone's knobs as text

typedef struct {
  int max_difficulty;                              // MaxPhraseDifficulty: the song's levels are one more; -1 without
  char gear[SONG_PLAN_SLOTS][SONG_PLAN_GEAR];      // " Knob=value ..." as Tone Host sends a tone; "" an empty slot
} song_plan;

// The knobs Tone Host passes on, of all the game sets by name
static inline int song_plan_is_gear(const char *n) {
  static const char *const prefixes[] = {"Amp_", "Bass_", "DI_", "Pedal_", "Rack_", "Meter_Tone_Balance_Return", NULL};
  for (int i = 0; prefixes[i]; i++)
    if (!strncmp(n, prefixes[i], strlen(prefixes[i]))) return 1;
  return 0;
}

// An arrangement's manifest: 1 and its slots, or 0 when it has no tone for Tone_Base. A slot's tone is the entry of
// "Tones" with that Key: every KnobValues of its GearList, and its Volume as Meter_Tone_Balance_Return.
int song_plan_parse(const char *json, song_plan *out);

#endif
