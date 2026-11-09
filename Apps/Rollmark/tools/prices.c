/* Prices Rollmark's shop and finds the rolls behind its rarity potions. Writes src/prices.h.   make prices
 *
 * RNGidle's pricing rules (packages/rules/src/items/catalogue.ts), applied to Rollmark's own rolls:
 * - a luck potion costs about 1.6 x the EP it is expected to add (best of the re-rolls, over its rolls);
 * - a rarity potion costs about 2 x the mean EP of a roll at least that rare;
 * - number items: priced in rolls (Roulette Splitter = 10 mean rolls), RNGidle's order, ratios squeezed;
 * The shop shows a ware with weight price^-0.35 (6 of them); how often it shows up gives its card's tier.
 * Rarity potions: seeds of the calculator's own generator that draw a roll that rare (the calculator can't search
 * millions of rolls on the spot, RNGidle's server keeps a "sack" of them for the same reason). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "items.h"

#define LUCK_SAMPLES 120000
#define POOL_SIZE 64
#define SHOPS 200000

static uint32_t S = 0x2545F491u;
static uint32_t rnd(void) { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }   /* the same as ui.c's */

static double draw_ep(void) {
  roll_t r;
  roll_draw(&r.num, rnd);
  roll_score(&r);
  return (double)r.ep;
}

static double sig2(double v) {   /* two significant digits */
  if (v <= 0) return 0;
  double p = pow(10, floor(log10(v)) - 1);
  return round(v / p) * p;
}

/* RNGidle's prices, for the scale */
static const double THEIRS[N_ITEMS] = {
    1e4, 5e4, 2.5e5, 1e6, 5e6, 2.5e7, 1e8, 2.5e8, 1e9, 5e9,   /* luck I..X */
    0, 0, 2e8, 5e8,                                            /* rarity (Mythic, Unreal: none in RNGidle) */
    1e7, 1.5e7, 2e7, 5e8, 7.5e8, 4e9, 5e9, 2e10};              /* number items */

