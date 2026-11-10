// Song Stems: takes the guitar or the bass out of the song being played, at a level of your own (SongStems = on). The
// level is a line of the volume mod, kept in RSMods.ini (SongStemsGuitarLevel, SongStemsBassLevel: 0 takes the part out,
// 100 is the song as it is), so a song starts at the level the last one ended at. The part follows the tone in play: a
// bass tone (bass gear, as Extended Range reads it) takes out the bass, any other tone the guitar.
//
// The song without the part comes from rs-stems (stems/), a separate process that separates the song with Demucs on the
// Neural Engine and keeps a cache of a fixed size (SongStemsCacheMB); stems/README.md has the contract between the two.
// When a song is picked (its options, its tuner, or its Play_ event), a thread finds the song's archive and the .wem
// its bank plays (stems_index.c: as the game finds them, so also an archive a mod serves), writes the .wem for the
// helper and asks for the song without the part; the helper writes it from the start into a file this maps. Every block
// the game decodes passes RS Re-ARMed's hook point ak_src_submit, with the source's file ID and the block's position. A
// block of the song whose stretch of the file is ready is mixed there, before the game's speed change and mixer, so
// Riff Repeater, rewinds and the volume sliders work on the result. Until then, and with the mod off, the song is as it
// is. Needs RS Re-ARMed with ak_src_submit (1.12.0 and later); without it the mod stays off.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rsmods.h"
#include "rsmods_files.h"
#include "rsmods_tuning.h"
#include "stems_index.h"
#include "stems_mix.h"

extern char **environ;

enum { PART_GUITAR, PART_BASS };
static const char *const PART_NAME[] = {"guitar", "bass"};
#define HEADER 64
#define RAMP_SECONDS 0.05f        // a change of level, or the backing becoming ready, ramps over this
#define CODEBOOK_SLACK 0x400      // the table says where each codebook starts, not where the last one ends
#define REINDEX_AFTER 30.0        // seconds: a song not found looks again for new archives at most this often

// ------------------------------------------------------------------------------------------------
// The mapped file and the mix (the audio thread)
// ------------------------------------------------------------------------------------------------
typedef struct stem_map {
  uint8_t *base;
  size_t len;
  ino_t ino;
  uint32_t file_id, part, rate, channels, total;
  double retired_at;
  struct stem_map *next;
} stem_map;

static _Atomic(stem_map *) g_map;  // what the audio thread mixes from; NULL: nothing
static _Atomic float g_target = 1.f;  // the level asked for, 0-1
static float g_gain = 1.f;            // where the ramp is (the audio thread's)
static int64_t g_off_pbi, g_off_info, g_off_id, g_off_pos;
static int g_names_ok;

static uint32_t file_id(const uint8_t *src) {
  const uint8_t *pbi = rc_mod_ptr(src + g_off_pbi);
  if (!pbi) return 0;
  const uint8_t *info = rc_mod_ptr(pbi + g_off_info);
  return info ? RC_LD(uint32_t, info + g_off_id) : 0;
}

