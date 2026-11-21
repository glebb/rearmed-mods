// A song's harmony from all its parts (song_harmony.h)
#include "song_harmony.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double MAX_RING = 1.0;   // a note without sustain sounds until the next, at most this long
static const double ONSET = 0.5;      // a note that starts in the piece counts this much more
static const double BASS_PART = 2.0;  // a bass part's lowest notes count this many times
static const double OTHER = -0.4, BASS_ROOT = 0.6, IN_KEY = 0.1, SAME = 0.05, NO_THIRD = 0.15, SLASH = 0.5;
static const double SPLIT = 0.15;      // a bar's halves are two chords when they fit this much better than the bar
static const double RIFF_FIRST = 0.3;  // a riff's bar is the chord of the note it starts on, by this much
static const double POWER = 0.85;      // a piece is one power chord when its root and fifth have this share of it

static const struct {
  const char *name;
  int n, iv[4];
  double extra;  // the rarer, a little less
  char kind;     // as the key's chords are told apart
} TEMPLATES[] = {
    {"", 3, {0, 4, 7}, 0, 'M'},           {"m", 3, {0, 3, 7}, 0, 'm'},        {"7", 4, {0, 4, 7, 10}, 0.06, 'M'},
    {"m7", 4, {0, 3, 7, 10}, 0.06, 'm'},  {"dim", 3, {0, 3, 6}, 0.1, 'd'},    {"sus4", 3, {0, 5, 7}, 0.1, 'M'},
};
enum { NT = sizeof TEMPLATES / sizeof *TEMPLATES };

// Is a chord on degree d (from the tonic), of kind k, one of the key's
static int in_key(int d, char k, int minor) {
  static const char MAJOR[12] = {'M', 0, 'm', 0, 'm', 'M', 0, 'M', 0, 'm', 'M', 'd'};  // with the flat 7th
  static const char MINOR[12] = {'m', 0, 'd', 'M', 0, 'm', 0, 'm', 'M', 0, 'M', 0};
  return minor ? MINOR[d] == k || (d == 7 && k == 'M') : MAJOR[d] == k;
}

typedef struct {
  float start, end;
  int lowest;
} ringing;

// A part's notes with how long each sounds, and the longest
static ringing *rings_of(const sh_part *p, double *longest) {
  ringing *r = malloc((p->n ? p->n : 1) * sizeof *r);
  *longest = 0;
  for (int i = 0, next = 0; r && i < p->n; i++) {
    const sk_event *e = &p->ev[i];
    if (next <= i) next = i + 1;
    while (next < p->n && p->ev[next].time <= e->time + 1e-3) next++;
    double gap = next < p->n ? p->ev[next].time - e->time : MAX_RING;
    double len = fmax(e->sustain, fmin(gap, MAX_RING));
    int lo = e->n ? e->midi[0] : 0;
    for (int j = 1; j < e->n; j++)
      if (e->midi[j] < lo) lo = e->midi[j];
    r[i] = (ringing){e->time, (float)(e->time + len), lo};
    if (len > *longest) *longest = len;
  }
  return r;
}

static int pc(int midi) { return ((midi % 12) + 12) % 12; }

typedef struct {
  float s0, s1;
  double h[12], b[12];  // the notes and the bass, by the share of the piece they sound in
  int root, bass;
  int chords;           // a chord of three notes or more is struck or rings in it; else a riff's notes
  int first;            // the lowest note struck as it starts, -1 none
} piece;

