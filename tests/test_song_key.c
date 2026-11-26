// Nashville Numbers without the game (src/song_key.c, src/song_sng.c): chords named from their notes, numerals in
// both styles and both ways of counting a minor key, key names both ways, the key of made-up songs, an SNG made
// here, encrypted as the game's are, read back as its whole chart, and the harmony of a made-up song's two parts.
#include <CommonCrypto/CommonCryptor.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../src/song_harmony.h"
#include "../src/song_key.h"
#include "../src/song_sng.h"
#include "check.h"

#define FLAT "\xe2\x99\xad"

static sk_chord chord(int n, ...) {
  int m[8];
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  for (int i = 0; i < n; i++) m[i] = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  sk_chord c = {0};
  CHECK(sk_chord_of(m, n, &c));
  return c;
}

static int is(sk_chord c, int root, const char *q, int bass) { return c.root == root && !strcmp(c.quality, q) && c.bass == bass; }

static const char *numd(sk_chord c, const char *key, int roman, int relative, int simple) {
  static char out[32];
  sk_key k;
  CHECK(sk_key_parse(key, &k));
  sk_numeral(&c, &k, roman, relative, simple, out, sizeof out);
  return out;
}
static const char *num(sk_chord c, const char *key, int roman, int relative) { return numd(c, key, roman, relative, 0); }

// ---- an SNG, written as the toolkit lays it out ----
typedef struct {
  uint8_t *p;
  size_t n, cap;
} buf;
static void put(buf *b, const void *d, size_t n) {
  if (b->n + n > b->cap) b->p = realloc(b->p, b->cap = (b->n + n) * 2 + 64);
  if (d) memcpy(b->p + b->n, d, n);
  else memset(b->p + b->n, 0, n);
  b->n += n;
}
static void u32(buf *b, uint32_t v) { put(b, &v, 4); }
static uint8_t *at(buf *b, size_t n) {
  put(b, NULL, n);
  return b->p + b->n - n;
}
static void note(buf *b, float t, float sus, int string, int fret, int chord, int iteration) {
  uint8_t *q = at(b, 63);
  memcpy(q + 12, &t, 4);
  q[16] = (uint8_t)string, q[17] = (uint8_t)fret;
  int32_t c = chord, it = iteration;
  memcpy(q + 20, &c, 4);
  memcpy(q + 32, &it, 4);
  memcpy(q + 55, &sus, 4);
  u32(b, 0);  // no bends
}

static buf make_sng(void) {
  buf b = {0};
  u32(&b, 0);  // beats
  u32(&b, 2);  // phrases: the first's notes are in level 1, the second's in level 0
  int32_t one = 1, zero = 0;
  memcpy(at(&b, 44) + 4, &one, 4);
  memcpy(at(&b, 44) + 4, &zero, 4);
  u32(&b, 1);  // a chord template: G major, open (G2 B2 D3 G3 B3 G4)
  int32_t g[6] = {43, 47, 50, 55, 59, 67};
  memcpy(at(&b, 72) + 16, g, sizeof g);
  u32(&b, 0), u32(&b, 0);  // chord notes, vocals
  u32(&b, 2);              // phrase iterations: phrase 0, then 1
  memcpy(at(&b, 24), &zero, 4);
  memcpy(at(&b, 24), &one, 4);
  for (int i = 0; i < 8; i++) u32(&b, 0);  // extra info, linked, actions, events, tones, DNA, sections ... and
  b.n -= 4;                                // ... one less: the levels' count follows
  u32(&b, 2);
  // level 0: a note of iteration 0 (not this level's), and iteration 1's E on the low string, fret 0, capo 2
  u32(&b, 0);
  for (int i = 0; i < 4; i++) u32(&b, 0);
  u32(&b, 2);
  note(&b, 1.0f, 0, 0, 3, -1, 0);
  note(&b, 5.0f, 1.5f, 0, 0, -1, 1);
  u32(&b, 0), u32(&b, 0), u32(&b, 0);
  // level 1: iteration 0's G chord, then D on the A string fret 5, and iteration 1's note (not this level's)
  u32(&b, 1);
  for (int i = 0; i < 4; i++) u32(&b, 0);
  u32(&b, 3);
  note(&b, 2.0f, 0.5f, 0, 3, 0, 0);
  note(&b, 0.5f, 0, 1, 5, -1, 0);
  note(&b, 6.0f, 0, 0, 7, -1, 1);
  u32(&b, 0), u32(&b, 0), u32(&b, 0);
  uint8_t *meta = at(&b, 79);
  meta[40] = 2;  // capo
  u32(&b, 6);
  int16_t tuning[6] = {-2, 0, 0, 0, 0, 0};  // drop D
  put(&b, tuning, sizeof tuning);
  put(&b, NULL, 12);  // the rest of the metadata
  return b;
}

