// RSMods: your own plug-in in place of the game's tones (ToneHost), with rs-tonehost (tonehost/) playing the guitar.
// On a tone change the game sets every knob of the gear as a Wwise RTPC by name (Amp_GB38_Gain,
// Rack_StudioCompressor_Ratio), then the tone's balance and P1_NoiseFloor, all in one go, as the tone changes in the
// song. This collects them and writes one "gear ..." line to the host's FIFO, and where the song is. While the host is
// there the game's own guitar is silent (Mixer_Player1 at 0: notes still score), and the guitar fader goes to the
// host as "mixer VALUE". With no host, or ToneHost off, the game plays its own tones. The empty line each second shows a
// host gone away as a failed write, and a host that starts late gets the tone in play. The game starts the host if it
// is installed (tonehost/start.sh); the host ends by itself when those lines stop, so also after a crash.
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rc_mid.h"
#include "rsmods.h"
#include "rsmods_files.h"
#include "rsmods_tuning.h"
#include "song_plan.h"
#include "stems_index.h"

#define FADER "Mixer_Player1"

static char g_line[8192], g_last[8192], g_path[900];
static size_t g_len, g_last_len;
static int g_fd = -1, g_frames, g_was_on;
static void send_plan(void);
static int g_self, g_remute;  // our own call is under way; the game has just set the fader
static float g_fader = -1;    // what the game or a key asked for
static char g_start[900];     // the host's start script
static int g_starts;
static double g_started_at;
static pid_t g_starter;  // the start script run last, until it has ended and been waited for

extern char **environ;

// The start script ends as soon as the host is launched: waited for, or it would stay in the process table for as long
// as the game runs
static void reap_starter(void) {
  if (g_starter > 0 && waitpid(g_starter, NULL, WNOHANG) != 0) g_starter = 0;
}

// A host that doesn't come is tried again three times, 30 s apart, not every second. One that came counts from nothing
// again (tick): the host ends by itself whenever ToneHost is turned off.
static void start_host(void) {
  if (g_starts >= 3 || (g_started_at > 0 && rsm_now() - g_started_at < 30) || access(g_start, X_OK)) return;
  g_starts++;
  g_started_at = rsm_now();
  char *argv[] = {"/bin/bash", g_start, NULL};
  pid_t pid = 0;
  int e = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
  if (!e) g_starter = pid;
  rsm_log("tone host: starting it (%s)", e ? "failed" : "started");
}

static int is_gear(const char *n) { return song_plan_is_gear(n); }

static void send_text(const char *text, size_t n) {
  if (g_fd < 0) return;
  if (write(g_fd, text, n) < 0 && errno != EAGAIN) {
    close(g_fd);
    g_fd = -1;
  }
}

static void send_fader(void) {
  char text[32];
  send_text(text, (size_t)snprintf(text, sizeof text, "mixer %g\n", g_fader));
}

// Where the song is, for a host that plays a change it has seen before a little ahead of the game: the game says a tone
// only as it changes, and a plug-in's state takes half a second to load. Every other frame, and ahead of a tone.
static double g_song_at = -1;

static void send_time(void) {
  const char *key = rsm_song_key();
  char text[192];
  if (g_song_at >= 0 && *key) send_text(text, (size_t)snprintf(text, sizeof text, "time %s %.3f\n", key, g_song_at));
}

static int before_rtpc(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  const char *name = OV_PTR(a, const char);
  float value = OV_F32(a);
  if (!name) return 0;
  if (!strcmp(name, FADER)) {
    if (!g_self) {
      g_fader = value;
      send_fader();
      g_remute = 1;
    }
  } else if (!strcmp(name, "P1_NoiseFloor")) {  // the last of a tone's values
    if (g_len) {
      if (g_len < sizeof g_line - 1) g_line[g_len++] = '\n';
      send_time();
      send_text(g_line, g_len);
      memcpy(g_last, g_line, g_len);
      g_last_len = g_len;
    }
    g_len = 0;
  } else if (is_gear(name)) {
    if (!g_len) g_len = (size_t)snprintf(g_line, sizeof g_line, "gear");
    if (g_len < sizeof g_line - 64) {
      // a knob that doesn't fit whole is left out: snprintf's result is what it would have written, not what it did
      int w = snprintf(g_line + g_len, sizeof g_line - g_len, " %s=%g", name, value);
      if (w > 0 && (size_t)w < sizeof g_line - g_len)
        g_len += (size_t)w;
      else
        g_line[g_len] = 0;
    }
  }
  return 0;
}

