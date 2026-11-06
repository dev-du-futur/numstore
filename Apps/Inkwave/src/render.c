/* Software 3D, drawn straight to the screen in ten vertical strips of 32 pixels (no full frame buffer).
 * Terrain: one ray per render column, stepped front to back through the stage's 0.5 m columns. Each column
 * draws its front face (stucco, stone courses, windows) and its top (brick, terrazzo, ink, sun shadow, contact
 * shadow), then the sea, distant hills, clouds and the sun fill what is left.
 * Kids, shots, bombs and droplets are billboards against a per-strip depth buffer; the HUD goes on top.
 * Three resolutions: 160x120 (each pixel drawn 2x2), 160x240 (2x1) and the screen's own 320x240. */
#include <eadk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

const char *const QUALITY_NAME[3] = {"Fast", "Balanced", "Full"};

/* ------------------------------------------------------------------ resolution, strips */
#define STRIP_PX 32                      /* screen pixels per strip */
static int RWm = 160, RHm = 120, KX = 2, KY = 2, SC = 16;   /* render size, screen px per render px, render columns per strip */
static float FOC = 100, TMAX = 28, TEXD = 6, FOG0 = 14, FOGK = 32.0f / 14;
static int quality = Q_FAST;
static uint16_t sbuf[240 * STRIP_PX];   /* the strip being drawn, render resolution, row-major SC wide */
static uint8_t sdep[240 * STRIP_PX];    /* its depth: metres x 8 */
static int c0, c1;                       /* render columns of the current strip */
int render_debug;

void render_set_quality(int q) {
  quality = q;
  KX = q == Q_FULL ? 1 : 2;
  KY = q == Q_FAST ? 2 : 1;
  RWm = 320 / KX;
  RHm = 240 / KY;
  SC = STRIP_PX / KX;
  FOC = 100.0f * RWm / 160;              /* the same 77 degree view at every size */
  TMAX = q == Q_FULL ? 30 : q == Q_BALANCED ? 30 : 28;
  TEXD = q == Q_FULL ? 14 : q == Q_BALANCED ? 9 : 6;
  FOG0 = TMAX * 0.5f;
  FOGK = 32.0f / (TMAX - FOG0);
}

/* ------------------------------------------------------------------ colours (RGB565) */
#define SKY_TOP RGB(78, 140, 225)
#define SKY_LOW RGB(200, 226, 246)
#define FOG_C RGB(196, 220, 238)
#define SEA_NEAR RGB(28, 104, 150)
#define SEA_FAR RGB(90, 160, 200)
#define FOAM RGB(235, 245, 250)
#define HILLS RGB(126, 156, 166)
#define SKIN RGB(246, 196, 160)
#define SHIRT RGB(44, 48, 66)
#define PANTS RGB(70, 74, 92)
#define SHOE RGB(240, 240, 240)
#define GUN RGB(96, 100, 112)
#define EYE RGB(26, 22, 40)
#define GLASS RGB(60, 86, 120)
#define GLASS_HI RGB(150, 186, 220)

