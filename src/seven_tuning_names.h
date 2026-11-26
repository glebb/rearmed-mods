// Seven-String Guitar (seven.h): the names of a 7/8-string tuning, values in and out (seven_names.c is the game side;
// tests/test_seven_names.c). After rs7string's src/features/tuner/tuningname.cpp (github.com/SomeeName/rs7string, MIT,
// by SomeeName). seven_names_build() makes the deeper drops' names once, before the first name7 / name8.
#ifndef RS_SEVEN_TUNING_NAMES_H
#define RS_SEVEN_TUNING_NAMES_H

#include <stdio.h>
#include <string.h>

// ---- the names (values in and out)

// strings 0..5 all at k (B Standard's offsets from E A D G B e: 0), the 7th string at k or k - 2. k = +1 (C) .. -9 (D),
// the owner's list in rs7string's issue #15; flats throughout, as the game's "Eb Standard" and "Eb Drop Db".
typedef struct {
  int k;
  const char *standard, *drop;
} family;

static const family g_families7[] = {
    {+1, "C Standard", "C Drop Bb"},   {0, "B Standard", "Drop A"},       {-1, "Bb Standard", "Bb Drop Ab"},
    {-2, "A Standard", "A Drop G"},    {-3, "Ab Standard", "Ab Drop Gb"}, {-4, "G Standard", "G Drop F"},
    {-5, "Gb Standard", "Gb Drop E"},  {-6, "F Standard", "F Drop Eb"},   {-7, "E Standard", "E Drop D"},
    {-8, "Eb Standard", "Eb Drop Db"}, {-9, "D Standard", "D Drop C"},
};

// Eight strings (the 8th string's base is F#1): the canonical instrument keeps F#, the others flats
static const family g_families8[] = {
    {1, "G Standard", "G Drop F"},     {0, "F# Standard", "Drop E"},      {-1, "F Standard", "F Drop Eb"},
    {-2, "E Standard", "E Drop D"},    {-3, "Eb Standard", "Eb Drop Db"}, {-4, "D Standard", "D Drop C"},
    {-5, "Db Standard", "Db Drop B"},  {-6, "C Standard", "C Drop Bb"},   {-7, "B Standard", "B Drop A"},
    {-8, "Bb Standard", "Bb Drop Ab"}, {-9, "A Standard", "A Drop G"},
};

// Exact offsets (string0..5, Rs7String6; the 7th string's base is B1). Extended Drop D / C / B: a 6-string Drop D,
// D Drop C, C# Drop B with the 7th string a fourth below string 0. Double Drop D and Open G7: rs7string's issue gives
// the notes only; these registers (D2 A2 D3 G3 B3 D4 D4, G1 D2 G2 B2 D3 F3 G3) are rs7string's guess.
static const struct {
  int t[7];
  const char *name;
} g_alternates[] = {
    {{-2, 0, 0, 0, 0, 0, -2}, "Extended Drop D"},      {{-4, -2, -2, -2, -2, -2, -4}, "Extended Drop C"},
    {{-5, -3, -3, -3, -3, -3, -5}, "Extended Drop B"}, {{5, 5, 5, 4, 3, -2, 3}, "Double Drop D"},
    {{-2, -2, -3, -5, -6, -9, -4}, "Open G7"},
};

// Deeper drops: the lowest string 3..5 semitones below its family (rs7string beta.4). "Drop <note>" in the family the
// instrument is named after (k = 0: the 7-string "Drop G", the 8-string "Drop D"), "<family note> Drop <note>"
// otherwise ("A Drop F"). The low notes in the families' spelling: flats, F# kept. [0] the 7-string families (low B),
// [1] the 8-string ones (low F#); built by seven_names_init before the game runs.
#define DEEP_MIN 3
#define DEEP_MAX 5
#define FAMILIES 11
static char g_deep[2][FAMILIES][DEEP_MAX - DEEP_MIN + 1][24];

static void build_deep(void) {
  static const char *const notes[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  const family *tables[2] = {g_families7, g_families8};
  const int low_pc[2] = {11, 6};  // B, F#
  for (int b = 0; b < 2; b++)
    for (int i = 0; i < FAMILIES; i++) {
      const family *f = &tables[b][i];
      const size_t note_len = strcspn(f->standard, " ");
      for (int d = DEEP_MIN; d <= DEEP_MAX; d++) {
        const char *low = notes[((low_pc[b] + f->k - d) % 12 + 12) % 12];
        char *out = g_deep[b][i][d - DEEP_MIN];
        if (f->k == 0)
          snprintf(out, sizeof g_deep[0][0][0], "Drop %s", low);
        else
          snprintf(out, sizeof g_deep[0][0][0], "%.*s Drop %s", (int)note_len, f->standard, low);
      }
    }
}

// The name of t[0..n-1] all at k but the last, which is at k (Standard), k - 2 (the family's drop) or k - 3..5 (a
// deeper drop); NULL when there is none. table: 0 the 7-string families, 1 the 8-string ones.
static const char *family_name(int table, const int *t, int n) {
  const family *fams = table ? g_families8 : g_families7;
  for (int s = 1; s < n - 1; s++)
    if (t[s] != t[0]) return NULL;
  for (int i = 0; i < FAMILIES; i++) {
    if (t[0] != fams[i].k) continue;
    const int d = t[0] - t[n - 1];
    if (d == 0) return fams[i].standard;
    if (d == 2) return fams[i].drop;
    if (d >= DEEP_MIN && d <= DEEP_MAX && g_deep[table][i][d - DEEP_MIN][0]) return g_deep[table][i][d - DEEP_MIN];
    return NULL;
  }
  return NULL;
}

// t[0..5] = string0..5 (low E .. high e), t[6] = Rs7String6: the canonical name ("B Standard", "Bb Drop Ab",
// "Extended Drop D"), or NULL
static const char *name7(const int t[7]) {
  for (size_t i = 0; i < sizeof g_alternates / sizeof *g_alternates; i++)
    if (!memcmp(g_alternates[i].t, t, sizeof g_alternates[i].t)) return g_alternates[i].name;
  return family_name(0, t, 7);
}

// The same with t[7] = Rs7String7: strings 0..6 all at k, the 8th at k, k - 2 or k - 3..5
static const char *name8(const int t[8]) {
  return family_name(1, t, 8);
}


static void seven_names_build(void) { build_deep(); }

#endif
