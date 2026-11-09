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

// PUBGB's bounded wait: 1 when Rnd_FretGrid_Build gave up on this guitar's cells 6/7 (its strings 6/7 were not loaded
// within 5 s): a song then keeps six strings on it. The strings part (C-lite) treats such a guitar as not built with
// the count a song wants, and calls seven_pitch_grid_forget_all after it builds a guitar (rs7string's
// Rs7Publish_GridForgetAll: no deadline or failed mark of the old guitar carries over).
int seven_pitch_neck_failed(const void *guitar);
void seven_pitch_grid_forget_all(void);

#endif
