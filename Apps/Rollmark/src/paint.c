#include "paint.h"
#include <math.h>

uint32_t rgb_mix(uint32_t a, uint32_t b, int k) {
  if (k <= 0) return b;
  if (k >= 256) return a;
  uint32_t r = (((a >> 16) & 255) * k + ((b >> 16) & 255) * (256 - k)) >> 8;
  uint32_t g = (((a >> 8) & 255) * k + ((b >> 8) & 255) * (256 - k)) >> 8;
  uint32_t bl = ((a & 255) * k + (b & 255) * (256 - k)) >> 8;
  return r << 16 | g << 8 | bl;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* signed distance from the pixel's centre to the card's rounded edge (negative inside) */
static float dist(const card_t *c, int x, int y) {
  float hx = c->w * 0.5f, hy = c->h * 0.5f;
  float px = fabsf(x + 0.5f - (c->x + hx)), py = fabsf(y + 0.5f - (c->y + hy));
  float qx = px - (hx - c->r), qy = py - (hy - c->r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy;
  return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - c->r;
}

uint32_t card_px(const card_t *c, int x, int y) {
  float d = dist(c, x, y);
  uint32_t out = c->out;
  if (c->glow && d > 0 && d < c->glow_px) {
    float g = 1 - d / c->glow_px;
    out = rgb_mix(c->glow, out, (int)(g * g * 120));
  }
  float cover = clampf(0.5f - d, 0, 1);
  if (cover <= 0) return out;
  float t = c->w > 1 ? clampf((x + 0.5f - c->x) / c->w, 0, 1) : 0;
  uint32_t fill = t < 0.5f ? rgb_mix(c->fill[1], c->fill[0], (int)(t * 512)) : rgb_mix(c->fill[2], c->fill[1], (int)((t - 0.5f) * 512));
  if (c->sheen) {
    float u = ((x - c->x) / (float)c->w + (y - c->y) / (float)c->h) * 0.5f;
    float a = u < 0.4f ? 1 - 0.75f * u / 0.4f : u < 0.6f ? 0.25f * (0.6f - u) / 0.2f : 0;
    fill = rgb_mix(0xFFFFFF, fill, (int)(a * c->sheen));
  }
  uint32_t col = fill;
  if (c->bw > 0) {
    float b = clampf(d + c->bw + 0.5f, 0, 1);
    uint32_t border = rgb_mix(c->border[1], c->border[0], (int)(t * 256));
    col = rgb_mix(border, fill, (int)(b * 256));
  }
  return rgb_mix(col, out, (int)(cover * 256));
}

void card_draw(const card_t *c) {
  int m = c->glow ? c->glow_px : 1;
  card_area(c, c->x - m, c->y - m, c->w + 2 * m, c->h + 2 * m);
}

void card_area(const card_t *c, int ax, int ay, int aw, int ah) {
  static uint16_t row[320];
  int x0 = ax, x1 = ax + aw, y0 = ay, y1 = ay + ah;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > 320) x1 = 320;
  if (y1 > 240) y1 = 240;
  if (x1 <= x0) return;
  /* inside the border and away from the corners only the gradient and the sheen matter: worked out per column */
  static uint32_t col_fill[320];
  int in0 = c->x + c->bw + 1, in1 = c->x + c->w - c->bw - 1;   /* columns fully inside */
  for (int x = in0; x < in1; x++) {
    float t = (x + 0.5f - c->x) / c->w;
    col_fill[x] = t < 0.5f ? rgb_mix(c->fill[1], c->fill[0], (int)(t * 512)) : rgb_mix(c->fill[2], c->fill[1], (int)((t - 0.5f) * 512));
  }
  for (int y = y0; y < y1; y++) {
    bool plain = y >= c->y + c->r + 1 && y < c->y + c->h - c->r - 1 && y >= c->y + c->bw + 1 && y < c->y + c->h - c->bw - 1;
    for (int x = x0; x < x1; x++) {
      if (plain && x >= in0 && x < in1) {
        uint32_t f = col_fill[x];
        if (c->sheen) {
          float u = ((x - c->x) / (float)c->w + (y - c->y) / (float)c->h) * 0.5f;
          float a = u < 0.4f ? 1 - 0.75f * u / 0.4f : u < 0.6f ? 0.25f * (0.6f - u) / 0.2f : 0;
          f = rgb_mix(0xFFFFFF, f, (int)(a * c->sheen));
        }
        row[x - x0] = RGB(f);
      } else row[x - x0] = RGB(card_px(c, x, y));
    }
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x0, (uint16_t)y, (uint16_t)(x1 - x0), 1}, row);
  }
}

static const glyph_t *glyph(const font_t *f, char ch) {
  if (ch < 32 || ch > 126 || f->map[ch - 32] == 255) return 0;
  return &f->g[f->map[ch - 32]];
}

int card_text(const card_t *c, const font_t *f, int x, int base, const char *s, uint32_t fg) {
  static uint16_t buf[48 * 48];
  for (; *s; s++) {
    const glyph_t *g = glyph(f, *s);
    if (!g) continue;
    int gx = x + g->xo, gy = base + g->yo;
    if (g->w && g->w * g->h <= 48 * 48 && gx >= 0 && gy >= 0 && gx + g->w <= 320 && gy + g->h <= 240) {
      const uint8_t *bits = f->bits + g->off;
      int rb = (g->w + 1) / 2;
      for (int yy = 0; yy < g->h; yy++, bits += rb)
        for (int xx = 0; xx < g->w; xx++) {
          int a = (bits[xx >> 1] >> ((xx & 1) * 4)) & 15;
          buf[yy * g->w + xx] = RGB(rgb_mix(fg, card_px(c, gx + xx, gy + yy), a * 256 / 15));
        }
      eadk_display_push_rect((eadk_rect_t){(uint16_t)gx, (uint16_t)gy, g->w, g->h}, buf);
    } else if (g->w) g_clipped++;
    x += g->adv;
  }
  return x;
}

int card_text_c(const card_t *c, const font_t *f, int cx, int base, const char *s, uint32_t fg) {
  return card_text(c, f, cx - g_text_w(f, s) / 2, base, s, fg);
}

/* a picture (RGB565 and 4-bit alpha) blended over the card under it, or over bg when c is NULL */
void img_draw(const img_t *im, int x, int y, const card_t *c, uint32_t bg) {
  static uint16_t row[320];
  for (int yy = 0; yy < im->h; yy++) {
    int py = y + yy;
    if (py < 0 || py >= 240) continue;
    int x0 = x < 0 ? -x : 0, x1 = x + im->w > 320 ? 320 - x : im->w;
    if (x1 <= x0) return;
    for (int xx = x0; xx < x1; xx++) {
      int i = yy * im->w + xx, a = (im->a[i >> 1] >> ((i & 1) * 4)) & 15;
      uint32_t under = c ? card_px(c, x + xx, py) : bg;
      if (a == 0) { row[xx - x0] = RGB(under); continue; }
      uint16_t p = im->px[i];
      if (a == 15) { row[xx - x0] = p; continue; }
      uint32_t rgb = (uint32_t)((p >> 11) << 3 | (p >> 13)) << 16 | (uint32_t)(((p >> 5) & 63) << 2 | ((p >> 9) & 3)) << 8 |
                     (uint32_t)((p & 31) << 3 | ((p >> 2) & 7));
      row[xx - x0] = RGB(rgb_mix(rgb, under, a * 256 / 15));
    }
    eadk_display_push_rect((eadk_rect_t){(uint16_t)(x + x0), (uint16_t)py, (uint16_t)(x1 - x0), 1}, row);
  }
}
