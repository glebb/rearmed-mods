// RSMods: Custom Tunings and the default tones (its app's Set & Forget mods: SetAndForgetMods.cs AddCustomTunings,
// SetDefaultTones and SetGuitarArcadeTone). The game names the tunings it knows from manifests/tuning.database.json,
// and takes its default tones (slot 1 of the tone stick for Rhythm, Lead and Bass; each Guitarcade game's) from
// manifests/tonemanager.database.json, both in cache7.7z inside cache.psarc; a tuning's name is looked up in the
// localization, localization/maingame.csv in cache4.7z. RSMods' app writes them into the game's cache.psarc, which
// stays changed until it is restored.
//
// Here cache.psarc stays as it is. The game opens cache7.7z and cache4.7z by name through FrontEnd_OpenFile, a hook
// point (frontend_open_file). At the first of those opens in a run, the library writes its own in rearmed-mods/ in the
// game folder, again at every start (a quarter of a second, most of it the localization): the game's, with those files
// changed and every other file's packed bytes as they are (rsmods_7z.c), from RS Re-ARMed's overlay
// (Update/patcharchives.psarc, which has its menu entries) when that has the archive, else from cache.psarc.
// Then the game opens those. On the Mac RS Re-ARMed's hook asks for PatchUpdate/<name> through the hook point; in
// Learn & Play its override opens the overlay itself and the hook sees the name: the library takes either. With both
// mods off, or nothing to change, the game opens its own as before. A change takes effect at the next start, as the
// game reads the files once.
//
// How the game gets them differs. On the Mac the hook opens rearmed-mods/<name> (the game's file system finds a path
// relative to the game folder on disk) and gives the game that stream, or lets the game open its own when that fails.
// In Learn & Play a mod can't call FrontEnd_OpenFile (it takes its path in a register of its own, which the runtime's
// calls don't do), so the hook changes the path the game asked for, as a hook may, to the library's archive in the
// Update folder: c:\rocksmith2014\update\rearmed-mods-<name>, as the game itself opens the overlay there
// ("%spatcharchives.psarc" on its update folder, 0x101b297), C:\Rocksmith2014 being the game folder in RS Re-ARMed's
// drive layout. The archive was just written, so it is there.
//
// The tunings are [Custom Tunings]' lines, a name and the strings' semitones. As in RSMods' app, each gets a line of
// its own in the localization, its name in every language, under an id the file hasn't from 37500 on; its definition
// is named "$[<id>]<name>". (The tuner's list shows "$[-1]<name>" as the name, but a song's details look the id up and
// show #-1# for it.) The tones are the profiles' saved tones (CustomTones), by name, as in RSMods' app, whose GearList
// the game's tone takes; here its Volume too, so it sounds as it does in the profile. The Guitarcade tones are found
// by their keys. The names of the profiles' tones go to rearmed-mods/tones.txt for the app.
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rsmods.h"
#include "rsmods_7z.h"
#include "rsmods_cache.h"
#include "rsmods_tuning.h"

#define DIR_NAME "rearmed-mods"
#define TONES_LIST DIR_NAME "/tones.txt"
#define TUNINGS_FILE "manifests/tuning.database.json"
#define TONES_FILE "manifests/tonemanager.database.json"
#define LOCALIZATION_FILE "localization/maingame.csv"
#define MAX_TUNINGS 128
#define MAX_PROFILES 16
#define MAX_TONES 512

// [Default Tones]: the game's tones RSMods' app sets (not the emulated bass's, which it leaves alone on purpose)
static const struct {
  const char *setting, *key;
} SLOTS[] = {
    {"Rhythm", "ToneDefault"},           {"Lead", "ToneDefault_Lead"},       {"Bass", "ToneDefault_Bass"},
    {"TempleOfBends", "gc_tob_02"},      {"ScaleWarriors", "gc_sw_01"},      {"StringSkipSaloon", "gc_sss_01"},
    {"ScaleRacer", "gc_sr_01"},          {"StarChordsLaser", "gc_sc_01"},    {"NinjaSlideN", "gc_ns_01"},
    {"HurtlinHurdles", "gc_hurdles_01"}, {"HarmonicHeist", "gc_hheist_01"}, {"DucksRedux", "gc_dp_02"},
    {"RainbowLaser", "gc_cd_02"},        {"GoneWailin", "gc_wrd_01"},
};
#define NSLOTS (sizeof SLOTS / sizeof SLOTS[0])

