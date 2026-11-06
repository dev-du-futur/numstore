/* Software renderer for Infinite Stacker.
 * The original draws with three.js r160: an orthographic camera at (4, y, 4) looking at the stack (4 units of
 * view height), an ambient light 0.6 and a directional light 0.6 from (10, 20, 0) on Lambert boxes, in linear
 * colour with sRGB output, and a sky shader (gradient and drifting fbm clouds) behind.
 * Here the screen is cut into 15 bands of 16 rows. Each frame only the parts that changed are drawn and pushed:
 * the sliding block, falling pieces, sparkles, the tower when the camera moves, the texts, and two bands of sky
 * a frame while the clouds drift (they move about 4 pixels a second). Boxes are flat-shaded quads with a depth
 * buffer per band. */
#include <eadk.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "font_data.h"

#define W 320
#define H 240
#define BAND 16
#define NB (H / BAND)

static uint16_t sbuf[BAND * W];
static float zbuf[BAND * W];
static int rx0, rx1, ry0, ry1, rw;   /* the region being drawn */

/* ------------------------------------------------------------------ colour */
static uint8_t lin2srgb[1025];
static float srgb2lin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }

static inline uint16_t mix565(uint16_t a, uint16_t b, int k) {   /* k 0..32: share of b */
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81Fu, B = (b | (uint32_t)b << 16) & 0x07E0F81Fu;
  uint32_t r = (A + (((B - A) * (uint32_t)k) >> 5)) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint16_t dim565(uint16_t c, int k) {
  uint32_t A = (c | (uint32_t)c << 16) & 0x07E0F81Fu;
  uint32_t r = ((A * (uint32_t)k) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}

/* Lambert as three.js r160 does it (physically based light units: the BRDF divides by pi) */
static const float LIGHT[3] = {0.4472136f, 0.8944272f, 0};
static uint16_t shade(const float col[3], const float emis[3], const float n[3]) {
  float d = n[0] * LIGHT[0] + n[1] * LIGHT[1] + n[2] * LIGHT[2];
  float k = (0.6f + 0.6f * (d > 0 ? d : 0)) * 0.31830989f;
  int c[3];
  for (int i = 0; i < 3; i++) {
    float v = col[i] * k + (emis ? emis[i] : 0);
    if (v > 1) v = 1;
    if (v < 0) v = 0;
    c[i] = lin2srgb[(int)(v * 1024 + 0.5f)];
  }
  return RGB(c[0], c[1], c[2]);
}

/* ------------------------------------------------------------------ camera */
typedef struct {
  float P[3];
  float X[3], Y[3], Z[3];   /* camera axes (Z points back at the camera) */
  float S;                  /* pixels per unit */
  float camY, lookY, top;
  float ux[3], uy[3], uz[3];   /* world unit axes, projected: (sx, sy, depth) per unit */
} cam_t;
static cam_t cam, cam_prev;
static bool cam_valid;

static void cam_make(cam_t *c, float camY, float lookY, float top) {
  c->camY = camY;
  c->lookY = lookY;
  c->top = top;
  c->P[0] = 4; c->P[1] = camY; c->P[2] = 4;
  float z[3] = {4, camY - lookY, 4}, l = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
  for (int i = 0; i < 3; i++) c->Z[i] = z[i] / l;
  const float r = 0.70710678f;
  c->X[0] = r; c->X[1] = 0; c->X[2] = -r;
  c->Y[0] = -c->Z[1] * r; c->Y[1] = (c->Z[0] + c->Z[2]) * r; c->Y[2] = -c->Z[1] * r;
  c->S = (H / 2) / top;
  float *u[3] = {c->ux, c->uy, c->uz};
  for (int a = 0; a < 3; a++) {
    u[a][0] = c->S * c->X[a];
    u[a][1] = -c->S * c->Y[a];
    u[a][2] = -c->Z[a];
  }
}
static inline void project(const cam_t *c, float x, float y, float z, float out[3]) {
  float d[3] = {x - c->P[0], y - c->P[1], z - c->P[2]};
  out[0] = W / 2 + c->S * (d[0] * c->X[0] + d[1] * c->X[1] + d[2] * c->X[2]);
  out[1] = H / 2 - c->S * (d[0] * c->Y[0] + d[1] * c->Y[1] + d[2] * c->Y[2]);
  out[2] = -(d[0] * c->Z[0] + d[1] * c->Z[1] + d[2] * c->Z[2]);
}
static inline void project_vec(const cam_t *c, const float v[3], float out[3]) {
  out[0] = v[0] * c->ux[0] + v[1] * c->uy[0] + v[2] * c->uz[0];
  out[1] = v[0] * c->ux[1] + v[1] * c->uy[1] + v[2] * c->uz[1];
  out[2] = v[0] * c->ux[2] + v[1] * c->uy[2] + v[2] * c->uz[2];
}

/* ------------------------------------------------------------------ projected boxes */
typedef struct {
  float c[3];        /* centre: sx, sy, depth */
  float a[3][3];     /* half axes, projected */
  int8_t sign[3];    /* visible side of each axis (0: edge on) */
  uint16_t col[3];   /* colour of that face */
  int16_t x0, y0, x1, y1;
} pbox_t;

#define MAX_DYN (2 + MAX_BODIES + MAX_PARTS + N_SPARK + N_MSPARK)
static pbox_t dyn[MAX_DYN];
static int ndyn;

/* axes: three world vectors (half extents), normals: their unit vectors */
static void pbox_make(const cam_t *c, const float ctr[3], const float axes[3][3], const float units[3][3],
                      const float col[3], const float emis[3], pbox_t *b) {
  project(c, ctr[0], ctr[1], ctr[2], b->c);
  float ex = 0, ey = 0;
  for (int k = 0; k < 3; k++) {
    project_vec(c, axes[k], b->a[k]);
    ex += fabsf(b->a[k][0]);
    ey += fabsf(b->a[k][1]);
    float dz = b->a[k][2];
    b->sign[k] = dz < -1e-5f ? 1 : dz > 1e-5f ? -1 : 0;
    if (b->sign[k]) {
      float n[3] = {units[k][0] * b->sign[k], units[k][1] * b->sign[k], units[k][2] * b->sign[k]};
      b->col[k] = shade(col, emis, n);
    }
  }
  b->x0 = (int16_t)fmaxf(-1, floorf(b->c[0] - ex) - 1);
  b->x1 = (int16_t)fminf(W + 1, ceilf(b->c[0] + ex) + 1);
  b->y0 = (int16_t)fmaxf(-1, floorf(b->c[1] - ey) - 1);
  b->y1 = (int16_t)fminf(H + 1, ceilf(b->c[1] + ey) + 1);
}
static const float UNITS[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
static void pbox_aligned(const cam_t *c, float x, float y, float z, float hx, float hy, float hz,
                         const float col[3], const float emis[3], pbox_t *b) {
  float ctr[3] = {x, y, z};
  float axes[3][3] = {{hx, 0, 0}, {0, hy, 0}, {0, 0, hz}};
  pbox_make(c, ctr, axes, UNITS, col, emis, b);
}

static void fill_face(const float F[3], const float B[3], const float C[3], uint16_t col) {
  float det = B[0] * C[1] - B[1] * C[0];
  if (fabsf(det) < 0.02f) return;
  float zx = (C[1] * B[2] - B[1] * C[2]) / det, zy = (B[0] * C[2] - C[0] * B[2]) / det;
  float vx[4], vy[4];
  for (int k = 0; k < 4; k++) {
    float sb = (k == 1 || k == 2) ? 1 : -1, sc = k >= 2 ? 1 : -1;
    vx[k] = F[0] + sb * B[0] + sc * C[0];
    vy[k] = F[1] + sb * B[1] + sc * C[1];
  }
  float miny = fminf(fminf(vy[0], vy[1]), fminf(vy[2], vy[3])), maxy = fmaxf(fmaxf(vy[0], vy[1]), fmaxf(vy[2], vy[3]));
  int ya = (int)ceilf(miny - 0.5f), yb = (int)ceilf(maxy - 0.5f);
  if (ya < ry0) ya = ry0;
  if (yb > ry1) yb = ry1;
  for (int y = ya; y < yb; y++) {
    float yc = y + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int k = 0; k < 4; k++) {
      int j = (k + 1) & 3;
      float y0 = vy[k], y1 = vy[j];
      if ((y0 <= yc && yc < y1) || (y1 <= yc && yc < y0)) {
        float x = vx[k] + (yc - y0) * (vx[j] - vx[k]) / (y1 - y0);
        if (x < xl) xl = x;
        if (x > xr) xr = x;
      }
    }
    if (xr < xl) continue;
    int xa = (int)ceilf(xl - 0.5f), xb = (int)ceilf(xr - 0.5f);
    if (xa < rx0) xa = rx0;
    if (xb > rx1) xb = rx1;
    if (xa >= xb) continue;
    float z = F[2] + zx * (xa + 0.5f - F[0]) + zy * (yc - F[1]);
    uint16_t *p = &sbuf[(y - ry0) * rw + xa - rx0];
    float *zp = &zbuf[(y - ry0) * rw + xa - rx0];
    for (int x = xa; x < xb; x++, p++, zp++, z += zx)
      if (z < *zp) { *zp = z; *p = col; }
  }
}

static void draw_pbox(const pbox_t *b) {
  if (b->x1 <= rx0 || b->x0 >= rx1 || b->y1 <= ry0 || b->y0 >= ry1) return;
  for (int k = 0; k < 3; k++) {
    if (!b->sign[k]) continue;
    int i = (k + 1) % 3, j = (k + 2) % 3;
    float F[3] = {b->c[0] + b->a[k][0] * b->sign[k], b->c[1] + b->a[k][1] * b->sign[k], b->c[2] + b->a[k][2] * b->sign[k]};
    fill_face(F, b->a[i], b->a[j], b->col[k]);
  }
}

/* ------------------------------------------------------------------ stack */
static int vis_lo, vis_hi;   /* layers that can be on screen */
static float flash_emis[3];

static bool block_flash(const block_t *b, float e[3]) {
  if (b->flash_at < 0) return false;
  int32_t dt = G.now - b->flash_at;
  if (dt < 0 || dt >= 400) return false;
  float s = 1 - dt / 400.0f;
  e[0] = 0.85f * s; e[1] = 0.75f * s; e[2] = 0.4f * s;
  return true;
}

static void layer_pbox(const cam_t *c, int i, pbox_t *pb) {
  const block_t *b = blk(i);
  float hy = i == 0 ? BASE_H / 2 : BLOCK_H / 2;
  bool fl = block_flash(b, flash_emis);
  pbox_aligned(c, b->x, layer_y(i), b->z, b->w / 2, hy, b->d / 2, b->col, fl ? flash_emis : NULL, pb);
}

/* quick screen bounds of a layer, without its colours */
static void layer_bounds(const cam_t *c, int i, int *x0, int *y0, int *x1, int *y1) {
  const block_t *b = blk(i);
  float hy = i == 0 ? BASE_H / 2 : BLOCK_H / 2, p[3];
  project(c, b->x, layer_y(i), b->z, p);
  float ex = fabsf(c->ux[0]) * b->w / 2 + fabsf(c->uy[0]) * hy + fabsf(c->uz[0]) * b->d / 2;
  float ey = fabsf(c->ux[1]) * b->w / 2 + fabsf(c->uy[1]) * hy + fabsf(c->uz[1]) * b->d / 2;
  *x0 = (int)floorf(p[0] - ex) - 1;
  *x1 = (int)ceilf(p[0] + ex) + 1;
  *y0 = (int)floorf(p[1] - ey) - 1;
  *y1 = (int)ceilf(p[1] + ey) + 1;
}

static int tower_box[4], tower_box_prev[4];
static void find_visible_layers(const cam_t *c) {
  int first = G.n > MAX_STACK ? G.n - MAX_STACK + 1 : 0;
  vis_lo = G.n;
  vis_hi = -1;
  int tb[4] = {W, H, 0, 0};
  for (int i = G.n - 1; i >= first; i--) {
    int x0, y0, x1, y1;
    layer_bounds(c, i, &x0, &y0, &x1, &y1);
    if (y0 >= H + 40) {   /* layers below are further down still */
      if (i > 0) { i = 1; continue; }   /* but the base reaches up */
      continue;
    }
    if (x1 <= 0 || x0 >= W || y1 <= 0 || y0 >= H) continue;
    if (i < vis_lo) vis_lo = i;
    if (i > vis_hi) vis_hi = i;
    if (x0 < tb[0]) tb[0] = x0;
    if (y0 < tb[1]) tb[1] = y0;
    if (x1 > tb[2]) tb[2] = x1;
    if (y1 > tb[3]) tb[3] = y1;
  }
  if (first > 0 && vis_lo > 0) {   /* the base, under a tower taller than we keep */
    int x0, y0, x1, y1;
    layer_bounds(c, 0, &x0, &y0, &x1, &y1);
    if (y0 < H && y1 > 0) vis_lo = 0;
  }
  memcpy(tower_box, tb, sizeof tb);
}

/* ------------------------------------------------------------------ sky */
#define RING (W + 16)          /* a little wider than the screen: bands that lag a column behind still find theirs */
static uint8_t cloud[H][RING / 2];   /* 4-bit cover, a ring of columns: absolute column k at k % RING */
static int32_t cloud_next;        /* columns [cloud_next - RING, cloud_next) are filled */
static int32_t band_scroll[NB];   /* the scroll each band's sky was drawn with */
static uint16_t sky_v[H][3];      /* gradient per row, 0..4080 */

static inline float hash2f(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xFFFFFF) * (1.0f / 16777216.0f);
}
static float vnoise(float x, float y) {
  float fx = floorf(x), fy = floorf(y);
  int ix = (int)fx, iy = (int)fy;
  fx = x - fx;
  fy = y - fy;
  float a = hash2f(ix, iy), b = hash2f(ix + 1, iy), c = hash2f(ix, iy + 1), d = hash2f(ix + 1, iy + 1);
  float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  return a + (b - a) * ux + (c - a) * uy * (1 - ux) + (d - b) * ux * uy;
}
static void cloud_column(int32_t k) {
  float X = 3.0f * (k + 0.5f) / W;   /* fbm(uv * 2) with uv.x stretched 1.5 */
  int col = k % RING, sh = (col & 1) ? 0 : 4;
  for (int y = 0; y < H; y++) {
    float Y = 2.0f * (1 - (y + 0.5f) / H), v = 0, amp = 0.5f, sx = X, sy = Y;
    for (int o = 0; o < 6; o++) {
      v += amp * vnoise(sx, sy);
      sx *= 2;
      sy *= 2;
      amp *= 0.5f;
    }
    float t = (v - 0.4f) / 0.2f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    t = t * t * (3 - 2 * t);
    int q = (int)(t * 15 + 0.5f);
    uint8_t *p = &cloud[y][col >> 1];
    *p = (uint8_t)((*p & ~(0xF << sh)) | q << sh);
  }
}
float render_sky_speed = 1;   /* the host test sets 0 to compare partial and full redraws exactly */
static int32_t sky_scroll(int32_t now) { return (int32_t)(now * render_sky_speed * (0.04f * W / 3) / 1000); }   /* 4.27 px/s */
static void sky_advance(int32_t scroll) {
  if (cloud_next < scroll) cloud_next = scroll;   /* first call, or after a long pause */
  while (cloud_next < scroll + W) cloud_column(cloud_next++);
}
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static void draw_sky(int band) {
  int32_t sc = band_scroll[band];
  for (int y = ry0; y < ry1; y++) {
    const uint16_t *sv = sky_v[y];
    uint16_t *p = &sbuf[(y - ry0) * rw];
    const uint8_t *cl = cloud[y], *bay = BAYER[y & 3];
    int col = (sc + rx0) % RING;
    for (int x = rx0; x < rx1; x++, p++) {
      int a = (cl[col >> 1] >> ((col & 1) ? 0 : 4)) & 15;
      if (++col == RING) col = 0;
      int b = bay[x & 3] * 256 + 128;
      int r = sv[0] + (((4080 - sv[0]) * a * 273) >> 12);
      int g = sv[1] + (((4080 - sv[1]) * a * 273) >> 12);
      int bl = sv[2] + (((4080 - sv[2]) * a * 273) >> 12);
      *p = (uint16_t)(((r * 31 + b) >> 12) << 11 | ((g * 63 + b) >> 12) << 5 | ((bl * 31 + b) >> 12));
    }
  }
}

/* ------------------------------------------------------------------ text */
static void glyph_draw(const font_t *f, char ch, float gx, float gy, float scale, uint16_t col, int alpha, int mode) {
  if (ch < f->first || ch > f->last) return;
  const glyph_t *g = &f->g[ch - f->first];
  if (!g->w) return;
  const uint8_t *px = f->px + g->off;
  float x0 = gx + g->bx * scale, y0 = gy + g->by * scale;
  int X0 = (int)floorf(x0), Y0 = (int)floorf(y0), X1 = (int)ceilf(x0 + g->w * scale) + 1, Y1 = (int)ceilf(y0 + g->h * scale) + 1;
  if (X1 <= rx0 || X0 >= rx1 || Y1 <= ry0 || Y0 >= ry1) return;
  if (X0 < rx0) X0 = rx0;
  if (Y0 < ry0) Y0 = ry0;
  if (X1 > rx1) X1 = rx1;
  if (Y1 > ry1) Y1 = ry1;
  float inv = 1 / scale;
  for (int y = Y0; y < Y1; y++) {
    int sy = (int)floorf((y + 0.5f - y0) * inv);
    if (sy < 0 || sy >= g->h) continue;
    uint16_t *p = &sbuf[(y - ry0) * rw + X0 - rx0];
    for (int x = X0; x < X1; x++, p++) {
      int sx = (int)floorf((x + 0.5f - x0) * inv);
      if (sx < 0 || sx >= g->w) continue;
      uint8_t v = px[sy * g->w + sx];
      int a = mode == 1 ? (v & 15) : (v >> 4);   /* 1: the grown glyph (outline) */
      if (!a) continue;
      int k = (a * alpha * 32 + 7 * 256) / (15 * 256);
      if (k > 32) k = 32;
      *p = mix565(*p, col, k);
    }
  }
}
static int text_width(const font_t *f, const char *s) {
  int w = 0;
  for (; *s; s++)
    if (*s >= f->first && *s <= f->last) w += f->g[*s - f->first].adv;
  return w;
}
/* text centred on cx (or left at x when cx < 0 is not used), baseline y; shadow: 0 none, 1 drop shadow, 2 outline */
static void text_at(const font_t *f, const char *s, float x, float y, float scale, uint16_t col, int alpha, int shadow) {
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    float cx = x;
    for (const char *c = s; *c; c++) {
      if (pass == 0) {
        if (shadow == 2) glyph_draw(f, *c, cx, y, scale, 0, alpha / 2, 1);
        else glyph_draw(f, *c, cx + 1, y + 1, scale, 0, alpha / 2, 0);
      } else {
        glyph_draw(f, *c, cx, y, scale, col, alpha, 0);
      }
      if (*c >= f->first && *c <= f->last) cx += f->g[*c - f->first].adv * scale;
    }
  }
}
static void text_c(const font_t *f, const char *s, float cx, float y, float scale, uint16_t col, int alpha, int shadow) {
  text_at(f, s, cx - text_width(f, s) * scale / 2, y, scale, col, alpha, shadow);
}

