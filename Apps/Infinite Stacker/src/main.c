/* Infinite Stacker for NumWorks: menu, input and the frame loop.
 *   OK or EXE: drop the block (play, restart)   Back: menu (quit from the menu)   0: frame counter
 * The keyboard is read every millisecond between frames, and a drop is judged where the block was at the
 * moment the key went down, as the original judges a tap at its event time. */
#include <eadk.h>
#include "game.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Infinite Stacker";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define FRAME_MS 16

enum { M_MENU, M_PLAY, M_FALL, M_CARD };

static uint64_t t_base;
static int32_t now_ms(void) { return (int32_t)(eadk_timing_millis() - t_base); }

int main(void) {
  t_base = eadk_timing_millis();
  render_init();
  G.rng = eadk_random() | 1;
  game_reset(now_ms());

  hud_t h = {0};
  int mode = M_MENU, best = 0;
  int32_t instr_hide = -1, frames_t = now_ms();
  int frames = 0;
  eadk_keyboard_state_t prev = eadk_keyboard_scan();   /* the OK that opened the app is still down */

  for (;;) {
    int32_t t0 = now_ms();
    game_update(t0);
    if (mode == M_PLAY && G.state == GS_OVER) mode = M_FALL;
    if (mode == M_FALL && G.now >= G.over_at) {
      mode = M_CARD;
      h.card_at = G.now;
      h.new_best = game_score() > best;
      if (h.new_best) best = game_score();
    }

    h.mode = mode == M_MENU ? 0 : mode == M_CARD ? 2 : 1;
    h.show_score = mode != M_MENU;
    h.best = best;
    if (mode != M_PLAY) h.instr_alpha = 0;
    else if (instr_hide < 0) h.instr_alpha = 1;
    else {
      float p = (G.now - instr_hide) / 500.0f;
      h.instr_alpha = p >= 1 ? 0 : 1 - p;
    }
    render_frame(&h);

    frames++;
    if (t0 - frames_t >= 1000) {
      h.fps = frames * 1000 / (t0 - frames_t);
      frames = 0;
      frames_t = t0;
    }

    /* until the next frame, watch the keys */
    for (;;) {
      eadk_keyboard_state_t k = eadk_keyboard_scan(), hit = k & ~prev;
      prev = k;
      int32_t t = now_ms();
      if (eadk_keyboard_key_down(k, eadk_key_home)) return 0;
      bool ok = eadk_keyboard_key_down(hit, eadk_key_ok) || eadk_keyboard_key_down(hit, eadk_key_exe);
      bool back = eadk_keyboard_key_down(hit, eadk_key_back);
      if (eadk_keyboard_key_down(hit, eadk_key_zero)) h.show_fps = !h.show_fps;
      if (mode == M_MENU) {
        if (back) return 0;
        if (ok) {
          game_start(t);
          mode = M_PLAY;
          instr_hide = -1;
          render_invalidate();
          break;
        }
      } else if (mode == M_PLAY) {
        if (back) {
          game_reset(t);
          mode = M_MENU;
          render_invalidate();
          break;
        }
        if (ok) {
          game_update(t);
          game_press(t);
          if (instr_hide < 0) instr_hide = t;
        }
      } else {   /* falling or the game-over card */
        if (back) {
          mode = M_MENU;
          break;
        }
        if (ok && mode == M_CARD && t - h.card_at > 600) {
          game_start(t);
          mode = M_PLAY;
          instr_hide = -1;
          render_invalidate();
          break;
        }
      }
      if (t - t0 >= FRAME_MS) break;
      eadk_timing_msleep(1);
    }
  }
}
