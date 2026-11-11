// Song Stems' index of the song archives and the .wem it reads from one (src/stems_index.c), on archives as the game
// finds them: built twice by test.sh. With RS Re-ARMed's file layer (tests/rc_file.c, the runtime's own, which the
// game executable has) a provider here serves an archive that is no file on disk and hides the file it stands for,
// as a mod that makes one platform's archive from the other's does. Without the layer (-DNO_LAYER: a game before
// 1.14.0) the files on disk are read as they always were.
#include <CommonCrypto/CommonCryptor.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../src/rsmods_files.h"
#include "../src/song_info.h"
#include "../src/stems_index.h"
#include "check.h"
#ifndef NO_LAYER
#include "rc_file.h"
#endif

static void put_be(uint8_t *p, uint64_t v, int n) {
  for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * (n - 1 - i)));
}

// An archive of n files in 64 KiB blocks, each zlib when that shrinks it, its table encrypted: malloc'ed, *len long
static uint8_t *make_psarc(const uint8_t key[32], int n, const char **names, const uint8_t **bodies, const size_t *lens,
                           size_t *len) {
  enum { BLOCK = 65536 };
  int entries = n + 1, nblocks = 0;
  char list[4096] = "";
  for (int i = 0; i < n; i++) strcat(strcat(list, names[i]), i + 1 < n ? "\n" : "");
  uint8_t *data = malloc(4 << 20), table[8192] = {0};
  uint32_t zlen[256], first[64];
  uint64_t offset[64], size[64];
  size_t used = 0;
  for (int e = 0; e < entries; e++) {
    const uint8_t *body = e ? bodies[e - 1] : (const uint8_t *)list;
    size_t blen = e ? lens[e - 1] : strlen(list);
    first[e] = (uint32_t)nblocks, offset[e] = used, size[e] = blen;
    for (size_t at = 0; at < blen; at += BLOCK) {
      size_t part = blen - at < BLOCK ? blen - at : BLOCK;
      uLongf z = compressBound((uLong)part);
      uint8_t *buf = malloc(z);
      compress2(buf, &z, body + at, (uLong)part, 9);
      if (z < part) {
        memcpy(data + used, buf, z);
        used += z, zlen[nblocks++] = (uint32_t)z;
      } else {
        memcpy(data + used, body + at, part);
        used += part, zlen[nblocks++] = part == BLOCK ? 0 : (uint32_t)part;
      }
      free(buf);
    }
  }
  uint32_t toc_end = 32 + (uint32_t)entries * 30 + (uint32_t)nblocks * 2;
  for (int e = 0; e < entries; e++) {
    uint8_t *p = table + (size_t)e * 30;
    put_be(p + 16, first[e], 4);
    put_be(p + 20, size[e], 5);
    put_be(p + 25, toc_end + offset[e], 5);
  }
  for (int b = 0; b < nblocks; b++) put_be(table + (size_t)entries * 30 + (size_t)b * 2, zlen[b], 2);
  size_t toc_len = toc_end - 32, whole = (toc_len + 15) & ~(size_t)15, moved = 0;
  static const uint8_t iv[16] = {0};
  uint8_t *enc = malloc(whole);
  CCCryptorRef c;
  CCCryptorCreateWithMode(kCCEncrypt, kCCModeCFB, kCCAlgorithmAES, ccNoPadding, iv, key, 32, NULL, 0, 0, 0, &c);
  CCCryptorUpdate(c, table, whole, enc, whole, &moved);
  CCCryptorRelease(c);
  uint8_t *out = malloc(toc_end + used), *h = out;
  memcpy(h, "PSAR", 4);
  put_be(h + 4, 0x00010004, 4);
  memcpy(h + 8, "zlib", 4);
  put_be(h + 12, toc_end, 4);
  put_be(h + 16, 30, 4);
  put_be(h + 20, (uint64_t)entries, 4);
  put_be(h + 24, BLOCK, 4);
  put_be(h + 28, 4, 4);
  memcpy(out + 32, enc, toc_len);
  memcpy(out + toc_end, data, used);
  free(enc);
  free(data);
  *len = toc_end + used;
  return out;
}