static int before_submit(rc_ctx *c, void *user) {
  (void)user;
  stem_map *m = atomic_load_explicit(&g_map, memory_order_acquire);
  if (!m) return 0;
  OV_ARGS(a, c);
  const uint8_t *src = OV_PTR(a, const uint8_t);
  int16_t *pcm = OV_PTR(a, int16_t);
  uint32_t frames = OV_U32(a) & 0xffff;
  uint32_t rate = OV_U32(a);
  uint32_t mask = OV_U32(a);
  if (!src || !pcm || !frames || rate != m->rate || (uint32_t)__builtin_popcount(mask) != m->channels) return 0;
  if (file_id(src) != m->file_id) return 0;
  uint32_t pos = RC_LD(uint32_t, src + g_off_pos);
  uint32_t ready = atomic_load_explicit((_Atomic uint32_t *)(m->base + 32), memory_order_acquire);
  if (pos >= m->total || frames > m->total - pos || pos + frames > ready) {
    g_gain = 1.f;  // nothing to mix with yet: the song as it is
    return 0;
  }
  float target = atomic_load_explicit(&g_target, memory_order_relaxed);
  if (target >= 1.f && g_gain >= 1.f) return 0;
  stems_mix(pcm, (const int16_t *)(m->base + HEADER) + (size_t)pos * m->channels, frames, m->channels, &g_gain, target,
            1.f / ((float)m->rate * RAMP_SECONDS));
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Folders and the helper
// ------------------------------------------------------------------------------------------------
static char g_home[700], g_cache[700], g_work[720], g_fifo[720];
static int g_fd = -1, g_starts, g_said_missing;
static double g_started_at, g_beat_at;
static pid_t g_helper;  // the helper started last, until it has ended and been waited for
static long g_budget_sent = -1;
static char g_pending[2048];  // a command the helper hasn't taken yet (the tick's)
static char g_last_cmd[2048]; // the last song asked for, sent again to a helper that has started again
static pthread_mutex_t g_cmd_lock = PTHREAD_MUTEX_INITIALIZER;

static void mkdir_p(const char *path) {
  char p[800];
  snprintf(p, sizeof p, "%s", path);
  for (char *s = p + 1; *s; s++)
    if (*s == '/') {
      *s = 0;
      mkdir(p, 0755);
      *s = '/';
    }
  mkdir(p, 0755);
}

static void folders(void) {
  const char *h = getenv("HOME"), *e = getenv("RS_STEMS_HOME");
  if (e && *e)
    snprintf(g_home, sizeof g_home, "%s", e);
  else
    snprintf(g_home, sizeof g_home, "%s/Library/Application Support/RS Re-ARMed/stems", h ? h : "");
  e = getenv("RS_STEMS_CACHE");
  if (e && *e)
    snprintf(g_cache, sizeof g_cache, "%s", e);
  else
    snprintf(g_cache, sizeof g_cache, "%s/Library/Caches/RS Re-ARMed/stems", h ? h : "");
  snprintf(g_work, sizeof g_work, "%s/work", g_cache);
  snprintf(g_fifo, sizeof g_fifo, "%s/commands", g_cache);
  mkdir_p(g_work);
}

// The helper gets the network guard, as the game has it, and nothing else of DYLD_INSERT_LIBRARIES (a test's key
// injector would take the game's keys), and no RS_KEY_FIFO
static char **helper_env(void) {
  int n = 0;
  while (environ[n]) n++;
  char **env = calloc((size_t)n + 1, sizeof *env);
  if (!env) return NULL;
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!strncmp(environ[i], "RS_KEY_FIFO=", 12)) continue;
    if (!strncmp(environ[i], "DYLD_INSERT_LIBRARIES=", 22)) {
      const char *v = environ[i] + 22, *g = strstr(v, "libnetguard.dylib");
      if (!g) continue;
      const char *s = g;
      while (s > v && s[-1] != ':') s--;
      int len = (int)(g + strlen("libnetguard.dylib") - s);
      if (asprintf(&env[k], "DYLD_INSERT_LIBRARIES=%.*s", len, s) > 0) k++;
      continue;
    }
    if ((env[k] = strdup(environ[i]))) k++;
  }
  return env;
}

// A helper that has ended is waited for, or it would stay in the process table for as long as the game runs
static void reap_helper(void) {
  if (g_helper > 0 && waitpid(g_helper, NULL, WNOHANG) != 0) g_helper = 0;
}

// A helper that doesn't come is tried again three times, 30 s apart, not every second. One that came counts from
// nothing again (heartbeat): the helper ends by itself whenever Song Stems is turned off.
static void start_helper(void) {
  if (g_starts >= 3 || (g_started_at > 0 && rsm_now() - g_started_at < 30)) return;
  char exe[760];
  snprintf(exe, sizeof exe, "%s/rs-stems", g_home);
  if (access(exe, X_OK)) {
    if (!g_said_missing++) rsm_log("song stems: no helper at %s (stems/install.sh): the songs stay as they are", exe);
    return;
  }
  g_starts++;
  g_started_at = rsm_now();
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  for (int f = 0; f < 3; f++) posix_spawn_file_actions_addopen(&fa, f, "/dev/null", f ? O_WRONLY : O_RDONLY, 0);
  // Nothing of the game's open files goes to the helper: only the three opened above
  posix_spawnattr_t at;
  posix_spawnattr_init(&at);
  posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
  char *argv[] = {exe, "serve", NULL};
  char **env = helper_env();
  pid_t pid = 0;
  int e = posix_spawn(&pid, exe, &fa, &at, argv, env ? env : environ);
  if (!e) g_helper = pid;
  posix_spawnattr_destroy(&at);
  posix_spawn_file_actions_destroy(&fa);
  if (env) {
    for (char **p = env; *p; p++) free(*p);
    free(env);
  }
  rsm_log("song stems: starting the helper (%s)", e ? strerror(e) : "started");
}

