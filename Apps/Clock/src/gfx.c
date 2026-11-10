#include "gfx.h"

int g_clipped;

void g_fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0 || y < 0 || x + w > 320 || y + h > 240) { g_clipped++; return; }
  if (w <= 0 || h <= 0) return;
  eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
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

#define MAXCH 64
void g_box(const font_t *f, int x, int y, int w, int h, int tx, int base, const char *s, uint16_t fg, uint16_t bg) {
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 320 || y + h > 240) { g_clipped++; return; }
  static uint16_t row[320];
  uint16_t ramp[16];
  for (int a = 0; a < 16; a++) ramp[a] = mix(fg, bg, a);
  const glyph_t *gs[MAXCH];
  int gx[MAXCH], n = 0, cx = x + tx;
  for (; *s && n < MAXCH; s++) {
    const glyph_t *g = glyph(f, *s);
    if (!g) continue;
    if (g->w) {
      if (cx + g->xo < x || cx + g->xo + g->w > x + w) g_clipped++;   /* still drawn, cut to the box */
      gs[n] = g, gx[n++] = cx + g->xo;
    }
    cx += g->adv;
  }
  for (int r = 0; r < h; r++) {
    int py = y + r;
    for (int i = 0; i < w; i++) row[i] = bg;
    for (int k = 0; k < n; k++) {
      const glyph_t *g = gs[k];
      int gy = py - (y + base + g->yo);
      if (gy < 0 || gy >= g->h) continue;
      const uint8_t *bits = f->bits + g->off + gy * ((g->w + 1) / 2);
      for (int xx = 0; xx < g->w; xx++) {
        int px = gx[k] + xx - x;
        if (px < 0 || px >= w) continue;
        int a = (bits[xx >> 1] >> ((xx & 1) * 4)) & 15;
        if (a) row[px] = ramp[a];
      }
    }
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)py, (uint16_t)w, 1}, row);
  }
}

void g_box_c(const font_t *f, int x, int y, int w, int h, const char *s, uint16_t fg, uint16_t bg) {
  g_box(f, x, y, w, h, (w - g_text_w(f, s)) / 2, (h + f->cap) / 2, s, fg, bg);
}
