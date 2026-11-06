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

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : 12345;
  rnd_seed(seed);
  nav_init();
  game_new(W_SHOOTER, D_FRESH, true);
  const float dt = 1.0f / 30;
  double t_sim = 0, t_ren = 0, worst = 0;
  int frames = 0, maxproj = 0;
  const int snaps[] = {30 * 4, 30 * 40, 30 * 100, 30 * 170};
  while (G.phase != PH_DONE && frames < 30 * 200) {
    clock_t a = clock();
    game_step(dt);
    clock_t b = clock();
    render_frame(30, 0, true);
    frame_push();
    clock_t c = clock();
    t_sim += (double)(b - a) / CLOCKS_PER_SEC;
    double r = (double)(c - b) / CLOCKS_PER_SEC;
    t_ren += r;
    if (r > worst) worst = r;
    int np = 0;
    for (int i = 0; i < MAX_PROJ; i++) np += G.p[i].used;
    if (np > maxproj) maxproj = np;
    for (int i = 0; i < 4; i++)
      if (frames == snaps[i]) { char n[16]; snprintf(n, sizeof n, "view%d", i); shot(dir, n); }
    frames++;
  }
  render_map();
  frame_push();
  shot(dir, "map");
  printf("%d frames, sim %.3f ms, render %.3f ms (worst %.2f), most shots in the air %d\n", frames,
         t_sim * 1000 / frames, t_ren * 1000 / frames, worst * 1000, maxproj);
  printf("Alpha %.1f%%  Bravo %.1f%%  (inkable cells %d)\n", team_share(0) * 100, team_share(1) * 100, (int)G.paintable);
  for (int i = 0; i < N_ACTORS; i++)
    printf("  %-9s team %d %-13s turf %5.0f  splats %d  deaths %d\n", G.a[i].name, G.a[i].team, WEAPON_NAME[G.a[i].weapon], G.a[i].turf, G.a[i].splats, G.a[i].deaths);
  return 0;
}
