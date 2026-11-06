/* The match: kids, their three weapons and the bomb, shots, ink and the clock.
 * Numbers from Inkwave's src/config.js (PLAYER, WEAPONS.shooter / roller / charger, SUBS.bomb, MATCH). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

game_t G;

const char *const WEAPON_NAME[W_COUNT] = {"Spritzer", "Swell Roller", "Glint Charger"};
const char *const DIFF_NAME[3] = {"Chill", "Fresh", "Fierce"};
/* DIFFICULTY in config.js: reaction (s), aim error (rad), awareness (m), aim turn rate cap (rad/s) */
const float DIFF_REACTION[3] = {0.55f, 0.32f, 0.17f}, DIFF_AIM_ERR[3] = {0.11f, 0.06f, 0.03f};
const float DIFF_AWARE[3] = {16, 21, 26}, DIFF_TURN[3] = {7, 10, 14};

static const struct { uint16_t a, b; const char *na, *nb; } PALETTES[5] = {
  {RGB(255, 138, 20), RGB(47, 91, 255), "Tangerine", "Cobalt"},
  {RGB(255, 63, 158), RGB(24, 212, 140), "Bubblegum", "Mint"},
  {RGB(242, 227, 18), RGB(138, 60, 255), "Lemon", "Grape"},
  {RGB(16, 210, 230), RGB(255, 65, 80), "Aqua", "Cherry"},
  {RGB(166, 240, 26), RGB(224, 44, 216), "Lime", "Magenta"},
};
static const char *const BOT_NAMES[20] = {
  "Squiddo", "Blotch", "Marlo", "Inky Vee", "Pip", "Coral", "Riptide", "Nori", "Suki", "Zest",
  "Kelp", "Drip", "Tako", "Sprinkle", "Bubbles", "Moxie", "Juno", "Wasabi", "Fizz", "Loop",
};

/* PLAYER */
#define HP 100.0f
#define RUN 6.0f
#define SQUID_DRY 2.9f
#define SWIM 11.8f
#define ENEMY_INK_SPEED 1.9f
#define INK_MAX 100.0f
#define REFILL_SWIM 42.0f
#define REFILL_KID 9.0f
#define REFILL_DELAY 0.9f
#define ENEMY_INK_DPS 20.0f
#define ENEMY_INK_CAP 40.0f
#define REGEN_DELAY 1.3f
#define REGEN 22.0f
#define REGEN_SWIM 60.0f
#define RESPAWN 5.5f
#define SPAWN_INVULN 1.6f
#define GRAVITY 25.0f
#define RADIUS 0.3f
#define SPAWN_BARRIER 4.2f
/* SUBS.bomb */
#define BOMB_INK 70.0f
#define BOMB_FUSE 0.95f
#define BOMB_RADIUS 3.1f
#define BOMB_DMG_MAX 180.0f
#define BOMB_DMG_MIN 35.0f
#define BOMB_PAINT 2.7f

/* ------------------------------------------------------------------ rng, maths */
static uint32_t rs = 0x9E3779B9u;
void rnd_seed(uint32_t s) { rs = s ? s : 1; }
uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
float frnd(void) { return (rnd() >> 8) * (1.0f / 16777216.0f); }
float frnd2(void) { return frnd() * 2 - 1; }
float wrap_angle(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}
float weapon_range(int w) { return w == W_SHOOTER ? 12.5f : w == W_ROLLER ? 6.0f : 27.0f; }
float team_share(int team) { return G.paintable ? (float)G.cells[team + 1] / G.paintable : 0; }

static void say(const char *s) {
  strncpy(G.msg, s, sizeof G.msg - 1);
  G.msg[sizeof G.msg - 1] = 0;
  G.msg_t = 2.5f;
}