static inline uint16_t mix(uint16_t a, uint16_t b, int k) {   /* k 0..32: share of b */
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81Fu, B = (b | (uint32_t)b << 16) & 0x07E0F81Fu;
  uint32_t r = (A + (((B - A) * (uint32_t)k) >> 5)) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint16_t dim(uint16_t c, int k) {   /* k 0..32 brightness */
  if (k >= 32) return c;
  if (k < 0) k = 0;
  uint32_t A = (c | (uint32_t)c << 16) & 0x07E0F81Fu;
  uint32_t r = ((A * (uint32_t)k) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint8_t dz(float t) { int d = (int)(t * 8); return (uint8_t)(d > 255 ? 255 : d < 0 ? 0 : d); }
static inline uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static inline float fracf(float v) { return v - floorf(v); }

/* ------------------------------------------------------------------ strip pixels */
static inline void P(int x, int y, uint16_t c) {
  if (x >= c0 && x < c1 && (unsigned)y < (unsigned)RHm) sbuf[y * SC + x - c0] = c;
}
static inline void PD(int x, int y, uint16_t c, uint8_t d) {
  if (x >= c0 && x < c1 && (unsigned)y < (unsigned)RHm) {
    int i = y * SC + x - c0;
    if (d < sdep[i]) sbuf[i] = c;
  }
}
static inline void PDIM(int x, int y, int k, uint8_t d) {   /* darken (shadows) */
  if (x >= c0 && x < c1 && (unsigned)y < (unsigned)RHm) {
    int i = y * SC + x - c0;
    if (d < sdep[i]) sbuf[i] = dim(sbuf[i], k);
  }
}

/* ------------------------------------------------------------------ camera, sky */
typedef struct { float x, y, z, fx, fy, rx, ry, hz; } cam_t;
static cam_t C;
static float col_rx[320], col_ry[320];
static uint8_t col_az[320];              /* the ray's compass direction, 0..255 */
static uint16_t sky_row[240];
static float row_e[240];                 /* tangent of each row's elevation */
static float row_inv[240];               /* FOC / (rows below the horizon), 0 above it */
/* the sky, split into per-column and per-row parts so a sky pixel is a few table reads */
static uint8_t col_hill[320], col_cloud[320];
static float col_sun[320], row_sun[240];
static int16_t row_e400[240];            /* elevation x 400, or -1 below the horizon */
static int8_t row_cloud[240];            /* cloud table row, or -1 */
static uint8_t clouds[32][128];          /* 0..26 cover, by elevation and direction */
static float sint[256];                  /* sine table (the sea is drawn per pixel) */
#define FSIN(a) sint[(int)((a) * 40.743665f) & 255]
static uint8_t hills[256];               /* skyline height, in 1/400 radian */
static int sun_az;
static float sun_el;
static float sun_hx, sun_hy;             /* the sun's horizontal direction */

void render_init(void) {
  /* value noise for the clouds: two octaves on a wrapped lattice */
  for (int i = 0; i < 256; i++) sint[i] = sinf(i * (2 * PI_F / 256));
  uint8_t lat[2][16][32];
  for (int o = 0; o < 2; o++)
    for (int j = 0; j < 16; j++)
      for (int i = 0; i < 32; i++) lat[o][j][i] = (uint8_t)(hash2(i + o * 31, j + o * 57) & 255);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 128; x++) {
      float v = 0;
      for (int o = 0; o < 2; o++) {
        int cell = o ? 8 : 16;
        float fx = (float)x / cell, fy = (float)y / (o ? 4 : 8);
        int ix = (int)fx, iy = (int)fy;
        float ax = fx - ix, ay = fy - iy;
        int w = 128 / cell, h = o ? 8 : 4;
        float a = lat[o][iy % h][ix % w], b = lat[o][iy % h][(ix + 1) % w];
        float c = lat[o][(iy + 1) % h][ix % w], d = lat[o][(iy + 1) % h][(ix + 1) % w];
        ax = ax * ax * (3 - 2 * ax);
        ay = ay * ay * (3 - 2 * ay);
        v += ((a + (b - a) * ax) * (1 - ay) + (c + (d - c) * ax) * ay) * (o ? 0.35f : 0.65f);
      }
      float k = (v - 120) / 60;   /* threshold: patches of cloud */
      clouds[y][x] = (uint8_t)(k <= 0 ? 0 : k >= 1 ? 26 : k * 26);
    }
  for (int a = 0; a < 256; a++) {
    float t = a * (2 * PI_F / 256);
    float h = 6 + 5 * sinf(t * 3 + 1) + 3 * sinf(t * 7 + 2) + 2 * sinf(t * 13);
    hills[a] = (uint8_t)(h < 1 ? 1 : h);
  }
  float hl = sqrtf(SUN[0] * SUN[0] + SUN[1] * SUN[1]);
  sun_hx = SUN[0] / hl;
  sun_hy = SUN[1] / hl;
  sun_az = (int)(atan2f(SUN[1], SUN[0]) * (256 / (2 * PI_F))) & 255;
  sun_el = SUN[2] / hl;   /* tangent of the sun's elevation */
  render_set_quality(quality);
}

static void frame_rays(void) {
  for (int c = 0; c < RWm; c++) {
    float k = (c + 0.5f - RWm / 2) / FOC;
    col_rx[c] = C.fx + C.rx * k;
    col_ry[c] = C.fy + C.ry * k;
    int az = (int)(atan2f(col_ry[c], col_rx[c]) * (256 / (2 * PI_F))) & 255;
    col_az[c] = (uint8_t)az;
    col_hill[c] = hills[az];
    int da = (az - sun_az) & 255;
    if (da > 128) da -= 256;
    col_sun[c] = (da * 0.0245f) * (da * 0.0245f);
    col_cloud[c] = (uint8_t)((az * 2 + (int)(G.clock * 0.6f)) & 127);
  }
  for (int r = 0; r < RHm; r++) {
    float e = (C.hz - r - 0.5f) / FOC;   /* tangent of the elevation */
    row_e[r] = e;
    row_inv[r] = e < 0 ? -1 / e : 0;     /* = FOC / (r + 0.5 - hz) */
    row_e400[r] = (int16_t)(e < 0 ? -1 : e * 400 > 30000 ? 30000 : (int)(e * 400));
    row_sun[r] = (e - sun_el) * (e - sun_el);
    row_cloud[r] = (int8_t)(e > 0.02f && e < 0.7f ? (int)(e * 45.7f) : -1);
    int k = e <= 0 ? 32 : e > 0.45f ? 0 : (int)(32 - e * 71);
    sky_row[r] = mix(SKY_TOP, SKY_LOW, k < 0 ? 0 : k);
  }
}

/* the sky above row n of a column: the sea to the horizon, the far shore, the sun and clouds */
static void sky_fill(int c, uint16_t *col, uint8_t *dep, int n, int sc) {
  if (render_debug & RD_NOSKY) {
    for (int r = 0; r < n; r++) { col[r * sc] = sky_row[r]; dep[r * sc] = 255; }
    return;
  }
  const int hill = col_hill[c];
  const float csun = col_sun[c];
  const uint8_t *cl = &clouds[0][col_cloud[c]];
  const int16_t *e400 = row_e400;
  const float *rsun = row_sun;
  const int8_t *rcl = row_cloud;
  const uint16_t *srow = sky_row;
  for (int r = 0; r < n; r++) {
    int e4 = e400[r];
    uint16_t s;
    if (e4 < 0) s = mix(SEA_FAR, FOG_C, 20);
    else if (e4 < hill) s = mix(HILLS, FOG_C, 8 + e4);
    else {
      s = srow[r];
      float d2 = csun + rsun[r];
      if (d2 < 0.03f) s = d2 < 0.0016f ? RGB(255, 252, 230) : mix(s, RGB(255, 246, 214), (int)(14 - d2 * 460));
      int cr = rcl[r];
      if (cr >= 0) {
        int cv = cl[cr * 128];
        if (cv) s = mix(s, e4 < 48 ? RGB(236, 240, 246) : RGB(255, 255, 255), cv);
      }
    }
    col[r * sc] = s;
    dep[r * sc] = 255;
  }
}

/* ------------------------------------------------------------------ surfaces */
static uint16_t top_texture(int surf, uint16_t base, float wx, float wy) {
  switch (surf) {
    case 1: {   /* herringbone-ish brick paving: 0.24 m squares of two bricks, turning each square */
      float X = wx * 4.1667f, Y = wy * 4.1667f;
      int ix = (int)floorf(X), iy = (int)floorf(Y);
      float fx = X - ix, fy = Y - iy;
      bool turn = (ix + iy) & 1;
      bool joint = fx < 0.07f || fy < 0.07f || (turn ? fabsf(fy - 0.5f) < 0.04f : fabsf(fx - 0.5f) < 0.04f);
      if (joint) return dim(base, 23);
      int half = turn ? fy >= 0.5f : fx >= 0.5f;
      return dim(base, 28 + (hash2(ix * 2 + half, iy) & 3));
    }
    case 2: {   /* terrazzo: 1.2 m panels framed by brass strips, stone chips */
      float fx = fracf(wx * 0.8333f), fy = fracf(wy * 0.8333f);
      if (fx < 0.025f || fy < 0.025f) return RGB(198, 170, 110);
      uint32_t h = hash2((int)floorf(wx * 20), (int)floorf(wy * 20));
      if ((h & 15) == 0) return dim(base, 18);
      if ((h & 15) == 1) return mix(base, 0xFFFF, 16);
      if ((h & 31) == 2) return mix(base, RGB(240, 140, 120), 12);
      return base;
    }
    case 3: {   /* stone steps: a worn nosing line at each 0.5 m */
      float fx = fracf(wx * 2), fy = fracf(wy * 2);
      if (fx < 0.08f || fy < 0.08f) return dim(base, 24);
      return dim(base, 29 + (hash2((int)floorf(wx * 6), (int)floorf(wy * 6)) & 3));
    }
    default: {
      uint32_t h = hash2((int)floorf(wx * 4), (int)floorf(wy * 4));
      return dim(base, 30 + (h & 2));
    }
  }
}

static inline uint16_t water_px(float wx, float wy, float t, int light, int ix, int iy, bool glint, float ph1, float ph2,
                                float ph3, float fog0, float fogk) {
  if (render_debug & RD_NOWATER) return SEA_NEAR;
  uint16_t c = mix(SEA_NEAR, SEA_FAR, t > 24 ? 32 : (int)(t * 1.33f));
  /* waves: moving bands */
  float w = FSIN(wx * 1.7f + wy * 0.9f + ph1) + FSIN(wx * 0.6f - wy * 1.9f + ph2);
  if (w > 1.3f) c = mix(c, SEA_FAR, 10);
  else if (w < -1.3f) c = dim(c, 27);
  /* sun glint, looking towards the sun */
  if (glint && w > 0.9f && (hash2((int)(wx * 8), (int)(wy * 8)) & 3) == 0) c = 0xFFFF;
  /* foam along the sea wall */
  if (light & 0xF0) {
    float fx = wx * 2 - ix, fy = wy * 2 - iy;
    float e = 1;
    if ((light & 16) && fy < e) e = fy;
    if ((light & 32) && 1 - fy < e) e = 1 - fy;
    if ((light & 64) && fx < e) e = fx;
    if ((light & 128) && 1 - fx < e) e = 1 - fx;
    float foam = 0.35f + 0.15f * FSIN(ph3 + wx * 3 + wy * 2);
    if (e < foam && (e < foam * 0.5f || (hash2((int)(wx * 10), (int)(wy * 10)) & 1))) c = FOAM;
  }
  if (t > fog0) { int k = (int)((t - fog0) * fogk); c = mix(c, FOG_C, k > 32 ? 32 : k); }
  return c;
}

/* ------------------------------------------------------------------ terrain: one column of pixels */
static void draw_column(int c) {
  /* the globals the loops use, copied to locals: the depth buffer is written through a byte pointer, which may
   * alias anything, so the compiler would otherwise reload every global after each pixel */
  const int sc = SC, rhm = RHm;
  const float cx = C.x, cy = C.y, foc = FOC, texd = TEXD, tmax = TMAX, fog0 = FOG0, fogk = FOGK;
  const uint16_t tc[2] = {G.col[0], G.col[1]}, td[2] = {G.col_dark[0], G.col_dark[1]};
  const bool notex = render_debug & RD_NOTEX;
  const float ph1 = G.clock * 1.6f + 64, ph2 = -G.clock * 1.1f + 64, ph3 = G.clock * 2 + 64;
  float rx = col_rx[c], ry = col_ry[c];
  float px = cx * 2, py = cy * 2, dx = rx * 2, dy = ry * 2;   /* in 0.5 m columns */
  int ix = (int)floorf(px), iy = (int)floorf(py);
  int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
  float ddx = dx != 0 ? fabsf(1 / dx) : 1e9f, ddy = dy != 0 ? fabsf(1 / dy) : 1e9f;
  float tx = (dx < 0 ? px - ix : ix + 1 - px) * ddx, ty = (dy < 0 ? py - iy : iy + 1 - py) * ddy;
  float t0 = 0, cz = C.z, hz = C.hz, near = 0.12f;
  int side = 0, ybuf = rhm, x = c - c0;
  uint16_t *col = sbuf + x;
  uint8_t *dep = sdep + x;
  /* the faces' light: how square each face is to the sun */
  float lit_x = -sx * sun_hx, lit_y = -sy * sun_hy;
  int kx_face = 17 + (int)(13 * (lit_x > 0 ? lit_x : 0)), ky_face = 17 + (int)(13 * (lit_y > 0 ? lit_y : 0));
  float spx[2], spy[2];
  spawn_point(0, &spx[0], &spy[0]);
  spawn_point(1, &spx[1], &spy[1]);
  bool glint = (rx * sun_hx + ry * sun_hy) > 0.93f * sqrtf(rx * rx + ry * ry);
  while (ybuf > 0 && t0 < tmax) {
    /* out past the stage and heading away: only flat sea is left */
    if ((ix < 0 && sx < 0) || (ix >= 188 && sx > 0) || (iy < 0 && sy < 0) || (iy >= 112 && sy > 0)) {
      float h = cz + 1.6f;
      for (int r = ybuf - 1; r >= 0; r--) {
        float k = r + 0.5f - hz;
        if (k <= 0) break;
        float t = h * row_inv[r];
        if (t > tmax) break;
        float wx = cx + rx * t, wy = cy + ry * t;
        col[r * sc] = water_px(wx, wy, t, 0, 0, 0, glint, ph1, ph2, ph3, fog0, fogk);
        dep[r * sc] = dz(t);
        ybuf = r;
      }
      break;
    }
    float t1 = tx < ty ? tx : ty;
    int kind = col_kind(ix, iy);
    float H = col_top(ix, iy);
    if (t1 > near) {
      float tn = t0 > near ? t0 : near;
      float yf = hz - (H - cz) * foc / tn, yb = hz - (H - cz) * foc / t1;
      int rf = yf < 0 ? 0 : yf >= rhm ? rhm : (int)ceilf(yf);
      int rb = yb < 0 ? 0 : yb >= rhm ? rhm : (int)ceilf(yb);
      uint16_t base = kind == K_WATER ? SEA_NEAR : map_colour(col_colour(ix, iy));
      /* front face */
      if (t0 > near && rf < ybuf) {
        uint8_t d = dz(tn);
        int kf = side ? ky_face : kx_face;
        uint16_t fc = dim(base, kind == K_WALL ? kf - 2 : kf);
        int fk = tn <= fog0 ? 0 : (int)((tn - fog0) * fogk);
        if (fk > 32) fk = 32;
        float u = side ? (cx + rx * tn) : (cy + ry * tn);   /* along the face, metres */
        float zstep = tn / foc, z = cz + (hz - rf - 0.5f) * zstep;
        bool near_tex = tn < texd * 1.6f;
        if (kind == K_WALL) {
          /* stucco lined out as ashlar (0.4 m courses, 1.2 m blocks), windows in 2.4 m bays */
          float bay = fracf(u * 0.4167f);
          bool wcol = bay > 0.3f && bay < 0.7f;
          for (int r = rf; r < ybuf; r++, z -= zstep) {
            uint16_t pc = fc;
            float st = fracf((z + 2.0f) * 0.3125f);   /* 3.2 m storeys */
            if (wcol && st > 0.28f && st < 0.75f && z > 0.6f) {
              pc = st > 0.68f ? mix(GLASS, GLASS_HI, 20) : (bay < 0.33f || bay > 0.67f) ? dim(0xFFFF, 26) : GLASS;
            } else if (near_tex) {
              float zc = (z + 2.0f) * 2.5f;
              int course = (int)floorf(zc);
              if (zc - course < 0.08f || fracf(u * 0.8333f + (course & 1) * 0.5f) < 0.03f) pc = dim(fc, 26);
            }
            col[r * sc] = fk ? mix(pc, FOG_C, fk) : pc;
            dep[r * sc] = d;
          }
        } else {
          for (int r = rf; r < ybuf; r++, z -= zstep) {
            uint16_t pc = fc;
            if (near_tex && fracf(z * 4) < 0.12f) pc = dim(fc, 27);   /* stone courses */
            col[r * sc] = fk ? mix(pc, FOG_C, fk) : pc;
            dep[r * sc] = d;
          }
        }
      }
      /* top, seen from above */
      int top_end = rf < ybuf ? rf : ybuf;
      if (rb < top_end && H < cz) {
        float hgt = cz - H;
        int light = col_light(ix, iy), surf = col_surface(ix, iy);
        /* the spawn pads' rings, only on columns near a pad */
        int rings = 0;
        for (int s = 0; s < 2; s++)
          if (fabsf(ix * 0.5f + 0.25f - spx[s]) < 2.4f && fabsf(iy * 0.5f + 0.25f - spy[s]) < 2.4f) rings |= 1 << s;
        for (int r = rb; r < top_end; r++) {
          float t = hgt * row_inv[r];
          float wx = cx + rx * t, wy = cy + ry * t;
          uint16_t pc;
          if (kind == K_WATER) pc = water_px(wx, wy, t, light, ix, iy, glint, ph1, ph2, ph3, fog0, fogk);
          else {
            float fx = wx * 2 - ix, fy = wy * 2 - iy;
            if (kind == K_WALL) {   /* roofs: rows of tiles */
              pc = dim(base, 19);
              if (t < texd && fracf(wy * 3.3f) < 0.18f) pc = dim(base, 15);
            } else {
              int qx = (int)floorf(wx * 4), qy = (int)floorf(wy * 4), p = paint_get(qx, qy);
              if (p) {
                uint32_t h = hash2(qx, qy);
                pc = (h & 3) ? tc[p - 1] : td[p - 1];
                if (t < texd && (h & 60) == 0) pc = mix(pc, 0xFFFF, 14);   /* wet shine */
              } else {
                pc = t < texd && !notex ? top_texture(surf, base, wx, wy) : base;
                if (kind == K_RAIL && ((qx + qy) & 1)) pc = RGB(56, 70, 66);   /* iron railing */
                if (rings) for (int s = 0; s < 2; s++) {
                  if (!(rings >> s & 1)) continue;
                  float ex = wx - spx[s], ey = wy - spy[s], e2 = ex * ex + ey * ey;
                  if (e2 > 3.2f && e2 < 4.4f) pc = mix(pc, tc[s], 20);
                }
              }
              /* contact shadow along taller neighbours */
              int k = 32;
              if (light & 0xF0) {
                if (((light & 16) && fy < 0.22f) || ((light & 32) && fy > 0.78f) ||
                    ((light & 64) && fx < 0.22f) || ((light & 128) && fx > 0.78f)) k = 25;
              }
              if (light & 15) {
                int q = (fx >= 0.5f) + 2 * (fy >= 0.5f);
                if (light >> q & 1) k -= 12;   /* in the sun's shadow */
              }
              pc = dim(pc, k);
            }
            if (kind == K_WALL && (light & 15)) {
              int q = (fx >= 0.5f) + 2 * (fy >= 0.5f);
              if (light >> q & 1) pc = dim(pc, 22);
            }
            if (t > fog0) { int k = (int)((t - fog0) * fogk); pc = mix(pc, FOG_C, k > 32 ? 32 : k); }
          }
          col[r * sc] = pc;
          dep[r * sc] = dz(t);
        }
      }
      int top = rb < rf ? rb : rf;
      if (top < ybuf) ybuf = top;
    }
    if (tx < ty) { ix += sx; t0 = tx; tx += ddx; side = 0; }
    else { iy += sy; t0 = ty; ty += ddy; side = 1; }
  }
  sky_fill(c, col, dep, ybuf, sc);
}

/* ------------------------------------------------------------------ billboards */
static bool project(float x, float y, float z, float *sx, float *sy, float *t) {
  float ox = x - C.x, oy = y - C.y;
  float d = ox * C.fx + oy * C.fy;
  if (d < 0.3f) return false;
  float l = ox * C.rx + oy * C.ry;
  *sx = RWm / 2 + l * FOC / d;
  *sy = C.hz - (z - C.z) * FOC / d;
  *t = d;
  return true;
}

/* a sprite's frame: screen point of its feet, px per metre, depth, mirror */
static float SPX, SPY, SPS, SPU;
static uint8_t SPD;
static void sbox(float u0, float v0, float u1, float v1, uint16_t c) {
  if (SPU < 0) { float t = -u0; u0 = -u1; u1 = t; }
  int x0 = (int)floorf(SPX + u0 * SPS + 0.5f), x1 = (int)floorf(SPX + u1 * SPS + 0.5f);
  int y0 = (int)floorf(SPY - v1 * SPS + 0.5f), y1 = (int)floorf(SPY - v0 * SPS + 0.5f);
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;
  if (x0 < c0) x0 = c0;
  if (x1 > c1) x1 = c1;
  if (y0 < 0) y0 = 0;
  if (y1 > RHm) y1 = RHm;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      int i = y * SC + x - c0;
      if (SPD < sdep[i]) sbuf[i] = c;
    }
}
static void sell(float uc, float vc, float ru, float rv, uint16_t c) {
  if (SPU < 0) uc = -uc;
  float cx = SPX + uc * SPS, cy = SPY - vc * SPS, ax = ru * SPS, ay = rv * SPS;
  if (ax < 0.5f) ax = 0.5f;
  if (ay < 0.5f) ay = 0.5f;
  int x0 = (int)floorf(cx - ax), x1 = (int)ceilf(cx + ax), y0 = (int)floorf(cy - ay), y1 = (int)ceilf(cy + ay);
  if (x0 < c0) x0 = c0;
  if (x1 > c1) x1 = c1;
  if (y0 < 0) y0 = 0;
  if (y1 > RHm) y1 = RHm;
  for (int y = y0; y < y1; y++) {
    float ey = (y + 0.5f - cy) / ay;
    for (int x = x0; x < x1; x++) {
      float ex = (x + 0.5f - cx) / ax;
      if (ex * ex + ey * ey <= 1) {
        int i = y * SC + x - c0;
        if (SPD < sdep[i]) sbuf[i] = c;
      }
    }
  }
}
static void sshadow(float r) {   /* a soft dark patch on the ground under it */
  float cx = SPX, cy = SPY, ax = r * SPS, ay = r * SPS * 0.3f + 0.5f;
  int x0 = (int)floorf(cx - ax), x1 = (int)ceilf(cx + ax), y0 = (int)floorf(cy - ay), y1 = (int)ceilf(cy + ay);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      float ex = (x + 0.5f - cx) / ax, ey = (y + 0.5f - cy) / ay, e = ex * ex + ey * ey;
      if (e <= 1) PDIM(x, y, e < 0.5f ? 18 : 24, SPD > 4 ? SPD - 4 : 0);
    }
}

