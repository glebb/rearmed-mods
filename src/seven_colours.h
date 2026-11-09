// Seven-String Guitar (seven.h): what the colours part shares with the others, and with mod_colors.c.
#ifndef RS_SEVEN_COLOURS_H
#define RS_SEVEN_COLOURS_H

#include <stdint.h>

// The colour manager's layout: per colour mode (0 normal, 1 colourblind) 28 kinds of colour, each with a column per
// string, eight here (seven_colours.c). Kinds the other parts read:
enum {
  SEVEN_COLOUR_MODES = 2,
  SEVEN_COLOUR_KINDS = 28,
  SEVEN_COLOUR_COLUMNS = 8,
  SEVEN_KIND_LIT = 1,   // a string as it is played
  SEVEN_KIND_DIM = 2,   // dimmed (a section without it)
  SEVEN_KIND_GLOW = 3,  // its glow
  SEVEN_KIND_PEG = 4,   // its peg in the tuner (rs7string's peg names 2 to 9)
};

// The seventh string's (string 6's) colour in a colour mode: three floats from 0 to 1, as the game keeps a colour. Its
// other kinds (dimmed, glow, ...) are column 6 of the manager (seven_colours_get). The values follow RSMods.ini.
const float *seven_colours_rgb(int mode);

// The game's colour manager now (a host pointer into the game's memory), or NULL before it is built
uint8_t *seven_colours_manager(void);
// Its colour mode for player 0 or 1: 0 normal, 1 colourblind
int seven_colours_mode(const uint8_t *manager, int player);
// The colour of a kind for string 0 to 7 in a mode: three floats in the game's memory (a host pointer), or NULL
const float *seven_colours_get(const uint8_t *manager, int mode, int kind, int string);

// rs7string's model of a string's kinds (model/stringcolour): the dimmed colour of rgb, its brightest channel at dim
// (the game's own strings' dimmed brightness, SEVEN_COLOURS_DIM without any), and the glow colour, rgb at full
// brightness. Black stays black.
#define SEVEN_COLOURS_DIM (76.0f / 255.0f)
void seven_colours_dim(const float rgb[3], float dim, float out[3]);
void seven_colours_glow(const float rgb[3], float out[3]);

// On the game thread: a colour change (RSMods.ini) published into the manager now, if the manager is the one built
// on this thread; otherwise it waits for the manager's next build (rs7string's DrainPending). The colours part's own
// frame hook calls it; no other part needs to.
void seven_colours_drain(void);

// The note ramp: the game colours a note by sampling a 256 x 128 texture (note_gradient_map.dds, mod_colors.c's
// RAMP_A), 16 rows of 8 lines, a row a string and colour mode, at V = (2 row + 1) / 32. String 6 samples lines no row
// is sampled at, in the grey rows (7 and 15): in the game's own ramp it is grey, until a ramp with its colour there is
// drawn. mod_colors.c, which has the ramp's rule, draws one:
//   seven_colours_ramp(rgba, w, h): string 6's lines painted into a ramp of that layout (RGBA, rows top first, h 128)
//     it made; 0, and rgba as it was, when the unit is off or h isn't 128
//   seven_colours_game_ramp(): the game's own ramp with string 6's lines, RGBA 256 x 128, to draw in place of the
//     game's; NULL when the unit is off, the renderer has no render API, or the game's ramp isn't read (static.psarc,
//     and it must be what the game draws). Game thread.
//   seven_colours_ramp_serial(): changes when either gives other pixels (string 6's colour changed)
int seven_colours_ramp(uint8_t *rgba, int w, int h);
const uint8_t *seven_colours_game_ramp(void);
unsigned seven_colours_ramp_serial(void);

#endif
