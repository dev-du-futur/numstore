/* Inkwave for NumWorks: menus, input and the frame loop.
 *   , walk forward   sqrt back   i left   x^y right (also pi / x^2 for left / right, as in NumBlocks)
 *   Arrows: look (turn, look up and down)
 *   OK or EXE: fire (hold)   Shift: squid form (hold)   Alpha: bomb   Back: pause   0: frame counter */
#include <eadk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Inkwave";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define FRAME_MS 33   /* 30 fps */

static bool down(eadk_keyboard_state_t k, eadk_key_t key) { return eadk_keyboard_key_down(k, key); }

static void wait_release(void) {
  while (eadk_keyboard_scan() != 0) eadk_timing_msleep(10);
}

/* ------------------------------------------------------------------ text screens (full resolution) */
#define C_BG RGB(24, 28, 44)
#define C_TXT RGB(235, 235, 240)
#define C_DIM RGB(150, 156, 180)
#define C_HI RGB(255, 170, 40)

static void say_at(int x, int y, const char *s, eadk_color_t c, bool big) {
  eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, big, c, C_BG);
}
static void page(const char *title, const char *const *lines) {
  eadk_display_push_rect_uniform(eadk_screen_rect, C_BG);
  eadk_display_push_rect_uniform((eadk_rect_t){0, 0, 320, 30}, RGB(255, 138, 20));
  eadk_display_draw_string(title, (eadk_point_t){10, 6}, true, 0xFFFF, RGB(255, 138, 20));
  for (int i = 0; lines[i]; i++) say_at(8, 38 + i * 15, lines[i], lines[i][0] == ' ' ? C_DIM : C_TXT, false);
  say_at(8, 222, "Back: return", C_DIM, false);
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (down(k, eadk_key_back) || down(k, eadk_key_ok) || down(k, eadk_key_exe)) break;
    eadk_timing_msleep(20);
  }
  wait_release();
}
static void how_to_play(void) {
  static const char *const L[] = {
    "Turf War, 4 against 4, 3 minutes. The team that",
    "has inked more of the ground at the end wins.",
    ",  / sqrt     walk forward / back",
    "i  / x^y      step left / right (pi, x2 too)",
    "Arrows        look around",
    "OK or EXE     fire (hold)",
    "Shift         squid form (hold): swim fast and",
    "              refill in your own ink, hidden",
    "Alpha         throw a bomb (70% of your ink)",
    "Back          pause and map    0  frame counter",
    "Graphics: Auto keeps 30 fps. Full looks best but",
    " runs at about 25 fps on the calculator.",
    "Enemy ink slows and hurts you. Rest or swim to heal.",
    NULL};
  page("How to play", L);
}
static void about(void) {
  static const char *const L[] = {
    "A blocky 3D remake of Inkwave's Turf War for the",
    "NumWorks calculator: Tidewater Plaza, the",
    "Spritzer, Swell Roller, Glint Charger and bombs.",
    "",
    "Based on INKWAVE by Jayden Davis",
    " github.com/jaydendavisnc/inkwave",
    " MIT licence, (c) 2026 Jayden Davis",
    "Its stage layout, names and numbers are used here.",
    "",
    "Calculator version: Awe, for Numstore.",
    NULL};
  page("About", L);
}

