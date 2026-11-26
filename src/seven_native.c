// Seven-String Guitar (seven.h): the native N-string format, read. Ported from rs7string 1.0.0-beta.5/6
// (github.com/SomeeName/rs7string, MIT, by SomeeName): src/model/nativesng and nativemanifest (the converters) and
// src/features/native/hooks.cpp (the three hooks); the format is docs/release/NATIVE-FORMAT.md there (seven_native.h).
//
// rs7string beta.5 reads a native package by translating it back, while the game loads it, to its internal form;
// that form changed since the commit this port was made from (2edcf68), so what is written here is the old one, which
// the rest of this unit reads: the SNG's extension block is RS7X (upstream now RSXR), the manifest's extra keys
// Rs7String6 / Rs7String7 (upstream now RsxrString6 / 7), and the stock strings are in the game's slot order: string
// index i of an N-string guitar (thickest first) is slot 6 + (N - 7 - i) for the strings below the low E, and slot
// i - (N - 6) for the rest (a 7-string: i 0 = slot 6, the low B; i 1..6 = slots 0..5; an 8-string: i 0 = slot 7, i 1 =
// slot 6).
//   seven_native_psarc  PN1, the PSARC open function's compare with "PSAR" (the byte-swapped magic of the header,
//                       also kept for the table of contents): a "PSXR" magic is replaced by "PSAR" in both places.
//                       Only when Plain Packages is on: a native package is plain, the encrypted table of contents
//                       reader would take its table for ciphertext
//   seven_native_json   JM1, the entry of the JSON parser (all its callers, the array and object parsers too): a text
//                       with "ExtendedRange" (the whole document comes in at its first, outermost call; the rewritten
//                       text has no marker, so the nested calls and the parsers' loops leave it alone) is rewritten in
//                       a buffer of the game's memory, one a thread, and the text pointer the caller passed by
//                       reference is moved to it
//   seven_native_sng    XI1, SNG_Song_Load after the inflate and its size check, before the first section is read:
//                       a body starting "SNXR" is translated in place (the same size: the chart's zero padding after
//                       MetaData takes the RS7X block), one that cannot be goes to the loader's own failure exit
// The three points are taken together or the part stays off (seven_mid_optional); the unit stays on either way.
//
// An extended bass (instrument 1 with 5 or 6 strings) is translated when this RS Re-ARMed can build one
// (seven_feature(SEVEN_F_BASS), the bass part's points): the six stock slots hold its strings (the low B slot 4, a high
// C slot 5), its chart gets an RS7X block with STRS only, its manifest Rs7String4 (and Rs7String5). Otherwise its chart
// is refused with a log line (the song does not load) and its manifest gets a stock Tuning, as before. A four-string
// bass in a native header is a stock bass and loads. A six-string guitar in a native header is stock too (no block).
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seven.h"
#include "seven_native.h"

// ---- the three points -------------------------------------------------------------------------------------------
#define MAX_BODY (64u << 20)
#define MAX_JSON (16u << 20)
#define LOG_MAX 200

// "PSXR" and "PSAR" as the two games hold the header's first word: Learn & Play after its byte swap (the number
// 0x50535852), the Mac before it (the file's bytes read as a little-endian number)
#define PSXR_SWAPPED 0x50535852u
#define PSAR_SWAPPED 0x50534152u
#define PSXR_RAW 0x52585350u
#define PSAR_RAW 0x52415350u

static int g_on;
static int g_logs_sng, g_logs_json, g_logs_psarc, g_hint;

static void nlog(int *budget, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void nlog(int *budget, const char *fmt, ...) {
  if (__atomic_add_fetch(budget, 1, __ATOMIC_RELAXED) > LOG_MAX) return;
  char buf[300];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof buf, fmt, a);
  va_end(a);
  rsm_log("seven-string: native: %s", buf);
}

// Plain Packages' edits on (a native package is a plain archive); an RS Re-ARMed without rc_edit_is_on is taken to have them
static int plain_packages_on(void) {
  static int (*is_on)(const char *) = (int (*)(const char *))-1;
  if (is_on == (int (*)(const char *))-1) is_on = (int (*)(const char *))dlsym(RTLD_DEFAULT, "rc_edit_is_on");
  return !is_on || is_on("plain_packages");
}