/* rounded rectangle: fill (dim = 0) or darken to dim/32 */
static void round_rect(int x0, int y0, int x1, int y1, int r, uint16_t col, int dimk) {
  int ya = y0 > ry0 ? y0 : ry0, yb = y1 < ry1 ? y1 : ry1;
  for (int y = ya; y < yb; y++) {
    int inset = 0;
    int dy = y < y0 + r ? y0 + r - y : y >= y1 - r ? y - (y1 - r - 1) : 0;
    if (dy) {
      float fy = dy - 0.5f;
      inset = r - (int)sqrtf(fmaxf(0, r * r - fy * fy));
    }
    int xa = x0 + inset, xb = x1 - inset;
    if (xa < rx0) xa = rx0;
    if (xb > rx1) xb = rx1;
    uint16_t *p = &sbuf[(y - ry0) * rw + xa - rx0];
    for (int x = xa; x < xb; x++, p++) *p = dimk ? dim565(*p, dimk) : col;
  }
}

/* ------------------------------------------------------------------ HUD */
enum { HI_SCORE, HI_BEST, HI_STREAK, HI_INSTR, HI_CARD, HI_FPS, NHUD };
typedef struct {
  bool on;
  int x0, y0, x1, y1;
  uint32_t hash;
} hitem_t;
static hitem_t hud[NHUD], hud_prev[NHUD];
static const hud_t *HUD;
static char score_s[12], best_s[24], fps_s[12], card_l1[32], card_l2[32];
static float st_y, st_scale;
static int st_alpha;
static char st_text[24];

