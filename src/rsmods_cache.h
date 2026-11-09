// What Custom Tunings and the default tones change in the game's files (manifests/tuning.database.json and
// manifests/tonemanager.database.json of cache7.7z), and the tones of a profile they take. Text in, text out: nothing
// here touches the game (the tests use it as it is; mod_cache.c puts the files in the game's cache7.7z).
#ifndef RS_RSMODS_CACHE_H
#define RS_RSMODS_CACHE_H

#include <stddef.h>

// ------------------------------------------------------------------------------------------------
// Custom Tunings: tuning.database.json's TuningDefinitions name the tunings the game knows ("$[17874]E Standard", the
// six strings' semitones from E standard). The game shows that name for a song's arrangement whose strings are those,
// and CUSTOM TUNING for any other.
// ------------------------------------------------------------------------------------------------
typedef struct {
  char name[96];
  int strings[6];  // low string first
  int id;          // its row in the game's localization (rsm_localization_add), 0 for none
} rsm_tuning;

// A [Custom Tunings] line: the name, and six numbers (spaces or commas between). 1 if it is one.
int rsm_tuning_parse(const char *name, const char *value, rsm_tuning *out);
// tuning.database.json with these tunings added, named "$[<id>]<name>" (the game looks the name up by the id in its
// localization: "$[-1]" without one, which only the tuner's list shows as the text); a tuning of the game's with the
// same strings, or the same key, gives way to it, as does a later one of these with the same strings or name.
// malloc'ed; NULL if the file isn't as expected.
char *rsm_tunings_edit(const char *json, const rsm_tuning *t, int n);
// The game's localization (localization/maingame.csv: "id,English,French,Spanish,Italian,German,Japanese,...,", a
// line a text, CRLF) with a line for each tuning, its name in every language, under an id the file hasn't from 37500
// on (as RSMods' app picks them), which goes in t[i].id. malloc'ed; NULL without memory.
char *rsm_localization_add(const char *csv, size_t len, rsm_tuning *t, int n, size_t *out_len);

// ------------------------------------------------------------------------------------------------
// Default tones: tonemanager.database.json's ToneManager.Tones, each with a Key (ToneDefault, ToneDefault_Lead,
// ToneDefault_Bass, Guitarcade's gc_*), a GearList (amp, cabinet, pedals, racks and their knobs) and a Volume.
// ------------------------------------------------------------------------------------------------
// A tone saved in a profile (its CustomTones): pointers into the profile's JSON
typedef struct {
  char name[128];
  const char *gear, *volume;  // the GearList object's and the Volume value's text (NULL: none)
  size_t gear_len, volume_len;
  int bass;  // a bass amp (Bass_...)
} rsm_tone;

// The profile's saved tones, up to max; how many
int rsm_profile_tones(const char *profile_json, rsm_tone *out, int max);

typedef struct {
  const char *key;  // the game's tone (ToneDefault_Lead)
  const rsm_tone *tone;
} rsm_tone_change;

// tonemanager.database.json with the tones' GearList and Volume those of the profile's tones (every tone of a key, if
// the file has one twice). *applied: how many tones it changed. malloc'ed; NULL if the file isn't as expected.
char *rsm_tones_edit(const char *json, const rsm_tone_change *c, int n, int *applied);

#endif
