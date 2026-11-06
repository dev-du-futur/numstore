/* Software 3D: one ray per column of a 160 x 120 frame, stepped through the stage's 0.5 m columns
 * (front to back, each column fills the rows above what is already drawn), then billboards for the
 * kids, shots and bombs against a depth buffer, then the HUD. The frame is pushed at 2x in strips. */
#include <eadk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

uint16_t frame[RH][RW];
static uint8_t depth[RH][RW];   /* metres x 8 */

#define FOCAL 100.0f            /* px: about a 77 degree view */
#define NEAR 0.15f
#define TMAX 30.0f
#define FOG0 15.0f
#define SKY_TOP RGB(110, 170, 235)
#define SKY_LOW RGB(205, 228, 245)
#define FOG_C SKY_LOW
#define SEA RGB(46, 120, 170)
#define SEA_HI RGB(120, 190, 225)
#define OUTFIT RGB(40, 44, 60)

typedef struct { float x, y, z, fx, fy, rx, ry; int hz; } cam_t;
static cam_t C;

/* ------------------------------------------------------------------ colour helpers (RGB565) */
static inline uint16_t mix(uint16_t a, uint16_t b, int k) {   /* k 0..32: share of b */
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81Fu, B = (b | (uint32_t)b << 16) & 0x07E0F81Fu;
  uint32_t r = (A + (((B - A) * (uint32_t)k) >> 5)) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint16_t dim(uint16_t c, int k) {   /* k 0..32 brightness */
  uint32_t A = (c | (uint32_t)c << 16) & 0x07E0F81Fu;
  uint32_t r = ((A * (uint32_t)k) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint16_t fog(uint16_t c, float t) {
  if (t <= FOG0) return c;
  int k = (int)((t - FOG0) * (32.0f / (TMAX - FOG0)));
  return mix(c, FOG_C, k > 32 ? 32 : k);
}
static inline uint8_t dz(float t) { int d = (int)(t * 8); return (uint8_t)(d > 255 ? 255 : d); }

/* ------------------------------------------------------------------ terrain */
static uint16_t sky_row[RH];

static void draw_column(int c) {
  float k = (c + 0.5f - RW / 2) / FOCAL;
  float rx = C.fx + C.rx * k, ry = C.fy + C.ry * k;      /* per metre of depth */
  float px = C.x * 2, py = C.y * 2, dx = rx * 2, dy = ry * 2;   /* in columns */
  int ix = (int)floorf(px), iy = (int)floorf(py);
  int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
  float ddx = dx != 0 ? fabsf(1 / dx) : 1e9f, ddy = dy != 0 ? fabsf(1 / dy) : 1e9f;
  float tx = (dx < 0 ? px - ix : ix + 1 - px) * ddx, ty = (dy < 0 ? py - iy : iy + 1 - py) * ddy;
  float t0 = 0;
  int side = 0, ybuf = RH, hz = C.hz;
  float cz = C.z;
  while (ybuf > 0 && t0 < TMAX) {
    float t1 = tx < ty ? tx : ty;
    int kind = col_kind(ix, iy);
    float H = col_top(ix, iy);
    if (t1 > NEAR) {
      float tn = t0 > NEAR ? t0 : NEAR;
      float yf = hz - (H - cz) * FOCAL / tn, yb = hz - (H - cz) * FOCAL / t1;
      int rf = yf < 0 ? 0 : yf >= RH ? RH : (int)ceilf(yf);
      int rb = yb < 0 ? 0 : yb >= RH ? RH : (int)ceilf(yb);
      uint16_t base = kind == K_WATER ? SEA : map_colour(col_colour(ix, iy));
      /* the column's front face */
      if (t0 > NEAR && rf < ybuf) {
        uint16_t fc = dim(base, kind == K_WALL ? (side ? 17 : 22) : (side ? 21 : 26));
        uint8_t d = dz(tn);
        if (kind == K_WALL) {   /* building: storeys of windows */
          float hitu = side ? px * 0.5f + rx * tn : py * 0.5f + ry * tn;   /* along the face, metres */
          bool col_win = ((int)floorf(hitu * 0.8f)) & 1;
          uint16_t win = dim(RGB(70, 90, 120), 28);
          for (int r = rf; r < ybuf; r++) {
            float z = cz + (hz - r - 0.5f) * tn / FOCAL;   /* height of this row on the face */
            float zz = z + 2.0f - (int)(z + 2.0f) / 2 * 2.0f;
            uint16_t pc = (col_win && zz > 0.7f && zz < 1.6f && z > 0.6f) ? win : fc;
            frame[r][c] = fog(pc, tn);
            depth[r][c] = d;
          }
        } else {
          uint16_t f = fog(fc, tn);
          for (int r = rf; r < ybuf; r++) { frame[r][c] = f; depth[r][c] = d; }
        }
      }
      /* the column's top, seen from above: per pixel, for the ink */
      int top_end = rf < ybuf ? rf : ybuf;
      if (rb < top_end && H < cz) {
        float hgt = cz - H;
        uint16_t roof = dim(base, kind == K_WALL ? 20 : ((ix + iy) & 1 ? 32 : 30));
        for (int r = rb; r < top_end; r++) {
          float t = hgt * FOCAL / (r + 0.5f - hz);
          uint16_t pc = roof;
          if (kind == K_FLOOR || kind == K_RAIL) {
            int qx = (int)floorf((C.x + rx * t) * 4), qy = (int)floorf((C.y + ry * t) * 4);
            int p = paint_get(qx, qy);
            if (p) pc = ((qx ^ qy) & 3) ? G.col[p - 1] : G.col_dark[p - 1];
            else if (kind == K_RAIL && ((qx + qy) & 1)) pc = RGB(60, 70, 70);
          } else if (kind == K_WATER) {
            int qx = (int)floorf((C.x + rx * t) * 2), qy = (int)floorf((C.y + ry * t) * 4);
            if (((qx * 7 + qy * 13 + (int)(G.clock * 3)) & 31) == 0) pc = SEA_HI;
          }
          frame[r][c] = fog(pc, t);
          depth[r][c] = dz(t);
        }
      }
      int top = rb < rf ? rb : rf;
      if (top < ybuf) ybuf = top;
    }
    if (tx < ty) { ix += sx; t0 = tx; tx += ddx; side = 0; }
    else { iy += sy; t0 = ty; ty += ddy; side = 1; }
  }
  for (int r = 0; r < ybuf; r++) { frame[r][c] = sky_row[r]; depth[r][c] = 255; }
}

/* ------------------------------------------------------------------ billboards */
static bool project(float x, float y, float z, float *sx, float *sy, float *t) {
  float ox = x - C.x, oy = y - C.y;
  float d = ox * C.fx + oy * C.fy;
  if (d < NEAR * 2) return false;
  float l = ox * C.rx + oy * C.ry;
  *sx = RW / 2 + l * FOCAL / d;
  *sy = C.hz - (z - C.z) * FOCAL / d;
  *t = d;
  return true;
}
static inline void dot(int x, int y, uint16_t c, uint8_t d) {
  if ((unsigned)x < RW && (unsigned)y < RH && d < depth[y][x]) frame[y][x] = c;
}
static void rect_d(float x0, float y0, float x1, float y1, uint16_t c, uint8_t d) {
  int a = (int)x0, b = (int)ceilf(x1), e = (int)y0, f = (int)ceilf(y1);
  if (b <= a) b = a + 1;
  if (f <= e) f = e + 1;
  for (int y = e; y < f; y++)
    for (int x = a; x < b; x++) dot(x, y, c, d);
}
static void disc_d(float cx, float cy, float r, uint16_t c, uint8_t d) {
  if (r < 0.7f) { dot((int)cx, (int)cy, c, d); return; }
  int y0 = (int)(cy - r), y1 = (int)(cy + r), x0 = (int)(cx - r), x1 = (int)(cx + r);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float ex = x + 0.5f - cx, ey = y + 0.5f - cy;
      if (ex * ex + ey * ey <= r * r) dot(x, y, c, d);
    }
}
static void ellipse_d(float cx, float cy, float rx, float ry, uint16_t c, uint8_t d, bool ring) {
  int y0 = (int)(cy - ry), y1 = (int)(cy + ry), x0 = (int)(cx - rx), x1 = (int)(cx + rx);
  if (rx < 0.5f || ry < 0.5f) { dot((int)cx, (int)cy, c, d); return; }
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float ex = (x + 0.5f - cx) / rx, ey = (y + 0.5f - cy) / ry, e = ex * ex + ey * ey;
      if (e <= 1 && (!ring || e > 0.55f)) dot(x, y, c, d);
    }
}
static void line_d(float x0, float y0, float x1, float y1, uint16_t c, uint8_t d, int dash) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
  if (n > 400) n = 400;
  for (int i = 0; i <= n; i++) {
    if (dash && (i / dash) & 1) continue;
    dot((int)(x0 + dx * i / n), (int)(y0 + dy * i / n), c, d);
  }
}
/* a 3D segment, clipped to the near plane */
static void seg3(float x0, float y0, float z0, float x1, float y1, float z1, uint16_t c, int dash) {
  float d0 = (x0 - C.x) * C.fx + (y0 - C.y) * C.fy, d1 = (x1 - C.x) * C.fx + (y1 - C.y) * C.fy;
  float nearp = NEAR * 3;
  if (d0 < nearp && d1 < nearp) return;
  if (d0 < nearp) { float s = (nearp - d0) / (d1 - d0); x0 += (x1 - x0) * s; y0 += (y1 - y0) * s; z0 += (z1 - z0) * s; }
  if (d1 < nearp) { float s = (nearp - d1) / (d0 - d1); x1 += (x0 - x1) * s; y1 += (y0 - y1) * s; z1 += (z0 - z1) * s; }
  float ax, ay, at, bx, by, bt;
  if (!project(x0, y0, z0, &ax, &ay, &at) || !project(x1, y1, z1, &bx, &by, &bt)) return;
  line_d(ax, ay, bx, by, c, dz(at < bt ? at : bt) , dash);
}

