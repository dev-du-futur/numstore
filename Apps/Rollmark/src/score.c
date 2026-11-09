#include <string.h>
#include "odds.h"
#include "roll.h"

uint64_t badge_ep(int b) { return BADGE_EP[b]; }
uint64_t tier_min(int t) { return TIER_MIN[t]; }

void roll_score(roll_t *r) {
  roll_badges(r);
  roll_ep_with(r, BADGE_EP);
  const number_t *n = &r->num;
  if (!n->fn && (!strcmp(n->digits, "404") || !strcmp(n->digits, "666"))) {
    r->tier = n->digits[0] == '4' ? T_ERROR : T_DEVILISH;
    return;
  }
  r->tier = T_TRASHEST;
  for (int t = T_TRASH; t <= T_TRANSCENDENT; t++)
    if (r->ep >= TIER_MIN[t]) r->tier = (uint8_t)t;
}
