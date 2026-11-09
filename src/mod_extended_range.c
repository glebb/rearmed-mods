// RSMods: Extended Range Mode (ExtendedRangeMod.cpp, ExtendedRangeMode.cpp, SongTuning.cpp). In a song tuned low enough
// for a seven-string guitar or a five-string bass, the strings take the extended range colors: with
// CustomStringColors = 1 ZZ's (ZagatoZee's: the game's colors a string down, a green for the low B), with 2 the
// player's colorblind colors (string0_CB and so on). ExtendedRange = on, [Mod Settings] ExtendedRangeModeAt (-5: B, as
// semitones from E), ExtendedRangeDropTuning, ExtendedRangeFixBassTuning; the key ([Keybinds] ToggleExtendedRangeKey, E)
// turns it on or off for the song. In the song and its tuner, as RSMods.
//
// RSMods reads the arrangement's tuning from the tuner in the game's memory; the Mac build has no name for it. The tuning
// is in the song's archive too: each arrangement's manifest has it (rsmods_tuning.c). A thread lists the manifests of
// every archive (dlc/, its folders, songs.psarc) once, and again when a song isn't in the list (one added since), and
// reads the song's when its key changes (the song key of the preview, rsmods.c). The archives' table of contents is
// encrypted with the game's key, which the game has by name (key_psarc_hex).
//
// Which arrangement: the tone says guitar or bass (a bass tone's gear is Bass_*, in the RTPCs the game sets when the tone
// loads), and the song's arrangements for that instrument decide; when they differ (a low lead, a standard rhythm) the
// colors change only if all of them call for it, and the key is there for the rest.
//
// The colors themselves are mod_colors.c's (rsm_extended_range): RSMods turns the game's colorblind mode on and writes
// the colorblind bank; here both banks get the colors, and the notes the colorblind rows.
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rsmods.h"
#include "rsmods_tuning.h"

#define MAX_ARRANGEMENTS 16
#define REINDEX_AFTER 30.0  // seconds: a song not found looks again for new archives at most this often

// The pre-song tuners (GameState.hpp: preSongTuners)
static const char *const PRE_SONG_TUNERS[] = {
    "SelectionListDialog", "LearnASong_PreSongTuner", "LearnASong_PreSongTunerMP", "NonStopPlay_PreSongTuner",
    "NonStopPlay_PreSongTunerMP", "ScoreAttack_PreSongTuner", "SessionMode_PreSMTunerMP", "SessionMode_PreSMTuner",
    "Duet_PreSongTuner", "H2H_PreSongTuner", "PreGame_GETuner", NULL};

// The archives' manifests (the worker's alone)
typedef struct {
  char *path;
  int n;
  char **names;
} archive;
static archive *g_archives;
static int g_narchives;
static double g_indexed_at = -1;

// Between the worker and the tick
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static char g_wanted[128];   // the song key to look up
static char g_found_for[128];  // the key of g_arr
static rsm_arrangement g_arr[MAX_ARRANGEMENTS];
static int g_narr;
static uint8_t g_key[32];
static int g_have_key = -1;

static atomic_int g_bass;          // the tone in play is a bass tone
static atomic_int g_batch_bass;    // the tone being set has bass gear
static atomic_int g_active;        // the extended range colors are on now
static int g_flip;                 // the key turned it the other way for this song
static char g_flip_for[128];

int rsm_extended_range(void) { return atomic_load(&g_active); }
int rsm_tone_bass(void) { return atomic_load(&g_bass); }

static void free_index(void) {
  for (int i = 0; i < g_narchives; i++) {
    for (int k = 0; k < g_archives[i].n; k++) free(g_archives[i].names[k]);
    free(g_archives[i].names);
    free(g_archives[i].path);
  }
  free(g_archives);
  g_archives = NULL;
  g_narchives = 0;
}

static void index_archive(const char *path) {
  rsm_psarc *a = rsm_psarc_open(path, g_key);
  if (!a) return;
  archive r = {strdup(path), 0, NULL};
  int n = rsm_psarc_files(a);
  for (int i = 0; i < n; i++) {
    const char *nm = rsm_psarc_name(a, i);
    size_t l = strlen(nm);
    if (strncmp(nm, "manifests/", 10) || l < 5 || strcmp(nm + l - 5, ".json")) continue;
    char **more = realloc(r.names, (size_t)(r.n + 1) * sizeof *more);
    if (!more) break;
    r.names = more;
    r.names[r.n++] = strdup(nm);
  }
  rsm_psarc_close(a);
  archive *more = realloc(g_archives, (size_t)(g_narchives + 1) * sizeof *more);
  if (!more || !r.n) {
    for (int k = 0; k < r.n; k++) free(r.names[k]);
    free(r.names);
    free(r.path);
    if (more) g_archives = more;
    return;
  }
  g_archives = more;
  g_archives[g_narchives++] = r;
}

static void index_folder(const char *dir, int depth) {
  DIR *d = opendir(dir);
  if (!d) return;
  for (struct dirent *e; (e = readdir(d));) {
    if (e->d_name[0] == '.') continue;
    char path[2048];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    if (stat(path, &st)) continue;
    size_t l = strlen(e->d_name);
    if (S_ISDIR(st.st_mode) && depth < 8)
      index_folder(path, depth + 1);
    else if (S_ISREG(st.st_mode) && l > 6 && !strcasecmp(e->d_name + l - 6, ".psarc"))
      index_archive(path);
  }
  closedir(d);
}

static void build_index(void) {
  double t = rsm_now();
  free_index();
  index_folder("dlc", 0);
  index_archive("songs.psarc");
  g_indexed_at = rsm_now();
  rsm_log("extended range: %d archives with manifests, listed in %.2f s", g_narchives, g_indexed_at - t);
}

