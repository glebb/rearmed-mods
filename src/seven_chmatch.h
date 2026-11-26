// Seven-String Guitar (seven.h): two rules of the chord matcher, pure logic. Ported from rs7string
// (github.com/SomeeName/rs7string, MIT, by SomeeName): src/features/nslot/match.cpp (LowChord, TableRow) as of
// 1.0.0-beta.6. Used by seven_chdetect.c; tests/test_seven_chmatch.c runs it.
#ifndef RS_SEVEN_CHMATCH_H
#define RS_SEVEN_CHMATCH_H

#include <stdint.h>

// Below E1 (MIDI 28, the lowest note of a stock bass) a string sounds mostly at its octave: in a chord of two or three
// notes the lowest one (the game: its fundamental only) may also match on its octave.
#define SEVEN_LOW_OCTAVE 28

// The game's subharmonic table has rows of six notes for MIDI 22-93 only (its own lookups bound it the same way,
// unsigned compare with 0x47 after midi - 22); the stock matcher indexes it unchecked, so a very low extra string would
// read past it
#define SEVEN_TABLE_FIRST 22
#define SEVEN_TABLE_ROWS 72
static inline int seven_table_has_row(int32_t midi) {
  return midi >= SEVEN_TABLE_FIRST && midi < SEVEN_TABLE_FIRST + SEVEN_TABLE_ROWS;
}

// Two or three different targets (a negative one is none; a repeated one counts once) and the lowest positive one is
// below SEVEN_LOW_OCTAVE: the chords the octave rule changes
static inline int seven_low_chord(const int32_t *in, int n) {
  int count = 0;
  int32_t low = 0x80;
  for (int i = 0; i < n; i++) {
    int repeat = 0;
    for (int j = 0; j < i; j++) repeat = repeat || in[j] == in[i];
    if (in[i] < 0 || repeat) continue;
    count++;
    if (in[i] > 0 && in[i] < low) low = in[i];
  }
  return count >= 2 && count <= 3 && low < SEVEN_LOW_OCTAVE;
}

#endif
