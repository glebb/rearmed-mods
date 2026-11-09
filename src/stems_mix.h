// Song Stems' arithmetic, without the game (tests/test_stems.c): the mix of a block, and which .wem a song's bank plays.
#ifndef STEMS_MIX_H
#define STEMS_MIX_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// A block of the song as the game decoded it, in place: backing + gain * (song - backing), where backing is the song
// without the part. Gain 1 is the song as it is, 0 the song without the part. The gain moves from *gain towards target
// by at most step a frame, so a change of level, or the backing becoming ready, is a short ramp and not a click; *gain
// is where it got to. Interleaved int16, channels samples a frame.
static inline void stems_mix(int16_t *pcm, const int16_t *backing, uint32_t frames, uint32_t channels, float *gain,
                             float target, float step) {
  float g = *gain;
  for (uint32_t f = 0; f < frames; f++) {
    if (g < target) {
      g += step;
      if (g > target) g = target;
    } else if (g > target) {
      g -= step;
      if (g < target) g = target;
    }
    for (uint32_t c = 0; c < channels; c++) {
      size_t i = (size_t)f * channels + c;
      float b = backing[i], v = b + g * ((float)pcm[i] - b);
      pcm[i] = (int16_t)(v > 32767.f ? 32767 : v < -32768.f ? -32768 : (v < 0 ? v - 0.5f : v + 0.5f));
    }
  }
  *gain = g;
}

// The file ID of a .wem named in the archive ("audio/mac/123732889.wem"), or 0
static inline uint32_t stems_wem_id(const char *name) {
  size_t l = strlen(name);
  if (l < 5 || strcmp(name + l - 4, ".wem")) return 0;
  const char *p = name + l - 4;
  while (p > name && p[-1] >= '0' && p[-1] <= '9') p--;
  if (p == name + l - 4 || (p > name && p[-1] != '/')) return 0;
  uint64_t v = 0;
  for (; *p != '.'; p++) v = v * 10 + (uint64_t)(*p - '0');
  return v <= 0xffffffffu ? (uint32_t)v : 0;
}

// Which of the archive's .wem files (ids, n) a song's bank (song_<key>.bnk, the full song's, not the preview's) plays:
// the bank names its streamed source by file ID, as a little-endian u32. The index into ids, or -1. A bank naming more
// than one takes the largest of them (sizes, n; NULL: the first).
static inline int stems_bank_wem(const uint8_t *bank, size_t len, const uint32_t *ids, const uint64_t *sizes, int n) {
  int best = -1;
  for (int i = 0; i < n; i++) {
    uint8_t le[4] = {(uint8_t)ids[i], (uint8_t)(ids[i] >> 8), (uint8_t)(ids[i] >> 16), (uint8_t)(ids[i] >> 24)};
    int found = 0;
    for (size_t k = 0; k + 4 <= len && !found; k++) found = !memcmp(bank + k, le, 4);
    if (!found) continue;
    if (best < 0 || (sizes && sizes[i] > sizes[best])) best = i;
    if (!sizes) break;
  }
  return best;
}
#endif
