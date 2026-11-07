// A song's correction of an amp's gain (tonehost/tone_gain.h)
#include <math.h>
#include "../tonehost/tone_gain.h"
#include "check.h"

static int near(float a, float b) { return fabsf(a - b) < 0.001f; }

int main(void) {
  CHECK(near(corrected_gain(0.8f, 0), 0.8f));
  CHECK(near(corrected_gain(0.8f, 1), 0.4667f));
  CHECK(near(corrected_gain(0.2f, 1), 0.2f));
  CHECK(near(corrected_gain(0.2f, -1), 0.2f));
  CHECK(near(corrected_gain(0.4f, 2), 0.f));
  CHECK(near(corrected_gain(0.9f, -1), 1.f));
  return check_done("gain");
}
