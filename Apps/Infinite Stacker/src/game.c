/* Infinite Stacker rules, after the original game.js / game-objects.js:
 * a block slides back and forth (x, then z, then x...) between -2 and 2 at 0.03 + 0.001 x (blocks) units per
 * frame of 60 Hz; dropping it keeps the part over the block below and cuts off the rest, which falls. A drop
 * within 5 % of the block below is perfect: it snaps into place with nothing cut. 5 perfects in a row turn on
 * golden mode (gold blocks that grow 5 % on each perfect, up to 2 x 2) for 5 more perfects or until a miss.
 * Physics (cannon-es in the original) is a small 2D rigid-box solver per falling piece. */
#include "game.h"
#include <math.h>
#include <string.h>

game_t G;

static int64_t ticks_done;   /* 60 Hz ticks run since the app started */

static uint32_t rnd(void) {
  uint32_t x = G.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return G.rng = x;
}
static float frand(void) { return (rnd() >> 8) * (1.0f / 16777216.0f); }

static float srgb_to_lin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }

/* hsl(h, 70%, 50%) as three.js turns it into linear RGB */
static void block_colour(int index, bool golden, float out[3]) {
  if (golden) {   /* 0xFFD700 */
    out[0] = 1;
    out[1] = srgb_to_lin(215 / 255.0f);
    out[2] = 0;
    return;
  }
  float h = fmodf(index * 8.0f, 360.0f) / 360.0f, s = 0.7f, l = 0.5f;
  float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  for (int i = 0; i < 3; i++) {
    float t = h + (1 - i) / 3.0f;   /* r: h + 1/3, g: h, b: h - 1/3 */
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    float c = t < 1 / 6.0f ? p + (q - p) * 6 * t : t < 0.5f ? q : t < 2 / 3.0f ? p + (q - p) * 6 * (2 / 3.0f - t) : p;
    out[i] = srgb_to_lin(c);
  }
}

static void push_block(float x, float z, float w, float d) {
  block_t *b = blk(G.n);
  b->x = x;
  b->z = z;
  b->w = w;
  b->d = d;
  block_colour(G.n, G.golden, b->col);
  b->flash_at = -1;
  G.n++;
  G.version++;
}

static void spawn(void) {
  block_t *prev = blk(G.n - 1);
  G.adir = G.n % 2 == 0 ? 1 : 0;
  G.aw = prev->w;
  G.ad = prev->d;
  G.ax = G.adir == 0 ? -2 : prev->x;
  G.az = G.adir == 1 ? -2 : prev->z;
  block_colour(G.n, G.golden, G.acol);
  G.aspeed = 0.03f + G.n * 0.001f; /* the original starts at 0.02: too slow on the calculator */
  G.aspawn = G.now;
  G.active = true;
}

void game_reset(int32_t now) {
  uint32_t keep = G.rng ? G.rng : 0x9E3779B9u;
  uint32_t v = G.version + 1;
  memset(&G, 0, sizeof G);
  G.rng = keep;
  G.version = v;
  G.now = now;
  G.state = GS_IDLE;
  G.camY = 4;
  G.lookY = 0;
  G.top = 2;
  G.streak_at = G.hide_streak_at = -1;
  G.over_at = -1;
  push_block(0, 0, 1, 1);   /* the base: 1 x 1, 20 tall */
}

void game_start(int32_t now) {
  game_reset(now);
  spawn();
  G.state = GS_PLAYING;
}

float game_active_pos(int32_t t) {
  float travelled = G.aspeed * 60 * (t > G.aspawn ? t - G.aspawn : 0) / 1000.0f;
  float phase = fmodf(travelled, 8);
  return -2 + (phase <= 4 ? phase : 8 - phase);
}

static void streak_label(int count) {
  G.streak_count = count;
  G.streak_golden = G.golden;
  G.streak_at = G.now;
  G.hide_streak_at = -1;
}
static void hide_streak(void) {
  if (G.streak_at >= 0 && G.hide_streak_at < G.streak_at) G.hide_streak_at = G.now;
}
static void reset_streaks(void) {
  G.perfect_streak = G.golden_streak = 0;
  G.golden = false;
  hide_streak();
}

