#include "items.h"
#include <string.h>
#ifndef PRICES_TOOL   /* tools/prices.c builds this file before prices.h exists */
#include "prices.h"
#endif

#define LUCK(n, m10, cap, lo, hi, d) {"Luck Potion " n, d, K_LUCK, m10, cap, 0, lo, hi}
#define RARITY(n, t, lo, hi, d) {n " Potion", d, K_RARITY, 0, 0, t, lo, hi}
#define NUMBER(n, lo, hi, d) {n, d, K_NUMBER, 0, 0, 0, lo, hi}

const item_info_t ITEMS[N_ITEMS] = {
    LUCK("I", 11, 2, 250, 250, "x1.1 luck on your next 2 rolls"),
    LUCK("II", 12, 5, 250, 250, "x1.2 luck on your next 5 rolls"),
    LUCK("III", 13, 15, 100, 100, "x1.3 luck on your next 15 rolls"),
    LUCK("IV", 15, 40, 100, 100, "x1.5 luck on your next 40 rolls"),
    LUCK("V", 17, 140, 15, 30, "x1.7 luck on your next 140 rolls"),
    LUCK("VI", 19, 500, 8, 15, "x1.9 luck on your next 500 rolls"),
    LUCK("VII", 21, 1500, 4, 8, "x2.1 luck on your next 1,500 rolls"),
    LUCK("VIII", 25, 3000, 3, 5, "x2.5 luck on your next 3,000 rolls"),
    LUCK("IX", 30, 10000, 1, 3, "x3 luck on your next 10,000 rolls"),
    LUCK("X", 50, 25000, 1, 2, "x5 luck on your next 25,000 rolls"),
    RARITY("Mythic", T_MYTHIC, 5, 10, "Your next roll is Mythic or better"),
    RARITY("Unreal", T_UNREAL, 4, 8, "Your next roll is Unreal or better"),
    RARITY("Aberration", T_ABERRATION, 3, 6, "Your next roll is Aberration or better"),
    RARITY("Otherworldly", T_OTHERWORLDLY, 2, 4, "Your next roll is Otherworldly or better"),
    NUMBER("Roulette Splitter", 5, 12, "A wheel: one digit moves by 1 or 2, or not"),
    NUMBER("Slot Splitter", 5, 12, "Every digit moves by +1, 0 or -1"),
    NUMBER("Digit Splitter", 5, 10, "One random digit becomes another"),
    NUMBER("Digit Swap", 1, 3, "Two different digits trade places"),
    NUMBER("Digit Mirror", 1, 3, "The digits read backwards"),
    NUMBER("Gravity Potion", 1, 2, "Every digit falls by one (0 drops to 9)"),
    NUMBER("Grinder", 1, 1, "One digit goes (a lone digit: 404)"),
    NUMBER("Edge of Infinity", 1, 1, "A new digit appears somewhere"),
};

#ifndef PRICES_TOOL
uint64_t item_price(int id) { return ITEM_PRICE[id]; }
int item_tier(int id) { return ITEM_TIER[id]; }

uint32_t item_pool_seed(int id, uint32_t pick) { return POOL[id - IT_MYTHIC][pick % POOL_SIZE]; }
#endif

/* ---------------------------------------------------------------- number items */

/* every digit of the number in one row (whole part then decimals), as RNGidle's items see them */
typedef struct {
  char d[MAX_DIGITS + MAX_FRAC + 2];
  int len, split;   /* split: digits of the whole part */
} digits_t;

static void get(const number_t *n, digits_t *g) {
  memcpy(g->d, n->digits, n->n);
  memcpy(g->d + n->n, n->frac, n->fn);
  g->len = n->n + n->fn;
  g->split = n->n;
  g->d[g->len] = 0;
}

static void put(number_t *n, const digits_t *g) {
  memcpy(n->digits, g->d, (size_t)g->split);
  n->digits[g->split] = 0;
  memcpy(n->frac, g->d + g->split, (size_t)(g->len - g->split));
  n->frac[g->len - g->split] = 0;
  roll_fix(n);
}

/* a leading digit never becomes 0 */
static bool leading(const digits_t *g, int i) { return i == 0 && g->split > 1; }

static int dig(const digits_t *g, int i) { return g->d[i] - '0'; }

static bool roulette(digits_t *g, rand_fn rnd) {
  int variants = 0;
  for (int i = 0; i < g->len; i++)
    for (int k = 0; k < 4; k++) {
      static const int BY[4] = {1, -1, 2, -2};
      if (!(leading(g, i) && (dig(g, i) + BY[k] + 10) % 10 == 0)) variants++;
    }
  if (!variants) return false;
  int wheel = variants < 15 ? variants : 15;   /* 16 areas: the number itself and 15 variants */
  if ((int)(rnd() % (uint32_t)(wheel + 1)) == wheel) return true;   /* landed on the number itself */
  int pick = (int)(rnd() % (uint32_t)variants);
  for (int i = 0; i < g->len; i++)
    for (int k = 0; k < 4; k++) {
      static const int BY[4] = {1, -1, 2, -2};
      int d = (dig(g, i) + BY[k] + 10) % 10;
      if (leading(g, i) && d == 0) continue;
      if (pick-- == 0) { g->d[i] = (char)('0' + d); return true; }
    }
  return true;
}

