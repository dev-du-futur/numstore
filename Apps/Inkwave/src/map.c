/* The stage (a heightfield of 0.5 m columns), the ink on it, sight lines and the bots' path search. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "map_data.h"

#define TW MAP_W   /* 188 columns along x */
#define TH MAP_H   /* 112 across */

uint8_t scratch[SCRATCH_BYTES] __attribute__((aligned(4)));
static uint8_t paint[PAINT_H][PAINT_W / 4];   /* 2 bits per cell: 0 none, 1 Alpha, 2 Bravo */

/* The stage's columns copied to RAM, packed: height | look << 8 (the arrays in map_data.h stay in the
 * calculator's external flash, which is slow to read at random; the renderer reads a column per ray step). */
static uint16_t colw[TH * TW];
static void cols_init(void) {
  for (int i = 0; i < TW * TH; i++) colw[i] = (uint16_t)(map_h[i] | map_look[i] << 8);
}
int col_kind(int tx, int ty) {
  if ((unsigned)tx >= TW || (unsigned)ty >= TH) return K_WATER;
  return colw[ty * TW + tx] >> 14;
}
float col_top(int tx, int ty) {
  if ((unsigned)tx >= TW || (unsigned)ty >= TH) return -1.6f;
  return (colw[ty * TW + tx] & 255) * 0.1f - 2.0f;
}
int col_colour(int tx, int ty) {
  if ((unsigned)tx >= TW || (unsigned)ty >= TH) return 0;
  return (colw[ty * TW + tx] >> 8) & 15;
}
uint16_t map_colour(int i) { return map_pal[i & 15]; }
int col_surface(int tx, int ty) {
  if ((unsigned)tx >= TW || (unsigned)ty >= TH) return 0;
  return (colw[ty * TW + tx] >> 12) & 3;
}
int col_light(int tx, int ty) {
  if ((unsigned)tx >= TW || (unsigned)ty >= TH) return 0;
  return map_light[ty * TW + tx];
}
const float *const SUN = map_sun;
float ground_at(float x, float y) { return col_top((int)floorf(x * 2), (int)floorf(y * 2)); }

/* ------------------------------------------------------------------ ink */
int paint_get(int px, int py) {
  if ((unsigned)px >= PAINT_W || (unsigned)py >= PAINT_H) return 0;
  return (paint[py][px >> 2] >> ((px & 3) * 2)) & 3;
}
static void paint_set(int px, int py, int v) {
  uint8_t *b = &paint[py][px >> 2];
  int sh = (px & 3) * 2;
  *b = (uint8_t)((*b & ~(3 << sh)) | (v << sh));
}
void paint_clear(void) {
  memset(paint, 0, sizeof paint);
  G.cells[0] = G.cells[1] = G.cells[2] = 0;
  G.paintable = 0;
  for (int ty = 0; ty < TH; ty++)
    for (int tx = 0; tx < TW; tx++) {
      int k = col_kind(tx, ty);
      if (k == K_FLOOR || k == K_RAIL) G.paintable += 4;
    }
  G.cells[0] = G.paintable;
}
int paint_at(float x, float y) { return paint_get((int)floorf(x * 4), (int)floorf(y * 4)); }

static uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

/* A splat: the cells within r of (x, y) whose column top is near z (so ink doesn't land on a roof above
 * or a street below). The edge is a little ragged. Returns the m² that became this team's. */
float paint_disc(float x, float y, float z, float r, int team) {
  int x0 = (int)floorf((x - r) * 4), x1 = (int)floorf((x + r) * 4);
  int y0 = (int)floorf((y - r) * 4), y1 = (int)floorf((y + r) * 4);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= PAINT_W) x1 = PAINT_W - 1;
  if (y1 >= PAINT_H) y1 = PAINT_H - 1;
  float r2 = r * r * 16;
  int v = team + 1, gained = 0;
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - y * 4;
    for (int px = x0; px <= x1; px++) {
      float dx = px + 0.5f - x * 4;
      float d2 = dx * dx + dy * dy;
      if (d2 > r2) continue;
      if (d2 > r2 * 0.6f && (hash2(px, py) & 255) < (d2 / r2 - 0.6f) * 500) continue;   /* ragged rim */
      int tx = px >> 1, ty = py >> 1, k = col_kind(tx, ty);
      if (k != K_FLOOR && k != K_RAIL) continue;
      if (fabsf(col_top(tx, ty) - z) > 0.7f) continue;
      int old = paint_get(px, py);
      if (old == v) continue;
      G.cells[old]--;
      G.cells[v]++;
      paint_set(px, py, v);
      gained++;
    }
  }
  return gained * 0.0625f;
}

/* Share of the ink-able cells around (x, y) (sampled) that are this team's / the other team's. */
float paint_share(float x, float y, float r, int team, float *enemy) {
  int own = 0, en = 0, n = 0;
  for (int i = -2; i <= 2; i++)
    for (int j = -2; j <= 2; j++) {
      float sx = x + i * r * 0.5f, sy = y + j * r * 0.5f;
      int k = col_kind((int)floorf(sx * 2), (int)floorf(sy * 2));
      if (k != K_FLOOR) continue;
      int p = paint_at(sx, sy);
      n++;
      if (p == team + 1) own++;
      else if (p) en++;
    }
  if (!n) { if (enemy) *enemy = 0; return 1; }
  if (enemy) *enemy = (float)en / n;
  return (float)own / n;
}