static void sparkle_burst(spark_t *pool, int n, const block_t *b, float topY, bool milestone) {
  for (int i = 0; i < n; i++) {
    spark_t *s = &pool[i];
    float a = frand() * 6.2831853f, dx = cosf(a), dz = sinf(a);
    s->p[0] = b->x + dx * b->w / 2;
    s->p[1] = topY;
    s->p[2] = b->z + dz * b->d / 2;
    float sp = milestone ? 0.006f + frand() * 0.012f : 0.012f + frand() * 0.018f;
    s->v[0] = dx * sp;
    s->v[1] = milestone ? 0.045f + frand() * 0.03f : 0.02f + frand() * 0.02f;
    s->v[2] = dz * sp;
    s->life = s->maxlife = milestone ? 40 + (int)(frand() * 20) : 24 + (int)(frand() * 14);
    s->base = milestone ? 0.9f + frand() * 0.8f : 0.7f + frand() * 0.6f;
  }
}

static body_t *new_body(void) {
  int oldest = 0;
  for (int i = 0; i < MAX_BODIES; i++) {
    if (!G.bodies[i].used) return &G.bodies[i];
    if (G.bodies[i].born < G.bodies[oldest].born) oldest = i;
  }
  return &G.bodies[oldest];
}

static void game_over(void) {
  if (G.state == GS_OVER) return;
  G.state = GS_OVER;
  reset_streaks();
  if (G.active) {   /* the block that missed falls, pushed along +x */
    body_t *b = new_body();
    memset(b, 0, sizeof *b);
    b->used = true;
    b->axis = G.adir;
    b->y = BLOCK_H * G.n;
    b->hy = BLOCK_H / 2;
    if (G.adir == 0) {
      b->u = G.ax; b->w = G.az; b->hu = G.aw / 2; b->hw = G.ad / 2;
      b->vu = G.aspeed * 50;
    } else {
      b->u = G.az; b->w = G.ax; b->hu = G.ad / 2; b->hw = G.aw / 2;
      b->vw = G.aspeed * 50;
    }
    memcpy(b->col, G.acol, sizeof b->col);
    b->born = G.now;
    G.active = false;
  }
  float tower = game_score() * BLOCK_H;
  if (tower > G.top * 2 * 0.9f) {   /* zoom out to show the whole tower */
    G.anim = true;
    G.anim_at = G.now;
    G.a0y = G.camY;
    G.a0top = G.top;
    G.a0look = G.lookY;
    G.a1top = (tower + 1.5f) / 2;
    G.a1y = tower / 2 + 4;
    G.a1look = tower / 2;
    G.over_at = G.now + 1500 + 500;
  } else {
    G.over_at = G.now + 1000;
  }
}

void game_press(int32_t t) {
  if (G.state != GS_PLAYING || !G.active) return;
  if (t > G.now) t = G.now;
  float pos = game_active_pos(t);
  if (G.adir == 0) G.ax = pos; else G.az = pos;

  block_t *prev = blk(G.n - 1);
  float px = prev->x, pz = prev->z, pw = prev->w, pd = prev->d;
  int dir = G.adir;
  float apos = dir == 0 ? G.ax : G.az, ppos = dir == 0 ? px : pz, psize = dir == 0 ? pw : pd;
  float overlap = psize - fabsf(apos - ppos);
  float side = apos > ppos ? 1.0f : -1.0f;
  if (overlap <= 0) {
    reset_streaks();
    game_over();
    return;
  }
  bool perfect = overlap >= psize * 0.95f;
  float nx = px, nz = pz, nw = pw, nd = pd;
  if (!perfect) {
    if (dir == 0) { nw = overlap; nx = (G.ax + px) / 2; }
    else { nd = overlap; nz = (G.az + pz) / 2; }
  }
  float ay = BLOCK_H * G.n;
  push_block(nx, nz, nw, nd);
  block_t *placed = blk(G.n - 1);
  memcpy(placed->col, G.acol, sizeof placed->col);   /* same colour as the sliding block */

  if (!perfect) {   /* the cut-off part */
    float ow = psize - overlap;
    if (ow >= 0.01f) {
      body_t *b = new_body();
      memset(b, 0, sizeof *b);
      b->used = true;
      b->axis = dir;
      b->hu = ow / 2;
      b->hy = BLOCK_H / 2;
      b->hw = (dir == 0 ? pd : pw) / 2;
      b->u = (dir == 0 ? nx : nz) + overlap / 2 * side + ow / 2 * side;
      b->w = dir == 0 ? nz : nx;
      b->y = ay;
      b->vu = G.aspeed * 40 * side;
      b->vy = -1;
      memcpy(b->col, G.acol, sizeof b->col);
      b->born = G.now;
    }
  }

  /* streaks */
  if (perfect) {
    int count;
    if (G.golden) {
      count = ++G.golden_streak;
      if (G.golden_streak >= 5) reset_streaks();
    } else {
      count = ++G.perfect_streak;
      if (G.perfect_streak >= 5) {
        G.perfect_streak = 0;
        G.golden = true;
      }
    }
    streak_label(count);
  } else {
    reset_streaks();
  }

  if (perfect && G.golden) {   /* golden mode: the block grows 5 % (the mode as it is after this drop) */
    placed->w = fminf(placed->w * 1.05f, 2);
    placed->d = fminf(placed->d * 1.05f, 2);
  }
  float topY = ay + BLOCK_H / 2;
  if (perfect) {
    placed->flash_at = G.now;
    sparkle_burst(G.spark, N_SPARK, placed, topY, false);
  }
  int score = G.n - 1;
  if (score > 0 && score % 10 == 0) {
    placed->flash_at = G.now;
    sparkle_burst(G.mspark, N_MSPARK, placed, topY, true);
  }
  G.active = false;
  spawn();
}

