// Seven-String Guitar (seven.h): what the pitch part shares with the others.
//
// The detection (the game's pitch detection, rs7string's "detector") and the matcher keep their six-entry arrays and
// grow by the edits of seven_string (PA1, PA2): beside them an eight-entry tuning tail (offsets.map:
// seven_detection_tuning8, seven_matcher_tuning8), the detection's open MIDI notes of eight strings
// (seven_detection_open8) and the count of strings it scores (seven_detection_count: 6, a bass's 4, or the 7/8 of a
// song that widened it). Slots 0-5 of a tail are always the game's own six.
#ifndef RS_SEVEN_PITCH_H
#define RS_SEVEN_PITCH_H

#include <stdint.h>

// The detection's eight-entry tuning (offsets from standard), 1; 0 and zeros when it has no tail (a host pointer to the
// game's detection, or NULL). rs7string's tuner reads strings 6/7 of a song from it (Rs7Pitch_Tailed + kDetTuning8).
int seven_pitch_tuning8(const void *detection, int16_t out[8]);
// The count of strings the detection scores (6, 4 for a bass, 7 or 8 when a song widened it); 0 without a tail
int seven_pitch_count(const void *detection);

// The detection's raw candidate floor (the lowest MIDI note whose peaks count, rs7string's rsxrp::Floor): the game's
// reset writes 24 (SEVEN_FLOOR_STOCK); a song whose lowest open note is below the floor gets one semitone below that
// note, never less than SEVEN_FLOOR_MIN (the detector's lag buffer reaches MIDI 18.3 at 48 kHz, so G0 = 19 is the lowest
// open string it can hear). `reset` is the floor the game's reset wrote, `lowest` the detection's lowest open note.
#define SEVEN_FLOOR_STOCK 24
#define SEVEN_FLOOR_MIN 18
static inline int seven_pitch_floor(int reset, int lowest) {
  if (lowest >= reset) return reset;
  const int low = reset < SEVEN_FLOOR_MIN ? reset : SEVEN_FLOOR_MIN;
  return lowest - 1 > low ? lowest - 1 : low;
}
// Sets the floor of a detection (a host pointer to the game's detection) from its lowest open note, after anything that
// wrote either: its reset (reset = 1: the floor just written is this detection's own), a SetTuning, a song's setup, the
// tuner's low range. Nothing on a build whose RS Re-ARMed lacks the floor's offset.
void seven_pitch_det_floor(void *detection, int reset);

// PUBGB's bounded wait: 1 when Rnd_FretGrid_Build gave up on this guitar's cells 6/7 (its strings 6/7 were not loaded
// within 5 s): a song then keeps six strings on it. The strings part (C-lite) treats such a guitar as not built with
// the count a song wants, and calls seven_pitch_grid_forget_all after it builds a guitar (rs7string's
// Rs7Publish_GridForgetAll: no deadline or failed mark of the old guitar carries over).
int seven_pitch_neck_failed(const void *guitar);
void seven_pitch_grid_forget_all(void);

#endif
