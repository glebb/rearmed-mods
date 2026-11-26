// The names of 7/8-string tunings (src/seven_tuning_names.h), with rs7string beta.4's deeper drops
#include <string.h>
#include "../src/seven_tuning_names.h"
#include "check.h"

static int is(const char *got, const char *want) { return got && !strcmp(got, want); }

// 7 strings: all six at k, the low B at low
static const char *n7(int k, int low) {
  const int t[7] = {k, k, k, k, k, k, low};
  return name7(t);
}

static const char *n8(int k, int low) {
  const int t[8] = {k, k, k, k, k, k, k, low};
  return name8(t);
}

int main(void) {
  seven_names_build();
  // the families as before
  CHECK(is(n7(0, 0), "B Standard"));
  CHECK(is(n7(0, -2), "Drop A"));
  CHECK(is(n7(-1, -3), "Bb Drop Ab"));
  CHECK(is(n7(-7, -7), "E Standard"));
  CHECK(is(n7(-9, -11), "D Drop C"));
  CHECK(is(n7(1, 1), "C Standard"));
  CHECK(n7(0, -1) == NULL);  // one below: no name
  CHECK(n7(0, 1) == NULL);
  CHECK(n7(2, 2) == NULL);   // outside the families
  CHECK(n7(-10, -10) == NULL);
  // a deeper drop: the lowest string 3..5 below its family
  CHECK(is(n7(0, -3), "Drop Ab"));
  CHECK(is(n7(0, -4), "Drop G"));
  CHECK(is(n7(0, -5), "Drop F#"));
  CHECK(n7(0, -6) == NULL);
  CHECK(is(n7(-2, -6), "A Drop F"));    // A Standard, the low B at F
  CHECK(is(n7(-2, -7), "A Drop E"));     // five below
  CHECK(is(n7(-1, -4), "Bb Drop G"));
  CHECK(is(n7(1, -2), "C Drop A"));
  CHECK(is(n7(-3, -8), "Ab Drop Eb"));   // the families' spelling: flats
  CHECK(is(n7(-5, -10), "Gb Drop Db"));
  CHECK(is(n7(-9, -14), "D Drop A"));
  CHECK(n7(-9, -15) == NULL);
  { const int t[7] = {0, 0, 0, 0, 0, 1, -4}; CHECK(name7(t) == NULL); }  // the other strings must agree
  // the alternates still win by their exact offsets
  { const int t[7] = {-2, 0, 0, 0, 0, 0, -2}; CHECK(is(name7(t), "Extended Drop D")); }
  { const int t[7] = {-4, -2, -2, -2, -2, -2, -4}; CHECK(is(name7(t), "Extended Drop C")); }
  // 8 strings (low F#): the families and the deeper drops
  CHECK(is(n8(0, 0), "F# Standard"));
  CHECK(is(n8(0, -2), "Drop E"));
  CHECK(is(n8(0, -3), "Drop Eb"));
  CHECK(is(n8(0, -5), "Drop Db"));
  CHECK(is(n8(-2, -5), "E Drop Db"));
  CHECK(n8(0, -6) == NULL);
  { const int t[8] = {0, 0, 0, 0, 0, 1, 0, 0}; CHECK(name8(t) == NULL); }
  // 5/6-string bass (rs7string's NameBass): the low B (string 4) names it, every other played string at one offset
  {
    const int beadg[6] = {0, 0, 0, 0, 0, 0}, down[6] = {-1, -1, -1, -1, -1, 0}, drop_a[6] = {0, 0, 0, 0, -2, 0};
    const int drop_g[6] = {0, 0, 0, 0, -4, 0}, a_drop_fs[6] = {-2, -2, -2, -2, -5, -2}, too_low[6] = {0, 0, 0, 0, -6, 0};
    const int up[6] = {0, 0, 0, 0, 1, 0}, third[6] = {0, 0, 1, 0, 0, 0};
    CHECK(is(name_bass(beadg, 5), "B Standard"));
    CHECK(is(name_bass(beadg, 6), "B Standard"));
    CHECK(is(name_bass(down, 5), "Bb Standard"));  // [5] is not played on 5 strings
    CHECK(name_bass(down, 6) == NULL);
    CHECK(is(name_bass(drop_a, 5), "Drop A"));  // the low B two below the others
    CHECK(is(name_bass(drop_a, 6), "Drop A"));
    CHECK(name_bass(beadg, 4) == NULL);
    CHECK(name_bass(beadg, 7) == NULL);
    CHECK(is(name_bass(drop_g, 5), "Drop G"));
    CHECK(is(name_bass(a_drop_fs, 6), "A Drop F#"));
    CHECK(name_bass(too_low, 5) == NULL);  // 6 below: no name
    CHECK(name_bass(up, 5) == NULL);       // the low B above the others
    CHECK(name_bass(third, 5) == NULL);    // another string off
  }
  // the mark's count
  {
    char out[32];
    suffix_n(out, sizeof out, "7Str", 8); CHECK(is(out, "8Str"));
    suffix_n(out, sizeof out, "7Str", 5); CHECK(is(out, "5Str"));
    suffix_n(out, sizeof out, "7Str", 6); CHECK(is(out, "6Str"));
    suffix_n(out, sizeof out, "7-String", 8); CHECK(is(out, "8-String"));
    suffix_n(out, sizeof out, "", 5); CHECK(is(out, "5Str"));
    suffix_n(out, sizeof out, "Ext", 6); CHECK(is(out, "6Str"));
    suffix_n(out, sizeof out, NULL, 8); CHECK(is(out, "8Str"));
  }
  return check_done("seven_names");
}