// A song's archive for a platform's folder ("mac", "windows"): its bank, its preview's bank, the two .wem files. The
// bank names its song's .wem by file ID; the audio is `fill` and a count, too mixed for zlib, over two blocks.
#define WEM_LEN 150000
static uint8_t *song_psarc(const uint8_t key[32], const char *platform, const char *song, uint32_t id, uint8_t fill,
                           size_t *len) {
  static char names[4][160];
  static uint8_t wem[WEM_LEN], preview[900], bank[64], pbank[64];
  uint32_t seed = id;
  for (int i = 0; i < WEM_LEN; i++) wem[i] = (uint8_t)(fill ^ ((seed = seed * 1664525u + 1013904223u) >> 24));
  memset(preview, 'p', sizeof preview);
  memset(bank, 0, sizeof bank), memset(pbank, 0, sizeof pbank);
  memcpy(bank, "BKHD", 4), memcpy(pbank, "BKHD", 4);
  uint32_t pid = id + 1;
  memcpy(bank + 20, &id, 4);  // little-endian, as this machine is
  memcpy(pbank + 20, &pid, 4);
  snprintf(names[0], sizeof names[0], "audio/%s/song_%s_preview.bnk", platform, song);
  snprintf(names[1], sizeof names[1], "audio/%s/%u.wem", platform, pid);
  snprintf(names[2], sizeof names[2], "audio/%s/song_%s.bnk", platform, song);
  snprintf(names[3], sizeof names[3], "audio/%s/%u.wem", platform, id);
  const char *n[] = {names[0], names[1], names[2], names[3]};
  const uint8_t *b[] = {pbank, preview, bank, wem};
  size_t l[] = {sizeof pbank, sizeof preview, sizeof bank, sizeof wem};
  return make_psarc(key, 4, n, b, l, len);
}

static void write_bytes(const char *path, const void *data, size_t len) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK(fd >= 0 && write(fd, data, len) == (ssize_t)len);
  close(fd);
}

// 1 when the song's .wem read from where the index found it has this ID and is the audio song_psarc made
static int wem_is(const char *song, const uint8_t key[32], uint32_t id, uint8_t fill) {
  const char *path = stems_index_find(song);
  if (!path) return 0;
  uint32_t got = 0, seed = id;
  size_t len = 0;
  uint8_t *w = stems_index_wem(path, song, key, &got, &len);
  int ok = w && got == id && len == WEM_LEN;
  for (int i = 0; ok && i < WEM_LEN; i++) ok = w[i] == (uint8_t)(fill ^ ((seed = seed * 1664525u + 1013904223u) >> 24));
  free(w);
  return ok;
}

static int ends(const char *s, const char *end) {
  size_t n = s ? strlen(s) : 0, m = strlen(end);
  return s && n >= m && !strcmp(s + n - m, end);
}

#ifndef NO_LAYER
// --- the provider: archives in memory by their path's end, a real name hidden, and counts of what it was asked ---
typedef struct {
  const char *end;  // "/dlc/sub/served_m.psarc"
  const uint8_t *data;
  size_t len;
  int fail_reads;
} served;
static served g_served[4];
static int g_nserved, g_opens, g_open_now, g_listed;

static served *find_served(const char *path) {
  for (int i = 0; i < g_nserved; i++)
    if (path[0] == '/' && ends(path, g_served[i].end)) return &g_served[i];  // the layer gives whole paths
  return NULL;
}
static void *t_open(void *user, const char *path, rc_file_info *info) {
  served *s = find_served(path);
  if (!s) return NULL;
  *info = (rc_file_info){(int64_t)s->len, 1700000000};
  g_opens++, g_open_now++;
  return s;
}
static int64_t t_read(void *user, void *file, void *buf, uint64_t n, int64_t off) {
  served *s = file;
  if (s->fail_reads) return -1;
  if (off >= (int64_t)s->len) return 0;
  if (n > s->len - (size_t)off) n = s->len - (size_t)off;
  if (n > 5000) n = 5000;  // a provider may give less than was asked
  memcpy(buf, s->data + off, n);
  return (int64_t)n;
}
static void t_close(void *user, void *file) { g_open_now--; }
static int t_stat(void *user, const char *path, rc_file_info *info) {
  served *s = find_served(path);
  if (s) *info = (rc_file_info){(int64_t)s->len, 1700000000};
  return s != NULL;
}
static void t_list(void *user, const char *dir, rc_file_add add, void *ctx) {
  g_listed++;
  for (int i = 0; i < g_nserved; i++) {
    const char *name = strrchr(g_served[i].end, '/');
    size_t dl = strlen(dir), el = (size_t)(name - g_served[i].end);
    if (dl >= el && !strncmp(dir + dl - el, g_served[i].end, el)) add(ctx, name + 1);
  }
}
static int t_hide(void *user, const char *dir, const char *name) { return !strcmp(name, "foreign_p.psarc"); }
#endif