/* ------------------------------------------------------------------ falling pieces */
#define GRAV 9.82f
#define REST 0.3f
#define FRIC 0.4f

typedef struct { float pu, py, nu, ny, depth; } contact_t;
#define EPS 1e-3f

static void shatter(body_t *b) {
  float p[3];
  if (b->axis == 0) { p[0] = b->u; p[2] = b->w; } else { p[0] = b->w; p[2] = b->u; }
  p[1] = b->y;
  for (int k = 0; k < 20; k++) {
    part_t *q = NULL;
    for (int i = 0; i < MAX_PARTS; i++)
      if (G.parts[i].life <= 0) { q = &G.parts[i]; break; }
    if (!q) return;
    q->size = frand() * 0.08f + 0.04f;
    memcpy(q->p, p, sizeof p);
    float a = frand() * 6.2831853f, sp = frand() * 3 + 2;
    q->v[0] = cosf(a) * sp;
    q->v[1] = frand() * 2 + 1;
    q->v[2] = sinf(a) * sp;
    memcpy(q->col, b->col, sizeof q->col);
    q->life = 120;
  }
}

static int contacts_with(const body_t *b, float c, float s, float su0, float su1, float sy0, float sy1, contact_t *out, int n) {
  float ex[2] = {c * b->hu, s * b->hu}, ey[2] = {-s * b->hy, c * b->hy};   /* box axes in (u, y) */
  /* box corners inside the static rectangle */
  for (int k = 0; k < 4; k++) {
    float a = k & 1 ? 1 : -1, d = k & 2 ? 1 : -1;
    float pu = b->u + a * ex[0] + d * ey[0], py = b->y + a * ex[1] + d * ey[1];
    if (pu < su0 - EPS || pu > su1 + EPS || py <= sy0 || py >= sy1) continue;   /* flush edges count */
    float l = pu - su0, r = su1 - pu, bo = py - sy0, t = sy1 - py;
    if (l < EPS) l = 1e9f;   /* a corner flush with a side rests on the top (or hits the bottom) */
    if (r < EPS) r = 1e9f;
    contact_t *ct = &out[n++];
    ct->pu = pu; ct->py = py;
    if (t <= l && t <= r && t <= bo) { ct->nu = 0; ct->ny = 1; ct->depth = t; }
    else if (l <= r && l <= bo) { ct->nu = -1; ct->ny = 0; ct->depth = l; }
    else if (r <= bo) { ct->nu = 1; ct->ny = 0; ct->depth = r; }
    else { ct->nu = 0; ct->ny = -1; ct->depth = bo; }
  }
  /* static corners inside the box */
  for (int k = 0; k < 4; k++) {
    float pu = k & 1 ? su1 : su0, py = k & 2 ? sy1 : sy0;
    float du = pu - b->u, dy = py - b->y;
    float lu = du * c + dy * s, ly = -du * s + dy * c;   /* in box space */
    if (fabsf(lu) > b->hu + EPS || fabsf(ly) >= b->hy) continue;
    float pr = b->hu - lu, pl = b->hu + lu, pt = b->hy - ly, pb = b->hy + ly;
    if (pr < EPS) pr = 1e9f;
    if (pl < EPS) pl = 1e9f;
    float nlu, nly, dep;   /* outward normal of the box face nearest the corner */
    if (pb <= pt && pb <= pl && pb <= pr) { nlu = 0; nly = -1; dep = pb; }
    else if (pt <= pl && pt <= pr) { nlu = 0; nly = 1; dep = pt; }
    else if (pl <= pr) { nlu = -1; nly = 0; dep = pl; }
    else { nlu = 1; nly = 0; dep = pr; }
    contact_t *ct = &out[n++];
    ct->pu = pu; ct->py = py;
    ct->nu = -(nlu * c - nly * s);   /* the box is pushed away from the corner */
    ct->ny = -(nlu * s + nly * c);
    ct->depth = dep;
  }
  return n;
}

