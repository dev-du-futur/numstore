/* PC check for Infinite Stacker.
 * - The drop rules on a scripted run (cut, five perfects into golden mode, golden growth, a miss).
 * - A bot plays runs with the clouds frozen; every few frames the partial redraw on screen is compared with a
 *   full redraw of the same moment (they must match pixel for pixel).
 * - A run with drifting clouds, for the frame costs and screenshots (PPM) in the folder given. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "eadk.h"
#include "../src/game.h"

extern float render_sky_speed;
static uint16_t copy[240][320];
static const char *dir;

static void shot(const char *name) {
  char p[256];
  snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
  FILE *f = fopen(p, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = host_screen[y][x];
      unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static int fails, checks;
static void verify(const hud_t *h, int frame) {
  checks++;
  memcpy(copy, host_screen, sizeof copy);
  render_invalidate();
  render_frame(h);
  int diff = 0, fx = -1, fy = -1;
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++)
      if (copy[y][x] != host_screen[y][x]) {
        if (!diff) { fx = x; fy = y; }
        diff++;
      }
  if (diff) {
    fails++;
    if (fails <= 5) {
      printf("  frame %d: %d pixels differ from a full redraw (first at %d,%d)\n", frame, diff, fx, fy);
      char n[40];
      snprintf(n, sizeof n, "diff_%d_full", frame);
      shot(n);
      static uint16_t full[240][320];
      memcpy(full, host_screen, sizeof full);
      memcpy(host_screen, copy, sizeof copy);
      snprintf(n, sizeof n, "diff_%d_partial", frame);
      shot(n);
      memcpy(host_screen, full, sizeof full);
    }
  }
}

/* what the bot aims for on each drop: offset from the block below, as a share of its size */
static float plan(int drop, int miss_at) {
  if (drop < 10) return 0;                  /* 5 perfects (golden), 5 more (back to normal) */
  if (drop == miss_at) return 1.3f;         /* misses completely */
  if (drop % 7 == 3) return 0.02f;          /* within 5 %: perfect */
  return ((drop * 37) % 11 - 5) * 0.025f;   /* -12.5 % .. +12.5 % */
}

