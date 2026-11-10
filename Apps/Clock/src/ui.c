/* NumWorks gives an external app no clock: only a millisecond counter that starts at power-up and stops while the
 * calculator is off. So the user sets the time once; the app saves (time set, counter value then) in the
 * calculator's file list and shows  time set + (counter now - counter then)  every time it opens. */
#include "ui.h"
#include <string.h>
#include "clock.h"
#include "gfx.h"
#include "save.h"

#define KEY(k) (1ull << (k))
enum { K_LEFT = 0, K_UP = 1, K_DOWN = 2, K_RIGHT = 3, K_OK = 4, K_BACK = 5, K_BACKSPACE = 17, K_EXE = 52 };
static const int8_t DIGIT_KEY[10] = {48, 42, 43, 44, 36, 37, 38, 30, 31, 32};

#define C_BG RGB(0x0F1724)
#define C_PANEL RGB(0x1C2738)
#define C_TEXT RGB(0xF4F1E8)
#define C_SEC RGB(0x5CC8FF)
#define C_DATE RGB(0xB8C2D0)
#define C_NOTE RGB(0x8592A6)
#define C_WARN RGB(0xFFB46B)
#define C_SEL RGB(0xFF8A3D)

/* ------------------------------------------------------------------ the saved time */
#define SAVE_NAME "clock.sav"
#define SAVE_SIZE 40
#define MAGIC 0x4B434C43u   /* "CLCK" */
typedef struct { uint32_t magic, version; int64_t wall; uint64_t at; } saved_t;

static saved_t clk;
static bool have;                 /* a time is set */
static uint64_t restored_until;   /* the "kept from last time" note shows until then */

static int64_t wall_at(uint64_t now) { return clk.wall + (int64_t)(now - clk.at); }
static void store(void) { save_store(SAVE_NAME, SAVE_SIZE, &clk, sizeof clk); }

/* ------------------------------------------------------------------ state */
static bool setting, dirty = true, fields_dirty;   /* dirty: the whole screen; fields_dirty: the set screen's fields */
static int field, typed;   /* field 0..4 = hours, minutes, day, month, year; typed = digits typed in it so far */
static int fv[5];
static const int LO[5] = {0, 0, 1, 1, 2000}, HI[5] = {23, 59, 31, 12, 2099}, LEN[5] = {2, 2, 2, 2, 4};
static int rep_key = -1;
static uint64_t rep_next;
static char drawn_hm[8], drawn_s[4], drawn_date[40];
static int drawn_note = -1;

static int month_of(const char *m) {
  static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (int i = 0; i < 12; i++) if (!memcmp(names + 3 * i, m, 3)) return i + 1;
  return 1;
}

static void fix_day(void) {
  int n = month_days(fv[4], fv[3]);
  if (fv[2] > n) fv[2] = n;
}

static void end_field(void) {
  if (typed && field == 4 && fv[4] < 100) fv[4] += 2000;
  typed = 0;
  if (fv[field] < LO[field]) fv[field] = LO[field];
  if (fv[field] > HI[field]) fv[field] = HI[field];
  if (field != 2) fix_day();   /* not while on the day: the month may come next ("31", then "12") */
}

static void open_set(uint64_t now) {
  if (have) {
    when_t w = when_of(wall_at(now));
    fv[0] = w.hour, fv[1] = w.minute, fv[2] = w.day, fv[3] = w.month, fv[4] = w.year;
  } else {   /* nothing yet: noon on the day this app was built */
    const char *d = __DATE__;   /* "Nov 10 2026" */
    fv[0] = 12, fv[1] = 0;
    fv[2] = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + d[5] - '0';
    fv[3] = month_of(d);
    fv[4] = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + d[10] - '0';
    if (fv[4] < LO[4] || fv[4] > HI[4]) fv[4] = 2026;
  }
  setting = true, field = 0, typed = 0, dirty = true;
}

void ui_init(uint64_t now) {
  saved_t s;
  have = save_load(SAVE_NAME, SAVE_SIZE, &s, sizeof s) && s.magic == MAGIC && now >= s.at;   /* counter went back: a restart */
  if (have) clk = s, restored_until = now + 20000;
  setting = false, dirty = true, rep_key = -1;
  if (!have) open_set(now);
}