// All writes to the FIFO are the tick's. 1 if the whole line went.
static int send_line(const char *s) {
  if (g_fd < 0) return 0;
  size_t n = strlen(s);
  ssize_t w = write(g_fd, s, n);
  if (w == (ssize_t)n) return 1;
  if (w < 0 && errno == EAGAIN) return 0;
  close(g_fd);  // the helper has gone
  g_fd = -1;
  return 0;
}

static void heartbeat(void) {
  if (rsm_now() - g_beat_at < 1.0) return;
  g_beat_at = rsm_now();
  reap_helper();
  if (g_fd < 0) {
    g_fd = open(g_fifo, O_WRONLY | O_NONBLOCK);
    if (g_fd < 0) {
      start_helper();
      return;
    }
    fcntl(g_fd, F_SETNOSIGPIPE, 1);  // a helper that ends doesn't take the game with it
    g_starts = 0;
    g_budget_sent = -1;
    pthread_mutex_lock(&g_cmd_lock);
    if (!*g_pending) memcpy(g_pending, g_last_cmd, sizeof g_pending);  // a new helper doesn't know the song in play
    pthread_mutex_unlock(&g_cmd_lock);
    rsm_log("song stems: helper connected");
  }
  long mb = rsm_long("SongStemsCacheMB", 500);
  if (mb != g_budget_sent) {
    char line[64];
    snprintf(line, sizeof line, "budget\t%ld\n", mb);
    if (send_line(line)) g_budget_sent = mb;
  }
  pthread_mutex_lock(&g_cmd_lock);
  if (*g_pending && send_line(g_pending)) g_pending[0] = 0;
  pthread_mutex_unlock(&g_cmd_lock);
  send_line("\n");
}

// ------------------------------------------------------------------------------------------------
// From a song key to its .wem (the worker thread)
// ------------------------------------------------------------------------------------------------
static double g_indexed_at = -1;
static uint8_t g_key[32];
static int g_have_key = -1;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
// What the worker is asked, in two slots: a song picked in the menus (looked for harder: the archives are listed again
// if it isn't found), and a Play_ event's name, which is most often no song at all and mustn't push the other out
static char g_want[128], g_want_event[128];  // empty: nothing waiting
static int g_want_part, g_want_event_part;
static _Atomic uint32_t g_song_id;  // the file ID of the song asked for last, and its part
static _Atomic int g_song_part = -1;

static void build_index(void) {
  double t = rsm_now();
  int n = stems_index_build(g_key);
  g_indexed_at = rsm_now();
  rsm_log("song stems: %d archives with songs, listed in %.2f s%s", n, g_indexed_at - t,
          rsm_files_layer() ? "" : " (files on disk only: this RS Re-ARMed has no file layer)");
}

static int read_key(void) {
  if (g_have_key >= 0) return g_have_key;
  uint64_t a = RC_ADDR(key_psarc_hex);
  g_have_key = a && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(a), g_key);
  if (!g_have_key) rsm_log("song stems: the archives' key isn't known on %s: the mod stays off", rc_target());
  return g_have_key;
}

static int write_file(const char *path, const void *data, size_t len) {
  char tmp[900];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f) return 0;
  int ok = fwrite(data, 1, len, f) == len;
  ok = !fclose(f) && ok;
  if (ok) ok = !rename(tmp, path);
  if (!ok) unlink(tmp);
  return ok;
}