static void draw_actor(const actor_t *a) {
  if (!a->alive) return;
  if (a->invuln > 0 && ((int)(G.clock * 12) & 1)) return;
  float sx, sy, t;
  float ox = a->x - C.x, oy = a->y - C.y;
  if (!project(a->x, a->y, a->z, &sx, &sy, &t) || t > TMAX) return;
  float s = FOCAL / t;   /* px per metre */
  uint8_t d = dz(t - 0.2f);
  uint16_t tc = a->flash > 0 ? RGB(255, 255, 255) : G.col[a->team];
  if (a->swim) {   /* hidden in its ink: a ripple, for teammates (and a foe close by or swimming fast) */
    const actor_t *me = &G.a[0];
    float sp = sqrtf(a->vx * a->vx + a->vy * a->vy);
    if (a->team != me->team && !G.demo && !(t < 3.5f || (sp > 7 && t < 9))) return;
    ellipse_d(sx, sy, 0.45f * s, 0.12f * s + 0.5f, mix(G.col[a->team], RGB(255, 255, 255), 12), d, true);
    return;
  }
  bool facing_us = cosf(a->yaw) * ox + sinf(a->yaw) * oy < 0;
  if (a->squid) {   /* squid on dry ground: a little blob */
    ellipse_d(sx, sy - 0.2f * s, 0.38f * s, 0.24f * s, tc, d, false);
    if (facing_us && s > 14) { dot((int)(sx - 0.12f * s), (int)(sy - 0.25f * s), 0xFFFF, d); dot((int)(sx + 0.12f * s), (int)(sy - 0.25f * s), 0xFFFF, d); }
    return;
  }
  /* kid: legs, body, ink-tentacle head, and the weapon on the side it holds it */
  float w = 0.26f * s;
  rect_d(sx - w * 0.8f, sy - 0.45f * s, sx + w * 0.8f, sy, dim(OUTFIT, 20), d);
  rect_d(sx - w, sy - 0.9f * s, sx + w, sy - 0.42f * s, a->flash > 0 ? tc : OUTFIT, d);
  disc_d(sx, sy - 1.13f * s, 0.32f * s, tc, d);
  rect_d(sx - 0.34f * s, sy - 1.15f * s, sx + 0.34f * s, sy - 0.98f * s, tc, d);   /* tentacles */
  float side = cosf(a->yaw) * C.rx + sinf(a->yaw) * C.ry;   /* weapon toward where it points */
  float gx = sx + side * 0.35f * s;
  uint16_t gun = a->weapon == W_ROLLER ? RGB(220, 220, 220) : RGB(90, 90, 100);
  if (a->weapon == W_ROLLER) rect_d(gx - 0.25f * s, sy - 0.3f * s, gx + 0.25f * s, sy, gun, d);
  else rect_d(gx - 0.08f * s, sy - 0.8f * s, gx + 0.08f * s, sy - 0.55f * s, gun, d);
  if (facing_us && s > 10) {
    int ey = (int)(sy - 1.12f * s);
    dot((int)(sx - 0.12f * s), ey, 0xFFFF, d);
    dot((int)(sx + 0.12f * s), ey, 0xFFFF, d);
  }
  if (a->hp < 99 && a->idx != 0 && t < 16) {   /* health bar over hurt kids */
    int bw = (int)(0.6f * s) + 4, bx = (int)sx - bw / 2, by = (int)(sy - 1.6f * s) - 2;
    for (int i = 0; i < bw; i++) dot(bx + i, by, i < bw * a->hp / 100 ? RGB(80, 230, 90) : RGB(60, 20, 20), 0);
  }
}

