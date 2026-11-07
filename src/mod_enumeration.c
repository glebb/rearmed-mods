// RSMods: force re-enumeration (EnumerationMod.cpp, Enumeration.cpp): the game looks for new songs (DLC and
// custom songs) without a restart. ForceReEnumeration = manual: the key starts it; automatic: when the number
// of files under dlc/ changes (checked every CheckForNewSongsInterval ms).
//
// RSMods sets the two request flags of the game's DLC service (dlc_pending, dlc_check_on: +4 / +5 in Learn & Play,
// +8 / +9 on the Mac), whose per-frame update runs the enumeration step when both are set. That service is a member
// of the connect service (the global rs_connect_service; set through its vtable by the factory rsconnect_factory).
#include <fts.h>
#include <pthread.h>
#include <stdatomic.h>
#include <strings.h>
#include <unistd.h>

#include "rsmods.h"

static atomic_int g_automatic, g_interval_ms = 5000, g_requested;

static void enumerate(const char *why) {
  uint64_t a = RC_ADDR(rs_connect_service);
  if (!a || !RC_HAS_OFF(connect_dlc_service) || !RC_HAS_OFF(dlc_pending) || !RC_HAS_OFF(dlc_check_on)) return;
  const char *svc = rc_mod_ptr((const void *)(uintptr_t)RC_A(a));
  const char *dlc = svc ? rc_mod_ptr(svc + RC_OFF(connect_dlc_service)) : NULL;
  if (!dlc) return;
  RC_ST(uint8_t, dlc + RC_OFF(dlc_pending), 1);
  RC_ST(uint8_t, dlc + RC_OFF(dlc_check_on), 1);
  rsm_log("enumeration: started (%s)", why);
}

static long dlc_file_count(void) {
  char *paths[] = {"dlc", NULL};
  FTS *f = fts_open(paths, FTS_LOGICAL | FTS_NOSTAT, NULL);  // follows the launcher's symlinks
  if (!f) return 0;
  long n = 0;
  for (FTSENT *e; (e = fts_read(f));)
    if (e->fts_info == FTS_F || e->fts_info == FTS_NSOK) n++;
  fts_close(f);
  return n;
}

static void *watcher(void *arg) {
  (void)arg;
  long before = -1;
  for (;;) {
    int ms = atomic_load(&g_interval_ms);
    usleep((useconds_t)(ms < 500 ? 500 : ms) * 1000);
    if (!atomic_load(&g_automatic)) {
      before = -1;
      continue;
    }
    long n = dlc_file_count();
    if (before >= 0 && n != before) atomic_store(&g_requested, 1);
    before = n;
  }
  return NULL;
}

static void tick(void) {
  static int started;
  const char *mode = rsm_get("Toggle Switches", "ForceReEnumeration");
  atomic_store(&g_automatic, mode && !strcasecmp(mode, "automatic"));
  atomic_store(&g_interval_ms, (int)rsm_long("CheckForNewSongsInterval", 5000));
  if (!started && atomic_load(&g_automatic)) {
    pthread_t t;
    if (pthread_create(&t, NULL, watcher, NULL) == 0) pthread_detach(t);
    started = 1;
  }
  if (atomic_exchange(&g_requested, 0) && atomic_load(&g_automatic)) enumerate("new files in dlc");
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("ForceReEnumerationKey")) return;
  const char *mode = rsm_get("Toggle Switches", "ForceReEnumeration");
  if (mode && !strcasecmp(mode, "manual")) enumerate("key");
}

const rsm_mod rsm_mod_enumeration = {"enumeration", tick, key, NULL, NULL};