static uint32_t mixh(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static float bezier(float x, float x1, float y1, float x2, float y2) {   /* CSS cubic-bezier(x1, y1, x2, y2) */
  float t = x;
  for (int i = 0; i < 8; i++) {
    float cx = 3 * x1 * t * (1 - t) * (1 - t) + 3 * x2 * t * t * (1 - t) + t * t * t - x;
    float dx = 3 * x1 * (1 - t) * (1 - t) + 6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
    if (fabsf(dx) < 1e-5f) break;
    t -= cx / dx;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
  }
  return 3 * y1 * t * (1 - t) * (1 - t) + 3 * y2 * t * t * (1 - t) + t * t * t;
}

#define CARD_X0 50
#define CARD_X1 270
#define CARD_Y0 58
#define CARD_Y1 182

static void hud_layout(const hud_t *h) {
  memset(hud, 0, sizeof hud);
  if (h->show_score) {
    snprintf(score_s, sizeof score_s, "%d", game_score());
    int w = text_width(&FONT_SCORE, score_s);
    hitem_t *it = &hud[HI_SCORE];
    it->on = true;
    it->x0 = 160 - w / 2 - 3; it->x1 = 160 + w / 2 + 4; it->y0 = 12; it->y1 = 42;
    it->hash = mixh(1, (uint32_t)game_score());
  }
  if (h->mode != 0) {
    snprintf(best_s, sizeof best_s, "Best %d", h->best);
    int w = text_width(&FONT_SMALL, best_s);
    hitem_t *it = &hud[HI_BEST];
    it->on = true;
    it->x0 = 312 - w - 1; it->x1 = 314; it->y0 = 4; it->y1 = 20;
    it->hash = mixh(2, (uint32_t)h->best);
  }
  /* "Perfect! n": pops up 50 ms after the drop, stays 1.5 s, fades in 0.5 s (style.css #perfect-streak) */
  if (G.streak_at >= 0) {
    int32_t t = G.now - G.streak_at - 50;
    bool hiding = G.hide_streak_at >= G.streak_at;
    int32_t th = hiding ? G.now - G.hide_streak_at : -1;
    st_alpha = 0;
    if (t >= 0 && (!hiding || G.hide_streak_at > G.streak_at + 50)) {
      float p = t / 450.0f;
      if (p > 1) p = 1;
      float e = bezier(p, 0.175f, 0.885f, 0.32f, 1.275f);
      st_scale = 0.4f + 0.6f * e;
      st_y = 51 + 9 * e;   /* -170 px to -200 px above the centre */
      float op = p < 0.35f ? bezier(p / 0.35f, 0.175f, 0.885f, 0.32f, 1.275f) : 1;
      if (op > 1) op = 1;
      st_alpha = (int)(op * 256);
      if (hiding) {
        float q = th / 500.0f;
        if (q > 1) q = 1;
        float eq = bezier(q, 0.25f, 0.1f, 0.25f, 1);   /* ease */
        st_alpha = (int)(st_alpha * (1 - eq));
        st_scale = 1;
        st_y = st_y + (45 - st_y) * eq;   /* back to -150 px */
      }
    }
    if (st_alpha > 0) {
      snprintf(st_text, sizeof st_text, "%s %d", G.streak_golden ? "GOLDEN!" : "Perfect!", G.streak_count);
      hitem_t *it = &hud[HI_STREAK];
      int w = (int)(text_width(&FONT_BIG, st_text) * 1.3f / 2) + 4;
      it->on = true;
      it->x0 = 160 - w; it->x1 = 160 + w; it->y0 = 120 - 75; it->y1 = 120 - 30;
      uint32_t qy, qs;
      memcpy(&qy, &st_y, 4);
      memcpy(&qs, &st_scale, 4);
      it->hash = mixh(mixh(mixh(mixh(3, (uint32_t)st_alpha), qy), qs), (uint32_t)G.streak_count * 2 + G.streak_golden);
    }
  }
  if (h->instr_alpha > 0.01f) {
    hitem_t *it = &hud[HI_INSTR];
    it->on = true;
    it->x0 = 60; it->x1 = 260; it->y0 = 180; it->y1 = 202;
    it->hash = mixh(4, (uint32_t)(int)(h->instr_alpha * 256));
  }
  if (h->mode == 0 || h->mode == 2) {
    hitem_t *it = &hud[HI_CARD];
    it->on = true;
    it->x0 = CARD_X0; it->x1 = CARD_X1; it->y0 = CARD_Y0; it->y1 = CARD_Y1;
    if (h->mode == 0) {
      snprintf(card_l1, sizeof card_l1, "Best: %d", h->best);
    } else {
      snprintf(card_l1, sizeof card_l1, "Your score: %d", game_score());
      if (h->new_best) snprintf(card_l2, sizeof card_l2, "New best!");
      else snprintf(card_l2, sizeof card_l2, "Best: %d", h->best);
    }
    it->hash = mixh(mixh(mixh(5, (uint32_t)h->mode), (uint32_t)game_score()), (uint32_t)h->best * 2 + h->new_best);
  }
  if (h->show_fps) {
    snprintf(fps_s, sizeof fps_s, "%d fps", h->fps);
    hitem_t *it = &hud[HI_FPS];
    it->on = true;
    it->x0 = 2; it->x1 = 60; it->y0 = 3; it->y1 = 18;
    it->hash = mixh(6, (uint32_t)h->fps);
  }
}

static void hud_draw(void) {
  const uint16_t WHITE = 0xFFFF;
  if (hud[HI_SCORE].on && hud[HI_SCORE].y1 > ry0 && hud[HI_SCORE].y0 < ry1)
    text_c(&FONT_SCORE, score_s, 160, 36, 1, WHITE, 256, 2);
  if (hud[HI_BEST].on && hud[HI_BEST].y1 > ry0 && hud[HI_BEST].y0 < ry1)
    text_at(&FONT_SMALL, best_s, 312 - text_width(&FONT_SMALL, best_s), 15, 1, WHITE, 256, 1);
  if (hud[HI_INSTR].on && hud[HI_INSTR].y1 > ry0 && hud[HI_INSTR].y0 < ry1)
    text_c(&FONT_TEXT, "Press OK to place the block", 160, 196, 1, WHITE, (int)(HUD->instr_alpha * 256), 1);
  if (hud[HI_STREAK].on && hud[HI_STREAK].y1 > ry0 && hud[HI_STREAK].y0 < ry1) {
    /* scaled about the label's centre (cap height ~14 px) */
    float sc = st_scale, base = 120 - st_y + 7 * sc;
    text_c(&FONT_BIG, st_text, 160, base, sc, RGB(255, 215, 0), st_alpha, 1);
  }
  if (hud[HI_FPS].on && hud[HI_FPS].y1 > ry0 && hud[HI_FPS].y0 < ry1)
    text_at(&FONT_SMALL, fps_s, 4, 14, 1, WHITE, 256, 1);
  if (hud[HI_CARD].on && CARD_Y1 > ry0 && CARD_Y0 < ry1) {
    round_rect(CARD_X0, CARD_Y0, CARD_X1, CARD_Y1, 8, 0, 10);   /* rgba(0, 0, 0, 0.7) */
    const char *button, *hint;
    if (HUD->mode == 0) {
      text_c(&FONT_BIG, "Infinite Stacker", 160, CARD_Y0 + 30, 1, WHITE, 256, 1);
      text_c(&FONT_TEXT, card_l1, 160, CARD_Y0 + 54, 1, WHITE, 256, 1);
      button = "Play";
      hint = "OK: play    Back: quit";
    } else {
      text_c(&FONT_BIG, "Game Over", 160, CARD_Y0 + 28, 1, WHITE, 256, 1);
      text_c(&FONT_TEXT, card_l1, 160, CARD_Y0 + 48, 1, WHITE, 256, 1);
      if (HUD->new_best) text_c(&FONT_TEXT, card_l2, 160, CARD_Y0 + 65, 1, RGB(76, 175, 80), 256, 1);
      else text_c(&FONT_TEXT, card_l2, 160, CARD_Y0 + 65, 1, WHITE, 256, 1);
      button = "Restart";
      hint = "OK: restart    Back: menu";
    }
    int bw = text_width(&FONT_TEXT, button) + 34;
    round_rect(160 - bw / 2, CARD_Y0 + 75, 160 + bw / 2, CARD_Y0 + 101, 4, RGB(33, 150, 243), 0);   /* #2196F3 */
    text_c(&FONT_TEXT, button, 160, CARD_Y0 + 93, 1, WHITE, 256, 1);
    text_c(&FONT_SMALL, hint, 160, CARD_Y0 + 117, 1, WHITE, 180, 1);
  }
}

/* ------------------------------------------------------------------ frame */
static int span0[NB], span1[NB];       /* dirty columns per band this frame */
static int dyn0[NB], dyn1[NB];         /* where moving things are this frame */
static int dyn0p[NB], dyn1p[NB];       /* ... and were last frame */
static bool full;
static uint32_t version_seen;
static int roll;

static void mark_into(int *a0, int *a1, int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > W) x1 = W;
  if (y1 > H) y1 = H;
  if (x0 >= x1 || y0 >= y1) return;
  for (int b = y0 / BAND; b <= (y1 - 1) / BAND; b++) {
    if (x0 < a0[b]) a0[b] = x0;
    if (x1 > a1[b]) a1[b] = x1;
  }
}
static void mark(int x0, int y0, int x1, int y1) { mark_into(span0, span1, x0, y0, x1, y1); }

