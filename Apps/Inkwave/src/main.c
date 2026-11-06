/* Inkwave for NumWorks: menus, input and the frame loop.
 *   Up / Down: walk   Left / Right: turn   4 / 6: step sideways
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
    "Up / Down     walk forward / back",
    "Left / Right  turn",
    "4 / 6         step sideways",
    "OK or EXE     fire (hold)",
    "Shift         squid form (hold): swim fast and",
    "              refill in your own ink, hidden",
    "Alpha         throw a bomb (70% of your ink)",
    "Back          pause and map    0  frame counter",
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
  frame_push();
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (down(k, eadk_key_ok) || down(k, eadk_key_exe)) { wait_release(); return false; }
    if (down(k, eadk_key_back)) { wait_release(); return true; }
    eadk_timing_msleep(20);
  }
}

static bool show_fps = true;

static void play(int weapon, int difficulty) {
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
    in->fwd = (float)down(k, eadk_key_up) - (float)down(k, eadk_key_down);
    in->side = (float)down(k, eadk_key_six) - (float)down(k, eadk_key_four);
    int td = (int)down(k, eadk_key_right) - (int)down(k, eadk_key_left);
    turn_t = td ? turn_t + dt : 0;
    float rate = 1.6f + 1.8f * clampf(turn_t / 0.4f, 0, 1);   /* fine at first, faster when held */
    in->turn = td * rate * dt;
    in->fire = down(k, eadk_key_ok) || down(k, eadk_key_exe);
    in->squid = down(k, eadk_key_shift);
    in->bomb = down(k, eadk_key_alpha);

    game_step(dt);
    if (G.phase == PH_DONE) { results(); return; }
    render_frame(fps, work_ms, show_fps);
    frame_push();

    frames++;
    uint64_t t1 = eadk_timing_millis();
    work_sum += (int)(t1 - t0);
    if (t1 - sec >= 1000) {
      fps = frames;
      work_ms = frames ? work_sum / frames : 0;
      frames = 0;
      work_sum = 0;
      sec = t1;
    }
    int spent = (int)(t1 - t0);
    eadk_timing_msleep(spent < FRAME_MS - 1 ? FRAME_MS - spent : 1);   /* always yield a little */
  }
}

/* ------------------------------------------------------------------ title */
int main(void) {
  rnd_seed(eadk_random());
  nav_init();
  paint_clear();
  int sel = 0, weapon = W_SHOOTER, diff = D_FRESH;
  float orbit = 0;
  eadk_keyboard_state_t prev = 0;
  static const char *const ITEMS[6] = {"PLAY", "WEAPON", "BOTS", "HOW TO PLAY", "ABOUT", "QUIT"};
  G.col[0] = RGB(255, 138, 20);
  wait_release();
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan(), hit = k & ~prev;
    prev = k;
    if (down(hit, eadk_key_up)) sel = (sel + 5) % 6;
    if (down(hit, eadk_key_down)) sel = (sel + 1) % 6;
    int lr = (int)down(hit, eadk_key_right) - (int)down(hit, eadk_key_left);
    if (sel == 1 && lr) weapon = (weapon + W_COUNT + lr) % W_COUNT;
    if (sel == 2 && lr) diff = (diff + 3 + lr) % 3;
    if (down(hit, eadk_key_back)) return 0;
    if (down(hit, eadk_key_ok) || down(hit, eadk_key_exe)) {
      if (sel == 0) { play(weapon, diff); paint_clear(); }
      else if (sel == 3) how_to_play();
      else if (sel == 4) about();
      else if (sel == 5) return 0;
      prev = eadk_keyboard_scan();
      continue;
    }
    orbit += 0.004f;
    render_orbit(orbit);
    text(RW / 2 - text_w("INKWAVE", 3) / 2 + 1, 9, "INKWAVE", RGB(30, 20, 40), 3);
    text(RW / 2 - text_w("INKWAVE", 3) / 2, 8, "INKWAVE", RGB(255, 138, 20), 3);
    text(RW / 2 - text_w("TURF WAR", 1) / 2, 32, "TURF WAR", 0xFFFF, 1);
    for (int i = 0; i < 6; i++) {
      char b[40];
      if (i == 1) snprintf(b, sizeof b, "< %s >", WEAPON_NAME[weapon]);
      else if (i == 2) snprintf(b, sizeof b, "< BOTS: %s >", DIFF_NAME[diff]);
      else snprintf(b, sizeof b, "%s", ITEMS[i]);
      int y = 48 + i * 11, w = text_w(b, 1), x = RW / 2 - w / 2;
      if (i == sel) frame_fill(x - 4, y - 2, w + 8, 11, RGB(255, 138, 20));
      else frame_fill(x - 4, y - 2, w + 8, 11, RGB(24, 28, 44));
      text(x, y, b, 0xFFFF, 1);
    }
    text(2, RH - 8, "BASED ON INKWAVE (MIT)", RGB(230, 230, 240), 1);
    frame_push();
    eadk_timing_msleep(20);
  }
}