/* ------------------------------------------------------------------ HUD: 5x7 font */
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
  for (; *s; s++, x += 6 * scale) {
    int ch = *s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 'Z') ch = '?';
    const uint8_t *g = FONT[ch - 32];
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < 7; j++)
        if (g[i] >> j & 1)
          for (int a = 0; a < scale; a++)
            for (int b = 0; b < scale; b++) {
              int px = x + i * scale + a, py = y + j * scale + b;
              if ((unsigned)px < RW && (unsigned)py < RH) frame[py][px] = c;
            }
  }
}
/* text with a dark outline, readable over the scene */
static void text_o(int x, int y, const char *s, uint16_t c, int scale) {
  for (int k = 0; k < 4; k++) text(x + (k == 0) - (k == 1), y + (k == 2) - (k == 3), s, RGB(20, 20, 30), scale);
  text(x, y, s, c, scale);
}
static void text_c(int y, const char *s, uint16_t c, int scale) { text_o((RW - text_w(s, scale)) / 2, y, s, c, scale); }
void frame_fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      if ((unsigned)i < RW && (unsigned)j < RH) frame[j][i] = c;
}
static void shade_box(int x, int y, int w, int h) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      if ((unsigned)i < RW && (unsigned)j < RH) frame[j][i] = dim(frame[j][i], 12);
}