typedef struct { float sx, sy, s, t; int kind, i; } spr_t;
enum { S_ACTOR, S_PROJ, S_PART, S_BLAST };
static spr_t sprites[N_ACTORS + MAX_PROJ + MAX_PART + MAX_BLAST];
static int n_spr;

static void draw_kid(const actor_t *a, const spr_t *s) {
  SPX = s->sx; SPY = s->sy; SPS = s->s; SPD = dz(s->t - 0.25f); SPU = 1;
  uint16_t tc = a->flash > 0 ? 0xFFFF : G.col[a->team], td = G.col_dark[a->team];
  if (a->swim) {   /* hidden in its ink: a ripple, for teammates (and a foe close by or swimming fast) */
    float sp = sqrtf(a->vx * a->vx + a->vy * a->vy);
    if (a->team != G.a[0].team && !G.demo && !(s->t < 3.5f || (sp > 7 && s->t < 9))) return;
    float ph = fracf(G.clock * 1.5f + a->idx * 0.3f);
    uint16_t rc = mix(G.col[a->team], 0xFFFF, 14);
    float r = 0.2f + 0.35f * ph;
    for (int k = 0; k < 24; k++) {
      float an = k * (2 * PI_F / 24);
      PD((int)(SPX + cosf(an) * r * SPS), (int)(SPY + sinf(an) * r * SPS * 0.3f), rc, SPD);
    }
    sbox(-0.08f, 0, 0.08f, 0.06f, rc);
    return;
  }
  /* which side of it we see */
  float fx = cosf(a->yaw), fy = sinf(a->yaw);
  float along = fx * C.fx + fy * C.fy, across = fx * C.rx + fy * C.ry;
  int view = along < -0.55f ? 0 : along > 0.55f ? 2 : 1;   /* front, side, back */
  SPU = view == 1 && across < 0 ? -1 : 1;
  sshadow(0.42f);
  if (a->squid) {   /* squid on dry ground */
    sell(0, 0.2f, 0.27f, 0.2f, tc);
    sell(0, 0.4f, 0.32f, 0.07f, td);
    sbox(-0.2f, 0, -0.12f, 0.08f, td); sbox(-0.04f, 0, 0.04f, 0.08f, td); sbox(0.12f, 0, 0.2f, 0.08f, td);
    if (view != 2) {
      float e = view == 1 ? 0.12f : 0.09f;
      sell(e, 0.24f, 0.06f, 0.07f, 0xFFFF); sell(e + 0.01f, 0.23f, 0.03f, 0.04f, EYE);
      if (view == 0) { sell(-e, 0.24f, 0.06f, 0.07f, 0xFFFF); sell(-e - 0.01f, 0.23f, 0.03f, 0.04f, EYE); }
    }
    return;
  }
  uint16_t shirt = a->flash > 0 ? 0xFFFF : SHIRT;
  if (view == 1) {   /* side: facing screen right (mirrored when facing left) */
    sbox(-0.1f, 0, 0.16f, 0.09f, SHOE);
    sbox(-0.07f, 0.09f, 0.07f, 0.42f, SKIN);
    sbox(-0.12f, 0.36f, 0.12f, 0.5f, PANTS);
    sbox(-0.14f, 0.48f, 0.14f, 0.9f, shirt);
    sbox(-0.14f, 0.66f, 0.14f, 0.71f, tc);
    sbox(-0.3f, 0.52f, -0.14f, 0.86f, GUN);   /* the ink tank on its back */
    sbox(-0.28f, 0.54f, -0.16f, 0.54f + 0.3f * a->ink / 100, tc);
    sell(0.03f, 1.12f, 0.21f, 0.24f, SKIN);
    sell(-0.05f, 1.2f, 0.25f, 0.19f, tc);
    sbox(-0.28f, 0.84f, -0.14f, 1.16f, tc);
    sell(-0.21f, 0.85f, 0.07f, 0.05f, td);
    sell(0.13f, 1.1f, 0.04f, 0.065f, EYE);
    if (a->weapon == W_SHOOTER) { sbox(0.05f, 0.62f, 0.5f, 0.74f, GUN); sbox(0.12f, 0.74f, 0.26f, 0.84f, tc); }
    else if (a->weapon == W_CHARGER) { sbox(0.02f, 0.66f, 0.95f, 0.72f, GUN); sbox(0.2f, 0.72f, 0.4f, 0.78f, tc); }
    else { sbox(0.05f, 0.25f, 0.4f, 0.31f, GUN); sbox(0.34f, 0, 0.64f, 0.26f, RGB(225, 225, 225)); sbox(0.34f, 0, 0.64f, 0.07f, tc); }
    sbox(0.06f, 0.55f, 0.16f, 0.8f, shirt);   /* arm */
    return;
  }
  bool back = view == 2;
  sbox(-0.2f, 0, -0.03f, 0.09f, SHOE); sbox(0.03f, 0, 0.2f, 0.09f, SHOE);
  sbox(-0.16f, 0.09f, -0.05f, 0.42f, SKIN); sbox(0.05f, 0.09f, 0.16f, 0.42f, SKIN);
  sbox(-0.19f, 0.36f, 0.19f, 0.5f, PANTS);
  sbox(-0.22f, 0.48f, 0.22f, 0.9f, shirt);
  sbox(-0.22f, 0.66f, 0.22f, 0.71f, tc);
  sbox(-0.31f, 0.52f, -0.22f, 0.86f, shirt); sbox(0.22f, 0.52f, 0.31f, 0.86f, shirt);
  sbox(-0.31f, 0.47f, -0.22f, 0.53f, SKIN); sbox(0.22f, 0.47f, 0.31f, 0.53f, SKIN);
  if (back) {
    sbox(-0.13f, 0.52f, 0.13f, 0.88f, GUN);   /* the ink tank shows how much ink is left */
    sbox(-0.1f, 0.55f, 0.1f, 0.55f + 0.3f * a->ink / 100, tc);
    sell(0, 1.13f, 0.26f, 0.27f, tc);
    sbox(-0.3f, 0.82f, -0.17f, 1.15f, tc); sbox(0.17f, 0.82f, 0.3f, 1.15f, tc);
    sell(-0.235f, 0.83f, 0.065f, 0.05f, td); sell(0.235f, 0.83f, 0.065f, 0.05f, td);
    sbox(-0.12f, 0.9f, 0.12f, 1.0f, td);
  } else {
    sell(0, 1.12f, 0.24f, 0.25f, SKIN);
    sell(0, 1.24f, 0.27f, 0.16f, tc);
    sbox(-0.3f, 0.84f, -0.18f, 1.2f, tc); sbox(0.18f, 0.84f, 0.3f, 1.2f, tc);
    sell(-0.24f, 0.85f, 0.065f, 0.05f, td); sell(0.24f, 0.85f, 0.065f, 0.05f, td);
    sell(-0.09f, 1.09f, 0.045f, 0.065f, EYE); sell(0.09f, 1.09f, 0.045f, 0.065f, EYE);
    if (SPS > 30) { sbox(-0.1f, 1.11f, -0.08f, 1.13f, 0xFFFF); sbox(0.08f, 1.11f, 0.1f, 1.13f, 0xFFFF); }
  }
  if (a->weapon == W_ROLLER) {
    sbox(0.24f, 0.2f, 0.28f, 0.55f, GUN);
    sbox(-0.42f, 0, 0.42f, 0.22f, RGB(225, 225, 225));
    sbox(-0.42f, 0, 0.42f, 0.07f, tc);
  } else if (!back) {
    sbox(0.22f, 0.56f, 0.34f, 0.8f, GUN);
    if (a->weapon == W_CHARGER) sbox(0.24f, 0.8f, 0.32f, 0.86f, tc);
    else sbox(0.24f, 0.74f, 0.32f, 0.84f, tc);
  }
  if (a->hp < 99 && a->idx != 0 && s->t < 16) {   /* health bar over hurt kids */
    int bw = (int)(0.6f * SPS) + 4, bx = (int)SPX - bw / 2, by = (int)(SPY - 1.6f * SPS) - 2;
    for (int i = 0; i < bw; i++) for (int k = 0; k < KX; k++) P(bx + i, by, i < bw * a->hp / 100 ? RGB(80, 230, 90) : RGB(60, 20, 20));
  }
}