static int run(int seed, int miss_at, bool check, bool shots) {
  G.rng = 0x1234567u + seed;
  render_init();
  game_reset(0);
  hud_t h = {0};
  h.best = 7;
  int32_t t = 0;
  long pushed_sum = 0, pushed_max = 0;
  int frames = 0;
  double ms = 0, ms_max = 0;
  int drops = 0, golden_seen = 0, perfect_seen = 0;
  int mode = 0;   /* 0 menu, 1 play, 2 falling, 3 card */
  int32_t instr_hide = -1, card_at = 0;
  for (; frames < 60 * 180; frames++, t += 16) {
    game_update(t);
    if (frames == 60) { game_start(t); mode = 1; render_invalidate(); }
    if (mode == 1 && G.state == GS_OVER) mode = 2;
    if (mode == 2 && G.now >= G.over_at) {
      mode = 3;
      card_at = G.now;
      h.new_best = game_score() > h.best;
      if (h.new_best) h.best = game_score();
    }
    if (mode == 3 && G.now - card_at > 1500) break;
    h.mode = mode == 0 ? 0 : mode == 3 ? 2 : 1;
    h.show_score = mode != 0;
    h.instr_alpha = mode != 1 ? 0 : instr_hide < 0 ? 1 : fmaxf(0, 1 - (G.now - instr_hide) / 500.0f);
    h.fps = 60;
    h.show_fps = shots && mode == 1;
    long before = host_pushed;
    clock_t c0 = clock();
    render_frame(&h);
    double m = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC;
    ms += m;
    if (m > ms_max) ms_max = m;
    long px = host_pushed - before;
    if (frames > 0) {
      pushed_sum += px;
      if (px > pushed_max) pushed_max = px;
    }
    if (check && frames % 3 == 0) verify(&h, frames);
    if (shots) {
      char n[40];
      if (frames == 30) shot("menu");
      if (frames == 75) shot("start");
      if (G.streak_at >= 0 && G.now - G.streak_at >= 200 && G.now - G.streak_at < 216) {
        snprintf(n, sizeof n, "perfect%d%s", G.streak_count, G.streak_golden ? "_golden" : "");
        shot(n);
      }
      for (int i = 0; i < MAX_BODIES; i++)
        if (mode == 1 && drops >= 11 && drops < 16 && G.bodies[i].used && G.now - G.bodies[i].born >= 300 && G.now - G.bodies[i].born < 316) {
          snprintf(n, sizeof n, "overhang_%d", drops); printf("  shot %s: n %d camY %.3f lookY %.3f top %.3f active %d state %d\n", n, G.n, G.camY, G.lookY, G.top, G.active, G.state);
          shot(n);
        }
      if (mode == 1 && drops == 30 && frames % 20 == 0) shot("play30");
      if (mode == 1 && drops == 12 && G.bodies[0].used) {
        int age = G.now - G.bodies[0].born;
        if (age % 160 < 16) { snprintf(n, sizeof n, "fall_%04d", age); shot(n); }
      }
      if (mode == 2 && G.anim && G.now - G.anim_at >= 700 && G.now - G.anim_at < 716) shot("zoom");
      if (mode == 2 && !G.anim && G.over_at - G.now < 16 && G.over_at - G.now >= 0) shot("zoomed");
      if (mode == 3 && G.now - card_at >= 300 && G.now - card_at < 316) shot("card");
    }
    /* the bot: presses at the millisecond the block is where it wants */
    if (mode == 1 && G.active) {
      for (int32_t s = t + 1; s <= t + 16; s++) {
        block_t *p = blk(G.n - 1);
        float size = G.adir == 0 ? p->w : p->d, ppos = G.adir == 0 ? p->x : p->z;
        float want = ppos + plan(drops, miss_at) * size;
        float pos = game_active_pos(s), step = G.aspeed * 60 / 1000.0f;
        if (fabsf(pos - want) <= step * 0.5f + 1e-6f || (plan(drops, miss_at) > 1 && pos > 1.9f)) {
          int n0 = G.n;
          game_update(s);
          game_press(s);
          if (instr_hide < 0) instr_hide = s;
          if (G.n > n0) {
            if (G.streak_at == G.now) perfect_seen++;
            if (G.golden) golden_seen++;
          }
          drops++;
          break;
        }
      }
    }
  }
  printf("run %d: score %d after %d drops, %d perfect, %d drops ending in golden mode; %d frames, pushed %.0f px/frame avg (%.1f%% of the screen), %ld max; host %.3f ms/frame (max %.2f)\n",
         seed, game_score(), drops, perfect_seen, golden_seen, frames, (double)pushed_sum / frames, 100.0 * pushed_sum / frames / 76800, pushed_max, ms / frames, ms_max);
  return game_score();
}

/* the drop maths on a scripted run */
static int32_t time_to(float want) {   /* first pass of the sliding block (it starts at -2, rising) */
  return G.aspawn + (int32_t)((want + 2) / (G.aspeed * 60) * 1000 + 0.5f);
}
static void rules(void) {
  game_start(0);
  float p = game_active_pos(1000);
  printf("rules: block after 1 s at %.3f (expect %.3f); speed %.3f (expect 0.021); bounces back at 2: pos(4 s) %.3f (expect %.3f)\n", p,
         -2 + 0.021f * 60, G.aspeed, game_active_pos(4000), 2 - (0.021f * 60 * 4 - 4));
  int32_t tt = time_to(0.3f);
  game_update(tt);
  game_press(tt);
  block_t *b = blk(1);
  printf("rules: drop 0.3 off -> w %.3f (expect 0.7), x %.3f (expect 0.15), score %d, overhang %s (w %.3f), next slides along %c\n", b->w, b->x,
         game_score(), G.bodies[0].used ? "falls" : "missing", G.bodies[0].hu * 2, G.adir ? 'z' : 'x');
  for (int k = 0; k < 5; k++) {
    block_t *pr = blk(G.n - 1);
    int32_t s = time_to((G.adir == 0 ? pr->x : pr->z) + 0.01f);
    game_update(s);
    game_press(s);
  }
  printf("rules: after 5 perfects golden=%d, label \"%s %d\" (expect GOLDEN! 5)\n", G.golden, G.streak_golden ? "GOLDEN!" : "Perfect!", G.streak_count);
  block_t *pr = blk(G.n - 1);
  float bw = pr->w, bd = pr->d;
  int32_t s = time_to(G.adir == 0 ? pr->x : pr->z);
  game_update(s);
  game_press(s);
  block_t *nb = blk(G.n - 1);
  printf("rules: golden perfect grows %.4f x %.4f -> %.4f x %.4f (x1.05)\n", bw, bd, nb->w, nb->d);
  pr = blk(G.n - 1);
  s = time_to((G.adir == 0 ? pr->x : pr->z) + 1.5f);
  game_update(s);
  game_press(s);
  printf("rules: miss -> %s, score %d (expect 7)\n", G.state == GS_OVER ? "game over" : "still playing", game_score());
}