/* ------------------------------------------------------------------ setup */
static void place(actor_t *a) {
  float sx, sy;
  spawn_point(a->team, &sx, &sy);
  int slot = a->idx % TEAM_SIZE;
  a->x = sx + (a->team ? -0.6f : 0.6f) * (slot & 1);
  a->y = sy + (slot - 1.5f) * 1.6f;
  a->z = ground_at(a->x, a->y);
  a->vx = a->vy = a->vz = 0;
  a->yaw = a->team ? PI_F : 0;
  a->pitch = 0;
  a->hp = HP;
  a->ink = INK_MAX;
  a->alive = true;
  a->squid = a->swim = a->rolling = false;
  a->invuln = SPAWN_INVULN;
  a->charge = a->windup = a->cd = 0;
  a->ink_dmg = 0;
  a->dmg_idle = a->ink_idle = 10;
}

void game_new(int weapon, int difficulty, bool demo) {
  memset(&G, 0, sizeof G);
  G.demo = demo;
  G.difficulty = difficulty;
  int pal = rnd() % 5;
  bool swap = rnd() & 1;
  G.col[0] = swap ? PALETTES[pal].b : PALETTES[pal].a;
  G.col[1] = swap ? PALETTES[pal].a : PALETTES[pal].b;
  G.team_name[0] = swap ? PALETTES[pal].nb : PALETTES[pal].na;
  G.team_name[1] = swap ? PALETTES[pal].na : PALETTES[pal].nb;
  for (int t = 0; t < 2; t++) {
    uint16_t c = G.col[t];
    uint32_t A = (c | (uint32_t)c << 16) & 0x07E0F81Fu;
    uint32_t r = ((A * 26u) >> 5) & 0x07E0F81Fu;
    G.col_dark[t] = (uint16_t)(r | r >> 16);
  }
  paint_clear();
  int used = 0;
  for (int i = 0; i < N_ACTORS; i++) {
    actor_t *a = &G.a[i];
    a->idx = i;
    a->team = i / TEAM_SIZE;
    a->bot = demo || i != 0;
    if (i == 0 && !demo) { a->weapon = weapon; a->name = "You"; }
    else {
      float r = frnd();
      a->weapon = r < 0.45f ? W_SHOOTER : r < 0.75f ? W_ROLLER : W_CHARGER;
      int n;
      do n = rnd() % 20; while (used >> n & 1);
      used |= 1 << n;
      a->name = BOT_NAMES[n];
    }
    place(a);
  }
  G.time_left = MATCH_TIME;
  G.phase = PH_INTRO;
  bots_reset();
}

/* ------------------------------------------------------------------ damage */
static void splat_fx(float x, float y, float z, float r, int team, actor_t *by) {
  float m2 = paint_disc(x, y, z, r, team);
  if (by) by->turf += m2;
}

static void drops(float x, float y, float z, int team, int n) {   /* splash droplets, for the eye only */
  for (int i = 0; i < MAX_PART && n > 0; i++) {
    part_t *q = &G.part[i];
    if (q->t > 0) continue;
    float an = frnd() * 2 * PI_F, sp = 1 + 2 * frnd();
    *q = (part_t){x, y, z + 0.05f, cosf(an) * sp, sinf(an) * sp, 2.5f + 2.5f * frnd(), 0.35f + 0.2f * frnd(), team};
    n--;
  }
}

static void hurt(actor_t *v, float dmg, actor_t *by) {
  if (!v->alive || v->invuln > 0 || G.phase != PH_PLAY) return;
  v->hp -= dmg;
  v->flash = 0.08f;
  v->dmg_idle = 0;
  v->last_hit = G.clock;
  if (by) v->last_attacker = by->idx;
  if (v->hp > 0) return;
  v->hp = 0;
  v->alive = false;
  v->respawn = RESPAWN;
  v->deaths++;
  v->charge = 0;
  int t = by ? by->team : 1 - v->team;
  splat_fx(v->x, v->y, v->z, 1.6f, t, by);
  if (by) by->splats++;
  char b[48];
  if (!G.demo && v->idx == 0) { snprintf(b, sizeof b, "Splatted by %s", by ? by->name : "the ink"); say(b); }
  else if (!G.demo && by && by->idx == 0) { snprintf(b, sizeof b, "You splatted %s", v->name); say(b); }
}

