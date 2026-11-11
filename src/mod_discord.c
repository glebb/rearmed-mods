// Discord Rich Presence (not in RSMods): Discord shows "Playing RS Re-ARMed" on your profile, and under it the song
// you play, its artist and the game mode, with the time since the song began (DiscordRichPresence = on). In the menus
// it says so, with the time since the game started.
//
// The name is a Discord application's (DISCORD_APPLICATION, "RS Re-ARMed"; [Mod Settings] DiscordApplicationID names
// another), and the picture its art asset "logo" (gui/icon/make_discord_logo.py draws it). The game thread only says
// what is going on: the menu and the song's key (rsmods.c). A thread of the mod's own does the rest, so nothing here
// can hold a frame up: it finds the song's archive (stems_index.c, the list Song Stems uses), reads the title and
// the artist from its manifest (song_info.c) and talks to Discord (discord_ipc.c). No Discord running: it looks
// again every RECONNECT_AFTER seconds. Discord takes five activities in twenty seconds, so changes that come faster
// wait, and only the last one is sent. The activity goes when the mod is turned off and when the game quits.
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "discord_ipc.h"
#include "rsmods.h"
#include "rsmods_files.h"
#include "song_info.h"
#include "stems_index.h"

#define DISCORD_APPLICATION "1570119228806267021"
#define RECONNECT_AFTER 15.0  // seconds
#define REINDEX_AFTER 30.0    // seconds: a song not found looks again for new archives at most this often
#define SENDS 4               // activities in SENDS_WINDOW seconds, one fewer than Discord takes
#define SENDS_WINDOW 20.0

// What the game thread tells the worker
typedef struct {
  int on, in_song;
  char application[32];
  char song[128];  // the song's key
  char mode[32];   // "Learn a Song", or ""
  int64_t start;   // Unix seconds: the song's start, or the game's
} presence;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static presence g_want;
static int g_changed;

// ------------------------------------------------------------------------------------------------
// The worker thread
// ------------------------------------------------------------------------------------------------
static uint8_t g_key[32];
static int g_have_key = -1;
static double g_indexed_at;

static void list_archives(void) {
  double t = rsm_now();
  int n = stems_index_build(g_key);
  g_indexed_at = rsm_now();
  rsm_log("discord: %d archives with songs, listed in %.2f s", n, g_indexed_at - t);
}

// The song's names from its archive; 0 when it isn't found
static int song_names(const char *song, song_info *out) {
  if (g_have_key < 0) {
    uint64_t a = RC_ADDR(key_psarc_hex);
    g_have_key = a && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(a), g_key);
    if (!g_have_key) rsm_log("discord: the archives' key isn't known on %s: songs show by their key", rc_target());
  }
  if (!g_have_key) return 0;
  char path[2048];
  if (stems_index_listed() < 0) list_archives();  // Song Stems may have listed them already
  if (!stems_index_path(song, path, sizeof path)) {
    if (rsm_now() - g_indexed_at < REINDEX_AFTER) return 0;
    list_archives();
    if (!stems_index_path(song, path, sizeof path)) return 0;
  }
  if (song_info_read(path, song, g_key, out)) return 1;
  // a Rocksmith 1 DLC song: its audio is in the game's own archive, its manifest in the import pack's
  static const char *const PACKS[] = {"dlc/rs1compatibilitydlc_m.psarc", "dlc/rs1compatibilitydlc_p.psarc"};
  for (size_t i = 0; i < sizeof PACKS / sizeof PACKS[0]; i++)
    if (song_info_read(PACKS[i], song, g_key, out)) return 1;
  return 0;
}

static void activity_of(const presence *p, const song_info *names, discord_activity *a) {
  memset(a, 0, sizeof *a);
  snprintf(a->image, sizeof a->image, "logo");
  snprintf(a->image_text, sizeof a->image_text, "%s",
           strstr(rc_target(), "win") ? "RS Re-ARMed Learn & Play" : "RS Re-ARMed");
  a->start = p->start;
  if (!p->in_song) {
    snprintf(a->details, sizeof a->details, "In the menus");
    return;
  }
  const char *title = names && *names->title ? names->title : p->song, *artist = names ? names->artist : "";
  snprintf(a->details, sizeof a->details, "%s", *title ? title : "Playing a song");
  snprintf(a->state, sizeof a->state, "%s%s%s", artist, *artist && *p->mode ? " · " : "", p->mode);
}