// The game's Vorbis codebook library (ak_vorbis_codebooks), for the helper: "RSCB", the count, each codebook's offset
// from the first, then the data, with some slack after the last one's start (stems/README.md)
static int write_codebooks(void) {
  char path[760];
  struct stat st;
  snprintf(path, sizeof path, "%s/codebooks.bin", g_cache);
  if (!stat(path, &st) && st.st_size > 4096) return 1;
  uint64_t a = RC_ADDR(ak_vorbis_codebooks);
  int64_t n = RC_OFF(ak_vorbis_codebook_count);
  if (!a || n == RC_NO_OFF || n < 1 || n > 4096) return 0;
  const uint8_t *table = (const uint8_t *)(uintptr_t)RC_A(a), *first = rc_mod_ptr(table);
  if (!first) return 0;
  size_t hdr = 8 + 4 * (size_t)n, data = 0;
  uint8_t *out = malloc(hdr + (1u << 20));
  if (!out) return 0;
  memcpy(out, "RSCB", 4);
  uint32_t count = (uint32_t)n;
  memcpy(out + 4, &count, 4);
  for (int64_t i = 0; i < n; i++) {
    const uint8_t *p = rc_mod_ptr(table + i * rc_guest_ptr_size);
    uint32_t off = (uint32_t)(p - first);
    if (!p || p < first || (i && off < data) || off > (1u << 20) - CODEBOOK_SLACK) {
      free(out);
      rsm_log("song stems: the codebook table looks wrong at %lld", (long long)i);
      return 0;
    }
    memcpy(out + 8 + 4 * i, &off, 4);
    data = off;
  }
  data += CODEBOOK_SLACK;
  memcpy(out + hdr, first, data);
  int ok = write_file(path, out, hdr + data);
  free(out);
  rsm_log("song stems: %s %d codebooks", ok ? "wrote" : "couldn't write", (int)n);
  return ok;
}

// The .wem copies of songs before this one (the helper has read them by now)
static void remove_other_wems(const char *keep) {
  DIR *d = opendir(g_work);
  if (!d) return;
  for (struct dirent *e; (e = readdir(d));) {
    size_t l = strlen(e->d_name);
    if (l < 5 || strcmp(e->d_name + l - 4, ".wem") || !strcmp(e->d_name, keep)) continue;
    char path[1100];
    snprintf(path, sizeof path, "%s/%s", g_work, e->d_name);
    unlink(path);
  }
  closedir(d);
}

// The song's .wem written for the helper; its file ID, or 0
static uint32_t extract_wem(const char *archive_path, const char *song, char *wem_path, size_t n) {
  uint32_t id = 0;
  size_t wlen = 0;
  void *w = stems_index_wem(archive_path, song, g_key, &id, &wlen);
  if (!w) return 0;
  snprintf(wem_path, n, "%s/%u.wem", g_work, id);
  struct stat st;
  if (!((!stat(wem_path, &st) && (size_t)st.st_size == wlen) || write_file(wem_path, w, wlen))) id = 0;
  free(w);
  if (id) {
    char name[32];
    snprintf(name, sizeof name, "%u.wem", id);
    remove_other_wems(name);
  }
  return id;
}

static void *worker(void *arg) {
  (void)arg;
  for (;;) {
    char song[sizeof g_want];
    pthread_mutex_lock(&g_lock);
    while (!*g_want && !*g_want_event) pthread_cond_wait(&g_wake, &g_lock);
    int strict = *g_want != 0;
    memcpy(song, strict ? g_want : g_want_event, sizeof song);
    int part = strict ? g_want_part : g_want_event_part;
    (strict ? g_want : g_want_event)[0] = 0;
    pthread_mutex_unlock(&g_lock);
    if (!read_key() || !write_codebooks()) continue;
    if (g_indexed_at < 0) build_index();
    const char *path = stems_index_find(song);
    if (!path && strict && rsm_now() - g_indexed_at > REINDEX_AFTER) {
      build_index();
      path = stems_index_find(song);
    }
    if (!path) {
      if (strict) rsm_log("song stems: no archive with the song %s", song);
      continue;
    }
    char wem[800];
    double t = rsm_now();
    uint32_t id = extract_wem(path, song, wem, sizeof wem);
    if (!id) {
      rsm_log("song stems: no .wem for %s in %s", song, path);
      continue;
    }
    static uint32_t last_id;
    static int last_part = -1;
    atomic_store(&g_song_id, id);
    atomic_store(&g_song_part, part);
    if (id == last_id && part == last_part) continue;
    last_id = id;
    last_part = part;
    pthread_mutex_lock(&g_cmd_lock);
    snprintf(g_pending, sizeof g_pending, "separate\t%s\t%u\t%s\t%s\n", wem, id, PART_NAME[part], song);
    memcpy(g_last_cmd, g_pending, sizeof g_last_cmd);
    pthread_mutex_unlock(&g_cmd_lock);
    rsm_log("song stems: %s without the %s, file %u (%.2f s)", song, PART_NAME[part], id, rsm_now() - t);
  }
  return NULL;
}

