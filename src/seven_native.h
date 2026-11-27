// Seven-String Guitar (seven.h): the native N-string format (rs7string 1.0.0-beta.5, docs/release/NATIVE-FORMAT.md) read
// as the unit's own form. DotCharter exports only this format: a package with the magic PSXR (*_rsxr_p.psarc, a plain
// PSARC), manifests with "ExtendedRange" and a positional "Tuning" string0..N-1 (thickest first), and a chart whose
// inflated body starts "SNXR" with N-wide records. seven_native.c translates all three while the game loads them, to the
// form the rest of this unit reads (rs7string's at 2edcf68, the one this port was made from): a package with the magic
// PSAR, a manifest Tuning string0..5 in the game's slot order plus Rs7String6 / Rs7String7 (a bass: Rs7String4 / 5), a chart
// with the RS7X block (an extended bass: a block with STRS only, instrument 1).
// The pure converters (bytes in, bytes out; seven_native_core.c, tests/test_seven_native.c) are what the offline readers
// link too; the mod part is seven_native_init (seven_native.c).
#ifndef RS_SEVEN_NATIVE_H
#define RS_SEVEN_NATIVE_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
  SEVEN_NATIVE_OK,
  SEVEN_NATIVE_NOT_NATIVE,   // no SNXR magic
  SEVEN_NATIVE_BAD,          // malformed
  SEVEN_NATIVE_TOO_SMALL,    // too little zero padding after MetaData for the translation
  SEVEN_NATIVE_UNSUPPORTED,  // well formed, but an instrument this unit does not have (an extended bass, while it is off)
} seven_native_status;

// Whether an extended bass (5 or 6 strings) is translated: on at start (the offline readers want it). The game's part
// turns it off when its RS Re-ARMed cannot build such a bass (seven_feature(SEVEN_F_BASS)): the chart is then refused
// (SEVEN_NATIVE_UNSUPPORTED) and the manifest gets a stock Tuning counted as invalid, as before wave 2.
void seven_native_allow_bass(int on);

// 1 when an inflated SNG body starts with the native magic
int seven_native_is_sng(const uint8_t *body, size_t size);
// Translates a native body of `size` bytes into the same number of bytes at out (a buffer of its own: not body), on OK.
// why (when why_cap is not 0) always ends in a zero byte and says what failed.
seven_native_status seven_native_sng(const uint8_t *body, size_t size, uint8_t *out, char *why, size_t why_cap);

// The length of the JSON object or array at text (a byte-order mark and white space before it included): the bytes the
// game's parser reads; 0 when none starts there, or it does not end within limit bytes or before a zero byte. No byte
// after its closing bracket is read.
size_t seven_native_json_extent(const char *text, size_t limit);
// 0 when text (a manifest or header file, size bytes) has no native arrangement or is not JSON this scanner accepts.
// 1: *out is a malloc'd, zero-terminated copy of the whole text with every native arrangement's Tuning in the unit's
// form and its marker removed; *rewritten counts those, *bad the ones marked but not valid (given a harmless six-entry
// Tuning, so the game's readers never meet a seventh "stringI" key). Either count pointer may be NULL.
int seven_native_manifest(const char *text, size_t size, char **out, size_t *out_len, int *rewritten, int *bad);

// The mod part: three mid points, taken together or not at all; 0 (an RS Re-ARMed without them leaves the part off, with
// a log line, and the unit on)
int seven_native_init(void);
// Once the unit is on (its edit group switched on): Plain Packages started for the native charts, or the part off
void seven_native_unit_on(void);

#endif
