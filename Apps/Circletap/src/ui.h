#ifndef UI_H
#define UI_H
#include "game.h"

/* key numbers, as in eadk.h */
enum { K_LEFT = 0, K_UP = 1, K_DOWN = 2, K_RIGHT = 3, K_OK = 4, K_BACK = 5, K_HOME = 6, K_ZERO = 48, K_EXE = 52 };
enum { SCR_START, SCR_PLAY, SCR_MAPS };

typedef struct { int level, score; } map_best_t;
typedef struct {
  int screen, focus, diff, map_sel;
  float t, go_t, pop_t;
  uint64_t pressed;
  bool lvl_visible;
  int lvl_shown, lvl_next, lvl_phase;
  float lvl_t;
  int best[D_COUNT];
  map_best_t map_best[N_MAPS][D_COUNT];
  bool new_best;
  bool show_fps;
  int fps, work_ms, px_k;
} ui_t;
extern ui_t U;

void ui_init(void);
void ui_level_in(int level);
bool ui_frame(float dt, uint64_t pressed);   /* false: quit */
#endif