void render_init(void) {
  for (int i = 0; i <= 1024; i++) {
    float v = i / 1024.0f;
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f;
    lin2srgb[i] = (uint8_t)(s * 255 + 0.5f);
  }
  /* the sky shader writes linear colours straight out, so they show darker than their hex values */
  const uint32_t top = 0x73b3e8, bot = 0xd0e7f9;
  for (int y = 0; y < H; y++) {
    float v = 1 - (y + 0.5f) / H;
    for (int c = 0; c < 3; c++) {
      float a = srgb2lin(((bot >> (16 - 8 * c)) & 255) / 255.0f), b = srgb2lin(((top >> (16 - 8 * c)) & 255) / 255.0f);
      sky_v[y][c] = (uint16_t)((a + (b - a) * v) * 4080 + 0.5f);
    }
  }
  cloud_next = -1000000;
  full = true;
  cam_valid = false;
}

void render_invalidate(void) { full = true; }

static void add_dyn(const pbox_t *b) {
  if (ndyn >= MAX_DYN) return;
  if (b->x1 <= 0 || b->x0 >= W || b->y1 <= 0 || b->y0 >= H) return;
  if (b->c[2] < 1 || b->c[2] > 100) return;   /* the camera's near and far planes */
  dyn[ndyn++] = *b;
  mark_into(dyn0, dyn1, b->x0, b->y0, b->x1, b->y1);
}

