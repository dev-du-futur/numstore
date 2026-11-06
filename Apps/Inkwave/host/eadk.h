/* PC stand-in for the calculator's eadk.h: just what Inkwave's sim and renderer use. */
#ifndef EADK_H
#define EADK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint16_t eadk_color_t;
typedef struct { uint16_t x, y; } eadk_point_t;
typedef struct { uint16_t x, y, width, height; } eadk_rect_t;
void eadk_display_push_rect(eadk_rect_t rect, const eadk_color_t *pixels);
extern uint16_t host_screen[240][320];
uint64_t eadk_timing_millis(void);
#endif
