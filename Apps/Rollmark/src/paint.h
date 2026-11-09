/* Rounded "cards" like RNGidle's: smooth corners, a border, a left-to-right gradient, a light sheen from the top
 * left and an optional glow around it. Text drawn on a card blends with the card under each pixel. */
#ifndef PAINT_H
#define PAINT_H
#include "gfx.h"

typedef struct {
  int x, y, w, h;      /* the card itself (the glow spreads outside it) */
  int r, bw;           /* corner radius, border width */
  uint32_t fill[3];    /* gradient stops at the left, middle and right, as 0xRRGGBB */
  uint32_t border[2];  /* border colour, from left to right */
  uint32_t glow;       /* 0: none */
  int glow_px;         /* how far the glow reaches */
  int sheen;           /* strength of the top-left light, 0..64 */
  uint32_t out;        /* the colour around the card */
} card_t;

void card_draw(const card_t *c);
void card_area(const card_t *c, int x, int y, int w, int h);   /* repaints only that part of the card */
/* text over the card (or its glow), centred on cx; returns the end x */
int card_text_c(const card_t *c, const font_t *f, int cx, int base, const char *s, uint32_t fg);
int card_text(const card_t *c, const font_t *f, int x, int base, const char *s, uint32_t fg);
/* the colour of the card at a pixel, as 0xRRGGBB */
uint32_t card_px(const card_t *c, int x, int y);
uint32_t rgb_mix(uint32_t a, uint32_t b, int k);   /* k/256 of a */
void img_draw(const img_t *im, int x, int y, const card_t *c, uint32_t bg);   /* c NULL: over bg */

#endif
