/* Circle Tap for NumWorks: game state and rules (game.c), the renderer (render.c). */
#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "assets.h"

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318531f

enum { D_EASY, D_MEDIUM, D_HARD, D_COUNT };
enum { M_STANDARD, M_ZEN, M_MAP };
enum { GS_START, GS_PLAYING, GS_GAMEOVER };
enum { TAP_NONE, TAP_HIT, TAP_LEVELUP, TAP_MISS, TAP_GAMEOVER };

typedef struct { float speed, size, track; } difficulty_t;
extern const difficulty_t DIFF[D_COUNT];
extern const char *const DIFF_NAME[D_COUNT];

typedef struct { float h, s, l; } hsl_t;

#define N_PARTICLES 40
#define N_POPS 4
typedef struct { bool on; float x, y, vx, vy, life, max, size; uint32_t rgb; } particle_t;
typedef struct { bool on; float x, y, life, max; } pop_t;
typedef struct { bool on, on_arc; float life, max, a0, size, x, y; } flash_t;

/* positions of the effects are in units of the track radius, centred on the track */
typedef struct {
  int state, mode, diff;
  const map_t *map;
  int score, level, taps_level, taps_next, lives, max_lives;
  float angle, dir, speed, target_start, target_size;
  hsl_t col, col_to;
  bool fail_on;
  float fail_angle, fail_age;
  particle_t p[N_PARTICLES];
  pop_t pop[N_POPS];
  flash_t flash;
  float heat;
} game_t;
extern game_t G;

void rnd_seed(uint32_t s);
float rnd(void); /* 0..1 */

uint32_t hsl_rgb(hsl_t c);       /* 0xRRGGBB */
hsl_t level_hsl(int level);       /* hsl((level-1)*40, 100%, 60%) */
bool angle_in_arc(float a, float start, float size);
void path_point(const map_t *m, float progress, float *x, float *y, float *ang); /* map units (-1..1) */
void track_point(float angle, float *x, float *y);                              /* radius units */

void game_init(int diff);
void game_set_difficulty(int diff);
void game_reset(void);   /* back to the idle 'start' state */
void game_start(int mode, int diff, const map_t *map);
void game_update(float dt);
int game_tap(void);
void game_quit(void);   /* end the run without a game over (Back) */

/* ---------------------------------------------------------------- renderer */
#define SW 320
#define SH 240
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* the play area: a square of `size` pixels centred on (cx, cy), as the original's canvas */
typedef struct { float cx, cy, size; } view_t;

enum { F_S, F_M, F_SCORE, F_TITLE, F_LEVEL };
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

void r_begin(void);                                   /* start a new display list */
void r_scene(const view_t *v, bool cursor);           /* the stars, track, target, effects, cursor */
void r_stars_update(float dt);
void r_dim(uint32_t rgb, float alpha);                /* full-screen veil */
void r_rrect(float x, float y, float w, float h, float rad, uint32_t c1, uint32_t c2, float alpha, uint32_t border, float balpha);
void r_text(int font, float x, float y, int align, const char *s, uint32_t rgb, float alpha, float spacing, float scale);
float r_text_w(int font, const char *s, float spacing);
void r_heart(float x, float y, int px, uint32_t rgb, float alpha);
void r_end(void);                                     /* redraw what changed and push it */
void r_invalidate_all(void);
void r_redraw_all(void);
int r_last_pixels(void);                              /* pixels pushed by the last r_end, for the benchmark */
#endif