static void slot(digits_t *g, rand_fn rnd) {
  for (int i = 0; i < g->len; i++) {
    int opts[3], n = 0;
    for (int o = -1; o <= 1; o++)
      if (!(leading(g, i) && (dig(g, i) + o + 10) % 10 == 0)) opts[n++] = o;
    g->d[i] = (char)('0' + (dig(g, i) + opts[rnd() % (uint32_t)n] + 10) % 10);
  }
}

static void digit(digits_t *g, rand_fn rnd) {
  int i = (int)(rnd() % (uint32_t)g->len), old = dig(g, i), choices[10], n = 0;
  for (int d = 0; d < 10; d++)
    if (d != old && !(leading(g, i) && d == 0)) choices[n++] = d;
  g->d[i] = (char)('0' + choices[rnd() % (uint32_t)n]);
}

static bool swap(digits_t *g, rand_fn rnd, const char **why) {
  if (g->len < 2) { *why = "Digit Swap needs two digits"; return false; }
  bool same = true;
  for (int i = 1; i < g->len; i++) same &= g->d[i] == g->d[0];
  if (same) { *why = "Digit Swap needs two different digits"; return false; }
  int a, b, others[MAX_DIGITS + MAX_FRAC], n;
  do {   /* a digit with at least one different one to trade with */
    a = (int)(rnd() % (uint32_t)g->len);
    n = 0;
    for (int i = 0; i < g->len; i++)
      if (g->d[i] != g->d[a]) others[n++] = i;
  } while (!n);
  b = others[rnd() % (uint32_t)n];
  char t = g->d[a];
  g->d[a] = g->d[b];
  g->d[b] = t;
  return true;
}

static bool mirror(digits_t *g, const char **why) {
  if (g->len < 2) { *why = "Digit Mirror needs two digits"; return false; }
  char t[sizeof g->d];
  for (int i = 0; i < g->len; i++) t[i] = g->d[g->len - 1 - i];
  if (!memcmp(t, g->d, (size_t)g->len)) { *why = "It reads the same backwards"; return false; }
  memcpy(g->d, t, (size_t)g->len);
  return true;
}

static void gravity(digits_t *g) {
  for (int i = 0; i < g->len; i++) g->d[i] = (char)('0' + (dig(g, i) + 9) % 10);
}

static void grind(digits_t *g, rand_fn rnd) {
  if (g->len == 1) {   /* one digit alone grinds to 404 */
    strcpy(g->d, "404");
    g->len = g->split = 3;
    return;
  }
  int i = (int)(rnd() % (uint32_t)g->len);
  memmove(g->d + i, g->d + i + 1, (size_t)(g->len - i));
  g->len--;
  if (i < g->split) g->split--;
  if (!g->split) {   /* .25 is 0.25 */
    memmove(g->d + 1, g->d, (size_t)g->len + 1);
    g->d[0] = '0';
    g->len++;
    g->split = 1;
  }
}

static bool edge(digits_t *g, rand_fn rnd, const char **why) {
  for (int tries = 0; tries < 40; tries++) {
    int at = (int)(rnd() % (uint32_t)(g->len + 1));
    bool whole = at <= g->split;
    if (whole && g->split >= MAX_DIGITS) continue;
    if (!whole && g->len - g->split >= MAX_FRAC) continue;
    int d = at == 0 ? 1 + (int)(rnd() % 9) : (int)(rnd() % 10);
    if (at == g->len && !whole && d == 0) continue;   /* a trailing decimal 0 changes nothing */
    if (g->split == 1 && g->d[0] == '0' && at <= 1 && whole) continue;   /* 0 -> 05 would read 5: use another spot */
    memmove(g->d + at + 1, g->d + at, (size_t)(g->len - at + 1));
    g->d[at] = (char)('0' + d);
    g->len++;
    if (whole) g->split++;
    return true;
  }
  *why = "No room for another digit";
  return false;
}

bool item_use(int id, roll_t *r, rand_fn rnd, const char **why) {
  static const char *none = "";
  *why = none;
  if (ITEMS[id].kind != K_NUMBER) { *why = "Not a number item"; return false; }
  digits_t g;
  get(&r->num, &g);
  bool ok = true;
  switch (id) {
    case IT_ROULETTE: ok = roulette(&g, rnd); break;
    case IT_SLOT: slot(&g, rnd); break;
    case IT_DIGIT: digit(&g, rnd); break;
    case IT_SWAP: ok = swap(&g, rnd, why); break;
    case IT_MIRROR: ok = mirror(&g, why); break;
    case IT_GRAVITY: gravity(&g); break;
    case IT_GRINDER: grind(&g, rnd); break;
    case IT_EDGE: ok = edge(&g, rnd, why); break;
    default: ok = false;
  }
  if (!ok) return false;
  put(&r->num, &g);
  roll_score(r);
  return true;
}