static void *worker(void *arg) {
  (void)arg;
  int fd = -1, have_sent = 0, have_names = 0;
  unsigned nonce = 0;
  double tried = -RECONNECT_AFTER, sends[SENDS] = {0};
  for (int i = 0; i < SENDS; i++) sends[i] = -SENDS_WINDOW;
  char named[128] = "";  // the song `names` are of
  song_info names;
  discord_activity sent;
  for (;;) {
    presence p;
    pthread_mutex_lock(&g_lock);
    if (!g_changed) {
      struct timespec until;
      clock_gettime(CLOCK_REALTIME, &until);
      until.tv_sec += 1;
      pthread_cond_timedwait(&g_wake, &g_lock, &until);
    }
    g_changed = 0;
    p = g_want;
    pthread_mutex_unlock(&g_lock);

    if (!p.on) {
      if (fd >= 0) rsm_log("discord: off");
      discord_close(fd);
      fd = -1;
      continue;
    }
    if (fd < 0) {
      if (rsm_now() - tried < RECONNECT_AFTER) continue;
      tried = rsm_now();
      fd = discord_connect(NULL, p.application);
      if (fd < 0) continue;
      have_sent = 0;
      rsm_log("discord: connected as application %s", p.application);
    }
    char error[512];
    int alive = discord_poll(fd, error, sizeof error);
    if (*error) rsm_log("discord: %s", error);
    if (!alive) {
      rsm_log("discord: the connection ended");
      discord_close(fd);
      fd = -1;
      continue;
    }
    if (p.in_song && *p.song && strcmp(p.song, named)) {
      have_names = song_names(p.song, &names);
      // a song that isn't found is looked for again when the archives may be listed anew
      if (have_names || rsm_now() - g_indexed_at < REINDEX_AFTER) snprintf(named, sizeof named, "%s", p.song);
      rsm_log("discord: %s is %s%s%s", p.song, have_names ? names.title : "not found", have_names ? " by " : "",
              have_names ? names.artist : "");
    }
    discord_activity a;
    activity_of(&p, have_names && !strcmp(p.song, named) ? &names : NULL, &a);
    if (have_sent && !memcmp(&a, &sent, sizeof a)) continue;
    int oldest = 0;
    for (int i = 1; i < SENDS; i++)
      if (sends[i] < sends[oldest]) oldest = i;
    if (rsm_now() - sends[oldest] < SENDS_WINDOW) continue;  // too many just now: with the next round
    sends[oldest] = rsm_now();
    if (!discord_set_activity(fd, &a, ++nonce)) {
      discord_close(fd);
      fd = -1;
      continue;
    }
    sent = a;
    have_sent = 1;
    rsm_log("discord: %s%s%s", a.details, *a.state ? ", " : "", a.state);
  }
  return NULL;
}

// ------------------------------------------------------------------------------------------------
// The tick
// ------------------------------------------------------------------------------------------------
// A song begins on the screen it is played on, and lasts through the screens of a song (its pause menu, Riff
// Repeater, the tuner and the mixer from there)
static const char *mode_of(const char *menu) {
  static const struct {
    const char *prefix, *name;
  } MODES[] = {{"LearnASong_", "Learn a Song"}, {"NonStopPlay_", "Non-Stop Play"}, {"ScoreAttack_", "Score Attack"}};
  for (size_t i = 0; i < sizeof MODES / sizeof MODES[0]; i++)
    if (!strncmp(menu, MODES[i].prefix, strlen(MODES[i].prefix))) return MODES[i].name;
  return NULL;
}

static int g_started;
static presence g_now;
static int64_t g_game_start;

static void tick(void) {
  int on = rsm_on("DiscordRichPresence");
  if (!on && !g_started) return;
  if (!g_started) {
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL)) return;  // no thread now: tried again with the next frame
    pthread_detach(t);
    g_started = 1;
    g_game_start = (int64_t)time(NULL);
  }
  presence p = g_now;
  p.on = on;
  const char *id = rsm_get("Mod Settings", "DiscordApplicationID");
  memset(p.application, 0, sizeof p.application);
  snprintf(p.application, sizeof p.application, "%s", id && *id ? id : DISCORD_APPLICATION);
  const char *menu = rsm_menu(), *mode = mode_of(menu), *song = rsm_song_key();
  size_t ml = strlen(menu);
  if (!p.in_song) {
    if (mode && ml > 5 && !strcmp(menu + ml - 5, "_Game")) {
      p.in_song = 1;
      p.start = (int64_t)time(NULL);
    }
  } else if (!rsm_menu_in(RSM_SONG_MODES)) {
    p.in_song = 0;
  } else if (strcmp(song, p.song)) {
    p.start = (int64_t)time(NULL);  // the next song of Non-Stop Play
  }
  if (p.in_song) {
    if (mode) {
      memset(p.mode, 0, sizeof p.mode);
      snprintf(p.mode, sizeof p.mode, "%s", mode);
    }
    memset(p.song, 0, sizeof p.song);
    snprintf(p.song, sizeof p.song, "%s", song);
  } else {
    memset(p.song, 0, sizeof p.song);
    memset(p.mode, 0, sizeof p.mode);
    p.start = g_game_start;
  }
  if (!memcmp(&p, &g_now, sizeof p)) return;
  g_now = p;
  pthread_mutex_lock(&g_lock);
  g_want = p;
  g_changed = 1;
  pthread_cond_signal(&g_wake);
  pthread_mutex_unlock(&g_lock);
}

const rsm_mod rsm_mod_discord = {"discord", tick, NULL, NULL, NULL};
