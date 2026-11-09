// Song Stems' mix of a block and its search for a song's .wem (src/stems_mix.h)
#include <math.h>
#include "../src/stems_mix.h"
#include "check.h"

int main(void) {
  // gain 0 is the backing, gain 1 the song; the ramp moves by step a frame and stops at the target
  int16_t pcm[8] = {1000, -1000, 1000, -1000, 1000, -1000, 1000, -1000}, back[8] = {0};
  float g = 0.f;
  stems_mix(pcm, back, 4, 2, &g, 0.f, 0.1f);
  CHECK(pcm[0] == 0 && pcm[7] == 0 && g == 0.f);
  int16_t song[8] = {1000, -1000, 1000, -1000, 1000, -1000, 1000, -1000};
  g = 1.f;
  stems_mix(song, back, 4, 2, &g, 1.f, 0.1f);
  CHECK(song[0] == 1000 && song[7] == -1000 && g == 1.f);
  int16_t ramp[8] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
  g = 1.f;
  stems_mix(ramp, back, 4, 2, &g, 0.f, 0.25f);
  CHECK(ramp[0] == 750 && ramp[1] == 750 && ramp[2] == 500 && ramp[6] == 0 && g == 0.f);
  int16_t up[2] = {1000, 1000};
  g = 0.f;
  stems_mix(up, back, 1, 2, &g, 0.3f, 1.f);  // a step past the target stops at it
  CHECK(up[0] == 300 && fabsf(g - 0.3f) < 1e-6f);
  int16_t loud[2] = {32767, -32768}, b2[2] = {-32768, 32767};
  g = 1.f;
  stems_mix(loud, b2, 1, 2, &g, 1.f, 0.f);
  CHECK(loud[0] == 32767 && loud[1] == -32768);
  int16_t half[2] = {101, -101}, b3[2] = {0, 0};
  g = 0.5f;
  stems_mix(half, b3, 1, 2, &g, 0.5f, 0.f);  // rounds to the nearest, away from zero at a half
  CHECK(half[0] == 51 && half[1] == -51);

  CHECK(stems_wem_id("audio/mac/123732889.wem") == 123732889u);
  CHECK(stems_wem_id("audio/windows/525916663.wem") == 525916663u);
  CHECK(stems_wem_id("audio/mac/song_x.wem") == 0);
  CHECK(stems_wem_id("audio/mac/x12.wem") == 0);
  CHECK(stems_wem_id("audio/mac/123.bnk") == 0);
  CHECK(stems_wem_id("audio/mac/99999999999.wem") == 0);
  CHECK(stems_wem_id("123.wem") == 123u);

  uint8_t bank[] = {'B', 'K', 'H', 'D', 0x79, 0xbd, 0x5f, 0x07, 0, 0x99, 0x16, 0x41, 0x02};
  uint32_t ids[] = {44700311u, 0x075fbd79u, 1u};  // the second's bytes are in the bank, little-endian
  CHECK(stems_bank_wem(bank, sizeof bank, ids, NULL, 3) == 1);
  uint32_t none[] = {5u, 6u};
  CHECK(stems_bank_wem(bank, sizeof bank, none, NULL, 2) == -1);
  uint32_t two[] = {0x075fbd79u, 0x02411699u};
  uint64_t sizes[] = {100, 5000};
  CHECK(stems_bank_wem(bank, sizeof bank, two, sizes, 2) == 1);
  CHECK(stems_bank_wem(bank, sizeof bank, two, NULL, 2) == 0);
  return check_done("stems");
}
