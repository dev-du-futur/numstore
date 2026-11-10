/* Clock for NumWorks: shows the time you set, counted on with the calculator's millisecond timer.
 *   Clock screen: OK set the time   Up/Down one minute   0 round to the nearest minute   Back or Home: quit
 *   Set screen: digits or Up/Down change a field   Left/Right move   OK start at 00 seconds   Back cancel
 * The time set is kept in the calculator's file list (clock.sav, see save.h), so it carries on when you come back.
 */
#include <eadk.h>
#include "ui.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Clock";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

int main(void) {
  ui_init(eadk_timing_millis());
  eadk_keyboard_state_t prev = eadk_keyboard_scan();   /* keys held at launch don't count */
  for (;;) {
    eadk_keyboard_state_t k = eadk_keyboard_scan(), hit = k & ~prev;
    prev = k;
    if (eadk_keyboard_key_down(k, eadk_key_home)) return 0;
    if (!ui_frame(eadk_timing_millis(), hit, k)) return 0;
    eadk_timing_msleep(20);
  }
}