static void build_dyn(const cam_t *c) {
  ndyn = 0;
  pbox_t b;
  if (G.active) {
    pbox_aligned(c, G.ax, BLOCK_H * G.n, G.az, G.aw / 2, BLOCK_H / 2, G.ad / 2, G.acol, NULL, &b);
    add_dyn(&b);
  }
  for (int i = 0; i < MAX_BODIES; i++) {
    const body_t *o = &G.bodies[i];
    if (!o->used) continue;
    float cs = cosf(o->ang), sn = sinf(o->ang), ctr[3], ax[3][3], un[3][3];
    if (o->axis == 0) {
      float u0[3] = {cs, sn, 0}, u1[3] = {-sn, cs, 0}, u2[3] = {0, 0, 1};
      memcpy(un[0], u0, sizeof u0); memcpy(un[1], u1, sizeof u1); memcpy(un[2], u2, sizeof u2);
      ctr[0] = o->u; ctr[1] = o->y; ctr[2] = o->w;
    } else {
      float u0[3] = {0, sn, cs}, u1[3] = {0, cs, -sn}, u2[3] = {1, 0, 0};
      memcpy(un[0], u0, sizeof u0); memcpy(un[1], u1, sizeof u1); memcpy(un[2], u2, sizeof u2);
      ctr[0] = o->w; ctr[1] = o->y; ctr[2] = o->u;
    }
    float h[3] = {o->hu, o->hy, o->hw};
    for (int k = 0; k < 3; k++)
      for (int j = 0; j < 3; j++) ax[k][j] = un[k][j] * h[k];
    pbox_make(c, ctr, ax, un, o->col, NULL, &b);
    add_dyn(&b);
  }
  for (int i = 0; i < MAX_PARTS; i++) {
    const part_t *q = &G.parts[i];
    if (q->life <= 0) continue;
    float s = q->size / 2;
    pbox_aligned(c, q->p[0], q->p[1], q->p[2], s, s, s, q->col, NULL, &b);
    add_dyn(&b);
  }
  static float scol[3], semi[3];
  if (scol[0] == 0) {   /* 0xfff3b0 lit, plus 0xffd700 glow */
    scol[0] = 1; scol[1] = srgb2lin(0xf3 / 255.0f); scol[2] = srgb2lin(0xb0 / 255.0f);
    semi[0] = 1; semi[1] = srgb2lin(0xd7 / 255.0f); semi[2] = 0;
  }
  for (int k = 0; k < 2; k++) {
    const spark_t *pool = k ? G.mspark : G.spark;
    int n = k ? N_MSPARK : N_SPARK;
    float size = k ? 0.07f : 0.055f;
    for (int i = 0; i < n; i++) {
      const spark_t *s = &pool[i];
      if (s->life <= 0) continue;
      float h = size * s->base * s->life / s->maxlife / 2;
      pbox_aligned(c, s->p[0], s->p[1], s->p[2], h, h, h, scol, semi, &b);
      add_dyn(&b);
    }
  }
  /* blocks that flash */
  for (int i = G.n - 1; i >= 1 && i >= G.n - 8; i--) {
    const block_t *k = blk(i);
    if (k->flash_at >= 0 && G.now - k->flash_at < 400 + 40) {
      int x0, y0, x1, y1;
      layer_bounds(c, i, &x0, &y0, &x1, &y1);
      mark_into(dyn0, dyn1, x0, y0, x1, y1);
    }
  }
}

