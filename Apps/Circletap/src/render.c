/* Circle Tap renderer.
 * Every frame the game builds a display list (shapes and text, in drawing order). It is compared with the
 * previous frame's list: only the 16x16 tiles under shapes that changed are drawn again, from scratch
 * (background, stars, every shape over the tile), anti-aliased, and pushed. A full-screen push is rare. */
#include "game.h"
#include <eadk.h>
#include <math.h>
#include <string.h>

#define TILE 16
#define TW (SW / TILE)
#define TH (SH / TILE)

#define C_BG RGB(0x1a, 0x1a, 0x2e)
#define C_TRACK RGB(0x0f, 0x34, 0x60)
#define C_FAIL RGB(0xe9, 0x45, 0x60)

enum { L_NONE, L_RING, L_ARC, L_SEG, L_CSTROKE, L_DISC, L_POLY, L_TEXT, L_RRECT, L_HEART, L_DIM };

typedef struct {
  uint8_t type, font, round, alpha, balpha, pad[3];
  uint32_t c1, c2, c3;
  float f[8];
  const float *pts;
  int16_t n, x0, y0, x1, y1, pad2;
  uint32_t ver;
  char s[52];
} layer_t;

/* fixed slots for the scene so that a shape keeps its index from one frame to the next */
enum { S_TRACK, S_GLOW, S_TARGET, S_GHOST, S_X1, S_X2, S_FLASH_ARC, S_FLASH_RING, S_PART, S_POP = S_PART + N_PARTICLES,
       S_CURSOR = S_POP + N_POPS, S_UI };
#define MAXL 150

static layer_t cur[MAXL], prev[MAXL];
static int ncur, nprev;
static bool all_dirty = true;
static uint8_t dirty[TH][TW];
static int pushed;

/* ------------------------------------------------------------------ stars (background.js) */
#define N_STARS 400
static float st_x[N_STARS], st_y[N_STARS], st_z[N_STARS], st_v[N_STARS], st_s[N_STARS];
typedef struct { int16_t x2, y2; uint8_t r4, i5; } skey_t;
static skey_t sk[N_STARS], sk_prev[N_STARS];
static bool stars_ready;
static int16_t vis[N_STARS];
static int nvis;

static void star_spawn(int i, float z) {
  /* the original spreads 8000 stars over +-1000; only the ones inside the view cone can ever be seen,
   * so these are spawned there (at a lower density, for a small screen) */
  st_x[i] = (rnd() - 0.5f) * 1540;
  st_y[i] = (rnd() - 0.5f) * 1155;
  st_z[i] = z;
  st_v[i] = 0.5f + rnd();
  st_s[i] = 1 + rnd() * 2;
}

static void star_key(int i) {
  skey_t k = {0, 0, 0, 0};
  float z = st_z[i];
  if (z > -1000 && z < -1) {
    float f = 207.85f / -z;   /* 120 / tan(30 deg): the 60 deg camera */
    float x = 160 + st_x[i] * f, y = 120 + st_y[i] * f;
    float r = st_s[i] * 300 / -z * 0.25f;   /* half the point size, scaled for a 240 px tall screen */
    if (r > 3) r = 3;
    float in = 1 + z / 1000;
    if (r < 0.75f) { in *= (r / 0.75f) * (r / 0.75f); r = 0.75f; }
    int i5 = (int)(in * 32 + 0.5f);
    if (i5 > 32) i5 = 32;
    if (i5 > 0 && x > -4 && x < SW + 4 && y > -4 && y < SH + 4) {
      k.x2 = (int16_t)lrintf(x * 2);
      k.y2 = (int16_t)lrintf(y * 2);
      k.r4 = (uint8_t)lrintf(r * 4);
      k.i5 = (uint8_t)i5;
    }
  }
  sk[i] = k;
}

void r_stars_update(float dt) {
  if (!stars_ready) {
    for (int i = 0; i < N_STARS; i++) star_spawn(i, -rnd() * 2000);
    stars_ready = true;
  }
  float k = dt * 60;   /* the original moves them per frame at 60 fps */
  for (int i = 0; i < N_STARS; i++) {
    st_z[i] += st_v[i] * k;
    if (st_z[i] > 1) star_spawn(i, -2000);
    star_key(i);
  }
}