/* ------------------------------------------------------------------ results */
static void results(void) {
  eadk_display_push_rect_uniform(eadk_screen_rect, C_BG);
  float s0 = team_share(0) * 100, s1 = team_share(1) * 100;
  bool win = s0 > s1;
  const char *head = s0 == s1 ? "Draw!" : win ? "You win!" : "You lose...";
  eadk_display_push_rect_uniform((eadk_rect_t){0, 0, 320, 34}, win ? G.col[0] : G.col[1]);
  eadk_display_draw_string(head, (eadk_point_t){(uint16_t)(160 - (int)strlen(head) * 5), 8}, true, 0xFFFF, win ? G.col[0] : G.col[1]);
  /* the two bars grow, like the turf count at the end of an Inkwave match */
  char b[64];
  for (int f = 0; f <= 30; f++) {
    float k = f / 30.0f;
    int w0 = (int)(s0 * 2.8f * k), w1 = (int)(s1 * 2.8f * k);
    eadk_display_push_rect_uniform((eadk_rect_t){20, 44, 280, 18}, RGB(60, 64, 80));
    eadk_display_push_rect_uniform((eadk_rect_t){20, 44, (uint16_t)w0, 18}, G.col[0]);
    eadk_display_push_rect_uniform((eadk_rect_t){20, 68, 280, 18}, RGB(60, 64, 80));
    eadk_display_push_rect_uniform((eadk_rect_t){20, 68, (uint16_t)w1, 18}, G.col[1]);
    eadk_timing_msleep(25);
  }
  snprintf(b, sizeof b, "Alpha (%s) %d.%d%%   ", G.team_name[0], (int)(s0 * 10) / 10, (int)(s0 * 10) % 10);
  say_at(20, 90, b, C_TXT, false);
  snprintf(b, sizeof b, "Bravo (%s) %d.%d%%   ", G.team_name[1], (int)(s1 * 10) / 10, (int)(s1 * 10) % 10);
  say_at(20, 104, b, C_TXT, false);
  say_at(20, 124, "Name        Weapon         Turf  Splats", C_DIM, false);
  for (int i = 0; i < N_ACTORS; i++) {
    const actor_t *a = &G.a[i];
    snprintf(b, sizeof b, "%-11s %-14s %4dp  %d/%d", a->name, WEAPON_NAME[a->weapon], (int)a->turf, a->splats, a->deaths);
    eadk_display_push_rect_uniform((eadk_rect_t){10, (uint16_t)(141 + i * 11), 6, 6}, G.col[a->team]);
    eadk_display_draw_string(b, (eadk_point_t){20, (uint16_t)(137 + i * 11)}, false, i == 0 ? C_HI : C_TXT, C_BG);
  }
  say_at(20, 226, "OK: continue", C_DIM, false);
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (down(k, eadk_key_ok) || down(k, eadk_key_exe) || down(k, eadk_key_back)) break;
    eadk_timing_msleep(20);
  }
  wait_release();
}

/* ------------------------------------------------------------------ match */
static bool pause_menu(void) {   /* true: quit the match */
  render_map();
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (down(k, eadk_key_ok) || down(k, eadk_key_exe)) { wait_release(); return false; }
    if (down(k, eadk_key_back)) { wait_release(); return true; }
    eadk_timing_msleep(20);
  }
}

static bool show_fps = true;

/* gfx 0 (auto) starts at Balanced (Full measured 40 ms a frame on an N0120, Balanced 28) and steps down to Fast
 * if the game can't keep 30 fps */
static void play(int weapon, int difficulty, int gfx) {
  int q = gfx ? gfx - 1 : Q_BALANCED, slow = 0;
  render_set_quality(q);
  game_new(weapon, difficulty, false);
  wait_release();
  uint64_t last = eadk_timing_millis(), sec = last;
  int frames = 0, fps = 0, work_ms = 0, work_sum = 0;
  float turn_t = 0;
  bool zero_was = false;
  for (;;) {
    uint64_t t0 = eadk_timing_millis();
    float dt = (t0 - last) / 1000.0f;
    last = t0;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.001f) dt = 0.001f;

    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (down(k, eadk_key_back)) {
      if (pause_menu()) return;
      last = eadk_timing_millis();
      continue;
    }
    bool zero = down(k, eadk_key_zero);
    if (zero && !zero_was) show_fps = !show_fps;
    zero_was = zero;
    actor_t *me = &G.a[0];
    intent_t *in = &me->in;
    in->fwd = (float)down(k, eadk_key_comma) - (float)down(k, eadk_key_sqrt);
    in->side = (float)(down(k, eadk_key_power) || down(k, eadk_key_square)) -
               (float)(down(k, eadk_key_imaginary) || down(k, eadk_key_pi));
    int td = (int)down(k, eadk_key_right) - (int)down(k, eadk_key_left);
    turn_t = td ? turn_t + dt : 0;
    float rate = 1.6f + 1.8f * clampf(turn_t / 0.4f, 0, 1);   /* fine at first, faster when held */
    in->turn = td * rate * dt;
    in->look = ((int)down(k, eadk_key_up) - (int)down(k, eadk_key_down)) * 1.2f * dt;
    in->fire = down(k, eadk_key_ok) || down(k, eadk_key_exe);
    in->squid = down(k, eadk_key_shift);
    in->bomb = down(k, eadk_key_alpha);

    game_step(dt);
    if (G.phase == PH_DONE) { results(); return; }
    render_frame(fps, work_ms, show_fps);

    frames++;
    uint64_t t1 = eadk_timing_millis();
    work_sum += (int)(t1 - t0);
    if (t1 - sec >= 1000) {
      fps = frames;
      work_ms = frames ? work_sum / frames : 0;
      frames = 0;
      work_sum = 0;
      sec = t1;
      if (!gfx && q > Q_FAST && G.phase != PH_INTRO) {
        slow = fps < 29 ? slow + 1 : 0;
        if (slow >= 2) { render_set_quality(--q); slow = 0; }
      }
    }
    int spent = (int)(t1 - t0);
    eadk_timing_msleep(spent < FRAME_MS - 1 ? FRAME_MS - spent : 1);   /* always yield a little */
  }
}

