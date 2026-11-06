/* PC check: a whole Turf War match with 8 bots (the camera rides actor 0), frame times for the sim and
 * the renderer, the final score, and screenshots (PPM) in the folder given. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "eadk.h"
#include "../src/game.h"

static void shot(const char *dir, const char *name) {
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

static void title_overlay(void) { text_o(40, 8, "INKWAVE", RGB(255, 138, 20), 3); }

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : 12345;
  rnd_seed(seed);
  nav_init();
  render_init();
  game_new(W_SHOOTER, D_FRESH, true);
  const float dt = 1.0f / 30;
  double t_sim = 0, t_ren[3] = {0}, worst = 0;
  int n_ren[3] = {0}, frames = 0, maxproj = 0;
  const int snaps[] = {30 * 4, 30 * 40, 30 * 100, 30 * 170};
  while (G.phase != PH_DONE && frames < 30 * 200) {
    clock_t a = clock();
    game_step(dt);
    clock_t b = clock();
    t_sim += (double)(b - a) / CLOCKS_PER_SEC;
    /* every frame at Full; every 10th also at the other two, for their times */
    for (int q = Q_FULL; q >= (frames % 10 ? Q_FULL : Q_FAST); q--) {
      render_set_quality(q);
      clock_t c = clock();
      render_frame(30, 0, true);
      double r = (double)(clock() - c) / CLOCKS_PER_SEC;
      t_ren[q] += r;
      n_ren[q]++;
      if (q == Q_FULL && r > worst) worst = r;
      for (int i = 0; i < 4; i++)
        if (frames == snaps[i]) { char n[24]; snprintf(n, sizeof n, "view%d_%s", i, q == Q_FULL ? "full" : q == Q_BALANCED ? "bal" : "fast"); shot(dir, n); }
    }
    int np = 0;
    for (int i = 0; i < MAX_PROJ; i++) np += G.p[i].used;
    if (np > maxproj) maxproj = np;
    if (frames == 1500 || frames == 3000) {
      for (int q = Q_FAST; q <= Q_FULL; q++) {
        render_set_quality(q);
        clock_t c = clock();
        for (int i = 0; i < 400; i++) render_frame(30, 0, true);
        printf("bench f%d %s: %.3f ms\n", frames, q == Q_FULL ? "full" : q == Q_BALANCED ? "balanced" : "fast", (double)(clock() - c) / CLOCKS_PER_SEC * 1000 / 400);
      }
    }
    frames++;
  }
  render_set_quality(Q_FULL);
  render_map();
  shot(dir, "map");
  render_title(0.7f, title_overlay);
  shot(dir, "title");
  printf("%d frames, sim %.3f ms, render ms: fast %.2f, balanced %.2f, full %.2f (worst %.2f); most shots in the air %d\n", frames,
         t_sim * 1000 / frames, t_ren[0] * 1000 / n_ren[0], t_ren[1] * 1000 / n_ren[1], t_ren[2] * 1000 / n_ren[2], worst * 1000, maxproj);
  printf("Alpha %.1f%%  Bravo %.1f%%  (inkable cells %d)\n", team_share(0) * 100, team_share(1) * 100, (int)G.paintable);
  for (int i = 0; i < N_ACTORS; i++)
    printf("  %-9s team %d %-13s turf %5.0f  splats %d  deaths %d\n", G.a[i].name, G.a[i].team, WEAPON_NAME[G.a[i].weapon], G.a[i].turf, G.a[i].splats, G.a[i].deaths);
  return 0;
}
