// Sunlight's color grades (src/mod_sunlight.c): what a screen in the sun needs of them
#include "../src/mod_sunlight.c"
#include "check.h"

const char *const RSM_SONG_MODES[] = {NULL};
double rsm_now(void) { return 0; }
int rsm_on(const char *key) { (void)key; return 0; }
long rsm_long(const char *key, long dflt) { (void)key; return dflt; }
int rsm_menu_in(const char *const *list) { (void)list; return 0; }
int rsm_key(const char *key) { (void)key; return -1; }
void rsm_set(const char *s, const char *k, const char *v) { (void)s, (void)k, (void)v; }
void rsm_text(const char *t, float x, float y, float s, int a, uint32_t c) { (void)t, (void)x, (void)y, (void)s, (void)a, (void)c; }
void rsm_log(const char *fmt, ...) { (void)fmt; }

static float luma(const float c[3]) {
  float lin[3] = {to_linear(c[0]), to_linear(c[1]), to_linear(c[2])};
  return luminance(lin);
}

int main(void) {
  static const float COLORS[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {.5f, 0, 1}, {1, .5f, 0}};
  float out[3];
  const float black[3] = {0, 0, 0}, white[3] = {1, 1, 1};

  sunlight_grade(GRADE_BRIGHT, black, out);
  CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
  sunlight_grade(GRADE_DAYLIGHT, black, out);
  CHECK(out[0] == 1 && out[1] == 1 && out[2] == 1);  // the background goes white
  sunlight_grade(GRADE_DAYLIGHT, white, out);
  CHECK(luma(out) <= DAYLIGHT_INK + .01f);           // and what was bright is dark ink

  for (size_t i = 0; i < sizeof COLORS / sizeof COLORS[0]; i++) {
    sunlight_grade(GRADE_BRIGHT, COLORS[i], out);    // no color is left too dark to see
    CHECK(luma(out) >= BRIGHT_FLOOR - .01f);
    sunlight_grade(GRADE_DAYLIGHT, COLORS[i], out);  // none is too light to see on white
    CHECK(luma(out) <= DAYLIGHT_INK + .01f);
    for (int k = 0; k < 3; k++) CHECK(out[k] >= 0 && out[k] <= 1);
  }

  for (int g = 0; g < 2; g++) {  // a grey ramp stays a ramp
    int grade = g ? GRADE_DAYLIGHT : GRADE_BRIGHT;
    float last = g ? 2.f : -1.f;
    for (int i = 0; i <= 32; i++) {
      float v = (float)i / 32, in[3] = {v, v, v};
      sunlight_grade(grade, in, out);
      CHECK(g ? out[0] <= last + 1e-4f : out[0] >= last - 1e-4f);
      last = out[0];
    }
  }
  return check_done("sunlight");
}