/* ------------------------------------------------------------------ movement */
static bool blocked(const actor_t *a, float x, float y) {
  static const float OX[5] = {0, -RADIUS, RADIUS, -RADIUS, RADIUS}, OY[5] = {0, -RADIUS, -RADIUS, RADIUS, RADIUS};
  for (int k = 0; k < 5; k++) {
    int tx = (int)floorf((x + OX[k]) * 2), ty = (int)floorf((y + OY[k]) * 2);
    int kind = col_kind(tx, ty);
    if (kind == K_WALL || kind == K_WATER) return true;
    if (kind == K_RAIL && !a->squid) return true;
    if (col_top(tx, ty) > a->z + STEP_UP) return true;
  }
  float sx, sy;   /* the other team's spawn is off-limits */
  spawn_point(1 - a->team, &sx, &sy);
  float dx = x - sx, dy = y - sy;
  return dx * dx + dy * dy < SPAWN_BARRIER * SPAWN_BARRIER;
}

static void move(actor_t *a, float dt) {
  float fx = cosf(a->yaw), fy = sinf(a->yaw);
  float mx = fx * a->in.fwd - fy * a->in.side, my = fy * a->in.fwd + fx * a->in.side;
  float ml = sqrtf(mx * mx + my * my);
  if (ml > 1) { mx /= ml; my /= ml; }
  int under = paint_at(a->x, a->y);
  bool enemy_ink = under && under != a->team + 1 && a->z - ground_at(a->x, a->y) < 0.1f;
  float speed;
  if (a->squid) speed = a->swim ? SWIM : SQUID_DRY;
  else if (a->weapon == W_CHARGER && a->charge > 0) speed = 1.8f;
  else if (a->rolling) speed = 4.4f;
  else if (a->in.fire && a->weapon == W_SHOOTER) speed = 4.6f;
  else speed = RUN;
  if (enemy_ink && speed > ENEMY_INK_SPEED) speed = ENEMY_INK_SPEED;
  float accel = a->swim ? 60 : 42, tvx = mx * speed, tvy = my * speed;
  float ex = tvx - a->vx, ey = tvy - a->vy, el = sqrtf(ex * ex + ey * ey), st = accel * dt;
  if (el > st) { ex *= st / el; ey *= st / el; }
  a->vx += ex;
  a->vy += ey;
  float nx = a->x + a->vx * dt, ny = a->y + a->vy * dt;
  if (!blocked(a, nx, a->y)) a->x = nx; else a->vx = 0;
  if (!blocked(a, a->x, ny)) a->y = ny; else a->vy = 0;
  float g = ground_at(a->x, a->y);
  if (g >= a->z) { a->z = g; a->vz = 0; }
  else {
    a->vz -= GRAVITY * dt;
    a->z += a->vz * dt;
    if (a->z < g) { a->z = g; a->vz = 0; }
  }
}

/* ------------------------------------------------------------------ weapons */
static proj_t *new_proj(void) {
  for (int i = 0; i < MAX_PROJ; i++) if (!G.p[i].used) { memset(&G.p[i], 0, sizeof G.p[i]); G.p[i].used = true; return &G.p[i]; }
  return 0;
}

/* the player gets a little help lining shots up (there is no second stick to aim with) */
static float assist(const actor_t *a, float yaw, float range) {
  if (a->bot) return yaw;
  float best = 0.16f, out = yaw;
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *e = &G.a[i];
    if (e->team == a->team || !e->alive || e->swim) continue;
    float dx = e->x - a->x, dy = e->y - a->y, d = sqrtf(dx * dx + dy * dy);
    if (d > range * 1.1f || d < 0.3f) continue;
    float da = fabsf(wrap_angle(atan2f(dy, dx) - yaw));
    if (da < best && line_clear(a->x, a->y, a->z + 1, e->x, e->y, e->z + 0.8f)) { best = da; out = atan2f(dy, dx); }
  }
  return out;
}

