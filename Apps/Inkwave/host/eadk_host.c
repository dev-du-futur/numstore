#include "eadk.h"
uint16_t host_screen[240][320];
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.y + y < 240 && r.x + x < 320) host_screen[r.y + y][r.x + x] = px[y * r.width + x];
}

#include <time.h>
uint64_t eadk_timing_millis(void) { return (uint64_t)clock() * 1000 / CLOCKS_PER_SEC; }