static void line_d(float x0, float y0, float x1, float y1, uint16_t c, uint8_t d, int dash, int thick) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
  if (n > 700) n = 700;
  for (int i = 0; i <= n; i++) {
    if (dash && (i / dash) & 1) continue;
    int x = (int)(x0 + dx * i / n), y = (int)(y0 + dy * i / n);
    PD(x, y, c, d);
    if (thick > 1) { PD(x, y + 1, c, d); PD(x + 1, y, c, d); }
  }
}
static void seg3(float x0, float y0, float z0, float x1, float y1, float z1, uint16_t c, int dash) {
  float d0 = (x0 - C.x) * C.fx + (y0 - C.y) * C.fy, d1 = (x1 - C.x) * C.fx + (y1 - C.y) * C.fy;
  float nearp = 0.4f;
  if (d0 < nearp && d1 < nearp) return;
  if (d0 < nearp) { float s = (nearp - d0) / (d1 - d0); x0 += (x1 - x0) * s; y0 += (y1 - y0) * s; z0 += (z1 - z0) * s; }
  if (d1 < nearp) { float s = (nearp - d1) / (d0 - d1); x1 += (x0 - x1) * s; y1 += (y0 - y1) * s; z1 += (z0 - z1) * s; }
  float ax, ay, at, bx, by, bt;
  if (!project(x0, y0, z0, &ax, &ay, &at) || !project(x1, y1, z1, &bx, &by, &bt)) return;
  line_d(ax, ay, bx, by, c, dz(at < bt ? at : bt), dash * KX, quality == Q_FULL ? 2 : 1);
}

