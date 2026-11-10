/* PC stand-in for the calculator's eadk.h: just what Clock uses. Key numbers match the device's. */
#ifndef EADK_H
#define EADK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint16_t eadk_color_t;
typedef struct { uint16_t x, y, width, height; } eadk_rect_t;
typedef uint64_t eadk_keyboard_state_t;
typedef enum {
  eadk_key_left = 0, eadk_key_up = 1, eadk_key_down = 2, eadk_key_right = 3, eadk_key_ok = 4, eadk_key_back = 5,
  eadk_key_home = 6, eadk_key_on_off = 8, eadk_key_shift = 12, eadk_key_alpha = 13, eadk_key_xnt = 14,
  eadk_key_var = 15, eadk_key_toolbox = 16, eadk_key_backspace = 17, eadk_key_exp = 18, eadk_key_ln = 19,
  eadk_key_log = 20, eadk_key_imaginary = 21, eadk_key_comma = 22, eadk_key_power = 23, eadk_key_sine = 24,
  eadk_key_cosine = 25, eadk_key_tangent = 26, eadk_key_pi = 27, eadk_key_sqrt = 28, eadk_key_square = 29,
  eadk_key_seven = 30, eadk_key_eight = 31, eadk_key_nine = 32, eadk_key_left_parenthesis = 33,
  eadk_key_right_parenthesis = 34, eadk_key_four = 36, eadk_key_five = 37, eadk_key_six = 38,
  eadk_key_multiplication = 39, eadk_key_division = 40, eadk_key_one = 42, eadk_key_two = 43, eadk_key_three = 44,
  eadk_key_plus = 45, eadk_key_minus = 46, eadk_key_zero = 48, eadk_key_dot = 49, eadk_key_ee = 50,
  eadk_key_ans = 51, eadk_key_exe = 52
} eadk_key_t;
void eadk_display_push_rect(eadk_rect_t rect, const eadk_color_t *pixels);
void eadk_display_push_rect_uniform(eadk_rect_t rect, eadk_color_t color);
extern uint16_t host_screen[240][320];
extern long host_pushes, host_out_of_screen;
#endif
