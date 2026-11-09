/* Rollmark for NumWorks: roll numbers, earn badges for their patterns, climb the tiers.
 *   OK or EXE: roll (hold to keep rolling)   Left / Right or 1-4: x1 x10 x100 x1000   Up / Down: scroll
 *   Back: menu (quits from the menu)   Home: quit
 * Progress is saved in the calculator's file list (rollmark.sav, see save.h).
 */
#include <eadk.h>
#include "ui.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Rollmark";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define FRAME_MS 16

int main(void) {
  ui_init(eadk_random(), eadk_timing_millis());
  eadk_keyboard_state_t prev = eadk_keyboard_scan();   /* keys held at launch don't count */
  uint64_t last = eadk_timing_millis();
  for (;;) {
    uint64_t t0 = eadk_timing_millis();
    int ms = (int)(t0 - last);
    last = t0;
    if (ms > 50) ms = 50;
    eadk_keyboard_state_t k = eadk_keyboard_scan(), hit = k & ~prev;
    prev = k;
    if (eadk_keyboard_key_down(k, eadk_key_home)) { ui_save(); return 0; }
    if (!ui_frame(ms, hit, k)) return 0;
    int spent = (int)(eadk_timing_millis() - t0);
    eadk_timing_msleep(spent < FRAME_MS - 1 ? FRAME_MS - spent : 1);
  }
}
