// A song's correction of an amp's gain, on its own so that the tests can reach it.
#ifndef TONE_GAIN_H
#define TONE_GAIN_H

// offset is how many thirds of the gain knob the song's tones sit above its record, from RS Companion. A clean tone stays
// as it is, so that the song's parts stay apart.
static inline float corrected_gain(float gain, float offset) {
  if (gain < 1.f / 3) return gain;
  float g = gain - offset / 3.f;
  return g < 0.f ? 0.f : g > 1.f ? 1.f : g;
}

#endif
