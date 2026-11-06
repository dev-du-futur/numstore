/* Circle Tap screens: the start menu, the game (HUD and game-over menu) and the maps list.
 * Layout and animations after index.html, ui.js and the ui-*.css files of the original, sized for 320x240. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define C_TEXT RGB(0xdc, 0xdc, 0xdc)
#define C_ACCENT RGB(0xe9, 0x45, 0x60)
#define C_HINT RGB(0x8c, 0x90, 0xa8)
#define C_GOLD RGB(0xff, 0xd7, 0x00)

ui_t U;

/* ------------------------------------------------------------------ CSS easing */
static float bez(float a, float b, float t) { float u = 1 - t; return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t; }
static float cubic_bezier(float x1, float y1, float x2, float y2, float x) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  float lo = 0, hi = 1, t = x;
  for (int i = 0; i < 20; i++) {
    t = (lo + hi) / 2;
    if (bez(x1, x2, t) < x) lo = t; else hi = t;
  }
  return bez(y1, y2, t);
}
static float ease_in_out(float x) { return cubic_bezier(0.42f, 0, 0.58f, 1, x); }
static float ease_out(float x) { return cubic_bezier(0, 0, 0.58f, 1, x); }
static float ease_in(float x) { return cubic_bezier(0.42f, 0, 1, 1, x); }

/* .blink: opacity 1 -> 0.3 -> 1 over 2 s */
static float blink(float t) {
  float p = fmodf(t, 2) / 2;
  return p < 0.5f ? 1 - 0.7f * ease_in_out(p * 2) : 0.3f + 0.7f * ease_in_out(p * 2 - 1);
}

/* ------------------------------------------------------------------ pieces */
static const view_t VIEW_MAIN = {160, 120, 240};
static const view_t VIEW_PREVIEW = {226, 126, 196};

static void diff_bars(float x, float y, int diff, bool lit, float bw, float bh, float gap, float rad) {
  static const uint32_t COL[3] = {RGB(0x4c, 0xaf, 0x50), RGB(0xf0, 0x7b, 0x3f), RGB(0xe9, 0x45, 0x60)};
  for (int i = 0; i < 3; i++) {
    uint32_t c = lit && i <= diff ? COL[diff] : RGB(0x44, 0x44, 0x44);
    if (bw < 5 && !(lit && i <= diff)) c = RGB(0x55, 0x55, 0x55);
    r_rrect(x + i * (bw + gap), y, bw, bh, rad, c, c, 1, 0, 0);
  }
}

/* #difficulty-indicator: three bars in a dark box, top right */
static void diff_indicator(int diff) {
  r_rrect(270, 8, 42, 32, 6, 0, 0, 0.3f, 0, 0);
  diff_bars(278, 16, diff, true, 6, 16, 4, 2);
}

/* .difficulty-selector: Easy / Med / Hard */
static void diff_selector(float y, int active) {
  static const char *const L[3] = {"EASY", "MED", "HARD"};
  for (int i = 0; i < 3; i++) {
    bool on = i == active;
    float w = 62, h = 36, cx = 160 + (i - 1) * 72, cy = y + h / 2;
    if (on) w *= 1.05f, h *= 1.05f;
    r_rrect(cx - w / 2, cy - h / 2, w, h, 8, 0xFFFFFF, 0xFFFFFF, on ? 0.15f : 0.05f, 0xFFFFFF, on ? 0.3f : 0.1f);
    diff_bars(cx - 8, cy - 11, i, on, 4, 10, 2, 1);
    r_text(F_S, cx, cy + 12, AL_CENTER, L[i], on ? 0xFFFFFF : RGB(0x88, 0x88, 0x88), 1, 0.5f, 1);
  }
}

static void button(float cy, const char *label, bool red, bool focus) {
  float w = 160, h = 26, s = focus ? 1.05f : 1;
  w *= s, h *= s;
  uint32_t c1 = red ? RGB(0xe9, 0x45, 0x60) : RGB(0x0f, 0x34, 0x60), c2 = red ? RGB(0x2a, 0x0a, 0x10) : RGB(0x08, 0x1b, 0x33);
  /* box-shadow: a soft glow of the button colour, stronger on hover (focus here) */
  r_rrect(160 - w / 2 - 3, cy - h / 2 - 1, w + 6, h + 8, 11, c1, c1, focus ? 0.28f : 0.14f, 0, 0);
  r_rrect(160 - w / 2, cy - h / 2, w, h, 8, c1, c2, 1, focus ? 0xFFFFFF : c1, focus ? 0.9f : 1);
  r_text(F_M, 160, cy + font_m.cap / 2.0f, AL_CENTER, label, red ? C_TEXT : 0xFFFFFF, 1, 0, s);
}