static void set_fader(float v) {
  g_self = 1;
  ak_set_rtpc(FADER, v, AK_INVALID_GAME_OBJECT, 0, AK_CURVE_LINEAR);
  ak_set_rtpc(FADER, v, RS_SONG_GAME_OBJECT, 0, AK_CURVE_LINEAR);
  g_self = 0;
}

static void give_back(void) {
  if (g_was_on) set_fader(g_fader >= 0 ? g_fader : 100.f);
  g_was_on = 0;
}

int rsm_guitar_fader(float *value) {
  if (g_fd < 0 || g_fader < 0) return 0;
  *value = g_fader;
  return 1;
}

static void tick(void) {
  if (!rsm_on("ToneHost")) {
    if (g_fd >= 0) {
      close(g_fd);
      g_fd = -1;
    }
    give_back();
    return;
  }
  if (g_remute && g_fd >= 0) {
    g_remute = 0;
    set_fader(0.f);
  }
  g_song_at = rsm_song_time();  // -1 with no song playing, and while one is paused
  if (++g_frames % 2 == 0) send_time();
  else if (g_fd >= 0) send_plan();
  if (g_frames % 60) return;
  reap_starter();
  send_text("\n", 1);
  if (g_fd < 0) {
    g_fd = open(g_path, O_WRONLY | O_NONBLOCK);  // fails with no reader: no host, no change
    if (g_fd >= 0) {
      rsm_log("tone host found");
      g_starts = 0;
      if (g_last_len) send_text(g_last, g_last_len);
      if (g_fader >= 0) send_fader();
    } else
      start_host();
  }
  if (g_fd >= 0) {
    set_fader(0.f);
    g_was_on = 1;
  } else
    give_back();
}

// ---- A song's tones ahead of the game ----
// The game says a tone only as it changes, too late for a plug-in whose state takes half a second to load. But a loaded
// song has its changes (sng_song_tones: a time and a slot each), and the arrangement's manifest has each slot's tone, so
// the host gets both as the song loads: "plan SONG", a "plantone SLOT KNOB=VALUE ..." for each slot the song uses (0
// Tone_Base, 1 to 4 Tone_A to Tone_D), "planat TIME:SLOT ..." and "planend". Which manifest: the song's arrangements
// whose levels are as many as the loaded song's and which have a tone in every slot its changes name. Where they differ in a slot the song uses, no plan: the host then
// learns the song's changes as they come.
enum { MAX_CHANGES = 128, PLAN_TEXT = 6 * SONG_PLAN_GEAR };
static pthread_mutex_t g_plan_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_plan_wake = PTHREAD_COND_INITIALIZER;
static struct {
  float at;
  int slot;
} g_changes[MAX_CHANGES];
static int g_nchanges, g_levels, g_song_value = -1;
static unsigned g_loaded, g_planned;  // songs loaded, and the last one a plan was looked for
static char g_plan[PLAN_TEXT];        // the lines to send, or ""
static size_t g_plan_sent;

// The loaded song's own offsets: by name where RS Re-ARMed has it, else as its tables have it for the builds before
static int64_t tones_offset(void) {
  if (RC_HAS_OFF(sng_song_tones)) return RC_OFF(sng_song_tones);
  return !strcmp(rc_target(), "mac-x64") ? 0x190 : !strcmp(rc_target(), "win-x86") ? 0xdc : RC_NO_OFF;
}

