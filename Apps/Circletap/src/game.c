/* Circle Tap rules, after game.js / game-renderer.js / utils.js of "Circle Tap Challenge". */
#include "game.h"
#include <math.h>
#include <string.h>

const difficulty_t DIFF[D_COUNT] = {
  {1.2f * PI_F, 0.4f, 0.12f},
  {1.5f * PI_F, 0.25f, 0.07f},
  {1.8f * PI_F, 0.15f, 0.035f},
};
const char *const DIFF_NAME[D_COUNT] = {"Easy", "Medium", "Hard"};

game_t G;

static uint32_t rs = 0x9E3779B9u;
void rnd_seed(uint32_t s) { rs = s ? s : 0x9E3779B9u; }
float rnd(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return (rs >> 8) * (1.0f / 16777216.0f);
}

static float hue2rgb(float p, float q, float t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0f / 6) return p + (q - p) * 6 * t;
  if (t < 0.5f) return q;
  if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
  return p;
}
uint32_t hsl_rgb(hsl_t c) {
  float h = fmodf(c.h, 360) / 360;
  if (h < 0) h += 1;
  float s = c.s, l = c.l;
  float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  int r = (int)(hue2rgb(p, q, h + 1.0f / 3) * 255 + 0.5f), g = (int)(hue2rgb(p, q, h) * 255 + 0.5f),
      b = (int)(hue2rgb(p, q, h - 1.0f / 3) * 255 + 0.5f);
  return RGB(r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b);
}
hsl_t level_hsl(int level) { return (hsl_t){(float)(((level - 1) * 40) % 360), 1.0f, 0.6f}; }

static float norm_angle(float a) {
  a = fmodf(a, TWO_PI_F);
  return a < 0 ? a + TWO_PI_F : a;
}
bool angle_in_arc(float a, float start, float size) {
  float n = norm_angle(a), s = norm_angle(start), e = fmodf(s + size, TWO_PI_F);
  return s < e ? (n >= s && n <= e) : (n >= s || n <= e);
}

void path_point(const map_t *m, float progress, float *x, float *y, float *ang) {
  progress = fmodf(fmodf(progress, 1) + 1, 1);
  float target = progress * m->length, cur = 0;
  const float *p = m->xy;
  for (int i = 0; i < m->n - 1; i++) {
    float dx = p[2 * i + 2] - p[2 * i], dy = p[2 * i + 3] - p[2 * i + 1], d = sqrtf(dx * dx + dy * dy);
    if (cur + d >= target) {
      float t = d > 0 ? (target - cur) / d : 0;
      *x = p[2 * i] + dx * t;
      *y = p[2 * i + 1] + dy * t;
      if (ang) *ang = atan2f(dy, dx);
      return;
    }
    cur += d;
  }
  int l = m->n - 1;
  *x = p[2 * l];
  *y = p[2 * l + 1];
  if (ang) *ang = atan2f(p[2 * l + 1] - p[2 * l - 1], p[2 * l] - p[2 * l - 2]);
}

void track_point(float angle, float *x, float *y) {
  if (G.map) path_point(G.map, angle / TWO_PI_F, x, y, 0);
  else { *x = cosf(angle); *y = sinf(angle); }
}

static void new_target(void) { G.target_start = rnd() * TWO_PI_F; }

static void reset_fx(void) {
  memset(G.p, 0, sizeof G.p);
  memset(G.pop, 0, sizeof G.pop);
  G.flash.on = false;
  G.heat = 0;
}

void game_init(int diff) {
  memset(&G, 0, sizeof G);
  G.angle = -PI_F / 2;
  G.dir = 1;
  G.level = 1;
  G.taps_next = 1;
  G.col = G.col_to = level_hsl(1);
  game_set_difficulty(diff);
}

void game_set_difficulty(int diff) {
  G.diff = diff;
  G.target_size = DIFF[diff].size;
  G.speed = DIFF[diff].speed;
  if (G.state == GS_START) new_target();
}

void game_reset(void) {
  G.state = GS_START;
  G.score = 0;
  G.level = 1;
  G.taps_level = 0;
  G.taps_next = 1;
  G.fail_on = false;
  G.col = level_hsl(1);   /* as the original: only the shown colour, so it eases back to the last level's */
  game_set_difficulty(G.diff);
  G.angle = -PI_F / 2;
}

