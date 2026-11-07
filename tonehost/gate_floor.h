// How the gate learns the guitar's noise, on its own so that the tests can reach it. Fed the peak of every 100 ms.
#ifndef GATE_FLOOR_H
#define GATE_FLOOR_H

#include <math.h>
#include <stdint.h>

#define GATE_FLOOR_WINDOWS 16  // 1.6 s

typedef struct {
  float db;  // the floor
  float recent[GATE_FLOOR_WINDOWS];
  uint32_t seen;
} gate_floor_t;

// The floor falls at once and rises 3 dB a second, but only under a level that stays where it is. Noise does. A note
// ringing out falls, and a floor that rose under it would take the gate up with it and cut the note off.
static inline void gate_floor_window(gate_floor_t *f, float db, float open_db) {
  f->recent[f->seen++ % GATE_FLOOR_WINDOWS] = db;
  if (db < f->db) {
    f->db = fmaxf(db, -85.f);
    return;
  }
  if (db >= open_db + 6.f || f->seen < GATE_FLOOR_WINDOWS) return;  // a note, or too soon to tell
  float newer = 0, older = 0;  // the last half of the windows against the half before: each window alone wobbles too much
  for (uint32_t i = 0; i < GATE_FLOOR_WINDOWS; i++) {
    float level = f->recent[(f->seen - 1 - i) % GATE_FLOOR_WINDOWS];
    if (i < GATE_FLOOR_WINDOWS / 2) newer += level;
    else older += level;
  }
  if (fabsf(newer - older) / (GATE_FLOOR_WINDOWS / 2) < 0.8f) f->db = fminf(fminf(f->db + 0.3f, db), -45.f);
}
#endif
