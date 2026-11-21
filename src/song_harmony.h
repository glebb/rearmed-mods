// Nashville Numbers: a song's harmony through the whole song, from the notes of all its arrangements. Without the
// game (tests/test_song_key.c; tests/probe_harmony.c checks it on the songs on disc).
//
// A chart's harmony is simple: a chord a bar, two where it changes in the middle, and a riff is the chord it sits
// on. Each bar of the beat grid is a piece, or its two halves (a bar of four beats or more) where they hold two
// chords that fit 0.15 better than one. In a piece, every note of every part counts by how long it sounds there (its
// sustain, or until the part's next note, at most 1 s), half again for a note that starts in it; the lowest note of
// each thing a part plays counts for the bass, a bass part's twice. The piece's root is that of the template (major,
// minor, 7, m7, dim, sus4) that fits best: its own notes +1, other notes -0.4, plus 0.6 x the share of the bass on its
// root, 0.1 for a chord of the key, 0.05 for the root before (rarer templates a little less). Where no part plays a
// chord of three notes (single notes, power chords, a riff in fourths), the bar is one piece, of the note the bar
// starts on (+0.3), and major or minor only; two where each half is one power chord (its root and fifth 85% of what
// sounds), on two roots. Then the pieces of one root are one chord, whose quality is judged from
// all of them: a major or minor one whose third is hardly heard is a power chord. A bass held on another note than
// its root or fifth is its slash (an inversion, a walk down).
//
// Checked on the songs on disc (2026-11-21): for each guitar part's chords that the chart names or that have three
// notes or more, the harmony of the other parts has their root right for 77%, and 85% counting a change half a bar
// off; it changes every 1.4 bars.
#ifndef SONG_HARMONY_H
#define SONG_HARMONY_H

#include "song_key.h"
#include "song_sng.h"

typedef struct {
  float start, end;  // seconds
  sk_chord chord;    // root, quality ("", "m", "5", "7", "m7", "dim", "sus4"), bass
} sh_chord;

typedef struct {
  const sk_event *ev;  // in time order
  int n;
  int bass;  // a bass part
} sh_part;

// The harmony of the parts over the beat grid: how many chords (malloc'ed in *out), 0 for none. key: the song's,
// for the bonus of its chords (NULL: none).
int sh_infer(const sh_part *parts, int nparts, const sng_beat *beats, int nbeats, const sk_key *key, sh_chord **out);

#endif