static size_t vector_of(const uint8_t *song, int64_t at, const uint8_t **first) {
  const uint8_t *b = rc_mod_ptr(song + at + RC_OFF(std_vector_begin)), *e = rc_mod_ptr(song + at + RC_OFF(std_vector_end));
  *first = b;
  return b && e > b ? (size_t)(e - b) : 0;
}

// After the game has loaded a song (seven_pp1: SNG_Song_PostProcess is done)
static void song_loaded(rc_mid_frame *f, void *user) {
  (void)user;
  const uint8_t *song = (const uint8_t *)(uintptr_t)rc_mid_get(f, g_song_value), *tones, *levels;
  if (!song || g_fd < 0) return;
  size_t bytes = vector_of(song, tones_offset(), &tones), level_bytes = vector_of(song, RC_OFF(sng_song_levels), &levels);
  if (!level_bytes) return;  // the song's vocals, loaded after the arrangement: no notes, and no tones
  pthread_mutex_lock(&g_plan_lock);
  g_nchanges = 0;
  for (size_t i = 0; i + 8 <= bytes && g_nchanges < MAX_CHANGES; i += 8) {
    g_changes[g_nchanges].at = RC_LD(float, tones + i);
    g_changes[g_nchanges++].slot = RC_LD(int32_t, tones + i + 4) + 1;
  }
  g_levels = (int)(level_bytes / (size_t)RC_OFF(sng_level_size));
  g_loaded++;
  pthread_cond_signal(&g_plan_wake);
  pthread_mutex_unlock(&g_plan_lock);
}

// The plan's lines for the song, from its archive; "" when its manifests don't say
static void make_plan(const char *song, const uint8_t key[32], char *out, size_t n) {
  *out = 0;
  // The song's archive, and for a Rocksmith 1 DLC song, whose archive has its audio alone, the import pack's
  char path[2048] = "";
  if (stems_index_listed() < 0) stems_index_build(key);
  if (!stems_index_path(song, path, sizeof path)) {
    stems_index_build(key);  // a song added since
    if (!stems_index_path(song, path, sizeof path)) *path = 0;
  }
  const char *const archives[] = {path, "dlc/rs1compatibilitydlc_m.psarc", "dlc/rs1compatibilitydlc_p.psarc"};
  pthread_mutex_lock(&g_plan_lock);
  int levels = g_levels, changes = g_nchanges, used[SONG_PLAN_SLOTS] = {1};
  static __typeof__(g_changes) ch;
  memcpy(ch, g_changes, sizeof ch);
  pthread_mutex_unlock(&g_plan_lock);
  for (int i = 0; i < changes; i++)
    if (ch[i].slot > 0 && ch[i].slot < SONG_PLAN_SLOTS) used[ch[i].slot] = 1;
  // [0] the manifests whose levels are the loaded song's, [1] all of the song's, for a song where none says so
  static song_plan got[2], one;
  int found[2] = {0, 0}, same[2] = {1, 1};
  for (size_t at = 0; at < sizeof archives / sizeof *archives && !found[1]; at++) {
    rsm_psarc *a = *archives[at] ? rsm_psarc_open_io(archives[at], key, &rsm_file_io) : NULL;
    for (int i = 0; a && i < rsm_psarc_files(a); i++) {
      const char *name = rsm_psarc_name(a, i);
      if (strncasecmp(name, "manifests/", 10) || !rsm_is_song_manifest(name, song)) continue;
      char *json = rsm_psarc_read(a, i, NULL);
      int fits = json && song_plan_parse(json, &one);
      for (int t = 0; t < SONG_PLAN_SLOTS; t++) fits = fits && (!used[t] || *one.gear[t]);  // not a bass's for a guitar's song
      for (int k = fits ? one.max_difficulty + 1 != levels : 2; k < 2; k++) {
        for (int s = 0; found[k] && s < SONG_PLAN_SLOTS; s++)
          same[k] = same[k] && (!used[s] || !strcmp(one.gear[s], got[k].gear[s]));
        if (!found[k]++) got[k] = one;
      }
      free(json);
    }
    rsm_psarc_close(a);
  }
  int k = found[0] ? 0 : 1;
  if (!found[k] || !same[k]) {
    fprintf(stderr, "[rsmods] tone host: no plan for %s (%d levels, %d changes): %s\n", song, levels, changes,
            found[k] ? "its arrangements' tones differ" : "no manifest with its tones");
    return;
  }
  size_t len = (size_t)snprintf(out, n, "plan %s\n", song);
  for (int s = 0; s < SONG_PLAN_SLOTS; s++)
    if (used[s] && *got[k].gear[s]) len += (size_t)snprintf(out + len, n - len, "plantone %d%s\n", s, got[k].gear[s]);
  len += (size_t)snprintf(out + len, n - len, "planat");
  for (int i = 0; i < changes && len + 32 < n; i++) len += (size_t)snprintf(out + len, n - len, " %.3f:%d", ch[i].at, ch[i].slot);
  snprintf(out + len, n - len, "\nplanend\n");
  fprintf(stderr, "[rsmods] tone host: %s: a plan of %d changes, %d levels, from %d of its manifests%s\n", song, changes, levels,
          found[k], k ? " (none with as many levels)" : "");
}

