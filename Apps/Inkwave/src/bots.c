/* Bot brains, after Inkwave's src/game/bots.js (much reduced): pick turf to claim, path there (swimming
 * through their own ink), paint on the way, spot enemies with a human-ish reaction time and aim error,
 * fight, retreat to heal when losing, refill when the tank is low, throw bombs. */
#include <math.h>
#include <string.h>
#include "game.h"

enum { M_PAINT, M_FIGHT, M_REFILL, M_RETREAT };
#define PATH_MAX 48

typedef struct {
  int mode, target;
  uint16_t path[PATH_MAX];
  int n, pi;
  float gx, gy;
  float goal_t, repath, think, react, see, lost;
  float strafe_t, strafe_dir, retreat_t, refill_to;
  float stuck_t, lastx, lasty, wig_t, wig_yaw;
  float phase, chargeto, paint_cd;
} brain_t;

static brain_t B[N_ACTORS];

void bots_reset(void) {
  memset(B, 0, sizeof B);
  for (int i = 0; i < N_ACTORS; i++) {
    B[i].target = -1;
    B[i].phase = frnd() * 10;
    B[i].think = frnd() * 0.2f;
    B[i].strafe_dir = frnd() < 0.5f ? -1 : 1;
  }
}

static bool go_to(actor_t *a, brain_t *b, float x, float y) {
  b->n = nav_path(a->x, a->y, x, y, b->path, PATH_MAX);
  b->pi = b->n > 1 ? 1 : 0;
  b->gx = x;
  b->gy = y;
  b->repath = 0.8f + frnd() * 0.4f;
  return b->n > 0;
}

static float dist(float ax, float ay, float bx, float by) { float dx = bx - ax, dy = by - ay; return sqrtf(dx * dx + dy * dy); }

static bool visible(const actor_t *a, const actor_t *e) {
  float d = dist(a->x, a->y, e->x, e->y);
  if (d > DIFF_AWARE[G.difficulty]) return false;
  float sp = sqrtf(e->vx * e->vx + e->vy * e->vy);
  if (e->swim && d > 3 && !(sp > 7 && d < 9)) return false;   /* hidden in its ink */
  return line_clear(a->x, a->y, a->z + 1.3f, e->x, e->y, e->z + (e->squid ? 0.3f : 1.0f));
}

static void perceive(actor_t *a, brain_t *b) {
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < N_ACTORS; i++) {
    actor_t *e = &G.a[i];
    if (e->team == a->team || !e->alive || !visible(a, e)) continue;
    float s = dist(a->x, a->y, e->x, e->y) - (i == b->target ? 4 : 0);
    if (s < bd) { bd = s; best = i; }
  }
  if (best >= 0) {
    if (best != b->target) { b->target = best; b->react = DIFF_REACTION[G.difficulty] * (0.7f + 0.6f * frnd()); b->repath = 0; }
    b->see = 1.2f;
    b->lost = 0;
  } else {
    b->see -= 0.2f;
    if (b->target >= 0) {
      b->lost += 0.2f;
      if (b->lost > 2.5f) b->target = -1;
    }
  }
}

/* somewhere worth inking: unclaimed turf counts, enemy ink more, own ink nothing; further up the field
 * is better; spots near a teammate's goal are worse */
static void pick_paint_goal(actor_t *a, brain_t *b) {
  float ex, ey, ox, oy;
  spawn_point(1 - a->team, &ex, &ey);
  spawn_point(a->team, &ox, &oy);
  float total = dist(ox, oy, ex, ey), bs = -1e9f, bx = 0, by = 0;
  for (int i = 0; i < 24; i++) {
    int nx = rnd() % NAV_W, ny = rnd() % NAV_H;
    if (!nav_ok(nx, ny)) continue;
    float x = nx + 0.5f, y = ny + 0.5f, d = dist(a->x, a->y, x, y);
    if (d > 38) continue;
    float en, own = paint_share(x, y, 3, a->team, &en);
    float value = (1 - own - en) + en * 1.4f;
    float progress = clampf(1 - dist(x, y, ex, ey) / total, 0, 0.8f);
    float score = value * 16 - d * 0.14f + progress * 2.5f + frnd() * 1.5f;
    if (a->weapon == W_CHARGER) score += clampf(ground_at(x, y), 0, 4) * 1.6f;   /* high perches */
    if (value < 0.15f) score -= 8;
    for (int k = 0; k < N_ACTORS; k++) {
      actor_t *m = &G.a[k];
      if (m == a || m->team != a->team || !m->bot) continue;
      if (dist(B[k].gx, B[k].gy, x, y) < 8) score -= 5;
    }
    if (score > bs) { bs = score; bx = x; by = y; }
  }
  b->goal_t = 4 + frnd() * 3;
  b->repath = 0.3f;
  if (bs > -1e8f) go_to(a, b, bx, by);
}

