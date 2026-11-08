// The note the game hears the player play, for Show Current Note and GuitarSpeak (rsm_guitar_note), as RSMods reads it
// (GuitarSpeak.cpp, GetCurrentNote). The game's DetectionService (detection_service, a global) has a detector
// (detection_service_detector), whose pitch detection (detector_detection) keeps the note being played: detection_note,
// or detection_note_bass while the input is a bass (detection_instrument 2). The game's own check of a note in a song
// reads it so (the Mac's 0x10022dca0, Learn & Play's 0x1067130). A MIDI note, 0 when there is none and 255 at the end of
// one. These names are in RS Re-ARMed from 1.11.0; without them both mods stay off, with a line in the log.
//
// A new note, as RSMods' GuitarSpeak counts one: a note other than the last, or any note after the end of the last or
// after none.
#include <stdio.h>

#include "rsmods.h"

#define NO_NOTE 0
#define END_OF_NOTE 255
#define TOP_NOTE 96  // RSMods: C7, the highest the game gives

static int g_note = -1, g_last, g_value = -2;
static unsigned g_plucks;

int rsm_guitar_note(unsigned *plucks) {
  if (plucks) *plucks = g_plucks;
  return g_note;
}

void rsm_note_name(int midi, char *out, size_t n) {
  static const char *const NAMES[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  if (midi < 0 || midi > 127)
    snprintf(out, n, "%s", "");
  else
    snprintf(out, n, "%s%d", NAMES[midi % 12], midi / 12 - 1);
}

static int known(void) {
  static int k = -1;
  if (k < 0) {
    k = RC_ADDR(detection_service) && RC_HAS_OFF(detection_service_detector) && RC_HAS_OFF(detector_detection) &&
        RC_HAS_OFF(detection_instrument) && RC_HAS_OFF(detection_note) && RC_HAS_OFF(detection_note_bass);
    if (!k)
      fprintf(stderr, "[rsmods] the game's detected note isn't known on %s: Show Current Note and Guitar Speak need "
                      "RS Re-ARMed 1.11.0\n", rc_target());
  }
  return k;
}

// The game's value: a MIDI note, NO_NOTE or END_OF_NOTE; -1 while there is no detection to read
static int read_note(void) {
  const char *service = rc_mod_ptr((const void *)(uintptr_t)RC_GA(detection_service));
  const char *detector = service ? rc_mod_ptr(service + RC_OFF(detection_service_detector)) : NULL;
  const char *detection = detector ? rc_mod_ptr(detector + RC_OFF(detector_detection)) : NULL;
  if (!detection) return -1;
  uint32_t instrument = RC_LD(uint32_t, detection + RC_OFF(detection_instrument));
  int v = (int)RC_LD(int32_t, detection + (instrument == 2 ? RC_OFF(detection_note_bass) : RC_OFF(detection_note)));
  if (v != g_value) rsm_log("game note: %d (instrument %u)", v, instrument);  // with RS_MODS_LOG: what the game has
  g_value = v;
  return v;
}

static void tick(void) {
  if ((!rsm_on("ShowCurrentNoteOnScreen") && !rsm_on("GuitarSpeak")) || !known()) {
    g_note = -1;
    return;
  }
  int v = read_note();
  if (v < 0 && g_value != -1) {
    rsm_log("game note: no pitch detection yet");
    g_value = -1;
  }
  if (v == END_OF_NOTE || (v & 0xff) == END_OF_NOTE) {  // the game's byte, whatever is above it
    g_note = -1;
    g_last = NO_NOTE;
    return;
  }
  if (v <= NO_NOTE || v >= TOP_NOTE) {  // RSMods takes these as no note: the next note is a new one
    g_note = -1;
    g_last = NO_NOTE;
    return;
  }
  if (v != g_last) {
    g_plucks++;
    g_last = v;
  }
  g_note = v;
}

const rsm_mod rsm_mod_game_note = {"game_note", tick, NULL, NULL, NULL};