static void *planner(void *arg) {
  (void)arg;
  static uint8_t key[32];
  for (int have_key = 0;;) {
    pthread_mutex_lock(&g_plan_lock);
    while (g_planned == g_loaded) pthread_cond_wait(&g_plan_wake, &g_plan_lock);
    unsigned load = g_loaded;
    pthread_mutex_unlock(&g_plan_lock);
    if (!have_key) {  // read once a song has loaded: the game's memory isn't there yet when the mods load
      uint64_t hex = RC_ADDR(key_psarc_hex);
      have_key = hex && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(hex), key);
      if (!have_key) {
        fprintf(stderr, "[rsmods] tone host: the archives' key isn't known on %s: tones change as the game says them\n", rc_target());
        return NULL;
      }
    }
    static char text[PLAN_TEXT];
    char song[160];
    snprintf(song, sizeof song, "%s", rsm_song_key());
    make_plan(song, key, text, sizeof text);
    pthread_mutex_lock(&g_plan_lock);
    g_planned = load;
    if (load == g_loaded) {  // else another song has loaded since
      memcpy(g_plan, text, sizeof g_plan);
      g_plan_sent = 0;
    }
    pthread_mutex_unlock(&g_plan_lock);
  }
  return NULL;
}

// A line of the plan each frame: the FIFO takes a line whole
static void send_plan(void) {
  pthread_mutex_lock(&g_plan_lock);
  const char *line = g_plan + g_plan_sent, *end = strchr(line, '\n');
  if (*line && end) {
    send_text(line, (size_t)(end - line) + 1);
    g_plan_sent += (size_t)(end - line) + 1;
  }
  pthread_mutex_unlock(&g_plan_lock);
}

__attribute__((constructor)) static void init(void) {
  const char *p = getenv("RS_TONEHOST_FIFO"), *h = getenv("HOME");
  if (p) snprintf(g_path, sizeof g_path, "%s", p);
  else snprintf(g_path, sizeof g_path, "%s/Library/Application Support/RS Re-ARMed/tonehost/commands", h ? h : "");
  size_t n = strlen(g_path);
  while (n && g_path[n - 1] != '/') n--;
  snprintf(g_start, sizeof g_start, "%.*sstart.sh", (int)n, g_path);
  signal(SIGPIPE, SIG_IGN);
  rsm_hook("ak_set_rtpc_name", before_rtpc, NULL);
  // The plan needs the song the game loads; without that point the host learns a song's changes as they come
  int point = tones_offset() != RC_NO_OFF && RC_HAS_OFF(sng_song_levels) ? rc_mid_add_name("seven_pp1", song_loaded, NULL) : -1;
  if (point >= 0 && (g_song_value = rc_mid_value(point, "song")) >= 0) {
    pthread_t t;
    if (!pthread_create(&t, NULL, planner, NULL)) pthread_detach(t);
  }
}

const rsm_mod rsm_mod_tone_host = {"tone_host", tick, NULL, NULL, NULL};