/* ------------------------------------------------------------------ benchmark (0 on the title screen) */
/* Times the parts of a frame on the calculator: a demo match is played for 30 s without drawing, then the same
 * frame is drawn 20 times per line with parts left out. The ms are per frame. */
static int bench_ms(int q, int flags) {
  render_set_quality(q);
  render_debug = flags;
  int sum = 0;
  for (int i = 0; i < 20; i++) {
    uint64_t a = eadk_timing_millis();
    render_frame(30, 0, true);
    sum += (int)(eadk_timing_millis() - a);
    eadk_timing_msleep(2);
  }
  render_debug = 0;
  return sum * 10 / 20;   /* tenths of a ms */
}
static void benchmark(void) {
  eadk_display_push_rect_uniform(eadk_screen_rect, C_BG);
  say_at(8, 8, "Benchmark: playing 30 s of a demo match...", C_TXT, false);
  rnd_seed(7);
  game_new(W_SHOOTER, D_FRESH, true);
  int sim = 0;
  for (int i = 0; i < 900; i++) {
    uint64_t a = eadk_timing_millis();
    game_step(1.0f / 30);
    if (i >= 600) sim += (int)(eadk_timing_millis() - a);
    eadk_timing_msleep(1);
  }
  static const struct { const char *name; int q, flags; } R[] = {
    {"Full", Q_FULL, 0},
    {"Full, no push", Q_FULL, RD_NOPUSH},
    {"  and no sky", Q_FULL, RD_NOPUSH | RD_NOSKY},
    {"  and no ground texture", Q_FULL, RD_NOPUSH | RD_NOTEX},
    {"Raw pixel writes (Full)", -1, 0},
    {"  and no kids/shots", Q_FULL, RD_NOPUSH | RD_NOSPRITES},
    {"  and no HUD", Q_FULL, RD_NOPUSH | RD_NOHUD},
    {"  and none of these", Q_FULL, RD_NOPUSH | RD_NOSKY | RD_NOTEX | RD_NOWATER | RD_NOSPRITES | RD_NOHUD},
    {"Balanced", Q_BALANCED, 0},
    {"Balanced, no push", Q_BALANCED, RD_NOPUSH},
    {"Fast", Q_FAST, 0},
    {"Fast, no push", Q_FAST, RD_NOPUSH},
  };
  int ms[12];
  for (int i = 0; i < 12; i++) ms[i] = R[i].q < 0 ? render_bench_raw() : bench_ms(R[i].q, R[i].flags);
  eadk_display_push_rect_uniform(eadk_screen_rect, C_BG);
  eadk_display_push_rect_uniform((eadk_rect_t){0, 0, 320, 30}, RGB(255, 138, 20));
  eadk_display_draw_string("Benchmark (ms per frame)", (eadk_point_t){10, 6}, true, 0xFFFF, RGB(255, 138, 20));
  char b[48];
  for (int i = 0; i < 12; i++) {
    snprintf(b, sizeof b, "%-26s %3d.%d", R[i].name, ms[i] / 10, ms[i] % 10);
    say_at(8, 36 + i * 14, b, C_TXT, false);
  }
  snprintf(b, sizeof b, "%-26s %3d.%d", "Game logic (8 bots)", sim / 300, sim * 10 / 300 % 10);
  say_at(8, 36 + 12 * 14, b, C_HI, false);
  say_at(8, 222, "Back: return", C_DIM, false);
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (down(k, eadk_key_back) || down(k, eadk_key_ok) || down(k, eadk_key_exe)) break;
    eadk_timing_msleep(20);
  }
  wait_release();
  paint_clear();
  render_set_quality(Q_BALANCED);
}

