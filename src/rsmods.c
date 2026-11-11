// RSMods core: the hooks, game state, calls into translated code, and the dispatch to the mods. See rsmods.h.
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rsmods.h"

// The version, findable in the file without loading it (the settings app compares installed and bundled)
__attribute__((used)) const char rsm_version_tag[] = "rearmed-mods-version:" REARMED_MODS_VERSION;

static const rsm_mod *const MODS[] = {
    &rsm_mod_riff_repeater,
    &rsm_mod_looping,
    &rsm_mod_song_previews,
    &rsm_mod_song_timer,
    &rsm_mod_volume,
    &rsm_mod_autoload,
    &rsm_mod_enumeration,
    &rsm_mod_input_volume,
    &rsm_mod_screenshot_scores,
    &rsm_mod_two_rtc,
    &rsm_mod_background_audio,
    &rsm_mod_linear_rr,
    &rsm_mod_loft,
    &rsm_mod_scene_dump,
    &rsm_mod_hide_nodes,
    &rsm_mod_headstock,
    &rsm_mod_lyrics,
    &rsm_mod_skyline,
    &rsm_mod_fretboard,
    &rsm_mod_camera,
    &rsm_mod_extended_range,  // before the colors, which it changes
    &rsm_mod_colors,
    &rsm_mod_tone_host,
    &rsm_mod_song_stems,
    &rsm_mod_discord,
    &rsm_mod_sunlight,
    &rsm_mod_nsp_timer,
    &rsm_mod_secondary_monitor,
    &rsm_mod_song_lists,  // on the first frame, before the backups' thread starts
    &rsm_mod_profile_backups,
    &rsm_mod_game_note,  // before the two that use the note
    &rsm_mod_current_note,
    &rsm_mod_guitar_speak,
    &rsm_mod_buggy_tones,
    &rsm_mod_accuracy,
    &rsm_mod_song_list_titles,
};
#define NMODS (sizeof MODS / sizeof MODS[0])

// ------------------------------------------------------------------------------------------------
// Log (RS_MODS_LOG=1, like the mods module: stderr, which the launcher writes to logs/last-run.log)
// ------------------------------------------------------------------------------------------------
static int rsm_logging(void) {
  static int on = -1;
  if (on < 0) {
    const char *v = getenv("RS_MODS_LOG");
    on = v && *v && *v != '0';
  }
  return on;
}

