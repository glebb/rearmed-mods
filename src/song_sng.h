// A song's notes from its archive, for Nashville Numbers: each instrument arrangement's SNG (songs/bin/macos/ in
// the Mac's archives, songs/bin/generic/ in Learn & Play's), decrypted and read. Without the game
// (tests/test_song_key.c).
//
// An SNG file: u32 0x4a, u32 flags, a 16-byte IV, then AES-256-CTR (the counter big-endian) with the platform's
// key, giving a u32 length and zlib (flags 1, an unencrypted file as DotCharter writes them: that as it is). The
// plain file is the toolkit's Sng2014File: the beats (16 bytes: float time, int16 bar, int16 beat in the bar), the
// phrases (44 bytes: MaxDifficulty +4), the chord templates (72 bytes: int32 midi[6] +16, char name[32] +40), the
// chord notes (2376), the vocals (60), the phrase iterations (24: phrase +0), extra info (16), linked difficulties, actions (260), events (260), tone
// changes (8), DNA (8), sections (88), then the levels: each its difficulty, anchors (28), anchor extensions (12),
// two fingerprint lists (20), its notes (63 bytes + u32 bends + 12 bytes each: time +12, string +16, fret +17,
// chord +20, phrase iteration +32, sustain +55) and three int lists; then the metadata (capo +40, string count
// +79, then int16 tuning[]). The whole chart is, for each phrase iteration, the notes of the level that is its
// phrase's MaxDifficulty. A 7- or 8-string guitar (seven_chords.h) has its 7th and 8th strings as note strings 6 and 7
// (open B1 and F#1, the MetaData tuning's entries 6 and 7) and their chord frets in the RS7X block after MetaData
// (CHDT), or for an older 7-string package in a template's name[31]; an extended bass's low B and high C are the stock
// slots 4 and 5.
#ifndef SONG_SNG_H
#define SONG_SNG_H

#include <stddef.h>
#include <stdint.h>

#include "rsmods_tuning.h"
#include "song_key.h"

typedef struct {
  float time;
  int bar, beat;  // the bar's number, and the beat's in it (0: the bar's first)
} sng_beat;

typedef struct {
  char name[16];      // the manifest's ArrangementName
  rsm_arrangement manifest;  // and its tuning (sng_read_song's; nothing from sng_parse)
  int bass;           // a bass arrangement
  int levels;
  int *level_notes;   // each level's notes, all of them: what the loaded song is matched by
  sk_event *ev;       // the whole chart, in time order
  int n;
  sng_beat *beats;    // the beat grid, in time order
  int nbeats;
  char (*chord_names)[33];  // each chord template's name as the chart has it ("" none)
  int nchords;
} sng_arrangement;

// The plain file of an SNG: malloc'ed, or NULL. mac: the Mac's key, else the PC's (Learn & Play)
uint8_t *sng_decrypt(const uint8_t *file, size_t n, int mac, size_t *out_n);
// An arrangement from its plain SNG: 1, or 0 when it isn't one that can be read (vocals). bass: its strings are a
// bass's; cents: the manifest's CentOffset, in whole semitones added to every note.
int sng_parse(const uint8_t *d, size_t n, int bass, double cents, sng_arrangement *out);
void sng_free(sng_arrangement *a);

// The instrument arrangements of song_key (any case) in an archive: how many (malloc'ed in *out), 0 for none
int sng_read_song(const char *archive, const char *song_key, const uint8_t key[32], const rsm_psarc_io *io, sng_arrangement **out);
void sng_free_all(sng_arrangement *a, int n);

#endif