static void draw_region(int band, int x0, int x1) {
  rx0 = x0; rx1 = x1; ry0 = band * BAND; ry1 = ry0 + BAND; rw = x1 - x0;
  draw_sky(band);
  for (int i = 0; i < BAND * rw; i++) zbuf[i] = FLT_MAX;
  pbox_t pb;
  for (int i = vis_lo; i <= vis_hi; i++) {
    int bx0, by0, bx1, by1;
    layer_bounds(&cam, i, &bx0, &by0, &bx1, &by1);
    if (bx1 <= rx0 || bx0 >= rx1 || by1 <= ry0 || by0 >= ry1) continue;
    layer_pbox(&cam, i, &pb);
    draw_pbox(&pb);
  }
  for (int i = 0; i < ndyn; i++) draw_pbox(&dyn[i]);
  hud_draw();
  eadk_display_push_rect((eadk_rect_t){(uint16_t)rx0, (uint16_t)ry0, (uint16_t)rw, BAND}, sbuf);
}

int render_frame(const hud_t *h) {
  HUD = h;
  for (int b = 0; b < NB; b++) {
    span0[b] = W; span1[b] = 0;
    dyn0[b] = W; dyn1[b] = 0;
  }
  /* camera: only moved when it moves the picture by a third of a pixel */
  cam_t nc;
  cam_make(&nc, G.camY, G.lookY, G.top);
  bool moved = !cam_valid || fabsf(nc.camY - cam.camY) * nc.S > 0.3f || fabsf(nc.lookY - cam.lookY) * nc.S > 0.3f ||
               fabsf(nc.top - cam.top) > 1e-4f;
  if (moved) {
    cam_prev = cam;
    cam = nc;
    cam_valid = true;
  }
  find_visible_layers(&cam);
  if (moved) {
    mark(tower_box_prev[0], tower_box_prev[1], tower_box_prev[2], tower_box_prev[3]);
    mark(tower_box[0], tower_box[1], tower_box[2], tower_box[3]);
  }
  memcpy(tower_box_prev, tower_box, sizeof tower_box);
  if (G.version != version_seen) {   /* a block was added or grew */
    version_seen = G.version;
    for (int i = G.n - 1; i >= 0 && i >= G.n - 2; i--) {
      int x0, y0, x1, y1;
      layer_bounds(&cam, i, &x0, &y0, &x1, &y1);
      mark(x0, y0, x1, y1);
    }
  }
  build_dyn(&cam);
  for (int b = 0; b < NB; b++) {
    if (dyn0[b] < span0[b]) span0[b] = dyn0[b];
    if (dyn1[b] > span1[b]) span1[b] = dyn1[b];
    if (dyn0p[b] < span0[b]) span0[b] = dyn0p[b];
    if (dyn1p[b] > span1[b]) span1[b] = dyn1p[b];
  }
  memcpy(dyn0p, dyn0, sizeof dyn0);
  memcpy(dyn1p, dyn1, sizeof dyn1);

  hud_layout(h);
  for (int i = 0; i < NHUD; i++) {
    if (hud[i].on != hud_prev[i].on || hud[i].hash != hud_prev[i].hash ||
        (hud[i].on && (hud[i].x0 != hud_prev[i].x0 || hud[i].x1 != hud_prev[i].x1))) {
      if (hud_prev[i].on) mark(hud_prev[i].x0, hud_prev[i].y0, hud_prev[i].x1, hud_prev[i].y1);
      if (hud[i].on) mark(hud[i].x0, hud[i].y0, hud[i].x1, hud[i].y1);
    }
  }
  memcpy(hud_prev, hud, sizeof hud);

  /* clouds: bands catch up with the scroll two at a time */
  int32_t sc = sky_scroll(G.now);
  sky_advance(sc);
  if (full) {
    for (int b = 0; b < NB; b++) {
      band_scroll[b] = sc;
      span0[b] = 0; span1[b] = W;
    }
    full = false;
  } else {
    int done = 0;
    for (int k = 0; k < NB && done < 2; k++) {
      int b = (roll + k) % NB;
      if (band_scroll[b] != sc) {
        band_scroll[b] = sc;
        span0[b] = 0; span1[b] = W;
        done++;
        roll = (b + 1) % NB;
      }
    }
  }
  int pushed = 0;
  for (int b = 0; b < NB; b++) {
    if (span1[b] <= span0[b]) continue;
    int x0 = span0[b], x1 = span1[b];
    if (x1 - x0 > W - 40) { x0 = 0; x1 = W; }
    draw_region(b, x0, x1);
    pushed += (x1 - x0) * BAND;
  }
  return pushed;
}
