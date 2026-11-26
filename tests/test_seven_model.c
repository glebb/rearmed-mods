// Seven-String Guitar: the instrument model (seven_model.h/.c): slots against NATIVE-FORMAT's table, ranks, counts,
// the extended-bass setups of rsxrbass::SetupPlayer, the features, the settings' parsing.
#include <string.h>

#include "check.h"
#include "seven_model.h"

static const char *g_vals[16][2];
static int g_nvals, g_bad;
static const char *get(const char *k, void *u) {
  (void)u;
  for (int i = 0; i < g_nvals; i++)
    if (!strcmp(g_vals[i][0], k)) return g_vals[i][1];
  return NULL;
}
static void bad(const char *k, const char *v, void *u) {
  (void)k, (void)v, (void)u;
  g_bad++;
}
static void set(const char *k, const char *v) {
  for (int i = 0; i < g_nvals; i++)
    if (!strcmp(g_vals[i][0], k)) {
      g_vals[i][1] = v;
      return;
    }
  g_vals[g_nvals][0] = k;
  g_vals[g_nvals++][1] = v;
}

static int slots(int ins, int n, const int *want) {
  for (int i = 0; i < n; i++)
    if (seven_slot_of(ins, n, i) != want[i]) return 0;
  return seven_slot_of(ins, n, n) == -1 && seven_slot_of(ins, n, -1) == -1;
}