static void minimap(int x0, int y0) {   /* 1 px = 2 m */
  shade_box(x0 - 1, y0 - 1, 49, 30);
  for (int y = 0; y < 28; y++)
    for (int x = 0; x < 47; x++) {
      int tx = x * 4 + 1, ty = y * 4 + 1, k = col_kind(tx, ty);
      if (k == K_WATER) continue;
      uint16_t c = k == K_WALL ? RGB(70, 70, 80) : RGB(150, 150, 150);
      int p = paint_get(tx * 2 + 1, ty * 2 + 1);
      if (p) c = G.col[p - 1];
      frame[y0 + y][x0 + x] = c;
    }
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || (a->team != G.a[0].team && !G.demo)) continue;
    int px = x0 + (int)(a->x / 2), py = y0 + (int)(a->y / 2);
    uint16_t c = i == 0 ? 0xFFFF : dim(G.col[a->team], 18);
    frame_fill(px - (i == 0), py - (i == 0), 1 + 2 * (i == 0), 1 + 2 * (i == 0), c);
  }
}

static void hud(int fps, int ms, bool show_fps) {
  const actor_t *me = &G.a[0];
  char b[40];
  /* top: the two teams' kids (grey = splatted), the clock */
  for (int t = 0; t < 2; t++)
    for (int i = 0; i < TEAM_SIZE; i++) {
      const actor_t *a = &G.a[t * TEAM_SIZE + i];
      int x = t == 0 ? 3 + i * 9 : RW - 11 - i * 9;
      frame_fill(x - 1, 2, 9, 9, RGB(20, 20, 30));
      frame_fill(x, 3, 7, 7, a->alive ? G.col[t] : RGB(90, 90, 90));
      if (!a->alive) { frame_fill(x + 3, 4, 1, 5, RGB(30, 30, 30)); frame_fill(x + 1, 6, 5, 1, RGB(30, 30, 30)); }
    }
  int tl = (int)ceilf(G.time_left);
  snprintf(b, sizeof b, "%d:%02d", tl / 60, tl % 60);
  text_c(3, b, G.phase == PH_PLAY && tl <= 10 ? RGB(255, 120, 100) : 0xFFFF, 1);
  if (show_fps) {
    snprintf(b, sizeof b, "%dFPS %dMS", fps, ms);
    text_o(RW - text_w(b, 1) - 2, 13, b, RGB(255, 255, 120), 1);
  }
  /* bottom left: ink tank and health */
  int ix = 4, iy = RH - 44;
  frame_fill(ix - 1, iy - 1, 8, 42, RGB(20, 20, 30));
  int fill = (int)(me->ink * 40 / 100);
  frame_fill(ix, iy, 6, 40 - fill, RGB(70, 70, 80));
  frame_fill(ix, iy + 40 - fill, 6, fill, G.col[me->team]);
  frame_fill(ix + 9, RH - 7, 32, 4, RGB(20, 20, 30));
  frame_fill(ix + 10, RH - 6, (int)(30 * me->hp / 100), 2, me->hp > 40 ? RGB(80, 230, 90) : RGB(240, 70, 60));
  text_o(ix + 10, RH - 16, WEAPON_NAME[me->weapon], 0xFFFF, 1);
  minimap(RW - 49, RH - 30);
  /* the reticle: where a shot would go */
  if (me->alive && !me->squid) {
    float r = weapon_range(me->weapon);
    if (me->weapon == W_CHARGER) r = 11 + 16 * me->charge;
    float ex = me->x + cosf(me->yaw) * r, ey = me->y + sinf(me->yaw) * r, ez = me->z + 1.0f;
    float sx, sy, t;
    if (project(ex, ey, ez, &sx, &sy, &t)) {
      int x = (int)sx, y = (int)sy;
      uint16_t c = 0xFFFF;
      frame_fill(x - 4, y, 3, 1, c); frame_fill(x + 2, y, 3, 1, c);
      frame_fill(x, y - 4, 1, 3, c); frame_fill(x, y + 2, 1, 3, c);
    }
  }
  if (me->alive && me->ink < 5 && !me->squid && G.phase == PH_PLAY && ((int)(G.clock * 3) & 1)) text_c(RH / 2 + 10, "LOW INK", RGB(255, 210, 80), 1);
  /* banners */
  if (G.phase == PH_INTRO) text_c(40, G.phase_t < 2.0f ? "READY?" : "GO!", 0xFFFF, 3);
  else if (G.phase == PH_FINISH) text_c(40, "FINISH!", 0xFFFF, 3);
  else if (G.phase == PH_PLAY && tl <= 10 && tl > 0) { snprintf(b, sizeof b, "%d", tl); text_c(16, b, RGB(255, 255, 255), 2); }
  if (!me->alive && G.phase == PH_PLAY && !G.demo) {
    snprintf(b, sizeof b, "RESPAWN IN %d", (int)ceilf(me->respawn));
    text_c(RH / 2 + 10, b, 0xFFFF, 1);
  }
  if (G.msg_t > 0) text_c(26, G.msg, RGB(255, 240, 200), 1);
}

