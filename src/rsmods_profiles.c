// The player's profiles: where the game saves them, and their names, for ProfileToLoad (mod_autoload.c) and the
// profile backups (mod_profile_backups.c).
//
// The save folder is Steam Cloud's: <account>/221680/remote, with one <id>_PRFLDB per profile and LocalProfiles.json,
// their list. RS Re-ARMed's Steam layer keeps it in the edition's steam folder (../steam from the game folder, or
// RS_STEAM_DATA_DIR) and, with the Steam client running, the game may use Steam's own userdata. The launcher gives the
// account as RS_STEAM_ID (a SteamID64); of its folders the one written last is the one in use.
//
// LocalProfiles.json is as the profiles are: "EVAS", a 20-byte header (the last word the plain size), then AES-256-ECB
// of zlib data (RSMods' Profiles.cs). The key is the game's own (key_profile). The game's plain data is the JSON and
// its terminating zero, compressed at level 9 (78 DA), in whole blocks (after the stream, what was in its buffer).
// More Song Lists (mod_song_lists.c) writes a profile so too, with zeros after the stream as RSMods' app does: checked
// on four of the Mac's profiles, the same bytes up to the stream's end.
#include <CommonCrypto/CommonCryptor.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "rsmods.h"

#define STEAM_ID64_BASE 76561197960265728ull

static double mtime_of(const char *path) {
  struct stat st;
  if (stat(path, &st)) return -1;
  return (double)st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec * 1e-9;
}

// The folder of this account under root, if it has a LocalProfiles.json written after `best`
static void consider(const char *root, unsigned long long account, char *out, size_t n, double *best,
                     unsigned long long *best_account) {
  char dir[1024], list[1100];
  snprintf(dir, sizeof dir, "%s/%llu/221680/remote", root, account);
  snprintf(list, sizeof list, "%s/LocalProfiles.json", dir);
  double t = mtime_of(list);
  if (t > *best) {
    *best = t;
    *best_account = account;
    snprintf(out, n, "%s", dir);
  }
}

int rsm_save_folder(char *out, size_t n, unsigned long long *account) {
  const char *data = getenv("RS_STEAM_DATA_DIR"), *home = getenv("HOME"), *id = getenv("RS_STEAM_ID");
  char shim[1024], steam[1024];
  snprintf(shim, sizeof shim, "%s", data && *data ? data : "../steam");
  snprintf(steam, sizeof steam, "%s/Library/Application Support/Steam/userdata", home ? home : "");
  double best = -1;
  unsigned long long found = 0;
  unsigned long long id64 = id ? strtoull(id, NULL, 10) : 0;
  if (id64 > STEAM_ID64_BASE) {
    unsigned long long a = id64 - STEAM_ID64_BASE;
    consider(shim, a, out, n, &best, &found);
    consider(steam, a, out, n, &best, &found);
  } else {  // no account given: the folder written last
    DIR *d = opendir(shim);
    for (struct dirent *e; d && (e = readdir(d));)
      if (e->d_name[0] >= '1' && e->d_name[0] <= '9') consider(shim, strtoull(e->d_name, NULL, 10), out, n, &best, &found);
    if (d) closedir(d);
  }
  if (best < 0) return 0;
  *account = found;
  return 1;
}

char *rsm_profiles_decode(const uint8_t *data, size_t n, const uint8_t key[32]) {
  enum { HEADER = 20 };
  if (n <= HEADER || memcmp(data, "EVAS", 4)) return NULL;
  size_t len = (n - HEADER + 15) & ~(size_t)15;  // whole blocks: the last one padded with zeros, as RSMods does
  uint8_t *in = calloc(1, len), *plain = malloc(len);
  size_t moved = 0;
  char *out = NULL;
  if (!in || !plain) goto done;
  memcpy(in, data + HEADER, n - HEADER);
  if (CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionECBMode, key, 32, NULL, in, len, plain, len, &moved) !=
      kCCSuccess)
    goto done;
  uint32_t size = (uint32_t)data[16] | (uint32_t)data[17] << 8 | (uint32_t)data[18] << 16 | (uint32_t)data[19] << 24;
  if (!size || size > (64u << 20)) goto done;
  out = malloc((size_t)size + 1);
  uLongf got = size;
  if (!out || uncompress((Bytef *)out, &got, plain, (uLong)moved) != Z_OK) {
    // the zero padding after the stream makes uncompress report a data error with the text out in full: inflate it
    z_stream z = {0};
    if (!out || inflateInit(&z) != Z_OK) {
      free(out);
      out = NULL;
      goto done;
    }
    z.next_in = plain;
    z.avail_in = (uInt)moved;
    z.next_out = (Bytef *)out;
    z.avail_out = size;
    int r = inflate(&z, Z_FINISH);
    got = z.total_out;
    inflateEnd(&z);
    if (r != Z_STREAM_END) {
      free(out);
      out = NULL;
      goto done;
    }
  }
  out[got] = 0;
done:
  free(in);
  free(plain);
  return out;
}

char *rsm_profiles_json(const char *folder) {
  uint64_t a = RC_ADDR(key_profile);
  if (!a) return NULL;
  char path[1100];
  snprintf(path, sizeof path, "%s/LocalProfiles.json", folder);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t buf[1 << 16];
  size_t n = fread(buf, 1, sizeof buf, f);
  fclose(f);
  return rsm_profiles_decode(buf, n, (const uint8_t *)(uintptr_t)RC_A(a));
}