static void collect_sprites(void) {
  n_spr = 0;
  float sx, sy, t;
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || (a->invuln > 0 && ((int)(G.clock * 12) & 1))) continue;
    if (project(a->x, a->y, a->z, &sx, &sy, &t) && t < TMAX) sprites[n_spr++] = (spr_t){sx, sy, FOC / t, t, S_ACTOR, i};
  }
  for (int i = 0; i < MAX_PROJ; i++)
    if (G.p[i].used && project(G.p[i].x, G.p[i].y, G.p[i].z, &sx, &sy, &t) && t < TMAX) sprites[n_spr++] = (spr_t){sx, sy, FOC / t, t, S_PROJ, i};
  for (int i = 0; i < MAX_PART; i++)
    if (G.part[i].t > 0 && project(G.part[i].x, G.part[i].y, G.part[i].z, &sx, &sy, &t) && t < TMAX) sprites[n_spr++] = (spr_t){sx, sy, FOC / t, t, S_PART, i};
  for (int i = 0; i < MAX_BLAST; i++)
    if (G.blast[i].t > 0 && project(G.blast[i].x, G.blast[i].y, G.blast[i].z + 0.3f, &sx, &sy, &t)) sprites[n_spr++] = (spr_t){sx, sy, FOC / t, t, S_BLAST, i};
  for (int i = 1; i < n_spr; i++)   /* far to near */
    for (int j = i; j > 0 && sprites[j].t > sprites[j - 1].t; j--) { spr_t s = sprites[j]; sprites[j] = sprites[j - 1]; sprites[j - 1] = s; }
}

