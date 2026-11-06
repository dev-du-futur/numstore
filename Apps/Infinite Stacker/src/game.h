/* Infinite Stacker for NumWorks: shared state of the game and the renderer. */
#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include <stdint.h>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define BLOCK_H 0.25f
#define BASE_H 20.0f
#define MAX_STACK 1024 /* blocks kept (a ring); older ones are only missed by the game-over zoom of huge towers */
#define MAX_BODIES 12  /* falling overhangs */
#define MAX_PARTS 200  /* shatter debris */
#define N_SPARK 12
#define N_MSPARK 20

typedef struct {
  float x, z, w, d;
  float col[3];      /* linear RGB */
  int32_t flash_at;  /* ms of the last flash, or -1 */
} block_t;

/* A box that falls: an overhang, or the block that missed. It tumbles in the plane of its own axis u
 * (x or z) and y, and slides freely along the other horizontal axis w. */
typedef struct {
  bool used;
  int axis;                 /* 0: u = x, w = z; 1: u = z, w = x */
  float u, y, w;            /* centre */
  float vu, vy, vw;
  float hu, hy, hw;         /* half extents */
  float ang, angv;          /* rotation in the u-y plane */
  float col[3];
  int32_t born;             /* ms */
} body_t;

typedef struct {
  float p[3], v[3];
  float size;
  float col[3];
  int life;                 /* ticks; 0 = unused */
} part_t;

typedef struct {
  float p[3], v[3];
  float base;
  int life, maxlife;
} spark_t;

typedef enum { GS_IDLE, GS_PLAYING, GS_OVER } gstate_t;

typedef struct {
  gstate_t state;
  int32_t now;              /* ms since the app started */
  block_t stack[MAX_STACK]; /* block i at stack[i % MAX_STACK] */
  int n;                    /* blocks in the stack, the base included; score = n - 1 */

  bool active;
  float ax, az, aw, ad, acol[3], aspeed;
  int adir;                 /* 0 x, 1 z */
  int32_t aspawn;

  int perfect_streak, golden_streak;
  bool golden;

  body_t bodies[MAX_BODIES];
  part_t parts[MAX_PARTS];
  spark_t spark[N_SPARK], mspark[N_MSPARK];

  float camY, lookY, top;   /* camera at (4, camY, 4) looking at (0, lookY, 0); half height of the view */
  bool anim;
  int32_t anim_at;
  float a0y, a0top, a0look, a1y, a1top, a1look;
  int32_t over_at;          /* when the game-over card should appear */

  /* the "Perfect! n" label: set on each perfect drop, cleared on a miss */
  int streak_count;
  bool streak_golden;
  int32_t streak_at;        /* -1: hidden */
  int32_t hide_streak_at;   /* -1 or when it was hidden (fade out) */

  uint32_t version;         /* bumped whenever the stack changes */
  uint32_t rng;
} game_t;

extern game_t G;

static inline block_t *blk(int i) { return &G.stack[i % MAX_STACK]; }
static inline float layer_y(int i) { return i == 0 ? -BASE_H / 2 + BLOCK_H / 2 : BLOCK_H * i; }
static inline int game_score(void) { return G.n > 0 ? G.n - 1 : 0; }

void game_reset(int32_t now);          /* idle scene: the base block only */
void game_start(int32_t now);          /* a new run */
void game_press(int32_t t);            /* drop the block where it was at time t */
void game_update(int32_t now);         /* runs the 60 Hz ticks up to now */
float game_active_pos(int32_t t);

/* render.c */
typedef struct {
  int mode;                 /* 0 menu, 1 playing, 2 game over card */
  bool show_score;
  int best;
  bool new_best;
  float instr_alpha;        /* "Press OK to place the block" */
  bool show_fps;
  int fps;
  int32_t card_at;          /* when the card appeared (its fade in) */
} hud_t;

void render_init(void);
void render_invalidate(void);          /* next frame redraws the whole screen */
int render_frame(const hud_t *h);      /* returns the pixels pushed */
#endif
