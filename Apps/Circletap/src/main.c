/* Circle Tap for NumWorks.
 *   OK or EXE: tap (and select in the menus)   Left / Right: difficulty   Up / Down: menu
 *   Back: back to the menu (quits from the start menu)   Home: quit   0: frame counter */
#include <eadk.h>
#include "ui.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Circle Tap";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define FRAME_MS 16   /* about 60 fps */

int main(void) {
  rnd_seed(eadk_random());
  ui_init();
  r_invalidate_all();
  eadk_keyboard_state_t prev = eadk_keyboard_scan();   /* keys held at launch don't count */
  uint64_t last = eadk_timing_millis(), sec = last;
  int frames = 0, work = 0;
  for (;;) {
    uint64_t t0 = eadk_timing_millis();
    float dt = (t0 - last) / 1000.0f;
    last = t0;
    if (dt > 0.05f) dt = 0.05f;
    eadk_keyboard_state_t k = eadk_keyboard_scan(), hit = k & ~prev;
    prev = k;
    if (eadk_keyboard_key_down(k, eadk_key_home)) return 0;
    if (eadk_keyboard_key_down(hit, eadk_key_zero)) U.show_fps = !U.show_fps;
    if (!ui_frame(dt, hit)) return 0;

    uint64_t t1 = eadk_timing_millis();
    frames++;
    work += (int)(t1 - t0);
    if (t1 - sec >= 1000) {
      U.fps = frames;
      U.work_ms = work / frames;
      U.px_k = r_last_pixels() / 1000;
      frames = work = 0;
      sec = t1;
    }
    int spent = (int)(t1 - t0);
    eadk_timing_msleep(spent < FRAME_MS - 1 ? FRAME_MS - spent : 1);
  }
}