static void draw_sprites(void) {
  for (int k = 0; k < n_spr; k++) {
    const spr_t *s = &sprites[k];
    float reach = s->s * (s->kind == S_BLAST ? G.blast[s->i].r : 1.0f) + 4;
    if (s->sx + reach < c0 || s->sx - reach >= c1) continue;   /* not in this strip */
    SPX = s->sx; SPY = s->sy; SPS = s->s; SPD = dz(s->t); SPU = 1;
    if (s->kind == S_ACTOR) draw_kid(&G.a[s->i], s);
    else if (s->kind == S_PROJ) {
      const proj_t *p = &G.p[s->i];
      if (p->kind == P_BOMB) {
        uint16_t c = p->landed && ((int)(p->fuse * 10) & 1) ? 0xFFFF : G.col[p->team];
        sell(0, 0.18f, 0.2f, 0.2f, c);
        sell(-0.06f, 0.25f, 0.05f, 0.05f, mix(c, 0xFFFF, 20));
        sbox(-0.02f, 0.36f, 0.02f, 0.44f, GUN);
      } else {
        sell(0, 0, 0.11f, 0.11f, G.col[p->team]);
        sell(-0.03f, 0.03f, 0.04f, 0.04f, mix(G.col[p->team], 0xFFFF, 18));
      }
    } else if (s->kind == S_PART) {
      sell(0, 0, 0.06f, 0.06f, G.col[G.part[s->i].team]);
    } else {
      const blast_t *b = &G.blast[s->i];
      float k2 = 1 - b->t / 0.3f, r = b->r * (0.4f + 0.6f * k2);
      SPD = dz(s->t - b->r);
      uint16_t c = mix(G.col[b->team], 0xFFFF, (int)(12 * (1 - k2)));
      if (k2 < 0.5f) sell(0, 0.6f, r * 0.7f, r * 0.5f, c);
      else for (int i = 0; i < 40; i++) {
        float an = i * (2 * PI_F / 40);
        sell(cosf(an) * r, 0.3f + sinf(an) * r * 0.4f + 0.3f, 0.12f, 0.12f, c);
      }
    }
  }
  /* charger sights and shots */
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || a->weapon != W_CHARGER || a->charge <= 0.05f || a->squid) continue;
    float r = 11 + 16 * a->charge, z = a->z + 1.0f, fx = cosf(a->yaw), fy = sinf(a->yaw), sl = tanf(a->pitch), d = 0.5f;
    for (; d < r; d += 0.5f)
      if (col_kind((int)floorf((a->x + fx * d) * 2), (int)floorf((a->y + fy * d) * 2)) == K_WALL || ground_at(a->x + fx * d, a->y + fy * d) > z + sl * d) break;
    seg3(a->x + fx * 0.5f, a->y + fy * 0.5f, z + sl * 0.5f, a->x + fx * d, a->y + fy * d, z + sl * d, a->charge >= 1 ? 0xFFFF : G.col[a->team], 2);
  }
  for (int i = 0; i < MAX_BEAM; i++) {
    const beam_t *b = &G.beam[i];
    if (b->t > 0) seg3(b->x0, b->y0, b->z0, b->x1, b->y1, b->z1, mix(G.col[b->team], 0xFFFF, 10), 0);
  }
}