static buf encrypt_sng(const buf *plain) {
  static const uint8_t KEY_MAC[32] = {0x98, 0x21, 0x33, 0x0E, 0x34, 0xB9, 0x1F, 0x70, 0xD0, 0xA4, 0x8C, 0xBD, 0x62, 0x59, 0x93, 0x12,
                                      0x69, 0x70, 0xCE, 0xA0, 0x91, 0x92, 0xC0, 0xE6, 0xCD, 0xA6, 0x76, 0xCC, 0x98, 0x38, 0x28, 0x9D};
  buf z = {0};
  uint32_t len = (uint32_t)plain->n;
  put(&z, &len, 4);
  uLongf zn = compressBound(plain->n);
  uint8_t *zd = at(&z, zn);
  compress(zd, &zn, plain->p, plain->n);
  z.n = 4 + zn;
  put(&z, NULL, 56);  // the signature
  buf f = {0};
  u32(&f, 0x4a), u32(&f, 3);
  uint8_t iv[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xff};
  put(&f, iv, 16);
  uint8_t *out = at(&f, z.n);
  CCCryptorRef c;
  size_t moved;
  CCCryptorCreateWithMode(kCCEncrypt, kCCModeCTR, kCCAlgorithmAES, ccNoPadding, iv, KEY_MAC, 32, NULL, 0, 0, kCCModeOptionCTR_BE, &c);
  CCCryptorUpdate(c, z.p, z.n, out, z.n, &moved);
  CCCryptorRelease(c);
  free(z.p);
  return f;
}

static void add(sk_stats *s, const int (*chords)[3], int n, float each) {
  sk_event ev[64];
  for (int i = 0; i < n; i++) {
    ev[i] = (sk_event){.time = i * each, .sustain = each, .n = 3};
    memcpy(ev[i].midi, chords[i], sizeof chords[i]);
  }
  sk_stats_add(s, ev, n);
}

