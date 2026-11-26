// A song's tunings (src/rsmods_tuning.c): the PSARC reader on archives made here, with a key of its own, the
// manifests, and RSMods' Extended Range rule
#include <CommonCrypto/CommonCryptor.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "check.h"
#include "rsmods_tuning.h"

static void put_be(uint8_t *p, uint64_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; i--, v >>= 8) p[i] = (uint8_t)v;
}

// An archive of `n` files (names[i], bodies[i]), each in blocks of `block`: zlib when it shrinks them, else as they are
static void write_psarc(const char *path, const uint8_t key[32], int encrypt, uint32_t block, int n, const char **names,
                        const char **bodies) {
  int entries = n + 1;
  char list[4096] = "";
  for (int i = 0; i < n; i++) strcat(strcat(list, names[i]), i + 1 < n ? "\n" : "");
  const char **files = calloc((size_t)entries, sizeof *files);
  files[0] = list;
  for (int i = 0; i < n; i++) files[i + 1] = bodies[i];
  int width = block <= 0x100 ? 1 : block <= 0x10000 ? 2 : 3;
  uint8_t *data = malloc(1 << 20), *table = calloc(1, 1 << 16);
  uint32_t *zlen = calloc(4096, sizeof *zlen);
  size_t used = 0;
  int nblocks = 0;
  uint32_t first[64];
  uint64_t offset[64], size[64];
  for (int e = 0; e < entries; e++) {
    size_t len = strlen(files[e]);
    first[e] = (uint32_t)nblocks;
    offset[e] = used;
    size[e] = len;
    for (size_t at = 0; at < len; at += block) {
      size_t part = len - at < block ? len - at : block;
      uLongf z = compressBound((uLong)part);
      uint8_t *buf = malloc(z);
      compress2(buf, &z, (const Bytef *)files[e] + at, (uLong)part, 9);
      if (z < part) {
        memcpy(data + used, buf, z);
        used += z;
        zlen[nblocks++] = (uint32_t)z;
      } else {  // as it is: a whole block is 0
        memcpy(data + used, files[e] + at, part);
        used += part;
        zlen[nblocks++] = part == block ? 0 : (uint32_t)part;
      }
      free(buf);
    }
  }
  uint32_t toc_end = 32 + (uint32_t)entries * 30 + (uint32_t)nblocks * (uint32_t)width;
  for (int e = 0; e < entries; e++) {
    uint8_t *p = table + (size_t)e * 30;
    put_be(p + 16, first[e], 4);
    put_be(p + 20, size[e], 5);
    put_be(p + 25, toc_end + offset[e], 5);
  }
  for (int b = 0; b < nblocks; b++) put_be(table + (size_t)entries * 30 + (size_t)b * width, zlen[b], width);
  size_t toc_len = toc_end - 32;
  if (encrypt) {
    static const uint8_t iv[16] = {0};
    size_t whole = (toc_len + 15) & ~(size_t)15, moved = 0;
    uint8_t *out = malloc(whole);
    CCCryptorRef c;
    CCCryptorCreateWithMode(kCCEncrypt, kCCModeCFB, kCCAlgorithmAES, ccNoPadding, iv, key, 32, NULL, 0, 0, 0, &c);
    CCCryptorUpdate(c, table, whole, out, whole, &moved);
    CCCryptorRelease(c);
    memcpy(table, out, toc_len);
    free(out);
  }
  uint8_t h[32] = "PSAR";
  put_be(h + 4, 0x00010004, 4);
  memcpy(h + 8, "zlib", 4);
  put_be(h + 12, toc_end, 4);
  put_be(h + 16, 30, 4);
  put_be(h + 20, (uint64_t)entries, 4);
  put_be(h + 24, block, 4);
  put_be(h + 28, encrypt ? 4 : 0, 4);
  FILE *f = fopen(path, "wb");
  fwrite(h, 1, 32, f);
  fwrite(table, 1, toc_len, f);
  fwrite(data, 1, used, f);
  fclose(f);
  free(data);
  free(table);
  free(zlen);
  free(files);
}

static rsm_arrangement arr(const char *name, int t0, int t1, int t2, int t3, int t4, int t5, double cents) {
  rsm_arrangement a = {.tuning = {t0, t1, t2, t3, t4, t5}, .cents = cents};
  snprintf(a.name, sizeof a.name, "%s", name);
  return a;
}