// The notes that sound from s0 to s1
static void gather(const sh_part *parts, int nparts, ringing **rings, const double *longest, float s0, float s1, piece *pc_) {
  memset(pc_, 0, sizeof *pc_);
  pc_->s0 = s0, pc_->s1 = s1, pc_->first = -1;
  int first_midi = 1000;
  double L = s1 - s0;
  if (L <= 0) return;
  for (int p = 0; p < nparts; p++) {
    const sk_event *ev = parts[p].ev;
    int lo = 0, hi = parts[p].n;  // the first note that may still sound at s0
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (ev[mid].time < s0 - longest[p] - 1e-3) lo = mid + 1;
      else hi = mid;
    }
    for (int k = lo; k < parts[p].n && ev[k].time < s1; k++) {
      double ov = fmin(rings[p][k].end, s1) - fmax(rings[p][k].start, s0);
      if (ov <= 0) continue;
      double w = ov / L * (ev[k].time >= s0 - 0.03 ? 1 + ONSET : 1);
      int set = 0;
      for (int m = 0; m < ev[k].n; m++) pc_->h[pc(ev[k].midi[m])] += w, set |= 1 << pc(ev[k].midi[m]);
      pc_->b[pc(rings[p][k].lowest)] += w * (parts[p].bass ? BASS_PART : 1);
      if (__builtin_popcount(set) >= 3) pc_->chords = 1;
      if (fabs(ev[k].time - s0) < 0.06 && rings[p][k].lowest < first_midi) first_midi = rings[p][k].lowest;
    }
  }
  if (first_midi < 1000) pc_->first = pc(first_midi);
}

static double sum12(const double *x) {
  double s = 0;
  for (int k = 0; k < 12; k++) s += x[k];
  return s;
}

// How well template t on root r fits the notes h
static double fit(const double *h, double sh, int r, int t, const sk_key *key) {
  int set = 0;
  for (int k = 0; k < TEMPLATES[t].n; k++) set |= 1 << (r + TEMPLATES[t].iv[k]) % 12;
  double sc = 0;
  for (int k = 0; k < 12; k++) sc += h[k] * (set & 1 << k ? 1 : OTHER);
  sc = sc / sh - TEMPLATES[t].extra;
  if (key && in_key((r - key->tonic + 12) % 12, TEMPLATES[t].kind, sk_minor(key->mode))) sc += IN_KEY;
  return sc;
}

// The piece's root: the best fit of any template, with the bass on it and the root before; its score, or -1e9 when
// nothing plays
static double choose_root(piece *x, const sk_key *key, int prev) {
  double sh = sum12(x->h), sb = sum12(x->b), best = -1e9;
  if (sh < 0.05) return x->root = -1, best;
  for (int r = 0; r < 12; r++)
    for (int t = 0; t < NT; t++) {
      if (!x->chords && t > 1) continue;  // a riff: no 7ths or sus from its notes
      double sc = fit(x->h, sh, r, t, key) + (sb > 0 ? BASS_ROOT * x->b[r] / sb : 0) + (r == prev ? SAME : 0) +
                  (!x->chords && r == x->first ? RIFF_FIRST : 0);  // a riff: the note it starts the bar on
      if (sc > best) best = sc, x->root = r;
    }
  int low = 0;
  for (int k = 1; k < 12; k++)
    if (x->b[k] > x->b[low]) low = k;
  // a bass held on another note than the root, but not the fifth: a bass that goes root, fifth, root is no slash
  x->bass = sb > 0 && low != x->root && (low - x->root + 12) % 12 != 7 && x->b[low] / sb >= SLASH ? low : x->root;
  return best;
}

// The root of the one power chord the piece is (its root and fifth nearly all that sounds), or -1
static int power_chord(const piece *x) {
  double sh = sum12(x->h);
  for (int r = 0; r < 12 && sh > 0; r++)
    if ((x->h[r] + x->h[(r + 7) % 12]) / sh >= POWER && x->h[r] >= x->h[(r + 7) % 12] * 0.5) return r;
  return -1;
}

