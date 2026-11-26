// Seven-String Guitar (seven.h): the colours part's low B swap, as pure logic over the colour manager's eight-column
// tables (tests/test_seven_colours.c): rs7string's SwapBassLowB / RestoreSlot4 (src/features/colours/native.cpp,
// github.com/SomeeName/rs7string, MIT, by SomeeName).
//
// An extended bass (five or six strings, no guitar in the song) has its low B as native string 4, where a guitar has
// its B string (grey-blue). The swap makes column 4 of every kind show column 6's colours (the pointers of all kinds,
// the V of the ramp kinds), the colours of string 6 that the unit gives its seventh string. It is undone when the song
// has no such bass, and a build of the game's tables in between (which writes column 4 afresh) is respected: only an
// entry still holding what the swap wrote is put back.
#ifndef RS_SEVEN_COLOURS_SWAP_H
#define RS_SEVEN_COLOURS_SWAP_H

#include <stddef.h>
#include <stdint.h>

enum { SEVEN_SWAP_MODES = 2, SEVEN_SWAP_KINDS = 28, SEVEN_SWAP_COLUMNS = 8, SEVEN_SWAP_LOW_B = 4, SEVEN_SWAP_SOURCE = 6 };

// The tables of one manager: the colour pointers (ptr_size bytes each, 4 for Learn & Play, 8 on the Mac) and the V
// (four bytes each), [mode][kind][column]
typedef struct {
  uint8_t *ptrs, *uvs;
  int ptr_size;
} seven_swap_tables;

typedef struct {
  int swapped;                // the tables of `owner` show the swap
  const void *owner;          // whose (a new manager has a stock column 4 of its own)
  uint64_t saved_ptr[SEVEN_SWAP_MODES][SEVEN_SWAP_KINDS], swapped_ptr[SEVEN_SWAP_MODES][SEVEN_SWAP_KINDS];
  uint32_t saved_uv[SEVEN_SWAP_MODES][SEVEN_SWAP_KINDS], swapped_uv[SEVEN_SWAP_MODES][SEVEN_SWAP_KINDS];
} seven_swap;

// Whether a song wants it: an extended bass (5 or 6 strings) and no guitar
static inline int seven_swap_wanted(int bass_max_strings, int any_guitar) { return bass_max_strings >= 5 && !any_guitar; }

// Column 4 shows column 6 in every kind (the V only in uv_kinds). `owner` is the manager. Applying twice does nothing.
void seven_swap_apply(seven_swap *s, const seven_swap_tables *t, const void *owner, const int *uv_kinds, int n_uv);
// Puts back what the swap wrote, entry by entry, where the entry still holds it; a different owner's tables are left
// as they are (the swap is forgotten). Does nothing when there is no swap.
void seven_swap_restore(seven_swap *s, const seven_swap_tables *t, const void *owner);

#endif
