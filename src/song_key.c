// A song's key and its chords as numerals (song_key.h)
#include "song_key.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define FLAT "\xe2\x99\xad"  // ♭
#define DEG "\xc2\xb0"       // °
#define HALFDIM "\xc3\xb8"   // ø

// Krumhansl-Kessler
static const double KK_MAJOR[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
static const double KK_MINOR[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
enum { FIRST_SECONDS = 1 };
static const double NOTE_WEIGHT = 0.3, SUSTAIN_CAP = 2.0, FIRST_CUE = 0.2, LAST_CUE = 0.1;

void sk_stats_init(sk_stats *s) { memset(s, 0, sizeof *s); }

static int lowest(const sk_event *e) {
  int lo = e->midi[0];
  for (int i = 1; i < e->n; i++)
    if (e->midi[i] < lo) lo = e->midi[i];
  return lo;
}

static int pc(int midi) { return ((midi % 12) + 12) % 12; }

void sk_stats_add(sk_stats *s, const sk_event *ev, int n) {
  double h[12] = {0}, sum = 0;
  int first_chord = -1, last_chord = -1;
  for (int i = 0; i < n; i++) {
    const sk_event *e = &ev[i];
    if (e->n < 1) continue;
    double w = NOTE_WEIGHT + fmin(e->sustain > 0 ? e->sustain : 0, SUSTAIN_CAP), each = w / sqrt(e->n);
    for (int j = 0; j < e->n; j++) h[pc(e->midi[j])] += each, sum += each;
    if (e->n > 1) {
      sk_chord c;
      if (sk_chord_of(e->midi, e->n, &c)) {
        if (first_chord < 0) first_chord = c.root;
        last_chord = c.root;
      }
    }
  }
  if (sum <= 0) return;
  for (int i = 0; i < 12; i++) s->hist[i] += h[i] / sum;
  // the lowest note of the first second
  int lo = -1;
  float t0 = 0;
  for (int i = 0; i < n; i++) {
    if (ev[i].n < 1) continue;
    if (lo < 0) t0 = ev[i].time;
    else if (ev[i].time > t0 + FIRST_SECONDS) break;
    int l = lowest(&ev[i]);
    if (lo < 0 || l < lo) lo = l;
  }
  if (lo >= 0) s->first[pc(lo)] += 1;
  if (first_chord >= 0) {
    s->first_chord[first_chord] += 1;
    s->last_chord[last_chord] += 1;
    s->with_chords++;
  }
  s->arrangements++;
}

static double corr(const double *x, const double *y) {
  double mx = 0, my = 0, num = 0, dx = 0, dy = 0;
  for (int i = 0; i < 12; i++) mx += x[i], my += y[i];
  mx /= 12, my /= 12;
  for (int i = 0; i < 12; i++) num += (x[i] - mx) * (y[i] - my), dx += (x[i] - mx) * (x[i] - mx), dy += (y[i] - my) * (y[i] - my);
  return dx > 0 && dy > 0 ? num / sqrt(dx * dy) : 0;
}

static void profile(int mode, int tonic, double *out) {
  double p[12];
  memcpy(p, mode == SK_MAJOR || mode == SK_MIXOLYDIAN ? KK_MAJOR : KK_MINOR, sizeof p);
  if (mode == SK_MIXOLYDIAN) {
    double t = p[10];
    p[10] = p[11], p[11] = t;
  } else if (mode == SK_DORIAN) {
    double t = p[8];
    p[8] = p[9], p[9] = t;
  }
  for (int i = 0; i < 12; i++) out[i] = p[((i - tonic) % 12 + 12) % 12];
}

int sk_guess(const sk_stats *s, sk_key *out, int max) {
  if (!s->arrangements || max < 1) return 0;
  double nf = 0, ncf = 0, ncl = 0;
  for (int i = 0; i < 12; i++) nf += s->first[i], ncf += s->first_chord[i], ncl += s->last_chord[i];
  sk_key best[12];  // the best mode of each tonic
  for (int k = 0; k < 12; k++) {
    best[k] = (sk_key){k, SK_MAJOR, -1e9};
    double cue = s->with_chords ? (s->first[k] / (nf ? nf : 1) + s->first_chord[k] / ncf) / 2 : s->first[k] / (nf ? nf : 1);
    double last = s->with_chords ? s->last_chord[k] / ncl : 0;
    for (int m = SK_MAJOR; m <= SK_DORIAN; m++) {
      double p[12];
      profile(m, k, p);
      double sc = corr(s->hist, p) + FIRST_CUE * cue + LAST_CUE * last;
      if (sc > best[k].score) best[k] = (sk_key){k, m, sc};
    }
  }
  int n = 0;
  for (; n < max && n < 12; n++) {
    int b = -1;
    for (int k = 0; k < 12; k++) {
      int used = 0;
      for (int j = 0; j < n; j++) used |= out[j].tonic == k;
      if (!used && (b < 0 || best[k].score > best[b].score)) b = k;
    }
    out[n] = best[b];
  }
  return n;
}

// ---- Chords ----
static const struct {
  const char *name;
  int n, iv[5];
} QUALITIES[] = {
    {"", 3, {0, 4, 7}},          {"m", 3, {0, 3, 7}},          {"5", 2, {0, 7}},           {"dim", 3, {0, 3, 6}},
    {"aug", 3, {0, 4, 8}},       {"sus2", 3, {0, 2, 7}},       {"sus4", 3, {0, 5, 7}},     {"7", 4, {0, 4, 7, 10}},
    {"maj7", 4, {0, 4, 7, 11}},  {"m7", 4, {0, 3, 7, 10}},     {"6", 4, {0, 4, 7, 9}},     {"m6", 4, {0, 3, 7, 9}},
    {"add9", 4, {0, 2, 4, 7}},   {"madd9", 4, {0, 2, 3, 7}},   {"m7b5", 4, {0, 3, 6, 10}}, {"dim7", 4, {0, 3, 6, 9}},
    {"9", 5, {0, 2, 4, 7, 10}},  {"m9", 5, {0, 2, 3, 7, 10}},  {"maj9", 5, {0, 2, 4, 7, 11}},
    {"7sus4", 4, {0, 5, 7, 10}},
};

int sk_chord_of(const int *midi, int n, sk_chord *out) {
  if (n < 1) return 0;
  int lo = midi[0], set = 0;
  for (int i = 0; i < n; i++) {
    if (midi[i] < lo) lo = midi[i];
    set |= 1 << pc(midi[i]);
  }
  int bass = pc(lo), count = __builtin_popcount(set);
  out->bass = bass;
  if (count == 1) return out->root = bass, snprintf(out->quality, sizeof out->quality, "1"), 1;
  if (count == 2) {  // a power chord, or one turned over (its fifth below: a fourth up)
    int a = __builtin_ctz(set), b = 31 - __builtin_clz(set);
    if (b - a == 7) return out->root = a, snprintf(out->quality, sizeof out->quality, "5"), 1;
    if (b - a == 5) return out->root = b, snprintf(out->quality, sizeof out->quality, "5"), 1;
  }
  double best = 0;
  int found = 0;
  for (int r = 0; r < 12; r++) {
    if (!(set & 1 << r)) continue;
    int iv = 0;  // the notes as intervals from r
    for (int p = 0; p < 12; p++)
      if (set & 1 << p) iv |= 1 << ((p - r + 12) % 12);
    for (size_t q = 0; q < sizeof QUALITIES / sizeof *QUALITIES; q++) {
      int ts = 0;
      for (int i = 0; i < QUALITIES[q].n; i++) ts |= 1 << QUALITIES[q].iv[i];
      int have = __builtin_popcount(iv & ts), miss = __builtin_popcount(ts & ~iv), extra = __builtin_popcount(iv & ~ts);
      if ((ts & 1 << 4) && !(iv & 1 << 4) && !(ts & 1 << 3)) miss++;  // the third makes it major
      if (have < 2) continue;
      int no_fifth = (ts & 1 << 7) && !(iv & 1 << 7), m2 = miss - no_fifth;  // a missing fifth is allowed
      const char *nm = QUALITIES[q].name;
      double sc = have * 2 - m2 * 3 - (no_fifth ? 0.7 : 0) - extra * 3 + (r == bass ? 1.5 : 0) - 0.3 * QUALITIES[q].n -
                  (!strcmp(nm, "6") || !strcmp(nm, "m6") ? 0.8 : 0) - (!strcmp(nm, "sus2") || !strcmp(nm, "sus4") ? 0.5 : 0);
      if (!found || sc > best) {
        best = sc, found = 1;
        out->root = r;
        snprintf(out->quality, sizeof out->quality, "%s", nm);
      }
    }
  }
  if (!found) out->root = bass, snprintf(out->quality, sizeof out->quality, "?");
  return 1;
}

char sk_chord_kind(const char *q) {
  static const char *const neither[] = {"5", "1", "sus2", "sus4", "7sus4", "?"};
  for (size_t i = 0; i < sizeof neither / sizeof *neither; i++)
    if (!strcmp(q, neither[i])) return 'p';
  if (!strcmp(q, "dim") || !strcmp(q, "m7b5") || !strcmp(q, "dim7")) return 'd';
  if (q[0] == 'm' && strncmp(q, "maj", 3)) return 'm';
  return 'M';
}

// ---- Numerals ----
static const char *const DEGREE[12] = {"1", FLAT "2", "2", FLAT "3", "3", "4", FLAT "5", "5", FLAT "6", "6", FLAT "7", "7"};
static const char *const ROMAN[12] = {"I", FLAT "II", "II", FLAT "III", "III", "IV", FLAT "V", "V", FLAT "VI", "VI", FLAT "VII", "VII"};

// A quality as a chart writes it: what says the chord's job
static const char *simple_quality(const char *q) {
  static const char *const MAP[][2] = {{"5", "1"},     {"sus2", "1"},  {"?", "1"},   {"9", "7"},     {"maj7", ""},
                                       {"maj9", ""},   {"6", ""},      {"add9", ""}, {"m9", "m7"},   {"m6", "m"},
                                       {"madd9", "m"}, {"sus4", "sus"}, {"7sus4", "7sus"}};
  for (size_t i = 0; i < sizeof MAP / sizeof *MAP; i++)
    if (!strcmp(q, MAP[i][0])) return MAP[i][1];
  return q;
}

void sk_numeral(const sk_chord *c, const sk_key *k, int roman, int minor_from_relative, int simple, char *out, size_t n) {
  int ref = k->tonic + (sk_minor(k->mode) && minor_from_relative ? 3 : 0);
  int d = ((c->root - ref) % 12 + 12) % 12;
  const char *q = simple ? simple_quality(c->quality) : c->quality;
  char kind = !strcmp(q, "sus") || !strcmp(q, "7sus") ? 'p' : sk_chord_kind(q), head[24], tail[16] = "";
  if (roman) {
    snprintf(head, sizeof head, "%s", ROMAN[d]);
    if (kind == 'm' || kind == 'd')
      for (char *p = head; *p; p++) *p = (char)tolower((unsigned char)*p);
    // the quality without what the case already says
    if (!strcmp(q, "") || !strcmp(q, "m") || !strcmp(q, "1") || !strcmp(q, "?")) *tail = 0;
    else if (!strcmp(q, "dim")) snprintf(tail, sizeof tail, DEG);
    else if (!strcmp(q, "dim7")) snprintf(tail, sizeof tail, DEG "7");
    else if (!strcmp(q, "m7b5")) snprintf(tail, sizeof tail, HALFDIM "7");
    else if (!strcmp(q, "aug")) snprintf(tail, sizeof tail, "+");
    else if (kind == 'm') snprintf(tail, sizeof tail, "%s", q + 1);  // m7 -> ii7
    else snprintf(tail, sizeof tail, "%s", q);
  } else {
    snprintf(head, sizeof head, "%s", DEGREE[d]);
    if (!strcmp(q, "1") || !strcmp(q, "?") || !strcmp(q, "")) *tail = 0;
    else if (!strcmp(q, "dim")) snprintf(tail, sizeof tail, DEG);
    else if (!strcmp(q, "dim7")) snprintf(tail, sizeof tail, DEG "7");
    else if (!strcmp(q, "m7b5")) snprintf(tail, sizeof tail, "m7" FLAT "5");
    else if (!strcmp(q, "aug")) snprintf(tail, sizeof tail, "+");
    else snprintf(tail, sizeof tail, "%s", q);
  }
  if (c->bass != c->root) {
    int b = ((c->bass - ref) % 12 + 12) % 12;
    snprintf(out, n, "%s%s/%s", head, tail, DEGREE[b]);
  } else
    snprintf(out, n, "%s%s", head, tail);
}

static const char *const KEY_NAMES[2][12] = {
    {"C", "D" FLAT, "D", "E" FLAT, "E", "F", "F#", "G", "A" FLAT, "A", "B" FLAT, "B"},
    {"C", "C#", "D", "E" FLAT, "E", "F", "F#", "G", "G#", "A", "B" FLAT, "B"},
};

void sk_key_name(const sk_key *k, char *out, size_t n) {
  int minor = sk_minor(k->mode);
  snprintf(out, n, "%s%s", KEY_NAMES[minor][((k->tonic % 12) + 12) % 12], minor ? "m" : "");
}

int sk_key_parse(const char *s, sk_key *out) {
  static const int LETTER[7] = {9, 11, 0, 2, 4, 5, 7};  // A to G
  while (isspace((unsigned char)*s)) s++;
  int c = toupper((unsigned char)*s);
  if (c < 'A' || c > 'G') return 0;
  int t = LETTER[c - 'A'];
  s++;
  if (*s == '#') t++, s++;
  else if (*s == 'b') t--, s++;
  else if (!strncmp(s, FLAT, 3)) t--, s += 3;
  while (isspace((unsigned char)*s)) s++;
  int minor = 0;
  if (!strncasecmp(s, "minor", 5)) minor = 1, s += 5;
  else if (!strncasecmp(s, "min", 3)) minor = 1, s += 3;
  else if (!strncasecmp(s, "major", 5)) s += 5;
  else if (!strncasecmp(s, "maj", 3)) s += 3;
  else if (*s == 'm') minor = 1, s++;
  while (isspace((unsigned char)*s)) s++;
  if (*s) return 0;
  *out = (sk_key){(t + 12) % 12, minor ? SK_MINOR : SK_MAJOR, 0};
  return 1;
}
