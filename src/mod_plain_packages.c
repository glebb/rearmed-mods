// Plain packages: unencrypted song archives, as DotCharter writes its 7-string charts (a .psarc whose table of
// contents is not encrypted, song files with flags 1: compressed, not encrypted), load as the encrypted ones do. After
// rs7string's plain PSARC unit (github.com/SomeeName/rs7string, MIT, by SomeeName: src/features/psarc), which does what
// the CDLC enabler's plain PSARC build did.
//
// A unit of its own, beside the Seven-String Guitar: any custom song can come plain, and a 7-string chart can come
// encrypted. [Toggle Switches] PlainPackages = on in RSMods.ini, read once at the start: the edits go on before the
// game runs or not at all. Edit group plain_packages (names/edits.list) and one mid point (names/mids.list):
//   PP_TOC         an edit: the table of contents reader (0x1422d10) skips its decrypt block when the archive's flags
//                  lack 4 (an encrypted table of contents)
//   PP_SNGF        an edit: the song file loader (0x10d9f80) takes flags 1 as it takes 3 (compressed and encrypted)
//   plain_pp_sngd  the mid point on the loader's call of the song file decrypt: a file with magic 0x4a and flags 1 has
//                  its payload (after the 16-byte IV slot) copied to the output and the decrypt skipped; any other is
//                  decrypted
// RS Re-ARMed skips the song file's signature check itself (unless RS_MODS_BYPASS=0): a plain song file is unsigned.
// Other archives and song files load as before.
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "rsmods.h"

#define PLAIN_GROUP "plain_packages"
#define PLAIN_POINT "plain_pp_sngd"

static int g_on;
// The point's values and its continuation, by name once
static int g_src, g_len, g_out, g_out_len, g_ok, g_done;

enum { DECRYPT, COPY, REJECT };

// What the decrypt call does with a song file: its header (magic, flags), the length after the header (the IV slot
// and the payload) and the output's size
static int sng_action(uint32_t magic, uint32_t flags, uint32_t len, uint32_t out_len) {
  if (magic != 0x4a || flags != 1) return DECRYPT;
  if (len < 0x10 || out_len < len - 0x10) return REJECT;
  return COPY;
}

// plain_pp_sngd: before the loader's call of the AES-CTR decrypt (cdecl: the file past its 8-byte header, its length,
// the key and the key's length, the output and its size; al: decrypted). The loader's own checks have passed: magic
// 0x4a, flags 1 or 3, the signature.
static void sng_decrypt(rc_mid_frame *f, void *user) {
  if (!g_on) return;
  const uint8_t *src = (const uint8_t *)(uintptr_t)rc_mid_get(f, g_src);
  uint8_t *out = (uint8_t *)(uintptr_t)rc_mid_get(f, g_out);
  uint32_t len = (uint32_t)rc_mid_get(f, g_len);
  int action = REJECT;  // a file too short for its IV slot, as rs7string
  if (src && len >= 0x10) {
    uint32_t magic, flags;
    memcpy(&magic, src - 8, 4);
    memcpy(&flags, src - 4, 4);
    action = sng_action(magic, flags, len, (uint32_t)rc_mid_get(f, g_out_len));
  }
  if (action == DECRYPT) return;
  if (action == COPY && !out) action = REJECT;
  if (action == COPY) memmove(out, src + 0x10, len - 0x10);
  rc_mid_set(f, g_ok, action == COPY);
  f->go = g_done;
}

__attribute__((constructor)) static void plain_init(void) {
  rsm_ini_poll();
  if (!rsm_on("PlainPackages")) return;
  if (!dlsym(RTLD_DEFAULT, "rc_mid_add_name") || !dlsym(RTLD_DEFAULT, "rc_edit_enable")) {
    rsm_log("plain packages: this RS Re-ARMed has no edits and mid points: unencrypted packages don't load");
    return;
  }
  // The point first: the edits let plain song files through to the decrypt, which only the point skips
  int p = rc_mid_add_name(PLAIN_POINT, sng_decrypt, NULL);
  if (p < 0 || (g_src = rc_mid_value(p, "src")) < 0 || (g_len = rc_mid_value(p, "len")) < 0 ||
      (g_out = rc_mid_value(p, "out")) < 0 || (g_out_len = rc_mid_value(p, "out_len")) < 0 ||
      (g_ok = rc_mid_value(p, "ok")) < 0 || (g_done = rc_mid_cont(p, "done")) < 0) {
    rsm_log("plain packages: no mid point %s on %s: unencrypted packages don't load", PLAIN_POINT, rc_target());
    return;
  }
  if (rc_edit_enable(PLAIN_GROUP)) {
    rsm_log("plain packages: no edits of %s on %s: unencrypted packages don't load", PLAIN_GROUP, rc_target());
    return;
  }
  g_on = 1;
  const char *bypass = getenv("RS_MODS_BYPASS");
  if (bypass && bypass[0] && strchr("0nNfF", bypass[0]))
    rsm_log("plain packages: on, but RS_MODS_BYPASS=%s checks song files' signatures: plain ones are refused", bypass);
  else
    rsm_log("plain packages: on");
}