/* ------------------------------------------------------------------ display list */
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static layer_t *add(int slot) {
  if (slot < 0) slot = ncur;
  if (slot >= MAXL) return 0;
  if (slot >= ncur) ncur = slot + 1;
  layer_t *l = &cur[slot];
  memset(l, 0, sizeof *l);
  return l;
}
static void bbox(layer_t *l, float x0, float y0, float x1, float y1) {
  l->x0 = (int16_t)clampf(floorf(x0) - 1, 0, SW);
  l->y0 = (int16_t)clampf(floorf(y0) - 1, 0, SH);
  l->x1 = (int16_t)clampf(ceilf(x1) + 1, 0, SW);
  l->y1 = (int16_t)clampf(ceilf(y1) + 1, 0, SH);
  if (l->x1 <= l->x0 || l->y1 <= l->y0) l->type = L_NONE;
}
static uint8_t a8(float a) { return (uint8_t)clampf(a * 255 + 0.5f, 0, 255); }

void r_begin(void) {
  memset(cur, 0, sizeof(layer_t) * S_UI);
  ncur = S_UI;
}

static void ring(int slot, float cx, float cy, float R, float hw, uint32_t c, float a) {
  layer_t *l = add(slot);
  l->type = L_RING;
  l->c1 = c;
  l->alpha = a8(a);
  l->f[0] = cx, l->f[1] = cy, l->f[2] = R, l->f[3] = hw;
  bbox(l, cx - R - hw, cy - R - hw, cx + R + hw, cy + R + hw);
}
static void arc(int slot, float cx, float cy, float R, float a0, float size, float hw, uint32_t c, float a) {
  layer_t *l = add(slot);
  l->type = L_ARC;
  l->c1 = c;
  l->alpha = a8(a);
  l->f[0] = cx, l->f[1] = cy, l->f[2] = R, l->f[3] = a0, l->f[4] = size, l->f[5] = hw;
  /* bounding box of the arc: its ends and any axis extreme inside it */
  float xs[6], ys[6];
  int n = 0;
  xs[n] = cx + R * cosf(a0), ys[n++] = cy + R * sinf(a0);
  xs[n] = cx + R * cosf(a0 + size), ys[n++] = cy + R * sinf(a0 + size);
  for (int q = 0; q < 4; q++) {
    float qa = q * PI_F / 2;
    if (angle_in_arc(qa, a0, size)) xs[n] = cx + R * cosf(qa), ys[n++] = cy + R * sinf(qa);
  }
  float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
  for (int i = 1; i < n; i++) x0 = fminf(x0, xs[i]), x1 = fmaxf(x1, xs[i]), y0 = fminf(y0, ys[i]), y1 = fmaxf(y1, ys[i]);
  bbox(l, x0 - hw, y0 - hw, x1 + hw, y1 + hw);
}
static void seg(int slot, float x0, float y0, float x1, float y1, float hw, bool round, uint32_t c, float a) {
  layer_t *l = add(slot);
  l->type = L_SEG;
  l->c1 = c;
  l->alpha = a8(a);
  l->round = round;
  l->f[0] = x0, l->f[1] = y0, l->f[2] = x1, l->f[3] = y1, l->f[4] = hw;
  bbox(l, fminf(x0, x1) - hw, fminf(y0, y1) - hw, fmaxf(x0, x1) + hw, fmaxf(y0, y1) + hw);
}
static void cstroke(int slot, float cx, float cy, float r, float hw, uint32_t c, float a) {
  layer_t *l = add(slot);
  l->type = L_CSTROKE;
  l->c1 = c;
  l->alpha = a8(a);
  l->f[0] = cx, l->f[1] = cy, l->f[2] = r, l->f[3] = hw;
  bbox(l, cx - r - hw, cy - r - hw, cx + r + hw, cy + r + hw);
}
static void disc(int slot, float cx, float cy, float r, uint32_t c, float a) {
  layer_t *l = add(slot);
  l->type = L_DISC;
  l->c1 = c;
  l->alpha = a8(a);
  l->f[0] = cx, l->f[1] = cy, l->f[2] = r;
  bbox(l, cx - r, cy - r, cx + r, cy + r);
}
static void polyline(int slot, const float *pts, int n, float hw, uint32_t c, float a, uint32_t ver) {
  layer_t *l = add(slot);
  l->type = L_POLY;
  l->c1 = c;
  l->alpha = a8(a);
  l->pts = pts;
  l->n = (int16_t)n;
  l->f[0] = hw;
  l->ver = ver;
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (int i = 0; i < n; i++) {
    x0 = fminf(x0, pts[2 * i]), x1 = fmaxf(x1, pts[2 * i]);
    y0 = fminf(y0, pts[2 * i + 1]), y1 = fmaxf(y1, pts[2 * i + 1]);
  }
  bbox(l, x0 - hw, y0 - hw, x1 + hw, y1 + hw);
}