static void shoot(actor_t *a, int kind, float yaw, float speed, float range, float dmg, float dmg_far, float radius, float trail) {
  proj_t *p = new_proj();
  if (!p) return;
  float fx = cosf(yaw), fy = sinf(yaw);
  p->kind = (int8_t)kind;
  p->team = (int8_t)a->team;
  p->owner = (int8_t)a->idx;
  p->x = a->x + fx * 0.4f;
  p->y = a->y + fy * 0.4f;
  p->z = a->z + 1.0f;
  p->vx = fx * speed;
  p->vy = fy * speed;
  p->vz = speed * tanf(a->pitch);   /* aimed up or down */
  p->straight = range * 0.6f;
  float tdrop = range * 0.4f / speed;
  p->g = 2.0f / (tdrop * tdrop);   /* drops 1 m over the rest of its range: lands about `range` out */
  p->dmg = dmg;
  p->dmg_far = dmg_far;
  p->radius = radius;
  p->trail = trail;
}

static void fire_charger(actor_t *a) {
  float c = a->charge;
  float range = 11 + 16 * c, dmg = 40 + 120 * c;
  float yaw = assist(a, a->yaw, range);
  float fx = cosf(yaw), fy = sinf(yaw), z0 = a->z + 1.0f, sl = tanf(a->pitch), z = z0, d;
  actor_t *hit = 0;
  for (d = 0.5f; d < range; d += 0.25f) {
    float x = a->x + fx * d, y = a->y + fy * d;
    z = z0 + sl * d;
    int tx = (int)floorf(x * 2), ty = (int)floorf(y * 2);
    if (col_kind(tx, ty) == K_WALL || col_top(tx, ty) > z) break;
    for (int i = 0; i < N_ACTORS && !hit; i++) {
      actor_t *e = &G.a[i];
      if (e->team == a->team || !e->alive) continue;
      float ex = e->x - x, ey = e->y - y, top = e->z + (e->swim ? 0.3f : e->squid ? 0.7f : 1.5f);
      if (ex * ex + ey * ey < 0.45f * 0.45f && z > e->z - 0.2f && z < top + 0.3f) hit = e;
    }
    if (hit) break;
    /* the line inks the ground under it */
    if (fmodf(d, 1.2f) < 0.25f) { float g = ground_at(x, y); if (z - g < 2.6f) splat_fx(x, y, g, 0.55f, a->team, a); }
  }
  float ex = a->x + fx * d, ey = a->y + fy * d;
  if (hit) hurt(hit, dmg, a);
  else { float g = ground_at(ex, ey); if (z - g < 2.6f) splat_fx(ex, ey, g, 1.2f, a->team, a); }
  for (int i = 0; i < MAX_BEAM; i++)
    if (G.beam[i].t <= 0) { G.beam[i] = (beam_t){a->x + fx * 0.4f, a->y + fy * 0.4f, z0 + sl * 0.4f, ex, ey, z, 0.12f, a->team}; break; }
  a->ink -= 18 * (c > 0.15f ? c : 0.15f);
  a->ink_idle = 0;
  a->charge = 0;
  a->cd = 0.25f;
}