// The song's arrangements with a tuning, from its archive; how many
static int lookup(const char *song, rsm_arrangement *out, int max) {
  int n = 0;
  for (int i = 0; i < g_narchives && !n; i++) {
    rsm_psarc *a = NULL;
    for (int k = 0; k < g_archives[i].n && n < max; k++) {
      if (!rsm_is_song_manifest(g_archives[i].names[k], song)) continue;
      if (!a && !(a = rsm_psarc_open(g_archives[i].path, g_key))) break;
      for (int f = 0; f < rsm_psarc_files(a); f++) {
        if (strcmp(rsm_psarc_name(a, f), g_archives[i].names[k])) continue;
        char *json = rsm_psarc_read(a, f, NULL);
        if (json && rsm_manifest_arrangement(json, &out[n])) {
          rsm_log("extended range: %s %s %d %d %d %d %d %d, %.0f cents (%s)", song, out[n].name, out[n].tuning[0],
                  out[n].tuning[1], out[n].tuning[2], out[n].tuning[3], out[n].tuning[4], out[n].tuning[5],
                  out[n].cents, g_archives[i].path);
          n++;
        }
        free(json);
        break;
      }
    }
    rsm_psarc_close(a);
  }
  return n;
}

static void *worker(void *arg) {
  (void)arg;
  for (;;) {
    char song[sizeof g_wanted];
    pthread_mutex_lock(&g_lock);
    while (!*g_wanted) pthread_cond_wait(&g_wake, &g_lock);
    memcpy(song, g_wanted, sizeof song);
    pthread_mutex_unlock(&g_lock);
    static rsm_arrangement arr[MAX_ARRANGEMENTS];
    if (g_indexed_at < 0) build_index();
    int n = lookup(song, arr, MAX_ARRANGEMENTS);
    if (!n && rsm_now() - g_indexed_at > REINDEX_AFTER) {
      build_index();
      n = lookup(song, arr, MAX_ARRANGEMENTS);
    }
    if (!n) rsm_log("extended range: no tuning found for %s", song);
    pthread_mutex_lock(&g_lock);
    memcpy(g_arr, arr, sizeof g_arr);
    g_narr = n;
    snprintf(g_found_for, sizeof g_found_for, "%s", song);
    if (!strcmp(g_wanted, song)) g_wanted[0] = 0;
    pthread_mutex_unlock(&g_lock);
  }
  return NULL;
}

static int read_key(void) {
  if (g_have_key >= 0) return g_have_key;
  uint64_t a = RC_ADDR(key_psarc_hex);
  g_have_key = a && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(a), g_key);
  if (!g_have_key) fprintf(stderr, "[rsmods] the archives' key isn't known on %s: Extended Range stays off\n", rc_target());
  return g_have_key;
}

// The rule for the instrument in play: every one of its arrangements (or of all, if it has none) calls for it
static int decide(const rsm_arrangement *arr, int n) {
  long at = rsm_long("ExtendedRangeModeAt", -5);
  int drop = rsm_on("ExtendedRangeDropTuning"), fix = rsm_on("ExtendedRangeFixBassTuning");
  int bass = atomic_load(&g_bass), mine = 0, yes = 0;
  for (int i = 0; i < n; i++)
    if (rsm_is_bass(&arr[i]) == bass) {
      mine++;
      yes += rsm_extended_range_rule(&arr[i], at, drop, fix);
    }
  if (!mine)
    for (int i = 0; i < n; i++, mine++) yes += rsm_extended_range_rule(&arr[i], at, drop, fix);
  return mine && yes == mine;
}

static void tick(void) {
  static int started, shown = -1;
  int on = rsm_on("ExtendedRange") && read_key();
  const char *song = rsm_song_key();
  int active = 0;
  if (on && *song) {
    if (!started) {
      pthread_t t;
      if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
      started = 1;
    }
    pthread_mutex_lock(&g_lock);
    int have = !strcmp(g_found_for, song);
    if (!have && strcmp(g_wanted, song)) {
      snprintf(g_wanted, sizeof g_wanted, "%s", song);
      pthread_cond_signal(&g_wake);
    }
    int er = have && decide(g_arr, g_narr);
    pthread_mutex_unlock(&g_lock);
    if (strcmp(g_flip_for, song)) g_flip = 0;
    active = (er != g_flip) && (rsm_menu_in(RSM_SONG_MODES) || rsm_menu_in(PRE_SONG_TUNERS));
  }
  atomic_store(&g_active, active);
  if (active != shown) {
    shown = active;
    rsm_log("extended range: %s (%s, %s)", active ? "on" : "off", song, atomic_load(&g_bass) ? "bass" : "guitar");
  }
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("ToggleExtendedRangeKey") || !rsm_on("ExtendedRange")) return;
  snprintf(g_flip_for, sizeof g_flip_for, "%s", rsm_song_key());
  g_flip = !g_flip;
}

// The tone's RTPCs: gear names say guitar or bass, the noise floor ends the tone (as mod_tone_host.c reads them)
static int before_rtpc(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  const char *name = OV_PTR(a, const char);
  if (!name) return 0;
  if (!strncmp(name, "Bass_", 5)) {
    atomic_store(&g_batch_bass, 1);
  } else if (!strcmp(name, "P1_NoiseFloor")) {
    atomic_store(&g_bass, atomic_load(&g_batch_bass));
    atomic_store(&g_batch_bass, 0);
  }
  return 0;
}

__attribute__((constructor)) static void init(void) { rsm_hook("ak_set_rtpc_name", before_rtpc, NULL); }

const rsm_mod rsm_mod_extended_range = {"extended_range", tick, key, NULL, NULL};
