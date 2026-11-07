// RSMods: no popup for two Real Tone cables (TwoRTCBypassMod.cpp, QualityOfLife::PatchTwoRTC). RSMods NOPs the
// check in front of the popup; here the check itself, too_many_inputs (a second guitar input is active; only the
// popup's function calls it), answers no.
#include <stdatomic.h>

#include "rsmods.h"

static atomic_int g_on;

static void tick(void) { atomic_store(&g_on, rsm_on("BypassTwoRTCMessageBox")); }

static void after_check(rc_ctx *c, void *user) {
  (void)user;
  if (!atomic_load(&g_on)) return;
  static int said;
  if (!said++) rsm_log("two cables: the check answers no (it said %d)", (int)(c->r[0] & 0xff));
  c->r[0] = 0;
}

__attribute__((constructor)) static void init(void) {
  rsm_hook("too_many_inputs", NULL, after_check);
}

const rsm_mod rsm_mod_two_rtc = {"two_rtc", tick, NULL, NULL, NULL};
