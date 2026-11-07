// RSMods: linear Riff Repeater speeds (RiffRepeater::EnableLinearSpeeds): the slider's percent is the song's
// speed (68% plays at 68%, not about 50%). The game converts the slider value x in rr_speed_ratio:
// ratio = 100 / m, m = 400 - 3x up to 100 and 100 - 0.75 (x - 100) above, clamped to 25..400. RSMods makes
// m = 10000 / x (ratio = x / 100); so does this, with the game's clamp.
#include <stdatomic.h>

#include "rsmods.h"

static atomic_int g_on;

static void tick(void) { atomic_store(&g_on, rsm_on("LinearRiffRepeater")); }

static int before_ratio(rc_ctx *c, void *user) {
  (void)user;
  if (!atomic_load(&g_on)) return 0;
  OV_ARGS(a, c);
  float x = OV_F32(a);
  float m = x > 0 ? 10000.f / x : 400.f;
  if (m < 25.f) m = 25.f;
  if (m > 400.f) m = 400.f;
  static float last = -1;
  if (x != last) rsm_log("linear riff repeater: slider %.0f, ratio %.3f", x, 100.f / m);
  last = x;
  ov_ret_f32(c, 100.f / m);
  return 1;
}

__attribute__((constructor)) static void init(void) {
  rsm_hook("rr_speed_ratio", before_ratio, NULL);
}

const rsm_mod rsm_mod_linear_rr = {"linear_rr", tick, NULL, NULL, NULL};
