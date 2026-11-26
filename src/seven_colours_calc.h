// Seven-String Guitar (seven.h): the colours part's arithmetic, without the game (tests/test_seven_colours.c): the
// kinds of a string made from its colour, and the note ramp's lines of strings 6 and 7 (rs7string's model/stringcolour
// and Rs7_Recolor, github.com/SomeeName/rs7string, MIT, by SomeeName).
#ifndef RS_SEVEN_COLOURS_CALC_H
#define RS_SEVEN_COLOURS_CALC_H

#include <stdint.h>

// (also declared in seven_colours.h, defined in seven_colours_calc.c)
void seven_colours_dim(const float rgb[3], float dim, float out[3]);
void seven_colours_glow(const float rgb[3], float out[3]);

// The note ramp is 256 x 128, 16 rows of 8 lines (a row a string and colour mode, sampled at V = (2 row + 1) / 32).
// Strings 6 and 7 sample lines no row is sampled at, in the grey rows 7 (normal) and 15 (colourblind): V6 = 57/128 and
// 121/128 (lines 56, 57 and 120, 121), V7 = 62/128 and 126/128 (lines 61, 62 and 125, 126; rs7string's kRsxrV7)
#define SEVEN_RAMP_V6_0 (57.0f / 128.0f)
#define SEVEN_RAMP_V6_1 (121.0f / 128.0f)
#define SEVEN_RAMP_V7_0 (62.0f / 128.0f)
#define SEVEN_RAMP_V7_1 (126.0f / 128.0f)

// A channel of the grey line g recoloured to c around its anchor a, the grey's value where a row has its colour
// (rs7string's Rs7_Recolor): black to c up to a, c to white above
uint8_t seven_ramp_recolour(uint8_t g, uint8_t a, uint8_t c);

// Strings 6's and 7's lines painted into a ramp of 256 x 128 layout (RGBA, rows top first, w >= 8, h 128), from the
// grey line in the middle of the grey row, with colour rgb6[mode] / rgb7[mode] (three floats from 0 to 1). The rest
// of the ramp is untouched.
void seven_ramp_paint(uint8_t *rgba, int w, int h, const float rgb6[2][3], const float rgb7[2][3]);

#endif
