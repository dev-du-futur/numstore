/* PC check for Clock: calendar maths against known dates, setting the time with digits and arrows, counting on,
 * leaving and coming back (through the saved file), adjusting, a restart of the counter, random keys, and that
 * nothing is drawn off the screen. Screenshots (PPM) in the folder given. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eadk.h"
#include "gfx.h"
#include "clock.h"
#include "ui.h"
#include "save.h"

static const char *dir;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void shot(const char *name) {
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

enum { LEFT = 0, UP = 1, DOWN = 2, RIGHT = 3, OK = 4, BACK = 5, BKSP = 17, EXE = 52 };
static const int DIG[10] = {48, 42, 43, 44, 36, 37, 38, 30, 31, 32};
static uint64_t now = 5000;
static bool alive;
static void frame(uint64_t hit, uint64_t held) { if (alive && !ui_frame(now, hit, held)) alive = false; now += 20; }
static void press(int k) { frame(1ull << k, 1ull << k); frame(0, 0); }
static void type(const char *s) { for (; *s; s++) press(DIG[*s - '0']); }
static void wait_ms(uint64_t ms) { uint64_t end = now + ms; while (now < end) frame(0, 0); }
static void launch(void) { alive = true; ui_init(now); frame(0, 0); }

static int64_t W(int y, int m, int d, int hh, int mm, int ss) { return wall_from(y, m, d, hh, mm, ss); }
static void expect_wall(int64_t want, const char *what) {
  int64_t got = ui_wall(now);
  when_t g = when_of(got), e = when_of(want);
  CHECK(got / 1000 == want / 1000, "%s: got %04d-%02d-%02d %02d:%02d:%02d, want %04d-%02d-%02d %02d:%02d:%02d", what,
        g.year, g.month, g.day, g.hour, g.minute, g.second, e.year, e.month, e.day, e.hour, e.minute, e.second);
}

static void calendar(void) {
  CHECK(days_from_civil(1970, 1, 1) == 0, "epoch");
  CHECK(days_from_civil(2000, 1, 1) == 10957, "2000");
  CHECK(days_from_civil(2026, 11, 10) == 20767, "2026-11-10 = %lld", (long long)days_from_civil(2026, 11, 10));
  CHECK(when_of(W(2026, 11, 10, 0, 0, 0)).weekday == 1, "2026-11-10 is a Tuesday");
  CHECK(when_of(W(2000, 1, 1, 0, 0, 0)).weekday == 5, "2000-01-01 is a Saturday");
  CHECK(when_of(W(2024, 2, 29, 0, 0, 0)).weekday == 3, "2024-02-29 is a Thursday");
  CHECK(month_days(2024, 2) == 29 && month_days(2100, 2) == 28 && month_days(2000, 2) == 29 && month_days(2026, 2) == 28, "leap years");
  int y, m, d, wd = when_of(W(2000, 1, 1, 0, 0, 0)).weekday;
  for (int64_t z = days_from_civil(2000, 1, 1); z <= days_from_civil(2100, 12, 31); z++) {   /* every day both ways */
    civil_from_days(z, &y, &m, &d);
    if (days_from_civil(y, m, d) != z || d < 1 || d > month_days(y, m)) { CHECK(0, "day %lld", (long long)z); break; }
    when_t w = when_of(z * DAY_MS + 86399999);
    if (w.weekday != wd || w.hour != 23 || w.minute != 59 || w.second != 59) { CHECK(0, "when_of day %lld", (long long)z); break; }
    wd = (wd + 1) % 7;
  }
}

