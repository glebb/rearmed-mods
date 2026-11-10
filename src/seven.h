// Seven-String Guitar: a low B string, as rs7string (github.com/SomeeName/rs7string, MIT, by SomeeName) gives the
// Windows game. A 7-string chart (DotCharter's: a seven-entry tuning, the manifest's Rs7String6, chord data in the
// SNG's RS7X block) plays on a guitar built with seven strings: a seventh lane and string, its colour, the fret grid
// and bars across seven, the low B detected and scored, the tuner starting on it. Six-string songs and bass are as
// the game has them.
//
// rs7string patches the running Windows game. Here the same changes are RS Re-ARMed's edits and mid points
// (include/rs-rearmed/rc_mid.h, its tools/targets/<game>/edits.map and mids.map): the game is built with the
// changed instructions beside its own, and this library switches them on in its constructor, before the game runs.
// rs7string's design is kept: the game's arrays for six strings grow to eight (rs7string's "native layouts"), and
// its own code runs over them; where rs7string runs a callback in the middle of a function, a mid point here runs
// the same callback, ported to C.
//
// All or nothing, as in rs7string: the parts register their mid points first (seven_*_init), and only when every
// one has its points does the edit group seven_string go on. A callback does nothing unless it is on (seven_on), so
// the game's own instructions run.
//
// The parts (one source each): seven_colours.c (the colour manager's eight columns), seven_grid.c (fret grid, the
// next-note grid, the fret bars), seven_strings.c (the guitar's build with 7 strings, its string objects, pegs and
// the seventh string's look), seven_pitch.c (detection: the expected pitches, the song's 7-string mode, what the
// detector publishes), seven_tuner.c (the tuner over eight slots, low B first); chords with the seventh string:
// seven_chords.c (the SNG's RS7X block and the chord records over eight strings), seven_chdetect.c (a chord's notes
// detected and scored), seven_chdraw.c (a chord's notes, techniques and frame drawn), seven_diagrams.c (the chord
// book's seven-row diagrams); seven_names.c (a 7-string tuning's name: "B Standard 7Str"); seven_headstock.c (the seven-peg headstock,
// built from the player's own game files). mod_seven_string.c is the mod. DotCharter writes its charts unencrypted:
// those load with Plain Packages (mod_plain_packages.c), a mod of its own.
#ifndef RS_SEVEN_H
#define RS_SEVEN_H
#include "rsmods.h"

// The edit group of the whole unit (names/edits.list)
#define SEVEN_GROUP "seven_string"

// 1 once the unit is on (the edits switched on): every callback checks it first
extern int seven_on;

// Registers fn on the mid point `name`, and remembers it (seven_values). 0, or -1 with a log line: the mod stays off.
int seven_mid(const char *name, rc_mid_fn fn);
// The index of a point's value / continuation by name, looked up once per place (a point's index is f->point)
#define SEVEN_VAL(f, name) ({ static int v_ = -2; if (v_ == -2) v_ = rc_mid_value((f)->point, name); v_; })
#define SEVEN_CONT(f, name) ({ static int k_ = -2; if (k_ == -2) k_ = rc_mid_cont((f)->point, name); k_; })
// Read and write a value by name
#define SEVEN_GET(f, name) rc_mid_get((f), SEVEN_VAL(f, name))
#define SEVEN_SET(f, name, v) rc_mid_set((f), SEVEN_VAL(f, name), (uint64_t)(v))
#define SEVEN_GETF(f, name) rc_mid_get_f((f), SEVEN_VAL(f, name))
#define SEVEN_SETF(f, name, v) rc_mid_set_f((f), SEVEN_VAL(f, name), (double)(v))

// A game code address (RC_ADDR: unslid) as the game holds it: what a return address read from the stack, a function
// pointer or a vtable entry is, and what one written into game memory must be. The Mac image is mapped slid
// (RC_A); a 32-bit game's guest addresses are its image addresses, so there it is the address itself. 0 stays 0.
static inline uint64_t seven_code(uint64_t addr) {
  return addr ? RC_GUEST((const void *)(uintptr_t)RC_A(addr)) : 0;
}
// 1 when v, a code address read from the game, is the image address addr (0: never)
static inline int seven_at(uint64_t v, uint64_t addr) { return addr && v == seven_code(addr); }

// The parts: each registers its mid points; 0, or -1 (then the unit stays off)
int seven_colours_init(void);
int seven_grid_init(void);
int seven_strings_init(void);
int seven_pitch_init(void);
int seven_tuner_init(void);
int seven_chords_init(void);
int seven_chdetect_init(void);
int seven_chdraw_init(void);
int seven_diagrams_init(void);
int seven_names_init(void);
int seven_headstock_init(void);

// What the parts share is declared in their own headers
#include "seven_colours.h"
#include "seven_grid.h"
#include "seven_pitch.h"
#include "seven_strings.h"
#include "seven_tuner.h"
#include "seven_chords.h"
#include "seven_chdetect.h"
#include "seven_chdraw.h"
#include "seven_diagrams.h"
#include "seven_names.h"
#include "seven_headstock.h"

#endif