static const font_t *const FONTS[] = {&font_s, &font_m, &font_score, &font_title, &font_level};
static const glyph_t *glyph(const font_t *f, char ch) {
  int c = (unsigned char)ch - 32;
  if (c < 0 || c >= 96 || f->map[c] == 255) return 0;
  return &f->g[f->map[c]];
}
float r_text_w(int font, const char *s, float spacing) {
  const font_t *f = FONTS[font];
  float w = 0;
  int n = 0;
  for (; *s; s++, n++) {
    const glyph_t *g = glyph(f, *s);
    w += (g ? g->adv : f->cap / 2) + spacing;
  }
  return n ? w - spacing : 0;
}
/* x, y: the anchor (left / centre / right end of the baseline); scale grows the text around that point
 * vertically centred on the capitals, as a CSS transform of a centred box would */
void r_text(int font, float x, float y, int align, const char *s, uint32_t rgb, float alpha, float spacing, float scale) {
  if (alpha <= 0.004f || scale <= 0.01f) return;
  layer_t *l = add(-1);
  if (!l) return;
  const font_t *f = FONTS[font];
  float w = r_text_w(font, s, spacing);
  float left = align == AL_LEFT ? x : align == AL_CENTER ? x - w / 2 : x - w;
  left = floorf(left + 0.5f);
  y = floorf(y + 0.5f);
  l->type = L_TEXT;
  l->font = (uint8_t)font;
  l->c1 = rgb;
  l->alpha = a8(alpha);
  strncpy(l->s, s, sizeof l->s - 1);
  float ax = align == AL_LEFT ? left : align == AL_CENTER ? left + w / 2 : left + w, ay = y - f->cap / 2.0f;
  l->f[0] = left, l->f[1] = y, l->f[2] = spacing, l->f[3] = scale, l->f[4] = ax, l->f[5] = ay;
  float top = y - f->ascent, bot = y + f->ascent / 3.0f, rgt = left + w + 2;
  float lft = left - 2;
  bbox(l, ax + (lft - ax) * scale, ay + (top - ay) * scale, ax + (rgt - ax) * scale, ay + (bot - ay) * scale);
}
void r_rrect(float x, float y, float w, float h, float rad, uint32_t c1, uint32_t c2, float alpha, uint32_t border, float balpha) {
  layer_t *l = add(-1);
  if (!l) return;
  l->type = L_RRECT;
  l->c1 = c1, l->c2 = c2, l->c3 = border;
  l->alpha = a8(alpha), l->balpha = a8(balpha);
  l->f[0] = x, l->f[1] = y, l->f[2] = w, l->f[3] = h, l->f[4] = rad;
  bbox(l, x, y, x + w, y + h);
}
void r_heart(float x, float y, int px, uint32_t rgb, float alpha) {
  layer_t *l = add(-1);
  if (!l) return;
  l->type = L_HEART;
  l->c1 = rgb;
  l->alpha = a8(alpha);
  l->n = (int16_t)px;
  l->f[0] = floorf(x + 0.5f), l->f[1] = floorf(y + 0.5f);
  bbox(l, l->f[0], l->f[1], l->f[0] + px, l->f[1] + px);
}
void r_dim(uint32_t rgb, float alpha) {
  layer_t *l = add(-1);
  if (!l) return;
  l->type = L_DIM;
  l->c1 = rgb;
  l->alpha = a8(alpha);
  l->x0 = 0, l->y0 = 0, l->x1 = SW, l->y1 = SH;
}

/* ------------------------------------------------------------------ the scene (game-renderer.js draw) */
#define MAXPTS 128
static float track_xy[2 * MAXPTS], target_xy[2 * 21];
static uint32_t target_ver;
static float last_target = -1;

