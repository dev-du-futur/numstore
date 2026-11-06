#include "eadk.h"
uint16_t host_screen[240][320];
long host_pushed;
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  host_pushed += (long)r.width * r.height;
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.y + y < 240 && r.x + x < 320) host_screen[r.y + y][r.x + x] = px[y * r.width + x];
}
