// Seven-String Guitar (seven.h): the mod. [Toggle Switches] SevenString = on in RSMods.ini, read once at the start:
// the edits go on before the game runs or not at all, so a change of it needs a restart of the game.
//
// It needs an RS Re-ARMed with edits and mid points (rc_mid.h) and the tables of this game; without them, or when a
// part lacks one of its points here, the game is as before and the log says why.
#include <dlfcn.h>
#include <string.h>

#include "seven.h"

int seven_on;
static int g_points, g_none;

// The Mac game's own moments (its unrolled loops, functions Learn & Play inlines): `none` in Learn & Play's tables.
// An RS Re-ARMed older than the `none` state (no rc_mid_is_none) has none of these names and no Mac tables for this
// mod, so there a missing one of them is no reason to stay off.
static const char *const k_mac_only[] = {
    "seven_f1_setscale",       "seven_grid_cellrow_ctor", "seven_grid_wirerow_ctor", "seven_grid_wirerow_reset",
    "seven_grid_wirerow_mark", "seven_grid_wirerow_flush", "seven_grid_wires_dtor",  "seven_s1b",
    "seven_s4_input",          "seven_puban",             "seven_pubnl_count",       "seven_pubnl_open",
    "seven_tu8dt",             "seven_ds1_count",         "seven_cs1_ctor",          "seven_cs1_build",
    "seven_sh7",               "seven_pl7",               "seven_sm7",               "seven_gsc_tail",
    "seven_gsfr_tail",         "seven_psd_tail",          "seven_am1_b",             "seven_pd2_update",
    "seven_pd2_chord",         "seven_pd3_update",        "seven_pd4_update",        "seven_pd4_sustain",
    "seven_pd5_eval"};

int seven_mid(const char *name, rc_mid_fn fn) {
  // looked up: an older RS Re-ARMed has no rc_mid_is_none
  static int (*is_none)(int) = (int (*)(int))-1;
  if (is_none == (int (*)(int))-1) is_none = (int (*)(int))dlsym(RTLD_DEFAULT, "rc_mid_is_none");
  int point = rc_mid_add_name(name, fn, NULL);
  if (point < 0) {
    if (!is_none)
      for (size_t i = 0; i < sizeof k_mac_only / sizeof *k_mac_only; i++)
        if (!strcmp(name, k_mac_only[i])) return 0;
    return -1;
  }
  if (is_none && is_none(point))
    g_none++;  // taken, never called on this game
  else
    g_points++;
  return 0;
}

__attribute__((constructor)) static void seven_init(void) {
  rsm_ini_poll();
  if (!rsm_on("SevenString")) return;
  if (!dlsym(RTLD_DEFAULT, "rc_mid_add_name") || !dlsym(RTLD_DEFAULT, "rc_edit_enable")) {
    rsm_log("seven-string: this RS Re-ARMed has no edits and mid points: the guitar keeps six strings");
    return;
  }
  static const struct {
    const char *name;
    int (*init)(void);
  } parts[] = {{"colours", seven_colours_init}, {"grid", seven_grid_init},   {"strings", seven_strings_init},
               {"pitch", seven_pitch_init},     {"tuner", seven_tuner_init},     {"chords", seven_chords_init},
               {"chord detection", seven_chdetect_init}, {"chord drawing", seven_chdraw_init},
               {"chord diagrams", seven_diagrams_init}, {"tuning names", seven_names_init},
               {"headstock", seven_headstock_init}};
  for (size_t i = 0; i < sizeof parts / sizeof *parts; i++)
    if (parts[i].init()) {
      rsm_log("seven-string: the %s part has no mid points on %s: the guitar keeps six strings", parts[i].name,
              rc_target());
      return;
    }
  if (rc_edit_enable(SEVEN_GROUP)) {
    rsm_log("seven-string: no edits of %s on %s: the guitar keeps six strings", SEVEN_GROUP, rc_target());
    return;
  }
  seven_on = 1;
  if (g_none)
    rsm_log("seven-string: on, %d mid points (and %d this game needs no call at)", g_points, g_none);
  else
    rsm_log("seven-string: on, %d mid points", g_points);
}
