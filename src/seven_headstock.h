// Seven-String Guitar (seven.h): what the headstock part shares with the others.
//
// A guitar built with seven or eight strings, and a bass built with five or six, show rs7string's headstock for it: its
// models hang, hidden, under the stock frame (rsxr_alt, and rsxr_alt5 for one string fewer), and the instrument's
// frame shows the one it is built with in place of the stock part. The strings part asks it a few things.
#ifndef RS_SEVEN_HEADSTOCK_H
#define RS_SEVEN_HEADSTOCK_H

#include <stdint.h>

// PSL1: the pegs the PegSet of an instrument with this InstrumentInfo loads, from its build count (count): up to 8 for a
// guitar (6 stock), up to 6 for a bass (4 stock), as many as the models of its frame (its layout and hand) have, whose
// Locator_Peg_n the game's binder then gives peg n; else the stock count, as the game has them
int seven_headstock_pegs(const void *info, int count);

// GSHC: the headstock segment of string 6 or 7 (string_size_7 / _8 of the model) while this guitar (Rnd_Guitar) shows
// it: its colour (g_diffuseColor extra data, a host pointer), which the game's segment loop then gives that string's
// colour; NULL else (another string, a model without the segment, the stock headstock). seg7_colour is string 6's.
void *seven_headstock_seg_colour(const void *guitar, int string);
void *seven_headstock_seg7_colour(const void *guitar);

// 1 when the extended bass's headstock is in place (its PSL1 hook is taken): the bass part switches the bass on with it.
// A bass whose models can't be built (the game's files aren't as rs7string's program wants) plays with four pegs.
int seven_headstock_bass_ready(void);

// Every N5 frame of a guitar or bass (the headstock's own hook calls it; calling it again does no harm): with a
// [neck] spacing_* factor for this extended instrument (seven_spacing_k) the headstock frame is narrowed across the
// strings by it and the pegs follow. Needs the spacing feature.
void seven_headstock_frame(void *guitar);

// LT1: the in-line byte the tuner's peg letters go by for this InstrumentInfo: its own (+0x0a), or 1 on a 3+3 frame
// that shows the in-line seven-peg model (the letters of the pegs from count/2 on would turn round)
int seven_headstock_letter_inline(const void *info);

#endif