void r_scene(const view_t *v, bool show_cursor) {
  const difficulty_t *d = &DIFF[G.diff];
  float cx = v->cx, cy = v->cy, R = v->size * 0.35f, lw = v->size * d->track * (0.35f / 0.4f), hw = lw / 2;
  uint32_t col = hsl_rgb(G.col);
  const map_t *m = G.map;

  if (m) {
    int n = m->n < MAXPTS ? m->n : MAXPTS;
    for (int i = 0; i < n; i++) track_xy[2 * i] = cx + m->xy[2 * i] * R, track_xy[2 * i + 1] = cy + m->xy[2 * i + 1] * R;
    polyline(S_TRACK, track_xy, n, hw, C_TRACK, 1, (uint32_t)(uintptr_t)m);
    cur[S_TRACK].f[1] = cx, cur[S_TRACK].f[2] = cy, cur[S_TRACK].f[3] = R;
  } else {
    ring(S_TRACK, cx, cy, R, hw, C_TRACK, 1);
  }

  if (G.target_size > 0) {
    if (!m && G.heat > 0.03f)
      arc(S_GLOW, cx, cy, R, G.target_start, G.target_size, lw * (1.1f + 0.55f * G.heat) / 2, col, G.heat * 0.35f);
    if (m) {
      float sp = G.target_start / TWO_PI_F, sz = G.target_size / TWO_PI_F;
      for (int i = 0; i <= 20; i++) {
        float x, y;
        path_point(m, sp + i / 20.0f * sz, &x, &y, 0);
        target_xy[2 * i] = cx + x * R, target_xy[2 * i + 1] = cy + y * R;
      }
      if (G.target_start != last_target) target_ver++, last_target = G.target_start;
      polyline(S_TARGET, target_xy, 21, lw * 0.95f / 2, col, 1, target_ver);
      cur[S_TARGET].f[1] = cx, cur[S_TARGET].f[2] = cy, cur[S_TARGET].f[3] = R, cur[S_TARGET].f[4] = G.target_size;
    } else {
      arc(S_TARGET, cx, cy, R, G.target_start, G.target_size, lw * 0.95f / 2, col, 1);
    }
  }

  if (G.fail_on) {
    float op = fmaxf(0, 1 - G.fail_age / 3), xs = lw * 0.4f, sw = fmaxf(2, v->size * 0.005f) / 2;
    float px, py, ang;
    if (m) {
      path_point(m, G.fail_angle / TWO_PI_F, &px, &py, &ang);
      px = cx + px * R, py = cy + py * R;
    } else {
      ang = G.fail_angle;
      px = cx + cosf(ang) * R, py = cy + sinf(ang) * R;
      float c = cosf(ang), s = sinf(ang);
      seg(S_GHOST, cx + c * (R - hw), cy + s * (R - hw), cx + c * (R + hw), cy + s * (R + hw), lw / 2.5f / 2, false, C_FAIL, op);
    }
    float c = cosf(ang), s = sinf(ang), oy = m ? -lw : 0;
    /* rotate the local X strokes into place */
#define TX(lx, ly) (px + (lx) * c - (ly) * s)
#define TY(lx, ly) (py + (lx) * s + (ly) * c)
    seg(S_X1, TX(-xs, -xs + oy), TY(-xs, -xs + oy), TX(xs, xs + oy), TY(xs, xs + oy), sw, m != 0, C_FAIL, op);
    seg(S_X2, TX(xs, -xs + oy), TY(xs, -xs + oy), TX(-xs, xs + oy), TY(-xs, xs + oy), sw, m != 0, C_FAIL, op);
#undef TX
#undef TY
  }

  if (G.flash.on) {
    float t = G.flash.life / G.flash.max;
    if (G.flash.on_arc) arc(S_FLASH_ARC, cx, cy, R, G.flash.a0, G.flash.size, lw * (1 + 0.5f * (1 - t)) / 2, 0xFFFFFF, 0.85f * t);
    cstroke(S_FLASH_RING, cx + G.flash.x * R, cy + G.flash.y * R, lw * (0.35f + 1.1f * (1 - t)), fmaxf(2, lw * 0.08f * t) / 2, 0xFFFFFF, 0.7f * t);
  }
  for (int i = 0; i < N_PARTICLES; i++) {
    const particle_t *p = &G.p[i];
    if (!p->on) continue;
    float t = p->life / p->max;
    disc(S_PART + i, cx + p->x * R, cy + p->y * R, p->size * lw * (0.5f + 0.5f * t), p->rgb, t);
  }
  for (int i = 0; i < N_POPS; i++) {
    const pop_t *p = &G.pop[i];
    if (!p->on) continue;
    float t = p->life / p->max, rise = (1 - t) * lw * 1.2f;
    int keep = ncur;
    ncur = S_POP + i;   /* r_text appends: aim it at this pop's slot */
    r_text(F_S, cx + p->x * R, cy + p->y * R - rise + font_s.cap / 2.0f, AL_CENTER, "+1", 0xFFFFFF, fminf(1, t * 1.6f), 0, 1);
    ncur = keep;
  }

  if (show_cursor) {
    if (m) {
      float x, y, ang;
      path_point(m, G.angle / TWO_PI_F, &x, &y, &ang);
      x = cx + x * R, y = cy + y * R;
      float nx = -sinf(ang) * hw, ny = cosf(ang) * hw;
      seg(S_CURSOR, x - nx, y - ny, x + nx, y + ny, lw / 2.5f / 2, true, C_FAIL, 1);
    } else {
      float c = cosf(G.angle), s = sinf(G.angle);
      seg(S_CURSOR, cx + c * (R - hw), cy + s * (R - hw), cx + c * (R + hw), cy + s * (R + hw), lw / 2.5f / 2, false, C_FAIL, 1);
    }
  }
}

/* ------------------------------------------------------------------ rasteriser */
static uint32_t buf[TILE * SW];
static uint16_t out565[TILE * SW];
static int BX, BY, BW;   /* the strip being drawn */
#define BH TILE

