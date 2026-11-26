// The headstock's pure part (src/seven_headstock_build.h): the RSXRH1 program interpreter on a made-up package, the
// names a program may make, the frames' names, the DXT1 dimming, the texture names of a nif, a bass's segment names
// and the width's matrix. The game's side (seven_headstock.c) can't run here.
#include <CommonCrypto/CommonDigest.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "check.h"
#include "seven_headstock_build.h"

// ---- a package made here: texts, hex, SHA-256s
typedef struct {
  uint8_t *b;
  size_t n, cap;
} buf_t;
static void put(buf_t *o, const void *p, size_t n) {
  if (o->n + n > o->cap) o->b = realloc(o->b, o->cap = (o->n + n) * 2);
  memcpy(o->b + o->n, p, n);
  o->n += n;
}
static void put32(buf_t *o, uint32_t v) {
  uint8_t q[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  put(o, q, 4);
}
static void sha(const void *p, size_t n, uint8_t out[32]) { CC_SHA256(p, (CC_LONG)n, out); }

// An output: name, then ops: a literal run, then a copy (template 1, from 2, 4 bytes), then a literal
static void put_output(buf_t *o, const char *name, const uint8_t *tpl1, uint32_t bad_size_delta) {
  uint8_t want[40];
  size_t n = 0;
  const char lit1[] = "head", lit2[] = "tail";
  memcpy(want + n, lit1, 4), n += 4;
  memcpy(want + n, tpl1 + 2, 4), n += 4;
  memcpy(want + n, lit2, 4), n += 4;
  uint8_t h[32];
  sha(want, n, h);
  put32(o, (uint32_t)strlen(name));
  put(o, name, strlen(name));
  put32(o, (uint32_t)n + bad_size_delta);
  put(o, h, 32);
  put32(o, 3);
  uint8_t tag = 0;
  put(o, &tag, 1), put32(o, 4), put(o, lit1, 4);
  tag = 1;
  put(o, &tag, 1), put32(o, 1), put32(o, 2), put32(o, 4);
  tag = 0;
  put(o, &tag, 1), put32(o, 4), put(o, lit2, 4);
}

// The program text: magic, hex of the binary
static uint8_t *make_package(const uint8_t *t0, size_t l0, const uint8_t *t1, size_t l1, const char *const *names,
                             int nnames, uint32_t bad_size_delta, size_t *len) {
  buf_t bin = {0};
  uint8_t h[32];
  put32(&bin, 1);
  sha(t0, l0, h), put(&bin, h, 32);
  sha(t1, l1, h), put(&bin, h, 32);
  put32(&bin, (uint32_t)nnames);
  for (int i = 0; i < nnames; i++) put_output(&bin, names[i], t1, bad_size_delta);
  size_t n = 7 + bin.n * 2;
  uint8_t *text = malloc(n);
  memcpy(text, "RSXRH1\n", 7);
  for (size_t i = 0; i < bin.n; i++) {
    text[7 + 2 * i] = "0123456789abcdef"[bin.b[i] >> 4];
    text[8 + 2 * i] = "0123456789abcdef"[bin.b[i] & 15];
  }
  free(bin.b);
  *len = n;
  return text;
}

static void test_build(void) {
  uint8_t t0[16], t1[16];
  for (int i = 0; i < 16; i++) t0[i] = (uint8_t)(i * 3), t1[i] = (uint8_t)(100 + i);
  const uint8_t *const tpl[2] = {t0, t1};
  const size_t tlen[2] = {16, 16};
  const char *const names[] = {"rsxr_bass6.nif", "rsxr_bass5.nif", "rsxrhs_wood.dds"};
  size_t plen;
  uint8_t *pkg = make_package(t0, 16, t1, 16, names, 3, 0, &plen);
  hs_file out[HS_MAX_OUT];
  int count = -1;
  const char *why = hs_build(pkg, plen, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(!why);
  CHECK(count == 3);
  CHECK(!strcmp(out[0].name, "rsxr_bass6.nif") && !strcmp(out[2].name, "rsxrhs_wood.dds"));
  CHECK(out[0].size == 12 && out[0].data && !memcmp(out[0].data, "head", 4) && !memcmp(out[0].data + 4, t1 + 2, 4) &&
        !memcmp(out[0].data + 8, "tail", 4));
  CHECK(hs_find(out, count, "rsxr_bass5.nif") == &out[1]);
  CHECK(!hs_find(out, count, "rsxr_nothing.nif"));
  hs_free(out, HS_MAX_OUT);
  CHECK(!out[0].data && !out[0].name[0]);

  // only the outputs asked for have their bytes (the others are still named and sized)
  why = hs_build(pkg, plen, tpl, tlen, 1u << 1, out, HS_MAX_OUT, &count);
  CHECK(!why && !out[0].data && out[1].data && !out[2].data && out[0].size == 12 && !strcmp(out[0].name, "rsxr_bass6.nif"));
  hs_free(out, HS_MAX_OUT);

  // refusals leave nothing
  uint8_t bad1[16];
  memcpy(bad1, t1, 16), bad1[5] ^= 1;
  const uint8_t *const tbad[2] = {t0, bad1};
  why = hs_build(pkg, plen, tbad, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !strcmp(why, "template hash mismatch") && !out[0].data && count == 0);
  const size_t tshort[2] = {16, 0};
  why = hs_build(pkg, plen, tpl, tshort, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !out[0].data);
  why = hs_build(pkg, plen - 2, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why);  // truncated
  why = hs_build(pkg, plen - 1, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !strcmp(why, "package hex length"));
  uint8_t *wrong = malloc(plen);
  memcpy(wrong, pkg, plen);
  memcpy(wrong, "RS7H1\n\n", 7);
  why = hs_build(wrong, plen, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !strcmp(why, "package magic/size"));  // the old program's magic
  memcpy(wrong, pkg, plen);
  wrong[plen - 1] = 'g';
  why = hs_build(wrong, plen, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !strcmp(why, "package hex digit"));
  free(wrong);
  why = hs_build(pkg, plen, tpl, tlen, ~0u, out, 2, &count);  // more outputs than the caller has room for
  CHECK(why && !strcmp(why, "artifact count"));
  free(pkg);

  // a size that is not what the instructions make, an unsafe or repeated name
  pkg = make_package(t0, 16, t1, 16, names, 1, 1, &plen);
  why = hs_build(pkg, plen, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !out[0].data);
  free(pkg);
  const char *const unsafe[] = {"../rsxr_x.nif"};
  pkg = make_package(t0, 16, t1, 16, unsafe, 1, 0, &plen);
  why = hs_build(pkg, plen, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !strcmp(why, "artifact name"));
  free(pkg);
  const char *const twice[] = {"rsxr_a.nif", "rsxr_a.nif"};
  pkg = make_package(t0, 16, t1, 16, twice, 2, 0, &plen);
  why = hs_build(pkg, plen, tpl, tlen, ~0u, out, HS_MAX_OUT, &count);
  CHECK(why && !strcmp(why, "artifact name repeated"));
  free(pkg);

  // the unpacking: the gzip stream must be the program's size and SHA-256
  pkg = make_package(t0, 16, t1, 16, names, 1, 0, &plen);
  uLongf gzn = compressBound((uLong)plen) + 64;
  uint8_t *gz = malloc(gzn);
  z_stream z;
  memset(&z, 0, sizeof z);
  deflateInit2(&z, 9, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  z.next_in = pkg, z.avail_in = (uInt)plen, z.next_out = gz, z.avail_out = (uInt)gzn;
  deflate(&z, Z_FINISH);
  size_t gzlen = z.total_out;
  deflateEnd(&z);
  hs_package pk = {gz, gzlen, plen, {0}};
  sha(pkg, plen, pk.sha);
  size_t got = 0;
  const char *w = NULL;
  uint8_t *un = hs_package_unpack(&pk, &got, &w);
  CHECK(un && got == plen && !memcmp(un, pkg, plen));
  free(un);
  pk.sha[0] ^= 1;
  CHECK(!hs_package_unpack(&pk, &got, &w) && !strcmp(w, "package hash mismatch"));
  pk.sha[0] ^= 1;
  pk.size = plen + 1;
  CHECK(!hs_package_unpack(&pk, &got, &w) && !strcmp(w, "package unpack"));
  free(gz);
  free(pkg);
}

static void test_names(void) {
  CHECK(hs_name_ok("rsxr_inline8.nif") && hs_name_ok("rsxr_bass5_lh.nif") && hs_name_ok("rsxrhs_hardware_gloss.dds"));
  CHECK(!hs_name_ok("rs7_inline7.nif"));  // the old programs' names
  CHECK(!hs_name_ok("rsxr_.nif") && !hs_name_ok("rsxr_a.dds") && !hs_name_ok("rsxrhs_a.nif"));
  CHECK(!hs_name_ok("rsxr_A.nif") && !hs_name_ok("rsxr_a/b.nif") && !hs_name_ok("rsxr_a.b.nif") && !hs_name_ok(NULL));
  CHECK(!hs_name_ok("rsxr_inline7.nif.exe") && !hs_name_ok(""));
}

static void test_frames(void) {
  hs_frame f = hs_frame_of("data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif");
  CHECK(f.kind == HS_FRAME_GUITAR_INLINE && !f.lh && !f.peg && !strcmp(f.base, "epi_wilshire_cherry"));
  f = hs_frame_of("assets\\GAMEPLAY\\guitar_standard_inline\\epi_whilshire_cherry_lh.nif");
  CHECK(f.kind == HS_FRAME_GUITAR_INLINE && f.lh);
  f = hs_frame_of("assets/gameplay/guitar_standard/gib_lespaul_lh.nif");
  CHECK(f.kind == HS_FRAME_GUITAR_33 && f.lh && !f.peg);
  f = hs_frame_of("assets/gameplay/guitar_standard/peg_bakelite.nif");
  CHECK(f.kind == HS_FRAME_GUITAR_33 && f.peg);
  f = hs_frame_of("assets/gameplay/guitar_bass_inline/bass_thunder.nif");
  CHECK(f.kind == HS_FRAME_BASS_INLINE && !f.lh);
  f = hs_frame_of("assets/gameplay/guitar_bass_inline/bass_thunder_lh.nif");
  CHECK(f.kind == HS_FRAME_BASS_INLINE && f.lh);
  f = hs_frame_of("assets/gameplay/guitar_bass/bass_2x2.nif");
  CHECK(f.kind == HS_FRAME_BASS_22);
  CHECK(hs_frame_of("assets/gameplay/guitar_bass_inline/peg_2clove.nif").kind == HS_FRAME_NONE);  // a bass's peg
  CHECK(hs_frame_of("assets/gameplay/guitar_bass/peg_2clove.nif").kind == HS_FRAME_NONE);
  CHECK(hs_frame_of("assets/gameplay/guitar_standard_inline/a.dds").kind == HS_FRAME_NONE);
  CHECK(hs_frame_of("assets/gameplay/guitar_standard_inline/sub/a.nif").kind == HS_FRAME_NONE);  // not directly in it
  CHECK(hs_frame_of("assets/gameplay/other/a.nif").kind == HS_FRAME_NONE);
  CHECK(hs_frame_of(NULL).kind == HS_FRAME_NONE && hs_frame_of(".nif").kind == HS_FRAME_NONE);
  CHECK(hs_frame_of("assets/gameplay/guitar_standard_inline/.nif").kind == HS_FRAME_NONE);
}

static void test_segments(void) {
  // a bass: E A D G are the stock string_size_10..7, the low B 11, the high C 6
  CHECK(hs_bass_seg(0) == 10 && hs_bass_seg(1) == 9 && hs_bass_seg(2) == 8 && hs_bass_seg(3) == 7);
  CHECK(hs_bass_seg(4) == 11 && hs_bass_seg(5) == 6);
  CHECK(hs_bass_neighbour(0) == 0 && hs_bass_neighbour(3) == 3 && hs_bass_neighbour(4) == 0 && hs_bass_neighbour(5) == 3);
  // the 8-peg model on a 7-string guitar: string_size_8 has no string to follow
  CHECK(!hs_extra_shown(0, 6) && hs_extra_shown(0, 7) && hs_extra_shown(0, 8));
  CHECK(!hs_extra_shown(1, 7) && hs_extra_shown(1, 8));
}

static float rnd(unsigned *s) { return (float)((*s = *s * 1103515245u + 12345u) >> 16 & 0x7fff) / 4096.0f; }

static void test_width(void) {
  const float in[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  float out[9];
  hs_width_matrix(in, 1.0f, out);
  CHECK(!memcmp(in, out, sizeof in));
  hs_width_matrix(in, 0.5f, out);
  CHECK(out[0] == 1 && out[1] == 2 && out[2] == 1.5f && out[3] == 4 && out[4] == 5 && out[5] == 3 && out[6] == 7 &&
        out[7] == 8 && out[8] == 4.5f);
  float again[9];
  hs_width_matrix(in, 2.0f, again);  // from the same stock copy, never from the last result
  hs_width_matrix(in, 1.0f, again);
  CHECK(!memcmp(in, again, sizeof in));
  (void)rnd;
}

// A 4x4 DXT1 texture: the dimmed blocks keep their mode and stay in order
static void test_dxt(void) {
  uint8_t dds[128 + 8 * 3];
  memset(dds, 0, sizeof dds);
  memcpy(dds, "DDS ", 4);
  memcpy(dds + 84, "DXT1", 4);
  uint16_t c[3][2] = {{0xffff, 0x0000}, {0x8410, 0x8410}, {0x1000, 0x2000}};  // 4-colour, equal ends, 3-colour
  for (int b = 0; b < 3; b++) {
    uint8_t *p = dds + 128 + 8 * b;
    p[0] = (uint8_t)c[b][0], p[1] = (uint8_t)(c[b][0] >> 8), p[2] = (uint8_t)c[b][1], p[3] = (uint8_t)(c[b][1] >> 8);
    p[4] = 0xe4, p[5] = 0xe4, p[6] = 0xe4, p[7] = 0xe4;
  }
  uint8_t orig[sizeof dds];
  memcpy(orig, dds, sizeof dds);
  CHECK(!hs_scale_dxt1(dds, sizeof dds, 1.0f) && !hs_scale_dxt1(dds, sizeof dds, 0.0f));
  CHECK(!memcmp(dds, orig, sizeof dds));
  CHECK(hs_scale_dxt1(dds, sizeof dds, 0.5f));
  for (int b = 0; b < 3; b++) {
    const uint8_t *p = dds + 128 + 8 * b, *q = orig + 128 + 8 * b;
    uint16_t a0 = (uint16_t)(p[0] | p[1] << 8), a1 = (uint16_t)(p[2] | p[3] << 8);
    uint16_t o0 = (uint16_t)(q[0] | q[1] << 8), o1 = (uint16_t)(q[2] | q[3] << 8);
    CHECK((a0 > a1) == (o0 > o1) || a0 == a1);  // the block's mode is the same (4-colour stays 4-colour)
    CHECK(((a0 >> 11) <= (o0 >> 11) + 1) && ((a1 >> 11) <= (o1 >> 11) + 1));  // darker, never brighter
  }
  CHECK(!hs_scale_dxt1(dds, 100, 0.5f));
  memcpy(dds + 84, "DXT5", 4);
  CHECK(!hs_scale_dxt1(dds, sizeof dds, 0.5f));
  CHECK(hs_is_wood_colour("rsxrhs_wood.dds") && !hs_is_wood_colour("rsxrhs_wood_gloss.dds") &&
        !hs_is_wood_colour("rsxrhs_wood_n.dds") && !hs_is_wood_colour("rsxrhs_hardware.dds") &&
        !hs_is_wood_colour("wood_normal.dds"));
}

static void test_dds_names(void) {
  const char nif[] = "xx data/gameplay/guitar/wood.dds\0yy hardware_gloss.DDS\0zz wood.dds\0 notdds .ddsx\0 ok/sub\\last.dds";
  char names[8][64];
  int n = hs_dds_names((const uint8_t *)nif, sizeof nif, names, 8);
  CHECK(n == 3 && !strcmp(names[0], "wood.dds") && !strcasecmp(names[1], "hardware_gloss.dds") && !strcmp(names[2], "last.dds"));
  CHECK(hs_name_in("WOOD.dds", names, n) && !hs_name_in("env.dds", names, n));
  CHECK(hs_dds_names(NULL, 0, names, 8) == 0);
  CHECK(hs_dds_names((const uint8_t *)nif, sizeof nif, names, 2) == 2);
}

int main(void) {
  test_build();
  test_names();
  test_frames();
  test_segments();
  test_width();
  test_dxt();
  test_dds_names();
  return check_done("seven_headstock");
}
