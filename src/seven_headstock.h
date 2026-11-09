// Seven-String Guitar (seven.h): what the headstock part shares with the others.
//
// A guitar built with seven strings shows rs7string's seven-peg headstock: its model hangs, hidden, under the stock
// frame (rs7_alt), and the guitar's frame shows it in place of the stock part. The strings part asks it three things.
#ifndef RS_SEVEN_HEADSTOCK_H
#define RS_SEVEN_HEADSTOCK_H

#include <stdint.h>

// PSL1: the pegs the PegSet of a guitar with this InstrumentInfo loads, from its build count (count): 7 for a guitar
// built with seven whose frame (its layout and hand) carries the model, whose Locator_Peg_7 the game's binder then
// gives peg 7; else at most six, as the game has them
int seven_headstock_pegs(const void *info, int count);

// GSHC: string 6's headstock segment, string_size_7 of the model, while this guitar (Rnd_Guitar) shows it: its colour
// (g_diffuseColor extra data, a host pointer), which the game's segment loop then gives string 6's colour; NULL else
void *seven_headstock_seg7_colour(const void *guitar);

// LT1: the in-line byte the tuner's peg letters go by for this InstrumentInfo: its own (+0x0a), or 1 on a 3+3 frame
// that shows the in-line seven-peg model (the letters of the pegs from count/2 on would turn round)
int seven_headstock_letter_inline(const void *info);

#endif