int main(void) {
  static double price[N_ITEMS];
  /* the mean roll */
  double mean = 0;
  for (int i = 0; i < LUCK_SAMPLES; i++) mean += draw_ep();
  mean /= LUCK_SAMPLES;
  fprintf(stderr, "mean roll %.0f EP\n", mean);

  /* luck: draws = 1 + (whole part - 1) + one more with the fraction as a chance; the best kept */
  double scale_log = 0;
  for (int id = IT_LUCK1; id <= IT_LUCK10; id++) {
    double m = ITEMS[id].luck10 / 10.0, best_sum = 0;
    int whole = (int)m;
    double frac = m - whole;
    for (int i = 0; i < LUCK_SAMPLES; i++) {
      double best = draw_ep();
      int extra = whole - 1 + ((rnd() % 1000) < frac * 1000);
      for (int k = 0; k < extra; k++) {
        double e = draw_ep();
        if (e > best) best = e;
      }
      best_sum += best;
    }
    double gain = best_sum / LUCK_SAMPLES - mean;
    price[id] = sig2(1.6 * gain * ITEMS[id].cap);
    scale_log += log(price[id] / THEIRS[id]);
    fprintf(stderr, "%s: +%.0f EP a roll, %.0f EP\n", ITEMS[id].name, gain, price[id]);
  }
  double scale = exp(scale_log / 10);
  fprintf(stderr, "scale to RNGidle: %.3g\n", scale);
  /* number items: RNGidle's price on our luck scale was far too dear (Edge of Infinity: 260 hours of rolling).
   * They are priced in rolls instead: Roulette Splitter = 10 mean rolls, the others keep RNGidle's order with
   * their price ratios squeezed (ratio^0.6): Edge of Infinity is about 1,000 rolls. */
  for (int id = IT_ROULETTE; id <= IT_EDGE; id++)
    price[id] = sig2(10 * mean * pow(THEIRS[id] / THEIRS[IT_ROULETTE], 0.6));

  /* rarity: seeds of rolls at least that rare (and not a special number), and their mean EP */
  static uint32_t pool[4][POOL_SIZE];
  int got[4] = {0};
  double sum[4] = {0};
  long cnt[4] = {0}, draws = 0;
  S = 0x9E3779B9u;
  while (got[3] < POOL_SIZE) {
    uint32_t seed = S;
    roll_t r;
    roll_draw(&r.num, rnd);
    roll_score(&r);
    draws++;
    for (int k = 0; k < 4; k++) {
      int t = ITEMS[IT_MYTHIC + k].tier;
      if (r.tier < t || r.tier > T_TRANSCENDENT) continue;
      sum[k] += (double)r.ep;
      cnt[k]++;
      if (got[k] < POOL_SIZE) pool[k][got[k]++] = seed;
    }
    if (draws % 2000000 == 0) fprintf(stderr, "%ld draws, otherworldly %d/%d\n", draws, got[3], POOL_SIZE);
  }
  for (int k = 0; k < 4; k++) {
    price[IT_MYTHIC + k] = sig2(2 * sum[k] / cnt[k]);
    fprintf(stderr, "%s: mean %.0f EP over %ld rolls\n", ITEMS[IT_MYTHIC + k].name, sum[k] / cnt[k], cnt[k]);
  }

  /* how often the shop shows each ware */
  static long seen[N_ITEMS];
  double w[N_ITEMS];
  for (int id = 0; id < N_ITEMS; id++) w[id] = pow(price[id], -0.35);
  for (int s = 0; s < SHOPS; s++) {
    bool taken[N_ITEMS] = {0};
    for (int k = 0; k < 6; k++) {
      double total = 0;
      for (int id = 0; id < N_ITEMS; id++) if (!taken[id]) total += w[id];
      double x = (rnd() / 4294967296.0) * total;
      int id = 0;
      for (; id < N_ITEMS - 1; id++) {
        if (taken[id]) continue;
        if ((x -= w[id]) < 0) break;
      }
      while (taken[id]) id--;
      taken[id] = true;
      seen[id]++;
    }
  }
  int tier[N_ITEMS];
  for (int id = 0; id < N_ITEMS; id++) {
    double f = (double)seen[id] / SHOPS;
    tier[id] = f >= 0.5 ? T_COMMON : f >= 0.25 ? T_UNCOMMON : f >= 0.12 ? T_RARE : f >= 0.06 ? T_EPIC
             : f >= 0.03 ? T_MYTHIC : f >= 0.015 ? T_UNREAL : T_ABERRATION;
  }

  FILE *f = fopen("src/prices.h", "w");
  if (!f) return 1;
  fprintf(f, "/* Generated by tools/prices.c (make prices), do not edit. Mean roll: %.0f EP. */\n", mean);
  fprintf(f, "#ifndef PRICES_H\n#define PRICES_H\n#include <stdint.h>\n#define POOL_SIZE %d\n", POOL_SIZE);
  fprintf(f, "static const uint64_t ITEM_PRICE[%d] = {\n", N_ITEMS);
  for (int id = 0; id < N_ITEMS; id++)
    fprintf(f, "  %lluull, /* %s, shown in %.1f%% of shops */\n", (unsigned long long)price[id], ITEMS[id].name, 100.0 * seen[id] / SHOPS);
  fprintf(f, "};\nstatic const uint8_t ITEM_TIER[%d] = {", N_ITEMS);
  for (int id = 0; id < N_ITEMS; id++) fprintf(f, "%d%s", tier[id], id + 1 < N_ITEMS ? ", " : "");
  fprintf(f, "};\n/* seeds of the calculator's generator: Mythic, Unreal, Aberration, Otherworldly or better */\n");
  fprintf(f, "static const uint32_t POOL[4][POOL_SIZE] = {\n");
  for (int k = 0; k < 4; k++) {
    fprintf(f, "  {");
    for (int i = 0; i < POOL_SIZE; i++) fprintf(f, "%uu%s", pool[k][i], i + 1 < POOL_SIZE ? ", " : "");
    fprintf(f, "},\n");
  }
  fprintf(f, "};\n#endif\n");
  fclose(f);
  for (int id = 0; id < N_ITEMS; id++)
    printf("%-20s %16.0f EP   shown %5.1f%%   %s\n", ITEMS[id].name, price[id], 100.0 * seen[id] / SHOPS, TIER_NAMES[tier[id]]);
  return 0;
}
