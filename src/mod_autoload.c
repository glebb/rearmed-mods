// RSMods: auto enter the last used profile (AutoLoadProfileMod.cpp). Until the main menu shows, presses
// Enter (and Escape on dialogs) twice a second, as RSMods does, which goes from the title screen into the top
// profile of the list: the last used one. The first-time setup screens are left alone.
//
// ProfileToLoad (a profile's name) enters that one instead. RSMods presses Down on the profile screen until the
// highlighted name is it, reading the name from the game. Here the list comes from the profiles' own list,
// LocalProfiles.json (rsmods_profiles.c), in the order the game shows it, last used first: on the profile screen the
// mod presses Down as many times as the profile is down the list, then Enter. A name that isn't there stops the
// mod, as in RSMods (nothing is entered).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods.h"

#define KVK_RETURN 0x24
#define KVK_ESCAPE 0x35
#define KVK_DOWN 0x7d
#define CH_DOWN 0xf701  // NSDownArrowFunctionKey

static const char *const DONT_AUTO_ENTER[] = {
    "TextEntryDialog", "PlayedRS1Select", "ExperienceSelect", "PathSelect", "HandSelect", "HeadstockSelect",
    "FE_InputSelect", "FECalibrationMeter", "VideoPlayer", "FETuner", "FirstEncounter_Game", "RefresherSelect",
    NULL};

static int g_done;  // the main menu was reached: only at startup
static double g_next;
static int g_downs = -1;   // on the profile screen: Down presses still to make; -1 not worked out yet
static int g_stopped;      // ProfileToLoad isn't a profile, or the profile screen was tried once
static double g_profile_screen_at;

// How far down the list ProfileToLoad is; -1 if it isn't there (or the list can't be read)
static int profile_index(const char *want) {
  char folder[1024];
  unsigned long long account = 0;
  if (!rsm_save_folder(folder, sizeof folder, &account)) {
    rsm_log("auto load profile: no save folder found");
    return -1;
  }
  char *json = rsm_profiles_json(folder);
  if (!json) {
    rsm_log("auto load profile: %s/LocalProfiles.json can't be read", folder);
    return -1;
  }
  static char names[64][64];
  int n = rsm_profiles_names(json, names, 64), found = -1;
  free(json);
  for (int i = 0; i < n && found < 0; i++)
    if (!strcmp(names[i], want)) found = i;
  for (int i = 0; i < n && found < 0; i++)
    if (!strcasecmp(names[i], want)) found = i;
  rsm_log("auto load profile: \"%s\" is %d of %d in account %llu's list", want, found, n, account);
  return found;
}

// On the profile screen with ProfileToLoad set: Down to it, then Enter. 1 while it is at it.
static int to_profile(const char *menu, double t) {
  const char *want = rsm_get("Toggle Switches", "ProfileToLoad");
  if (!want || !*want || strcmp(menu, "ProfileSelect")) {
    g_profile_screen_at = 0;
    return 0;
  }
  if (g_stopped) return 1;
  if (!g_profile_screen_at) g_profile_screen_at = t;
  if (t - g_profile_screen_at < 1.0) return 1;  // the list fills in
  if (g_downs < 0) {
    g_downs = profile_index(want);
    if (g_downs < 0) {
      fprintf(stderr, "[rsmods] auto load profile: no profile \"%s\": nothing is entered\n", want);
      g_stopped = 1;
      return 1;
    }
  }
  if (t < g_next) return 1;
  g_next = t + 0.2;
  if (g_downs > 0) {
    rsm_send_key(KVK_DOWN, CH_DOWN);
    g_downs--;
    return 1;
  }
  rsm_send_key(KVK_RETURN, 0x0d);
  rsm_log("auto load profile: Enter on \"%s\"", want);
  g_stopped = 1;  // once: if the screen stays, the player takes it from here
  return 1;
}

static void tick(void) {
  if (g_done) return;
  const char *m = rsm_menu();
  if (!strcmp(m, "MainMenu")) {
    g_done = 1;
    return;
  }
  if (!rsm_on("ForceProfileLoad") || !*m || rsm_menu_in(DONT_AUTO_ENTER)) return;
  double t = rsm_now();
  if (to_profile(m, t)) return;
  if (t < g_next) return;
  g_next = t + 0.5;
  if (!strcmp(m, "SelectionListDialog") || !strcmp(m, "UplayLoginDialog")) rsm_send_key(KVK_ESCAPE, 0x1b);
  rsm_send_key(KVK_RETURN, 0x0d);
  rsm_log("auto load profile: Enter (%s)", m);
}

const rsm_mod rsm_mod_autoload = {"autoload", tick, NULL, NULL, NULL};