int sh_infer(const sh_part *parts, int nparts, const sng_beat *beats, int nbeats, const sk_key *key, sh_chord **out) {
  *out = NULL;
  if (nbeats < 2 || nparts < 1) return 0;
  ringing **rings = calloc((size_t)nparts, sizeof *rings);
  double *longest = calloc((size_t)nparts, sizeof *longest);
  int cap = 64, n = 0, npieces = 0, pcap = 64;
  sh_chord *chords = malloc((size_t)cap * sizeof *chords);
  piece *pieces = malloc((size_t)pcap * sizeof *pieces);
  if (!rings || !longest || !chords || !pieces) goto done;
  for (int p = 0; p < nparts; p++)
    if (!(rings[p] = rings_of(&parts[p], &longest[p]))) goto done;
  // the roots: a bar's, or each half's where its halves clearly hold two chords
  int prev = -1;
  for (int i = 0; i < nbeats; i++) {
    if (beats[i].beat != 0) continue;
    int j = i + 1;
    while (j < nbeats && beats[j].beat != 0) j++;
    int in_bar = j - i;
    float bar_end = j < nbeats ? beats[j].time : beats[j - 1].time + (in_bar > 1 ? beats[j - 1].time - beats[j - 2].time : 0.5f);
    piece whole, half[2];
    gather(parts, nparts, rings, longest, beats[i].time, bar_end, &whole);
    double sw = choose_root(&whole, key, prev);
    int use_halves = 0;
    if (in_bar >= 4 && whole.root >= 0) {
      float mid = beats[i + in_bar / 2].time;
      gather(parts, nparts, rings, longest, beats[i].time, mid, &half[0]);
      gather(parts, nparts, rings, longest, mid, bar_end, &half[1]);
      double s0 = choose_root(&half[0], key, prev), s1 = choose_root(&half[1], key, half[0].root);
      if (whole.chords)
        use_halves = half[0].root >= 0 && half[1].root >= 0 && half[0].root != half[1].root && (s0 + s1) / 2 - sw > SPLIT;
      else {  // a riff's bar: two only where its halves are two power chords, as G5 then F5
        int p0 = power_chord(&half[0]), p1 = power_chord(&half[1]);
        if ((use_halves = p0 >= 0 && p1 >= 0 && p0 != p1)) half[0].root = p0, half[1].root = p1;
      }
    }
    for (int k = 0; k < (use_halves ? 2 : 1); k++) {
      piece *x = use_halves ? &half[k] : &whole;
      prev = x->root;
      if (x->root < 0) continue;
      if (npieces == pcap) {
        piece *more = realloc(pieces, (size_t)(pcap *= 2) * sizeof *pieces);
        if (!more) goto done;
        pieces = more;
      }
      pieces[npieces++] = *x;
    }
  }
  // the runs of one root (and bass), and each run's quality from all of it
  for (int i = 0; i < npieces;) {
    int j = i + 1;
    while (j < npieces && pieces[j].root == pieces[i].root && pieces[j].bass == pieces[i].bass && pieces[j].s0 <= pieces[j - 1].s1 + 1e-3) j++;
    double h[12] = {0}, top = 0;
    int struck = 0;
    for (int k = i; k < j; k++) struck |= pieces[k].chords;
    for (int k = i; k < j; k++)
      for (int m = 0; m < 12; m++) h[m] += pieces[k].h[m] * (pieces[k].s1 - pieces[k].s0);
    for (int m = 0; m < 12; m++)
      if (h[m] > top) top = h[m];
    int r = pieces[i].root, q = 0;
    double best = -1e9, sh = sum12(h);
    for (int t = 0; t < (struck ? NT : 2); t++) {  // a riff's: major or minor
      double sc = fit(h, sh, r, t, key);
      if (sc > best) best = sc, q = t;
    }
    sh_chord c = {pieces[i].s0, pieces[j - 1].s1, {r, pieces[i].bass, ""}};
    double third = fmax(h[(r + 3) % 12], h[(r + 4) % 12]);
    snprintf(c.chord.quality, sizeof c.chord.quality, "%s", q <= 1 && third < NO_THIRD * top ? "5" : TEMPLATES[q].name);
    if (n == cap) {
      sh_chord *more = realloc(chords, (size_t)(cap *= 2) * sizeof *chords);
      if (!more) goto done;
      chords = more;
    }
    chords[n++] = c;
    i = j;
  }
done:
  for (int p = 0; rings && p < nparts; p++) free(rings[p]);
  free(rings), free(longest), free(pieces);
  if (!n) free(chords), chords = NULL;
  *out = chords;
  return n;
}
