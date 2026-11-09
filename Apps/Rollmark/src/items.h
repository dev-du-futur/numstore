/* Rollmark's shop items, after RNGidle's catalogue (packages/rules/src/items): luck potions, rarity potions and
 * number items. Prices come from tools/prices.c (make prices) with RNGidle's own pricing rules applied to
 * Rollmark's rolls. */
#ifndef ITEMS_H
#define ITEMS_H
#include <stdbool.h>
#include <stdint.h>
#include "roll.h"

/* ids are kept in saves: only add at the end */
enum {
  IT_LUCK1, IT_LUCK2, IT_LUCK3, IT_LUCK4, IT_LUCK5, IT_LUCK6, IT_LUCK7, IT_LUCK8, IT_LUCK9, IT_LUCK10,
  IT_MYTHIC, IT_UNREAL, IT_ABERRATION, IT_OTHERWORLDLY,
  IT_ROULETTE, IT_SLOT, IT_DIGIT, IT_SWAP, IT_MIRROR, IT_GRAVITY, IT_GRINDER, IT_EDGE,
  N_ITEMS
};
enum { K_LUCK, K_RARITY, K_NUMBER };

typedef struct {
  const char *name, *desc;   /* desc: one short line */
  uint8_t kind;
  uint16_t luck10;           /* luck: the multiplier x10 (x1.7 -> 17) */
  uint32_t cap;              /* luck: rolls it covers */
  uint8_t tier;              /* rarity: the tier it reaches */
  uint16_t stock_min, stock_max;   /* how many one shop holds */
} item_info_t;

extern const item_info_t ITEMS[N_ITEMS];
uint64_t item_price(int id);
int item_tier(int id);       /* the colour of its card: from how rarely the shop shows it */

/* number items: change r's number (then r is scored again). false: the item can't work on this number
 * (why in *why), nothing changed and the item is kept. */
bool item_use(int id, roll_t *r, rand_fn rnd, const char **why);

/* rarity potions: a roll at least that rare, from seeds found ahead by tools/prices.c */
uint32_t item_pool_seed(int id, uint32_t pick);

#endif