// ------------------------------------------------------------------------------------------------
// The tick
// ------------------------------------------------------------------------------------------------
static char g_asked[128];        // the song and part asked for last
static int g_asked_part = -1;
static char g_played[128];       // a Play_ event's name (any thread writes it)
static pthread_mutex_t g_played_lock = PTHREAD_MUTEX_INITIALIZER;
static stem_map *g_retired;
static double g_mapped_check;
static int g_started, g_was_on;

static int g_set_level[2] = {-1, -1};  // a level just set by the key, until RSMods.ini has been read back
static double g_set_at[2];

static int shown_part(void) { return atomic_load(&g_song_part) == PART_BASS ? PART_BASS : PART_GUITAR; }

static int level_of(int part) {
  if (g_set_level[part] >= 0 && rsm_now() - g_set_at[part] < 3.0) return g_set_level[part];
  g_set_level[part] = -1;
  long v = rsm_long(part == PART_BASS ? "SongStemsBassLevel" : "SongStemsGuitarLevel", 0);
  return v < 0 ? 0 : v > 100 ? 100 : (int)v;
}

int rsm_stems_level(void) { return g_names_ok && rsm_on("SongStems") ? level_of(shown_part()) : -1; }

void rsm_stems_set_level(int v) {
  int part = shown_part();
  char text[16];
  v = v < 0 ? 0 : v > 100 ? 100 : v;
  snprintf(text, sizeof text, "%d", v);
  rsm_set("Mod Settings", part == PART_BASS ? "SongStemsBassLevel" : "SongStemsGuitarLevel", text);
  g_set_level[part] = v;
  g_set_at[part] = rsm_now();
}

const char *rsm_stems_label(void) { return atomic_load(&g_song_part) == PART_BASS ? "Bass in Song: " : "Guitar in Song: "; }

static void ask(const char *song, int part, int strict) {
  if (!*song || (!strcasecmp(song, g_asked) && part == g_asked_part)) return;
  if (strict) {
    snprintf(g_asked, sizeof g_asked, "%s", song);
    g_asked_part = part;
  }
  pthread_mutex_lock(&g_lock);
  snprintf(strict ? g_want : g_want_event, sizeof g_want, "%s", song);
  *(strict ? &g_want_part : &g_want_event_part) = part;
  pthread_cond_signal(&g_wake);
  pthread_mutex_unlock(&g_lock);
}

static void retire(stem_map *m) {
  if (!m) return;
  m->retired_at = rsm_now();
  m->next = g_retired;
  g_retired = m;
}

static void unmap_retired(int all) {
  for (stem_map **p = &g_retired; *p;) {
    stem_map *m = *p;
    if (all || rsm_now() - m->retired_at > 3.0) {  // the audio thread has long left a block it read it in
      *p = m->next;
      munmap(m->base, m->len);
      free(m);
    } else {
      p = &m->next;
    }
  }
}

