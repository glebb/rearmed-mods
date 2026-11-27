// Seven-String Guitar (seven.h): what the tuner part shares with the others.
//
// Every UI_PlayerTuner (the tuner's session) is grown by the edits of seven_string (278h -> 380h on Learn & Play,
// 308h -> 458h on the Mac):
// its per-string arrays have a tail of eight slots at the end (offsets.map, seven_tuner_*), which the tuner's methods
// read instead of the game's six, and next to them the session's string count (seven_tuner_count: 4, 5 or 6 as the
// guitar is built, 7 or 8 for a 7/8-string target), the order the tuner goes through them (seven_tuner_order: 0..n-1,
// or the low strings first: 6,0..5 and 7,6,0..5, an extended bass's 4,0..3 and 4,0..3,5), a 7/8-string target (seven_tuner_target) and the camera's reference
// string (seven_tuner_edge). String 6 is the low B (its GuitarStrings slot, peg, FretGrid cell and colour row 6); on
// an extended bass slot 4 is.
#ifndef RS_SEVEN_TUNER_H
#define RS_SEVEN_TUNER_H

#include <stdint.h>

// Whether Play_Tuning_ReadStringOffsets found the manifest key Rs7String6 (the 7th string's offset) for the offset
// array at `offsets` (a host pointer: the tuner menus' arrays the game hands to ApplyTuning); its offset in *t6.
// For the tuning names of a 7-string arrangement (rs7string's TN), a later part.
int seven_tuner_seven_for(const void *offsets, int *t6);

// The extended instrument TK read the manifest keys of for the offset array at `offsets` (Rs7String6, and Rs7String7 for
// eight; for a bass Rs7String4, and Rs7String5 for six): its string count, 7 or 8 for a guitar (extra[0], extra[1] = the
// 7th and 8th strings' offsets, 0 without the key) or 5 or 6 for a bass (extra both 0: its low B and high C offsets are
// string4 and string5 of the array), with *instrument SEVEN_GUITAR or SEVEN_BASS; 0 for an array without such keys
// (a stock tuning; extra 0, *instrument SEVEN_GUITAR). The tuning names of the tuner (seven_names.c) use it.
int seven_tuner_strings_for(const void *offsets, int16_t extra[2], int *instrument);

// The session's string count from its 7/8-string target (0 none), the count its guitar was built with (0 not known)
// and the count it has: the target, but no more than a guitar of 7 or more was built with (an 8-string tuning on a
// guitar built with 7, when this RS Re-ARMed cannot build eight, tunes seven); else the build count, six on a guitar
// built with more when the target has six strings (as rs7string's); a 5 or 6 of a bass's build stays (the caller tells
// a 6-string bass from a guitar)
static inline int seven_tuner_count(int target, int built, int current) {
  if (target >= 7) return built >= 7 && built < target ? built : target;
  const int n = built > 0 ? built : current;
  return built == 5 || built == 6 ? n : n > 6 ? 6 : n;
}

#endif