/* ------------------------------------------------------------------ HUD, in a virtual 160 x 120 */
static int vkx, vky;   /* render px per virtual px */
void frame_fill(int x, int y, int w, int h, uint16_t c) {
  int x0 = x * vkx, x1 = (x + w) * vkx, y0 = y * vky, y1 = (y + h) * vky;
  if (x0 < c0) x0 = c0;
  if (x1 > c1) x1 = c1;
  if (y0 < 0) y0 = 0;
  if (y1 > RHm) y1 = RHm;
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) sbuf[yy * SC + xx - c0] = c;
}
static const uint8_t FONT[59][5] = {
  {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
  {0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0,0x08,0x07,0x03,0},{0,0x1C,0x22,0x41,0},
  {0,0x41,0x22,0x1C,0},{0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},{0,0x50,0x30,0,0},
  {0x08,0x08,0x08,0x08,0x08},{0,0,0x60,0x60,0},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},
  {0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1E},{0,0,0x14,0,0},{0,0x40,0x34,0,0},
  {0,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,0x08},{0x02,0x01,0x59,0x09,0x06},
  {0x3E,0x41,0x5D,0x59,0x4E},{0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x51,0x73},
  {0x7F,0x08,0x08,0x08,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},
  {0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},
};
int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }
void text(int x, int y, const char *s, uint16_t c, int scale) {
  int x_lo = c0 / vkx - 6 * scale, x_hi = c1 / vkx + 1;
  for (; *s; s++, x += 6 * scale) {
    if (x < x_lo || x > x_hi) continue;   /* not in this strip */
    int ch = *s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 'Z') ch = '?';
    const uint8_t *g = FONT[ch - 32];
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < 7; j++)
        if (g[i] >> j & 1) frame_fill(x + i * scale, y + j * scale, scale, scale, c);
  }
}
void text_o(int x, int y, const char *s, uint16_t c, int scale) {
  for (int k = 0; k < 4; k++) text(x + (k == 0) - (k == 1), y + (k == 2) - (k == 3), s, RGB(20, 20, 30), scale);
  text(x, y, s, c, scale);
}
static void text_c(int y, const char *s, uint16_t c, int scale) { text_o((VW - text_w(s, scale)) / 2, y, s, c, scale); }

static uint16_t mini[28][47];   /* the minimap, built once per frame (1 px = 2 m) */
static void build_minimap(void) {
  for (int y = 0; y < 28; y++)
    for (int x = 0; x < 47; x++) {
      int tx = x * 4 + 1, ty = y * 4 + 1, k = col_kind(tx, ty);
      uint16_t c = k == K_WATER ? RGB(30, 60, 90) : k == K_WALL ? RGB(70, 70, 80) : RGB(150, 150, 150);
      int p = k == K_WATER ? 0 : paint_get(tx * 2 + 1, ty * 2 + 1);
      if (p) c = G.col[p - 1];
      mini[y][x] = c;
    }
}
static void minimap(int x0, int y0) {
  if ((x0 + 48) * vkx < c0 || (x0 - 1) * vkx >= c1) return;
  frame_fill(x0 - 1, y0 - 1, 49, 30, RGB(20, 20, 30));
  int xs = x0 * vkx, ys = y0 * vky, xa = xs > c0 ? xs : c0, xb = xs + 47 * vkx < c1 ? xs + 47 * vkx : c1;
  for (int y = 0; y < 28 * vky; y++) {
    const uint16_t *m = mini[y / vky];
    uint16_t *d = sbuf + (ys + y) * SC - c0;
    for (int x = xa; x < xb; x++) d[x] = m[(x - xs) / vkx];
  }
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || (a->team != G.a[0].team && !G.demo)) continue;
    int px = x0 + (int)(a->x / 2), py = y0 + (int)(a->y / 2);
    if (i == 0) frame_fill(px - 1, py - 1, 3, 3, 0xFFFF);
    frame_fill(px, py, 1, 1, i == 0 ? EYE : dim(G.col[a->team], 18));
  }
}

static void reticle(void) {
  const actor_t *me = &G.a[0];
  if (!me->alive || me->squid) return;
  float r = me->weapon == W_CHARGER ? 11 + 16 * me->charge : weapon_range(me->weapon);
  float sl = tanf(me->pitch);
  float ex = me->x + cosf(me->yaw) * r, ey = me->y + sinf(me->yaw) * r, ez = me->z + 1.0f + sl * r;
  float sx, sy, t;
  if (!project(ex, ey, ez, &sx, &sy, &t)) return;
  int x = (int)sx, y = (int)sy, a = 2 * vkx, b = 4 * vkx, ay = 2 * vky, by = 4 * vky;
  for (int k = a; k <= b; k++) { P(x - k, y, 0xFFFF); P(x + k, y, 0xFFFF); }
  for (int k = ay; k <= by; k++) { P(x, y - k, 0xFFFF); P(x, y + k, 0xFFFF); }
}

static int hud_fps, hud_ms;
static bool hud_show;
static void hud(void) {
  const actor_t *me = &G.a[0];
  char b[40];
  reticle();
  for (int t = 0; t < 2; t++)
    for (int i = 0; i < TEAM_SIZE; i++) {
      const actor_t *a = &G.a[t * TEAM_SIZE + i];
      int x = t == 0 ? 3 + i * 9 : VW - 11 - i * 9;
      frame_fill(x - 1, 2, 9, 9, RGB(20, 20, 30));
      frame_fill(x, 3, 7, 7, a->alive ? G.col[t] : RGB(90, 90, 90));
      if (!a->alive) { frame_fill(x + 3, 4, 1, 5, RGB(30, 30, 30)); frame_fill(x + 1, 6, 5, 1, RGB(30, 30, 30)); }
    }
  int tl = (int)ceilf(G.time_left);
  snprintf(b, sizeof b, "%d:%02d", tl / 60, tl % 60);
  text_c(3, b, G.phase == PH_PLAY && tl <= 10 ? RGB(255, 120, 100) : 0xFFFF, 1);
  if (hud_show) {
    snprintf(b, sizeof b, "%dFPS %dMS %s", hud_fps, hud_ms, QUALITY_NAME[quality]);
    text_o(VW - text_w(b, 1) - 2, 13, b, RGB(255, 255, 120), 1);
  }
  int ix = 4, iy = VH - 44;
  frame_fill(ix - 1, iy - 1, 8, 42, RGB(20, 20, 30));
  int fill = (int)(me->ink * 40 / 100);
  frame_fill(ix, iy, 6, 40 - fill, RGB(70, 70, 80));
  frame_fill(ix, iy + 40 - fill, 6, fill, G.col[me->team]);
  frame_fill(ix + 9, VH - 7, 32, 4, RGB(20, 20, 30));
  frame_fill(ix + 10, VH - 6, (int)(30 * me->hp / 100), 2, me->hp > 40 ? RGB(80, 230, 90) : RGB(240, 70, 60));
  text_o(ix + 10, VH - 16, WEAPON_NAME[me->weapon], 0xFFFF, 1);
  minimap(VW - 49, VH - 30);
  if (me->alive && me->ink < 5 && !me->squid && G.phase == PH_PLAY && ((int)(G.clock * 3) & 1)) text_c(VH / 2 + 10, "LOW INK", RGB(255, 210, 80), 1);
  if (G.phase == PH_INTRO) text_c(40, G.phase_t < 2.0f ? "READY?" : "GO!", 0xFFFF, 3);
  else if (G.phase == PH_FINISH) text_c(40, "FINISH!", 0xFFFF, 3);
  else if (G.phase == PH_PLAY && tl <= 10 && tl > 0) { snprintf(b, sizeof b, "%d", tl); text_c(16, b, 0xFFFF, 2); }
  if (!me->alive && G.phase == PH_PLAY && !G.demo) {
    snprintf(b, sizeof b, "RESPAWN IN %d", (int)ceilf(me->respawn));
    text_c(VH / 2 + 10, b, 0xFFFF, 1);
  }
  if (G.msg_t > 0) text_c(26, G.msg, RGB(255, 240, 200), 1);
}