static void weapons(actor_t *a, float dt) {
  bool fire = a->in.fire && !a->squid && G.phase == PH_PLAY;
  bool pressed = fire && !a->fire_was;
  a->rolling = false;
  if (a->weapon == W_SHOOTER) {
    if (fire && a->cd <= 0 && a->ink >= 0.95f) {
      float yaw = assist(a, a->yaw, 12.5f) + frnd2() * (5.5f * PI_F / 180);
      shoot(a, P_SHOT, yaw, 34, 12.5f * (0.9f + 0.1f * frnd()), 36, 36, 0.85f, 1.05f);
      a->ink -= 0.95f;
      a->ink_idle = 0;
      a->cd += 0.1f;
      if (a->cd < 0) a->cd = 0.1f;
    }
  } else if (a->weapon == W_ROLLER) {
    if (pressed && a->cd <= 0 && a->windup <= 0 && a->ink >= 9) a->windup = 0.22f;
    if (a->windup > 0) {
      a->windup -= dt;
      if (a->windup <= 0) {   /* the flick: a fan of heavy drops */
        float yaw = assist(a, a->yaw, 6);
        for (int i = 0; i < 9; i++) {
          float off = (i / 8.0f - 0.5f) * (34 * PI_F / 180) + frnd2() * 0.03f;
          float sp = 17 * (0.75f + 0.25f * frnd());
          shoot(a, P_DROP, yaw + off, sp, 4.5f + 2.5f * frnd(), 125, 30, 1.0f, 0);
        }
        a->ink -= 9;
        a->ink_idle = 0;
        a->cd = 0.62f;
      }
    } else if (fire && a->cd < 0.4f && a->ink > 0.5f) {   /* still held: roll */
      float sp = sqrtf(a->vx * a->vx + a->vy * a->vy);
      a->rolling = true;
      if (sp > 0.5f) {
        float fx = cosf(a->yaw), fy = sinf(a->yaw), dist = sp * dt;
        a->roll_dist += dist;
        a->ink -= 1.1f * dist;
        a->ink_idle = 0;
        if (a->roll_dist > 0.25f) {
          a->roll_dist = 0;
          for (int k = -1; k <= 1; k++) {
            float x = a->x + fx * 0.7f - fy * 0.6f * k, y = a->y + fy * 0.7f + fx * 0.6f * k;
            splat_fx(x, y, a->z, 0.45f, a->team, a);
          }
        }
        for (int i = 0; i < N_ACTORS; i++) {   /* running someone over */
          actor_t *e = &G.a[i];
          if (e->team == a->team || !e->alive || a->hit_cd[i] > 0) continue;
          float dx = e->x - (a->x + fx * 0.7f), dy = e->y - (a->y + fy * 0.7f);
          if (dx * dx + dy * dy < 1.1f * 1.1f && fabsf(e->z - a->z) < 1) { hurt(e, 140, a); a->hit_cd[i] = 0.5f; }
        }
      }
    }
  } else {   /* charger */
    if (fire && a->cd <= 0 && (a->charge > 0 || a->ink >= 18 * 0.15f)) {
      a->charge += dt;
      if (a->charge > 1) a->charge = 1;
    } else if (!fire && a->charge > 0) {
      if (a->squid || G.phase != PH_PLAY) a->charge = 0;
      else fire_charger(a);
    }
  }
  /* bomb */
  if (a->in.bomb && !a->squid && a->bomb_cd <= 0 && a->ink >= BOMB_INK && G.phase == PH_PLAY) {
    proj_t *p = new_proj();
    if (p) {
      float fx = cosf(a->yaw), fy = sinf(a->yaw);
      p->kind = P_BOMB;
      p->team = (int8_t)a->team;
      p->owner = (int8_t)a->idx;
      p->x = a->x + fx * 0.4f; p->y = a->y + fy * 0.4f; p->z = a->z + 1.2f;
      p->vx = fx * 10.5f; p->vy = fy * 10.5f; p->vz = 5.5f + 10.5f * tanf(a->pitch);
      p->g = 24;
      a->ink -= BOMB_INK;
      a->ink_idle = 0;
      a->bomb_cd = 0.6f;
    }
  }
  a->fire_was = a->in.fire;
}

static void explode(proj_t *p) {
  actor_t *by = &G.a[p->owner];
  for (int i = 0; i < N_ACTORS; i++) {
    actor_t *e = &G.a[i];
    if (e->team == p->team || !e->alive) continue;
    float dx = e->x - p->x, dy = e->y - p->y, dzv = e->z + 0.6f - p->z, d = sqrtf(dx * dx + dy * dy + dzv * dzv);
    if (d < BOMB_RADIUS && line_clear(p->x, p->y, p->z + 0.5f, e->x, e->y, e->z + 0.8f))
      hurt(e, BOMB_DMG_MAX - (BOMB_DMG_MAX - BOMB_DMG_MIN) * d / BOMB_RADIUS, by);
  }
  splat_fx(p->x, p->y, p->z, BOMB_PAINT, p->team, by);
  drops(p->x, p->y, p->z, p->team, 10);
  for (int i = 0; i < MAX_BLAST; i++)
    if (G.blast[i].t <= 0) { G.blast[i] = (blast_t){p->x, p->y, p->z, BOMB_RADIUS, 0.3f, p->team}; break; }
  p->used = false;
}

