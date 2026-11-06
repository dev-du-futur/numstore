/* PC check for Circle Tap: drives the real menus, game and renderer with scripted key presses, checks that
 * the partial redraw matches a full redraw, plays games with a bot, and saves screenshots (PPM) in the folder
 * given. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "eadk.h"
#include "ui.h"

static const char *dir;
static uint16_t saved[240][320];
static long frames, px_total, worst_px, mismatches, checks;
static double ms_total;
long stat_star_tiles;

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

/* one frame at 60 fps; every 7th frame the screen is redrawn whole and compared */
static void frame(uint64_t keys) {
  clock_t a = clock();
  ui_frame(1 / 60.0f, keys);
  ms_total += (double)(clock() - a) * 1000 / CLOCKS_PER_SEC;
  int px = r_last_pixels();
  px_total += px;
  if (px > worst_px && frames > 2) worst_px = px;
  frames++;
  if (frames % 7 == 0) {
    memcpy(saved, host_screen, sizeof saved);
    r_redraw_all();
    long bad = 0;
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 320; x++) bad += saved[y][x] != host_screen[y][x];
    checks++;
    if (bad) {
      mismatches++;
      if (mismatches < 4) {
        printf("frame %ld: %ld pixels differ from a full redraw\n", frames, bad);
        memcpy(host_screen, saved, sizeof saved);
        char n[32];
        snprintf(n, sizeof n, "mismatch%ld", mismatches);
        shot(n);
      }
    }
  }
}
static void idle(int n) { for (int i = 0; i < n; i++) frame(0); }
static void press(int key) { frame(1ull << key); }

/* taps when the cursor is inside the target (as a good player would), or misses on purpose */
static bool in_zone(void) {
  float tol = atanf(DIFF[G.diff].track);
  return angle_in_arc(G.angle, G.target_start - tol, G.target_size + 2 * tol);
}
static int bot_play(int max_frames, int stop_level, int tag) {
  int n = 0;
  bool shot_fx = false;
  while (G.state == GS_PLAYING && n < max_frames) {
    if (G.level >= stop_level) break;
    if (in_zone()) {
      press(K_OK);
      if (!shot_fx && G.score >= 5 && tag) {
        idle(4);
        char b[32];
        snprintf(b, sizeof b, "hit_fx_%d", tag);
        shot(b);
        shot_fx = true;
      }
    } else frame(0);
    n++;
  }
  return G.score;
}
static void miss(void) {
  while (in_zone()) frame(0);
  press(K_OK);
}

int main(int argc, char **argv) {
  dir = argc > 1 ? argv[1] : ".";
  rnd_seed(4242);
  ui_init();
  r_invalidate_all();
  idle(90);
  shot("01_start");
  press(K_RIGHT);
  idle(30);
  shot("02_start_medium");
  press(K_LEFT);
  idle(5);

  /* standard game on Easy */
  press(K_OK);
  idle(20);
  shot("03_play_start");
  bot_play(60 * 60, 7, 1);
  printf("Easy: score %d, level %d, speed %.2f rad/s\n", G.score, G.level, G.speed);
  idle(10);
  shot("04_play_level");
  miss();
  idle(10);
  shot("05_miss");
  idle(80);
  shot("06_gameover");
  press(K_RIGHT);
  press(K_RIGHT);
  idle(10);
  press(K_OK);   /* restart on Hard */
  idle(30);
  bot_play(60 * 30, 5, 2);
  printf("Hard: score %d, level %d\n", G.score, G.level);
  shot("07_play_hard");
  miss();
  idle(100);
  shot("08_gameover_hard");
  press(K_BACK);
  idle(30);
  press(K_LEFT);
  press(K_LEFT);

  /* zen */
  press(K_DOWN);
  idle(10);
  shot("09_start_focus_zen");
  press(K_OK);
  bot_play(60 * 10, 4, 0);
  miss();
  idle(20);
  printf("Zen after a miss: score %d, level %d, state %d\n", G.score, G.level, G.state);
  shot("10_zen");
  press(K_BACK);
  idle(10);

  /* maps */
  press(K_DOWN);
  press(K_OK);
  idle(30);
  shot("11_maps");
  for (int i = 0; i < N_MAPS; i++) {
    press(K_OK);
    idle(10);
    bot_play(60 * 20, 4, 0);
    idle(5);
    char b[32];
    snprintf(b, sizeof b, "12_map_%d", i);
    shot(b);
    int lives = G.lives;
    miss();
    idle(5);
    printf("Map %-8s score %2d level %d lives %d -> %d\n", MAPS[i].name, G.score, G.level, lives, G.lives);
    if (i == 4) { idle(20); shot("13_map_miss"); }
    while (G.state == GS_PLAYING) miss();
    idle(60);
    if (i == 4) shot("14_map_gameover");
    press(K_BACK);
    idle(10);
    press(K_DOWN);
    idle(10);
  }
  shot("15_maps_after");

  printf("%ld frames, %.3f ms a frame on this PC, %ld px pushed a frame on average (worst %ld of 76800)\n", frames,
         ms_total / frames, px_total / frames, worst_px);
  printf("tiles added by the stars alone: %.1f a frame (%ld px)\n", (double)stat_star_tiles / frames, stat_star_tiles * 256 / frames);
  printf("partial vs full redraw: %ld of %ld checks differ\n", mismatches, checks);
  return mismatches != 0;
}
