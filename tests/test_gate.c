// How the gate learns the guitar's noise (tonehost/gate_floor.h)
#include <stdlib.h>
#include "../tonehost/gate_floor.h"
#include "check.h"

// The threshold of a high-gain tone: the config's 12 dB, and most of its 8 dB for being tight
static float open_of(const gate_floor_t *f) { return f->db + 19.f; }

// The peak of 100 ms of noise moves about a little from one window to the next
static float noisy(float db) { return db + (float)(rand() % 2001) / 1000.f - 1.f; }

static gate_floor_t settled(float noise_db) {
  gate_floor_t f = {.db = -70.f};
  for (int i = 0; i < 300; i++) gate_floor_window(&f, noisy(noise_db), open_of(&f));
  return f;
}

int main(void) {
  // falls to the noise at once, and no further than -85
  gate_floor_t f = {.db = -70.f};
  gate_floor_window(&f, -78.f, open_of(&f));
  CHECK(f.db == -78.f);
  gate_floor_window(&f, -120.f, open_of(&f));
  CHECK(f.db == -85.f);

  // finds noise that got louder, within seconds
  f = settled(-80.f);
  CHECK(f.db < -79.f);
  for (int i = 0; i < 100; i++) gate_floor_window(&f, noisy(-70.f), open_of(&f));
  CHECK(f.db > -73.f && f.db < -69.f);

  // a long note ringing out, at each rate a string falls: the threshold stays under it to the end
  static const float RATES[] = {2.f, 3.f, 5.f, 8.f};
  for (int r = 0; r < 4; r++) {
    f = settled(-78.f);
    float start = f.db;
    for (float note = -20.f; note > -60.f; note -= RATES[r] / 10) gate_floor_window(&f, noisy(note), open_of(&f));
    CHECK(f.db < start + 1.5f);
    CHECK(open_of(&f) < -55.f);
  }

  // what a host.log showed: 5 s of a quiet tail took the threshold from -64 to -49, over the note it was meant to let by
  f = settled(-83.f);
  for (int i = 0; i < 50; i++) gate_floor_window(&f, noisy(-56.f - i * 0.25f), open_of(&f));
  CHECK(open_of(&f) < -60.f);

  // a loud note never counts as noise, however long it holds
  f = settled(-80.f);
  float before = f.db;
  for (int i = 0; i < 100; i++) gate_floor_window(&f, noisy(-20.f), open_of(&f));
  CHECK(f.db == before);

  // the floor stops at -45, so the gate can still open
  f = (gate_floor_t){.db = -46.f};
  for (int i = 0; i < 100; i++) gate_floor_window(&f, noisy(-40.f), open_of(&f));
  CHECK(f.db == -45.f);
  return check_done("gate");
}
