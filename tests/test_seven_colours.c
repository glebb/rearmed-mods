// Seven-String Guitar: the colours part's logic without the game: the string 7 / string 6 kinds from a colour, the
// ramp's V6 and V7 lines, and the low B swap over a fake 2 x 28 x 8 table (src/seven_colours_calc.c, _swap.c).
#include <math.h>
#include <string.h>

#include "check.h"
#include "seven_colours_calc.h"
#include "seven_colours_swap.h"

static int near(float a, float b) { return fabsf(a - b) < 1e-5f; }

static void test_kinds(void) {
  float pink[3] = {1.0f, 0.4f, 0.8f}, out[3];
  seven_colours_glow(pink, out);  // full brightness: the brightest channel 1
  CHECK(near(out[0], 1.0f) && near(out[1], 0.4f) && near(out[2], 0.8f));
  float dim_c[3] = {0.5f, 0.2f, 0.4f};
  seven_colours_glow(dim_c, out);
  CHECK(near(out[0], 1.0f) && near(out[1], 0.4f) && near(out[2], 0.8f));
  seven_colours_dim(pink, 76.0f / 255.0f, out);  // the brightest channel at dim
  CHECK(near(out[0], 76.0f / 255.0f) && near(out[1], 0.4f * 76.0f / 255.0f));
  float black[3] = {0, 0, 0};
  seven_colours_glow(black, out);
  CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
  seven_colours_dim(black, 0.3f, out);
  CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
}

enum { W = 256, H = 128 };
static uint8_t g_ramp[W * H * 4];

// A grey ramp: every line a gradient with the line's own offset, so that a painted line differs from its neighbours
static void grey_ramp(void) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t v = (uint8_t)x;
      uint8_t *p = g_ramp + ((size_t)y * W + x) * 4;
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
}

static const uint8_t *line_of(int y) { return g_ramp + (size_t)y * W * 4; }

static void test_ramp(void) {
  CHECK(seven_ramp_recolour(0, 100, 200) == 0);
  CHECK(seven_ramp_recolour(100, 100, 200) == 200);  // the anchor has the colour
  CHECK(seven_ramp_recolour(255, 100, 200) == 255);  // white above
  CHECK(seven_ramp_recolour(0, 0, 200) == 0 && seven_ramp_recolour(255, 0, 200) == 255);
  CHECK(seven_ramp_recolour(10, 255, 77) == (uint8_t)((77 * 10 + 127) / 255));

  float c6[2][3] = {{0.8f, 0.8f, 0.8f}, {0.0f, 1.0f, 0.5f}}, c7[2][3] = {{1.0f, 0.4f, 0.8f}, {1.0f, 0.4f, 0.8f}};
  grey_ramp();
  uint8_t before[W * H * 4];
  memcpy(before, g_ramp, sizeof before);
  seven_ramp_paint(g_ramp, W, H, c6, c7);
  // the lines painted: 56, 57 (V6) and 61, 62 (V7) in the normal mode's grey row 7; 120, 121 and 125, 126 in row 15
  static const int painted[] = {56, 57, 61, 62, 120, 121, 125, 126};
  for (int y = 0; y < H; y++) {
    int is = 0;
    for (size_t i = 0; i < sizeof painted / sizeof *painted; i++) is |= painted[i] == y;
    CHECK(is == (memcmp(line_of(y), before + (size_t)y * W * 4, W * 4) != 0));
  }
  int knee = W * 2 / 5;  // the colour sits at 40% across
  const uint8_t *l7 = line_of(61);
  CHECK(l7[knee * 4] == 255 && l7[knee * 4 + 1] == 102 && l7[knee * 4 + 2] == 204);
  CHECK(memcmp(line_of(61), line_of(62), W * 4) == 0 && memcmp(line_of(56), line_of(57), W * 4) == 0);
  const uint8_t *l6 = line_of(120);  // colourblind: 0, 255, 128
  CHECK(l6[knee * 4] == 0 && l6[knee * 4 + 1] == 255 && l6[knee * 4 + 2] == 128);
  CHECK(line_of(125)[knee * 4 + 1] == 102);  // string 7's colourblind colour
  // V7 samples those lines: the sample position of V is V * 128 - 0.5, between lines 61 and 62
  CHECK((int)(SEVEN_RAMP_V7_0 * H) - 1 == 61 && (int)(SEVEN_RAMP_V7_1 * H) - 1 == 125);
  // and no row's own V lands there: row r is sampled at (2r + 1) / 32
  for (int r = 0; r < 16; r++) {
    float v = (2.0f * (float)r + 1.0f) / 32.0f;
    CHECK(!near(v, SEVEN_RAMP_V6_0) && !near(v, SEVEN_RAMP_V6_1) && !near(v, SEVEN_RAMP_V7_0) && !near(v, SEVEN_RAMP_V7_1));
  }
}

enum { KINDS = SEVEN_SWAP_KINDS, COLS = SEVEN_SWAP_COLUMNS };
static const int UV_KINDS[] = {15, 17, 21};

// Both pointer sizes: the table of 2 x 28 x 8 pointers; entry value = 0x1000 * mode + 0x100 * kind + column
static void fill(seven_swap_tables *t, uint8_t *pb, uint8_t *ub, int ptr_size) {
  t->ptrs = pb, t->uvs = ub, t->ptr_size = ptr_size;
  for (int m = 0; m < 2; m++)
    for (int k = 0; k < KINDS; k++)
      for (int c = 0; c < COLS; c++) {
        size_t i = (size_t)(m * KINDS + k) * COLS + c;
        uint64_t p = 0x100000u + 0x1000u * (uint64_t)m + 0x100u * (uint64_t)k + (uint64_t)c;
        uint32_t u = 0x3f000000u + (uint32_t)(0x1000 * m + 0x100 * k + c);
        if (ptr_size == 8)
          memcpy(pb + i * 8, &p, 8);
        else {
          uint32_t q = (uint32_t)p;
          memcpy(pb + i * 4, &q, 4);
        }
        memcpy(ub + i * 4, &u, 4);
      }
}

