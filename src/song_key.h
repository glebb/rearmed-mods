// Nashville Numbers: a song's key, guessed from its notes, and its chords as numerals of that key. Without the game
// (tests/test_song_key.c).
//
// Rocksmith songs don't say their key, so it is guessed: the notes of all of a song's arrangements as one pitch-class
// histogram (each note weighted 0.3 + its sustain, at most 2 s; a chord's notes each divided by the square root of their
// count), matched (correlation) against the Krumhansl-Kessler profiles of the major and minor keys and of the
// mixolydian and dorian modes (the major profile with its 7th and flat 7th swapped, the minor one with its flat 6th
// and 6th), plus two cues for the tonic: 0.2 x the lowest note of each arrangement's first second and its first
// chord's root, 0.1 x its last chord's root. Checked against 50 songs on disc whose key is known (2026-11-20): the tonic
// right for 44, in the first two candidates for 47.
#ifndef SONG_KEY_H
#define SONG_KEY_H

#include <stddef.h>

enum { SK_MAJOR, SK_MINOR, SK_MIXOLYDIAN, SK_DORIAN };

typedef struct {
  int tonic;  // pitch class, 0 = C
  int mode;   // SK_MAJOR ... SK_DORIAN
  double score;
} sk_key;

static inline int sk_minor(int mode) { return mode == SK_MINOR || mode == SK_DORIAN; }

// One thing played: a note, or a chord's notes (MIDI, any order)
enum { SK_MAX_NOTES = 8 };
typedef struct {
  float time, sustain;
  int n;
  int midi[SK_MAX_NOTES];
  int chord;  // the chart's chord template, -1 for a single note
} sk_event;

// A song's notes, summed over its arrangements
typedef struct {
  double hist[12], first[12], first_chord[12], last_chord[12];
  int arrangements, with_chords;
} sk_stats;

void sk_stats_init(sk_stats *s);
// One arrangement's events, in time order
void sk_stats_add(sk_stats *s, const sk_event *ev, int n);
// The candidates, best first, one per tonic: how many (up to max), 0 for a song without notes
int sk_guess(const sk_stats *s, sk_key *out, int max);

// A chord from its notes: its root, its quality ("" major, "m", "5", "dim", "aug", "sus2", "sus4", "7", "maj7",
// "m7", "6", "m6", "add9", "madd9", "m7b5", "dim7", "9", "m9", "maj9", "7sus4"; "1" a single pitch class) and its
// lowest note's pitch class
typedef struct {
  int root, bass;
  char quality[8];
} sk_chord;
int sk_chord_of(const int *midi, int n, sk_chord *out);  // 0 for no notes
// 'M' major, 'm' minor, 'd' diminished, 'p' neither (power chords, sus, single notes)
char sk_chord_kind(const char *quality);

// A chord as a numeral of the key. roman: "IV", "vi", "♭VII", "ii7", "vii°", "V/3"; else Nashville numbers: "4",
// "6m", "♭7", "2m7", "7°", "5/3". minor_from_relative: a minor key is counted from its relative major (Am: 6m) and not
// from its own tonic (1m). simple: as a Nashville chart writes it, the chord's job and not its voicing: no power
// chord's 5, sus2, add9, 6, maj7 or 9 (a 9 as its 7), sus4 as sus.
void sk_numeral(const sk_chord *c, const sk_key *k, int roman, int minor_from_relative, int simple, char *out, size_t n);
// "D", "F#m", "B♭"; and back from "D", "f#m", "Bb minor", "E♭": 1, or 0
void sk_key_name(const sk_key *k, char *out, size_t n);
int sk_key_parse(const char *s, sk_key *out);

#endif
