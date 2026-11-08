// The player's profiles: where the game saves them, and their names, for ProfileToLoad (mod_autoload.c) and the
// profile backups (mod_profile_backups.c).
//
// The save folder is Steam Cloud's: <account>/221680/remote, with one <id>_PRFLDB per profile and LocalProfiles.json,
// their list. RS Re-ARMed's Steam layer keeps it in the edition's steam folder (../steam from the game folder, or
// RS_STEAM_DATA_DIR) and, with the Steam client running, the game may use Steam's own userdata. The launcher gives the
// account as RS_STEAM_ID (a SteamID64); of its folders the one written last is the one in use.
//
// LocalProfiles.json is as the profiles are: "EVAS", a 20-byte header (the last word the plain size), then AES-256-ECB
// of zlib data (RSMods' Profiles.cs). The key is the game's own (key_profile).
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
