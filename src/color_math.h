// The color maths of mod_colors.c, on its own so that the tests can reach it.
#ifndef COLOR_MATH_H
#define COLOR_MATH_H

#include <math.h>
#include <stdint.h>

typedef struct {
  float r, g, b;  // 0 to 1
} rgb;

typedef struct {
  float h;  // degrees, 0 up to 360
  float s, l;
} hsl;

static float clamp_unit(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static float wrap_hue(float degrees) {
  float h = fmodf(degrees, 360.0f);
  return h < 0.0f ? h + 360.0f : h;
}

static hsl to_hsl(rgb c) {
  float top = fmaxf(c.r, fmaxf(c.g, c.b)), bottom = fminf(c.r, fminf(c.g, c.b)), chroma = top - bottom;
  hsl out = {0.0f, 0.0f, (top + bottom) / 2.0f};
  if (chroma <= 0.0f) return out;
  out.s = chroma / (1.0f - fabsf(2.0f * out.l - 1.0f));
  float sector = top == c.r ? (c.g - c.b) / chroma : top == c.g ? (c.b - c.r) / chroma + 2.0f : (c.r - c.g) / chroma + 4.0f;
  out.h = wrap_hue(sector * 60.0f);
  return out;
}

static rgb to_rgb(hsl c) {
  float chroma = (1.0f - fabsf(2.0f * c.l - 1.0f)) * c.s, sector = wrap_hue(c.h) / 60.0f;
  float second = chroma * (1.0f - fabsf(fmodf(sector, 2.0f) - 1.0f)), base = c.l - chroma / 2.0f;
  rgb parts[6] = {{chroma, second, 0}, {second, chroma, 0}, {0, chroma, second},
                  {0, second, chroma}, {second, 0, chroma}, {chroma, 0, second}};
  rgb p = parts[(int)sector % 6];
  return (rgb){p.r + base, p.g + base, p.b + base};
}

// A channel of a palette's row at t, 0 to 1: up from black to the color at the knee, then on to white
#define RAMP_KNEE 0.4f
static uint8_t ramp_channel(uint8_t value, float t) {
  float v = (float)value;
  return (uint8_t)(t < RAMP_KNEE ? v * (t / RAMP_KNEE) : v + (255.0f - v) * ((t - RAMP_KNEE) / (1.0f - RAMP_KNEE)));
}

// `custom` as the game would have it for another kind of color. The game's table has each kind for its own string
// color, so what it does to that one is done to `custom`: the hue turned as far, saturation and lightness scaled as
// much. A kind that is grey in the game, such as white, stays as it is there.
static rgb restyle(rgb custom, rgb game_string, rgb game_kind) {
  hsl c = to_hsl(custom), from = to_hsl(game_string), to = to_hsl(game_kind);
  if (to.s < 0.02f) return game_kind;
  float s = from.s > 0.0f ? to.s / from.s : 1.0f, l = from.l > 0.0f ? to.l / from.l : 1.0f;
  return to_rgb((hsl){wrap_hue(c.h + to.h - from.h), clamp_unit(c.s * s), clamp_unit(c.l * l)});
}

#endif
