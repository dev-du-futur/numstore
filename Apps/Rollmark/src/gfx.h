/* Drawing straight to the screen: filled rectangles and anti-aliased text over a known background. */
#ifndef GFX_H
#define GFX_H
#include <eadk.h>
#include "assets.h"

#define RGB(h) ((uint16_t)(((((h) >> 19) & 0x1F) << 11) | ((((h) >> 10) & 0x3F) << 5) | (((h) >> 3) & 0x1F)))

extern int g_clipped;   /* glyphs that fell off the screen and were skipped (the host test wants 0) */
void g_fill(int x, int y, int w, int h, uint16_t c);
void g_frame(int x, int y, int w, int h, int t, uint16_t c);
int g_text_w(const font_t *f, const char *s);
int g_text(const font_t *f, int x, int base, const char *s, uint16_t fg, uint16_t bg);   /* returns the end x */
int g_text_c(const font_t *f, int cx, int base, const char *s, uint16_t fg, uint16_t bg);

#endif