/* the nearest patch of own ink (away from the enemy, when retreating), else our spawn */
static void pick_own_ink(actor_t *a, brain_t *b, bool away) {
  float bx = 0, by = 0, bd = 1e9f;
  const actor_t *e = b->target >= 0 ? &G.a[b->target] : 0;
  for (int i = 0; i < 20; i++) {
    float ang = frnd() * 2 * PI_F, r = 1 + frnd() * (i < 12 ? 7 : 18);
    float x = a->x + cosf(ang) * r, y = a->y + sinf(ang) * r;
    if (!nav_ok((int)x, (int)y)) continue;
    float own = paint_share(x, y, 1.2f, a->team, 0);
    if (own < 0.6f) continue;
    float s = r;
    if (away && e) s -= dist(x, y, e->x, e->y) * 0.8f;
    if (s < bd) { bd = s; bx = x; by = y; }
  }
  float sx, sy;
  spawn_point(a->team, &sx, &sy);
  if (bd > 1e8f || !go_to(a, b, bx, by)) go_to(a, b, sx, sy);
  b->repath = 1.2f;
}

void bot_think(actor_t *a, float dt) {
  brain_t *b = &B[a->idx];
  intent_t *in = &a->in;
  b->think -= dt; b->react -= dt; b->goal_t -= dt; b->repath -= dt; b->strafe_t -= dt; b->paint_cd -= dt;
  b->phase += dt;
  if (b->think <= 0) { b->think = 0.15f + frnd() * 0.1f; perceive(a, b); }
  actor_t *tg = b->target >= 0 ? &G.a[b->target] : 0;
  if (tg && !tg->alive) { b->target = -1; tg = 0; }
  bool seen = tg && b->see > 0;
  float hpf = a->hp / 100, inkf = a->ink / 100;

  /* ---- mode */
  if (b->mode == M_RETREAT) {
    b->retreat_t -= dt;
    if (hpf > 0.85f || b->retreat_t <= 0 || (!tg && hpf > 0.6f)) { b->mode = M_PAINT; b->n = 0; b->goal_t = 0; }
  } else if (seen && (hpf < 0.34f || hpf < 0.2f) && a->weapon != W_ROLLER && frnd() < 0.6f * dt * 10) {
    b->mode = M_RETREAT;
    b->retreat_t = 2.2f + frnd() * 1.4f;
    pick_own_ink(a, b, true);
  }
  if (b->mode != M_REFILL && b->mode != M_RETREAT && inkf < 0.12f && !(seen && inkf > 0.05f && a->weapon != W_ROLLER)) {
    b->mode = M_REFILL;
    b->refill_to = 0.85f + frnd() * 0.1f;
    pick_own_ink(a, b, false);
  }
  if (b->mode == M_REFILL && inkf >= b->refill_to) { b->mode = M_PAINT; b->goal_t = 0; }
  if (b->mode != M_REFILL && b->mode != M_RETREAT) {
    int m = tg ? M_FIGHT : M_PAINT;
    if (m == M_PAINT && b->mode == M_FIGHT) b->goal_t = 0;
    b->mode = m;
  }

  /* ---- where to go */
  float d_t = tg ? dist(a->x, a->y, tg->x, tg->y) : 0;
  float range = weapon_range(a->weapon) * (a->weapon == W_CHARGER ? 0.9f : 1);
  if (b->mode == M_FIGHT) {
    bool hold = a->weapon == W_CHARGER && seen && d_t < range;   /* chargers hold their spot */
    if (hold) b->n = 0;
    else if (b->repath <= 0) go_to(a, b, tg->x, tg->y);
  } else if (b->mode == M_REFILL || b->mode == M_RETREAT) {
    if (b->repath <= 0 && b->n == 0) pick_own_ink(a, b, b->mode == M_RETREAT);
  } else {
    if (b->n > 0 && b->goal_t > 0 && fmodf(b->phase, 1.0f) < dt && paint_share(b->gx, b->gy, 3, a->team, 0) > 0.85f) b->goal_t = 0;
    if (b->goal_t <= 0 || ((b->n == 0 || b->pi >= b->n) && b->repath <= 0)) pick_paint_goal(a, b);
  }

  /* ---- steering along the path */
  float mx = 0, my = 0, left = 0;
  if (b->n > 0 && b->pi < b->n) {
    int c = b->path[b->pi];
    float wx = (c & 255) + 0.5f, wy = (c >> 8) + 0.5f;
    if (dist(a->x, a->y, wx, wy) < 0.6f) { b->pi++; if (b->pi >= b->n) b->n = 0; }
    if (b->n > 0) {
      /* look a waypoint ahead when the way there is clear, for smoother lines */
      if (b->pi + 1 < b->n) {
        int c2 = b->path[b->pi + 1];
        float x2 = (c2 & 255) + 0.5f, y2 = (c2 >> 8) + 0.5f;
        if (line_clear(a->x, a->y, a->z + 0.9f, x2, y2, a->z + 0.9f)) { wx = x2; wy = y2; }
      }
      float dx = wx - a->x, dy = wy - a->y, l = sqrtf(dx * dx + dy * dy);
      if (l > 0.01f) { mx = dx / l; my = dy / l; }
      left = (b->n - b->pi) * 1.0f;
    }
  }
  /* stuck: wiggle a moment, then plan again */
  b->stuck_t += dt;
  if (b->stuck_t > 0.8f) {
    if ((mx || my) && dist(a->x, a->y, b->lastx, b->lasty) < 0.3f && b->wig_t <= 0) { b->wig_t = 0.5f; b->wig_yaw = frnd() * 2 * PI_F; b->repath = 0; b->goal_t = 0; }
    b->stuck_t = 0; b->lastx = a->x; b->lasty = a->y;
  }
  if (b->wig_t > 0) { b->wig_t -= dt; mx = cosf(b->wig_yaw); my = sinf(b->wig_yaw); }

  /* ---- aim and actions */
  in->fire = in->bomb = in->squid = false;
  float want_yaw = (mx || my) ? atan2f(my, mx) : a->yaw;
  bool on_own = paint_at(a->x, a->y) == a->team + 1;
  float err = DIFF_AIM_ERR[G.difficulty] * (sinf(b->phase * 1.7f) * 0.6f + sinf(b->phase * 3.9f + 1.3f) * 0.4f);
  if (b->mode == M_FIGHT && tg) {
    float lead = a->weapon == W_SHOOTER ? d_t / 34 : 0;
    float tx = tg->x + tg->vx * lead, ty = tg->y + tg->vy * lead;
    want_yaw = atan2f(ty - a->y, tx - a->x) + err;
    float aim_off = fabsf(wrap_angle(want_yaw - a->yaw));
    bool can = seen && b->react <= 0;
    if (a->weapon == W_SHOOTER) {
      if (d_t < range * 0.55f) { mx = -(tg->x - a->x) / d_t; my = -(tg->y - a->y) / d_t; }   /* back off */
      if (d_t < range * 0.95f) {   /* strafe while trading shots */
        if (b->strafe_t <= 0) { b->strafe_t = 0.6f + frnd() * 0.8f; b->strafe_dir = -b->strafe_dir; }
        float sx = -(tg->y - a->y) / d_t * b->strafe_dir, sy = (tg->x - a->x) / d_t * b->strafe_dir;
        mx = mx * 0.5f + sx; my = my * 0.5f + sy;
      }
      in->fire = can && d_t < range * 1.05f && aim_off < 0.25f && a->ink > 1;
    } else if (a->weapon == W_ROLLER) {
      if (can && d_t < 2.6f && aim_off < 0.6f) in->fire = true;                         /* run them over */
      else if (can && d_t < 6.5f && aim_off < 0.3f) in->fire = (int)(b->phase * 4) & 1; /* flick */
      if (!in->fire && d_t > 3 && on_own && a->ink > 5) in->squid = true;                /* swim in */
    } else {
      if (seen) { mx *= 0.2f; my *= 0.2f; }
      if (can && d_t < 27 && a->ink > 3) {
        bool ready = a->charge >= 1 || (a->charge > 0.55f && d_t < 11);
        in->fire = !(ready && aim_off < 0.06f);   /* holding charges; letting go fires */
      }
    }
    if (can && d_t > 4 && d_t < 8.5f && a->ink > 80 && frnd() < 0.5f * dt) in->bomb = true;
  } else {
    /* travelling: swim through own ink, ink what's ahead */
    if (b->mode == M_PAINT && a->weapon == W_SHOOTER) want_yaw += sinf(b->phase * 2.3f) * 0.35f;
    float ax = a->x + cosf(a->yaw) * 4, ay = a->y + sinf(a->yaw) * 4;
    float en, own_ahead = paint_share(ax, ay, 1.5f, a->team, &en);
    if (b->mode == M_REFILL || b->mode == M_RETREAT) in->squid = on_own || left > 2;
    else if (on_own && left > 4 && own_ahead > 0.7f) in->squid = true;
    else if (b->mode == M_PAINT && (mx || my) && own_ahead < 0.75f) {
      if (a->weapon == W_SHOOTER) in->fire = a->ink > 20;
      else if (a->weapon == W_ROLLER) in->fire = a->ink > 12;
      else if (a->ink > 30 && b->paint_cd <= 0) {   /* the charger shoots a line down the lane */
        in->fire = a->charge < b->chargeto;
        mx *= 0.2f; my *= 0.2f;
        if (!in->fire) { b->paint_cd = 1.2f + frnd(); b->chargeto = 0.5f + frnd() * 0.5f; }
      }
    }
    if (b->mode == M_PAINT && a->ink >= 99 && frnd() < 0.06f * dt && own_ahead < 0.3f && en < 0.5f) in->bomb = true;
    if (b->chargeto == 0) b->chargeto = 0.7f;
  }

  /* turn (rate-capped), then move in that frame */
  float turn = wrap_angle(want_yaw - a->yaw), cap = DIFF_TURN[G.difficulty] * dt;
  in->turn = clampf(turn * 0.5f, -cap, cap);
  float yaw = a->yaw + in->turn, fx = cosf(yaw), fy = sinf(yaw);
  float ml = sqrtf(mx * mx + my * my);
  if (ml > 1) { mx /= ml; my /= ml; }
  in->fwd = mx * fx + my * fy;
  in->side = -mx * fy + my * fx;
}
