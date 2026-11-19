// How loud a tone is: the guitar it is measured with, and the meter (tonehost/tone_level.h)
#include "../tonehost/tone_level.h"
#include "check.h"

static int near(double a, double b, double within) { return fabs(a - b) < within; }

// What the meter says of a sine at full scale, once the filter has settled
static float sine_level(double hz) {
  double z[4] = {0}, sum = 0;
  for (int i = 0; i < 96000; i++) {
    double v = level_weigh(z, sin(2 * M_PI * hz * i / LEVEL_RATE));
    if (i >= 48000) sum += v * v;
  }
  return level_db(sum, 48000);
}

int main(void) {
  // The meter is BS.1770's: a full-scale sine at 997 Hz is -3.01 LUFS, and a low hum counts for less than a mid, a
  // high for more
  CHECK(near(sine_level(997), -3.01, 0.02));
  CHECK(near(sine_level(100), -4.83, 0.1));
  CHECK(near(sine_level(3000), 0.11, 0.1));
  CHECK(level_db(0, 0) < -119 && level_db(0, 100) < -119);
  CHECK(near(level_db(50, 100), -0.691 - 3.01, 0.01));

  static float loop[LEVEL_LOOP];
  level_phrase(loop);
  double most = 0, least = 1e9, total = 0, peak = 0, low = 0, state = 0, k = 1 - exp(-2 * M_PI * 700 / LEVEL_RATE);
  for (int from = 0; from < LEVEL_LOOP; from += 240) {
    double sum = 0;
    for (int i = 0; i < 2400; i++) sum += (double)loop[(from + i) % LEVEL_LOOP] * loop[(from + i) % LEVEL_LOOP];
    if (sum > most) most = sum;
    if (sum < least) least = sum;
  }
  for (int round = 0; round < 2; round++)
    for (int i = 0; i < LEVEL_LOOP; i++) {
      state += (loop[i] - state) * k;  // what is under 700 Hz, more or less
      if (!round) continue;
      total += (double)loop[i] * loop[i];
      low += state * state;
      if (fabs(loop[i]) > peak) peak = fabs(loop[i]);
    }
  // Its loudest 50 ms are 4 dB under 1, and all of it 10.8 dB: as loud as the low pluck it replaced was
  CHECK(near(10 * log10(most / 2400), -4, 0.01));
  CHECK(near(10 * log10(total / LEVEL_LOOP), -10.83, 0.1));
  CHECK(near(20 * log10(peak), 5.45, 0.2));
  // loud and quiet, as a guitar is: the measure of a tone depends on it
  CHECK(10 * log10(least / most) < -30);
  // most of a guitar is under 700 Hz, and not all of it
  CHECK(low / total > 0.75 && low / total < 0.92);
  // it goes round with no step at its end, and making it again adds nothing to it
  CHECK(fabs(loop[0] - loop[LEVEL_LOOP - 1]) < 0.001);
  float first = loop[1000];
  level_phrase(loop);
  CHECK(loop[1000] == first);
  return check_done("level");
}