int main(void) {
  char dir[] = "/tmp/rsmods-stems-XXXXXX";
  CHECK(mkdtemp(dir) && !chdir(dir));
  CHECK(stems_index_listed() == -1);
  uint8_t key[32], wrong[32];
  for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i * 7 + 3), wrong[i] = (uint8_t)i;
  CHECK(!mkdir("dlc", 0755) && !mkdir("dlc/sub", 0755) && !mkdir("dlc/empty", 0755));

  // on disk: a song in dlc, one two folders down, the game's own songs, a file that is no archive, one cut short,
  // one with another key, and the other platform's archive of a song
  size_t len = 0;
  uint8_t *a = song_psarc(key, "mac", "PlainSong", 1001, 1, &len);
  write_bytes("dlc/plain_m.psarc", a, len);
  write_bytes("dlc/cut_m.psarc", a, len / 3);  // its table is whole, its audio isn't
  free(a);
  a = song_psarc(key, "mac", "deepsong", 2001, 2, &len);
  write_bytes("dlc/sub/deep_m.psarc", a, len);
  write_bytes("dlc/sub/short_m.psarc", a, 20);
  free(a);
  a = song_psarc(key, "mac", "disc", 3001, 3, &len);
  write_bytes("songs.psarc", a, len);
  free(a);
  write_bytes("dlc/junk_m.psarc", "PSAR but not one", 16);
  write_bytes("dlc/notes.txt", "x", 1);
  a = song_psarc(wrong, "mac", "otherkey", 4001, 4, &len);
  write_bytes("dlc/otherkey_m.psarc", a, len);
  free(a);
  a = song_psarc(key, "windows", "foreign", 5001, 5, &len);  // the other platform's audio, with its own file ID
  write_bytes("dlc/foreign_p.psarc", a, len);
  free(a);

#ifdef NO_LAYER
  CHECK(!rsm_files_layer());
  // every archive that reads: plain, cut (it has its names), deep, foreign_p, songs
  CHECK(stems_index_build(key) == 5);
  CHECK(wem_is("plainsong", key, 1001, 1) && wem_is("DEEPSONG", key, 2001, 2) && wem_is("disc", key, 3001, 3));
  CHECK(ends(stems_index_find("foreign"), "dlc/foreign_p.psarc") && wem_is("foreign", key, 5001, 5));
  CHECK(!stems_index_find("otherkey") && !stems_index_find("served") && !stems_index_find("plainsong_preview"));
#else
  CHECK(rsm_files_layer());
  // the layer with no provider is the files on disk
  CHECK(stems_index_build(key) == 5 && wem_is("plainsong", key, 1001, 1));
  CHECK(ends(stems_index_find("foreign"), "dlc/foreign_p.psarc"));

  // a provider: an archive that is no file (a package made into one), this platform's archive of `foreign` in place
  // of the file, which it hides, and one whose reads fail
  static const rc_file_provider p = {.open = t_open, .read = t_read, .close = t_close, .stat = t_stat, .list = t_list,
                                     .hide = t_hide};
  size_t l1 = 0, l2 = 0, l3 = 0;
  uint8_t *s1 = song_psarc(key, "mac", "ServedSongE0467599", 6001, 6, &l1);
  uint8_t *s2 = song_psarc(key, "mac", "foreign", 7001, 7, &l2);
  uint8_t *s3 = song_psarc(key, "mac", "broken", 8001, 8, &l3);
  g_served[0] = (served){"/dlc/sub/served_m.psarc", s1, l1, 0};
  g_served[1] = (served){"/dlc/foreign_m.psarc", s2, l2, 0};
  g_served[2] = (served){"/dlc/broken_m.psarc", s3, l3, 1};
  g_nserved = 3;
  CHECK(!rc_file_provide(&p));
  struct stat st;
  CHECK(stat("dlc/sub/served_m.psarc", &st) && stat("dlc/foreign_m.psarc", &st));  // no such files
  // plain, cut, deep, songs, and the two served ones that read; foreign_p is hidden and broken_m doesn't read
  CHECK(stems_index_build(key) == 6);
  CHECK(g_listed >= 3 && g_opens >= 3);
  CHECK(ends(stems_index_find("servedsonge0467599"), "dlc/sub/served_m.psarc"));
  CHECK(wem_is("ServedSongE0467599", key, 6001, 6));
  // one song, once, under the name that opens: the served archive's audio and file ID, not the hidden file's
  CHECK(ends(stems_index_find("foreign"), "dlc/foreign_m.psarc") && wem_is("foreign", key, 7001, 7));
  CHECK(!stems_index_find("broken"));
  CHECK(wem_is("plainsong", key, 1001, 1) && wem_is("deepsong", key, 2001, 2) && wem_is("disc", key, 3001, 3));
  CHECK(g_open_now == 0);  // every served file was closed by the layer

  // an archive that stops reading after it was listed gives no .wem, and nothing else
  g_served[0].fail_reads = 1;
  uint32_t id = 9;
  size_t wl = 9;
  CHECK(!stems_index_wem(stems_index_find("servedsonge0467599"), "servedsonge0467599", key, &id, &wl) && !id && !wl);
  g_served[0].fail_reads = 0;
  CHECK(g_open_now == 0);
  // and one that has gone is no longer there after the next listing
  g_nserved = 0;
  CHECK(stems_index_build(key) == 4 && !stems_index_find("servedsonge0467599"));
  CHECK(!stems_index_find("foreign"));  // still hidden, as the provider says
  free(s1), free(s2), free(s3);