/* ------------------------------------------------------------------ frame */
static void setup_camera(void) {
  static float cz_s, yaw_s;
  static bool init;
  const actor_t *me = &G.a[0];
  float back = 4.0f, up = 2.2f, shoulder = 0.6f;   /* over the right shoulder, so the kid doesn't hide the reticle */
  if (me->squid) { back = 3.2f; up = 1.6f; shoulder = 0.3f; }
  float yaw = me->yaw;
  if (!init || G.clock < 0.05f) { cz_s = me->z; yaw_s = yaw; init = true; }
  yaw_s += wrap_angle(yaw - yaw_s) * 0.5f;   /* a little lag on turns */
  cz_s += (me->z - cz_s) * 0.3f;             /* and on steps */
  float fx = cosf(yaw_s), fy = sinf(yaw_s);
  /* pull in where a building is behind us */
  float camz = cz_s + up;
  for (float d = 0.4f; d <= back; d += 0.25f) {
    float x = me->x - fx * d - fy * shoulder, y = me->y - fy * d + fx * shoulder;
    if (ground_at(x, y) > camz - 0.4f || col_kind((int)floorf(x * 2), (int)floorf(y * 2)) == K_WALL) { back = d - 0.4f; break; }
  }
  if (back < 0.3f) back = 0.3f;
  C.x = me->x - fx * back - fy * shoulder;
  C.y = me->y - fy * back + fx * shoulder;
  C.z = camz;
  C.fx = fx; C.fy = fy;
  C.rx = -fy; C.ry = fx;   /* right-hand side (y grows to the right of +x when looking along +x) */
  C.hz = RH * 3 / 8;
}

