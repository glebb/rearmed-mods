// Where a parameter has its value in a preset for a plug-in's engine (tonehost/preset_places.h)
#include <math.h>
#include "../tonehost/preset_places.h"
#include "check.h"

static float at(const uint8_t *p, size_t o) {
  float v;
  memcpy(&v, p + o, sizeof v);
  return v;
}

int main(void) {
  uint8_t p[16] = {0}, q[16];
  double knob[3] = {4, -60, 0}, plain[3] = {8, 0, 1}, sw[1] = {13};
  // A knob goes along its line from the value at 0 to the value at 1, and on outside them
  CHECK(!place_value(p, sizeof p, knob, 3, 0.3f) && fabsf(at(p, 4) + 42.f) < 1e-4f);
  CHECK(!place_value(p, sizeof p, knob, 3, 1.f) && at(p, 4) == 0.f);
  CHECK(!place_value(p, sizeof p, plain, 3, 1.5f) && at(p, 8) == 1.5f);
  CHECK(!place_value(p, sizeof p, plain, 3, -0.5f) && at(p, 8) == -0.5f);
  CHECK(p[3] == 0 && p[12] == 0);  // and nothing beside it is touched
  // A switch is a byte
  CHECK(!place_value(p, sizeof p, sw, 1, 1.f) && p[13] == 1);
  CHECK(!place_value(p, sizeof p, sw, 1, 0.f) && p[13] == 0);
  CHECK(!place_value(p, sizeof p, sw, 1, 0.7f) && p[13] == 1 && p[14] == 0);
  // A place outside the preset, or one that is no place, writes nothing
  double late[3] = {13, 0, 1}, far[1] = {16}, between[1] = {2.5}, before[1] = {-1};
  memcpy(q, p, sizeof p);
  CHECK(place_value(p, sizeof p, late, 3, 1.f) == -1);
  CHECK(place_value(p, sizeof p, far, 1, 1.f) == -1);
  CHECK(place_value(p, sizeof p, between, 1, 1.f) == -1);
  CHECK(place_value(p, sizeof p, before, 1, 1.f) == -1);
  CHECK(place_value(p, sizeof p, knob, 2, 1.f) == -1 && place_value(p, sizeof p, knob, 0, 1.f) == -1);
  CHECK(!memcmp(p, q, sizeof p));
  // Two presets are the same where a knob differs in its last digits alone, and from then on byte for byte
  place_value(p, sizeof p, knob, 3, 0.3816667f);
  memcpy(q, p, sizeof p);
  float nearly = at(p, 4) + 2e-5f, other = at(p, 4) + 0.5f;
  memcpy(q + 4, &nearly, sizeof nearly);
  CHECK(memcmp(p, q, sizeof p) && place_same(p, q, sizeof p, knob, 3) && !memcmp(p, q, sizeof p));
  memcpy(q + 4, &other, sizeof other);
  CHECK(!place_same(p, q, sizeof p, knob, 3));
  CHECK(place_same(p, q, sizeof p, sw, 1) && !place_same(p, q, sizeof p, late, 3));
  return check_done("places");
}