// The helper's file for the song asked for, mapped when it is there, and again when it is a new file
static void map_file(void) {
  uint32_t id = atomic_load(&g_song_id);
  int part = atomic_load(&g_song_part);
  if (!id || part < 0 || rsm_now() - g_mapped_check < 0.5) return;
  g_mapped_check = rsm_now();
  char path[800];
  snprintf(path, sizeof path, "%s/%u.%s.pcm", g_work, id, PART_NAME[part]);
  stem_map *cur = atomic_load(&g_map);
  int fd = open(path, O_RDONLY);
  if (fd < 0) return;
  struct stat st;
  uint8_t h[HEADER];
  if (fstat(fd, &st) || (cur && cur->ino == st.st_ino && cur->file_id == id && cur->part == (uint32_t)part) ||
      st.st_size < HEADER || pread(fd, h, HEADER, 0) != HEADER || memcmp(h, "RSSTEMS1", 8)) {
    close(fd);
    return;
  }
  uint32_t f[6];
  memcpy(f, h + 8, sizeof f);  // file ID, part, rate, channels, total frames, state
  if (f[0] != id || f[1] != (uint32_t)part || f[2] < 8000 || f[2] > 192000 || f[3] < 1 || f[3] > 2 || !f[4] ||
      (uint64_t)st.st_size < HEADER + (uint64_t)f[4] * f[3] * 2) {
    close(fd);
    return;
  }
  void *base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  stem_map *m = base == MAP_FAILED ? NULL : calloc(1, sizeof *m);
  if (!m) {
    if (base != MAP_FAILED) munmap(base, (size_t)st.st_size);
    return;
  }
  *m = (stem_map){base, (size_t)st.st_size, st.st_ino, id, (uint32_t)part, f[2], f[3], f[4], 0, NULL};
  retire(atomic_exchange_explicit(&g_map, m, memory_order_acq_rel));
  rsm_log("song stems: mapped %s (%u frames at %u Hz)", path, f[4], f[2]);
}

static int picked_song_menu(const char *m) {
  size_t l = strlen(m);
  return (l > 11 && !strcmp(m + l - 11, "SongOptions")) || strstr(m, "PreSongTuner") || strstr(m, "PreSMTuner") ||
         rsm_menu_in(RSM_SONG_MODES);
}

static void tick(void) {
  int on = g_names_ok && rsm_on("SongStems");
  if (!on) {
    if (g_was_on) {
      retire(atomic_exchange(&g_map, NULL));
      g_asked[0] = 0;
      g_asked_part = -1;
      if (g_fd >= 0) close(g_fd);  // no more heartbeats: the helper ends by itself
      g_fd = -1;
      rsm_log("song stems: off");
    }
    g_was_on = 0;
    unmap_retired(0);
    return;
  }
  if (!g_started) {
    folders();
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL)) return;  // no thread now: tried again with the next frame
    pthread_detach(t);
    g_started = 1;
  }
  g_was_on = 1;
  heartbeat();

  int part = rsm_tone_bass() ? PART_BASS : PART_GUITAR;
  const char *menu = rsm_menu();
  if (picked_song_menu(menu)) ask(rsm_song_key(), part, 1);
  char played[sizeof g_played];
  pthread_mutex_lock(&g_played_lock);
  memcpy(played, g_played, sizeof played);
  g_played[0] = 0;
  pthread_mutex_unlock(&g_played_lock);
  if (*played) ask(played, part, 0);

  atomic_store_explicit(&g_target, (float)level_of(shown_part()) / 100.f, memory_order_relaxed);
  map_file();
  unmap_retired(0);
}

// Play_<key> starts a song (and many other sounds): a key with no '_' in it, or the song key of the previews, may be
// a song; the worker knows. Any thread.
static int post_event(const char *name) {
  if (strncmp(name, "Play_", 5)) return 0;
  const char *k = name + 5;
  if (!*k || strchr(k, '_') || strlen(k) >= sizeof g_played) return 0;
  pthread_mutex_lock(&g_played_lock);
  snprintf(g_played, sizeof g_played, "%s", k);
  pthread_mutex_unlock(&g_played_lock);
  return 0;
}

__attribute__((constructor)) static void init(void) {
  g_off_pbi = RC_OFF(ak_src_pbi);
  g_off_info = RC_OFF(ak_pbi_src_info);
  g_off_id = RC_OFF(ak_src_info_file_id);
  g_off_pos = RC_OFF(ak_src_position);
  if (g_off_pbi == RC_NO_OFF || g_off_info == RC_NO_OFF || g_off_id == RC_NO_OFF || g_off_pos == RC_NO_OFF) return;
  if (rsm_hook("ak_src_submit", before_submit, NULL)) return;
  g_names_ok = 1;
}

const rsm_mod rsm_mod_song_stems = {"song stems", tick, NULL, NULL, post_event};
