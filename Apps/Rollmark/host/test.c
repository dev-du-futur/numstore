/* PC check for Rollmark: badge rules on known numbers, primality, family pay, unique tiers, the draw's odds over
 * two million rolls, scripted play through every screen (screenshots, PPM) and random-key runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eadk.h"
#include "gfx.h"
#include "roll.h"
#include "ui.h"
#include "save.h"
#include "items.h"

static const char *dir;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void shot(const char *name) {
  static int last_clip;
  if (g_clipped != last_clip) printf("clipped glyphs before %s: %d\n", name, g_clipped - last_clip);
  last_clip = g_clipped;
  char p[256];
  snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
  FILE *f = fopen(p, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = host_screen[y][x];
      unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

#ifndef RANDOM_RUNS
#define RANDOM_RUNS 200
#endif
static bool alive = true;
static void frame(uint64_t hit, uint64_t held) { if (alive && !ui_frame(16, hit, held)) alive = false; }
static void press(int k) { frame(1ull << k, 1ull << k); frame(0, 0); }
static void settle(void) { for (int i = 0; i < 300 && ui_busy(); i++) frame(0, 0); }

static roll_t of(uint32_t seed) {
  roll_t r;
  char t[16];
  snprintf(t, sizeof t, "%u", seed);
  roll_from_text(&r.num, t);
  roll_score(&r);
  return r;
}
static bool has(const roll_t *r, int b) {
  for (int i = 0; i < r->nb; i++) if (r->badges[i] == b) return true;
  return false;
}
static bool pays(const roll_t *r, int b) {
  for (int i = 0; i < r->nb; i++) if (r->badges[i] == b) return BIT(r->paid, i);
  return false;
}
static void expect(uint32_t seed, const int *want, int n) {
  roll_t r = of(seed);
  for (int i = 0; i < n; i++) CHECK(has(&r, want[i]), "%u should earn %s", seed, BADGES[want[i]].name);
}
#define EXPECT(seed, ...) do { const int w_[] = {__VA_ARGS__}; expect(seed, w_, sizeof w_ / sizeof w_[0]); } while (0)

static uint32_t s32 = 99;
static uint32_t rnd(void) { s32 ^= s32 << 13; s32 ^= s32 >> 17; s32 ^= s32 << 5; return s32; }

int main(int argc, char **argv) {
  dir = argc > 1 ? argv[1] : ".";

  /* badges on known numbers */
  EXPECT(0, B_LITTLE, B_TINY, B_ATOM, B_NOTHING, B_EVEN, B_LONE0);
  EXPECT(121, B_MIRROR, B_SQUARE, B_LITTLE, B_LOW, B_ODD, B_FULL_CIRCLE, B_BOOKENDS, B_SMOOTH, B_MOUNTAIN, B_ZIGZAG, B_SKIP2);
  EXPECT(777777, B_MONOTONE, B_RUN6, B_GRAND_MIRROR, B_SEVEN6, B_ECHO, B_ODDS, B_HIGH_DIGITS, B_HANDFUL);
  EXPECT(123456, B_RISE, B_LONG_STEP, B_ALL_DIFF, B_LOW, B_STAIRCASE, B_ASCENT, B_STRAIGHT);
  EXPECT(11910, B_SPLIT3_MIX);      /* 11 | 9 | 10 */
  EXPECT(654321, B_FALL, B_LONG_STEP, B_ALL_DIFF, B_COUNTDOWN, B_DESCENT);
  EXPECT(101010, B_BITS, B_ZIGZAG, B_PING_PONG, B_TAIL1, B_LOW, B_SKIP3);
  EXPECT(271828, B_E4, B_E_X, B_EVEN);
  EXPECT(161803, B_PHI4, B_PHI_X, B_LOW);
  EXPECT(314159, B_PI3, B_PI5, B_PI_X, B_PRIME);
  EXPECT(13370, B_LEET, B_UNLUCKY, B_TAIL1);
  EXPECT(999983, B_PRIME, B_TOP_SHELF, B_HIGH);
  EXPECT(832040, B_SPIRAL, B_TAIL1);
  EXPECT(262144, B_BASE2, B_SQUARE, B_CUBE, B_POW6);
  EXPECT(500500, B_TRIANGLE, B_TAIL2, B_ECHO);
  EXPECT(1042, B_ANSWER, B_LOW);
  EXPECT(12315, B_EQUATION);        /* 12 + 3 = 15 */
  EXPECT(91011, B_SPLIT3);          /* 9 | 10 | 11 */
  EXPECT(4142, B_NEXT_DOOR);
  EXPECT(3711, B_ARITH_SPLIT);      /* 3 | 7 | 11 */
  EXPECT(2618, B_GEOM_SPLIT);       /* 2 | 6 | 18 */
  EXPECT(77744, B_PACKED_HOUSE, B_FULL_HOUSE);
  EXPECT(113355, B_AABBCC, B_THREE_PAIR);
  EXPECT(69869, B_UPSIDE_DOWN);
  EXPECT(40320, B_FACTORIAL);
  EXPECT(46656, B_SELF_POWER);
  EXPECT(156, B_PRONIC);            /* 12 x 13 */
  EXPECT(1000000, B_MILLION, B_GROW7, B_TAIL5);
  roll_t r = of(5000);
  CHECK(has(&r, B_TAIL1) && has(&r, B_TAIL3) && pays(&r, B_TAIL3) && !pays(&r, B_TAIL1) && !pays(&r, B_TAIL2), "a family pays only its best");
  CHECK(of(404).tier == T_ERROR && of(666).tier == T_DEVILISH, "unique tiers");
  CHECK(of(0).tier >= T_UNREAL, "zero is a great roll (tier %s)", TIER_NAMES[of(0).tier]);

  /* primality, including numbers past 32 bits */
  CHECK(is_prime64(2) && is_prime64(999983) && is_prime64(1000003) && is_prime64(4294967311ull), "primes");
  CHECK(is_prime64(2305843009213693951ull), "2^61 - 1 is prime");
  CHECK(!is_prime64(1) && !is_prime64(561) && !is_prime64(3215031751ull) && !is_prime64(4611686014132420609ull),
        "composites (Carmichael, strong pseudoprime, (2^31-1)^2)");
  for (uint64_t v = 0; v < 20000; v++) {
    bool p = v >= 2;
    for (uint64_t d = 2; d * d <= v && p; d++) if (v % d == 0) p = false;
    CHECK(p == is_prime64(v), "is_prime64(%llu)", (unsigned long long)v);
    if (fails > 5) break;
  }

  /* text */
  char t[48];
  fmt_int(1234567, t);
  CHECK(!strcmp(t, "1,234,567"), "fmt_int %s", t);
  fmt_int(0, t);
  CHECK(!strcmp(t, "0"), "fmt_int 0");

  /* the draw's odds */
  long N = 2000000, neg = 0, grown = 0, fr = 0, tiers[N_TIERS] = {0};
  double ep = 0;
  for (long i = 0; i < N; i++) {
    roll_draw(&r.num, rnd);
    roll_score(&r);
    neg += r.num.neg;
    grown += r.num.n >= 7;
    fr += r.num.fn > 0;
    tiers[r.tier]++;
    ep += (double)r.ep;
    CHECK(r.num.n >= 1 && r.num.n <= MAX_DIGITS && !(r.num.fn && r.num.frac[r.num.fn - 1] == '0'), "draw shape");
    if (fails > 10) return 1;
  }
  printf("negative 1 in %.1f, grown 1 in %.1f, fraction 1 in %.1f, mean %.0f EP\n", (double)N / neg, (double)N / grown, (double)N / fr, ep / N);
  CHECK(N / neg > 18 && N / neg < 22 && N / fr > 90 && N / fr < 111 && N / grown > 17 && N / grown < 21, "draw odds");
  for (int i = 0; i < N_TIERS; i++) if (tiers[i]) printf("  %-13s %7.3f%%\n", TIER_NAMES[i], 100.0 * tiers[i] / N);

  /* screens */
  save_host_reset();
  ui_init(4242, 0);
  CHECK(!ui_loaded(), "no save at first");
  shot("01_menu");
  press(eadk_key_ok);
  CHECK(ui_screen() == S_ROLL, "roll view");
  shot("02_roll_empty");
  press(eadk_key_ok);
  for (int i = 0; i < 10; i++) frame(0, 0);
  shot("03_rolling");
  settle();
  shot("04_rolled");
  CHECK(ui_stats()->rolls == 1 && ui_bank() == START_ROLLS - 1, "one roll spent");
  press(eadk_key_right);
  shot("04b_amount_x10");
  press(eadk_key_four);
  press(eadk_key_two);                                       /* keys 1-4 pick the amount straight away */
  press(eadk_key_ok);
  CHECK(ui_screen() == S_BULK && ui_stats()->rolls == 11, "x10");
  for (int i = 0; i < 12; i++) frame(0, 0);
  shot("05a_bulk10_turning");
  settle();
  CHECK(ui_screen() == S_BULK && ui_bulk_open(), "the rare roll waits folded in the grid");
  shot("05b_bulk10_folded");
  press(eadk_key_ok);                                        /* the outlined card */
  CHECK(ui_screen() == S_ROLL, "OK opens the outlined card");
  settle();
  shot("05e_peek");
  press(eadk_key_back);
  CHECK(ui_screen() == S_BULK, "back to the grid");
  press(eadk_key_exe);                                       /* unfold the rare rolls */
  CHECK(ui_screen() == S_ROLL && ui_busy(), "a rare roll unfolds with its reveal");
  settle();
  shot("05c_unfolded");
  for (int i = 0; i < 12 && ui_bulk_open(); i++) { press(eadk_key_ok); settle(); }   /* NEXT ... */
  CHECK(!ui_bulk_open() && ui_screen() == S_ROLL && ui_stats()->rolls == 11, "every rare roll shown");
  shot("05d_bulk_done");
  press(eadk_key_exe);                                       /* back to the grid */
  CHECK(ui_screen() == S_BULK, "EXE: back to the bulk's grid");
  shot("05_bulk10");
  press(eadk_key_back);                                      /* done: the roll view */
  CHECK(ui_screen() == S_ROLL && ui_stats()->rolls == 11, "done");
  press(eadk_key_right); press(eadk_key_right);
  press(eadk_key_ok);                                        /* x1000 with ~89 left: rolls what is there */
  CHECK(ui_screen() == S_BULK && ui_bank() == 0, "bulk uses what the bank has (bank %ld)", ui_bank());
  settle();
  shot("06_bulk_all");
  press(eadk_key_exe);
  settle();
  if (ui_bulk_open()) {                                      /* more than one: Back keeps the rest folded */
    press(eadk_key_back);
    CHECK(ui_screen() == S_BULK && ui_bulk_open(), "back to the grid mid-unfold");
    shot("06a_mid_unfold");
  } else press(eadk_key_back);
  press(eadk_key_back);                                      /* done: the rest kept unrevealed */
  CHECK(ui_screen() == S_ROLL && !ui_bulk_open(), "done with rare rolls left");
  press(eadk_key_exe);
  press(eadk_key_exe);                                       /* best first */
  shot("06b_bulk_sorted");
  for (int i = 0; i < 9; i++) press(eadk_key_down);         /* scrolls the grid */
  shot("06c_bulk_scrolled");
  for (int i = 0; i < 9; i++) press(eadk_key_up);
  press(eadk_key_right); press(eadk_key_ok);
  CHECK(ui_screen() == S_ROLL && ui_shown()->ep > 0, "view a batch roll");
  shot("07_batch_roll");
  press(eadk_key_back);
  press(eadk_key_back);
  CHECK(ui_screen() == S_ROLL, "grid, then done");
  while (ui_bank() > 0) { press(eadk_key_ok); settle(); }   /* rolls refilled while browsing the grid */
  long made = ui_stats()->rolls;
  press(eadk_key_ok);
  CHECK(ui_stats()->rolls == made, "no roll with an empty bank (%ld)", ui_stats()->rolls);
  shot("08_empty_bank");
  for (int i = 0; i < 2000 / 16 * 3 + 5; i++) frame(0, 0);   /* 6 s: 3 rolls back */
  CHECK(ui_bank() == 3, "refill (%ld)", ui_bank());
  press(eadk_key_back);
  press(eadk_key_down); press(eadk_key_ok);
  CHECK(ui_screen() == S_SHOP, "shop");
  shot("08b_shop");
  press(eadk_key_back);
  press(eadk_key_down); press(eadk_key_ok);
  CHECK(ui_screen() == S_BOOK, "book");
  shot("09_book");
  for (int i = 0; i < 20; i++) press(eadk_key_down);
  shot("10_book_scrolled");
  press(eadk_key_back);
  press(eadk_key_down); press(eadk_key_ok);
  CHECK(ui_screen() == S_RECORDS, "records");
  shot("11_records");
  press(eadk_key_back);
  press(eadk_key_down); press(eadk_key_ok);
  shot("12_help");
  CHECK(g_clipped == 0, "%d glyphs drawn off screen", g_clipped);

  /* saving: what the screens above did is kept, other files are left as they were */
  {
    stats_t before = *ui_stats();
    long bank = ui_bank();
    ui_save();
    uint32_t n;
    uint8_t *l = save_host_list(&n);
    CHECK(l[0] == 2 + 9 + 100 && !memcmp(l + 2, "hello.py", 9) && l[2 + 9] == 'A' && l[110] == 'A', "first file kept");
    CHECK(!memcmp(l + 113, "maths.py", 9) && l[113 + 9] == 'B' && l[221] == 'B', "second file kept");
    int at = 222, sz = l[at] | l[at + 1] << 8;
    CHECK(!memcmp(l + at + 2, "rollmark.sav", 13) && sz == 2 + 13 + 3072 && l[at + sz] == 0 && l[at + sz + 1] == 0,
          "save added at the end, list still ends");
    ui_init(1, 600000);   /* ten minutes later on the same clock */
    const stats_t *after = ui_stats();
    CHECK(ui_loaded(), "save found");
    CHECK(after->rolls == before.rolls && after->total_ep == before.total_ep, "rolls and EP kept");
    CHECK(!memcmp(after->tiers, before.tiers, sizeof before.tiers) && !memcmp(after->found, before.found, sizeof before.found), "tiers and badges kept");
    CHECK(after->best.ep == before.best.ep && !strcmp(after->best.num.digits, before.best.num.digits), "best roll kept");
    CHECK(ui_bank() > bank + 250, "rolls given back for the time away (%ld -> %ld)", bank, ui_bank());
    shot("13_menu_back");
    ui_save();
    l = save_host_list(&n);
    CHECK(l[at + sz] == 0 && l[at + sz + 1] == 0 && (l[at] | l[at + 1] << 8) == sz, "saved again in place");
    l[at + 2 + 13 + 50] ^= 1;   /* damage it: read as no save */
    ui_init(1, 0);
    CHECK(!ui_loaded() && ui_stats()->rolls == 0 && ui_bank() == START_ROLLS, "a damaged save is ignored");
    /* badges are saved by a 16-bit key made from the name (same as ui.c): no two may share one */
    static uint16_t key[N_BADGES];
    for (int b = 0; b < N_BADGES; b++) {
      uint32_t h = 2166136261u;
      for (const char *q = BADGES[b].name; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
      key[b] = (uint16_t)(h ^ (h >> 16));
      for (int c = 0; c < b; c++) CHECK(key[c] != key[b], "badges %s and %s share a save key", BADGES[c].name, BADGES[b].name);
    }
    CHECK(g_clipped == 0, "%d glyphs drawn off screen", g_clipped);
  }

  /* items: the rarity potions' rolls, number items on many numbers */
  for (int id = IT_MYTHIC; id <= IT_OTHERWORLDLY; id++)
    for (uint32_t k = 0; k < 64; k++) {
      s32 = item_pool_seed(id, k);
      roll_draw(&r.num, rnd);
      roll_score(&r);
      CHECK(r.tier >= ITEMS[id].tier && r.tier <= T_TRANSCENDENT, "%s seed %u: %s", ITEMS[id].name, k, TIER_NAMES[r.tier]);
    }
  for (int id = 0; id < N_ITEMS; id++)
    CHECK(item_price(id) > 0 && (id == 0 || ITEMS[id].kind != ITEMS[id - 1].kind || item_price(id) > item_price(id - 1)),
          "%s: priced above the one before", ITEMS[id].name);
  {
    const char *why;
    r = of(121);
    CHECK(!item_use(IT_MIRROR, &r, rnd, &why) && why[0], "121 reads the same backwards");
    CHECK(item_use(IT_GRAVITY, &r, rnd, &why) && !strcmp(r.num.digits, "10") && r.num.v == 10, "gravity: 121 -> 010 = 10 (%s)", r.num.digits);
    r = of(7);
    CHECK(item_use(IT_GRINDER, &r, rnd, &why) && r.num.v == 404 && r.tier == T_ERROR, "a lone digit grinds to 404");
    r = of(7);
    CHECK(!item_use(IT_SWAP, &r, rnd, &why), "one digit: no swap");
    r = of(4242);
    CHECK(item_use(IT_MIRROR, &r, rnd, &why) && r.num.v == 2424, "mirror 4242");
    r = of(12345);
    CHECK(item_use(IT_EDGE, &r, rnd, &why) && r.num.n + r.num.fn == 6, "edge adds a digit");
    long changed = 0;
    for (int i = 0; i < 40000; i++) {
      int id = IT_ROULETTE + i % (IT_EDGE - IT_ROULETTE + 1);
      roll_draw(&r.num, rnd);
      roll_score(&r);
      roll_t was = r;
      bool ok = item_use(id, &r, rnd, &why);
      if (!ok) { CHECK(!memcmp(&was.num, &r.num, sizeof r.num), "%s failed but changed the number", ITEMS[id].name); continue; }
      changed += strcmp(was.num.digits, r.num.digits) || strcmp(was.num.frac, r.num.frac);
      roll_t again;
      again.num = r.num;
      roll_score(&again);
      CHECK(r.num.n >= 1 && (r.num.n == 1 || r.num.digits[0] != '0') && (int)strlen(r.num.digits) == r.num.n &&
            (int)strlen(r.num.frac) == r.num.fn && !(r.num.fn && r.num.frac[r.num.fn - 1] == '0') && again.ep == r.ep,
            "%s on %s gives a bad number %s.%s", ITEMS[id].name, was.num.digits, r.num.digits, r.num.frac);
      if (fails > 20) return 1;
    }
    CHECK(changed > 30000, "number items change numbers (%ld)", changed);
  }

  /* the shop: buy every ware, use every item */
  {
    save_host_reset();
    ui_init(777, 0);
    CHECK(ui_balance() == 0, "nothing to spend at first");
    for (int k = 0; k < 6; k++) CHECK(ui_ware(k) >= 0 && ui_ware(k) < N_ITEMS, "ware %d", k);
    ui_give_ep(100000000000ull);
    press(eadk_key_down); press(eadk_key_ok);
    CHECK(ui_screen() == S_SHOP, "menu: shop");
    shot("20_shop");
    uint64_t bal = ui_balance();
    int got = 0;
    for (int k = 0; k < 6; k++) {
      int id = ui_ware(k);
      int had = ui_inv(id);
      press(eadk_key_ok);
      CHECK(ui_inv(id) == had + 1 && ui_balance() == bal - item_price(id), "bought %s", ITEMS[id].name);
      bal = ui_balance();
      got++;
      press(eadk_key_down);
    }
    shot("21_shop_bought");
    for (int i = 0; i < 260; i++) frame(0, 0);   /* Glim stops talking: the bubble tells the outlined ware */
    shot("21b_shop_idle");
    for (int id = 0; id < N_ITEMS; id++) ui_give_item(id, 2);
    press(eadk_key_right);
    shot("22_bag");
    press(eadk_key_ok);   /* the first row: Luck Potion I */
    CHECK(ui_luck_left() == 2 && ui_inv(IT_LUCK1) >= 2, "luck at work (%d)", ui_luck_left());
    shot("23_bag_luck");
    press(eadk_key_down); press(eadk_key_ok);
    CHECK(ui_luck_left() == 2, "a second luck potion waits");
    for (int i = 0; i < 9; i++) press(eadk_key_down);
    press(eadk_key_ok);   /* Mythic Potion */
    CHECK(ui_armed(), "rarity potion armed");
    press(eadk_key_back);
    press(eadk_key_up); press(eadk_key_ok);
    CHECK(ui_screen() == S_ROLL, "roll view");
    shot("24_roll_potions");
    press(eadk_key_one); press(eadk_key_ok);
    settle();
    CHECK(!ui_armed() && ui_shown()->tier >= T_MYTHIC, "the rarity potion's roll (%s)", TIER_NAMES[ui_shown()->tier]);
    CHECK(ui_luck_left() == 2, "a rarity roll keeps the luck");
    shot("25_rarity_roll");
    press(eadk_key_ok); settle();
    press(eadk_key_ok); settle();
    CHECK(ui_luck_left() == 0, "luck used up");
    /* number items on a grid roll */
    press(eadk_key_two); press(eadk_key_ok);
    settle();
    press(eadk_key_back);   /* done: the best roll */
    press(eadk_key_exe);    /* the grid */
    CHECK(ui_screen() == S_BULK, "grid");
    press(eadk_key_right); press(eadk_key_ok);   /* peek */
    settle();
    int before = ui_inv(IT_ROULETTE);
    long rolls = ui_stats()->rolls;
    shot("26_peek_items");
    press(eadk_key_exe);
    settle();
    CHECK(ui_inv(IT_ROULETTE) == before - 1 && ui_stats()->rolls == rolls, "roulette used, no roll spent");
    shot("27_item_used");
    press(eadk_key_right);
    press(eadk_key_exe);
    CHECK(ui_inv(IT_SLOT) == 2, "one item per number");
    press(eadk_key_back);
    CHECK(ui_screen() == S_BULK, "back to the grid");
    shot("28_grid_changed");
    /* kept in the save */
    uint64_t b = ui_balance();
    int inv = ui_inv(IT_ROULETTE), w0 = ui_ware(0);
    ui_save();
    ui_init(5, 1000);
    CHECK(ui_balance() == b && ui_inv(IT_ROULETTE) == inv && ui_ware(0) == w0, "the shop is saved");
    ui_init(5, 400000);   /* past a restock */
    CHECK(ui_loaded(), "loaded");
    CHECK(g_clipped == 0, "%d glyphs drawn off screen", g_clipped);
  }

  /* random keys */
  srand(11);
  for (int run = 0; run < RANDOM_RUNS; run++) {
    if (run % 50 == 0) save_host_reset();   /* most runs start from the last one's save */
    ui_init((uint32_t)rand(), (uint64_t)run * 1000);
    if (run % 3 == 0) {   /* EP and items to play with */
      ui_give_ep((uint64_t)rand() * 1000);
      for (int id = 0; id < N_ITEMS; id++) ui_give_item(id, rand() % 3);
    }
    alive = true;
    for (int f = 0; f < 4000 && alive; f++) {
      uint64_t k = 0;
      if (rand() % 10 < 5) {
        int key = rand() % 53;
        if (key == eadk_key_back && rand() % 6) key = eadk_key_ok;
        k = 1ull << key;
      }
      frame(k, k | (rand() % 4 == 0 ? 1ull << eadk_key_ok : 0));
      CHECK(ui_bank() >= 0 && ui_bank() <= MAX_BANK, "bank");
      if (fails > 20) return 1;
    }
  }
  CHECK(g_clipped == 0, "%d glyphs off screen in random runs", g_clipped);
  CHECK(host_out_of_screen == 0, "%ld pushes off screen", host_out_of_screen);
  printf(fails ? "%d FAILED\n" : "all checks passed\n", fails);
  return fails != 0;
}