int main(int argc, char **argv) {
  dir = argc > 1 ? argv[1] : ".";
  calendar();
  save_host_reset();

  /* first launch: nothing saved, so the set screen, noon on the build day */
  launch();
  CHECK(ui_setting(), "first launch opens the set screen");
  CHECK(ui_wall(now) == -1, "no time yet");
  shot("01_set_first");
  /* Back with no time quits */
  press(BACK);
  CHECK(!alive, "Back on the first set screen quits");

  /* type 21:47 on 9 Nov 2026: each field moves on by itself once full */
  launch();
  type("2147");
  type("0911");
  type("2026");
  shot("02_set_typed");
  press(OK);
  CHECK(!ui_setting(), "OK shows the clock");
  expect_wall(W(2026, 11, 9, 21, 47, 0), "just set");
  shot("03_clock");
  wait_ms(73500);
  expect_wall(W(2026, 11, 9, 21, 48, 13), "73.5 s later");

  /* past midnight: the date turns */
  wait_ms((uint64_t)(2 * 3600 + 12 * 60) * 1000);
  expect_wall(W(2026, 11, 10, 0, 0, 13), "after midnight");
  shot("04_after_midnight");

  /* leave and come back much later (the calculator stayed on): the counter carried on */
  press(BACK);
  CHECK(!alive, "Back on the clock quits");
  now += 3 * 3600 * 1000;
  launch();
  CHECK(!ui_setting(), "coming back shows the clock");
  expect_wall(W(2026, 11, 10, 3, 0, 13), "came back 3 h later");
  CHECK(strstr(ui_note(0), "Kept from last time") != 0, "the restored note shows first: %s", ui_note(0));
  shot("05_restored");
  wait_ms(21000);
  CHECK(strstr(ui_note(0), "no clock") != 0, "then the usual note: %s", ui_note(0));

  /* Up/Down: one minute; held, it repeats */
  int64_t before = ui_wall(now);
  press(UP);
  CHECK(ui_wall(now) - before >= 60000 && ui_wall(now) - before < 61000, "Up adds a minute");
  press(DOWN), press(DOWN);
  CHECK(ui_wall(now) - before >= -60000 && ui_wall(now) - before < -59000 + 200, "Down takes one off");
  before = ui_wall(now);
  frame(1ull << UP, 1ull << UP);
  for (int i = 0; i < 50; i++) frame(0, 1ull << UP);   /* 1 s held: 400 ms, then every 70 ms */
  frame(0, 0);
  int64_t gained = (ui_wall(now) - before) / 60000;
  CHECK(gained >= 8 && gained <= 11, "holding Up for a second: %lld minutes", (long long)gained);

  /* 0 rounds to the nearest minute */
  int64_t w0 = ui_wall(now) % 60000;
  press(DIG[0]);
  int64_t w1 = ui_wall(now) % 60000;
  CHECK(w1 < 1000, "0 rounds to :00 (was %lld ms into the minute, now %lld)", (long long)w0, (long long)w1);

  /* OK opens the set screen on the current time; Back cancels without change */
  int64_t keep = ui_wall(now);
  press(OK);
  CHECK(ui_setting(), "OK opens the set screen");
  shot("06_set_from_clock");
  press(BACK);
  CHECK(!ui_setting() && llabs(ui_wall(now) - keep) < 200, "Back cancels");

  /* arrows: 23:59 on 31 Dec 2026 -> Up on minutes wraps, Up on day wraps; 31 -> February clamps */
  press(OK);
  type("2359");
  type("31");
  type("12");
  type("2026");
  press(OK);
  expect_wall(W(2026, 12, 31, 23, 59, 0), "New Year's Eve");
  wait_ms(61000);
  expect_wall(W(2027, 1, 1, 0, 0, 1), "New Year");
  shot("07_new_year");
  press(OK);                       /* fields: 00 00 01 01 2027 */
  press(RIGHT), press(RIGHT);      /* day */
  type("31");                       /* full: moves on to the month */
  type("2");                        /* February: one digit is enough; on to the year */
  shot("08_set_feb");
  press(OK);
  expect_wall(W(2027, 2, 28, 0, 0, 0), "31 -> February clamps to the 28th");
  press(OK);
  press(LEFT);                      /* from hours, Left wraps to the year */
  type("24");                       /* a two-digit year means 20xx */
  press(OK);
  expect_wall(W(2024, 2, 28, 0, 0, 0), "two-digit year");
  press(OK);
  type("3");                        /* 3 can't start an hour of two digits: 03, move on */
  type("7");                        /* 7 can't start minutes of two digits: 07 */
  press(OK);
  expect_wall(W(2024, 2, 28, 3, 7, 0), "single digits that can't go on");
  press(OK);
  type("1");
  press(BKSP);
  type("2");
  press(LEFT);                      /* leaving a field half typed keeps what was typed */
  press(OK);
  expect_wall(W(2024, 2, 28, 2, 7, 0), "backspace, then a half-typed field");

  /* counter restarted (a reset keeps the file only in theory): the set screen comes back */
  press(BACK);
  now = 1000;
  launch();
  CHECK(ui_setting(), "counter went back: set the time again");
  press(OK);
  CHECK(!ui_setting(), "set again");

  /* a damaged save reads as none */
  press(BACK);
  uint32_t n;
  uint8_t *b = save_host_list(&n);
  for (uint32_t i = 0; i + 9 < n; i++) if (!memcmp(b + i, "clock.sav", 9)) { b[i + 20] ^= 0x55; break; }
  launch();
  CHECK(ui_setting(), "damaged save: set screen");
  press(OK);
  press(BACK);
  /* the other files in the list are untouched */
  CHECK(!memcmp(b + 2, "hello.py", 9), "other files kept");

  /* the longest dates fit */
  for (int m = 1; m <= 12; m++)
    for (int wd = 0; wd < 7; wd++) {
      char t[48];
      snprintf(t, sizeof t, "%s 30 %s 2026", WEEKDAYS[wd], MONTHS[m - 1]);
      CHECK(g_text_w(&font_l, t) <= 312 || g_text_w(&font_m, t) <= 312, "date fits: %s", t);
    }
  launch();
  press(OK);
  type("0930");
  type("30");
  type("09");
  type("2026");
  press(OK);
  shot("09_long_date");
  for (int i = 0; i < 4; i++) CHECK(g_text_w(&font_s, ui_note(i)) <= 316, "note fits: %s", ui_note(i));

  /* random keys, with time passing and relaunches */
  srand(7);
  static const int keys[] = {LEFT, UP, DOWN, RIGHT, OK, BACK, BKSP, EXE, 48, 42, 43, 44, 36, 37, 38, 30, 31, 32};
  for (int run = 0; run < 300; run++) {
    launch();
    for (int i = 0; i < 400 && alive; i++) {
      int k = keys[rand() % (int)(sizeof keys / sizeof *keys)];
      if (rand() % 3) frame(0, 0);
      else press(k);
      if (rand() % 50 == 0) now += (uint64_t)(rand() % 100000);
      int64_t w = ui_wall(now);
      if (!ui_setting() && w >= 0) {
        when_t t = when_of(w);
        if (t.year < 2000 || t.year > 2101) { CHECK(0, "year out of range: %d", t.year); break; }
      }
    }
  }

  CHECK(g_clipped == 0, "%d boxes off the screen or text cut", g_clipped);
  CHECK(host_out_of_screen == 0, "%ld pushes off the screen", host_out_of_screen);
  printf("%s (%d failures)\n", fails ? "FAILED" : "all good", fails);
  return fails != 0;
}