static inline void blend(uint32_t *p, uint32_t c, int a) {   /* a: 0..256 */
  if (a <= 0) return;
  if (a >= 256) { *p = c; return; }
  uint32_t d = *p;
  uint32_t rb = ((c & 0xFF00FF) * a + (d & 0xFF00FF) * (256 - a)) >> 8;
  uint32_t g = ((c & 0x00FF00) * a + (d & 0x00FF00) * (256 - a)) >> 8;
  *p = (rb & 0xFF00FF) | (g & 0x00FF00);
}
static inline int cov256(float d, float alpha) {   /* d: signed distance inside the shape, in pixels */
  if (d <= -0.5f) return 0;
  float c = d >= 0.5f ? 1 : d + 0.5f;
  return (int)(c * alpha * 256);
}

typedef struct { int x0, y0, x1, y1; } clip_t;
static bool clip_of(const layer_t *l, clip_t *c) {
  c->x0 = l->x0 > BX ? l->x0 : BX;
  c->y0 = l->y0 > BY ? l->y0 : BY;
  c->x1 = l->x1 < BX + BW ? l->x1 : BX + BW;
  c->y1 = l->y1 < BY + BH ? l->y1 : BY + BH;
  return c->x0 < c->x1 && c->y0 < c->y1;
}
#define PIX(x, y) (&buf[((y) - BY) * BW + ((x) - BX)])

/* nearest and farthest distance from (cx, cy) to the clip box */
static void box_dist(const clip_t *c, float cx, float cy, float *dmin, float *dmax) {
  float nx = clampf(cx, (float)c->x0, (float)c->x1) - cx, ny = clampf(cy, (float)c->y0, (float)c->y1) - cy;
  float fx = fmaxf(fabsf(c->x0 - cx), fabsf(c->x1 - cx)), fy = fmaxf(fabsf(c->y0 - cy), fabsf(c->y1 - cy));
  *dmin = sqrtf(nx * nx + ny * ny);
  *dmax = sqrtf(fx * fx + fy * fy);
}

static void draw_ring(const layer_t *l, const clip_t *c) {
  float cx = l->f[0], cy = l->f[1], R = l->f[2], hw = l->f[3], al = l->alpha / 255.0f, dmin, dmax;
  box_dist(c, cx, cy, &dmin, &dmax);
  if (dmin > R + hw + 1 || dmax < R - hw - 1) return;
  for (int y = c->y0; y < c->y1; y++) {
    float dy = y + 0.5f - cy;
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      float dx = x + 0.5f - cx;
      blend(p, l->c1, cov256(hw - fabsf(sqrtf(dx * dx + dy * dy) - R), al));
    }
  }
}

static void draw_arc(const layer_t *l, const clip_t *c) {
  float cx = l->f[0], cy = l->f[1], R = l->f[2], a0 = l->f[3], a1 = a0 + l->f[4], hw = l->f[5], al = l->alpha / 255.0f, dmin, dmax;
  box_dist(c, cx, cy, &dmin, &dmax);
  if (dmin > R + hw + 1 || dmax < R - hw - 1) return;
  float ux = cosf(a0), uy = sinf(a0), vx = cosf(a1), vy = sinf(a1);
  float Ax = cx + R * ux, Ay = cy + R * uy, Bx = cx + R * vx, By = cy + R * vy;
  for (int y = c->y0; y < c->y1; y++) {
    float py = y + 0.5f, dy = py - cy;
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      float px = x + 0.5f, dx = px - cx, d;
      if (ux * dy - uy * dx >= 0 && dx * vy - dy * vx >= 0) d = fabsf(sqrtf(dx * dx + dy * dy) - R);
      else {
        float ax = px - Ax, ay = py - Ay, bx = px - Bx, by = py - By;
        d = sqrtf(fminf(ax * ax + ay * ay, bx * bx + by * by));
      }
      blend(p, l->c1, cov256(hw - d, al));
    }
  }
}

static void draw_seg(const layer_t *l, const clip_t *c) {
  float x0 = l->f[0], y0 = l->f[1], x1 = l->f[2], y1 = l->f[3], hw = l->f[4], al = l->alpha / 255.0f;
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 1e-4f) return;
  float ux = dx / len, uy = dy / len, mx = (x0 + x1) / 2, my = (y0 + y1) / 2, hl = len / 2;
  for (int y = c->y0; y < c->y1; y++) {
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      float px = x + 0.5f - mx, py = y + 0.5f - my;
      float u = px * ux + py * uy, v = px * uy - py * ux;
      int a;
      if (l->round) {
        float t = fabsf(u) - hl;
        float d = t > 0 ? sqrtf(t * t + v * v) : fabsf(v);
        a = cov256(hw - d, al);
      } else {
        float cu = hl - fabsf(u) + 0.5f, cv = hw - fabsf(v) + 0.5f;
        if (cu <= 0 || cv <= 0) continue;
        a = (int)(fminf(cu, 1) * fminf(cv, 1) * al * 256);
      }
      blend(p, l->c1, a);
    }
  }
}