#endif

  // what the archives don't give: a cut file's audio, a song of another archive, a missing archive
  uint32_t id2 = 0;
  size_t wl2 = 0;
  CHECK(!stems_index_wem("dlc/cut_m.psarc", "plainsong", key, &id2, &wl2) && !id2);
  CHECK(!stems_index_wem("dlc/plain_m.psarc", "deepsong", key, &id2, &wl2));
  CHECK(!stems_index_wem("dlc/none_m.psarc", "plainsong", key, &id2, &wl2));
  CHECK(!stems_index_wem("dlc/junk_m.psarc", "plainsong", key, &id2, &wl2));

  // the list for two threads: the path copied out, when it fits
  char found[2048], tiny[8];
  CHECK(stems_index_listed() >= 4 && stems_index_path("DeepSong", found, sizeof found));
  CHECK(ends(found, "dlc/sub/deep_m.psarc") && !stems_index_path("deepsong", tiny, sizeof tiny));
  CHECK(!stems_index_path("nosuchsong", found, sizeof found));

  // a song's names from its archive (song_info.c): the pack's header has them, for each of its songs; an archive
  // without a header has them in an arrangement's manifest; one without manifests has none
  static const char hsan[] =
      "{\"Entries\":{\"A1\":{\"Attributes\":{\"ArtistName\":\"First Band\",\"SongKey\":\"PackOne\","
      "\"SongName\":\"One\"}},\"B2\":{\"Attributes\":{\"AlbumName\":\"Second Album\",\"ArtistName\":\"Second Band\","
      "\"SongKey\":\"PackTwo\",\"SongName\":\"Two\"}}},\"InsertRoot\":\"Static.Songs.Headers\"}";
  static const char lead[] =
      "{\"Entries\":{\"C3\":{\"Attributes\":{\"ArtistName\":\"Lone Band\",\"SongKey\":\"Lone\",\"SongName\":\"Alone\"}}}}";
  CHECK(!mkdir("other", 0755));
  const char *pn[] = {"manifests/songs_dlc_pack/songs_dlc_pack.hsan", "audio/mac/song_packtwo.bnk"};
  const uint8_t *pb[] = {(const uint8_t *)hsan, (const uint8_t *)"BKHD"};
  size_t pl[] = {sizeof hsan - 1, 4};
  a = make_psarc(key, 2, pn, pb, pl, &len);
  write_bytes("other/pack_m.psarc", a, len);
  free(a);
  const char *ln[] = {"manifests/songs_dlc_lone/lone_vocals.json", "manifests/songs_dlc_lone/lone_lead.json"};
  const uint8_t *lb[] = {(const uint8_t *)"{\"Entries\":{}}", (const uint8_t *)lead};
  size_t ll[] = {14, sizeof lead - 1};
  a = make_psarc(key, 2, ln, lb, ll, &len);
  write_bytes("other/lone_m.psarc", a, len);
  free(a);
  song_info info;
  CHECK(song_info_read("other/pack_m.psarc", "packtwo", key, &info));
  CHECK(!strcmp(info.title, "Two") && !strcmp(info.artist, "Second Band") && !strcmp(info.album, "Second Album"));
  CHECK(song_info_read("other/pack_m.psarc", "PackOne", key, &info) && !strcmp(info.title, "One") && !*info.album);
  CHECK(!song_info_read("other/pack_m.psarc", "lone", key, &info));
  CHECK(song_info_read("other/lone_m.psarc", "Lone", key, &info) && !strcmp(info.artist, "Lone Band"));
  CHECK(!song_info_read("other/lone_m.psarc", "Lone", wrong, &info));
  CHECK(!song_info_read("dlc/plain_m.psarc", "plainsong", key, &info));
  CHECK(!song_info_read("other/none_m.psarc", "lone", key, &info));

  stems_index_free();
  CHECK(!stems_index_find("plainsong"));

  char cmd[200];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
  chdir("/");
  CHECK(!system(cmd));
#ifdef NO_LAYER
  return check_done("stems index (files on disk)");
#else
  return check_done("stems index (file layer)");
#endif
}
