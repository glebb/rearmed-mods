// Seven-String Guitar (seven.h): the spacing part (seven_spacing.h). After rs7string's spacing feature
// (github.com/SomeeName/rs7string, MIT, by SomeeName): its PUBGA hook answers Rnd_GetStringAttachPoint scaled.
//
// The point seven_pubga (Rnd_GetStringAttachPoint's entry; the pitch part answers strings 6/7 there, from strings 0 and
// 1 by the game's query, which this hook scales too, so the extra strings follow the spacing) gets a second hook here:
// for strings 0-5 of a guitar built with 7 or 8 strings it asks the game's own query for the stock point (a thread-
// local flag makes the hook let that nested query through) and scales it about the middle of the stock six. The
// stock points of strings 0 and 1 are cached per guitar (seven_spacing_forget at each new InstrumentInfo).
// A bass built with 5 or 6 strings is spaced the same way (spacing_bass5 / spacing_bass6) about the middle of its stock
// four strings; its strings 4 and 5 are the strings part's, answered from the (scaled) rows 0/1 and 3/2.
// The feature needs only points the unit has (it is off without the pitch part's hook, i.e. never alone).
#include <stdio.h>
#include <string.h>

#include "seven.h"

static __thread int g_in_query;
static struct {
  const void *info;
  int valid;
  float a0[3], a1[3];
} g_stock;
static uint8_t *g_scratch;  // two vec3 in the game's memory
static int g_logs = 6;

static int stock_query(void *info, unsigned s, float *out) {
  g_in_query++;
  const int ok = (int)(OV_GUEST(guitar_string_attach_point, (uintptr_t)info, s, (uintptr_t)out) & 0xff);
  g_in_query--;
  return ok;
}

void seven_spacing_forget(void) { g_stock.info = NULL, g_stock.valid = 0; }

int seven_spacing_stock(const void *info, float a0[3], float a1[3]) {
  if (!info) return 0;
  if (g_stock.valid && g_stock.info == info) {
    memcpy(a0, g_stock.a0, sizeof g_stock.a0);
    memcpy(a1, g_stock.a1, sizeof g_stock.a1);
    return 1;
  }
  if (!g_scratch && !(g_scratch = rc_guest_alloc(2 * 3 * sizeof(float)))) return 0;
  float *p0 = (float *)g_scratch, *p1 = p0 + 3;
  if (!stock_query((void *)info, 0, p0) || !stock_query((void *)info, 1, p1)) return 0;
  for (int i = 0; i < 3; i++) {
    g_stock.a0[i] = RC_LD(float, p0 + i);
    g_stock.a1[i] = RC_LD(float, p1 + i);
  }
  g_stock.info = info;
  g_stock.valid = 1;
  memcpy(a0, g_stock.a0, sizeof g_stock.a0);
  memcpy(a1, g_stock.a1, sizeof g_stock.a1);
  return 1;
}

// What spacing applies to an InstrumentInfo: 1 with *instrument, *n (its build count) and *far (its stock far string, 5
// on a guitar of 7 or 8 strings, 3 on a bass of 5 or 6); 0 for any other
static int spaced_kind(const void *info, int *instrument, int *n, int *far) {
  const int count = seven_strings_info_count(info);
  if (seven_strings_info_is_bass(info)) {
    if (!seven_feature(SEVEN_F_BASS) || !seven_is_ext_bass(count)) return 0;
    *instrument = SEVEN_BASS, *far = SEVEN_BASS_STOCK - 1;
  } else {
    if (!seven_is_ext_guitar(count)) return 0;
    *instrument = SEVEN_GUITAR, *far = SEVEN_STOCK - 1;
  }
  *n = count;
  return 1;
}

static float dist3(const float a[3], const float b[3]) {
  float s = 0;
  for (int i = 0; i < 3; i++) s += (a[i] - b[i]) * (a[i] - b[i]);
  return sqrtf(s);
}

int seven_spacing_attach(void *info, unsigned s, float *out) {
  if (!seven_on || !seven_feature(SEVEN_F_SPACING) || g_in_query || !info || !out || s >= 6) return -1;
  int instrument, n, far;
  if (!spaced_kind(info, &instrument, &n, &far) || s > (unsigned)far) return -1;  // a bass's 4 / 5 are the strings part's
  const float want = seven_spacing_for(instrument, n);
  if (!(want > 0.0f)) return -1;
  float a0[3], a1[3];
  if (!seven_spacing_stock(info, a0, a1)) return -1;  // no headstock to ask: the game's query answers as it does
  const float k = seven_spacing_scale(want, dist3(a0, a1));
  const int ok = stock_query(info, s, out);
  if (!ok || k == 1.0f) return ok;
  float c[3], p[3], q[3];
  seven_spacing_centre_far(a0, a1, far, c);
  for (int i = 0; i < 3; i++) p[i] = RC_LD(float, out + i);
  seven_spacing_point(p, c, k, q);
  for (int i = 0; i < 3; i++) RC_ST(float, out + i, q[i]);
  if (g_logs > 0 && !s) {
    g_logs--;
    rsm_log("seven-string: spacing: %d-string %s, spacing %.3f (stock %.3f): k %.4f", n,
            instrument == SEVEN_BASS ? "bass" : "guitar", want, dist3(a0, a1), k);
  }
  return ok;
}

float seven_spacing_k(const void *guitar) {
  if (!seven_on || !seven_feature(SEVEN_F_SPACING) || !guitar) return 1.0f;
  const void *info = rc_mod_ptr((const uint8_t *)guitar + RC_OFF(guitar_info));
  int instrument, n, far;
  if (!spaced_kind(info, &instrument, &n, &far)) return 1.0f;
  float a0[3], a1[3];
  if (!seven_spacing_stock(info, a0, a1)) return 1.0f;
  const uint8_t *s0 = seven_strings_child((void *)guitar, 0), *s1 = seven_strings_child((void *)guitar, 1);
  if (!s0 || !s1) return 1.0f;
  float b0[3], b1[3];
  for (int i = 0; i < 3; i++) {
    b0[i] = RC_LD(float, s0 + RC_OFF(guitar_string_attach) + 4 * i);
    b1[i] = RC_LD(float, s1 + RC_OFF(guitar_string_attach) + 4 * i);
  }
  const float stock = dist3(a0, a1), now = dist3(b0, b1);
  return stock > 1e-3f && now > 1e-3f ? now / stock : 1.0f;
}

// seven_pubga, after the pitch part's hook (parts register in order): strings 0-5 of a spaced guitar
static void on_pubga(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !seven_feature(SEVEN_F_SPACING) || g_in_query) return;
  const unsigned s = (unsigned)(SEVEN_GET(f, "string") & 0xff);
  if (s >= 6) return;
  void *info = (void *)(uintptr_t)SEVEN_GET(f, "info");
  float *out = (float *)(uintptr_t)SEVEN_GET(f, "out");
  const int r = seven_spacing_attach(info, s, out);
  if (r < 0) return;
  SEVEN_SET(f, "point", (uint32_t)r);
  f->go = SEVEN_CONT(f, "ret");
}

int seven_spacing_init(void) {
  seven_feature_set(SEVEN_F_SPACING, seven_mid_optional("seven_pubga", on_pubga, "string spacing"));
  return 0;
}