/* Nothing taller than the line between the two points (sampled every 0.25 m). */
bool line_clear(float x0, float y0, float z0, float x1, float y1, float z1) {
  float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
  int n = (int)(d * 4) + 1;
  for (int i = 1; i < n; i++) {
    float t = (float)i / n;
    float x = x0 + dx * t, y = y0 + dy * t;
    int tx = (int)floorf(x * 2), ty = (int)floorf(y * 2);
    if (col_kind(tx, ty) == K_WALL || col_top(tx, ty) > z0 + (z1 - z0) * t) return false;
  }
  return true;
}

void spawn_point(int team, float *x, float *y) {
  *x = (map_spawn[team][0] + 0.5f) * 0.5f;
  *y = (map_spawn[team][1] + 0.5f) * 0.5f;
}

/* ------------------------------------------------------------------ navigation (1 m cells) */
/* nav_h: the cell's height in 0.1 m (from -2 m), or 255 where a kid can't stand (a wall, rail or water
 * in any of its four columns, or a cell that straddles a ledge). */
static uint8_t nav_h[NAV_H][NAV_W];
static uint8_t nav_from[NAV_H][NAV_W];   /* path search: the step that reached the cell (0..7), 255 unseen */
static const int8_t DX8[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

void nav_init(void) {
  cols_init();
  for (int ny = 0; ny < NAV_H; ny++)
    for (int nx = 0; nx < NAV_W; nx++) {
      int lo = 255, hi = 0, ok = 1;
      for (int k = 0; k < 4; k++) {
        int tx = nx * 2 + (k & 1), ty = ny * 2 + (k >> 1);
        if (col_kind(tx, ty) != K_FLOOR) ok = 0;
        int h = map_h[ty * TW + tx];
        if (h < lo) lo = h;
        if (h > hi) hi = h;
      }
      nav_h[ny][nx] = ok && hi - lo <= 6 ? (uint8_t)hi : 255;
    }
}
bool nav_ok(int nx, int ny) {
  return (unsigned)nx < NAV_W && (unsigned)ny < NAV_H && nav_h[ny][nx] != 255;
}
static bool nav_step(int ax, int ay, int d) {   /* can a kid go from cell a one step in direction d? */
  int bx = ax + DX8[d], by = ay + DY8[d];
  if (!nav_ok(bx, by)) return false;
  if (nav_h[by][bx] > nav_h[ay][ax] + 8) return false;   /* up a ledge (dropping down is fine) */
  if (d >= 4) {   /* diagonal: both sides must be open, so the step doesn't clip a corner */
    if (!nav_ok(ax + DX8[d], ay) || !nav_ok(ax, ay + DY8[d])) return false;
  }
  return true;
}
static bool nav_nearest(float x, float y, int *nx, int *ny) {
  int cx = (int)x, cy = (int)y;
  for (int r = 0; r < 4; r++)
    for (int j = -r; j <= r; j++)
      for (int i = -r; i <= r; i++) {
        if (i != -r && i != r && j != -r && j != r) continue;
        if (nav_ok(cx + i, cy + j)) { *nx = cx + i; *ny = cy + j; return true; }
      }
  return false;
}

/* Breadth-first search over the nav cells. Writes up to max waypoints (x | y << 8), the goal last, and
 * returns how many (0: no way there). */
int nav_path(float x0, float y0, float x1, float y1, uint16_t *out, int max) {
  int sx, sy, gx, gy;
  if (!nav_nearest(x0, y0, &sx, &sy) || !nav_nearest(x1, y1, &gx, &gy)) return 0;
  uint16_t *q = (uint16_t *)scratch;   /* NAV_W * NAV_H entries fit in the scratch area */
  memset(nav_from, 255, sizeof nav_from);
  int head = 0, tail = 0;
  q[tail++] = (uint16_t)(sx | sy << 8);
  nav_from[sy][sx] = 8;
  bool found = sx == gx && sy == gy;
  while (head < tail && !found) {
    int c = q[head++], cx = c & 255, cy = c >> 8;
    for (int d = 0; d < 8; d++) {
      int nx = cx + DX8[d], ny = cy + DY8[d];
      if ((unsigned)nx >= NAV_W || (unsigned)ny >= NAV_H || nav_from[ny][nx] != 255) continue;
      if (!nav_step(cx, cy, d)) continue;
      nav_from[ny][nx] = (uint8_t)d;
      if (nx == gx && ny == gy) { found = true; break; }
      q[tail++] = (uint16_t)(nx | ny << 8);
    }
  }
  if (!found) return 0;
  /* walk back from the goal, then reverse */
  int n = 0, cx = gx, cy = gy;
  uint16_t *rev = q;   /* the queue is no longer needed */
  while (nav_from[cy][cx] != 8 && n < NAV_W * NAV_H) {
    rev[n++] = (uint16_t)(cx | cy << 8);
    int d = nav_from[cy][cx];
    cx -= DX8[d];
    cy -= DY8[d];
  }
  if (n == 0) rev[n++] = (uint16_t)(gx | gy << 8);
  int m = n < max ? n : max;   /* keep the first steps if the path is long: it is re-planned as it goes */
  for (int i = 0; i < m; i++) out[i] = rev[n - 1 - i];
  return m;
}
