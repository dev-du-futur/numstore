/* Rollmark's rules: how a number is drawn, which badges it earns, its EP and its tier.
 * The draw follows RNGidle's model (a 0..999,999 seed that may grow digits, 1 in 20 negative, 1 in 100 with
 * decimals). The badges are in badges.def. No drawing, no keys. */
#ifndef ROLL_H
#define ROLL_H
#include <stdbool.h>
#include <stdint.h>

#define MAX_DIGITS 18   /* fits a uint64; a longer number is about as likely as 1 in 10^24 */
#define MAX_FRAC 10
#define MAX_EARNED 128

typedef struct {
  char digits[MAX_DIGITS + 1];   /* the integer part, no sign */
  char frac[MAX_FRAC + 1];       /* decimals, "" when none */
  uint8_t n, fn;
  bool neg;
  uint64_t v;                    /* the integer part's value */
} number_t;

enum {
#define B(id, name, desc, fam, test) B_##id,
#include "badges.def"
#undef B
  N_BADGES
};

enum {
  F_NONE, F_TAIL, F_FIVES, F_QUARTER, F_NINES, F_HOLES, F_SMALL, F_LONE, F_PAIRS, F_KIND, F_HOUSE, F_RUN,
  F_ADJPAIRS, F_SEVENS, F_STEP, F_MIRROR, F_ENDS, F_ENDS2, F_FRAME, F_UP, F_DOWN, F_UP2, F_DOWN2, F_SPACING,
  F_PEAK, F_TROUGH, F_ZIG, F_SKIP, F_PARITY, F_PRIME, F_POWER, F_BASE, F_SPLIT, F_CHUNKS, F_PROGRESS, F_42,
  F_666, F_1337, F_7734, F_911, F_404, F_1984, F_67, F_86, F_101, F_365, F_PI, F_E, F_TAU, F_PHI, F_GROW,
  F_FRAC, N_FAMILIES
};

typedef struct { const char *name, *desc; uint8_t family; } badge_info_t;
extern const badge_info_t BADGES[N_BADGES];
extern const char *const FAMILY_NAMES[N_FAMILIES];

enum {
  T_TRASHEST, T_TRASH, T_COMMON, T_UNCOMMON, T_RARE, T_EPIC, T_ANOMALY, T_MYTHIC, T_UNREAL, T_ABERRATION,
  T_OTHERWORLDLY, T_CATACLYSMIC, T_DIVINE, T_TRANSCENDENT, T_DEVILISH, T_ERROR, N_TIERS
};
extern const char *const TIER_NAMES[N_TIERS];

typedef struct {
  number_t num;
  uint8_t badges[MAX_EARNED];   /* earned ids, in badges.def order */
  uint8_t nb;
  uint32_t paid[MAX_EARNED / 32];   /* bit i: badges[i] pays (the best of its family) */
  uint64_t ep;
  uint8_t tier;
} roll_t;

#define BIT(a, i) (((a)[(i) >> 5] >> ((i) & 31)) & 1)
#define SETBIT(a, i) ((a)[(i) >> 5] |= 1u << ((i) & 31))

typedef uint32_t (*rand_fn)(void);
void roll_draw(number_t *n, rand_fn rnd);
void roll_from_seed(number_t *n, uint32_t seed);      /* a plain seed: no growth, sign or fraction */
void roll_from_text(number_t *n, const char *digits); /* an integer given as digits, for tools and tests */
void roll_badges(roll_t *r);                          /* fills badges, nb */
void roll_ep_with(roll_t *r, const uint64_t *price);  /* paid and ep from these badge prices */
void roll_score(roll_t *r);                           /* badges, paid, ep and tier with the built-in prices */
bool is_prime64(uint64_t v);
uint64_t badge_ep(int b);                             /* the built-in prices (score.c) */
uint64_t tier_min(int t);
void roll_fix(number_t *n);                           /* after digits or frac were edited: lengths, value, no stray zeros */
void roll_text(const number_t *n, char *out);         /* "-1234567.89" */
void fmt_int(uint64_t v, char *out);                  /* "12,345,678" */

#endif