// The archives the library writes, and the game then opens
enum { CACHE7, CACHE4, NARCHIVES };
static struct {
  const char *name;
  const char *path, *game_path;          // the Mac's: written there, given to the game as that (relative)
  const char *lp_path, *lp_game_path;    // Learn & Play's: in the Update folder, as the game names it
  char *guest_path;  // the path the game is given, in memory it can be given
  int built;
} g_archives[NARCHIVES] = {
    {"cache7.7z", DIR_NAME "/cache7.7z", DIR_NAME "/cache7.7z", "Update/rearmed-mods-cache7.7z",
     "c:\\rocksmith2014\\update\\rearmed-mods-cache7.7z", NULL, 0},
    {"cache4.7z", DIR_NAME "/cache4.7z", DIR_NAME "/cache4.7z", "Update/rearmed-mods-cache4.7z",
     "c:\\rocksmith2014\\update\\rearmed-mods-cache4.7z", NULL, 0},
};
static int g_learn_play;  // win-x86: the path is changed, not the file opened
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static uint8_t g_key[32];

// RS Re-ARMed's overlay, unless RS_MODS_OVERLAY turns it off (as RS Re-ARMed reads it)
static int overlay_on(void) {
  const char *v = getenv("RS_MODS_OVERLAY");
  return !(v && (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F'));
}

static uint8_t *read_file(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t *d = NULL;
  if (!fseek(f, 0, SEEK_END)) {
    long len = ftell(f);
    rewind(f);
    if (len > 0 && len < (64l << 20) && (d = malloc((size_t)len + 1)) && fread(d, 1, (size_t)len, f) != (size_t)len) {
      free(d);
      d = NULL;
    }
    if (d) *n = (size_t)len;
  }
  fclose(f);
  return d;
}

static uint8_t *archive_from(const char *psarc, const char *name, size_t *n) {
  rsm_psarc *p = rsm_psarc_open(psarc, g_key);
  if (!p) return NULL;
  uint8_t *d = NULL;
  for (int i = 0; i < rsm_psarc_files(p) && !d; i++)
    if (!strcasecmp(rsm_psarc_name(p, i), name)) d = rsm_psarc_read(p, i, n);
  rsm_psarc_close(p);
  return d;
}

// ------------------------------------------------------------------------------------------------
// The profiles' tones
// ------------------------------------------------------------------------------------------------
typedef struct {
  char *json[MAX_PROFILES];
  int n;
  rsm_tone tones[MAX_TONES];
  int ntones;
  double newest;  // the newest profile's modification time
} profiles;

static double mtime_of(const char *path) {
  struct stat st;
  return stat(path, &st) ? -1 : (double)st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec * 1e-9;
}

// The save folder's profiles (*_PRFLDB): newest first, so a name in two is taken from the one used last
static void load_profiles(profiles *p, int decode) {
  char folder[1024], path[1300];
  unsigned long long account;
  struct {
    char name[256];
    double t;
  } files[MAX_PROFILES];
  int nf = 0;
  memset(p, 0, sizeof *p);
  p->newest = -1;
  if (!rsm_save_folder(folder, sizeof folder, &account)) return;
  DIR *d = opendir(folder);
  for (struct dirent *e; d && (e = readdir(d)) && nf < MAX_PROFILES;) {
    size_t len = strlen(e->d_name);
    if (len < 8 || strcmp(e->d_name + len - 7, "_PRFLDB") || len >= sizeof files[0].name) continue;
    snprintf(path, sizeof path, "%s/%s", folder, e->d_name);
    snprintf(files[nf].name, sizeof files[nf].name, "%s", e->d_name);
    files[nf].t = mtime_of(path);
    if (files[nf].t > p->newest) p->newest = files[nf].t;
    nf++;
  }
  if (d) closedir(d);
  if (!decode) return;
  uint64_t a = RC_ADDR(key_profile);
  if (!a) return;
  for (int i = 0; i < nf; i++)  // newest first
    for (int j = i + 1; j < nf; j++)
      if (files[j].t > files[i].t) {
        __typeof__(files[0]) t = files[i];
        files[i] = files[j];
        files[j] = t;
      }
  for (int i = 0; i < nf; i++) {
    size_t n;
    snprintf(path, sizeof path, "%s/%s", folder, files[i].name);
    uint8_t *data = read_file(path, &n);
    char *json = data ? rsm_profiles_decode(data, n, (const uint8_t *)(uintptr_t)RC_A(a)) : NULL;
    free(data);
    if (!json) continue;
    p->json[p->n++] = json;
    p->ntones += rsm_profile_tones(json, p->tones + p->ntones, MAX_TONES - p->ntones);
  }
}

static void free_profiles(profiles *p) {
  for (int i = 0; i < p->n; i++) free(p->json[i]);
}

static const rsm_tone *tone_named(const profiles *p, const char *name) {
  for (int i = 0; i < p->ntones; i++)
    if (!strcmp(p->tones[i].name, name)) return &p->tones[i];
  return NULL;
}

// rearmed-mods/tones.txt: "guitar" or "bass", a tab and the name, a line a tone, for the app's lists
static void write_tone_list(const profiles *p) {
  FILE *f = fopen(TONES_LIST ".part", "w");
  if (!f) return;
  for (int i = 0; i < p->ntones; i++) {
    int dup = 0;
    for (int j = 0; j < i && !dup; j++) dup = !strcmp(p->tones[i].name, p->tones[j].name);
    if (!dup && !strchr(p->tones[i].name, '\n')) fprintf(f, "%s\t%s\n", p->tones[i].bass ? "bass" : "guitar", p->tones[i].name);
  }
  fclose(f);
  rename(TONES_LIST ".part", TONES_LIST);
}

// ------------------------------------------------------------------------------------------------
// The archive
// ------------------------------------------------------------------------------------------------
static int read_tunings(rsm_tuning *t, int max) {
  int n = 0;
  const char *k, *v;
  for (int i = 0; n < max && rsm_ini_entry("Custom Tunings", i, &k, &v); i++) {
    if (rsm_tuning_parse(k, v, &t[n]))
      n++;
    else
      rsm_log("custom tunings: \"%s = %s\" isn't a name and six numbers", k, v);
  }
  return n;
}

// An archive as the game would open it: RS Re-ARMed's overlay's, else cache.psarc's
typedef struct {
  uint8_t *data;
  rsm_7z *z;
  const char *from;
} source;

static int open_source(source *src, const char *name) {
  size_t n = 0;
  char why[128] = "";
  src->from = "Update/patcharchives.psarc";
  if (!overlay_on() || !(src->data = archive_from(src->from, name, &n)))
    src->data = archive_from(src->from = "cache.psarc", name, &n);
  if (src->data && (src->z = rsm_7z_open(src->data, n, why, sizeof why))) return 1;
  rsm_log("%s: can't be read from %s (%s): the game's own is used", name, src->from, src->data ? why : "not there");
  return 0;
}

static void close_source(source *src) {
  rsm_7z_close(src->z);
  free(src->data);
}

// The archive written again with nc files changed, in its place in rearmed-mods/
static void save(int which, const source *src, int nc, const int *files, const uint8_t *const *data,
                 const size_t *lens, const char *what) {
  const char *path = g_archives[which].path;
  char part[64];
  size_t out_len = 0;
  uint8_t *out = nc ? rsm_7z_rewrite(src->z, nc, files, data, lens, &out_len) : NULL;
  if (!out) return;
  snprintf(part, sizeof part, "%s.part", path);
  FILE *f = fopen(part, "wb");
  int ok = f && fwrite(out, 1, out_len, f) == out_len;
  if (f && fclose(f)) ok = 0;
  free(out);
  if (ok && !rename(part, path)) {
    g_archives[which].built = 1;
    rsm_log("%s: %s, from %s", g_archives[which].name, what, src->from);
  } else {
    unlink(part);
    rsm_log("%s: %s can't be written: the game's own is used", g_archives[which].name, path);
  }
}

// The tunings' lines in the localization, which gives them their ids
static void build_localization(rsm_tuning *t, int nt) {
  source src = {0};
  if (!open_source(&src, "cache4.7z")) return;
  int f = rsm_7z_find(src.z, LOCALIZATION_FILE);
  size_t len = 0, out_len = 0;
  uint8_t *csv = f >= 0 ? rsm_7z_read(src.z, f, &len) : NULL;
  char *e = csv ? rsm_localization_add((const char *)csv, len, t, nt, &out_len) : NULL;
  free(csv);
  if (e) {
    char what[64];
    snprintf(what, sizeof what, "%d tunings' names", nt);
    const uint8_t *data[] = {(const uint8_t *)e};
    save(CACHE4, &src, 1, &f, data, &out_len, what);
  }
  if (!g_archives[CACHE4].built) {
    for (int i = 0; i < nt; i++) t[i].id = 0;  // the names as text: right in the tuner's list at least
    rsm_log("custom tunings: no names in the localization (%s)", e ? "not written" : LOCALIZATION_FILE);
  }
  free(e);
  close_source(&src);
}

static void build(void) {
  rsm_ini_poll();
  int tunings_on = rsm_on("CustomTunings"), tones_on = rsm_on("DefaultTones");
  mkdir(DIR_NAME, 0755);
  if (g_learn_play) mkdir("Update", 0755);  // the archives' folder there; RS Re-ARMed's overlay is in it when it has one
  profiles *prof = calloc(1, sizeof *prof);  // large: not on the game thread's stack
  rsm_tuning *t = calloc(MAX_TUNINGS, sizeof *t);
  source src = {0};
  char *edited[2] = {NULL, NULL};
  int files[2], nc = 0;
  const uint8_t *data[2];
  size_t lens[2];
  char what[160] = "";
  if (!prof || !t) goto done;
  load_profiles(prof, 0);
  if (tones_on || mtime_of(TONES_LIST) < prof->newest) {  // the app's list is older than a profile
    free_profiles(prof);
    load_profiles(prof, 1);
    if (prof->n) write_tone_list(prof);
  }
  int nt = tunings_on ? read_tunings(t, MAX_TUNINGS) : 0;
  if (!nt && !tones_on) goto done;
  uint64_t a = RC_ADDR(key_psarc_hex);
  if (!a || !rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(a), g_key)) {
    fprintf(stderr, "[rsmods] the archives' key isn't known on %s: Custom Tunings and the default tones stay off\n",
            rc_target());
    goto done;
  }
  if (nt) build_localization(t, nt);
  if (!open_source(&src, "cache7.7z")) goto done;
  if (nt) {
    int f = rsm_7z_find(src.z, TUNINGS_FILE);
    size_t len;
    uint8_t *json = f >= 0 ? rsm_7z_read(src.z, f, &len) : NULL;
    char *e = json ? rsm_tunings_edit((const char *)json, t, nt) : NULL;
    free(json);
    if (e) {
      files[nc] = f;
      data[nc] = (const uint8_t *)(edited[nc] = e);
      lens[nc++] = strlen(e);
      snprintf(what + strlen(what), sizeof what - strlen(what), "%d tunings", nt);
    } else {
      rsm_log("custom tunings: %s isn't as expected", TUNINGS_FILE);
    }
  }
  if (tones_on) {
    rsm_tone_change ch[NSLOTS];
    int nch = 0;
    for (size_t i = 0; i < NSLOTS; i++) {
      const char *name = rsm_get("Default Tones", SLOTS[i].setting);
      if (!name || !*name) continue;
      const rsm_tone *tone = tone_named(prof, name);
      if (!tone) {
        rsm_log("default tones: %s: no tone \"%s\" in the profiles", SLOTS[i].setting, name);
        continue;
      }
      ch[nch++] = (rsm_tone_change){SLOTS[i].key, tone};
      rsm_log("default tones: %s is \"%s\"", SLOTS[i].setting, name);
    }
    int f = rsm_7z_find(src.z, TONES_FILE), applied = 0;
    size_t len;
    uint8_t *json = nch && f >= 0 ? rsm_7z_read(src.z, f, &len) : NULL;
    char *e = json ? rsm_tones_edit((const char *)json, ch, nch, &applied) : NULL;
    free(json);
    if (e && applied) {
      files[nc] = f;
      data[nc] = (const uint8_t *)(edited[nc] = e);
      lens[nc++] = strlen(e);
      snprintf(what + strlen(what), sizeof what - strlen(what), "%s%d tones", *what ? ", " : "", applied);
    } else {
      free(e);
      if (nch) rsm_log("default tones: %s isn't as expected", TONES_FILE);
    }
  }
  save(CACHE7, &src, nc, files, data, lens, what);
done:
  for (int i = 0; i < NARCHIVES; i++)
    if (!g_archives[i].built) unlink(g_archives[i].path);  // none of an earlier start's left to look current
  free(edited[0]);
  free(edited[1]);
  close_source(&src);
  if (prof) free_profiles(prof);
  free(prof);
  free(t);
}

static int before_open(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  uint64_t out = OV_U64(a);
  const char *path = OV_PTR(a, const char);
  if (!out || !path) return 0;
  if (!strncasecmp(path, "patchupdate/", 12)) path += 12;
  int which = -1;
  for (int i = 0; i < NARCHIVES; i++)
    if (!strcasecmp(path, g_archives[i].name)) which = i;
  if (which < 0 || !g_archives[which].guest_path) return 0;
  pthread_once(&g_once, build);
  if (!g_archives[which].built) return 0;
  if (g_learn_play) {
    rc_mod_set_arg(c, 1, (uint64_t)(uintptr_t)g_archives[which].guest_path);
    rsm_log("%s: the game opens %s", g_archives[which].name, g_archives[which].path);
    return 0;
  }
  OV_GUEST(frontend_open_file, out, (uint64_t)(uintptr_t)g_archives[which].guest_path);
  if (!rc_mod_ptr((const void *)(uintptr_t)out)) {
    rsm_log("%s: the game can't open %s: its own is used", g_archives[which].name, g_archives[which].path);
    return 0;
  }
  ov_ret_u64(c, out);
  return 1;
}

__attribute__((constructor)) static void init(void) {
  g_learn_play = !strcmp(rc_target(), "win-x86");
  for (int i = 0; i < NARCHIVES; i++) {
    if (g_learn_play) {
      g_archives[i].path = g_archives[i].lp_path;
      g_archives[i].game_path = g_archives[i].lp_game_path;
    }
    if ((g_archives[i].guest_path = rc_guest_alloc(strlen(g_archives[i].game_path) + 1)))
      strcpy(g_archives[i].guest_path, g_archives[i].game_path);
  }
  rsm_hook("frontend_open_file", before_open, NULL);
}
