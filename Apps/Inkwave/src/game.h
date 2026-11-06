/* Inkwave for the NumWorks calculator: a blocky 3D remake of Inkwave's Turf War.
 * Inkwave (c) 2026 Jayden Davis, MIT licence. Rules, names and numbers come from its src/config.js;
 * the stage is Tidewater Plaza, turned into a heightfield of 0.5 m columns by tools/make_map.mjs.
 *
 * World units are metres: x along the stage (Alpha's spawn at low x), y across it, z up.
 * Ink sits on the tops of the columns, on a grid of 0.25 m cells, 2 bits each. */
#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#define MAP_WM 94.0f      /* stage size in metres */
#define MAP_HM 56.0f
#define PAINT_W 376       /* ink cells (0.25 m) */
#define PAINT_H 224
#define NAV_W 94          /* bot navigation cells (1 m) */
#define NAV_H 56

enum { K_FLOOR, K_WALL, K_RAIL, K_WATER };
enum { W_SHOOTER, W_ROLLER, W_CHARGER, W_COUNT };
enum { D_CHILL, D_FRESH, D_FIERCE };

#define TEAM_SIZE 4
#define N_ACTORS 8
#define MATCH_TIME 180.0f
#define STEP_UP 1.0f      /* the highest ledge you can walk onto */

typedef struct {
  float fwd, side;  /* move, in the actor's frame: -1..1 */
  float turn;       /* yaw change wanted this frame (radians) */
  float look;       /* pitch change wanted this frame (radians, + = up) */
  bool fire, squid, bomb;
} intent_t;

typedef struct {
  /* body */
  float x, y, z, vz, yaw, pitch;
  float vx, vy;
  float hp, ink;
  int team, weapon, idx;
  bool alive, squid, swim, bot;   /* swim: squid form in own ink (hidden, fast, refills) */
  float respawn, invuln, flash, last_hit;
  int last_attacker;
  /* weapon state */
  float cd, charge, windup, roll_dist, ink_idle, dmg_idle, ink_dmg, bomb_cd;
  bool fire_was, rolling;
  float hit_cd[N_ACTORS];         /* roller contact, per victim */
  /* score */
  int splats, deaths;
  float turf;                     /* m² inked */
  const char *name;
  intent_t in;
} actor_t;

enum { P_SHOT, P_DROP, P_BOMB };
typedef struct {
  float x, y, z, vx, vy, vz;
  float straight;    /* metres left before it starts to drop */
  float g;           /* its gravity once dropping */
  float trail;       /* metres to the next trail splat (0 = none) */
  float dmg, dmg_far, radius, travelled, fuse;
  int8_t kind, team, owner;
  bool landed, used;
} proj_t;

typedef struct { float x0, y0, z0, x1, y1, z1, t; int team; } beam_t;   /* charger shot, drawn briefly */
typedef struct { float x, y, z, r, t; int team; } blast_t;             /* bomb burst */
typedef struct { float x, y, z, vx, vy, vz, t; int team; } part_t;     /* ink droplet (looks only) */
#define MAX_PART 64

#define MAX_PROJ 160
#define MAX_BEAM 8
#define MAX_BLAST 8

typedef struct {
  actor_t a[N_ACTORS];
  proj_t p[MAX_PROJ];
  beam_t beam[MAX_BEAM];
  blast_t blast[MAX_BLAST];
  part_t part[MAX_PART];
  float time_left, clock;
  int phase;               /* PH_* */
  float phase_t;
  int difficulty;
  uint16_t col[2], col_dark[2];
  const char *team_name[2];
  int32_t cells[3];        /* ink cells: none, Alpha, Bravo */
  int32_t paintable;
  char msg[40];
  float msg_t;
  bool demo;               /* every actor is a bot (host test) */
} game_t;

enum { PH_INTRO, PH_PLAY, PH_FINISH, PH_DONE };

extern game_t G;

/* map.c */
int col_kind(int tx, int ty);              /* 0.5 m columns; outside the stage = water */
float col_top(int tx, int ty);             /* metres */
float ground_at(float x, float y);
int col_colour(int tx, int ty);            /* index into map_pal */
uint16_t map_colour(int i);
int paint_get(int px, int py);
void paint_clear(void);
float paint_disc(float x, float y, float z, float r, int team);   /* m² newly inked for the team */
int paint_at(float x, float y);                                   /* 0 none, 1 Alpha, 2 Bravo */
float paint_share(float x, float y, float r, int team, float *enemy);
bool line_clear(float x0, float y0, float z0, float x1, float y1, float z1);
void nav_init(void);
int nav_path(float x0, float y0, float x1, float y1, uint16_t *out, int max);
bool nav_ok(int nx, int ny);
void spawn_point(int team, float *x, float *y);
int col_surface(int tx, int ty);           /* 0 plain, 1 brick, 2 terrazzo, 3 steps */
int col_light(int tx, int ty);             /* map_light byte */
extern const float *const SUN;   /* towards the sun */
extern uint8_t scratch[];                  /* shared: the renderer's push strip, the path search's queue */
#define SCRATCH_BYTES 11264

/* sim.c */
void game_new(int weapon, int difficulty, bool demo);
void game_step(float dt);
float weapon_range(int w);
float team_share(int team);   /* 0..1 of the stage's ink-able floor */
extern const char *const WEAPON_NAME[W_COUNT];
extern const char *const DIFF_NAME[3];
extern const float DIFF_REACTION[3], DIFF_AIM_ERR[3], DIFF_AWARE[3], DIFF_TURN[3];

/* bots.c */
void bots_reset(void);
void bot_think(actor_t *a, float dt);

/* render.c: draws straight to the screen, in vertical strips */
enum { Q_FAST, Q_BALANCED, Q_FULL };   /* 160x120, 160x240, 320x240 */
extern const char *const QUALITY_NAME[3];
void render_init(void);
void render_set_quality(int q);
void render_frame(int fps, int frame_ms, bool show_fps);
/* benchmark switches: leave parts of the frame out, to time them on the calculator */
enum { RD_NOPUSH = 1, RD_NOSKY = 2, RD_NOTEX = 4, RD_NOWATER = 8, RD_NOSPRITES = 16, RD_NOHUD = 32 };
extern int render_debug;
int render_bench_raw(void);   /* ms for one frame's worth of pixel and depth writes, no drawing */
void render_map(void);                          /* pause screen: the whole stage from above */
void render_title(float angle, void (*overlay)(void));   /* the camera circles the plaza */
/* HUD drawing for overlays, in a virtual 160 x 120 space whatever the resolution */
void text(int x, int y, const char *s, uint16_t c, int scale);
void text_o(int x, int y, const char *s, uint16_t c, int scale);   /* with a dark outline */
int text_w(const char *s, int scale);
void frame_fill(int x, int y, int w, int h, uint16_t c);
#define VW 160
#define VH 120

/* rng / maths */
uint32_t rnd(void);
float frnd(void);              /* 0..1 */
float frnd2(void);             /* -1..1 */
void rnd_seed(uint32_t s);
float wrap_angle(float a);
#define PI_F 3.14159265f
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#endif