static void draw_cstroke(const layer_t *l, const clip_t *c) {
  float cx = l->f[0], cy = l->f[1], r = l->f[2], hw = l->f[3], al = l->alpha / 255.0f;
  for (int y = c->y0; y < c->y1; y++) {
    float dy = y + 0.5f - cy;
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      float dx = x + 0.5f - cx;
      blend(p, l->c1, cov256(hw - fabsf(sqrtf(dx * dx + dy * dy) - r), al));
    }
  }
}

static void draw_disc(const layer_t *l, const clip_t *c) {
  float cx = l->f[0], cy = l->f[1], r = l->f[2], al = l->alpha / 255.0f;
  if (r < 0.6f) { al *= r / 0.6f * (r / 0.6f); r = 0.6f; }
  for (int y = c->y0; y < c->y1; y++) {
    float dy = y + 0.5f - cy;
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      float dx = x + 0.5f - cx;
      blend(p, l->c1, cov256(r - sqrtf(dx * dx + dy * dy), al));
    }
  }
}

static void draw_poly(const layer_t *l, const clip_t *c) {
  float hw = l->f[0], al = l->alpha / 255.0f, m = hw + 1;
  int16_t idx[MAXPTS];
  int k = 0;
  const float *P = l->pts;
  for (int i = 0; i < l->n - 1; i++) {
    float x0 = fminf(P[2 * i], P[2 * i + 2]) - m, x1 = fmaxf(P[2 * i], P[2 * i + 2]) + m;
    float y0 = fminf(P[2 * i + 1], P[2 * i + 3]) - m, y1 = fmaxf(P[2 * i + 1], P[2 * i + 3]) + m;
    if (x1 < c->x0 || x0 > c->x1 || y1 < c->y0 || y0 > c->y1) continue;
    idx[k++] = (int16_t)i;
  }
  if (!k) return;
  float lim = (hw + 0.5f) * (hw + 0.5f);
  for (int y = c->y0; y < c->y1; y++) {
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      float px = x + 0.5f, py = y + 0.5f, best = 1e9f;
      for (int j = 0; j < k; j++) {
        int i = idx[j];
        float ax = P[2 * i], ay = P[2 * i + 1], bx = P[2 * i + 2] - ax, by = P[2 * i + 3] - ay;
        float wx = px - ax, wy = py - ay, bb = bx * bx + by * by;
        float t = bb > 0 ? clampf((wx * bx + wy * by) / bb, 0, 1) : 0;
        float ex = wx - bx * t, ey = wy - by * t, d2 = ex * ex + ey * ey;
        if (d2 < best) best = d2;
      }
      if (best < lim) blend(p, l->c1, cov256(hw - sqrtf(best), al));
    }
  }
}

static inline int g4(const font_t *f, const glyph_t *g, int gx, int gy) {
  int stride = (g->w + 1) / 2;
  uint8_t b = f->bits[g->off + gy * stride + gx / 2];
  return (gx & 1 ? b >> 4 : b & 15) * 17;
}

static void draw_text(const layer_t *l, const clip_t *c) {
  const font_t *f = FONTS[l->font];
  float pen = l->f[0], base = l->f[1], sp = l->f[2], sc = l->f[3], ax = l->f[4], ay = l->f[5];
  int al = l->alpha;
  for (const char *s = l->s; *s; s++) {
    const glyph_t *g = glyph(f, *s);
    float adv = (g ? g->adv : f->cap / 2) + sp;
    if (g && g->w) {
      float gx0 = pen + g->xo, gy0 = base + g->yo;
      if (sc == 1) {
        int ix = (int)gx0, iy = (int)gy0;
        int x0 = ix > c->x0 ? ix : c->x0, x1 = ix + g->w < c->x1 ? ix + g->w : c->x1;
        int y0 = iy > c->y0 ? iy : c->y0, y1 = iy + g->h < c->y1 ? iy + g->h : c->y1;
        for (int y = y0; y < y1; y++) {
          uint32_t *p = PIX(x0, y);
          for (int x = x0; x < x1; x++, p++) {
            int a = g4(f, g, x - ix, y - iy);
            if (a) blend(p, l->c1, (a * al) >> 7 >> 1);
          }
        }
      } else {
        /* scaled around (ax, ay): bilinear samples of the glyph */
        float sx0 = ax + (gx0 - ax) * sc, sy0 = ay + (gy0 - ay) * sc;
        int x0 = (int)floorf(sx0), y0 = (int)floorf(sy0), x1 = (int)ceilf(sx0 + g->w * sc), y1 = (int)ceilf(sy0 + g->h * sc);
        if (x0 < c->x0) x0 = c->x0;
        if (y0 < c->y0) y0 = c->y0;
        if (x1 > c->x1) x1 = c->x1;
        if (y1 > c->y1) y1 = c->y1;
        float inv = 1 / sc;
        for (int y = y0; y < y1; y++) {
          float ly = (y + 0.5f - sy0) * inv - 0.5f;
          int iy = (int)floorf(ly);
          float fy = ly - iy;
          uint32_t *p = PIX(x0, y);
          for (int x = x0; x < x1; x++, p++) {
            float lx = (x + 0.5f - sx0) * inv - 0.5f;
            int ix = (int)floorf(lx);
            float fx = lx - ix;
            float v = 0;
            for (int j = 0; j < 2; j++)
              for (int i = 0; i < 2; i++) {
                int gx = ix + i, gy = iy + j;
                if (gx < 0 || gy < 0 || gx >= g->w || gy >= g->h) continue;
                v += g4(f, g, gx, gy) * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
              }
            if (v > 0) blend(p, l->c1, (int)(v * al / 255.0f + 0.5f));
          }
        }
      }
    }
    pen += adv;
  }
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return RGB(r, g, bl);
}

