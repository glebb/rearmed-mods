// Seven-String Guitar (seven.h): what the tuner part shares with the others.
//
// Every UI_PlayerTuner (the tuner's session) is 380h bytes with the edits of seven_string, where the game makes 278h:
// its per-string arrays have a tail of eight slots at the end (offsets.map, seven_tuner_*), which the tuner's methods
// read instead of the game's six, and next to them the session's string count (seven_tuner_count: 4 or 6 as the
// guitar is built, 7 or 8 for a 7/8-string target), the order the tuner goes through them (seven_tuner_order: 0..n-1,
// or the low strings first: 6,0..5 and 7,6,0..5), a 7/8-string target (seven_tuner_target) and the camera's reference
// string (seven_tuner_edge). String 6 is the low B (its GuitarStrings slot, peg, FretGrid cell and colour row 6).
#ifndef RS_SEVEN_TUNER_H
#define RS_SEVEN_TUNER_H

#include <stdint.h>

// Whether Play_Tuning_ReadStringOffsets found the manifest key Rs7String6 (the 7th string's offset) for the offset
// array at `offsets` (a host pointer: the tuner menus' arrays the game hands to ApplyTuning); its offset in *t6.
// For the tuning names of a 7-string arrangement (rs7string's TN), a later part.
int seven_tuner_seven_for(const void *offsets, int *t6);

#endif
