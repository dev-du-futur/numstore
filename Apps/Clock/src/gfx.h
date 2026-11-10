/* Drawing: filled rectangles, and text composed row by row inside a box, so nothing flickers when it changes. */
#ifndef GFX_H
#define GFX_H
#include <eadk.h>
#include "assets.h"

#define RGB(h) ((uint16_t)(((((h) >> 19) & 0x1F) << 11) | ((((h) >> 10) & 0x3F) << 5) | (((h) >> 3) & 0x1F)))

extern int g_clipped;   /* boxes that went off the screen (the host test wants 0) */
void g_fill(int x, int y, int w, int h, uint16_t c);
int g_text_w(const font_t *f, const char *s);
/* fills the box x,y,w,h with bg and draws s with its baseline at y + base, starting at x + tx */
void g_box(const font_t *f, int x, int y, int w, int h, int tx, int base, const char *s, uint16_t fg, uint16_t bg);
/* the same, centred across the box, baseline so the digits' height sits in the middle */
void g_box_c(const font_t *f, int x, int y, int w, int h, const char *s, uint16_t fg, uint16_t bg);

#endif
