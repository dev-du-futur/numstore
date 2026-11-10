#include "eadk.h"
uint16_t host_screen[240][320];
long host_pushes, host_out_of_screen;
static int fits(eadk_rect_t r) {
  if (r.x + r.width > 320 || r.y + r.height > 240) { host_out_of_screen++; return 0; }
  return 1;
}
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  host_pushes++;
  fits(r);
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.y + y < 240 && r.x + x < 320) host_screen[r.y + y][r.x + x] = px[y * r.width + x];
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  host_pushes++;
  fits(r);
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.y + y < 240 && r.x + x < 320) host_screen[r.y + y][r.x + x] = c;
}