/* ------------------------------------------------------------------ strips */
static void push_strip(void) {
  if (render_debug & RD_NOPUSH) return;
  int sx = c0 * KX;
  if (KX == 1 && KY == 1) {
    eadk_display_push_rect((eadk_rect_t){(uint16_t)sx, 0, STRIP_PX, 240}, sbuf);
    return;
  }
  /* widen to screen pixels in place, last row first, so no row is overwritten before it is read
   * (row y goes to y * KY * 32 and up, at or after where it was, y * SC) */
  for (int y = RHm - 1; y >= 0; y--) {
    const uint16_t *src = sbuf + y * SC;
    uint16_t *d = sbuf + y * KY * STRIP_PX;
    if (KX == 2) for (int x = SC - 1; x >= 0; x--) { uint16_t v = src[x]; d[2 * x + 1] = v; d[2 * x] = v; }
    else memmove(d, src, STRIP_PX * 2);
    if (KY == 2) memcpy(d + STRIP_PX, d, STRIP_PX * 2);
  }
  eadk_display_push_rect((eadk_rect_t){(uint16_t)sx, 0, STRIP_PX, 240}, sbuf);
}
int render_bench_raw(void) {
  uint64_t a = eadk_timing_millis();
  for (int k = 0; k < 10; k++)
    for (int s = 0; s < 10; s++)
      for (int x = 0; x < 32; x++) {
        uint16_t *col = sbuf + x;
        uint8_t *dep = sdep + x;
        for (int r = 0; r < 240; r++) { col[r * 32] = (uint16_t)(r + x); dep[r * 32] = (uint8_t)r; }
      }
  return (int)(eadk_timing_millis() - a);   /* for 10 frames: tenths of a ms per frame */
}
static void run_strips(void (*draw)(void)) {
  vkx = RWm / VW;
  vky = RHm / VH;
  for (c0 = 0; c0 < RWm; c0 += SC) {
    c1 = c0 + SC;
    draw();
    push_strip();
  }
}

static void scene_strip(void) {
  for (int c = c0; c < c1; c++) draw_column(c);
  if (!(render_debug & RD_NOSPRITES)) draw_sprites();
}
static void game_strip(void) {
  scene_strip();
  if (!(render_debug & RD_NOHUD)) hud();
}

static void setup_camera(void) {
  static float cz_s, yaw_s, pitch_s;
  const actor_t *me = &G.a[0];
  float back = 4.0f, up = 2.2f, shoulder = 0.6f;   /* over the right shoulder, so the kid doesn't hide the reticle */
  if (me->squid) { back = 3.2f; up = 1.6f; shoulder = 0.3f; }
  if (G.clock < 0.05f) { cz_s = me->z; yaw_s = me->yaw; pitch_s = me->pitch; }
  yaw_s += wrap_angle(me->yaw - yaw_s) * 0.5f;
  pitch_s += (me->pitch - pitch_s) * 0.5f;
  cz_s += (me->z - cz_s) * 0.3f;
  float fx = cosf(yaw_s), fy = sinf(yaw_s), camz = cz_s + up;
  for (float d = 0.4f; d <= back; d += 0.25f) {   /* pull in where a building is behind */
    float x = me->x - fx * d - fy * shoulder, y = me->y - fy * d + fx * shoulder;
    if (ground_at(x, y) > camz - 0.4f || col_kind((int)floorf(x * 2), (int)floorf(y * 2)) == K_WALL) { back = d - 0.4f; break; }
  }
  if (back < 0.3f) back = 0.3f;
  C.x = me->x - fx * back - fy * shoulder;
  C.y = me->y - fy * back + fx * shoulder;
  C.z = camz;
  C.fx = fx; C.fy = fy;
  C.rx = -fy; C.ry = fx;   /* +y is on the right when looking along +x */
  C.hz = RHm * 0.375f + tanf(pitch_s) * FOC;
}

void render_frame(int fps, int ms, bool show_fps) {
  hud_fps = fps; hud_ms = ms; hud_show = show_fps;
  setup_camera();
  frame_rays();
  collect_sprites();
  build_minimap();
  run_strips(game_strip);
}

static void (*title_overlay)(void);
static void title_strip(void) {
  for (int c = c0; c < c1; c++) draw_column(c);
  if (title_overlay) title_overlay();
}
void render_title(float a, void (*overlay)(void)) {
  C.x = 47 + cosf(a) * 30; C.y = 28 + sinf(a) * 18; C.z = 9;
  float fx = 47 - C.x, fy = 28 - C.y, l = sqrtf(fx * fx + fy * fy);
  C.fx = fx / l; C.fy = fy / l; C.rx = -C.fy; C.ry = C.fx;
  C.hz = RHm / 6.0f;
  frame_rays();
  title_overlay = overlay;
  run_strips(title_strip);
}

static void map_strip(void) {
  frame_fill(0, 0, VW, VH, RGB(20, 40, 70));
  for (int y = 0; y < 84; y++)
    for (int x = 0; x < 141; x++) {
      if ((10 + x + 1) * vkx < c0 || (10 + x) * vkx >= c1) continue;
      int tx = x * 4 / 3, ty = y * 4 / 3, k = col_kind(tx, ty);
      if (k == K_WATER) continue;
      int h = (int)(col_top(tx, ty) * 2);
      uint16_t c = dim(map_colour(col_colour(tx, ty)), k == K_WALL ? 14 : 22 + (h > 8 ? 8 : h));
      int p = paint_get(tx * 2 + 1, ty * 2 + 1);
      if (p && k != K_WALL) c = G.col[p - 1];
      if ((col_light(tx, ty) & 15) && k != K_WALL) c = dim(c, 24);
      frame_fill(10 + x, 18 + y, 1, 1, c);
    }
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || (a->team != G.a[0].team && !G.demo)) continue;
    int px = 10 + (int)(a->x * 1.5f), py = 18 + (int)(a->y * 1.5f);
    frame_fill(px - 1, py - 1, 3, 3, i == 0 ? 0xFFFF : RGB(20, 20, 30));
    frame_fill(px, py, 1, 1, G.col[a->team]);
  }
  text_c(4, "PAUSED", 0xFFFF, 1);
  text_c(VH - 14, "OK: RESUME   BACK: QUIT", RGB(230, 230, 230), 1);
}
void render_map(void) { run_strips(map_strip); }