int main(void) {
  // slots: NATIVE-FORMAT's table
  CHECK(slots(SEVEN_GUITAR, 6, (int[]){0, 1, 2, 3, 4, 5}));
  CHECK(slots(SEVEN_GUITAR, 7, (int[]){6, 0, 1, 2, 3, 4, 5}));
  CHECK(slots(SEVEN_GUITAR, 8, (int[]){7, 6, 0, 1, 2, 3, 4, 5}));
  CHECK(slots(SEVEN_BASS, 4, (int[]){0, 1, 2, 3}));
  CHECK(slots(SEVEN_BASS, 5, (int[]){4, 0, 1, 2, 3}));
  CHECK(slots(SEVEN_BASS, 6, (int[]){4, 0, 1, 2, 3, 5}));
  CHECK(seven_slot_of(SEVEN_GUITAR, 5, 0) == -1 && seven_slot_of(SEVEN_GUITAR, 9, 0) == -1);
  CHECK(seven_slot_of(SEVEN_BASS, 3, 0) == -1 && seven_slot_of(SEVEN_BASS, 7, 0) == -1 && seven_slot_of(2, 6, 0) == -1);

  // ranks: slot -> position from the lowest; the inverse of slot_of
  CHECK(seven_rank_of(SEVEN_GUITAR, 6, 0) == 0 && seven_rank_of(SEVEN_GUITAR, 6, 5) == 5);
  CHECK(seven_rank_of(SEVEN_GUITAR, 7, 6) == 0 && seven_rank_of(SEVEN_GUITAR, 7, 0) == 1);
  CHECK(seven_rank_of(SEVEN_GUITAR, 8, 7) == 0 && seven_rank_of(SEVEN_GUITAR, 8, 6) == 1 &&
        seven_rank_of(SEVEN_GUITAR, 8, 5) == 7);
  CHECK(seven_rank_of(SEVEN_GUITAR, 7, 7) == -1);
  CHECK(seven_rank_of(SEVEN_BASS, 5, 4) == 0 && seven_rank_of(SEVEN_BASS, 5, 3) == 4 && seven_rank_of(SEVEN_BASS, 5, 5) == -1);
  CHECK(seven_rank_of(SEVEN_BASS, 6, 4) == 0 && seven_rank_of(SEVEN_BASS, 6, 5) == 5);
  CHECK(seven_rank_of(SEVEN_BASS, 4, 2) == 2);
  // ascending open pitch in rank order (standard tuning)
  for (int n = 6; n <= 8; n++)
    for (int r = 1; r < n; r++)
      CHECK(seven_guitar_open[seven_slot_of(SEVEN_GUITAR, n, r - 1)] < seven_guitar_open[seven_slot_of(SEVEN_GUITAR, n, r)]);
  for (int n = 4; n <= 6; n++)
    for (int r = 1; r < n; r++)
      CHECK(seven_bass_open[seven_slot_of(SEVEN_BASS, n, r - 1)] < seven_bass_open[seven_slot_of(SEVEN_BASS, n, r)]);

  // desired counts
  CHECK(seven_guitar_desired(0, 0) == 6 && seven_guitar_desired(1, 0) == 7 && seven_guitar_desired(1, 1) == 8);
  CHECK(seven_bass_desired(0, 0) == 4 && seven_bass_desired(1, 0) == 5 && seven_bass_desired(1, 1) == 6);
  CHECK(seven_is_ext_guitar(7) && seven_is_ext_guitar(8) && !seven_is_ext_guitar(6));
  CHECK(seven_is_ext_bass(5) && seven_is_ext_bass(6) && !seven_is_ext_bass(4));
  CHECK(seven_desired_guitar(0) == 6 && seven_desired_bass(1) == 4);
  seven_set_desired(1, 8, 6);
  CHECK(seven_desired_guitar(1) == 8 && seven_desired_bass(1) == 6 && seven_desired_guitar(0) == 6);
  seven_set_desired(0, 99, 0);
  CHECK(seven_desired_guitar(0) == 8 && seven_desired_bass(0) == 4);
  seven_set_desired(2, 7, 5);
  CHECK(seven_desired_guitar(2) == 6 && seven_desired_bass(-1) == 4);

  // bass setups (SetupPlayer)
  int a = 0, b = 0, c = 0;
  CHECK(seven_bass_max_strings() == 0 && !seven_bass_any_guitar());
  uint32_t s0 = seven_bass_serial();
  seven_bass_setup(&a, 2, 5, 1000);
  CHECK(seven_bass_strings_of(&a) == 5 && seven_bass_strings_of(&b) == 0 && seven_bass_strings_of(NULL) == 0);
  CHECK(seven_bass_max_strings() == 5 && !seven_bass_any_guitar() && seven_bass_serial() != s0);
  seven_bass_setup(&b, 0, 0, 2000);  // a guitar player of the same song
  CHECK(seven_bass_any_guitar() && seven_bass_max_strings() == 5 && seven_bass_strings_of(&a) == 5);
  uint32_t s1 = seven_bass_serial();
  seven_bass_setup(&b, 0, 0, 2500);  // the same again: nothing changes
  CHECK(seven_bass_serial() == s1);
  seven_bass_setup(&a, 2, 6, 3000);  // the same detector replaces its setup
  CHECK(seven_bass_strings_of(&a) == 6 && seven_bass_max_strings() == 6);
  seven_bass_setup(&c, 2, 5, 8000);  // a is exactly 5000 old: kept (only more than 5000 goes); b (2500) goes
  CHECK(seven_bass_strings_of(&a) == 6 || seven_bass_strings_of(&c) == 5);
  CHECK(!seven_bass_any_guitar());
  seven_bass_clear();
  CHECK(seven_bass_max_strings() == 0 && !seven_bass_any_guitar());
  seven_bass_setup(&a, 2, 6, 1000);
  seven_bass_setup(&b, 0, 0, 3000);
  seven_bass_setup(&c, 2, 5, 6000);  // a is 5000 old: kept; the table is full: the oldest (a) is replaced
  CHECK(seven_bass_strings_of(&a) == 0 && seven_bass_strings_of(&c) == 5 && seven_bass_strings_of(&b) == 0);
  CHECK(seven_bass_any_guitar() && seven_bass_max_strings() == 5);
  seven_bass_setup(&a, 2, 6, 9001);  // b (3000) is 6001 old: dropped; c (6000) stays
  CHECK(!seven_bass_any_guitar() && seven_bass_strings_of(&c) == 5 && seven_bass_strings_of(&a) == 6);
  seven_bass_setup(&b, 0, 0, 20000);  // everything earlier is dropped
  CHECK(seven_bass_max_strings() == 0 && seven_bass_any_guitar() && seven_bass_strings_of(&a) == 0);
  CHECK(seven_bass_strings_of(&b) == 0);
  seven_bass_clear();

  // features
  CHECK(!seven_feature(SEVEN_F_BASS));
  seven_feature_set(SEVEN_F_BASS, 1);
  CHECK(seven_feature(SEVEN_F_BASS) && !seven_feature(SEVEN_F_COOP));
  seven_feature_set(SEVEN_F_BASS, 0);
  CHECK(!seven_feature(SEVEN_F_BASS) && !seven_feature(SEVEN_F_COUNT));

  // parsing
  uint32_t rgb = 0;
  CHECK(seven_parse_rgb("FF66cc", &rgb) && rgb == 0xFF66CC);
  CHECK(seven_parse_rgb(" #00C68E ", &rgb) && rgb == 0x00C68E);
  CHECK(!seven_parse_rgb("FF66C", &rgb) && !seven_parse_rgb("FF66CCC", &rgb) && !seven_parse_rgb("GG0000", &rgb));
  CHECK(!seven_parse_rgb("", &rgb) && !seven_parse_rgb(NULL, &rgb));
  float f = -1;
  CHECK(seven_parse_spacing("", &f) && f == 0);
  CHECK(seven_parse_spacing("0", &f) && f == 0);
  CHECK(seven_parse_spacing("0.5", &f) && f == 0.5f);
  CHECK(seven_parse_spacing(" 4.0 ", &f) && f == 4.0f);
  CHECK(seven_parse_spacing("1.25", &f) && f == 1.25f);
  CHECK(!seven_parse_spacing("0.4", &f) && !seven_parse_spacing("4.1", &f) && !seven_parse_spacing("-1", &f));
  CHECK(!seven_parse_spacing("abc", &f) && !seven_parse_spacing("1.2x", &f) && !seven_parse_spacing("nan", &f));

  seven_settings s;
  seven_settings_parse(&s, get, bad, NULL);  // no keys: the defaults
  CHECK(s.spacing_guitar7 == 0 && s.spacing_bass6 == 0 && s.string7_line == 1 && s.diagrams7 == 1 && s.lowrange == 1);
  CHECK(s.string7_rgb[0] == 0xFF66CC && s.string7_rgb[1] == 0xFF66CC && !s.string7_set[0] && !s.string7_set[1] && !g_bad);
  set("spacing_guitar7", "1.5");
  set("spacing_guitar8", "9");
  set("spacing_bass5", "0.5");
  set("lowrange", "0");
  set("string7_line", "off");
  set("diagrams7", "1");
  set("string7_N", "112233");
  seven_settings_parse(&s, get, bad, NULL);
  CHECK(s.spacing_guitar7 == 1.5f && s.spacing_guitar8 == 0 && s.spacing_bass5 == 0.5f && s.spacing_bass6 == 0);
  CHECK(g_bad == 1 && !s.lowrange && !s.string7_line && s.diagrams7);
  CHECK(s.string7_rgb[0] == 0x112233 && s.string7_rgb[1] == 0x112233 && s.string7_set[0] && !s.string7_set[1]);
  set("string7_CB", "445566");
  seven_settings_parse(&s, get, bad, NULL);
  CHECK(s.string7_rgb[1] == 0x445566 && s.string7_set[1]);
  set("string7_CB", "zz");
  g_bad = 0;
  seven_settings_parse(&s, get, bad, NULL);
  CHECK(g_bad == 2 && s.string7_rgb[1] == 0x112233 && !s.string7_set[1]);
  return check_done("seven_model");
}