static void step_proj(proj_t *p, float dt) {
  actor_t *by = &G.a[p->owner];
  if (p->kind == P_BOMB && p->landed) {
    p->fuse -= dt;
    if (p->fuse <= 0) explode(p);
    return;
  }
  float sp = sqrtf(p->vx * p->vx + p->vy * p->vy) + fabsf(p->vz);
  int n = (int)(sp * dt / 0.35f) + 1;
  float h = dt / n;
  for (int s = 0; s < n; s++) {
    float ox = p->x, oy = p->y, step = sqrtf(p->vx * p->vx + p->vy * p->vy) * h;
    p->x += p->vx * h;
    p->y += p->vy * h;
    p->travelled += step;
    if (p->kind == P_BOMB) { p->vz -= p->g * h; p->z += p->vz * h; }
    else if (p->straight > 0) { p->straight -= step; p->z += p->vz * h; }
    else { p->vz -= p->g * h; p->z += p->vz * h; }
    if (p->x < -2 || p->y < -2 || p->x > MAP_WM + 2 || p->y > MAP_HM + 2 || p->z < -3) { p->used = false; return; }
    /* hits a kid */
    if (p->kind != P_BOMB) {
      for (int i = 0; i < N_ACTORS; i++) {
        actor_t *e = &G.a[i];
        if (e->team == p->team || !e->alive) continue;
        float dx = e->x - p->x, dy = e->y - p->y, top = e->z + (e->swim ? 0.35f : e->squid ? 0.7f : 1.55f);
        if (dx * dx + dy * dy < 0.5f * 0.5f && p->z > e->z - 0.3f && p->z < top) {
          float k = clampf((p->travelled - 1.5f) / 4.5f, 0, 1);
          hurt(e, p->dmg + (p->dmg_far - p->dmg) * k, by);
          splat_fx(p->x, p->y, e->z, 0.5f, p->team, by);
          drops(p->x, p->y, p->z, p->team, 3);
          p->used = false;
          return;
        }
      }
    }
    /* hits the stage */
    int tx = (int)floorf(p->x * 2), ty = (int)floorf(p->y * 2), kind = col_kind(tx, ty);
    float top = col_top(tx, ty);
    if (kind == K_WALL && top > p->z) {   /* a building: shots stop, bombs bounce off */
      if (p->kind == P_BOMB) {
        int ox_t = (int)floorf(ox * 2), oy_t = (int)floorf(oy * 2);
        if (ox_t != tx) p->vx = -p->vx * 0.4f;
        if (oy_t != ty) p->vy = -p->vy * 0.4f;
        p->x = ox; p->y = oy;
        continue;
      }
      p->used = false;
      return;
    }
    if (top >= p->z) {
      if (top - p->z > 0.4f && p->kind == P_BOMB) {   /* a ledge face */
        int ox_t = (int)floorf(ox * 2), oy_t = (int)floorf(oy * 2);
        if (ox_t != tx) p->vx = -p->vx * 0.4f;
        if (oy_t != ty) p->vy = -p->vy * 0.4f;
        p->x = ox; p->y = oy;
        continue;
      }
      if (top - p->z > 0.4f) { p->used = false; return; }
      p->z = top;
      if (kind == K_WATER) { p->used = false; return; }
      if (p->kind == P_BOMB) { p->landed = true; p->fuse = BOMB_FUSE; p->vx = p->vy = p->vz = 0; return; }
      splat_fx(p->x, p->y, top, p->radius, p->team, by);
      drops(p->x, p->y, top, p->team, p->kind == P_DROP ? 2 : 1);
      p->used = false;
      return;
    }
    /* the shooter's trail inks the ground it passes over */
    if (p->trail > 0 && p->travelled > 2.0f && fmodf(p->travelled, p->trail) < step) {
      float g = ground_at(p->x, p->y);
      if (p->z - g < 2.6f) splat_fx(p->x, p->y, g, 0.44f, p->team, by);
    }
  }
}

