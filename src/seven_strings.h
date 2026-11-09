// Seven-String Guitar (seven.h): what the strings part shares with the others.
//
// The string count is a property of the guitar's build (rs7string's C-lite): a guitar profile carries it (+0x38),
// the guitar built from it keeps it in its InstrumentInfo (+0x0b) for its whole life, and a song selection that asks
// for another count rebuilds player 0's guitar through the game's own rebuild. The song asks for seven strings when its
// arrangement's Tuning has the manifest key Rs7String6 (DotCharter's 7-string charts).
//
// Every GuitarStrings, InstrumentInfo and PegSet of the game has the native layout of eight (offsets.map, seven_*):
//   GuitarStrings  +0x40 main[8], +0x60 ghost[8] (the game's six at +0x4 / +0x1c stay as copies of the first six),
//                  +0x80 glow[8] (the renderer's glow ramp), +0xa0 tail, +0xa4 its guitar, +0xa8 its InstrumentInfo
//   InstrumentInfo +0x40 active strings, +0x41 loaded strings (both the build count: 4 bass, 6, 7), +0x38 / +0x3c
//                  perStringMode of strings 6 / 7 (string 0's)
//   PegSet         slots[8] at +0xc, pegColor[8] at +0x14c, tail at +0x1cc
#ifndef RS_SEVEN_STRINGS_H
#define RS_SEVEN_STRINGS_H

#include <stdint.h>

#define SEVEN_STOCK 6     // the game's strings of a guitar
#define SEVEN_CAPACITY 8  // the native arrays

// The count the guitar (Rnd_Guitar, a host pointer) was built with, InstrumentInfo +0x0b: 4 (bass), 6 or 7; 0 for
// none. seven_strings_count is the same.
int seven_strings_guitar_count(const void *guitar);
int seven_strings_count(const void *guitar);
// The same for an InstrumentInfo
int seven_strings_info_count(const void *info);
// The strings the guitar of this InstrumentInfo shows (its active count, +0x40), 0 for none
int seven_strings_active(const void *info);

// The count the last song selection asked for: 7 for an arrangement with Rs7String6, else 6. Player 0's next guitar
// profile is built with it (a mode entry, Session Mode, Guitarcade, Lessons, the technique hub, asks for 6).
int seven_strings_desired(void);

// String s (0-7) of a GuitarStrings (host pointer), its ghost when ghost: a host pointer, NULL for none
uint8_t *seven_strings_string(const void *strings, int s, int ghost);
// The main string object s (0-7) of a guitar (Rnd_Guitar), NULL for none (rs7string's Child(gs, s, false))
void *seven_strings_child(void *guitar, int s);
// 0 when the strings past six the guitar loads (its build count: string 6 of a guitar built with 7; none on a guitar
// built with 6 or a bass) and their ghosts exist with their models loaded; else the first failing check, as
// rs7string's ExtraWhy numbers them (1 no GuitarStrings, 3 no InstrumentInfo, 4 not the guitar's, 8 + (s - 6) * 6 +
// ghost * 3 + {0 no object, 1 no entity, 2 not loaded}). Only strings below the build count are loaded here (not all
// eight, as rs7string's): string 7 of a 7-string guitar never loads.
int seven_strings_extra_why(void *guitar);
// The attach point of string 6 or 7 (vec3 to out, the caller's in the game's memory), from strings 0 and 1 by the
// game's own query; 1 when written (rs7string's AttachExt)
int seven_strings_attach_ext(void *info, unsigned string, float *out);

// LT1 (the tuner's peg letters): the in-line byte of the guitar of this InstrumentInfo (+0x0a), or 1 on a 3+3 frame
// that shows the 7-string in-line headstock model (seven_headstock_letter_inline)
int seven_strings_letter_inline(const void *info);

#endif