static void step_body(body_t *b, float dt) {
  b->vy -= GRAV * dt;
  float ld = powf(1 - 0.1f, dt), ad = powf(1 - 0.2f, dt);
  b->vu *= ld; b->vy *= ld; b->vw *= ld; b->angv *= ad;
  b->u += b->vu * dt;
  b->y += b->vy * dt;
  b->w += b->vw * dt;
  b->ang += b->angv * dt;

  float c = cosf(b->ang), s = sinf(b->ang);
  float ru = fabsf(c * b->hu) + fabsf(s * b->hy), ry = fabsf(s * b->hu) + fabsf(c * b->hy);
  contact_t ct[24];
  int n = 0;
  for (int i = G.n - 1; i >= 1 && i >= G.n - MAX_STACK + 1 && n <= 16; i--) {   /* the base has no body */
    float sy = layer_y(i);
    if (sy - BLOCK_H / 2 > b->y + ry) continue;
    if (sy + BLOCK_H / 2 < b->y - ry) break;   /* lower blocks are lower still */
    block_t *k = blk(i);
    float su = b->axis == 0 ? k->x : k->z, sh = (b->axis == 0 ? k->w : k->d) / 2;
    float sw = b->axis == 0 ? k->z : k->x, swh = (b->axis == 0 ? k->d : k->w) / 2;
    if (fabsf(sw - b->w) >= swh + b->hw) continue;
    if (fabsf(su - b->u) >= sh + ru) continue;
    n = contacts_with(b, c, s, su - sh, su + sh, sy - BLOCK_H / 2, sy + BLOCK_H / 2, ct, n);
  }
  if (!n) return;
  float I = ((2 * b->hu) * (2 * b->hu) + (2 * b->hy) * (2 * b->hy)) / 12;
  /* sequential impulses, accumulated and clamped per contact (so two corners on a floor share the load evenly) */
  float Pn[24], Pt[24], bias[24];
  for (int k = 0; k < n; k++) {
    float rx = ct[k].pu - b->u, ry2 = ct[k].py - b->y;
    float vn = (b->vu - b->angv * ry2) * ct[k].nu + (b->vy + b->angv * rx) * ct[k].ny;
    bias[k] = vn < -0.5f ? -REST * vn : 0;   /* bounce only real impacts */
    Pn[k] = Pt[k] = 0;
  }
  float Pw = 0;
  for (int it = 0; it < 10; it++) {
    float Ptot = 0;
    for (int k = 0; k < n; k++) {
      float rx = ct[k].pu - b->u, ry2 = ct[k].py - b->y;
      float vcu = b->vu - b->angv * ry2, vcy = b->vy + b->angv * rx;
      float vn = vcu * ct[k].nu + vcy * ct[k].ny;
      float rn = rx * ct[k].ny - ry2 * ct[k].nu;
      float j = (bias[k] - vn) / (1 + rn * rn / I);
      float old = Pn[k];
      Pn[k] = fmaxf(0, old + j);
      j = Pn[k] - old;
      float Ju = j * ct[k].nu, Jy = j * ct[k].ny;
      b->vu += Ju;
      b->vy += Jy;
      b->angv += (rx * Jy - ry2 * Ju) / I;
      /* friction along the contact */
      float tu = -ct[k].ny, ty = ct[k].nu;
      vcu = b->vu - b->angv * ry2;
      vcy = b->vy + b->angv * rx;
      float vt = vcu * tu + vcy * ty, rt = rx * ty - ry2 * tu;
      float jt = -vt / (1 + rt * rt / I);
      old = Pt[k];
      Pt[k] = fmaxf(-FRIC * Pn[k], fminf(FRIC * Pn[k], old + jt));
      jt = Pt[k] - old;
      b->vu += jt * tu;
      b->vy += jt * ty;
      b->angv += (rx * jt * ty - ry2 * jt * tu) / I;
      Ptot += Pn[k];
    }
    /* sliding along w: friction too */
    float old = Pw;
    Pw = fmaxf(-FRIC * Ptot, fminf(FRIC * Ptot, old - b->vw));
    b->vw += Pw - old;
  }
  float mu = 0, my = 0;   /* push out of the deepest overlap */
  for (int k = 0; k < n; k++) {
    float pu = ct[k].nu * ct[k].depth, py = ct[k].ny * ct[k].depth;
    if (fabsf(pu) > fabsf(mu)) mu = pu;
    if (fabsf(py) > fabsf(my)) my = py;
  }
  b->u += mu * 0.8f;
  b->y += my * 0.8f;
}