static uint64_t ptr_of(const seven_swap_tables *t, int m, int k, int c) {
  size_t i = (size_t)(m * KINDS + k) * COLS + c;
  uint64_t v = 0;
  memcpy(&v, t->ptrs + i * (size_t)t->ptr_size, (size_t)t->ptr_size);
  return v;
}
static uint32_t uv_of(const seven_swap_tables *t, int m, int k, int c) {
  uint32_t v;
  memcpy(&v, t->uvs + ((size_t)(m * KINDS + k) * COLS + c) * 4, 4);
  return v;
}
static void put_ptr_at(const seven_swap_tables *t, int m, int k, int c, uint64_t v) {
  size_t i = (size_t)(m * KINDS + k) * COLS + c;
  memcpy(t->ptrs + i * (size_t)t->ptr_size, &v, (size_t)t->ptr_size);
}
static int is_uv(int k) { return k == 15 || k == 17 || k == 21; }

static void test_swap(int ptr_size) {
  static uint8_t pb[2 * KINDS * COLS * 8], ub[2 * KINDS * COLS * 4], orig_p[sizeof pb], orig_u[sizeof ub];
  seven_swap_tables t;
  seven_swap s;
  int owner;
  memset(&s, 0, sizeof s);
  fill(&t, pb, ub, ptr_size);
  memcpy(orig_p, pb, sizeof pb), memcpy(orig_u, ub, sizeof ub);

  CHECK(seven_swap_wanted(5, 0) && seven_swap_wanted(6, 0));
  CHECK(!seven_swap_wanted(4, 0) && !seven_swap_wanted(0, 0) && !seven_swap_wanted(5, 1) && !seven_swap_wanted(6, 1));

  seven_swap_apply(&s, &t, &owner, UV_KINDS, 3);
  int all = 1, others = 1;
  for (int m = 0; m < 2; m++)
    for (int k = 0; k < KINDS; k++) {
      all &= ptr_of(&t, m, k, 4) == ptr_of(&t, m, k, 6);
      all &= is_uv(k) ? uv_of(&t, m, k, 4) == uv_of(&t, m, k, 6) : uv_of(&t, m, k, 4) != uv_of(&t, m, k, 6);
      for (int c = 0; c < COLS; c++)
        if (c != 4 && (ptr_of(&t, m, k, c) != 0x100000u + 0x1000u * (uint64_t)m + 0x100u * (uint64_t)k + (uint64_t)c))
          others = 0;
    }
  CHECK(all && others);
  CHECK(s.swapped);
  // twice is nothing (it would save the swapped values as the originals)
  seven_swap_apply(&s, &t, &owner, UV_KINDS, 3);
  seven_swap_restore(&s, &t, &owner);
  CHECK(memcmp(pb, orig_p, sizeof pb) == 0 && memcmp(ub, orig_u, sizeof ub) == 0);
  CHECK(!s.swapped);
  seven_swap_restore(&s, &t, &owner);  // nothing to restore: harmless
  CHECK(memcmp(pb, orig_p, sizeof pb) == 0);

  // The game's build writes column 4 afresh while the swap is on: its entries are kept, the others restored
  seven_swap_apply(&s, &t, &owner, UV_KINDS, 3);
  put_ptr_at(&t, 0, 3, 4, 0xABC000u);
  put_ptr_at(&t, 1, 15, 4, 0xDEF000u);
  uint32_t fresh = 0x40000000u;
  memcpy(t.uvs + ((size_t)(0 * KINDS + 17) * COLS + 4) * 4, &fresh, 4);
  seven_swap_restore(&s, &t, &owner);
  CHECK(ptr_of(&t, 0, 3, 4) == 0xABC000u && ptr_of(&t, 1, 15, 4) == 0xDEF000u);
  CHECK(uv_of(&t, 0, 17, 4) == 0x40000000u);
  CHECK(ptr_of(&t, 0, 5, 4) == 0x100000u + 0x500u + 4);  // restored
  CHECK(uv_of(&t, 1, 21, 4) == 0x3f000000u + 0x1000u + 0x1500u + 4);
  CHECK(uv_of(&t, 0, 15, 4) == 0x3f000000u + 0xf00u + 4);

  // Another manager (its column 4 is its own): the old swap is forgotten, nothing is written
  fill(&t, pb, ub, ptr_size);
  int other;
  seven_swap_apply(&s, &t, &owner, UV_KINDS, 3);
  fill(&t, pb, ub, ptr_size);  // a new manager at the same place: the game filled it afresh
  seven_swap_restore(&s, &t, &other);
  CHECK(!s.swapped && memcmp(pb, orig_p, sizeof pb) == 0 && memcmp(ub, orig_u, sizeof ub) == 0);
  // and a swap of the new one starts from its own column 4
  seven_swap_apply(&s, &t, &other, UV_KINDS, 3);
  CHECK(ptr_of(&t, 0, 1, 4) == ptr_of(&t, 0, 1, 6));
  seven_swap_restore(&s, &t, &other);
  CHECK(memcmp(pb, orig_p, sizeof pb) == 0);
}

int main(void) {
  test_kinds();
  test_ramp();
  test_swap(4);
  test_swap(8);
  return check_done("test_seven_colours");
}