static void drop_at(float pos) {
  int32_t s = time_to(pos);
  game_update(s);
  game_press(s);
}
static void ledge(float second, const char *what) {
  G.rng = 99;
  game_start(0);
  drop_at(0);       /* layer 1, x: perfect */
  drop_at(0);       /* layer 2, z: perfect */
  drop_at(-0.3f);   /* layer 3, x: cut, spans -0.5..0.2 */
  drop_at(blk(G.n - 1)->z);   /* layer 4, z: perfect */
  drop_at(second);  /* layer 5, x: the cut piece falls onto layer 2's exposed top */
  body_t *b = NULL;
  for (int i = 0; i < MAX_BODIES; i++)
    if (G.bodies[i].used && G.bodies[i].born == G.now) b = &G.bodies[i];
  if (!b) { printf("ledge: no piece\n"); return; }
  printf("ledge (%s): piece u %.2f w %.2f; layer 2 top at %.3f\n", what, b->u, b->hu * 2, layer_y(2) + BLOCK_H / 2);
  int32_t t0 = G.now;
  for (int k = 1; k <= 14; k++) {
    game_update(t0 + k * 100);
    if (!b->used) { printf("  %4d ms: gone (%s)\n", k * 100, G.parts[0].life > 0 ? "shattered" : "fell"); break; }
    if (k % 2 == 0) printf("  %4d ms: u %.3f y %.3f (bottom %.3f) angle %.1f deg\n", k * 100, b->u, b->y, b->y - b->hy, b->ang * 57.3f);
  }
}

static void resting(void) {
  G.rng = 5;
  game_start(0);
  drop_at(0);
  drop_at(0);
  drop_at(0);   /* three 1 x 1 layers */
  body_t *b = &G.bodies[0];
  memset(b, 0, sizeof *b);
  b->used = true; b->axis = 0; b->u = 0.3f; b->w = 0; b->y = layer_y(3) + 0.5f; b->hu = 0.2f; b->hy = 0.125f; b->hw = 0.5f;
  b->born = G.now;
  int32_t t0 = G.now;
  for (int k = 1; k <= 14; k++) {
    game_update(t0 + k * 100);
    if (k % 3 == 0) printf("resting: %4d ms: u %.3f bottom %.4f (layer top %.4f) angle %.2f deg\n", k * 100, b->u, b->y - b->hy, layer_y(3) + BLOCK_H / 2, b->ang * 57.3f);
  }
  /* overhanging the edge with its centre outside: tips */
  b->used = true; b->u = 0.55f; b->y = layer_y(3) + 0.3f; b->vu = b->vy = b->vw = b->ang = b->angv = 0; b->born = G.now;
  t0 = G.now;
  for (int k = 1; k <= 8; k++) {
    game_update(t0 + k * 100);
    if (k % 2 == 0) printf("tipping: %4d ms: u %.3f y %.3f angle %.1f deg\n", k * 100, b->u, b->y, b->ang * 57.3f);
  }
}

int main(int argc, char **argv) {
  dir = argc > 1 ? argv[1] : ".";
  rules();
  resting();
  ledge(0.25f, "lands on the ledge, carried off by its outward push");
  ledge(0.45f, "lands half off the ledge");
  render_sky_speed = 0;
  run(1, 40, true, false);
  run(2, 18, true, false);
  printf("partial vs full redraw: %d of %d checks differ\n", fails, checks);
  render_sky_speed = 1;
  run(3, 60, false, true);
  return fails != 0;
}