uint8_t *rsm_profiles_encode(const char *json, const uint8_t header[16], const uint8_t key[32], size_t *n) {
  uLong plain = (uLong)strlen(json) + 1;  // with its terminating zero, as the game writes it
  uLong bound = compressBound(plain);
  size_t cap = ((size_t)bound + 15) & ~(size_t)15;
  uint8_t *z = calloc(1, cap), *out = malloc(20 + cap);
  uLongf zn = bound;
  size_t moved = 0;
  if (!z || !out || compress2(z, &zn, (const Bytef *)json, plain, 9) != Z_OK) goto fail;
  size_t whole = ((size_t)zn + 15) & ~(size_t)15;  // the last block padded with zeros (calloc)
  memcpy(out, header, 16);
  for (int i = 0; i < 4; i++) out[16 + i] = (uint8_t)(plain >> (8 * i));
  if (CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode, key, 32, NULL, z, whole, out + 20, whole, &moved) !=
          kCCSuccess ||
      moved != whole)
    goto fail;
  free(z);
  *n = 20 + whole;
  return out;
fail:
  free(z);
  free(out);
  return NULL;
}

// ------------------------------------------------------------------------------------------------
// A profile's song lists: SongListsRoot.SongLists, an array of arrays of song keys
// ------------------------------------------------------------------------------------------------
static const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// The end of the string at p (its opening quote), or NULL
static const char *skip_string(const char *p) {
  for (p++; *p && *p != '"'; p++)
    if (*p == '\\' && p[1]) p++;
  return *p ? p + 1 : NULL;
}

// The end of the JSON value at p, or NULL
static const char *skip_value(const char *p) {
  p = skip_ws(p);
  if (*p == '"') return skip_string(p);
  if (*p == '[' || *p == '{') {
    int depth = 0;
    for (; *p; p++) {
      if (*p == '"') {
        if (!(p = skip_string(p))) return NULL;
        p--;
      } else if (*p == '[' || *p == '{') {
        depth++;
      } else if ((*p == ']' || *p == '}') && --depth == 0) {
        return p + 1;
      }
    }
    return NULL;
  }
  const char *s = p;  // a number, true, false or null
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
  return p > s ? p : NULL;
}

// The value of a member of the object at p, or NULL
static const char *member(const char *p, const char *key) {
  size_t kn = strlen(key);
  p = skip_ws(p);
  if (*p != '{') return NULL;
  for (p = skip_ws(p + 1); *p == '"'; p = skip_ws(p + 1)) {
    const char *end = skip_string(p);
    if (!end) return NULL;
    int match = (size_t)(end - p - 2) == kn && !memcmp(p + 1, key, kn);
    p = skip_ws(end);
    if (*p != ':') return NULL;
    p = skip_ws(p + 1);
    if (match) return p;
    if (!(p = skip_value(p))) return NULL;
    p = skip_ws(p);
    if (*p != ',') return NULL;  // the object's end, or not JSON
  }
  return NULL;
}

char *rsm_song_lists_edit(const char *json, int want, int *had, int *now) {
  enum { MAX = 256 };
  const char *ends[MAX];
  int empty[MAX], n = 0;
  *had = *now = -1;
  if (want < 1) return NULL;
  const char *root = member(json, "SongListsRoot");
  const char *p = root ? member(root, "SongLists") : NULL;
  if (!p || *p != '[') return NULL;
  const char *after = p + 1;  // where a list is added: after the last one, or the array's [
  for (p = skip_ws(p + 1); *p != ']'; p = skip_ws(p)) {
    if (n) {
      if (*p != ',') return NULL;
      p = skip_ws(p + 1);
    }
    if (*p != '[' || n == MAX) return NULL;
    const char *end = skip_value(p);
    if (!end) return NULL;
    empty[n] = *skip_ws(p + 1) == ']';
    ends[n++] = after = p = end;
  }
  *had = *now = n;
  size_t len = strlen(json);
  if (n < want) {  // empty lists after the last, as the game writes them
    static const char LIST[] = ",\n\n[\n]";
    size_t at = (size_t)(after - json), add = (size_t)(want - n) * (sizeof LIST - 1) - (n ? 0 : 1);
    char *out = malloc(len + add + 1), *o = out;
    if (!out) return NULL;
    memcpy(o, json, at);
    o += at;
    for (int i = n; i < want; i++) {
      const char *l = i ? LIST : LIST + 1;
      memcpy(o, l, strlen(l));
      o += strlen(l);
    }
    memcpy(o, json + at, len - at + 1);
    *now = want;
    return out;
  }
  int keep = n;
  while (keep > want && empty[keep - 1]) keep--;  // from the end, while empty: a list with songs stays
  if (keep == n) return NULL;
  size_t from = (size_t)(ends[keep - 1] - json), to = (size_t)(ends[n - 1] - json);
  char *out = malloc(len - (to - from) + 1);
  if (!out) return NULL;
  memcpy(out, json, from);
  memcpy(out + from, json + to, len - to + 1);
  *now = keep;
  return out;
}

// A JSON string at p (after its opening quote) into out; the end of it, or NULL
static const char *json_string(const char *p, char *out, size_t n) {
  size_t k = 0;
  for (; *p && *p != '"'; p++) {
    char c = *p;
    if (c == '\\' && p[1]) {
      c = *++p;
      if (c == 'u') {  // not kept: a name with one is matched as far as the rest goes
        for (int i = 0; i < 4 && p[1]; i++) p++;
        c = '?';
      } else if (c == 'n' || c == 't' || c == 'r')
        c = ' ';
    }
    if (k + 1 < n) out[k++] = c;
  }
  out[k] = 0;
  return *p == '"' ? p + 1 : NULL;
}

int rsm_profiles_names(const char *json, char names[][64], int max) {
  int count = 0;
  for (const char *p = json; p && count < max && (p = strstr(p, "\"PlayerName\""));) {
    p += 12;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
    if (*p != '"') continue;
    p = json_string(p + 1, names[count], 64);
    if (p) count++;
  }
  return count;
}
