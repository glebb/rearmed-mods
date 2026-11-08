// RSMods: prevent buggy tones (BugPrevention::PreventStuckTone; FixBrokenTones = on, "Prevent Buggy Tones"). In some
// songs the tone system gives up and plays the clean tone whatever tone is chosen, until a restart. A tone change for a
// player is applied at once unless one is already under way for that player, in which case it waits for that one to end
// (the busy byte, tone_change_busy). The stuck case is a change that never ends: every later one waits. RSMods makes the
// check jump past the wait. Here a hook on the request (tone_change, a hook point from RS Re-ARMed 1.11.0: the Mac's
// 0x1002bebc0, Learn & Play's 0x1352620, where RSMods patches) clears the busy byte first, and the game's own code
// applies the change.
#include <stdatomic.h>
#include <stdio.h>

#include "rsmods.h"

#define PLAYERS 2  // the busy bytes: one a player

static atomic_int g_on;

static void tick(void) { atomic_store(&g_on, rsm_on("FixBrokenTones")); }

static int before_tone_change(rc_ctx *c, void *user) {
  (void)user;
  if (!atomic_load(&g_on)) return 0;
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  uint32_t player = OV_U32(a);
  if (!self || player >= PLAYERS) return 0;
  volatile uint8_t *busy = self + RC_OFF(tone_change_busy) + player;
  if (*busy) {
    rsm_log("buggy tones: player %u's tone change was still waiting on the last; applied now", player + 1);
    *busy = 0;
  }
  return 0;
}

__attribute__((constructor)) static void init(void) {
  if (!RC_HAS_OFF(tone_change_busy)) {
    fprintf(stderr, "[rsmods] the tone change isn't known on %s: Prevent Buggy Tones needs RS Re-ARMed 1.11.0\n",
            rc_target());
    return;
  }
  rsm_hook("tone_change", before_tone_change, NULL);
}

const rsm_mod rsm_mod_buggy_tones = {"buggy_tones", tick, NULL, NULL, NULL};