int main(void) {
  char dir[] = "/tmp/rsmods-tuning-XXXXXX";
  CHECK(mkdtemp(dir) && !chdir(dir));
  uint8_t key[32], wrong[32];
  CHECK(rsm_psarc_key_from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1F", key));
  CHECK(key[0] == 0 && key[15] == 15 && key[31] == 31);
  CHECK(!rsm_psarc_key_from_hex("00010203zz", wrong));
  memcpy(wrong, key, 32);
  wrong[5] ^= 1;

  // a big file: some blocks shrink with zlib, the random ones are kept as they are, the last one is short
  static char big[5000];
  srand(7);
  for (int i = 0; i < (int)sizeof big - 1; i++) big[i] = i < 2100 ? 'a' + i % 3 : (char)('!' + rand() % 90);
  const char *lead =
      "{\"Entries\":{\"X\":{\"Attributes\":{\"ArrangementName\":\"Lead\",\"CentOffset\":-1200.0,\"Tuning\":"
      "{\"string0\":-7, \"string1\":-5,\"string2\":-5,\"string3\":-5,\"string4\":-5,\"string5\":-5}}}}}";
  const char *vocals = "{\"Entries\":{\"Y\":{\"Attributes\":{\"ArrangementName\":\"Vocals\"}}}}";
  const char *names[] = {"manifests/songs_dlc_lowsong/lowsong_lead.json", "manifests/songs_dlc_lowsong/lowsong_vocals.json",
                         "songs/bin/macos/lowsong_lead.sng"};
  const char *bodies[] = {lead, vocals, big};
  for (int encrypt = 0; encrypt < 2; encrypt++)
    for (int b = 0; b < 2; b++) {
      uint32_t block = b ? 1024 : 65536;
      write_psarc("a.psarc", key, encrypt, block, 3, names, bodies);
      rsm_psarc *a = rsm_psarc_open("a.psarc", key);
      CHECK(a != NULL);
      if (!a) continue;
      CHECK(rsm_psarc_files(a) == 3);
      for (int i = 0; i < 3; i++) {
        CHECK(!strcmp(rsm_psarc_name(a, i), names[i]));
        size_t len = 0;
        char *body = rsm_psarc_read(a, i, &len);
        CHECK(body && len == strlen(bodies[i]) && !memcmp(body, bodies[i], len) && !body[len]);
        free(body);
      }
      CHECK(rsm_psarc_name(a, 3) == NULL && rsm_psarc_read(a, 3, NULL) == NULL);
      rsm_psarc_close(a);
      a = rsm_psarc_open("a.psarc", wrong);  // a wrong key reads an encrypted table as nonsense
      CHECK(encrypt ? a == NULL : a != NULL);
      rsm_psarc_close(a);
    }
  // a native package (magic PSXR, plain): opened, flagged, its manifests in the form the game is given. A 7-string guitar,
  // positional Tuning from the thickest (B E A D G B e): the stock strings come out in slot order, the low B apart.
  const char *seven =
      "{\"Entries\":{\"X\":{\"Attributes\":{\"ArrangementName\":\"Lead\",\"ExtendedRange\":{\"Version\":1},\"Tuning\":"
      "{\"string0\":-2,\"string1\":-2,\"string2\":0,\"string3\":0,\"string4\":0,\"string5\":0,\"string6\":3}}}}}";
  const char *nnames[] = {"manifests/songs_dlc_sev/sev_lead.json", "manifests/songs_dlc_sev/sev_vocals.json",
                          "manifests/songs_dlc_sev/sev.hsan", "songs/bin/macos/sev_lead.sng"};
  const char *nbodies[] = {seven, vocals, seven, big};
  write_psarc("n.psarc", key, 0, 1024, 4, nnames, nbodies);
  FILE *f = fopen("n.psarc", "r+b");
  fwrite("PSXR", 1, 4, f);
  fclose(f);
  rsm_psarc *na = rsm_psarc_open("n.psarc", key);
  CHECK(na && rsm_psarc_is_native(na) && rsm_psarc_files(na) == 4);
  if (na) {
    rsm_arrangement sa;
    for (int i = 0; i < 4; i += 2) {
      char *t = rsm_psarc_read(na, i, NULL);
      CHECK(t && !strstr(t, "ExtendedRange") && strstr(t, "Rs7String6"));
      CHECK(t && rsm_manifest_arrangement(t, &sa) && !strcmp(sa.name, "Lead") && sa.tuning[0] == -2 && sa.tuning[1] == 0 &&
            sa.tuning[4] == 0 && sa.tuning[5] == 3);
      free(t);
    }
    size_t len = 0;
    char *v = rsm_psarc_read(na, 1, &len), *sng = rsm_psarc_read(na, 3, &len);
    CHECK(v && !strcmp(v, vocals) && sng && len == strlen(big) && !memcmp(sng, big, len));  // not manifests: untouched
    free(v), free(sng);
    rsm_psarc_close(na);
  }
  // a plain package keeps its manifests as they are, a PSXR one can't be encrypted
  rsm_psarc *pa = rsm_psarc_open("a.psarc", key);
  CHECK(pa && !rsm_psarc_is_native(pa));
  rsm_psarc_close(pa);
  write_psarc("n2.psarc", key, 1, 1024, 4, nnames, nbodies);
  f = fopen("n2.psarc", "r+b");
  fwrite("PSXR", 1, 4, f);
  fclose(f);
  CHECK(rsm_psarc_open("n2.psarc", key) == NULL);
  f = fopen("not.psarc", "w");
  fputs("PSAR but not one", f);
  fclose(f);
  CHECK(rsm_psarc_open("not.psarc", key) == NULL && rsm_psarc_open("missing.psarc", key) == NULL);

  // manifests
  rsm_arrangement a;
  CHECK(rsm_manifest_arrangement(lead, &a) && !strcmp(a.name, "Lead") && a.tuning[0] == -7 && a.tuning[5] == -5 &&
        a.cents == -1200);
  CHECK(!rsm_manifest_arrangement(vocals, &a));
  CHECK(!rsm_manifest_arrangement("{\"Tuning\":{\"string0\":1}}", &a));  // all six or none
  CHECK(rsm_is_song_manifest("manifests/songs_dlc_lowsong/lowsong_lead.json", "LowSong"));
  CHECK(rsm_is_song_manifest("manifests/songs/lowsong_rhythm2.json", "lowsong"));
  CHECK(!rsm_is_song_manifest("manifests/songs/lowsongtwo_lead.json", "lowsong"));
  CHECK(!rsm_is_song_manifest("manifests/songs/lowsong_two_lead.json", "lowsong"));
  CHECK(!rsm_is_song_manifest("manifests/songs_dlc_lowsong/songs_dlc_lowsong.hsan", "lowsong"));
  CHECK(!rsm_is_song_manifest("songs/arr/lowsong_lead.json", "lowsong"));
  CHECK(rsm_is_bass(&(rsm_arrangement){.name = "Bass"}) && !rsm_is_bass(&(rsm_arrangement){.name = "Lead"}));

  // RSMods' rule, at B (-5)
  rsm_arrangement e_std = arr("Lead", 0, 0, 0, 0, 0, 0, 0), b_std = arr("Lead", -5, -5, -5, -5, -5, -5, 0),
                  c_std = arr("Lead", -4, -4, -4, -4, -4, -4, 0), cs_drop_b = arr("Lead", -5, -3, -3, -3, -3, -3, 0),
                  drop_a = arr("Lead", -7, -5, -5, -5, -5, -5, 0), low_b_string = arr("Lead", -5, 0, 0, 0, 0, 0, 0),
                  bass_a220 = arr("Bass", 7, 7, 7, 7, 7, 7, -1200), bass_e = arr("Bass", 0, 0, 0, 0, 0, 0, 0),
                  bass_open = arr("Bass", -7, -5, -5, -5, 0, 0, 0), a432 = arr("Lead", -5, -5, -5, -5, -5, -5, -31.8);
  CHECK(!rsm_extended_range_rule(&e_std, -5, 0, 0) && !rsm_extended_range_rule(&c_std, -5, 0, 0));
  CHECK(rsm_extended_range_rule(&b_std, -5, 0, 0) && rsm_extended_range_rule(&a432, -5, 0, 0));
  CHECK(rsm_extended_range_rule(&low_b_string, -5, 0, 0));
  CHECK(!rsm_extended_range_rule(&cs_drop_b, -5, 0, 0));  // a drop tuning counts from two lower
  CHECK(rsm_extended_range_rule(&cs_drop_b, -5, 1, 0));   // unless drop tunings count from the tuning
  CHECK(rsm_extended_range_rule(&drop_a, -5, 0, 0));
  CHECK(rsm_extended_range_rule(&bass_a220, -5, 0, 0) && !rsm_extended_range_rule(&bass_e, -5, 0, 0));
  CHECK(rsm_extended_range_rule(&b_std, -4, 0, 0) && !rsm_extended_range_rule(&b_std, -6, 0, 0));
  // an extended bass (Rs7String4): its slots 4 / 5 are the low B / high C at their own standard (0), so a 5-string
  // in standard is not low, and its four stock strings decide
  rsm_arrangement bass5 = arr("Bass", 0, 0, 0, 0, 0, 0, 0), bass5_low = arr("Bass", -5, -5, -5, -5, 0, 0, 0);
  bass5.extra = bass5_low.extra = 1;
  CHECK(!rsm_extended_range_rule(&bass5, -5, 0, 0) && rsm_extended_range_rule(&bass5_low, -5, 0, 0));
  rsm_arrangement parsed;
  CHECK(rsm_manifest_arrangement("{\"Entries\":{\"x\":{\"Attributes\":{\"ArrangementName\":\"Bass\",\"Tuning\":"
                                 "{\"string0\":0,\"string1\":0,\"string2\":0,\"string3\":0,\"string4\":0,"
                                 "\"string5\":0,\"Rs7String4\":0}}}}}", &parsed) && parsed.extra == 1);
  // a bass with its top strings left at E: not a drop tuning, unless they are fixed to the G string's
  CHECK(rsm_extended_range_rule(&bass_open, -7, 0, 0));      // lowest -7, not a drop: counts at -7
  CHECK(!rsm_extended_range_rule(&bass_open, -6, 0, 1));     // fixed: a drop, so it needs -6 - 2
  CHECK(rsm_extended_range_rule(&bass_open, -6, 1, 1));      // or drop tunings counting

  return check_done("rsmods_tuning");
}
