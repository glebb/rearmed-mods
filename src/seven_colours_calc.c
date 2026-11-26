// Seven-String Guitar (seven.h): the colours part's arithmetic (seven_colours_calc.h). Pure.
#include <string.h>

#include "seven_colours.h"
#include "seven_colours_calc.h"

static float brightest(const float c[3]) {
  float m = c[0] > c[1] ? c[0] : c[1];
  return m > c[2] ? m : c[2];
}

void seven_colours_dim(const float rgb[3], float dim, float out[3]) {
  float m = brightest(rgb), k = m > 0.0f ? dim / m : 0.0f;
  for (int i = 0; i < 3; i++) out[i] = rgb[i] * k;
}

void seven_colours_glow(const float rgb[3], float out[3]) {
  float m = brightest(rgb), k = m > 0.0f ? 1.0f / m : 0.0f;
  for (int i = 0; i < 3; i++) out[i] = rgb[i] * k;
}

uint8_t seven_ramp_recolour(uint8_t g, uint8_t a, uint8_t c) {
  if (a == 0) return g == 0 ? 0 : (uint8_t)(c + ((255 - c) * g + 127) / 255);
  if (g <= a) return (uint8_t)((c * g + a / 2) / a);
  if (a == 255) return c;
  return (uint8_t)(c + ((255 - c) * (g - a) + (255 - a) / 2) / (255 - a));
}

void seven_ramp_paint(uint8_t *rgba, int w, int h, const float rgb6[2][3], const float rgb7[2][3]) {
  static const float v[2][2] = {{SEVEN_RAMP_V6_0, SEVEN_RAMP_V6_1}, {SEVEN_RAMP_V7_0, SEVEN_RAMP_V7_1}};
  size_t line = (size_t)w * 4;
  int knee = w * 2 / 5;  // 40% across: where the game's rows (and mod_colors.c's) have their colour
  for (int m = 0; m < 2; m++)
    for (int band = 0; band < 2; band++) {
      const float(*rgb)[3] = band ? rgb7 : rgb6;
      int row = m ? 15 : 7, from = row * (h / 16) + h / 32;
      int to = (int)(v[band][m] * (float)h) - 1;  // the two lines around V
      uint8_t c[3] = {(uint8_t)(rgb[m][0] * 255.0f + 0.5f), (uint8_t)(rgb[m][1] * 255.0f + 0.5f),
                      (uint8_t)(rgb[m][2] * 255.0f + 0.5f)};
      const uint8_t *src = rgba + (size_t)from * line, *anchor = src + knee * 4;
      uint8_t *out = rgba + (size_t)to * line;
      for (int x = 0; x < w; x++) {
        for (int k = 0; k < 3; k++) out[x * 4 + k] = seven_ramp_recolour(src[x * 4 + k], anchor[k], c[k]);
        out[x * 4 + 3] = src[x * 4 + 3];
      }
      memcpy(out + line, out, line);
    }
}
