// A song's tunings, from its archive: the PSARC reader, the manifests' tunings, and RSMods' Extended Range rule.
// Nothing here touches the game (rsmods_tuning.c): the tests use it as it is.
#ifndef RS_RSMODS_TUNING_H
#define RS_RSMODS_TUNING_H

#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------------------------------------------
// PSARC, the game's archives (songs.psarc, dlc/*.psarc): a table of contents, AES-256-CFB with the game's key and a
// zero IV when the archive's flags say so (4), then the files in zlib blocks. The first file is the list of the other
// files' names.
// ------------------------------------------------------------------------------------------------
typedef struct rsm_psarc rsm_psarc;
rsm_psarc *rsm_psarc_open(const char *path, const uint8_t key[32]);  // NULL if it isn't one, or can't be read
int rsm_psarc_files(const rsm_psarc *a);                             // the named files
const char *rsm_psarc_name(const rsm_psarc *a, int i);               // file i's name (from 0)
void *rsm_psarc_read(rsm_psarc *a, int i, size_t *len);              // file i, malloc'ed and NUL-terminated; NULL
void rsm_psarc_close(rsm_psarc *a);
// The game's key, as it has it: 64 hex digits
int rsm_psarc_key_from_hex(const char *hex, uint8_t key[32]);

// ------------------------------------------------------------------------------------------------
// An arrangement's manifest (manifests/<dir>/<songkey>_<arrangement>.json in the song's archive)
// ------------------------------------------------------------------------------------------------
typedef struct {
  char name[16];   // ArrangementName: Lead, Rhythm, Combo, Bass (Vocals and others have no tuning)
  int tuning[6];   // semitones from E standard, low string first
  double cents;    // CentOffset: the true tuning is 440 * 2^(cents / 1200)
} rsm_arrangement;

// 1 if the manifest has an arrangement with a tuning
int rsm_manifest_arrangement(const char *json, rsm_arrangement *out);
// 1 if `name` is a manifest of song_key (any case): manifests/.../<song_key>_*.json
int rsm_is_song_manifest(const char *name, const char *song_key);
int rsm_is_bass(const rsm_arrangement *a);

// ------------------------------------------------------------------------------------------------
// RSMods' Extended Range rule (SongTuning.cpp, IsExtendedRangeSong): the lowest string (of the first four with
// fix_bass and the last two left at E or an octave up) at or below `at` semitones ([Mod Settings]
// ExtendedRangeModeAt, -5: B), an octave lower for a true tuning at or below 260 Hz (a bass an octave down). A drop
// tuning counts from two lower down, or from `at` itself with drop_tunings (ExtendedRangeDropTuning).
// ------------------------------------------------------------------------------------------------
int rsm_extended_range_rule(const rsm_arrangement *a, long at, int drop_tunings, int fix_bass);

#endif