static void tick(void) {
  const float dt = 1 / 60.0f;
  /* camera follows the stack while playing */
  if (G.state == GS_PLAYING && G.active) {
    float target = BLOCK_H * G.n + 4;
    G.camY += (target - G.camY) * 0.1f;
    if (fabsf(target - G.camY) < 1e-4f) G.camY = target;
    G.lookY = G.n > 10 ? BLOCK_H * (G.n - 5) : BLOCK_H * G.n * 0.5f;
  }
  if (G.state == GS_OVER && G.anim) {
    float p = (G.now - G.anim_at) / 1500.0f;
    if (p >= 1) p = 1;
    p = 1 - powf(1 - p, 5);
    G.camY = G.a0y + (G.a1y - G.a0y) * p;
    G.lookY = G.a0look + (G.a1look - G.a0look) * p;
    G.top = G.a0top + (G.a1top - G.a0top) * p;
    if (G.now - G.anim_at >= 1500) G.anim = false;
  }
  for (int i = 0; i < MAX_BODIES; i++) {
    body_t *b = &G.bodies[i];
    if (!b->used) continue;
    for (int k = 0; k < 4; k++) step_body(b, dt / 4);
    if (b->y < -10) { b->used = false; continue; }
    if (G.now - b->born > 1500) {   /* in the original every piece still in view after 1.5 s shatters */
      shatter(b);
      b->used = false;
    }
  }
  float pd = powf(1 - 0.3f, dt);
  for (int i = 0; i < MAX_PARTS; i++) {
    part_t *q = &G.parts[i];
    if (q->life <= 0) continue;
    if (--q->life <= 0) continue;
    q->v[1] -= GRAV * dt;
    for (int k = 0; k < 3; k++) {
      q->v[k] *= pd;
      q->p[k] += q->v[k] * dt;
    }
    if (q->p[1] < -10) q->life = 0;
  }
  spark_t *pools[2] = {G.spark, G.mspark};
  int counts[2] = {N_SPARK, N_MSPARK};
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < counts[k]; i++) {
      spark_t *s = &pools[k][i];
      if (s->life <= 0) continue;
      if (--s->life <= 0) continue;
      s->v[1] -= 0.0025f;
      for (int a = 0; a < 3; a++) s->p[a] += s->v[a];
    }
  /* the label hides itself 1.5 s after it showed */
  if (G.streak_at >= 0 && G.hide_streak_at < G.streak_at && G.now - G.streak_at >= 1500) G.hide_streak_at = G.streak_at + 1500;
}

void game_update(int32_t now) {
  int64_t target = (int64_t)now * 60 / 1000;
  if (target - ticks_done > 15) ticks_done = target - 15;   /* after a long stall, catch up 0.25 s at most */
  if (target < ticks_done) ticks_done = target;            /* the clock restarted */
  while (ticks_done < target) {
    ticks_done++;
    G.now = (int32_t)(ticks_done * 1000 / 60);
    tick();
  }
  G.now = now;
  if (G.state == GS_PLAYING && G.active) {
    float p = game_active_pos(now);
    if (G.adir == 0) G.ax = p; else G.az = p;
  }
}