void rsm_log(const char *fmt, ...) {
  if (!rsm_logging()) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("[rsmods] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

// ------------------------------------------------------------------------------------------------
// Wwise
// ------------------------------------------------------------------------------------------------
// AK::SoundEngine's s_bInitialized, which IsInitialized returns: Init sets it, Term clears it after freeing the
// engine. On quit the game terminates Wwise and still runs a frame or two, where a call into the sound engine
// reads the freed engine and crashes; the calls below do nothing (and return AK_FAIL) while it is clear.
int ak_ready(void) {
  uint64_t a = RC_ADDR(ak_initialized);
  return a && RC_LD(uint8_t, RC_A(a)) != 0;
}

// What the game reads or fills has to be its own memory (rc_mod.h): a name, and the results, go through this,
// one per thread, each call starting over
#define AK_NAME_MAX 256
#define AK_OBJECTS_MAX 4096
typedef struct {
  uint32_t obj, parent;
  int32_t depth;
} ak_object_info;  // AkObjectInfo
typedef struct {
  char name[AK_NAME_MAX];
  float value;
  int32_t type, count, pos;
  ak_object_info objects[AK_OBJECTS_MAX];
} ak_scratch;

static ak_scratch *scratch(const char *name) {
  static __thread ak_scratch *s;
  if (!s) s = rc_guest_alloc(sizeof *s);
  if (s && name) snprintf(s->name, sizeof s->name, "%s", name);
  return s;
}

static uint64_t guest_arg(const void *p) { return (uint64_t)(uintptr_t)p; }

int ak_set_rtpc(const char *name, float value, uint64_t obj, int ms, int curve) {
  ak_scratch *s = scratch(name);
  if (!s || !ak_ready()) return AK_FAIL;
  const uint64_t a[] = {guest_arg(s->name), rsm_f32bits(value), obj, (uint32_t)ms, (uint32_t)curve};
  return (int)rc_call_name_f("ak_set_rtpc_name", a, 5, 1u << 1);
}

int ak_get_rtpc(const char *name, uint64_t obj, float *out) {
  ak_scratch *s = scratch(name);
  *out = 0;
  if (!s || !ak_ready()) return AK_FAIL;
  s->value = 0;
  s->type = 1;  // RTPCValue_GameObject
  int r = (int)OV_GUEST(ak_get_rtpc_name, guest_arg(s->name), obj, guest_arg(&s->value), guest_arg(&s->type));
  *out = s->value;
  return r;
}

int ak_set_actor_mixer_effect(uint32_t node, uint32_t slot, uint32_t fx) {
  if (!ak_ready()) return AK_FAIL;
  return (int)OV_GUEST(ak_set_actor_mixer_effect, node, slot, fx);
}

int ak_seek_on_event(const char *event, uint64_t obj, int32_t ms, int nearest_marker) {
  ak_scratch *s = scratch(event);
  if (!s || !ak_ready()) return AK_FAIL;
  return (int)OV_GUEST(ak_seek_on_event_name, guest_arg(s->name), obj, (uint32_t)ms, nearest_marker ? 1 : 0);
}

int ak_get_source_play_position(uint32_t playing_id, int32_t *ms, int extrapolate) {
  ak_scratch *s = scratch(NULL);
  if (!s || !ak_ready()) return AK_FAIL;
  s->pos = 0;
  int r = (int)OV_GUEST(ak_source_play_position, playing_id, guest_arg(&s->pos), extrapolate ? 1 : 0);
  *ms = s->pos;
  return r;
}

// The first audio object of an event: the song's actor-mixer node for "Play_<SongKey>" (as RSMods).
uint32_t ak_query_first_object(const char *event) {
  ak_scratch *s = scratch(event);
  if (!s || !ak_ready()) return 0;
  s->count = 0;
  int r = (int)OV_GUEST(ak_query_objects_name, guest_arg(s->name), guest_arg(&s->count), 0);
  if ((r != AK_SUCCESS && r != AK_PARTIAL_SUCCESS) || s->count <= 0 || s->count > AK_OBJECTS_MAX) return 0;
  r = (int)OV_GUEST(ak_query_objects_name, guest_arg(s->name), guest_arg(&s->count), guest_arg(s->objects));
  return (r == AK_SUCCESS || r == AK_PARTIAL_SUCCESS) && s->count ? s->objects[0].obj : 0;
}

// ------------------------------------------------------------------------------------------------
// Game state
// ------------------------------------------------------------------------------------------------
// The UI manager has the current menu (Mac 0x100365eb0, Learn & Play 0xf85e7e); the menu's name is an efd string,
// inline unless efd_str_inline holds another address than its size word's, as the game reads it (Mac 0x1000e0330,
// Learn & Play 0xf85e92). A name the game hasn't: logged by RC_ADDR, and every menu is "" for the mods.
const char *rsm_menu(void) {
  static int known = -1;
  if (known < 0) known = RC_ADDR(ui_manager) && RC_HAS_OFF(ui_current_menu) && RC_HAS_OFF(menu_name) &&
                         RC_HAS_OFF(efd_str_size) && RC_HAS_OFF(efd_str_inline);
  if (!known) return "";
  uint64_t a = RC_ADDR(ui_manager);
  const char *mgr = rc_mod_ptr((const void *)(uintptr_t)RC_A(a));
  if (!mgr) return "";
  const char *m = rc_mod_ptr(mgr + RC_OFF(ui_current_menu));
  if (!m) return "";
  const char *s = m + RC_OFF(menu_name);
  const char *p = rc_mod_ptr(s + RC_OFF(efd_str_inline)) == s + RC_OFF(efd_str_size) ? s : rc_mod_ptr(s);
  return p ? p : "";
}

int rsm_menu_in(const char *const *list) {
  const char *m = rsm_menu();
  for (; *list; list++)
    if (!strcmp(m, *list)) return 1;
  return 0;
}

// GameState.hpp of RSMods
const char *const RSM_SONG_MODES[] = {
    "LearnASong_Game", "NonStopPlay_Game", "ScoreAttack_Game", "LearnASong_Pause", "NonStopPlay_Pause",
    "ScoreAttack_Pause", "RiffRepeater", "LearnASong_RiffRepeater", "RiffRepeater_AdvancedSettings",
    "RiffRepeater_Pause", "Tuner", "MixerMenu", "HelpList", "SideList", "CalibrationMeter", NULL};
const char *const RSM_FAST_RR_MODES[] = {
    "LearnASong_Game", "NonStopPlay_Game", "LearnASong_Pause", "NonStopPlay_Pause", "LearnASong_RiffRepeater",
    "RiffRepeater", "RiffRepeater_AdvancedSettings", "RiffRepeater_Pause", NULL};
const char *const RSM_SCORE_SCREENS[] = {"LearnASong_SongReview", "ScoreAttack_SongReview", "Duet_SongReview",
                                         "H2H_SongReview", NULL};
const char *const RSM_LAS_PLAYING[] = {"LearnASong_Game", "NonStopPlay_Game", NULL};
const char *const RSM_LAS_MODES[] = {"LearnASong_Game", "NonStopPlay_Game", "LearnASong_Pause", "NonStopPlay_Pause",
                                     NULL};
const char *const RSM_LAS_PAUSE[] = {"LearnASong_Pause", "NonStopPlay_Pause", NULL};

// Song key: the game previews a song with the event "Play_<SongKey>_Preview" ("_Invalid" with previews off).
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_key_posted[128], g_key[128];

const char *rsm_song_key(void) { return g_key; }

// The song itself plays as "Play_<SongKey>": its playing ID (PostEvent's result) gives the position.
static char g_play_event[160];
static uint32_t g_play_id;
static __thread int t_song_event;  // the PostEvent running on this thread is a song's (pre -> post hook)

static void note_event(const char *name) {
  t_song_event = 0;
  if (!name || strncmp(name, "Play_", 5)) return;
  size_t n = strnlen(name, 256);
  if (n <= 5 || n >= 256) return;
  const char *tail = n > 13 ? name + n - 8 : "";
  if (strcmp(tail, "_Preview") && strcmp(tail, "_Invalid")) {
    // only the song itself ("Play_<the previewed song's key>"), not the game's other sounds (Play_Crowd, ...)
    pthread_mutex_lock(&g_key_lock);
    if (*g_key_posted && !strcmp(name + 5, g_key_posted) && n < sizeof g_play_event) {
      memcpy(g_play_event, name, n + 1);
      g_play_id = 0;
      t_song_event = 1;
    }
    pthread_mutex_unlock(&g_key_lock);
    return;
  }
  size_t k = n - 13;
  if (k >= sizeof g_key_posted) return;
  pthread_mutex_lock(&g_key_lock);
  memcpy(g_key_posted, name + 5, k);
  g_key_posted[k] = 0;
  pthread_mutex_unlock(&g_key_lock);
}

static void note_playing_id(uint32_t id) {
  if (!t_song_event) return;
  t_song_event = 0;
  pthread_mutex_lock(&g_key_lock);
  g_play_id = id;
  pthread_mutex_unlock(&g_key_lock);
  rsm_log("%s: playing id %u", g_play_event, id);
}

// The playing ID of "Play_<the current song key>", or 0
static uint32_t song_playing_id(void) {
  char want[160];
  snprintf(want, sizeof want, "Play_%s", g_key);
  pthread_mutex_lock(&g_key_lock);
  uint32_t id = !strcmp(g_play_event, want) ? g_play_id : 0;
  pthread_mutex_unlock(&g_key_lock);
  return id;
}

double rsm_song_time(void) {
  uint32_t id = song_playing_id();
  if (!id) return -1;
  int32_t ms = 0;
  if (ak_get_source_play_position(id, &ms, 1) != AK_SUCCESS) return -1;
  return ms / 1000.0;
}

void rsm_song_seek_ms(int32_t ms) {
  if (!*g_key) return;
  char ev[160];
  snprintf(ev, sizeof ev, "Play_%s", g_key);
  ak_seek_on_event(ev, RS_SONG_GAME_OBJECT, ms < 0 ? 0 : ms, 0);
}

void rsm_format_time(char *out, size_t n, double seconds) {
  int s = seconds > 0 ? (int)seconds : 0, h = s / 3600, m = s / 60 % 60;
  if (h > 0)
    snprintf(out, n, "%02dh:%02dm:%02ds", h, m, s % 60);
  else
    snprintf(out, n, "%02dm:%02ds", m, s % 60);
}

double rsm_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

// Modifier keys, from AppKit (+[NSEvent modifierFlags])
static unsigned long modifier_flags(void) {
  static Class cls;
  static SEL sel;
  if (!cls) {
    cls = objc_getClass("NSEvent");
    sel = sel_registerName("modifierFlags");
  }
  return cls ? ((unsigned long (*)(Class, SEL))objc_msgSend)(cls, sel) : 0;
}
int rsm_ctrl_down(void) { return (modifier_flags() & (1ul << 18)) != 0; }
int rsm_shift_down(void) { return (modifier_flags() & (1ul << 17)) != 0; }

// ------------------------------------------------------------------------------------------------
// Per frame and per key
// ------------------------------------------------------------------------------------------------
static char g_last_menu[64];

static int g_ak_was_ready;

static void rsmods_tick(void) {
  // Wwise up once and down again: the game is quitting, and the mods' frame work talks to the sound engine
  if (ak_ready())
    g_ak_was_ready = 1;
  else if (g_ak_was_ready)
    return;
  if (rsm_ini_poll())
    rsm_log("RSMods.ini (re)loaded: RRSpeedAboveOneHundred=%s", rsm_on("RRSpeedAboveOneHundred") ? "on" : "off");
  const char *m = rsm_menu();
  if (strncmp(m, g_last_menu, sizeof g_last_menu - 1)) {
    rsm_log("menu: %s", m);
    snprintf(g_last_menu, sizeof g_last_menu, "%s", m);
  }
  char key[sizeof g_key_posted];
  pthread_mutex_lock(&g_key_lock);
  memcpy(key, g_key_posted, sizeof key);
  pthread_mutex_unlock(&g_key_lock);
  if (strcmp(key, g_key)) {
    memcpy(g_key, key, sizeof g_key);
    rsm_log("song: %s", g_key);
    for (size_t i = 0; i < NMODS; i++)
      if (MODS[i]->song_changed) MODS[i]->song_changed(g_key);
  }
  rsm_overlay_begin();
  for (size_t i = 0; i < NMODS; i++)
    if (MODS[i]->tick) MODS[i]->tick();
  rsm_scene_tick();
  rsm_overlay_draw();
}

// Letters typed into the game are text, not hotkeys. A text entry dialog is a screen of its own. The song search is
// not: the screen's name stays, and nothing of the game says the box is open. It opens with the game's own key (`/` on
// the Mac, Shift in Learn & Play) or a click on it, and takes every letter until Esc or another screen. Return is not
// an end to it: with no match it leaves the box open, and letters go on into it. So hotkeys stop from such a key or
// any click on a screen outside a song, and come back with Esc or the next screen: better a hotkey that waits for
// that than one that goes off while the player types.
#define KVK_ESCAPE 0x35
static char g_search_menu[64];  // the screen whose search box may be open

static void search_may_open(const char *menu) {
  if (!rsm_menu_in(RSM_SONG_MODES)) snprintf(g_search_menu, sizeof g_search_menu, "%s", menu);
}

static int typing(const rsm_key_event *e) {
  const char *m = rsm_menu();
  if (!strcmp(m, "TextEntryDialog")) return 1;
  if (*g_search_menu && strcmp(m, g_search_menu)) g_search_menu[0] = 0;
  if (e->down) {
    if (e->keycode == RC_OFF(song_search_key) || e->keycode == RC_OFF(song_search_key2)) search_may_open(m);
    if (e->keycode == KVK_ESCAPE) g_search_menu[0] = 0;
  }
  return *g_search_menu != 0;
}

// The input sink's codes: the shell's event type + 1 (rsp_gfx_event_code): 1 key down, 2 key up, a = virtual key
// code; 5 button down.
static void rsmods_input(int code, int a) {
  if (code == 5) {
    rsm_log("button down");
    search_may_open(rsm_menu());
  }
  if (code != 1 && code != 2) return;
  rsm_key_event e = {a, code == 1, rsm_ctrl_down(), rsm_shift_down()};
  if (e.down) rsm_log("key down 0x%x%s%s", a, e.ctrl ? " +ctrl" : "", e.shift ? " +shift" : "");
  if (typing(&e)) return;
  for (size_t i = 0; i < NMODS; i++)
    if (MODS[i]->key) MODS[i]->key(&e);
}

// ------------------------------------------------------------------------------------------------
// Hooks
// ------------------------------------------------------------------------------------------------
void rsm_send_key(int keycode, uint16_t ch) {
  OV_GUEST(input_sink, 1, (uint32_t)keycode, ch);
  OV_GUEST(input_sink, 2, (uint32_t)keycode, ch);
}

// After the game's frame; keeps its result.
static void on_frame(rc_ctx *c, void *user) {
  (void)user;
  uint64_t rax = c->r[0];
  rsmods_tick();
  c->r[0] = rax;
}

// The mods see a key first; the game still gets it.
static int on_input(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  int code = OV_I32(a);
  int key = OV_I32(a);
  rsmods_input(code, key);
  return 0;
}

static int on_post_event(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  const char *name = OV_PTR(a, const char);
  note_event(name);
  for (size_t i = 0; i < NMODS; i++)
    if (MODS[i]->post_event && name && MODS[i]->post_event(name)) {
      t_song_event = 0;
      ov_ret_u32(c, 0);  // AK_INVALID_PLAYING_ID, as for an event Wwise does not know
      return 1;
    }
  return 0;
}

static void on_posted_event(rc_ctx *c, void *user) {
  (void)user;
  note_playing_id((uint32_t)c->r[0]);  // AkPlayingID
}

int rsm_hook(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post) {
  if (!rc_hook_add_name(name, pre, post, NULL)) return 0;
  fprintf(stderr, "[rsmods] no hook point %s on %s: its mod stays off\n", name, rc_target());
  return -1;
}

// The SHA-256 of this library's file, in the log: which build ran is then a fact of the run
static void log_file_hash(void) {
  Dl_info info;
  struct stat st;
  int fd = dladdr((void *)log_file_hash, &info) ? open(info.dli_fname, O_RDONLY) : -1;
  if (fd < 0) return;
  void *m = fstat(fd, &st) ? MAP_FAILED : mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (m != MAP_FAILED) {
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    char hex[2 * sizeof d + 1];
    CC_SHA256(m, (CC_LONG)st.st_size, d);
    for (size_t k = 0; k < sizeof d; k++) snprintf(hex + 2 * k, 3, "%02x", d[k]);
    rsm_log("file sha256 %s", hex);
    munmap(m, (size_t)st.st_size);
  }
  close(fd);
}

__attribute__((constructor)) static void rsmods_init(void) {
  int bad = 0;
  bad |= rsm_hook("game_frame", NULL, on_frame);
  bad |= rsm_hook("input_sink", on_input, NULL);
  bad |= rsm_hook("ak_post_event_name", on_post_event, on_posted_event);
  if (bad) fprintf(stderr, "[rsmods] this RS Re-ARMed lacks hook points: rearmed-mods needs RS Re-ARMed 1.5.1 or later\n");
  rsm_log("rearmed-mods %s loaded on %s, %d mods", REARMED_MODS_VERSION, rc_target(), (int)NMODS);
  if (rsm_logging()) log_file_hash();
}