static void draw_rrect(const layer_t *l, const clip_t *c) {
  float x = l->f[0], y = l->f[1], w = l->f[2], h = l->f[3], rad = l->f[4];
  float mx = x + w / 2, my = y + h / 2, hx = w / 2 - rad, hy = h / 2 - rad;
  float al = l->alpha / 255.0f, bal = l->balpha / 255.0f;
  bool grad = l->c2 != l->c1;
  for (int py = c->y0; py < c->y1; py++) {
    uint32_t *p = PIX(c->x0, py);
    for (int px = c->x0; px < c->x1; px++, p++) {
      float qx = fabsf(px + 0.5f - mx) - hx, qy = fabsf(py + 0.5f - my) - hy;
      float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
      float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;   /* < 0 inside */
      if (d >= 0.5f) continue;
      if (al > 0) {
        /* CSS 45deg: from the bottom-left corner to the top-right one */
        uint32_t col = grad ? lerp_rgb(l->c1, l->c2, clampf(((px + 0.5f - x) + (y + h - py - 0.5f)) / (w + h), 0, 1)) : l->c1;
        blend(p, col, cov256(-d, al));
      }
      if (bal > 0 && d > -1.5f) blend(p, l->c3, (int)(clampf(0.5f - d, 0, 1) * clampf(d + 1.5f, 0, 1) * bal * 256));
    }
  }
}

static void draw_heart(const layer_t *l, const clip_t *c) {
  const uint8_t *hm = l->n == 14 ? heart14 : heart10;
  int ox = (int)l->f[0], oy = (int)l->f[1], al = l->alpha;
  for (int y = c->y0; y < c->y1; y++) {
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) {
      int hx = x - ox, hy = y - oy;
      if (hx < 0 || hy < 0 || hx >= l->n || hy >= l->n) continue;
      blend(p, l->c1, (hm[hy * l->n + hx] * al) >> 8);
    }
  }
}

static void draw_dim(const layer_t *l, const clip_t *c) {
  for (int y = c->y0; y < c->y1; y++) {
    uint32_t *p = PIX(c->x0, y);
    for (int x = c->x0; x < c->x1; x++, p++) blend(p, l->c1, l->alpha);
  }
}

static void draw_stars(void) {
  for (int j = 0; j < nvis; j++) {
    const skey_t *k = &sk[vis[j]];
    float cx = k->x2 * 0.5f, cy = k->y2 * 0.5f, r = k->r4 * 0.25f, in = k->i5 / 32.0f;
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r), y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    if (x1 <= BX || x0 >= BX + BW || y1 <= BY || y0 >= BY + BH) continue;
    if (x0 < BX) x0 = BX;
    if (y0 < BY) y0 = BY;
    if (x1 > BX + BW) x1 = BX + BW;
    if (y1 > BY + BH) y1 = BY + BH;
    float ir2 = 1 / (r * r);
    for (int y = y0; y < y1; y++) {
      float dy = y + 0.5f - cy;
      uint32_t *p = PIX(x0, y);
      for (int x = x0; x < x1; x++, p++) {
        float dx = x + 0.5f - cx, q = 1 - (dx * dx + dy * dy) * ir2;
        if (q <= 0) continue;
        int add = (int)(q * in * 255);   /* additive white */
        uint32_t d = *p;
        int rr = ((d >> 16) & 255) + add, gg = ((d >> 8) & 255) + add, bb = (d & 255) + add;
        *p = RGB(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb);
      }
    }
  }
}