bool ui_setting(void) { return setting; }
int64_t ui_wall(uint64_t now) { return have ? wall_at(now) : -1; }

/* ------------------------------------------------------------------ text */
static char *two(char *p, int v) { p[0] = (char)('0' + v / 10 % 10), p[1] = (char)('0' + v % 10), p[2] = 0; return p + 2; }
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *num(char *p, int v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10; while (v);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

static const char *const NOTES[2][3] = {
  {"The calculator has no clock: this counts on", "from the time you set, and pauses while", "the calculator is off. Check it after."},
  {"Kept from last time. If the calculator was off", "since then, it is behind by that long:", "press OK to set it again."},
};
static const char KEYS_LINE[] = "OK set   Up/Down 1 min   0 round to :00";
static int note_kind(uint64_t now) { return now < restored_until ? 1 : 0; }
const char *ui_note(int line) { return line < 3 ? NOTES[drawn_note < 0 ? 0 : drawn_note][line] : KEYS_LINE; }

/* ------------------------------------------------------------------ the clock screen */
static void draw_clock(uint64_t now) {
  when_t w = when_of(wall_at(now));
  char hm[8], s[4], date[40], *p;
  p = two(hm, w.hour);
  *p++ = ':';
  two(p, w.minute);
  two(s, w.second);
  p = put(date, WEEKDAYS[w.weekday]);
  *p++ = ' ';
  p = num(p, w.day);
  *p++ = ' ';
  p = put(p, MONTHS[w.month - 1]);
  *p++ = ' ';
  num(p, w.year);
  const font_t *df = g_text_w(&font_l, date) <= 312 ? &font_l : &font_m;   /* "Wednesday 30 September 2026" */
  int nk = note_kind(now);
  if (dirty) {
    g_fill(0, 0, 320, 240, C_BG);
    drawn_hm[0] = drawn_s[0] = drawn_date[0] = 0, drawn_note = -1;
    g_box(&font_s, 0, 224, 320, 16, (320 - g_text_w(&font_s, KEYS_LINE)) / 2, 12, KEYS_LINE, C_DATE, C_PANEL);
    dirty = false;
  }
  if (strcmp(date, drawn_date)) g_box_c(df, 0, 10, 320, 28, date, C_DATE, C_BG), strcpy(drawn_date, date);
  if (strcmp(hm, drawn_hm)) g_box_c(&font_big, 0, 44, 320, 80, hm, C_TEXT, C_BG), strcpy(drawn_hm, hm);
  if (strcmp(s, drawn_s)) g_box_c(&font_sec, 110, 126, 100, 44, s, C_SEC, C_BG), strcpy(drawn_s, s);
  if (nk != drawn_note) {
    for (int i = 0; i < 3; i++) g_box_c(&font_s, 0, 174 + i * 15, 320, 15, NOTES[nk][i], nk ? C_WARN : C_NOTE, C_BG);
    drawn_note = nk;
  }
}

/* ------------------------------------------------------------------ the set screen */
static const int FX[5] = {84, 172, 12, 100, 188}, FY[5] = {58, 58, 136, 136, 136}, FW[5] = {64, 64, 64, 64, 120};
#define FH 48

static void draw_field(int i) {
  char t[6];
  int n = LEN[i], v = fv[i];
  if (i == field && typed) {   /* the digits typed so far, then blanks */
    int k = typed, x = v;
    for (int j = k - 1; j >= 0; j--) t[j] = (char)('0' + x % 10), x /= 10;
    for (int j = k; j < n; j++) t[j] = '_';
  } else
    for (int j = n - 1; j >= 0; j--) t[j] = (char)('0' + v % 10), v /= 10;
  t[n] = 0;
  bool sel = i == field;
  g_box_c(&font_sec, FX[i], FY[i], FW[i], FH, t, sel ? C_BG : C_TEXT, sel ? C_SEL : C_PANEL);
}

static void draw_set(void) {
  g_fill(0, 0, 320, 240, C_BG);
  g_box_c(&font_l, 0, 6, 320, 26, "Set the time", C_TEXT, C_BG);
  g_box_c(&font_s, 0, 36, 320, 16, "Time, 24-hour", C_NOTE, C_BG);
  g_box_c(&font_s, 0, 114, 320, 16, "Date: day / month / year", C_NOTE, C_BG);
  g_box_c(&font_sec, 148, 58, 24, FH, ":", C_TEXT, C_BG);
  g_box_c(&font_sec, 76, 136, 24, FH, "/", C_NOTE, C_BG);
  g_box_c(&font_sec, 164, 136, 24, FH, "/", C_NOTE, C_BG);
  for (int i = 0; i < 5; i++) draw_field(i);
  g_box_c(&font_s, 0, 190, 320, 15, "Type digits, or Up/Down.  Left/Right: next.", C_DATE, C_BG);
  g_box_c(&font_s, 0, 205, 320, 15, "OK starts the clock at 00 seconds:", C_DATE, C_BG);
  g_box_c(&font_s, 0, 220, 320, 15, have ? "press it as the minute turns.  Back: cancel." : "press it as the minute turns.", C_DATE, C_BG);
  dirty = false;
}

static void step(int d) {
  end_field();
  int hi = field == 2 ? month_days(fv[4], fv[3]) : HI[field];
  fv[field] += d;
  if (fv[field] > hi) fv[field] = LO[field];
  if (fv[field] < LO[field]) fv[field] = hi;
  fix_day();
}

static bool set_key(uint64_t now, uint64_t hit) {
  for (int d = 0; d < 10; d++)
    if (hit & KEY(DIGIT_KEY[d])) {
      fv[field] = typed ? fv[field] * 10 + d : d;
      typed++;
      if (typed >= LEN[field] || fv[field] * 10 > HI[field]) {   /* no further digit could follow */
        end_field();
        if (field < 4) field++;
      }
      fields_dirty = true;
    }
  if (hit & KEY(K_BACKSPACE) && typed) typed--, fv[field] /= 10, fields_dirty = true;
  if (hit & KEY(K_LEFT)) end_field(), field = (field + 4) % 5, fields_dirty = true;
  if (hit & KEY(K_RIGHT)) end_field(), field = (field + 1) % 5, fields_dirty = true;
  if (hit & (KEY(K_OK) | KEY(K_EXE))) {
    end_field();
    fix_day();
    clk.magic = MAGIC, clk.version = 1;
    clk.wall = wall_from(fv[4], fv[3], fv[2], fv[0], fv[1], 0);
    clk.at = now;
    have = true, restored_until = 0;
    store();
    setting = false, dirty = true;
    return true;
  }
  if (hit & KEY(K_BACK)) {
    if (!have) return false;
    setting = false, dirty = true;
  }
  return true;
}

/* ------------------------------------------------------------------ frame */
static void repeat(uint64_t now, uint64_t hit, uint64_t held, int k, void (*act)(int), int arg) {
  if (hit & KEY(k)) act(arg), rep_key = k, rep_next = now + 400, fields_dirty |= setting;
  else if (rep_key == k && held & KEY(k) && now >= rep_next) act(arg), rep_next = now + 70, fields_dirty |= setting;
}
static void nudge(int d) { clk.wall += d * 60000LL; store(); restored_until = 0; }
static void act(int d) { if (setting) step(d); else nudge(d); }

bool ui_frame(uint64_t now, uint64_t hit, uint64_t held) {
  if (rep_key >= 0 && !(held & KEY(rep_key))) rep_key = -1;
  if (setting) {
    if (!set_key(now, hit & ~(KEY(K_UP) | KEY(K_DOWN)))) return false;
  } else {
    if (hit & KEY(K_BACK)) return false;
    if (hit & (KEY(K_OK) | KEY(K_EXE))) open_set(now);
    else if (hit & KEY(DIGIT_KEY[0])) {   /* round to the nearest minute */
      int64_t w = wall_at(now), r = (w + 30000) / 60000 * 60000;
      clk.wall += r - w;
      store();
      restored_until = 0;
    }
  }
  repeat(now, hit, held, K_UP, act, 1);
  repeat(now, hit, held, K_DOWN, act, -1);
  if (setting) {
    if (dirty) draw_set();
    else if (fields_dirty) for (int i = 0; i < 5; i++) draw_field(i);
    fields_dirty = false;
  } else
    draw_clock(now);
  return true;
}