static void hint(const char *s) { r_text(F_S, 160, 234, AL_CENTER, s, C_HINT, 1, 0, 1); }

static void title(float y) {
  /* text-shadow: 0 0 20px rgba(255,255,255,0.2) -- a faint halo */
  r_text(F_TITLE, 160, y, AL_CENTER, "CIRCLE TAP", C_TEXT, 1, 4, 1);
}

/* ------------------------------------------------------------------ level number (#level-display) */
static void level_draw(void) {
  if (!U.lvl_visible) return;
  char b[8];
  snprintf(b, sizeof b, "%d", U.lvl_shown);
  float w = r_text_w(F_LEVEL, b, 0), h = 88;   /* the box: text width x line height */
  float s = 1, ox = 0, oy = 0, op = 1;
  if (U.lvl_phase == 1) {   /* bounceIn 0.5 s */
    float p = U.lvl_t / 0.5f;
    if (p < 0.6f) {
      float e = cubic_bezier(0.175f, 0.885f, 0.32f, 1.275f, p / 0.6f);
      s = 0.1f + 1.1f * e;
      float tr = -0.5f * (1 - e);
      ox = s * tr * w, oy = s * tr * h;
      op = fminf(1, e);
    } else {
      float e = cubic_bezier(0.175f, 0.885f, 0.32f, 1.275f, (p - 0.6f) / 0.4f);
      s = 1.2f - 0.2f * e;
    }
  } else if (U.lvl_phase == 2) {   /* bounceOut 0.4 s */
    float e = cubic_bezier(0.6f, -0.28f, 0.735f, 0.045f, U.lvl_t / 0.4f);
    s = 1 + 0.5f * e;
    ox = s * 1.5f * w * e;
    op = 1 - e;
  }
  if (op < 0) op = 0;
  if (op > 1) op = 1;
  /* rgba(220,220,220,0.5) */
  r_text(F_LEVEL, 160 + ox, 120 + oy + font_level.cap / 2.0f, AL_CENTER, b, C_TEXT, 0.5f * op, 0, s);
}

void ui_level_in(int level) {
  U.lvl_visible = true;
  U.lvl_shown = level;
  U.lvl_phase = 1;
  U.lvl_t = 0;
}
static void level_up(int level) {
  U.lvl_next = level;
  if (U.lvl_phase != 2) { U.lvl_phase = 2; U.lvl_t = 0; }
}
static void level_tick(float dt) {
  if (!U.lvl_phase) return;
  U.lvl_t += dt;
  if (U.lvl_phase == 1 && U.lvl_t >= 0.5f) U.lvl_phase = 0;
  if (U.lvl_phase == 2 && U.lvl_t >= 0.4f) { U.lvl_shown = U.lvl_next; U.lvl_phase = 1; U.lvl_t = 0; }
}

/* ------------------------------------------------------------------ screens */
static void go_start(void) {
  U.screen = SCR_START;
  G.mode = M_STANDARD;
  G.map = 0;
  G.diff = U.diff;
  game_reset();
  U.lvl_visible = false;
}

static void to_maps(void) {
  U.screen = SCR_MAPS;
  G.mode = M_STANDARD;
  G.map = &MAPS[U.map_sel];
  G.diff = U.diff;
  game_reset();
  U.lvl_visible = false;
}

void ui_init(void) {
  memset(&U, 0, sizeof U);
  game_init(D_EASY);
  go_start();
}

static void start_game(int mode, const map_t *map) {
  int diff = mode == M_ZEN ? D_EASY : U.diff;   /* Zen always runs on Easy, as in the original */
  game_start(mode, diff, map);
  U.screen = SCR_PLAY;
  U.go_t = -1;
  U.pop_t = 1;
  U.new_best = false;
  ui_level_in(1);
}

static void record_best(void) {
  if (G.mode == M_STANDARD) {
    if (G.score > U.best[G.diff]) { U.best[G.diff] = G.score; U.new_best = G.score > 0; }
  } else if (G.mode == M_MAP && G.score > 0) {
    int mi = (int)(G.map - MAPS);
    map_best_t *b = &U.map_best[mi][G.diff];
    if (G.level > b->level || (G.level == b->level && G.score > b->score)) { b->level = G.level; b->score = G.score; U.new_best = true; }
  }
}