void game_start(int mode, int diff, const map_t *map) {
  G.mode = mode;
  G.map = map;
  G.diff = diff;
  G.speed = DIFF[diff].speed;
  G.target_size = DIFF[diff].size;
  G.score = 0;
  G.angle = -PI_F / 2;
  G.dir = 1;
  G.level = 1;
  G.taps_level = 0;
  G.taps_next = 1;
  G.fail_on = false;
  G.col = G.col_to = level_hsl(1);
  G.max_lives = G.lives = map ? (map->lives ? map->lives : 1) : 0;
  new_target();
  reset_fx();
  G.state = GS_PLAYING;
}

void game_quit(void) {
  G.state = GS_GAMEOVER;
}

static void spawn_hit_fx(void) {
  float x, y;
  track_point(G.angle, &x, &y);
  uint32_t c = hsl_rgb(G.col);
  int n = 0;
  for (int i = 0; i < N_PARTICLES && n < 10; i++) {
    particle_t *p = &G.p[i];
    if (p->on) continue;
    p->on = true;
    p->x = x;
    p->y = y;
    float d = rnd() * TWO_PI_F, sp = 0.35f + rnd() * 0.85f;
    p->vx = cosf(d) * sp;
    p->vy = sinf(d) * sp;
    p->max = p->life = 0.35f + rnd() * 0.25f;
    p->size = 0.08f + rnd() * 0.1f;   /* in track widths */
    p->rgb = rnd() < 0.35f ? 0xFFFFFF : c;
    n++;
  }
  for (int i = 0; i < N_POPS; i++)
    if (!G.pop[i].on) {
      G.pop[i] = (pop_t){true, x, y, 0.7f, 0.7f};
      break;
    }
  G.flash = (flash_t){true, G.map == 0, 0.3f, 0.3f, G.target_start, G.target_size, x, y};
  G.heat = fminf(1, G.heat + 0.34f);
}

int game_tap(void) {
  if (G.state != GS_PLAYING) return TAP_NONE;
  /* atan2(lineWidth / 2.5, radius): lineWidth is 2.5 x track x radius, so this is atan(track) */
  float tol = atanf(DIFF[G.diff].track);
  if (angle_in_arc(G.angle, G.target_start - tol, G.target_size + 2 * tol)) {
    G.score++;
    spawn_hit_fx();
    int r = TAP_HIT;
    if (++G.taps_level >= G.taps_next) {
      G.col_to = level_hsl(G.level + 1);
      G.level++;
      G.taps_level = 0;
      G.taps_next = G.level;
      r = TAP_LEVELUP;
    }
    G.dir = -G.dir;
    G.speed *= 1.04f;
    new_target();
    return r;
  }
  G.heat = 0;
  G.fail_on = true;
  G.fail_angle = G.angle;
  G.fail_age = 0;
  if (G.mode == M_ZEN) {
    G.score = G.score > 5 ? G.score - 5 : 0;
    G.speed = fmaxf(DIFF[G.diff].speed, G.speed / 1.1f);
    if (G.level > 1) {
      G.level--;
      G.taps_level = 0;
      G.taps_next = G.level;
      G.col_to = level_hsl(G.level);
    }
    new_target();
    return TAP_MISS;
  }
  if (G.mode == M_MAP && --G.lives > 0) {
    new_target();
    return TAP_MISS;
  }
  G.state = GS_GAMEOVER;
  return TAP_GAMEOVER;
}

void game_update(float dt) {
  float k = fminf(dt * 2.5f, 1);
  G.col.h += (G.col_to.h - G.col.h) * k;
  G.col.s += (G.col_to.s - G.col.s) * k;
  G.col.l += (G.col_to.l - G.col.l) * k;
  if (G.state == GS_PLAYING) G.angle += G.speed * G.dir * dt;
  else G.angle += DIFF[G.diff].speed * dt;   /* idle spin in the menus */
  if (G.angle > 1000 || G.angle < -1000) G.angle = norm_angle(G.angle);
  if (G.fail_on && (G.fail_age += dt) >= 3) G.fail_on = false;

  float fdt = fminf(dt, 0.05f);
  if (G.heat > 0) G.heat = fmaxf(0, G.heat - fdt * 0.45f);
  if (G.flash.on && (G.flash.life -= fdt) <= 0) G.flash.on = false;
  for (int i = 0; i < N_PARTICLES; i++) {
    particle_t *p = &G.p[i];
    if (!p->on) continue;
    if ((p->life -= fdt) <= 0) { p->on = false; continue; }
    p->x += p->vx * fdt;
    p->y += p->vy * fdt;
  }
  for (int i = 0; i < N_POPS; i++)
    if (G.pop[i].on && (G.pop[i].life -= fdt) <= 0) G.pop[i].on = false;
}
