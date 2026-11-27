// Seven-String Guitar (seven.h): the instrument model of the unit, shared by all its parts: how many strings each
// player's guitar or bass has, where a native string lives in the game's arrays, the extended-bass setups per
// detector, which optional features are on, and the settings of [Seven String] in RSMods.ini. rs7string's model/slots,
// model/instrument, C-lite's Desired / BassDesired and rsxrbass::Players (github.com/SomeeName/rs7string, MIT, by
// SomeeName). Pure logic and state only (seven_model.c, seven_settings.c); no game address.
#ifndef RS_SEVEN_MODEL_H
#define RS_SEVEN_MODEL_H
#include <stdint.h>

enum { SEVEN_PLAYERS = 2 };
enum seven_instrument { SEVEN_GUITAR = 0, SEVEN_BASS = 1 };  // the native header's / the STRS block's instrument
#define SEVEN_BASS_STOCK 4
#define SEVEN_GUITAR_MAX 8
#define SEVEN_BASS_MAX 6

static inline int seven_is_ext_guitar(int n) { return n == 7 || n == 8; }
static inline int seven_is_ext_bass(int n) { return n == 5 || n == 6; }
// The counts from the manifest keys: guitar 6/7/8 (Rs7String6, and Rs7String7), bass 4/5/6 (Rs7String4, and Rs7String5)
static inline int seven_guitar_desired(int has6, int has7) { return !has6 ? 6 : has7 ? 8 : 7; }
static inline int seven_bass_desired(int has4, int has5) { return !has4 ? 4 : has5 ? 6 : 5; }

// The manifest keys of the unit's internal form (seven_native.c writes them; rs7string's RsxrStringN)
#define SEVEN_KEY_G6 "Rs7String6"
#define SEVEN_KEY_G7 "Rs7String7"
#define SEVEN_KEY_B4 "Rs7String4"
#define SEVEN_KEY_B5 "Rs7String5"

// The open MIDI note of each storage slot at standard tuning
extern const int8_t seven_guitar_open[8];  // 40,45,50,55,59,64,35,30
extern const int8_t seven_bass_open[6];    // 28,33,38,43,23,48
// Native (positional, thickest first) string i of an n-string instrument -> the storage slot; -1 for a bad argument
// (NATIVE-FORMAT: guitar 7: 0 -> 6, 1-6 -> 0-5; guitar 8: 0 -> 7, 1 -> 6, 2-7 -> 0-5; bass 5: 0 -> 4, 1-4 -> 0-3;
// bass 6: the same and 5 -> 5)
int seven_slot_of(int instrument, int n, int i);
// Storage slot -> pitch rank (0 = lowest) on an n-string instrument; -1 for a bad argument (the inverse of
// seven_slot_of, whose order the tuner follows; the finger markers of a 5/6-string bass don't use it yet: README).
int seven_rank_of(int instrument, int n, int slot);

// The counts each player's instrument is built with (C-lite). The strings part writes them where the game builds
// the instrument; others read them. A player out of range reads 6 / 4 and is not written.
int seven_desired_guitar(int player);  // 6, 7, 8
int seven_desired_bass(int player);    // 4, 5, 6
void seven_set_desired(int player, int guitar, int bass);

// Per-detector extended-bass setups (rsxrbass::Players), written by the pitch part where a song is set up, read by
// everyone. A setup more than 5000 ms older than a new one belongs to an earlier song and is dropped. `instrument`
// is the detector's (2 = bass), `strings` 5 / 6 for an extended bass, else 0.
void seven_bass_setup(const void *det, int instrument, int strings, uint32_t ms);
// The clock (rsm_now's seconds, from the machine's start) as the setups' ms: modulo 2^32, so it wraps after 49.7 days
// as the age test (ms - then, unsigned) expects, where a plain conversion of a larger number is undefined
static inline uint32_t seven_ms_of(double seconds) {
  return seconds >= 0 && seconds < 1.8e16 ? (uint32_t)(uint64_t)(seconds * 1000.0) : 0;
}
void seven_bass_clear(void);                  // forgets every setup (a test; a game restart)
int seven_bass_strings_of(const void *det);   // 0 / 5 / 6
int seven_bass_max_strings(void);             // the largest of the song's players (lock-free read)
int seven_bass_any_guitar(void);              // a guitar player in this song (then no low B in slot 4)
uint32_t seven_bass_serial(void);             // changes whenever the above change (colours and tuner poll it)

// The optional features of wave 2: a part switches its feature on when all its points were taken, and the callbacks
// of its points check it first (a point registered cannot be unregistered)
enum seven_feature {
  SEVEN_F_EIGHT,
  SEVEN_F_BASS,
  SEVEN_F_COOP,
  SEVEN_F_SPACING,
  SEVEN_F_ARRANGEMENTS,
  SEVEN_F_COUNT
};
void seven_feature_set(enum seven_feature f, int on);
int seven_feature(enum seven_feature f);  // 0 before and without it

// ------------------------------------------------------------------------------------------------
// Settings ([Seven String] in RSMods.ini; rs7string's names in brackets)
// ------------------------------------------------------------------------------------------------
typedef struct {
  float spacing_guitar7, spacing_guitar8, spacing_bass5, spacing_bass6;  // 0 = stock ([neck] spacing_*)
  int string7_line, diagrams7, lowrange;  // 0/1; defaults 1 ([neck] string7_line, [chords] diagrams7, [tuner] lowrange)
  uint32_t string7_rgb[2];                // [0] string7_N, [1] string7_CB (falls back to N); default 0xFF66CC
  int string7_set[2];                     // whether the key was present (RSMods Extended Range rules may override)
} seven_settings;

// The settings, read on the first call and again when RSMods.ini changed (checked at most every half second, from
// whichever thread asks). The pointer stays valid; its contents are replaced whole between two calls.
const seven_settings *seven_settings_get(void);
unsigned seven_settings_serial(void);  // changes whenever a value did
// The matching spacing_* value for an instrument with n strings, 0 for stock counts
float seven_spacing_for(int instrument, int n);

// The parsing alone (seven_model.c, no ini): `get` gives the raw value of a key or NULL; `bad` (may be NULL) is called
// for a value that isn't valid (the setting then has its default)
typedef const char *(*seven_get_fn)(const char *key, void *user);
typedef void (*seven_bad_fn)(const char *key, const char *value, void *user);
void seven_settings_parse(seven_settings *s, seven_get_fn get, seven_bad_fn bad, void *user);
// RRGGBB, "#" before it allowed, blanks around it: 1 and *rgb, or 0
int seven_parse_rgb(const char *s, uint32_t *rgb);
// A spacing: empty is 0 (stock), else a number 0.5 to 4.0 (or exactly 0): 1 and *v, or 0
int seven_parse_spacing(const char *s, float *v);
#endif