// PN1: the header's magic at the compare with "PSAR"
static void on_psarc(rc_mid_frame *f, void *user) {
  if (!g_on || !seven_on) return;
  const uint32_t magic = (uint32_t)SEVEN_GET(f, "magic");
  const uint32_t stock = magic == PSXR_SWAPPED ? PSAR_SWAPPED : magic == PSXR_RAW ? PSAR_RAW : 0;
  if (!stock) return;
  if (!plain_packages_on()) {
    if (!__atomic_exchange_n(&g_hint, 1, __ATOMIC_RELAXED))
      rsm_log("seven-string: native: a package with the PSXR magic is refused: Plain Packages is off (a native package is plain)");
    return;
  }
  SEVEN_SET(f, "magic", stock);
  SEVEN_SET(f, "stored", stock);
  nlog(&g_logs_psarc, "package with the PSXR magic accepted");
}

// One buffer of the game's memory a thread, kept until that thread's next rewrite (the game parses from it)
static __thread char *g_jbuf;
static __thread size_t g_jcap;

// JM1: the JSON parser's entry, the address of the text pointer
static void on_json(rc_mid_frame *f, void *user) {
  if (!g_on || !seven_on) return;
  uint8_t *cursor = (uint8_t *)(uintptr_t)SEVEN_GET(f, "cursor");
  if (!cursor) return;
  seven_native_allow_bass(seven_feature(SEVEN_F_BASS));
  const char *text = rc_mod_ptr(cursor);
  if (!text || (g_jbuf && text >= g_jbuf && text < g_jbuf + g_jcap)) return;  // the text we wrote: nothing in it to do
  const size_t size = seven_native_json_extent(text, MAX_JSON);
  char *out;
  size_t n;
  int rewritten = 0, bad = 0;
  if (!size || !seven_native_manifest(text, size, &out, &n, &rewritten, &bad)) return;
  if (n + 1 > g_jcap) {
    size_t cap = n + 1 > 2 * g_jcap ? n + 1 : 2 * g_jcap;
    char *mem = rc_guest_alloc(cap);  // the old one is the game's, never freed
    if (!mem) {
      free(out);
      return;
    }
    g_jbuf = mem;
    g_jcap = cap;
  }
  memcpy(g_jbuf, out, n + 1);
  free(out);
  const uint64_t g = RC_GUEST(g_jbuf);
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, cursor, g);
  else
    RC_ST(uint32_t, cursor, (uint32_t)g);
  nlog(&g_logs_json, "manifest text of %zu bytes: %d native arrangement(s) rewritten%s", size, rewritten,
       bad ? ", INVALID native arrangement(s) given a six-string tuning" : "");
}

// XI1: the inflated body in its memory stream
static void on_sng(rc_mid_frame *f, void *user) {
  if (!g_on || !seven_on) return;
  uint8_t *buf = (uint8_t *)(uintptr_t)SEVEN_GET(f, "body");
  const size_t size = (uint32_t)SEVEN_GET(f, "size");
  if (!buf || size < 4 || memcmp(buf, "SNXR", 4)) return;
  const unsigned instrument = size > 6 ? buf[5] : 0, strings = size > 6 ? buf[6] : 0;
  seven_native_allow_bass(seven_feature(SEVEN_F_BASS));
  char why[200] = "";
  uint8_t *in = size <= MAX_BODY ? malloc(size) : NULL, *out = in ? malloc(size) : NULL;
  seven_native_status st = SEVEN_NATIVE_BAD;
  if (!in || !out) {
    if (size > MAX_BODY)
      snprintf(why, sizeof why, "a body of %zu bytes is too large", size);
    else
      snprintf(why, sizeof why, "no memory");
  } else {
    memcpy(in, buf, size);
    st = seven_native_sng(in, size, out, why, sizeof why);
  }
  if (st == SEVEN_NATIVE_OK) {
    memcpy(buf, out, size);
    nlog(&g_logs_sng, "SNXR body converted in place: %s, %u strings, %zu bytes", instrument ? "bass" : "guitar", strings, size);
  } else {
    nlog(&g_logs_sng, "SNXR body (%zu bytes) not converted (%s): load refused", size, why);
    f->go = SEVEN_CONT(f, "fail");
  }
  free(in);
  free(out);
}

int seven_native_init(void) {
  const int a = seven_mid_optional("seven_native_psarc", on_psarc, "native charts");
  const int b = seven_mid_optional("seven_native_json", on_json, "native charts");
  const int c = seven_mid_optional("seven_native_sng", on_sng, "native charts");
  g_on = a && b && c;
  // a native package is a plain archive: Plain Packages comes on with it, whatever its own switch says
  if (g_on && plain_packages_start("native charts")) g_on = 0;
  if (a || b || c) {
    if (g_on)
      rsm_log("seven-string: native N-string charts: on (PSXR packages, ExtendedRange manifests, SNXR charts; Plain Packages on with them)");
    else
      rsm_log("seven-string: native N-string charts: off, this RS Re-ARMed lacks some of their mid points");
  }
  return 0;
}
