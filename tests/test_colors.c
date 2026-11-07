// The color maths (src/color_math.h)
#include "../src/color_math.h"
#include "check.h"

static int near(float a, float b, float within) { return fabsf(a - b) <= within; }
static int same(rgb a, rgb b) {
  return near(a.r, b.r, 1e-4f) && near(a.g, b.g, 1e-4f) && near(a.b, b.b, 1e-4f);
}

int main(void) {
  // the known corners of the color wheel
  static const struct {
    rgb c;
    float h;
  } WHEEL[] = {{{1, 0, 0}, 0},   {{1, 1, 0}, 60},  {{0, 1, 0}, 120},
               {{0, 1, 1}, 180}, {{0, 0, 1}, 240}, {{1, 0, 1}, 300}};
  for (int i = 0; i < 6; i++) {
    hsl v = to_hsl(WHEEL[i].c);
    CHECK(near(v.h, WHEEL[i].h, 1e-3f) && near(v.s, 1, 1e-5f) && near(v.l, .5f, 1e-5f));
    CHECK(same(to_rgb((hsl){WHEEL[i].h, 1, .5f}), WHEEL[i].c));
  }

  // grey has no hue or saturation; black and white survive
  hsl grey = to_hsl((rgb){.5f, .5f, .5f});
  CHECK(grey.h == 0 && grey.s == 0 && near(grey.l, .5f, 1e-6f));
  CHECK(same(to_rgb(to_hsl((rgb){0, 0, 0})), (rgb){0, 0, 0}));
  CHECK(same(to_rgb(to_hsl((rgb){1, 1, 1})), (rgb){1, 1, 1}));

  // there and back, across the whole cube
  for (int r = 0; r <= 10; r++)
    for (int g = 0; g <= 10; g++)
      for (int b = 0; b <= 10; b++) {
        rgb c = {r / 10.f, g / 10.f, b / 10.f};
        CHECK(same(to_rgb(to_hsl(c)), c));
      }

  CHECK(near(wrap_hue(370), 10, 1e-4f) && near(wrap_hue(-10), 350, 1e-4f) && wrap_hue(0) == 0);

  // the ramp: black, the color at the knee, white
  CHECK(ramp_channel(200, 0) == 0);
  CHECK(ramp_channel(200, RAMP_KNEE) == 200);
  CHECK(ramp_channel(200, 1) == 255);
  CHECK(ramp_channel(0, RAMP_KNEE) == 0 && ramp_channel(255, RAMP_KNEE) == 255);
  for (int i = 1; i <= 100; i++) CHECK(ramp_channel(120, i / 100.f) >= ramp_channel(120, (i - 1) / 100.f));

  // a string in the game's own color gets exactly the game's color of each kind
  rgb string = {1, .31f, .35f}, dim = {.3f, .1f, .12f}, glow = {1, .1f, .05f};
  CHECK(same(restyle(string, string, dim), dim));
  CHECK(same(restyle(string, string, glow), glow));
  CHECK(same(restyle(string, string, string), string));

  // another color keeps its own hue, moved as far as the game moves its own
  rgb blue = {0, .3f, 1};
  hsl dimmed = to_hsl(restyle(blue, string, dim));
  float turn = to_hsl(dim).h - to_hsl(string).h;
  CHECK(near(dimmed.h, wrap_hue(to_hsl(blue).h + turn), .01f));
  CHECK(dimmed.l < to_hsl(blue).l);  // the game's dim kind is darker, so this is too

  // a kind that is white in the game stays white; a grey string gets no color; nothing leaves the range
  CHECK(same(restyle(blue, string, (rgb){1, 1, 1}), (rgb){1, 1, 1}));
  CHECK(to_hsl(restyle((rgb){.5f, .5f, .5f}, string, glow)).s < 1e-4f);
  rgb bright = restyle((rgb){.95f, .95f, 1}, dim, (rgb){1, .9f, .9f});
  CHECK(bright.r >= 0 && bright.r <= 1.0001f && bright.g >= 0 && bright.b <= 1.0001f);

  // a game with nothing in the table for the string: no dividing by zero
  rgb none = restyle(blue, (rgb){0, 0, 0}, dim);
  CHECK(none.r == none.r && none.g == none.g && none.b == none.b);

  return check_done("color_math");
}