/* ------------------------------------------------------------------ kids */
static void step_actor(actor_t *a, float dt) {
  if (a->flash > 0) a->flash -= dt;
  if (!a->alive) {
    a->respawn -= dt;
    if (a->respawn <= 0 && G.phase == PH_PLAY) place(a);
    return;
  }
  if (a->invuln > 0) a->invuln -= dt;
  a->cd -= dt;
  a->bomb_cd -= dt;
  a->ink_idle += dt;
  a->dmg_idle += dt;
  for (int i = 0; i < N_ACTORS; i++) if (a->hit_cd[i] > 0) a->hit_cd[i] -= dt;
  if (G.phase != PH_PLAY) { memset(&a->in, 0, sizeof a->in); }
  a->yaw = wrap_angle(a->yaw + a->in.turn);
  a->pitch = clampf(a->pitch + a->in.look, -0.5f, 0.45f);
  a->squid = a->in.squid && a->windup <= 0;
  if (a->squid) a->charge = 0;
  int under = paint_at(a->x, a->y);
  bool grounded = a->z - ground_at(a->x, a->y) < 0.1f;
  a->swim = a->squid && grounded && under == a->team + 1;
  bool enemy_ink = grounded && under && under != a->team + 1;
  move(a, dt);
  weapons(a, dt);
  if (a->ink < 0) a->ink = 0;
  /* ink tank */
  if (a->swim) a->ink += REFILL_SWIM * dt;
  else if (!enemy_ink && a->ink_idle > REFILL_DELAY && !a->squid) a->ink += REFILL_KID * dt;
  if (a->ink > INK_MAX) a->ink = INK_MAX;
  /* enemy ink hurts (up to a cap), own ink heals faster */
  if (enemy_ink) {
    if (a->ink_dmg < ENEMY_INK_CAP && a->invuln <= 0) {
      float d = ENEMY_INK_DPS * dt;
      a->ink_dmg += d;
      a->hp -= d;
      if (a->hp < 1) a->hp = 1;
      a->dmg_idle = 0;
    }
  } else {
    a->ink_dmg = 0;
    if (a->dmg_idle > REGEN_DELAY) a->hp += (a->swim ? REGEN_SWIM : REGEN) * dt;
    if (a->hp > HP) a->hp = HP;
  }
}

void game_step(float dt) {
  G.clock += dt;
  G.phase_t += dt;
  if (G.msg_t > 0) G.msg_t -= dt;
  if (G.phase == PH_INTRO && G.phase_t > 2.6f) { G.phase = PH_PLAY; G.phase_t = 0; }
  if (G.phase == PH_PLAY) {
    G.time_left -= dt;
    if (G.time_left <= 0) { G.time_left = 0; G.phase = PH_FINISH; G.phase_t = 0; }
  }
  if (G.phase == PH_FINISH && G.phase_t > 2.5f) G.phase = PH_DONE;
  for (int i = 0; i < N_ACTORS; i++) if (G.a[i].bot && G.a[i].alive && G.phase == PH_PLAY) bot_think(&G.a[i], dt);
  for (int i = 0; i < N_ACTORS; i++) step_actor(&G.a[i], dt);
  for (int i = 0; i < MAX_PROJ; i++) if (G.p[i].used) step_proj(&G.p[i], dt);
  for (int i = 0; i < MAX_BEAM; i++) if (G.beam[i].t > 0) G.beam[i].t -= dt;
  for (int i = 0; i < MAX_BLAST; i++) if (G.blast[i].t > 0) G.blast[i].t -= dt;
  for (int i = 0; i < MAX_PART; i++) {
    part_t *q = &G.part[i];
    if (q->t <= 0) continue;
    q->t -= dt;
    q->vz -= 14 * dt;
    q->x += q->vx * dt; q->y += q->vy * dt; q->z += q->vz * dt;
  }
}
