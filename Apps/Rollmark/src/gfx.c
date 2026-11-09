#include "gfx.h"

int g_clipped;

void g_fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 320) w = 320 - x;
  if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0) return;
  eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}

void g_frame(int x, int y, int w, int h, int t, uint16_t c) {
  g_fill(x, y, w, t, c);
  g_fill(x, y + h - t, w, t, c);
  g_fill(x, y + t, t, h - 2 * t, c);
  g_fill(x + w - t, y + t, t, h - 2 * t, c);
}

static const glyph_t *glyph(const font_t *f, char ch) {
  if (ch < 32 || ch > 126 || f->map[ch - 32] == 255) return 0;
  return &f->g[f->map[ch - 32]];
}

int g_text_w(const font_t *f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    const glyph_t *g = glyph(f, *s);
    if (g) w += g->adv;
  }
  return w;
}

static uint16_t mix(uint16_t fg, uint16_t bg, int a) { /* a in 0..15 */
  int r = ((fg >> 11) * a + (bg >> 11) * (15 - a)) / 15;
  int gr = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (15 - a)) / 15;
  int b = ((fg & 31) * a + (bg & 31) * (15 - a)) / 15;
  return (uint16_t)(r << 11 | gr << 5 | b);
}

/* glyphs are pushed one by one over a background already filled with bg */
int g_text(const font_t *f, int x, int base, const char *s, uint16_t fg, uint16_t bg) {
  static uint16_t buf[48 * 48];
  uint16_t ramp[16];
  for (int a = 0; a < 16; a++) ramp[a] = mix(fg, bg, a);
  for (; *s; s++) {
    const glyph_t *g = glyph(f, *s);
    if (!g) continue;
    int gx = x + g->xo, gy = base + g->yo;
    if (g->w && g->w * g->h <= 48 * 48 && gx >= 0 && gy >= 0 && gx + g->w <= 320 && gy + g->h <= 240) {
      const uint8_t *row = f->bits + g->off;
      int rb = (g->w + 1) / 2;
      for (int yy = 0; yy < g->h; yy++, row += rb)
        for (int xx = 0; xx < g->w; xx++) buf[yy * g->w + xx] = ramp[(row[xx >> 1] >> ((xx & 1) * 4)) & 15];
      eadk_display_push_rect((eadk_rect_t){(uint16_t)gx, (uint16_t)gy, g->w, g->h}, buf);
    } else if (g->w) g_clipped++;
    x += g->adv;
  }
  return x;
}

int g_text_c(const font_t *f, int cx, int base, const char *s, uint16_t fg, uint16_t bg) {
  return g_text(f, cx - g_text_w(f, s) / 2, base, s, fg, bg);
}
