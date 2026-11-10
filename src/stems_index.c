// Song Stems' way from a song's key to its .wem. See stems_index.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods_files.h"
#include "stems_index.h"
#include "stems_mix.h"

typedef struct {
  char *path;
  int n;
  char **keys;  // the songs whose bank it has (song_<key>.bnk), as named there
} archive;
static archive *g_archives;
static int g_narchives;

void stems_index_free(void) {
  for (int i = 0; i < g_narchives; i++) {
    for (int k = 0; k < g_archives[i].n; k++) free(g_archives[i].keys[k]);
    free(g_archives[i].keys);
    free(g_archives[i].path);
  }
  free(g_archives);
  g_archives = NULL;
  g_narchives = 0;
}

// "audio/mac/song_stro1251.bnk" -> "stro1251" (not a preview's bank); 0 otherwise
static int bank_key(const char *name, char *key, size_t n) {
  const char *b = strrchr(name, '/');
  b = b ? b + 1 : name;
  size_t l = strlen(b);
  if (strncasecmp(b, "song_", 5) || l < 10 || strcasecmp(b + l - 4, ".bnk")) return 0;
  if (l >= 17 && !strncasecmp(b + l - 12, "_preview.bnk", 12)) return 0;
  if (l - 9 >= n) return 0;
  memcpy(key, b + 5, l - 9);
  key[l - 9] = 0;
  return 1;
}

static void index_archive(const char *path, const uint8_t key[32]) {
  rsm_psarc *a = rsm_psarc_open_io(path, key, &rsm_file_io);
  if (!a) return;
  archive r = {strdup(path), 0, NULL};
  for (int i = 0, n = rsm_psarc_files(a); r.path && i < n; i++) {
    char song[128];
    if (!bank_key(rsm_psarc_name(a, i), song, sizeof song)) continue;
    char **more = realloc(r.keys, (size_t)(r.n + 1) * sizeof *more);
    if (!more) break;
    r.keys = more;
    if ((r.keys[r.n] = strdup(song))) r.n++;
  }
  rsm_psarc_close(a);
  archive *more = r.n ? realloc(g_archives, (size_t)(g_narchives + 1) * sizeof *more) : NULL;
  if (!more) {
    for (int k = 0; k < r.n; k++) free(r.keys[k]);
    free(r.keys);
    free(r.path);
    return;
  }
  g_archives = more;
  g_archives[g_narchives++] = r;
}

// The folder's names are the game's: a served archive is among them once, by the name it opens by, and has a size
// and a kind by rsm_file_stat alone
static void index_folder(const char *dir, int depth, const uint8_t key[32]) {
  DIR *d = rsm_file_opendir(dir);
  if (!d) return;
  for (struct dirent *e; (e = rsm_file_readdir(d));) {
    if (e->d_name[0] == '.') continue;
    char path[2048];
    struct stat st;
    if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
    if (rsm_file_stat(path, &st)) continue;
    size_t l = strlen(e->d_name);
    if (S_ISDIR(st.st_mode) && depth < 8)
      index_folder(path, depth + 1, key);
    else if (S_ISREG(st.st_mode) && l > 6 && !strcasecmp(e->d_name + l - 6, ".psarc"))
      index_archive(path, key);
  }
  rsm_file_closedir(d);
}

int stems_index_build(const uint8_t key[32]) {
  stems_index_free();
  index_folder("dlc", 0, key);
  index_archive("songs.psarc", key);
  return g_narchives;
}

const char *stems_index_find(const char *song) {
  for (int i = 0; i < g_narchives; i++)
    for (int k = 0; k < g_archives[i].n; k++)
      if (!strcasecmp(g_archives[i].keys[k], song)) return g_archives[i].path;
  return NULL;
}

void *stems_index_wem(const char *archive_path, const char *song, const uint8_t key[32], uint32_t *id, size_t *len) {
  *id = 0;
  *len = 0;
  rsm_psarc *a = rsm_psarc_open_io(archive_path, key, &rsm_file_io);
  if (!a) return NULL;
  int files = rsm_psarc_files(a), bank = -1, nids = 0;
  uint32_t *ids = calloc((size_t)files + 1, sizeof *ids);
  int *which = calloc((size_t)files + 1, sizeof *which);
  for (int i = 0; ids && which && i < files; i++) {
    const char *name = rsm_psarc_name(a, i);
    char k[128];
    uint32_t wem = stems_wem_id(name);
    if (wem) {
      ids[nids] = wem;
      which[nids++] = i;
    } else if (bank < 0 && bank_key(name, k, sizeof k) && !strcasecmp(k, song)) {
      bank = i;
    }
  }
  size_t blen = 0;
  uint8_t *b = bank >= 0 ? rsm_psarc_read(a, bank, &blen) : NULL;
  int k = b ? stems_bank_wem(b, blen, ids, NULL, nids) : -1;
  free(b);
  void *w = k >= 0 ? rsm_psarc_read(a, which[k], len) : NULL;
  if (w) *id = ids[k];
  free(ids);
  free(which);
  rsm_psarc_close(a);
  return w;
}