/* ------------------------------------------------------------------ title */
static int sel = 0, weapon = W_SHOOTER, diff = D_FRESH, gfx = 0;   /* gfx: 0 auto, else Q_* + 1 */
static const char *const GFX_NAME[4] = {"Auto", "Fast", "Balanced", "Full"};
static const char *const ITEMS[7] = {"PLAY", "WEAPON", "BOTS", "GRAPHICS", "HOW TO PLAY", "ABOUT", "QUIT"};

static void title_overlay(void) {
  text(VW / 2 - text_w("INKWAVE", 3) / 2 + 1, 9, "INKWAVE", RGB(30, 20, 40), 3);
  text(VW / 2 - text_w("INKWAVE", 3) / 2, 8, "INKWAVE", RGB(255, 138, 20), 3);
  text_o(VW / 2 - text_w("TURF WAR", 1) / 2, 32, "TURF WAR", 0xFFFF, 1);
  for (int i = 0; i < 7; i++) {
    char b[40];
    if (i == 1) snprintf(b, sizeof b, "< %s >", WEAPON_NAME[weapon]);
    else if (i == 2) snprintf(b, sizeof b, "< BOTS: %s >", DIFF_NAME[diff]);
    else if (i == 3) snprintf(b, sizeof b, "< GRAPHICS: %s >", GFX_NAME[gfx]);
    else snprintf(b, sizeof b, "%s", ITEMS[i]);
    int y = 44 + i * 10, w = text_w(b, 1), x = VW / 2 - w / 2;
    frame_fill(x - 4, y - 2, w + 8, 10, i == sel ? RGB(255, 138, 20) : RGB(24, 28, 44));
    text(x, y, b, 0xFFFF, 1);
  }
  text_o(2, VH - 9, "BASED ON INKWAVE (MIT)", RGB(230, 230, 240), 1);
}

int main(void) {
  rnd_seed(eadk_random());
  nav_init();
  render_init();
  render_set_quality(Q_BALANCED);
  paint_clear();
  float orbit = 0;
  eadk_keyboard_state_t prev = 0;
  G.col[0] = RGB(255, 138, 20);
  G.col[1] = RGB(47, 91, 255);
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan(), hit = k & ~prev;
    prev = k;
    if (down(hit, eadk_key_up)) sel = (sel + 6) % 7;
    if (down(hit, eadk_key_down)) sel = (sel + 1) % 7;
    int lr = (int)down(hit, eadk_key_right) - (int)down(hit, eadk_key_left);
    if (sel == 1 && lr) weapon = (weapon + W_COUNT + lr) % W_COUNT;
    if (sel == 2 && lr) diff = (diff + 3 + lr) % 3;
    if (sel == 3 && lr) gfx = (gfx + 4 + lr) % 4;
    if (down(hit, eadk_key_back)) return 0;
    if (down(hit, eadk_key_zero)) { benchmark(); prev = eadk_keyboard_scan(); continue; }
    if (down(hit, eadk_key_ok) || down(hit, eadk_key_exe)) {
      if (sel == 0) { play(weapon, diff, gfx); paint_clear(); render_set_quality(Q_BALANCED); }
      else if (sel == 4) how_to_play();
      else if (sel == 5) about();
      else if (sel == 6) return 0;
      prev = eadk_keyboard_scan();
      continue;
    }
    orbit += 0.004f;
    G.clock += 0.03f;
    render_title(orbit, title_overlay);
    eadk_timing_msleep(10);
  }
}
