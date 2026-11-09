// Seven-String Guitar (seven.h): the mod. [Toggle Switches] SevenString = on in RSMods.ini, read once at the start:
// the edits go on before the game runs or not at all, so a change of it needs a restart of the game.
//
// It needs an RS Re-ARMed with edits and mid points (rc_mid.h) and the tables of this game; without them, or when a
// part lacks one of its points here, the game is as before and the log says why.
#include <dlfcn.h>

#include "seven.h"

int seven_on;
static int g_points;

int seven_mid(const char *name, rc_mid_fn fn) {
  if (rc_mid_add_name(name, fn, NULL) < 0) return -1;
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
  rsm_log("seven-string: on, %d mid points", g_points);
}