static bool hit(int key) { return U.pressed & (1ull << key); }

bool ui_frame(float dt, uint64_t pressed) {
  U.pressed = pressed;
  U.t += dt;
  bool ok = hit(K_OK) || hit(K_EXE), back = hit(K_BACK);
  int lr = (int)hit(K_RIGHT) - (int)hit(K_LEFT), ud = (int)hit(K_DOWN) - (int)hit(K_UP);

  /* ---------------- input */
  if (U.screen == SCR_START) {
    if (back) return false;
    if (lr) { U.diff = (U.diff + 3 + lr) % 3; game_set_difficulty(U.diff); }
    if (ud) U.focus = (U.focus + 3 + ud) % 3;
    if (ok) {
      if (U.focus == 0) start_game(M_STANDARD, 0);
      else if (U.focus == 1) start_game(M_ZEN, 0);
      else { U.screen = SCR_MAPS; G.map = &MAPS[U.map_sel]; game_set_difficulty(U.diff); }
    }
  } else if (U.screen == SCR_MAPS) {
    if (back) { go_start(); U.focus = 2; }
    else {
      if (lr) { U.diff = (U.diff + 3 + lr) % 3; game_set_difficulty(U.diff); }
      if (ud) { U.map_sel = (U.map_sel + N_MAPS + ud) % N_MAPS; G.map = &MAPS[U.map_sel]; game_set_difficulty(U.diff); }
      if (ok) start_game(M_MAP, &MAPS[U.map_sel]);
    }
  } else if (G.state == GS_PLAYING) {
    if (back) {
      if (G.mode == M_MAP) to_maps();
      else go_start();
    } else if (ok) {
      int r = game_tap();
      if (r == TAP_HIT || r == TAP_LEVELUP) U.pop_t = 0;
      if (r == TAP_LEVELUP) level_up(G.level);
      if (r == TAP_MISS && G.mode == M_ZEN) level_up(G.level);
      if (r == TAP_GAMEOVER) { U.go_t = 0; record_best(); }
    }
  } else {   /* game over */
    float menu_at = G.mode == M_STANDARD ? 0.4f : 0;
    if (back) {
      if (G.mode == M_MAP) to_maps();
      else go_start();
    } else if (U.go_t >= menu_at) {
      if (lr) { U.diff = (U.diff + 3 + lr) % 3; game_set_difficulty(U.diff); }
      if (ok && U.go_t >= menu_at + 0.5f) { game_reset(); start_game(G.mode, G.map); }
    }
  }

  /* ---------------- update */
  game_update(dt);
  r_stars_update(dt);
  level_tick(dt);
  if (U.pop_t < 1) U.pop_t += dt;
  if (U.go_t >= 0) U.go_t += dt;
  if (U.screen == SCR_PLAY && G.state == GS_GAMEOVER) {
    float menu_at = G.mode == M_STANDARD ? 0.4f : 0;
    if (U.go_t >= menu_at + 3) U.lvl_visible = false;   /* the level number fades out 3 s after the menu */
  }

  /* ---------------- draw */
  char b[48];
  r_begin();
  if (U.screen == SCR_START) {
    r_scene(&VIEW_MAIN, true);
    r_dim(0, 0.1f);
    if (U.best[U.diff] > 0) {
      snprintf(b, sizeof b, "Best: %d", U.best[U.diff]);
      r_text(F_S, 8, 18, AL_LEFT, b, C_GOLD, 1, 0, 1);
    }
    title(50);
    r_text(F_M, 160, 79, AL_CENTER, "PRESS OK TO START", C_ACCENT, U.focus == 0 ? blink(U.t) : 0.35f, 2, 1);
    diff_selector(92, U.diff);
    button(154, "ZEN MODE", true, U.focus == 1);
    button(188, "MAPS", false, U.focus == 2);
    hint("OK: select   Up/Down: menu   </>: difficulty");
  } else if (U.screen == SCR_MAPS) {
    r_scene(&VIEW_PREVIEW, true);
    r_text(F_M, 8, 21, AL_LEFT, "MAPS", C_TEXT, 1, 2, 1);
    diff_indicator(U.diff);
    for (int i = 0; i < N_MAPS; i++) {
      bool sel = i == U.map_sel;
      float y = 32 + i * 28;
      r_rrect(6, y, 132, 24, 8, 0xFFFFFF, 0xFFFFFF, sel ? 0.2f : 0.1f, C_ACCENT, sel ? 0.9f : 0);
      r_text(F_M, 14, y + 17, AL_LEFT, MAPS[i].name, C_TEXT, 1, 0, 1);
      snprintf(b, sizeof b, "%d", MAPS[i].lives);
      r_text(F_S, 118, y + 16, AL_RIGHT, b, RGB(0xff, 0x44, 0x44), 1, 0, 1);
      r_heart(120, y + 7, 10, RGB(0xff, 0, 0), 1);
    }
    const map_best_t *mb = &U.map_best[U.map_sel][U.diff];
    if (mb->level) snprintf(b, sizeof b, "Best (%s): Lv.%d  %d pts", DIFF_NAME[U.diff], mb->level, mb->score);
    else snprintf(b, sizeof b, "No scores yet (%s)", DIFF_NAME[U.diff]);
    r_text(F_S, 8, 217, AL_LEFT, b, mb->level ? C_GOLD : RGB(0x88, 0x88, 0x88), 1, 0, 1);
    hint("OK: play   Up/Down: map   </>: difficulty");
  } else {
    r_scene(&VIEW_MAIN, true);
    level_draw();
    float menu_at = G.mode == M_STANDARD ? 0.4f : 0;
    bool menu = G.state == GS_GAMEOVER && U.go_t >= menu_at;
    if (!menu) {
      /* #score-display, with its pop when it goes up */
      float s = 1;
      if (U.pop_t < 0.3f) {
        float p = U.pop_t / 0.3f;
        s = p < 0.35f ? 1 + 0.25f * ease_out(p / 0.35f) : 1.25f - 0.25f * ease_out((p - 0.35f) / 0.65f);
      }
      snprintf(b, sizeof b, "Score: %d", G.score);
      r_text(F_SCORE, 10, 30, AL_LEFT, b, 0, 0.3f, 0, s);
      r_text(F_SCORE, 10, 28, AL_LEFT, b, C_ACCENT, 1, 0, s);
    }
    if (G.mode == M_MAP && !menu) {
      for (int i = 0; i < G.max_lives; i++) {
        bool alive = i < G.lives;
        r_heart(312 - 14 - (G.max_lives - 1 - i) * 18, 12, 14, alive ? RGB(0xff, 0, 0) : RGB(0x33, 0x33, 0x33), alive ? 1 : 0.5f);
      }
    } else {
      diff_indicator(G.diff);
    }
    if (G.mode == M_ZEN) {
      float w = r_text_w(F_S, "ZEN MODE", 0) + 20;
      r_rrect(160 - w / 2, 4, w, 18, 9, 0, 0, 0.6f, 0, 0);
      r_text(F_S, 160, 17, AL_CENTER, "ZEN MODE", C_GOLD, 1, 0, 1);
    }
    if (menu) {
      float t = U.go_t - menu_at;
      title(56);
      snprintf(b, sizeof b, "Final Score: %d", G.score);
      r_text(F_M, 160, 86, AL_CENTER, b, 0, 0.8f, 0, 1);
      r_text(F_M, 160, 84, AL_CENTER, b, C_ACCENT, 1, 0, 1);
      /* "Tap to Restart": hidden for 0.5 s, fades in for 0.5 s, then blinks */
      float op = t < 0.5f ? 0 : t < 1 ? ease_in((t - 0.5f) / 0.5f) : blink(t - 1);
      r_text(F_S, 160, 112, AL_CENTER, "PRESS OK TO RESTART", C_TEXT, op, 2, 1);
      diff_selector(124, U.diff);
      if (U.new_best) r_text(F_S, 160, 184, AL_CENTER, "NEW BEST!", C_GOLD, 1, 2, 1);
      else if (G.mode == M_STANDARD && U.best[G.diff] > 0) {
        snprintf(b, sizeof b, "Best: %d", U.best[G.diff]);
        r_text(F_S, 160, 184, AL_CENTER, b, C_GOLD, 1, 0, 1);
      }
      hint(G.mode == M_MAP ? "OK: restart   </>: difficulty   Back: maps" : "OK: restart   </>: difficulty   Back: menu");
    }
  }
  if (U.show_fps) {
    snprintf(b, sizeof b, "%d fps  %d ms  %dk px", U.fps, U.work_ms, U.px_k);
    r_rrect(0, 0, 130, 14, 0, 0, 0, 0.7f, 0, 0);
    r_text(F_S, 4, 11, AL_LEFT, b, 0xFFFFFF, 1, 0, 1);
  }
  r_end();
  return true;
}