static void draw_scene(void) {
  for (int r = 0; r < RH; r++) sky_row[r] = mix(SKY_TOP, SKY_LOW, r < C.hz ? r * 32 / (C.hz + 1) : 32);
  for (int c = 0; c < RW; c++) draw_column(c);
  /* kids, far to near */
  int order[N_ACTORS];
  float dist[N_ACTORS];
  for (int i = 0; i < N_ACTORS; i++) {
    order[i] = i;
    dist[i] = (G.a[i].x - C.x) * C.fx + (G.a[i].y - C.y) * C.fy;
  }
  for (int i = 1; i < N_ACTORS; i++)
    for (int j = i; j > 0 && dist[order[j]] > dist[order[j - 1]]; j--) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
  for (int i = 0; i < N_ACTORS; i++) draw_actor(&G.a[order[i]]);
  /* charger sights */
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || a->weapon != W_CHARGER || a->charge <= 0.05f || a->squid) continue;
    float r = 11 + 16 * a->charge, z = a->z + 1.0f, fx = cosf(a->yaw), fy = sinf(a->yaw), d = 0.5f;
    for (; d < r; d += 0.5f)
      if (col_kind((int)floorf((a->x + fx * d) * 2), (int)floorf((a->y + fy * d) * 2)) == K_WALL || ground_at(a->x + fx * d, a->y + fy * d) > z) break;
    seg3(a->x + fx * 0.5f, a->y + fy * 0.5f, z, a->x + fx * d, a->y + fy * d, z, a->charge >= 1 ? 0xFFFF : G.col[a->team], 2);
  }
  /* shots, drops, bombs */
  for (int i = 0; i < MAX_PROJ; i++) {
    const proj_t *p = &G.p[i];
    if (!p->used) continue;
    float sx, sy, t;
    if (!project(p->x, p->y, p->z, &sx, &sy, &t) || t > TMAX) continue;
    float s = FOCAL / t;
    if (p->kind == P_BOMB) {
      uint16_t c = p->landed && ((int)(p->fuse * 10) & 1) ? 0xFFFF : G.col[p->team];
      disc_d(sx, sy - 0.15f * s, 0.2f * s + 0.6f, c, dz(t));
    } else {
      float r = 0.1f * s + 0.4f;
      disc_d(sx, sy, r, mix(G.col[p->team], 0xFFFF, 8), dz(t));
    }
  }
  for (int i = 0; i < MAX_BEAM; i++) {
    const beam_t *b = &G.beam[i];
    if (b->t > 0) seg3(b->x0, b->y0, b->z0, b->x1, b->y1, b->z1, mix(G.col[b->team], 0xFFFF, 10), 0);
  }
  for (int i = 0; i < MAX_BLAST; i++) {
    const blast_t *b = &G.blast[i];
    if (b->t <= 0) continue;
    float sx, sy, t;
    if (!project(b->x, b->y, b->z + 0.3f, &sx, &sy, &t)) continue;
    float k = 1 - b->t / 0.3f, s = FOCAL / t;
    ellipse_d(sx, sy, b->r * s * (0.4f + 0.6f * k), b->r * s * (0.25f + 0.35f * k), G.col[b->team], dz(t - b->r), k > 0.5f);
  }
}