static void render_strip(int x, int y, int w) {
  BX = x, BY = y, BW = w;
  for (int i = 0; i < w * BH; i++) buf[i] = C_BG;
  draw_stars();
  for (int i = 0; i < ncur; i++) {
    const layer_t *l = &cur[i];
    clip_t c;
    if (l->type == L_NONE || !clip_of(l, &c)) continue;
    switch (l->type) {
      case L_RING: draw_ring(l, &c); break;
      case L_ARC: draw_arc(l, &c); break;
      case L_SEG: draw_seg(l, &c); break;
      case L_CSTROKE: draw_cstroke(l, &c); break;
      case L_DISC: draw_disc(l, &c); break;
      case L_POLY: draw_poly(l, &c); break;
      case L_TEXT: draw_text(l, &c); break;
      case L_RRECT: draw_rrect(l, &c); break;
      case L_HEART: draw_heart(l, &c); break;
      case L_DIM: draw_dim(l, &c); break;
    }
  }
  for (int i = 0; i < w * BH; i++) {
    uint32_t c = buf[i];
    out565[i] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
  }
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, BH}, out565);
  pushed += w * BH;
}

static void mark(int x0, int y0, int x1, int y1) {   /* pixels, x1 / y1 exclusive */
  if (x1 <= x0 || y1 <= y0) return;
  int tx0 = x0 / TILE, ty0 = y0 / TILE, tx1 = (x1 - 1) / TILE, ty1 = (y1 - 1) / TILE;
  if (tx0 < 0) tx0 = 0;
  if (ty0 < 0) ty0 = 0;
  if (tx1 >= TW) tx1 = TW - 1;
  if (ty1 >= TH) ty1 = TH - 1;
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) dirty[ty][tx] = 1;
}
static void mark_layer(const layer_t *l) {
  if (l->type != L_NONE) mark(l->x0, l->y0, l->x1, l->y1);
}
static void mark_star(const skey_t *k) {
  if (!k->i5) return;
  float cx = k->x2 * 0.5f, cy = k->y2 * 0.5f, r = k->r4 * 0.25f;
  mark((int)floorf(cx - r), (int)floorf(cy - r), (int)ceilf(cx + r) + 1, (int)ceilf(cy + r) + 1);
}

static void flush(void) {
  pushed = 0;
  for (int ty = 0; ty < TH; ty++)
    for (int tx = 0; tx < TW;) {
      if (!dirty[ty][tx]) { tx++; continue; }
      int t0 = tx;
      while (tx < TW && dirty[ty][tx]) dirty[ty][tx++] = 0;
      render_strip(t0 * TILE, ty * TILE, (tx - t0) * TILE);
    }
}

void r_invalidate_all(void) { all_dirty = true; }
/* draws the last frame again over the whole screen (the PC check compares it with the partial redraw) */
void r_redraw_all(void) {
  memset(dirty, 1, sizeof dirty);
  flush();
}
int r_last_pixels(void) { return pushed; }

void r_end(void) {
  if (!stars_ready) r_stars_update(0);
  if (all_dirty) {
    memset(dirty, 1, sizeof dirty);
    all_dirty = false;
  } else {
    int n = ncur > nprev ? ncur : nprev;
    for (int i = 0; i < n; i++) {
      const layer_t *a = i < nprev ? &prev[i] : 0, *b = i < ncur ? &cur[i] : 0;
      bool ea = !a || a->type == L_NONE, eb = !b || b->type == L_NONE;
      if (ea && eb) continue;
      if (ea != eb || memcmp(a, b, sizeof *a)) {
        if (!ea) mark_layer(a);
        if (!eb) mark_layer(b);
      }
    }
#ifdef HOST_STATS
    extern long stat_star_tiles;
    int before = 0;
    for (int i = 0; i < TH * TW; i++) before += ((uint8_t *)dirty)[i];
#endif
    for (int i = 0; i < N_STARS; i++)
      if (memcmp(&sk[i], &sk_prev[i], sizeof(skey_t))) {
        mark_star(&sk_prev[i]);
        mark_star(&sk[i]);
      }
#ifdef HOST_STATS
    int after = 0;
    for (int i = 0; i < TH * TW; i++) after += ((uint8_t *)dirty)[i];
    stat_star_tiles += after - before;
#endif
  }
  nvis = 0;
  for (int i = 0; i < N_STARS; i++)
    if (sk[i].i5) vis[nvis++] = (int16_t)i;
  flush();
  memcpy(prev, cur, sizeof(layer_t) * ncur);
  nprev = ncur;
  memcpy(sk_prev, sk, sizeof sk);
}