int main(void) {
  // chords
  CHECK(is(chord(3, 48, 52, 55), 0, "", 0));                  // C
  CHECK(is(chord(4, 45, 52, 57, 60), 9, "m", 9));             // Am
  CHECK(is(chord(3, 40, 47, 52), 4, "5", 4));                 // E5
  CHECK(is(chord(2, 45, 50), 2, "5", 9));                     // D5 over A: the fifth below
  CHECK(is(chord(6, 43, 47, 50, 55, 59, 67), 7, "", 7));      // open G
  CHECK(is(chord(4, 43, 47, 53, 62), 7, "7", 7));             // G7
  CHECK(is(chord(4, 52, 55, 59, 62), 4, "m7", 4));            // Em7
  CHECK(is(chord(4, 48, 52, 55, 59), 0, "maj7", 0));          // Cmaj7
  CHECK(is(chord(3, 47, 50, 53), 11, "dim", 11));             // Bdim
  CHECK(is(chord(3, 50, 55, 57), 2, "sus4", 2));              // Dsus4
  CHECK(is(chord(3, 52, 55, 60), 0, "", 4));                  // C/E
  CHECK(is(chord(1, 40), 4, "1", 4));
  CHECK(is(chord(2, 40, 52), 4, "1", 4));                     // octaves
  CHECK(sk_chord_kind("m7") == 'm' && sk_chord_kind("maj7") == 'M' && sk_chord_kind("5") == 'p' && sk_chord_kind("m7b5") == 'd');

  // numerals
  CHECK(!strcmp(num(chord(3, 48, 52, 55), "G", 1, 0), "IV"));
  CHECK(!strcmp(num(chord(3, 48, 52, 55), "G", 0, 0), "4"));
  CHECK(!strcmp(num(chord(4, 52, 55, 59, 62), "G", 1, 0), "vi7"));
  CHECK(!strcmp(num(chord(4, 52, 55, 59, 62), "G", 0, 0), "6m7"));
  CHECK(!strcmp(num(chord(3, 41, 48, 53), "G", 1, 0), FLAT "VII5"));
  CHECK(!strcmp(num(chord(3, 41, 48, 53), "G", 0, 0), FLAT "75"));
  CHECK(!strcmp(num(chord(3, 47, 50, 53), "C", 1, 0), "vii\xc2\xb0"));
  CHECK(!strcmp(num(chord(3, 52, 55, 60), "C", 1, 0), "I/3"));
  CHECK(!strcmp(num(chord(3, 52, 55, 60), "C", 0, 0), "1/3"));
  CHECK(!strcmp(num(chord(4, 45, 52, 57, 60), "Am", 1, 0), "i"));
  CHECK(!strcmp(num(chord(4, 45, 52, 57, 60), "Am", 0, 0), "1m"));
  CHECK(!strcmp(num(chord(4, 45, 52, 57, 60), "Am", 0, 1), "6m"));  // from C
  CHECK(!strcmp(num(chord(3, 41, 45, 48), "Am", 1, 0), FLAT "VI"));
  CHECK(!strcmp(num(chord(3, 41, 45, 48), "Am", 0, 1), "4"));

  // simple: the chord's job
  CHECK(!strcmp(numd(chord(3, 41, 48, 53), "G", 0, 0, 1), FLAT "7"));                   // F5: no 5
  CHECK(!strcmp(numd(chord(3, 41, 48, 53), "G", 1, 0, 1), FLAT "VII"));
  CHECK(!strcmp(numd(chord(3, 45, 47, 52), "C#m", 1, 0, 1), FLAT "VI"));                // Asus2
  CHECK(!strcmp(numd(chord(3, 50, 55, 57), "G", 0, 0, 1), "5sus"));                    // Dsus4
  CHECK(!strcmp(numd(chord(4, 48, 52, 55, 59), "C", 1, 0, 1), "I"));                   // Cmaj7
  CHECK(!strcmp(numd(chord(4, 43, 47, 53, 62), "C", 0, 0, 1), "57"));                  // G7 stays
  CHECK(!strcmp(numd(chord(4, 52, 55, 59, 62), "G", 1, 0, 1), "vi7"));                 // Em7 stays
  CHECK(!strcmp(numd(chord(5, 43, 47, 50, 53, 57), "C", 1, 0, 1), "V7"));              // G9: its 7
  CHECK(!strcmp(numd(chord(3, 52, 55, 60), "C", 0, 0, 1), "1/3"));
  CHECK(!strcmp(numd(chord(3, 47, 50, 53), "C", 1, 0, 1), "vii\xc2\xb0"));

  // key names
  sk_key k;
  char name[16];
  CHECK(sk_key_parse("F#m", &k) && k.tonic == 6 && k.mode == SK_MINOR);
  CHECK(sk_key_parse(" bb minor ", &k) && k.tonic == 10 && k.mode == SK_MINOR);
  CHECK(sk_key_parse("E" FLAT, &k) && k.tonic == 3 && k.mode == SK_MAJOR);
  CHECK(sk_key_parse("Cb", &k) && k.tonic == 11);
  CHECK(sk_key_parse("d major", &k) && k.tonic == 2 && k.mode == SK_MAJOR);
  CHECK(!sk_key_parse("H", &k) && !sk_key_parse("Gx", &k) && !sk_key_parse("", &k));
  sk_key_name(&(sk_key){10, SK_MAJOR, 0}, name, sizeof name);
  CHECK(!strcmp(name, "B" FLAT));
  sk_key_name(&(sk_key){8, SK_DORIAN, 0}, name, sizeof name);
  CHECK(!strcmp(name, "G#m"));

  // made-up songs: G C D G in G; Am G F E Am in A minor; nothing, no key
  sk_stats s;
  sk_key got[3];
  sk_stats_init(&s);
  CHECK(sk_guess(&s, got, 3) == 0);
  static const int in_g[][3] = {{43, 47, 50}, {48, 52, 55}, {50, 54, 57}, {43, 47, 50}, {40, 43, 47}, {48, 52, 55}, {50, 54, 57}, {43, 47, 50}};
  add(&s, in_g, 8, 2);
  CHECK(sk_guess(&s, got, 3) == 3 && got[0].tonic == 7 && !sk_minor(got[0].mode) && got[1].tonic != 7 && got[2].tonic != got[1].tonic);
  static const int in_am[][3] = {{45, 48, 52}, {43, 47, 50}, {41, 45, 48}, {40, 44, 47}, {45, 48, 52}, {43, 47, 50}, {41, 45, 48}, {40, 44, 47}, {45, 48, 52}};
  sk_stats_init(&s);
  add(&s, in_am, 9, 2);
  CHECK(sk_guess(&s, got, 1) == 1 && got[0].tonic == 9 && sk_minor(got[0].mode));

  // an SNG: the whole chart from the levels its phrases name, single notes from the tuning and the capo
  buf plain = make_sng(), file = encrypt_sng(&plain);
  size_t n = 0;
  uint8_t *back = sng_decrypt(file.p, file.n, 1, &n);
  CHECK(back && n == plain.n && !memcmp(back, plain.p, n));
  CHECK(sng_decrypt(file.p, file.n, 0, &n) == NULL);  // the PC's key
  sng_arrangement a;
  CHECK(back && sng_parse(back, n, 0, 0, &a));
  if (back && a.ev) {
    CHECK(a.levels == 2 && a.level_notes[0] == 2 && a.level_notes[1] == 3);
    CHECK(a.n == 3);
    CHECK(a.ev[0].time == 0.5f && a.ev[0].n == 1 && a.ev[0].midi[0] == 50);  // D3: A string, fret 5
    CHECK(a.ev[1].time == 2.0f && a.ev[1].n == 6 && a.ev[1].midi[0] == 43 && a.ev[1].sustain == 0.5f);
    CHECK(a.ev[2].time == 5.0f && a.ev[2].midi[0] == 40 && a.ev[2].sustain == 1.5f);  // drop D, open under capo 2: E2
    sng_free(&a);
  }
  CHECK(back && sng_parse(back, n, 1, -1200, &a) && a.n == 3 && a.ev[0].midi[0] == 38 - 12 && a.ev[1].midi[0] == 43 - 12);  // a bass
  sng_free(&a);
  CHECK(back && !sng_parse(back, n - 20, 0, 0, &a));  // cut short
  free(back);
  // unencrypted (flags 1): the zlib stream right after the IV slot
  buf open = {0};
  u32(&open, 0x4a), u32(&open, 1);
  put(&open, NULL, 16);
  uint32_t len = (uint32_t)plain.n;
  put(&open, &len, 4);
  uLongf zn = compressBound(plain.n);
  uint8_t *zd = at(&open, zn);
  compress(zd, &zn, plain.p, plain.n);
  open.n = 28 + zn;
  back = sng_decrypt(open.p, open.n, 1, &n);
  CHECK(back && n == plain.n && !memcmp(back, plain.p, n));
  free(back), free(open.p);
  // a native chart (SNXR: a 6-string guitar in a native header, which the format allows) in a plain SNG comes back as the
  // stock body the parser reads, the same notes
  {
    buf nat = {0};
    put(&nat, "SNXR\x01\x00\x06\x00", 8);
    put(&nat, plain.p, plain.n);
    put(&nat, NULL, 256);  // the padding after MetaData
    buf nf = {0};
    u32(&nf, 0x4a), u32(&nf, 1);
    put(&nf, NULL, 16);
    uint32_t nlen = (uint32_t)nat.n;
    put(&nf, &nlen, 4);
    uLongf nzn = compressBound(nat.n);
    uint8_t *nzd = at(&nf, nzn);
    compress(nzd, &nzn, nat.p, nat.n);
    nf.n = 28 + nzn;
    put(&nf, NULL, 56);
    size_t nn = 0;
    uint8_t *conv = sng_decrypt(nf.p, nf.n, 1, &nn);
    sng_arrangement na;
    CHECK(conv && nn == nat.n && memcmp(conv, "SNXR", 4));
    CHECK(conv && sng_parse(conv, nn, 0, 0, &na) && na.n == 3 && na.ev[0].midi[0] == 50 && na.ev[2].midi[0] == 40);
    if (conv && na.ev) sng_free(&na);
    free(conv), free(nf.p), free(nat.p);
  }
  file.p[30] ^= 1;
  CHECK(sng_decrypt(file.p, file.n, 1, &n) == NULL);
  free(file.p), free(plain.p);
  // harmony: a guitar strumming a chord a bar, a bass on whole notes; C G Am F, C over a B, E5; then a bar of nothing.
  // Bars of four beats, half a second each.
  static const int strum[6][4] = {{48, 52, 55, 60}, {43, 47, 50, 55}, {45, 52, 57, 60}, {41, 48, 53, 57}, {48, 52, 55, 60}, {40, 47, 52, 0}};
  static const int bass_notes[6] = {36, 31, 33, 29, 35, 28};
  sk_event gtr[12], bs[6];
  int ng = 0;
  for (int bar = 0; bar < 6; bar++) {
    for (int half = 0; half < 2; half++) {  // struck on beats 1 and 3
      sk_event *e = &gtr[ng++];
      *e = (sk_event){.time = bar * 2.0f + half, .sustain = 0.9f, .n = strum[bar][3] ? 4 : 3, .chord = bar};
      memcpy(e->midi, strum[bar], sizeof strum[bar]);
    }
    bs[bar] = (sk_event){.time = bar * 2.0f, .sustain = 1.9f, .n = 1, .midi = {bass_notes[bar]}, .chord = -1};
  }
  sng_beat grid[32];
  for (int i = 0; i < 32; i++) grid[i] = (sng_beat){i * 0.5f, i / 4, i % 4};
  sh_part parts[2] = {{gtr, ng, 0}, {bs, 6, 1}};
  sh_chord *hc = NULL;
  sk_key in_c = {0, SK_MAJOR, 0};
  int nh = sh_infer(parts, 2, grid, 32, &in_c, &hc);
  CHECK(nh == 6);
  if (nh == 6) {
    static const char *const want[6] = {"I", "V", "vi", "IV", "I/7", "III5"};
    for (int i = 0; i < 6; i++) {
      char got_num[32];
      sk_numeral(&hc[i].chord, &in_c, 1, 0, 0, got_num, sizeof got_num);
      CHECK(!strcmp(got_num, want[i]));
      if (strcmp(got_num, want[i])) fprintf(stderr, "  chord %d: %s, not %s\n", i, got_num, want[i]);
      CHECK(hc[i].start == i * 2.0f && hc[i].end == (i + 1) * 2.0f);  // a bar each, its halves one
    }
  }
  free(hc);
  CHECK(sh_infer(parts, 2, grid, 1, &in_c, &hc) == 0 && hc == NULL);  // no grid
  return check_done("song_key");
}