void render_frame(int fps, int ms, bool show_fps) {
  setup_camera();
  draw_scene();
  hud(fps, ms, show_fps);
}

/* Title screen: the camera circles the plaza. */
void render_orbit(float a) {
  C.x = 47 + cosf(a) * 30; C.y = 28 + sinf(a) * 18; C.z = 9;
  float fx = 47 - C.x, fy = 28 - C.y, l = sqrtf(fx * fx + fy * fy);
  C.fx = fx / l; C.fy = fy / l; C.rx = -C.fy; C.ry = C.fx;
  C.hz = RH / 6;
  for (int r = 0; r < RH; r++) sky_row[r] = mix(SKY_TOP, SKY_LOW, r < C.hz ? r * 32 / (C.hz + 1) : 32);
  for (int c = 0; c < RW; c++) draw_column(c);
}

/* Pause screen: the stage from above, 1.5 px per metre. */
void render_map(void) {
  frame_fill(0, 0, RW, RH, RGB(20, 40, 70));
  for (int y = 0; y < 84; y++)
    for (int x = 0; x < 141; x++) {
      int tx = x * 4 / 3, ty = y * 4 / 3, k = col_kind(tx, ty);
      if (k == K_WATER) continue;
      int h = (int)(col_top(tx, ty) * 2);
      uint16_t c = dim(map_colour(col_colour(tx, ty)), k == K_WALL ? 14 : 22 + (h > 8 ? 8 : h));
      int p = paint_get(tx * 2 + 1, ty * 2 + 1);
      if (p && k != K_WALL) c = G.col[p - 1];
      frame[18 + y][10 + x] = c;
    }
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    if (!a->alive || (a->team != G.a[0].team && !G.demo)) continue;
    int px = 10 + (int)(a->x * 1.5f), py = 18 + (int)(a->y * 1.5f);
    frame_fill(px - 1, py - 1, 3, 3, i == 0 ? 0xFFFF : RGB(20, 20, 30));
    frame[py][px] = G.col[a->team];
  }
  text_c(4, "PAUSED", 0xFFFF, 1);
  text_c(RH - 14, "OK: RESUME   BACK: QUIT", RGB(230, 230, 230), 1);
}

/* 2x up-scale, 8 low-res rows (16 screen rows) per push */
void frame_push(void) {
  uint16_t *strip = (uint16_t *)scratch;   /* 320 x 16 x 2 bytes = 10 KB */
  for (int y0 = 0; y0 < RH; y0 += 8) {
    for (int r = 0; r < 8; r++) {
      const uint16_t *src = frame[y0 + r];
      uint16_t *d0 = strip + r * 2 * 320, *d1 = d0 + 320;
      for (int x = 0; x < RW; x++) {
        uint16_t c = src[x];
        d0[2 * x] = d0[2 * x + 1] = c;
      }
      memcpy(d1, d0, 320 * 2);
    }
    eadk_display_push_rect((eadk_rect_t){0, (uint16_t)(y0 * 2), 320, 16}, strip);
  }
}
