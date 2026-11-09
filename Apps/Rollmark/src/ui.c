/* Rollmark's screens: menu, the roll view, a bulk summary, Glim's shop and the bag, the badge book, records and
 * how to play. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "items.h"
#include "paint.h"
#include "save.h"

/* RNGidle's dark theme */
#define P_PAGE 0x1C1A21u
#define P_SURFACE 0x25232Au
#define P_RAISED 0x312E36u
#define P_OUTLINE 0x45424Bu
#define P_PROSE 0xF0F0F0u
#define P_PROSE2 0xC4C4C4u
#define P_PROSE3 0x9A9A9Au
#define P_GREEN 0x34D399u
#define C_BG RGB(P_PAGE)
#define C_PANEL RGB(P_SURFACE)
#define C_SEL RGB(P_RAISED)
#define C_LINE RGB(P_OUTLINE)
#define C_TEXT RGB(P_PROSE)
#define C_DIM RGB(P_PROSE3)
#define C_FAINT RGB(0x5F5C66)
#define C_GOLD RGB(0xFCD34D)
#define C_NEW RGB(P_GREEN)

/* how each tier paints a number: the gem (left, middle, right of its gradient, border, digits, glow), its pill,
 * and its colour as text on the page. Trash to Mythic: RNGidle's dark palette over the page; rarer tiers: their
 * own gradients. */
typedef struct {
  uint32_t fill[3], border, text, glow;
  uint32_t pill[2], pill_border, pill_text;
  uint32_t ink;
} tier_style_t;

static const tier_style_t TS[N_TIERS] = {
    [T_TRASHEST] = {{0x38333A, 0x4B454D, 0x38333A}, 0x867D88, 0xF5F0F2, 0, {0x4B454D, 0x4B454D}, 0x867D88, 0xF5F0F2, 0xB5AEB8},
    [T_TRASH] = {{0x532A16, 0x27272A, 0x532A16}, 0x92400E, 0xFCD34D, 0, {0x2D1A15, 0x2D1A15}, 0x633015, 0xFCD34D, 0xFCD34D},
    [T_COMMON] = {{0x2D2C33, 0x27272A, 0x2D2C33}, 0x52525B, 0xD1D5DB, 0, {0x27272A, 0x27272A}, 0x52525B, 0xD1D5DB, 0xD1D5DB},
    [T_UNCOMMON] = {{0x0F3930, 0x27272A, 0x0F3930}, 0x065F46, 0x34D399, 0, {0x122222, 0x122222}, 0x0F4337, 0x34D399, 0x34D399},
    [T_RARE] = {{0x1D2D60, 0x27272A, 0x1D2D60}, 0x1E40AF, 0x60A5FA, 0, {0x1A1F36, 0x1A1F36}, 0x1D3076, 0x60A5FA, 0x60A5FA},
    [T_EPIC] = {{0x401B5E, 0x27272A, 0x401B5E}, 0x6B21A8, 0xC084FC, 0x6B21A8, {0x29133C, 0x29133C}, 0x4B1E72, 0xC084FC, 0xC084FC},
    [T_ANOMALY] = {{0x552518, 0x27272A, 0x552518}, 0x9A3412, 0xFB923C, 0x9A3412, {0x2C1817, 0x2C1817}, 0x672918, 0xFB923C, 0xFB923C},
    [T_MYTHIC] = {{0x571B1E, 0x27272A, 0x571B1E}, 0x9D174D, 0xF87171, 0xDB2777, {0x2D1418, 0x2D1418}, 0x69183B, 0xF87171, 0xF87171},
    [T_UNREAL] = {{0xD9F8E9, 0xECECEB, 0xFFE1EE}, 0x7ADBBC, 0x245548, 0x7ADBBC, {0xD9F8E9, 0xFFE1EE}, 0x7ADBBC, 0x245548, 0x7ADBBC},
    [T_ABERRATION] = {{0x3A2459, 0x5A4C6A, 0xB9E892}, 0xA97CE6, 0xEFFFE2, 0xCAFF92, {0x3A2459, 0xB9E892}, 0xA97CE6, 0xEFFFE2, 0xCAFF92},
    [T_OTHERWORLDLY] = {{0x020402, 0x0F3A16, 0x39FF6A}, 0x2BD957, 0xB8FF6A, 0x39FF6A, {0x020402, 0x39FF6A}, 0x2BD957, 0xB8FF6A, 0x39FF6A},
    [T_CATACLYSMIC] = {{0x1A0603, 0x8A1606, 0xFF6A1F}, 0xB3200C, 0xFFD2A1, 0xFF8A2A, {0x1A0603, 0xFF6A1F}, 0xB3200C, 0xFFD2A1, 0xFF8A2A},
    [T_DIVINE] = {{0xFFFFFF, 0xFFF8E0, 0xFFF2C0}, 0xD6B453, 0x795C16, 0xFFE9A0, {0xFFFFFF, 0xFFF2C0}, 0xD6B453, 0x795C16, 0xFFE9A0},
    [T_TRANSCENDENT] = {{0xFFF8DF, 0xE4F8EF, 0xC8F9FF}, 0x75BED0, 0x31566B, 0xC8F9FF, {0xFFF8DF, 0xC8F9FF}, 0x75BED0, 0x31566B, 0xC8F9FF},
    [T_DEVILISH] = {{0x050000, 0x5C0409, 0xE0141E}, 0x8A0A10, 0xFFD0C2, 0xFF3B1A, {0x050000, 0xE0141E}, 0x8A0A10, 0xFFD0C2, 0xFF3B1A},
    [T_ERROR] = {{0x05020A, 0x3B1270, 0x8B3DFF}, 0x5B21B6, 0xE9D5FF, 0x8B3DFF, {0x05020A, 0x8B3DFF}, 0x5B21B6, 0xE9D5FF, 0xB98CFF},
};

/* before the rarity shows: grey (zinc) */
static const tier_style_t NEUTRAL = {{0x27272A, 0x18181B, 0x27272A}, 0x52525B, 0x71717A, 0, {0, 0}, 0, 0, 0};

static uint16_t tier_c(int t) { return RGB(TS[t].ink); }

/* the badge's own rarity: the tier a roll would reach with that badge alone */
static int badge_tier(int b) {
  uint64_t ep = badge_ep(b);
  int t = T_TRASHEST;
  for (int i = T_TRASH; i <= T_TRANSCENDENT; i++)
    if (ep >= tier_min(i)) t = i;
  return t;
}

static const int SIZES[4] = {1, 10, 100, 1000};

/* RNGidle's bulk flow: the grid turns over with the rare rolls folded ("?" cards); OK reveals the outlined card
 * (a folded one with its full reveal), EXE unfolds the rare rolls one by one, rarest last, with NEXT; Back on the
 * roll view comes back to the grid, whatever is still folded. */
enum { BP_NONE, BP_TURN, BP_GRID };
enum { RM_NORMAL, RM_UNFOLD, RM_PEEK };
#define FEATURE_TIER T_MYTHIC   /* a roll this rare or better unfolds (404 and 666 too) */
#define FEATURE_MAX 10          /* the best ones, at most; with none, the batch's best roll unfolds */

#define BOOK_ROWS 11

/* Glim's shop, as in RNGidle: 6 wares, new ones every 5 minutes; EP from rolls is what it takes */
#define WARES 6
#define RESTOCK_MS 300000
#define MAX_OV 64                 /* batch rolls an item can change, per batch */
#define BAG_ROWS 7

static struct {
  int screen;
  uint32_t seed;
  long bank;
  int refill_ms, drawn_bank;
  int size;                       /* index in SIZES */
  roll_t shown;
  bool has_shown;
  uint32_t fresh[MAX_EARNED / 32];  /* bit i: shown.badges[i] was found for the first time */
  uint8_t order[MAX_EARNED];        /* display order of shown.badges */
  int anim_ms, anim_len;          /* reveal of a single roll; anim_len 0: none */
  int tick;
  int scroll;
  /* bulk */
  int bulk_n, bulk_new, bulk_sel, bulk_top, bulk_shown, bulk_ms, bulk_step;
  int bulk_phase;                 /* BP_*: where the batch is in RNGidle's bulk flow */
  uint16_t feat[FEATURE_MAX];     /* the rare rolls, rarest last */
  int feat_n, folded, bulk_last;  /* how many are still folded; the last one unfolded or opened */
  uint8_t bulk_fold[(1000 + 7) / 8];
  int roll_mode;                  /* RM_*: what the roll view's button does */
  bool from_bulk;                 /* the roll on view came from the grid: Back goes back to it */
  bool bulk_sorted;
  long bulk_tiers[N_TIERS];
  uint32_t bulk_seed[1000];       /* each roll of the batch, kept as the seed it was drawn from */
  uint64_t bulk_ep[1000];
  uint8_t bulk_tier[1000];
  uint16_t bulk_order[1000];      /* grid position -> roll */
  /* menus */
  int menu_sel, book_sel, book_top;
  char msg[48];
  int msg_ms;
  int rep_key, rep_ms;
  stats_t st;
  uint64_t clock;                 /* the calculator's clock, kept up to date frame by frame */
  int save_ms;
  bool loaded;
  long away_rolls;                /* rolls given back for the time away, shown on the menu */
  /* the shop: EP to spend, the bag, the potions at work */
  uint64_t balance;
  uint16_t inv[N_ITEMS];
  int luck_id;                    /* the luck potion at work, -1: none */
  long luck_left;                 /* rolls it still covers */
  int armed;                      /* the rarity potion waiting for the next roll, -1: none */
  uint32_t shop_slot;             /* restocks so far */
  int32_t shop_ms;                /* until the next restock */
  uint8_t ware[WARES];
  uint16_t stock[WARES];
  int shop_tab, shop_sel, bag_sel, bag_top, shop_sec;
  int glim_ms, glim_talk, glim_hold, glim_bob, glim_frame, glim_kind, glim_pick;   /* Glim, the shopkeeper */
  int num_sel;                    /* the number item picked in the roll view's footer */
  int shown_bulk;                 /* the batch roll on view, -1: a single roll */
  bool shown_mod;                 /* an item changed the roll on view already: one per number */
  uint8_t bulk_mod[(1000 + 7) / 8];
  int ov_n;
  struct { uint16_t i; number_t n; } ov[MAX_OV];   /* batch rolls changed by an item */
} U;

static uint32_t rnd(void) {
  U.seed ^= U.seed << 13;
  U.seed ^= U.seed >> 17;
  U.seed ^= U.seed << 5;
  return U.seed;
}

const stats_t *ui_stats(void) { return &U.st; }
const roll_t *ui_shown(void) { return &U.shown; }
int ui_screen(void) { return U.screen; }
long ui_bank(void) { return U.bank; }
bool ui_bulk_open(void) { return U.bulk_phase != BP_NONE && U.folded > 0; }
bool ui_busy(void) { return U.anim_len > 0 || (U.screen == S_BULK && U.bulk_phase == BP_TURN); }
uint64_t ui_balance(void) { return U.balance; }
int ui_inv(int id) { return U.inv[id]; }
int ui_luck_left(void) { return U.luck_id < 0 ? 0 : (int)U.luck_left; }
bool ui_armed(void) { return U.armed >= 0; }
int ui_ware(int k) { return U.stock[k] ? U.ware[k] : -1; }
#ifdef HOST
void ui_give_ep(uint64_t ep) { U.balance += ep; }
void ui_give_item(int id, int n) { U.inv[id] = (uint16_t)(U.inv[id] + n); }
#endif
#define FOLDED(i) ((U.bulk_fold[(i) >> 3] >> ((i) & 7)) & 1)
#define MODDED(i) ((U.bulk_mod[(i) >> 3] >> ((i) & 7)) & 1)

static uint64_t add_sat(uint64_t a, uint64_t b) { return a + b < a ? UINT64_MAX : a + b; }

/* ---------------------------------------------------------------- shared pieces */

static void header(const char *title) {
  g_fill(0, 0, 320, 19, C_PANEL);
  g_fill(0, 19, 320, 1, C_LINE);
  g_text(&font_s, 10, 14, title, RGB(P_PROSE2), C_PANEL);
  char t[32], n[16];
  fmt_int((uint64_t)U.bank, n);
  snprintf(t, sizeof t, "%s roll%s", n, U.bank == 1 ? "" : "s");
  g_text(&font_s, 310 - g_text_w(&font_s, t), 14, t, C_GOLD, C_PANEL);
  U.drawn_bank = (int)U.bank;
}

static void footer(const char *l, const char *r) {
  g_fill(0, 220, 320, 20, C_PANEL);
  g_fill(0, 220, 320, 1, C_LINE);
  g_text(&font_s, 10, 234, l, C_DIM, C_PANEL);
  g_text(&font_s, 310 - g_text_w(&font_s, r), 234, r, C_DIM, C_PANEL);
}

/* cuts s so it fits in w pixels, ending in ".." when cut */
static void fit(const font_t *f, char *s, int w) {
  int n = (int)strlen(s);
  if (g_text_w(f, s) <= w) return;
  while (n > 0) {
    s[--n] = 0;
    char t[64];
    snprintf(t, sizeof t, "%s..", s);
    if (g_text_w(f, t) <= w) { strcpy(s, t); return; }
  }
}

static void ep_text(uint64_t ep, char *out) {
  char n[32];
  fmt_int(ep, n);
  sprintf(out, "%s EP", n);
}

static void upper(const char *s, char *out) {
  for (; *s; s++) *out++ = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
  *out = 0;
}

/* a card in a tier's look; light: the pill's colours instead of the gem's */
static card_t tier_card(const tier_style_t *s, bool pill, int x, int y, int w, int h, int r, int bw) {
  card_t c;
  memset(&c, 0, sizeof c);
  c.x = x; c.y = y; c.w = w; c.h = h; c.r = r; c.bw = bw;
  if (pill) {
    c.fill[0] = s->pill[0];
    c.fill[1] = rgb_mix(s->pill[0], s->pill[1], 128);
    c.fill[2] = s->pill[1];
    c.border[0] = c.border[1] = s->pill_border;
  } else {
    memcpy(c.fill, s->fill, sizeof c.fill);
    c.border[0] = c.border[1] = s->border;
    c.sheen = 40;
  }
  c.out = P_PAGE;
  return c;
}

/* ---------------------------------------------------------------- roll view */

#define GEM_Y 25
#define GEM_H 44

static void number_band(void) {
  char t[40];
  const tier_style_t *s = &NEUTRAL;
  if (!U.has_shown) strcpy(t, "???,???");
  else {
    roll_text(&U.shown.num, t);
    if (!U.anim_len) s = &TS[U.shown.tier];
  }
  const font_t *f = &font_gem;
  if (g_text_w(f, t) > 260) f = &font_gem_s;
  if (g_text_w(f, t) > 280) f = &font_data;
  int w = g_text_w(f, t) + 44;
  if (w < 150) w = 150;
  if (w > 312) w = 312;
  card_t c = tier_card(s, false, 160 - w / 2, GEM_Y, w, GEM_H, 10, 2);
  if (U.has_shown && !U.anim_len && s->glow) { c.glow = s->glow; c.glow_px = 5; }
  int base = GEM_Y + (GEM_H + f->cap) / 2;
  if (U.anim_len && U.anim_ms > 0) card_area(&c, c.x + 4, base - f->ascent, c.w - 8, f->ascent + 8);   /* a spin frame: the digits only */
  else {
    g_fill(0, 20, 320, 55, C_BG);
    card_draw(&c);
  }
  if (!U.anim_len) {
    card_text_c(&c, f, 160, base, t, U.has_shown ? s->text : 0x71717A);
    return;
  }
  /* digits spin, then settle from the left */
  int len = (int)strlen(t), half = U.anim_len / 2, x = 160 - g_text_w(f, t) / 2;
  for (int i = 0; i < len; i++) {
    char ch[2] = {t[i], 0};
    int settle = half + (U.anim_len - half) * (i + 1) / (len + 1);
    bool spinning = U.anim_ms < settle && t[i] >= '0' && t[i] <= '9';
    if (spinning) ch[0] = (char)('0' + rnd() % 10);
    x = card_text(&c, f, x, base, ch, spinning ? 0x71717A : 0xE5E7EB);
  }
}

static const char *const TOP_TEXT[N_TIERS] = {
    "bottom 2%", "top 98%", "top 80%", "top 45%", "top 18%", "top 6%", "top 2%", "top 0.5%", "top 0.05%",
    "top 0.005%", "top 0.0005%", "1 in 4 million", "beyond 1 in 4M", "beyond 1 in 4M", "a special number",
    "a special number"};

static void tier_band(void) {
  g_fill(0, 75, 320, 37, C_BG);
  if (!U.has_shown || U.anim_len) return;
  int t = U.shown.tier;
  const tier_style_t *s = &TS[t];
  char name[24];
  upper(TIER_NAMES[t], name);
  int pw = g_text_w(&font_s, name) + 12, tw = g_text_w(&font_s, TOP_TEXT[t]);
  int x = 160 - (pw + 14 + tw) / 2;
  card_t c = tier_card(s, true, x, 77, pw, 15, 3, 1);
  card_draw(&c);
  card_text_c(&c, &font_s, x + pw / 2, 88, name, s->pill_text);
  g_fill(x + pw + 6, 83, 3, 3, RGB(0x625F69));
  g_text(&font_s, x + pw + 14, 88, TOP_TEXT[t], tier_c(t), C_BG);
  /* the EP pill */
  char e[40];
  ep_text(U.shown.ep, e);
  int ew = g_text_w(&font_data, e) + 22;
  card_t p = tier_card(s, true, 160 - ew / 2, 95, ew, 16, 8, 1);
  card_draw(&p);
  card_text_c(&p, &font_data, 160, 95 + (16 + font_data.cap) / 2, e, s->pill_text);
}

/* the black button with the pink-to-purple ring */
static void roll_button(void) {
  g_fill(0, 112, 320, 38, C_BG);
  card_t c;
  memset(&c, 0, sizeof c);
  c.x = 16; c.y = 118; c.w = 288; c.h = 26; c.r = 8; c.bw = 2;
  c.border[0] = 0xEC4899; c.border[1] = 0xA855F7;
  c.glow = 0xC0408A; c.glow_px = 5;
  c.out = P_PAGE;
  char t[64], n[16];
  uint32_t fg = 0xFFFFFF;
  const font_t *f = &font_l;
  if (U.anim_len) { strcpy(t, "PROCESSING..."); f = &font_m; }
  else if (U.roll_mode == RM_UNFOLD) {
    snprintf(t, sizeof t, "NEXT  -  %d of %d shown, %d left", U.feat_n - U.folded, U.feat_n, U.folded);
    f = &font_m;
  } else if (U.roll_mode == RM_PEEK) { strcpy(t, "BACK TO THE BULK ROLL"); f = &font_m; }
  else if (U.bank <= 0) { strcpy(t, "OUT OF ROLLS"); fg = P_PROSE3; c.glow = 0; }
  else if (SIZES[U.size] > 1) { fmt_int((uint64_t)SIZES[U.size], n); snprintf(t, sizeof t, "GENERATE x%s", n); }
  else strcpy(t, "GENERATE");
  card_draw(&c);
  card_text_c(&c, f, 160, c.y + (c.h + f->cap) / 2, t, fg);
}

/* the shown roll's badges in display order: paying ones first, the richest first */
static void sort_shown(void) {
  int n = U.shown.nb;
  for (int i = 0; i < n; i++) U.order[i] = (uint8_t)i;
  for (int i = 1; i < n; i++) {
    uint8_t k = U.order[i];
    int j = i;
    while (j > 0) {
      int o = U.order[j - 1];
      bool pk = BIT(U.shown.paid, k), po = BIT(U.shown.paid, o);
      bool before = pk != po ? pk : badge_ep(U.shown.badges[k]) > badge_ep(U.shown.badges[o]);
      if (!before) break;
      U.order[j] = U.order[j - 1];
      j--;
    }
    U.order[j] = k;
  }
}

#define BADGE_Y 152

/* a badge chip: paying badges in the colours of their own rarity, the others greyed */
static void badge_cell(int slot_k, int slot) {
  int x = slot % 2 ? 163 : 7, y = BADGE_Y + (slot / 2) * 16, w = 150, h = 14;
  g_fill(x, y, w, h, C_BG);
  if (slot_k >= U.shown.nb) return;
  int k = U.order[slot_k];
  int b = U.shown.badges[k];
  bool paid = BIT(U.shown.paid, k), fresh = BIT(U.fresh, k);
  const tier_style_t *s = &TS[badge_tier(b)];
  card_t c = tier_card(s, true, x, y, w, h, 3, 1);
  if (!paid) {
    c.fill[0] = c.fill[1] = c.fill[2] = P_SURFACE;
    c.border[0] = c.border[1] = P_OUTLINE;
  }
  uint32_t fg = paid ? s->pill_text : 0x77737E;
  card_draw(&c);
  char n[24], name[40];
  fmt_int(badge_ep(b), n);
  int nw = g_text_w(&font_s, n);
  int tx = x + 5;
  if (fresh) { g_fill(x + 4, y + 5, 4, 4, C_NEW); tx += 7; }
  snprintf(name, sizeof name, "%s", BADGES[b].name);
  fit(&font_s, name, x + w - 8 - nw - tx);
  card_text(&c, &font_s, tx, y + 11, name, fg);
  card_text(&c, &font_s, x + w - 5 - nw, y + 11, n, fg);
}

#define VIS_BADGES 8

static void roll_footer(void);

/* the number item EXE uses on the roll on view: the picked one if still in the bag, else the first one there */
static int num_item(void) {
  if (U.num_sel >= IT_ROULETTE && U.num_sel <= IT_EDGE && U.inv[U.num_sel]) return U.num_sel;
  for (int id = IT_ROULETTE; id <= IT_EDGE; id++)
    if (U.inv[id]) return U.num_sel = id;
  return -1;
}

static void num_step(int by) {
  int id = num_item();
  if (id < 0) return;
  for (int k = 0; k < IT_EDGE - IT_ROULETTE; k++) {
    id = IT_ROULETTE + (id - IT_ROULETTE + by + (IT_EDGE - IT_ROULETTE + 1)) % (IT_EDGE - IT_ROULETTE + 1);
    if (U.inv[id]) { U.num_sel = id; return; }
  }
}

/* the footer's left side on a grid roll: < EXE: the number item, how many > */
static void item_picker(void) {
  int id = num_item();
  if (id < 0) { g_text(&font_s, 10, 234, "Number items: in the shop", C_FAINT, C_PANEL); return; }
  char t[48];
  snprintf(t, sizeof t, "EXE: %s x%d", ITEMS[id].name, U.inv[id]);
  fit(&font_s, t, 190);
  int x = g_text(&font_s, 6, 234, "<", C_DIM, C_PANEL) + 4, w = g_text_w(&font_s, t) + 14;
  card_t c;
  memset(&c, 0, sizeof c);
  c.x = x; c.y = 223; c.w = w; c.h = 15; c.r = 4; c.bw = 1;
  c.border[0] = U.shown_mod ? P_OUTLINE : 0xEC4899; c.border[1] = U.shown_mod ? P_OUTLINE : 0xA855F7;
  c.out = P_SURFACE;
  card_draw(&c);
  card_text_c(&c, &font_s, x + w / 2, 234, t, U.shown_mod ? P_PROSE3 : 0xFFFFFF);
  g_text(&font_s, x + w + 4, 234, ">", C_DIM, C_PANEL);
}

static void badges_band(void) {
  g_fill(0, 150, 320, 70, C_BG);
  if (!U.has_shown || U.anim_len) return;
  if (U.shown.nb == 0) {
    g_text_c(&font_s, 160, 184, "No badge at all. That is rare too... sort of.", C_DIM, C_BG);
    return;
  }
  for (int s = 0; s < VIS_BADGES; s++) badge_cell(U.scroll * 2 + s, s);
  roll_footer();
}

/* the footer: how many to generate (x1 x10 x100 x1000, the chosen one ringed like the button), or a message */
static void roll_footer(void) {
  char r[40], b[16] = "";
  int rows = (U.shown.nb + 1) / 2;
  if (U.has_shown && !U.anim_len && rows > VIS_BADGES / 2) snprintf(b, sizeof b, "%d/%d", U.scroll + VIS_BADGES / 2, rows);
  if (U.roll_mode != RM_NORMAL || U.from_bulk) snprintf(r, sizeof r, "Back: grid");
  else if (U.bulk_phase == BP_GRID) snprintf(r, sizeof r, "EXE: grid");
  else if (b[0]) snprintf(r, sizeof r, "badges %s", b);
  else snprintf(r, sizeof r, "Back: menu");
  if (U.msg_ms > 0) { footer(U.msg, r); return; }
  footer("", r);
  if (U.roll_mode != RM_NORMAL) { item_picker(); return; }   /* a roll from the grid: number items instead */
  static const char *const AMOUNT[4] = {"x1", "x10", "x100", "x1000"};
  int x = 6;
  x = g_text(&font_s, x, 234, "<", C_DIM, C_PANEL) + 4;
  for (int i = 0; i < 4; i++) {
    int w = g_text_w(&font_s, AMOUNT[i]) + 10;
    if (i == U.size) {
      card_t c;
      memset(&c, 0, sizeof c);
      c.x = x; c.y = 223; c.w = w; c.h = 15; c.r = 4; c.bw = 1;
      c.border[0] = 0xEC4899; c.border[1] = 0xA855F7;
      c.out = P_SURFACE;
      card_draw(&c);
      card_text_c(&c, &font_s, x + w / 2, 234, AMOUNT[i], 0xFFFFFF);
    } else g_text_c(&font_s, x + w / 2, 234, AMOUNT[i], C_DIM, C_PANEL);
    x += w + 2;
  }
  g_text(&font_s, x + 2, 234, ">", C_DIM, C_PANEL);
}

/* the roll view's title, with the potions at work */
static void roll_title(char *t) {
  char l[32] = "";
  if (U.luck_id >= 0) {
    char n[16];
    int m = ITEMS[U.luck_id].luck10;
    fmt_int((uint64_t)U.luck_left, n);
    snprintf(l, sizeof l, "LUCK x%d.%d (%s)", m / 10, m % 10, n);
  }
  if (U.armed >= 0) {
    char a[24];
    upper(TIER_NAMES[ITEMS[U.armed].tier], a);
    sprintf(t, "%s%sNEXT: %s+", l, l[0] ? "  " : "", a);
  } else if (l[0]) sprintf(t, "ROLLMARK  %s", l);
  else strcpy(t, "ROLLMARK");
}

static void draw_roll(void) {
  g_fill(0, 20, 320, 200, C_BG);
  char t[64];
  roll_title(t);
  header(t);
  number_band();
  tier_band();
  roll_button();
  badges_band();
  roll_footer();
}

/* ---------------------------------------------------------------- bulk grid */

#define GRID_COLS 4
#define GRID_ROWS 6
#define CARD_W 75
#define CARD_H 26
#define GRID_X 3
#define GRID_Y 40

/* a batch roll's number, drawn again from its seed */
static void bulk_number(int i, number_t *n) {
  if (MODDED(i))
    for (int k = 0; k < U.ov_n; k++)
      if (U.ov[k].i == i) { *n = U.ov[k].n; return; }
  uint32_t keep = U.seed;
  U.seed = U.bulk_seed[i];
  roll_draw(n, rnd);
  U.seed = keep;
}

static void card_ring(int idx, uint16_t c) {
  int slot = idx - U.bulk_top * GRID_COLS;
  if (slot < 0 || slot >= GRID_COLS * GRID_ROWS) return;
  int x = GRID_X + (slot % GRID_COLS) * (CARD_W + 5), y = GRID_Y + (slot / GRID_COLS) * (CARD_H + 4);
  g_frame(x - 2, y - 2, CARD_W + 4, CARD_H + 4, 1, c);
}

static void grid_card(int idx) {
  int slot = idx - U.bulk_top * GRID_COLS;
  if (slot < 0 || slot >= GRID_COLS * GRID_ROWS) return;
  int x = GRID_X + (slot % GRID_COLS) * (CARD_W + 5), y = GRID_Y + (slot / GRID_COLS) * (CARD_H + 4);
  g_fill(x - 2, y - 2, CARD_W + 4, CARD_H + 4, C_BG);
  if (idx >= U.bulk_n) return;
  int i = U.bulk_order[idx];
  bool hidden = idx >= U.bulk_shown, fold = !hidden && FOLDED(i);
  const tier_style_t *s = hidden || fold ? &NEUTRAL : &TS[U.bulk_tier[i]];
  card_t c = tier_card(s, false, x, y, CARD_W, CARD_H, 6, 1);
  if (fold) { c.border[0] = 0xEC4899; c.border[1] = 0xA855F7; }   /* a rare roll, still folded */
  card_draw(&c);
  char t[40];
  if (fold) {
    card_text_c(&c, &font_card, x + CARD_W / 2, y + (CARD_H + font_card.cap) / 2, "? ? ?", 0xF9A8D4);
    if (idx == U.bulk_sel && U.bulk_phase == BP_GRID) card_ring(idx, C_TEXT);
    return;
  }
  if (hidden) strcpy(t, "??????");
  else {
    number_t n;
    bulk_number(i, &n);
    roll_text(&n, t);
  }
  const font_t *f = g_text_w(&font_card, t) <= CARD_W - 8 ? &font_card : &font_card_s;
  fit(f, t, CARD_W - 6);
  card_text_c(&c, f, x + CARD_W / 2, y + (CARD_H + f->cap) / 2, t, hidden ? 0x71717A : s->text);
  if (idx == U.bulk_sel && !hidden && U.bulk_phase == BP_GRID) card_ring(idx, C_TEXT);
}

/* under the header: how many, the rarest tiers in the batch, new badges */
static void bulk_strip(void) {
  g_fill(0, 20, 320, 17, C_BG);
  if (U.bulk_phase == BP_TURN) {
    g_text(&font_s, 7, 33, "Generating...", C_DIM, C_BG);
    return;
  }
  long count[N_TIERS] = {0};   /* the cards on the grid: the folded rolls stay a surprise */
  for (int k = 0; k < U.bulk_n; k++)
    if (!FOLDED(k)) count[U.bulk_tier[k]]++;
  int x = 7;
  for (int t = N_TIERS - 1; t >= 0; t--) {
    if (!count[t]) continue;
    char name[24], s[40], n[16];
    upper(TIER_NAMES[t], name);
    fmt_int((uint64_t)count[t], n);
    snprintf(s, sizeof s, "%s %s", name, n);
    int w = g_text_w(&font_s, s) + 10;
    if (x + w > 250) break;
    card_t c = tier_card(&TS[t], true, x, 22, w, 14, 3, 1);
    card_draw(&c);
    card_text_c(&c, &font_s, x + w / 2, 33, s, TS[t].pill_text);
    x += w + 4;
  }
  char t[24];
  snprintf(t, sizeof t, "+%d new", U.bulk_new);
  g_text(&font_s, 313 - g_text_w(&font_s, t), 33, t, U.bulk_new ? C_NEW : C_FAINT, C_BG);
}

static void bulk_title(char *t) {
  char n[16];
  fmt_int((uint64_t)U.bulk_n, n);
  sprintf(t, "%s ROLLS%s", n, U.bulk_sorted ? "  -  BEST FIRST" : "");
}

static void bulk_footer(void) {
  if (U.bulk_phase == BP_TURN) { footer("Any key: show them all", ""); return; }
  if (!U.folded) { footer("OK: open   EXE: sort", "Back: done"); return; }
  /* rare rolls still folded: EXE unfolds them, rarest last */
  footer("OK: open", "Back: done");
  char t[40];
  snprintf(t, sizeof t, "EXE: UNFOLD %d RARE", U.folded);
  int x = 10 + g_text_w(&font_s, "OK: open") + 12, w = g_text_w(&font_s, t) + 16;
  card_t c;
  memset(&c, 0, sizeof c);
  c.x = x; c.y = 223; c.w = w; c.h = 15; c.r = 4; c.bw = 1;
  c.border[0] = 0xEC4899; c.border[1] = 0xA855F7;
  c.out = P_SURFACE;
  card_draw(&c);
  card_text_c(&c, &font_s, x + w / 2, 234, t, 0xFFFFFF);
}

static void draw_bulk(void) {
  g_fill(0, 20, 320, 200, C_BG);
  char t[48];
  bulk_title(t);
  header(t);
  bulk_strip();
  for (int k = 0; k < GRID_COLS * GRID_ROWS; k++) grid_card(U.bulk_top * GRID_COLS + k);
  bulk_footer();
}

/* ---------------------------------------------------------------- badge book */

static void book_row(int i) {
  int y = 26 + (i - U.book_top) * 15;
  bool sel = i == U.book_sel;
  uint16_t bg = sel ? C_SEL : C_BG;
  g_fill(0, y, 320, 15, bg);
  if (i >= N_BADGES) return;
  long found = U.st.found[i];
  g_text(&font_s, 10, y + 11, BADGES[i].name, found ? C_TEXT : C_FAINT, bg);
  char t[32], n[24];
  fmt_int(badge_ep(i) / 10, n);
  snprintf(t, sizeof t, "1 in %s", n);
  g_text(&font_s, 236 - g_text_w(&font_s, t), y + 11, t, C_DIM, bg);
  fmt_int((uint64_t)found, n);
  g_text(&font_s, 310 - g_text_w(&font_s, n), y + 11, n, found ? C_GOLD : C_FAINT, bg);
}

static void book_detail(void) {
  g_fill(0, 194, 320, 26, C_PANEL);
  const badge_info_t *b = &BADGES[U.book_sel];
  char d[64];
  snprintf(d, sizeof d, "%s", b->desc);
  fit(&font_s, d, 300);
  g_text(&font_s, 10, 206, d, C_TEXT, C_PANEL);
  char t[96], e[40];
  ep_text(badge_ep(U.book_sel), e);
  if (b->family) snprintf(t, sizeof t, "%s  -  %s family, the best one pays", e, FAMILY_NAMES[b->family]);
  else snprintf(t, sizeof t, "%s", e);
  fit(&font_s, t, 300);
  g_text(&font_s, 10, 218, t, C_DIM, C_PANEL);
}

static void draw_book(void) {
  int got = 0;
  for (int i = 0; i < N_BADGES; i++) got += U.st.found[i] > 0;
  char t[40];
  snprintf(t, sizeof t, "BADGE BOOK  %d/%d", got, N_BADGES);
  g_fill(0, 22, 320, 218, C_BG);
  header(t);
  for (int i = U.book_top; i < U.book_top + BOOK_ROWS; i++) book_row(i);
  book_detail();
}

/* ---------------------------------------------------------------- records */

static void draw_records(void) {
  g_fill(0, 22, 320, 198, C_BG);
  header("RECORDS");
  char t[64], n[32];
  fmt_int((uint64_t)U.st.rolls, n);
  snprintf(t, sizeof t, "Rolls made: %s", n);
  g_text(&font_s, 10, 40, t, C_TEXT, C_BG);
  fmt_int(U.st.total_ep, n);
  snprintf(t, sizeof t, "Total: %s EP", n);
  g_text(&font_s, 10, 54, t, C_TEXT, C_BG);
  int got = 0;
  for (int i = 0; i < N_BADGES; i++) got += U.st.found[i] > 0;
  snprintf(t, sizeof t, "Badges found: %d/%d", got, N_BADGES);
  g_text(&font_s, 170, 40, t, C_TEXT, C_BG);
  fmt_int(U.balance, n);
  snprintf(t, sizeof t, "To spend: %s EP", n);
  fit(&font_s, t, 140);
  g_text(&font_s, 310 - g_text_w(&font_s, t), 54, t, C_GOLD, C_BG);
  g_text(&font_s, 10, 72, "Best roll", C_DIM, C_BG);
  if (U.st.rolls) {
    roll_text(&U.st.best.num, t);
    const font_t *f = g_text_w(&font_l, t) <= 300 ? &font_l : &font_s;
    g_text(f, 10, 92, t, tier_c(U.st.best.tier), C_BG);
    char e[40];
    ep_text(U.st.best.ep, e);
    snprintf(t, sizeof t, "%s, %s", TIER_NAMES[U.st.best.tier], e);
    g_text(&font_s, 10, 106, t, C_DIM, C_BG);
  } else g_text(&font_s, 10, 90, "None yet", C_FAINT, C_BG);
  for (int i = 0; i < N_TIERS; i++) {
    int x = i < 8 ? 10 : 166, y = 122 + (i % 8) * 11;
    g_text(&font_s, x, y + 9, TIER_NAMES[i], U.st.tiers[i] ? tier_c(i) : C_FAINT, C_BG);
    fmt_int((uint64_t)U.st.tiers[i], n);
    g_text(&font_s, x + 140 - g_text_w(&font_s, n), y + 9, n, U.st.tiers[i] ? C_TEXT : C_FAINT, C_BG);
  }
  footer("Saved on the calculator", "Back: menu");
}

/* ---------------------------------------------------------------- shop and bag */

/* Glim, RNGidle's orb shopkeeper, floats at the top of the shop and talks (lines from RNGidle's glim-lines.ts) */
enum { GL_GREET, GL_THANKS, GL_BROKE, GL_RESTOCK, GL_SOLD_OUT, GL_MYTH, GL_KINDS };
static const char *const GLIM_LINES[GL_KINDS][2] = {
    [GL_GREET] = {"Oh! A customer! I'm Glim. I float, I sell, I judge your EP. Lovingly.",
                  "Welcome! Mind the glass. I am the glass."},
    [GL_THANKS] = {"Sold! I'll miss that one. Not really.", "Sold! I'll miss that one. Not really."},
    [GL_BROKE] = {"Ha! Come back with more EP, friend.", "I take EP, not hopes and dreams."},
    [GL_RESTOCK] = {"Fresh stock! Straight out of the void.", "Ta-da! All new wares."},
    [GL_SOLD_OUT] = {"All gone! New wares soon, I promise.", "All gone! New wares soon, I promise."},
    [GL_MYTH] = {"I... did not put this on the shelf. Did I?", "I... did not put this on the shelf. Did I?"},
};
#define GLIM_X 4
#define GLIM_Y 20
#define BUBBLE_X 50

static void glim_draw(void) {
  int bob = U.glim_bob, f = U.glim_frame;
  img_draw(GLIM[f], GLIM_X, GLIM_Y + bob, NULL, P_PAGE);
  /* the rows the last position left */
  if (bob > 0) g_fill(GLIM_X, GLIM_Y, GLIM[f]->w, bob, C_BG);
  if (bob < 2) g_fill(GLIM_X, GLIM_Y + bob + GLIM[f]->h, GLIM[f]->w, 2 - bob, C_BG);
}

/* Glim's speech bubble: the line, cut into two rows at the spaces */
static void glim_bubble(void) {
  g_fill(BUBBLE_X - 6, 20, 320 - BUBBLE_X + 6, 46, C_BG);
  card_t c;
  memset(&c, 0, sizeof c);
  c.x = BUBBLE_X; c.y = 24; c.w = 312 - BUBBLE_X; c.h = 36; c.r = 8; c.bw = 1;
  c.fill[0] = c.fill[1] = c.fill[2] = P_RAISED;
  c.border[0] = c.border[1] = 0x7FD6FF;
  c.out = P_PAGE;
  card_draw(&c);
  for (int k = 0; k < 4; k++) g_fill(BUBBLE_X - 4 + k, 40 - k + 1, 1, 2 * k + 1, C_SEL);   /* the tail, to Glim */
  int max = c.w - 16;
  char a[96], b[96] = "";
  uint32_t fg = 0xF0F0F0, fg2 = 0xF0F0F0;
  const char *line = GLIM_LINES[U.glim_kind][U.glim_pick & 1];
  if (U.glim_hold <= 0) {   /* nothing to say: the outlined ware */
    int k = U.shop_sel, id = U.ware[k];
    snprintf(a, sizeof a, "%s", ITEMS[id].desc);
    fit(&font_s, a, max);
    if (U.stock[k]) snprintf(b, sizeof b, "%d left in stock", U.stock[k]);
    else strcpy(b, "Sold out until the next wares");
    fg2 = P_PROSE3;
    line = NULL;
  } else snprintf(a, sizeof a, "%s", line);
  if (line && g_text_w(&font_s, a) > max) {
    int cut = (int)strlen(a);
    while (cut > 0) {
      while (cut > 0 && a[cut] != ' ') cut--;
      a[cut] = 0;
      if (g_text_w(&font_s, a) <= max) break;
      a[cut] = ' ';
      cut--;
    }
    snprintf(b, sizeof b, "%s", line + cut + 1);
    fit(&font_s, b, max);
  }
  card_text(&c, &font_s, c.x + 8, b[0] ? 39 : 45, a, fg);
  if (b[0]) card_text(&c, &font_s, c.x + 8, 52, b, fg2);
}

static void glim_say(int kind) {
  U.glim_kind = kind;
  U.glim_pick++;
  U.glim_talk = 1200;
  U.glim_hold = 3000;
  if (U.screen == S_SHOP && U.shop_tab == 0) glim_bubble();
}

/* tabs and the balance in the panel at the top, and when the wares change */
static void shop_header(void) {
  g_fill(0, 0, 320, 19, C_PANEL);
  g_fill(0, 19, 320, 1, C_LINE);
  static const char *const TAB[2] = {"SHOP", "BAG"};
  int x = 6;
  for (int i = 0; i < 2; i++) {
    int w = g_text_w(&font_s, TAB[i]) + 16;
    card_t c;
    memset(&c, 0, sizeof c);
    c.x = x; c.y = 2; c.w = w; c.h = 15; c.r = 4; c.bw = 1;
    bool on = i == U.shop_tab;
    c.border[0] = on ? 0xEC4899 : P_OUTLINE; c.border[1] = on ? 0xA855F7 : P_OUTLINE;
    c.fill[0] = c.fill[1] = c.fill[2] = on ? P_RAISED : P_SURFACE;
    c.out = P_SURFACE;
    card_draw(&c);
    card_text_c(&c, &font_s, x + w / 2, 13, TAB[i], on ? 0xFFFFFF : P_PROSE3);
    x += w + 4;
  }
  char t[40];
  ep_text(U.balance, t);
  g_text(&font_s, 312 - g_text_w(&font_s, t), 14, t, C_GOLD, C_PANEL);
}

/* "new wares in 4:59" (the shop) or how many items (the bag), between the tabs and the balance */
static void shop_timer(void) {
  char t[40], e[40];
  ep_text(U.balance, e);
  int x0 = 104, x1 = 306 - g_text_w(&font_s, e);
  g_fill(x0, 1, x1 - x0, 17, C_PANEL);
  if (U.shop_tab == 0) {
    int sec = (U.shop_ms + 999) / 1000;
    snprintf(t, sizeof t, "new wares in %d:%02d", sec / 60, sec % 60);
    U.shop_sec = sec;
  } else {
    int n = 0;
    for (int id = 0; id < N_ITEMS; id++) n += U.inv[id];
    snprintf(t, sizeof t, "%d item%s", n, n == 1 ? "" : "s");
  }
  if (g_text_w(&font_s, t) <= x1 - x0) g_text(&font_s, x0, 14, t, C_DIM, C_PANEL);
}

static void shop_footer(void) {
  const char *r = "Back: menu";
  if (U.msg_ms > 0) { footer(U.msg, r); return; }
  footer(U.shop_tab ? "OK: use   Left/Right: shop" : "OK: buy   Left/Right: bag", r);
}

#define WARE_Y 68
#define WARE_H 25

static void ware_row(int k) {
  int y = WARE_Y + k * WARE_H;
  g_fill(0, y - 2, 320, WARE_H, C_BG);
  int id = U.ware[k];
  const tier_style_t *s = &TS[item_tier(id)];
  card_t c = tier_card(s, true, 6, y, 308, WARE_H - 3, 6, 1);
  bool sold = !U.stock[k];
  if (sold) {
    c.fill[0] = c.fill[1] = c.fill[2] = P_SURFACE;
    c.border[0] = c.border[1] = P_OUTLINE;
  }
  card_draw(&c);
  img_draw(ICONS[id], 9, y + 1, &c, 0);
  uint32_t fg = sold ? P_PROSE3 : s->pill_text, dim = rgb_mix(fg, c.fill[0], 150);
  char t[48], pr[40], st[24];
  ep_text(item_price(id), pr);
  int pw = g_text_w(&font_data, pr);
  card_text(&c, &font_data, 307 - pw, y + 16, pr, sold ? P_PROSE3 : U.balance >= item_price(id) ? 0xFCD34D : dim);
  if (sold) strcpy(st, "sold out");
  else snprintf(st, sizeof st, "x%d", U.stock[k]);
  int sw = g_text_w(&font_s, st), sx = 307 - pw - 10 - sw;
  card_text(&c, &font_s, sx, y + 15, st, dim);
  snprintf(t, sizeof t, "%s", ITEMS[id].name);
  fit(&font_m, t, sx - 33 - 6);
  card_text(&c, &font_m, 33, y + 16, t, fg);
  if (k == U.shop_sel) g_frame(3, y - 2, 314, WARE_H - 1, 1, C_TEXT);
}

/* the bag's items: what there is at least one of */
static int bag_ids(int *ids) {
  int n = 0;
  for (int id = 0; id < N_ITEMS; id++)
    if (U.inv[id]) ids[n++] = id;
  return n;
}

#define BAG_Y 22
#define BAG_H 24

static void bag_row(int r, const int *ids, int n) {
  int y = BAG_Y + (r - U.bag_top) * BAG_H;
  bool sel = r == U.bag_sel;
  uint16_t bg = sel ? C_SEL : C_BG;
  g_fill(0, y, 320, BAG_H, bg);
  if (r >= n) return;
  int id = ids[r];
  img_draw(ICONS[id], 8, y + 2, NULL, sel ? P_RAISED : P_PAGE);
  g_text(&font_s, 34, y + 16, ITEMS[id].name, RGB(TS[item_tier(id)].ink), bg);
  char t[16];
  snprintf(t, sizeof t, "x%d", U.inv[id]);
  g_text(&font_s, 310 - g_text_w(&font_s, t), y + 16, t, C_GOLD, bg);
}

static void bag_detail(const int *ids, int n) {
  g_fill(0, 190, 320, 30, C_PANEL);
  char t[64];
  if (U.luck_id >= 0) {
    char l[16];
    fmt_int((uint64_t)U.luck_left, l);
    snprintf(t, sizeof t, "At work: %s, %s rolls left", ITEMS[U.luck_id].name, l);
  } else if (U.armed >= 0) snprintf(t, sizeof t, "At work: %s, on the next roll", ITEMS[U.armed].name);
  else snprintf(t, sizeof t, "No potion at work");
  fit(&font_s, t, 300);
  if (n) {
    char d[64];
    snprintf(d, sizeof d, "%s", ITEMS[ids[U.bag_sel]].desc);
    fit(&font_s, d, 300);
    g_text(&font_s, 10, 202, d, C_TEXT, C_PANEL);
  }
  g_text(&font_s, 10, 215, t, U.luck_id >= 0 || U.armed >= 0 ? C_NEW : C_DIM, C_PANEL);
}

static void draw_bag(void) {
  int ids[N_ITEMS], n = bag_ids(ids);
  if (U.bag_sel >= n) U.bag_sel = n ? n - 1 : 0;
  if (U.bag_sel < U.bag_top) U.bag_top = U.bag_sel;
  if (U.bag_sel >= U.bag_top + BAG_ROWS) U.bag_top = U.bag_sel - BAG_ROWS + 1;
  g_fill(0, 20, 320, 170, C_BG);
  if (!n) {
    g_text_c(&font_s, 160, 100, "The bag is empty.", C_DIM, C_BG);
    g_text_c(&font_s, 160, 116, "Glim's shop sells potions and number items for EP.", C_FAINT, C_BG);
  } else
    for (int r = U.bag_top; r < U.bag_top + BAG_ROWS; r++) bag_row(r, ids, n);
  bag_detail(ids, n);
}

static void draw_shop(void) {
  g_fill(0, 20, 320, 200, C_BG);
  shop_header();
  shop_timer();
  if (U.shop_tab == 0) {
    glim_draw();
    glim_bubble();
    for (int k = 0; k < WARES; k++) ware_row(k);
  } else draw_bag();
  shop_footer();
}

/* Glim floats (2 pixels up and down), blinks now and then and moves its mouth while it talks */
static void glim_tick(int ms) {
  U.glim_ms += ms;
  if (U.glim_talk > 0) U.glim_talk -= ms;
  if (U.glim_hold > 0 && (U.glim_hold -= ms) <= 0 && U.screen == S_SHOP && U.shop_tab == 0) glim_bubble();
  static const int8_t BOB[8] = {0, 0, 1, 2, 2, 2, 1, 0};
  int bob = BOB[(U.glim_ms / 220) % 8];
  int f = U.glim_talk > 0 ? ((U.glim_ms / 150) & 1 ? 2 : 0) : (U.glim_ms % 4200 < 160 ? 1 : 0);
  if (bob == U.glim_bob && f == U.glim_frame) return;
  U.glim_bob = bob;
  U.glim_frame = f;
  if (U.screen == S_SHOP && U.shop_tab == 0) glim_draw();
}

/* ---------------------------------------------------------------- menu and help */

static const char *const MENU[] = {"Roll", "Shop", "Badge book", "Records", "How to play"};
#define MENU_N 5

/* the menu is drawn in parts: a whole-screen repaint each time the bank grows or a row changes flickered */
static void menu_row(int i) {
  int y = 86 + i * 24;
  uint16_t bg = i == U.menu_sel ? C_PANEL : C_BG;
  g_fill(80, y, 160, 21, bg);
  if (i == U.menu_sel) g_fill(80, y, 4, 21, C_GOLD);
  g_text_c(&font_m, 162, y + 16, MENU[i], i == U.menu_sel ? C_TEXT : C_DIM, bg);
}

static void menu_bank(void) {
  g_fill(0, 206, 320, 16, C_BG);
  char t[64], n[16];
  fmt_int((uint64_t)U.bank, n);
  if (U.away_rolls > 0) {
    char a[16];
    fmt_int((uint64_t)U.away_rolls, a);
    snprintf(t, sizeof t, "%s rolls ready (+%s while away)", n, a);
  } else snprintf(t, sizeof t, "%s rolls ready, one more every 2 seconds", n);
  g_text_c(&font_s, 160, 218, t, C_DIM, C_BG);
  U.drawn_bank = (int)U.bank;
}

static void draw_menu(void) {
  g_fill(0, 0, 320, 240, C_BG);
  g_text_c(&font_title, 160, 50, "ROLLMARK", C_GOLD, C_BG);
  g_text_c(&font_s, 160, 72, "Roll a number. Read its luck. Collect badges.", C_DIM, C_BG);
  for (int i = 0; i < MENU_N; i++) menu_row(i);
  menu_bank();
  g_text_c(&font_s, 160, 233, "Inspired by RNGidle", C_FAINT, C_BG);
}

static void draw_help(void) {
  g_fill(0, 0, 320, 240, C_BG);
  g_text_c(&font_m, 160, 18, "How to play", C_TEXT, C_BG);
  static const char *const L[] = {
      "Each roll draws a number from 0 to 999,999.",
      "Now and then it grows more digits, turns",
      "negative (1 in 20) or gets decimals (1 in 100).",
      "",
      "Patterns in it earn badges. A badge pays",
      "10 EP times its odds: 1 in 500 pays 5,000 EP.",
      "In a family (endings, runs, steps, mirrors,",
      "primes, size...) only the best badge pays.",
      "Total EP gives the tier, from Trash to Divine.",
      "",
      "OK rolls. Left/Right or keys 1-4: x1 to x1000.",
      "One roll comes back every 2 s, up to 99,999.",
      "EP buys potions and number items at the shop;",
      "on a roll from the grid, EXE uses a number item.",
  };
  for (int i = 0; i < 14; i++) g_text(&font_s, 12, 36 + i * 13, L[i], C_TEXT, C_BG);
  footer("Rollmark, inspired by RNGidle", "Back: menu");
}

static void redraw(void) {
  switch (U.screen) {
    case S_MENU: draw_menu(); break;
    case S_ROLL: draw_roll(); break;
    case S_BULK: draw_bulk(); break;
    case S_BOOK: draw_book(); break;
    case S_RECORDS: draw_records(); break;
    case S_SHOP: draw_shop(); break;
    default: draw_help(); break;
  }
}

/* ---------------------------------------------------------------- rolling */

static void say(const char *m) {
  snprintf(U.msg, sizeof U.msg, "%s", m);
  U.msg_ms = 1800;
  if (U.screen == S_ROLL) roll_footer();
  if (U.screen == S_SHOP) shop_footer();
}

/* one draw with the potions at work: a rarity potion's roll, or luck's best of a few draws (RNGidle: x1.7 is one
 * more draw 70% of the time). *seed draws r again. */
static void draw_one(roll_t *r, uint32_t *seed) {
  if (U.armed >= 0) {
    uint32_t pick = rnd(), keep = U.seed;
    *seed = U.seed = item_pool_seed(U.armed, pick);
    roll_draw(&r->num, rnd);
    roll_score(r);
    U.seed = keep;
    U.armed = -1;
    return;
  }
  *seed = U.seed;
  roll_draw(&r->num, rnd);
  roll_score(r);
  if (U.luck_id < 0) return;
  int m10 = ITEMS[U.luck_id].luck10, extra = m10 / 10 - 1 + ((int)(rnd() % 10) < m10 % 10);
  for (int k = 0; k < extra; k++) {
    static roll_t o;
    uint32_t at = U.seed;
    roll_draw(&o.num, rnd);
    roll_score(&o);
    if (o.ep > r->ep) { *r = o; *seed = at; }
  }
  if (--U.luck_left <= 0) { U.luck_id = -1; U.luck_left = 0; }
}

/* one roll into the stats; returns the bits of r's badges found for the first time */
static int record(roll_t *r, uint32_t *fresh, uint32_t *seed) {
  draw_one(r, seed);
  int count = 0;
  memset(fresh, 0, MAX_EARNED / 8);
  for (int i = 0; i < r->nb; i++)
    if (U.st.found[r->badges[i]]++ == 0) { SETBIT(fresh, i); count++; }
  U.st.rolls++;
  U.st.total_ep = add_sat(U.st.total_ep, r->ep);
  U.balance = add_sat(U.balance, r->ep);
  U.st.tiers[r->tier]++;
  if (U.st.rolls == 1 || r->ep > U.st.best.ep) U.st.best = *r;
  return count;
}

static void roll_now(void) {
  int want = SIZES[U.size];
  if (U.bank <= 0) { say("Out of rolls: +1 every 2 s"); return; }
  int n = U.bank < want ? (int)U.bank : want;
  U.bank -= n;
  U.away_rolls = 0;
  U.from_bulk = false;
  U.scroll = 0;
  if (n == 1) {
    U.bulk_phase = BP_NONE;
    U.roll_mode = RM_NORMAL;
    uint32_t seed;
    record(&U.shown, U.fresh, &seed);
    U.shown_bulk = -1;
    U.shown_mod = false;
    sort_shown();
    U.has_shown = true;
    U.anim_len = U.shown.tier >= T_MYTHIC && U.shown.tier <= T_TRANSCENDENT ? 1500 : 650;
    U.anim_ms = 0;
    U.screen = S_ROLL;
    draw_roll();
    ui_save();
    return;
  }
  memset(U.bulk_tiers, 0, sizeof U.bulk_tiers);
  U.bulk_new = 0;
  U.bulk_n = n;
  U.bulk_sel = U.bulk_top = 0;
  U.bulk_sorted = false;
  U.bulk_shown = U.bulk_ms = 0;
  U.bulk_step = n <= 10 ? 70 : 30;   /* the first page of cards turns over one by one */
  roll_t r;
  for (int i = 0; i < n; i++) {
    uint32_t fr[MAX_EARNED / 32];
    U.bulk_new += record(&r, fr, &U.bulk_seed[i]);
    U.bulk_ep[i] = r.ep;
    U.bulk_tier[i] = r.tier;
    U.bulk_tiers[r.tier]++;
  }
  /* the rare rolls (the best FEATURE_MAX at FEATURE_TIER or better, else the best roll) unfold later */
  U.feat_n = 0;
  for (int i = 0; i < n; i++) {
    if (U.bulk_tier[i] < FEATURE_TIER) continue;
    int k = U.feat_n < FEATURE_MAX ? U.feat_n++ : FEATURE_MAX;
    if (k == FEATURE_MAX && U.bulk_ep[i] <= U.bulk_ep[U.feat[0]]) continue;
    if (k == FEATURE_MAX) {   /* drop the poorest (first) */
      memmove(U.feat, U.feat + 1, (FEATURE_MAX - 1) * sizeof U.feat[0]);
      k = FEATURE_MAX - 1;
    }
    while (k > 0 && U.bulk_ep[U.feat[k - 1]] > U.bulk_ep[i]) { U.feat[k] = U.feat[k - 1]; k--; }   /* rarest last */
    U.feat[k] = (uint16_t)i;
  }
  if (!U.feat_n) {
    int b = 0;
    for (int i = 1; i < n; i++)
      if (U.bulk_ep[i] > U.bulk_ep[b]) b = i;
    if (U.bulk_ep[b] > 0) U.feat[U.feat_n++] = (uint16_t)b;
  }
  memset(U.bulk_fold, 0, sizeof U.bulk_fold);
  memset(U.bulk_mod, 0, sizeof U.bulk_mod);
  U.ov_n = 0;
  for (int k = 0; k < U.feat_n; k++) U.bulk_fold[U.feat[k] >> 3] |= (uint8_t)(1 << (U.feat[k] & 7));
  U.folded = U.feat_n;
  U.bulk_last = -1;
  for (int i = 0; i < n; i++) U.bulk_order[i] = (uint16_t)i;
  U.bulk_phase = BP_TURN;
  U.roll_mode = RM_NORMAL;
  U.screen = S_BULK;
  draw_bulk();
  ui_save();
}

/* ---------------------------------------------------------------- items */

/* a number item on the roll on view: its number changes and the EP it makes or loses counts (the rolls made stay
 * the same; badges found only go up). One item per number, as in RNGidle. */
static bool use_number(int id) {
  if (!U.has_shown) { say("Roll a number first"); return false; }
  if (U.shown_mod) { say("One item per number: this one is done"); return false; }
  if (U.shown_bulk >= 0 && U.ov_n >= MAX_OV) { say("Too many changed rolls in this batch"); return false; }
  static roll_t r;
  r = U.shown;
  const char *why;
  if (!item_use(id, &r, rnd, &why)) { say(why); return false; }
  const roll_t *o = &U.shown;
  U.inv[id]--;
  U.st.total_ep = add_sat(U.st.total_ep >= o->ep ? U.st.total_ep - o->ep : 0, r.ep);
  U.balance = r.ep >= o->ep ? add_sat(U.balance, r.ep - o->ep) : U.balance - (U.balance < o->ep - r.ep ? U.balance : o->ep - r.ep);
  if (U.st.tiers[o->tier] > 0) U.st.tiers[o->tier]--;
  U.st.tiers[r.tier]++;
  memset(U.fresh, 0, sizeof U.fresh);
  int fresh = 0;
  for (int i = 0; i < r.nb; i++) {
    bool had = false;
    for (int j = 0; j < o->nb && !had; j++) had = o->badges[j] == r.badges[i];
    if (!had && U.st.found[r.badges[i]]++ == 0) { SETBIT(U.fresh, i); fresh++; }
  }
  if (r.ep > U.st.best.ep) U.st.best = r;
  int i = U.shown_bulk;
  if (i >= 0) {   /* the grid shows the new number */
    U.ov[U.ov_n].i = (uint16_t)i;
    U.ov[U.ov_n++].n = r.num;
    U.bulk_mod[i >> 3] |= (uint8_t)(1 << (i & 7));
    if (U.bulk_tiers[o->tier] > 0) U.bulk_tiers[o->tier]--;
    U.bulk_tiers[r.tier]++;
    U.bulk_ep[i] = r.ep;
    U.bulk_tier[i] = (uint8_t)r.tier;
    U.bulk_new += fresh;
  }
  char m[48], d[24];
  uint64_t was = o->ep;
  U.shown = r;
  U.shown_mod = true;
  sort_shown();
  U.scroll = 0;
  U.anim_len = r.tier >= T_MYTHIC && r.tier <= T_TRANSCENDENT ? 1500 : 650;
  U.anim_ms = 0;
  U.screen = S_ROLL;
  draw_roll();
  fmt_int(r.ep >= was ? r.ep - was : was - r.ep, d);
  snprintf(m, sizeof m, "%s: %s%s EP", ITEMS[id].name, r.ep >= was ? "+" : "-", d);
  say(m);
  ui_save();
  return true;
}

/* a potion from the bag: luck works on the next rolls, a rarity potion on the next one */
static void use_item(int id) {
  char m[48];
  if (ITEMS[id].kind == K_NUMBER) { use_number(id); return; }
  if (ITEMS[id].kind == K_LUCK) {
    if (U.luck_id >= 0 && U.luck_id != id) { say("Another luck potion is at work"); return; }
    U.luck_left += (long)ITEMS[id].cap;
    U.luck_id = id;
    int m10 = ITEMS[id].luck10;
    char n[16];
    fmt_int((uint64_t)U.luck_left, n);
    snprintf(m, sizeof m, "x%d.%d luck for %s rolls", m10 / 10, m10 % 10, n);
  } else {
    if (U.armed >= 0) { say("A potion waits for the next roll already"); return; }
    U.armed = id;
    snprintf(m, sizeof m, "Next roll: %s or better", TIER_NAMES[ITEMS[id].tier]);
  }
  U.inv[id]--;
  draw_shop();
  say(m);
  ui_save();
}

static void buy(int k) {
  int id = U.ware[k];
  if (!U.stock[k]) { glim_say(GL_SOLD_OUT); return; }
  if (U.balance < item_price(id)) { glim_say(GL_BROKE); return; }
  if (U.inv[id] >= 9999) { say("The bag holds 9,999 of each"); return; }
  U.balance -= item_price(id);
  U.stock[k]--;
  U.inv[id]++;
  shop_header();
  shop_timer();
  for (int j = 0; j < WARES; j++) ware_row(j);   /* the prices turn gold or grey with the balance */
  glim_say(GL_THANKS);
  char m[48];
  snprintf(m, sizeof m, "Bought: %s", ITEMS[id].name);
  say(m);
  ui_save();
}

/* new wares: 6 different items, the cheaper ones more often (weight price^-0.35, as Glim does) */
static void restock(void) {
  U.shop_slot++;
  U.shop_ms = RESTOCK_MS;
  bool taken[N_ITEMS] = {0};
  float w[N_ITEMS];
  for (int id = 0; id < N_ITEMS; id++) w[id] = powf((float)item_price(id), -0.35f);
  for (int k = 0; k < WARES; k++) {
    float total = 0;
    for (int id = 0; id < N_ITEMS; id++)
      if (!taken[id]) total += w[id];
    float x = (float)(rnd() >> 8) / 16777216.0f * total;
    int pick = -1;
    for (int id = 0; id < N_ITEMS; id++) {
      if (taken[id]) continue;
      pick = id;
      if ((x -= w[id]) < 0) break;
    }
    taken[pick] = true;
    U.ware[k] = (uint8_t)pick;
  }
  for (int a = 1; a < WARES; a++)   /* cheapest first */
    for (int b = a; b > 0 && item_price(U.ware[b]) < item_price(U.ware[b - 1]); b--) {
      uint8_t t = U.ware[b];
      U.ware[b] = U.ware[b - 1];
      U.ware[b - 1] = t;
    }
  for (int k = 0; k < WARES; k++) {
    const item_info_t *it = &ITEMS[U.ware[k]];
    U.stock[k] = (uint16_t)(it->stock_min + rnd() % (uint32_t)(it->stock_max - it->stock_min + 1));
  }
}

/* ---------------------------------------------------------------- saving */

#define SAVE_NAME "rollmark.sav"
#define SAVE_SIZE 3072
#define SAVE_MAGIC 0x314B4D52u   /* "RMK1" */
#define MAX_SAVED_BADGES 400   /* room for more badges; the rest of the file is kept for later things */
#define SHOP_MAGIC 0x504F4853u  /* "SHOP": the save has the shop's part */
#define SAVED_ITEMS 32          /* room for more items (ids only grow) */
_Static_assert(N_ITEMS <= SAVED_ITEMS, "more items than the save has room for");

/* badges are saved by a number made from their name, so adding or reordering badges keeps the counts */
static uint16_t badge_key(int b) {
  uint32_t h = 2166136261u;
  for (const char *p = BADGES[b].name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return (uint16_t)(h ^ (h >> 16));
}

typedef struct {
  uint32_t magic;
  int32_t bank, refill_ms, size;
  uint64_t clock;                 /* the calculator's clock at the save */
  int32_t rolls;
  uint64_t total_ep;
  int32_t tiers[N_TIERS];
  char best_int[MAX_DIGITS + 1], best_frac[MAX_FRAC + 1];
  uint8_t best_neg;
  uint16_t nfound;
  uint16_t key[MAX_SAVED_BADGES];
  uint32_t count[MAX_SAVED_BADGES];
  /* the shop (an older save has zeros here) */
  uint32_t shop_magic;
  uint64_t balance;
  uint16_t inv[SAVED_ITEMS];
  int32_t luck_id, luck_left, armed;
  uint32_t shop_slot;
  int32_t shop_ms;
  uint8_t ware[WARES];
  uint16_t stock[WARES];
} save_t;

static save_t SV;   /* static: too big for the stack */
_Static_assert(sizeof(save_t) + 12 <= SAVE_SIZE, "the save no longer fits its file");

static bool all_digits(const char *s, int most) {
  int n = 0;
  for (; s[n]; n++)
    if (n >= most || s[n] < '0' || s[n] > '9') return false;
  return true;
}

void ui_save(void) {
  memset(&SV, 0, sizeof SV);
  SV.magic = SAVE_MAGIC;
  SV.bank = (int32_t)U.bank;
  SV.refill_ms = U.refill_ms;
  SV.size = U.size;
  SV.clock = U.clock;
  SV.rolls = (int32_t)U.st.rolls;
  SV.total_ep = U.st.total_ep;
  for (int t = 0; t < N_TIERS; t++) SV.tiers[t] = (int32_t)U.st.tiers[t];
  memcpy(SV.best_int, U.st.best.num.digits, sizeof SV.best_int);
  memcpy(SV.best_frac, U.st.best.num.frac, sizeof SV.best_frac);
  SV.best_neg = U.st.best.num.neg;
  for (int b = 0; b < N_BADGES && SV.nfound < MAX_SAVED_BADGES; b++)
    if (U.st.found[b]) {
      SV.key[SV.nfound] = badge_key(b);
      SV.count[SV.nfound++] = (uint32_t)U.st.found[b];
    }
  SV.shop_magic = SHOP_MAGIC;
  SV.balance = U.balance;
  for (int id = 0; id < N_ITEMS; id++) SV.inv[id] = U.inv[id];
  SV.luck_id = U.luck_id;
  SV.luck_left = (int32_t)U.luck_left;
  SV.armed = U.armed;
  SV.shop_slot = U.shop_slot;
  SV.shop_ms = U.shop_ms;
  memcpy(SV.ware, U.ware, sizeof SV.ware);
  memcpy(SV.stock, U.stock, sizeof SV.stock);
  save_store(SAVE_NAME, SAVE_SIZE, &SV, sizeof SV);
  U.save_ms = 0;
}

static bool load(void) {
  if (!save_load(SAVE_NAME, SAVE_SIZE, &SV, sizeof SV) || SV.magic != SAVE_MAGIC) return false;
  U.bank = SV.bank < 0 ? 0 : SV.bank > MAX_BANK ? MAX_BANK : SV.bank;
  U.refill_ms = SV.refill_ms >= 0 && SV.refill_ms < REFILL_MS ? SV.refill_ms : 0;
  U.size = SV.size >= 0 && SV.size < 4 ? SV.size : 0;
  U.st.rolls = SV.rolls;
  U.st.total_ep = SV.total_ep;
  for (int t = 0; t < N_TIERS; t++) U.st.tiers[t] = SV.tiers[t];
  for (int i = 0; i < SV.nfound && i < MAX_SAVED_BADGES; i++)
    for (int b = 0; b < N_BADGES; b++)
      if (badge_key(b) == SV.key[i]) { U.st.found[b] = SV.count[i]; break; }
  SV.best_int[MAX_DIGITS] = SV.best_frac[MAX_FRAC] = 0;
  if (U.st.rolls > 0 && SV.best_int[0] && all_digits(SV.best_int, MAX_DIGITS) && all_digits(SV.best_frac, MAX_FRAC)) {
    /* the best roll is scored again, so new badges and prices apply to it too */
    roll_from_text(&U.st.best.num, SV.best_int);
    U.st.best.num.fn = (uint8_t)strlen(SV.best_frac);
    memcpy(U.st.best.num.frac, SV.best_frac, sizeof SV.best_frac);
    U.st.best.num.neg = SV.best_neg != 0;
    roll_score(&U.st.best);
  } else U.st.rolls = 0;
  if (SV.shop_magic == SHOP_MAGIC) {
    U.balance = SV.balance;
    for (int id = 0; id < N_ITEMS; id++) U.inv[id] = SV.inv[id] > 9999 ? 9999 : SV.inv[id];
    bool luck = SV.luck_id >= IT_LUCK1 && SV.luck_id <= IT_LUCK10 && SV.luck_left > 0;
    U.luck_id = luck ? SV.luck_id : -1;
    U.luck_left = luck ? SV.luck_left : 0;
    U.armed = SV.armed >= IT_MYTHIC && SV.armed <= IT_OTHERWORLDLY ? SV.armed : -1;
    U.shop_slot = SV.shop_slot;
    U.shop_ms = SV.shop_ms > 0 && SV.shop_ms <= RESTOCK_MS ? SV.shop_ms : 0;
    for (int k = 0; k < WARES; k++) {
      U.ware[k] = SV.ware[k] < N_ITEMS ? SV.ware[k] : 0;
      U.stock[k] = SV.stock[k] <= ITEMS[U.ware[k]].stock_max ? SV.stock[k] : 0;
    }
  } else U.balance = U.st.total_ep;   /* a save from before the shop: every EP made so far can be spent */
  /* rolls earned while away: the clock runs as long as the calculator has not restarted */
  if (U.clock > SV.clock) {
    U.shop_ms -= U.clock - SV.clock < RESTOCK_MS ? (int32_t)(U.clock - SV.clock) : RESTOCK_MS;
    uint64_t away = U.clock - SV.clock + (uint64_t)U.refill_ms;
    uint64_t back = away / REFILL_MS;
    U.bank = U.bank + (long)back > MAX_BANK ? MAX_BANK : U.bank + (long)back;
    U.refill_ms = (int)(away % REFILL_MS);
    U.away_rolls = (long)back;
  }
  return true;
}

bool ui_loaded(void) { return U.loaded; }

void ui_init(uint32_t seed, uint64_t now_ms) {
  memset(&U, 0, sizeof U);
  U.seed = seed ? seed : 0x2545F491u;
  U.clock = now_ms;
  U.bank = START_ROLLS;
  U.rep_key = -1;
  U.luck_id = U.armed = U.shown_bulk = -1;
  U.num_sel = IT_ROULETTE;
  U.loaded = load();
  if (!U.shop_slot || U.shop_ms <= 0) restock();
  U.screen = S_MENU;
  redraw();
}

/* ---------------------------------------------------------------- input */

#define DOWN(m, k) (((m) >> (k)) & 1)

static void open_shown(const roll_t *r) {
  U.shown = *r;
  sort_shown();
  U.has_shown = true;
  memset(U.fresh, 0, sizeof U.fresh);
  U.anim_len = 0;
  U.scroll = 0;
  U.screen = S_ROLL;
  draw_roll();
}

static void open_bulk(int i) {
  roll_t r;
  bulk_number(i, &r.num);
  roll_score(&r);
  U.bulk_last = i;
  U.shown_bulk = i;
  U.shown_mod = MODDED(i);
  open_shown(&r);
}

/* every card of the grid is on screen */
static void bulk_turned(void) {
  U.bulk_shown = U.bulk_n;
  U.bulk_phase = BP_GRID;
  draw_bulk();
}

static void show_sel(void) {
  int row = U.bulk_sel / GRID_COLS;
  if (row < U.bulk_top) U.bulk_top = row;
  if (row >= U.bulk_top + GRID_ROWS) U.bulk_top = row - GRID_ROWS + 1;
}

/* a rare roll leaves its "?" card with its full reveal on the roll view */
static void unfold(int i) {
  roll_t r;
  bulk_number(i, &r.num);
  roll_score(&r);
  U.bulk_fold[i >> 3] &= (uint8_t)~(1 << (i & 7));
  U.folded--;
  U.bulk_last = i;
  U.shown_bulk = i;
  U.shown_mod = MODDED(i);
  U.shown = r;
  sort_shown();
  memset(U.fresh, 0, sizeof U.fresh);
  U.has_shown = true;
  U.scroll = 0;
  U.anim_len = r.tier >= T_MYTHIC && r.tier <= T_TRANSCENDENT ? 1500 : 650;
  U.anim_ms = 0;
  U.roll_mode = U.folded > 0 ? RM_UNFOLD : RM_NORMAL;   /* NEXT while some are left */
  U.from_bulk = true;
  U.screen = S_ROLL;
  draw_roll();
}

/* the next rare roll to unfold: rarest last */
static int next_folded(void) {
  for (int k = 0; k < U.feat_n; k++)
    if (FOLDED(U.feat[k])) return U.feat[k];
  return -1;
}

/* back to the grid, on the roll just shown */
static void to_grid(void) {
  U.roll_mode = RM_NORMAL;
  for (int k = 0; k < U.bulk_n; k++)
    if (U.bulk_order[k] == U.bulk_last) { U.bulk_sel = k; break; }
  show_sel();
  U.screen = S_BULK;
  draw_bulk();
}

static void bulk_sort(bool by_ep) {
  U.bulk_sorted = by_ep;
  for (int i = 0; i < U.bulk_n; i++) U.bulk_order[i] = (uint16_t)i;
  if (by_ep)   /* insertion sort, richest first; equal ones keep their order */
    for (int i = 1; i < U.bulk_n; i++) {
      uint16_t k = U.bulk_order[i];
      int j = i;
      while (j > 0 && U.bulk_ep[U.bulk_order[j - 1]] < U.bulk_ep[k]) { U.bulk_order[j] = U.bulk_order[j - 1]; j--; }
      U.bulk_order[j] = k;
    }
  U.bulk_sel = U.bulk_top = 0;
  draw_bulk();
}

static void bulk_move(int to) {
  if (to < 0 || to >= U.bulk_n || to == U.bulk_sel) return;
  int old = U.bulk_sel, top = U.bulk_top;
  U.bulk_sel = to;
  show_sel();
  if (U.bulk_top != top) {
    for (int k = 0; k < GRID_COLS * GRID_ROWS; k++) grid_card(U.bulk_top * GRID_COLS + k);
  } else {
    card_ring(old, C_BG);
    card_ring(to, C_TEXT);
  }
}

static int bulk_best(void) {
  int b = 0;
  for (int i = 1; i < U.bulk_n; i++)
    if (U.bulk_ep[i] > U.bulk_ep[b]) b = i;
  return b;
}

static bool handle(uint64_t p) {
  bool ok = DOWN(p, eadk_key_ok) || DOWN(p, eadk_key_exe), back = DOWN(p, eadk_key_back);
  switch (U.screen) {
    case S_MENU:
      if (back) { ui_save(); return false; }
      if (DOWN(p, eadk_key_up) || DOWN(p, eadk_key_down)) {
        int old = U.menu_sel;
        U.menu_sel = (U.menu_sel + (DOWN(p, eadk_key_up) ? MENU_N - 1 : 1)) % MENU_N;
        menu_row(old);
        menu_row(U.menu_sel);
      }
      if (ok) {
        static const int TO[] = {S_ROLL, S_SHOP, S_BOOK, S_RECORDS, S_HELP};
        U.screen = TO[U.menu_sel];
        if (U.screen == S_SHOP) { U.glim_kind = GL_GREET; U.glim_pick++; U.glim_talk = 1500; U.glim_hold = 3500; }
        redraw();
      }
      break;
    case S_ROLL:
      /* the amount: Left / Right, or 1 to 4 straight to x1 x10 x100 x1000; the button follows at once */
      if (U.roll_mode != RM_NORMAL && (DOWN(p, eadk_key_left) || DOWN(p, eadk_key_right))) {   /* the number item */
        num_step(DOWN(p, eadk_key_left) ? -1 : 1);
        U.msg_ms = 0;
        roll_footer();
      }
      if (U.roll_mode == RM_NORMAL) {
        int size = U.size;
        if (DOWN(p, eadk_key_left) && size > 0) size--;
        if (DOWN(p, eadk_key_right) && size < 3) size++;
        if (DOWN(p, eadk_key_one)) size = 0;
        if (DOWN(p, eadk_key_two)) size = 1;
        if (DOWN(p, eadk_key_three)) size = 2;
        if (DOWN(p, eadk_key_four)) size = 3;
        if (size != U.size) {
          U.size = size;
          U.msg_ms = 0;
          if (!U.anim_len) roll_button();
          roll_footer();
        }
      }
      if (U.anim_len) break;
      if (U.has_shown) {
        int rows = (U.shown.nb + 1) / 2, most = rows > VIS_BADGES / 2 ? rows - VIS_BADGES / 2 : 0;
        if (DOWN(p, eadk_key_up) && U.scroll > 0) { U.scroll--; badges_band(); }
        if (DOWN(p, eadk_key_down) && U.scroll < most) { U.scroll++; badges_band(); }
      }
      if (U.roll_mode != RM_NORMAL && DOWN(p, eadk_key_exe) && num_item() >= 0) { use_number(num_item()); break; }
      if (U.roll_mode == RM_UNFOLD) {   /* OK: NEXT; Back (or EXE without number items): the grid */
        if (DOWN(p, eadk_key_ok)) unfold(next_folded());
        else if (back || DOWN(p, eadk_key_exe)) to_grid();
        break;
      }
      if (U.roll_mode == RM_PEEK) {     /* BACK TO THE BULK ROLL */
        if (ok || back) to_grid();
        break;
      }
      if (back && U.from_bulk && U.bulk_phase == BP_GRID) { to_grid(); break; }
      if (back) { U.screen = S_MENU; redraw(); break; }
      if (DOWN(p, eadk_key_exe) && U.bulk_phase == BP_GRID) { to_grid(); break; }
      if (ok) { roll_now(); break; }
      break;
    case S_BULK:
      if (U.bulk_phase == BP_TURN) { if (p) bulk_turned(); break; }   /* any key: show them all now */
      if (back) {   /* done: whatever is still folded is kept without its reveal */
        memset(U.bulk_fold, 0, sizeof U.bulk_fold);
        U.folded = 0;
        U.roll_mode = RM_NORMAL;
        U.from_bulk = false;
        open_bulk(bulk_best());
        break;
      }
      if (DOWN(p, eadk_key_ok)) {   /* the outlined card: a folded one unfolds, the others open */
        int i = U.bulk_order[U.bulk_sel];
        if (FOLDED(i)) unfold(i);
        else { U.roll_mode = RM_PEEK; U.from_bulk = true; open_bulk(i); }
        break;
      }
      if (DOWN(p, eadk_key_exe)) {
        if (U.folded) unfold(next_folded());
        else bulk_sort(!U.bulk_sorted);
        break;
      }
      if (DOWN(p, eadk_key_left)) bulk_move(U.bulk_sel - 1);
      if (DOWN(p, eadk_key_right)) bulk_move(U.bulk_sel + 1);
      if (DOWN(p, eadk_key_up)) bulk_move(U.bulk_sel - GRID_COLS);
      if (DOWN(p, eadk_key_down) && U.bulk_sel / GRID_COLS < (U.bulk_n - 1) / GRID_COLS)
        bulk_move(U.bulk_sel + GRID_COLS < U.bulk_n ? U.bulk_sel + GRID_COLS : U.bulk_n - 1);
      break;
    case S_BOOK: {
      if (back || ok) { U.screen = S_MENU; redraw(); break; }
      int old = U.book_sel, old_top = U.book_top;
      if (DOWN(p, eadk_key_up) && U.book_sel > 0) U.book_sel--;
      if (DOWN(p, eadk_key_down) && U.book_sel < N_BADGES - 1) U.book_sel++;
      if (DOWN(p, eadk_key_left)) U.book_sel = U.book_sel >= BOOK_ROWS ? U.book_sel - BOOK_ROWS : 0;
      if (DOWN(p, eadk_key_right)) U.book_sel = U.book_sel + BOOK_ROWS < N_BADGES ? U.book_sel + BOOK_ROWS : N_BADGES - 1;
      if (U.book_sel < U.book_top) U.book_top = U.book_sel;
      if (U.book_sel >= U.book_top + BOOK_ROWS) U.book_top = U.book_sel - BOOK_ROWS + 1;
      if (U.book_top != old_top) draw_book();
      else if (old != U.book_sel) { book_row(old); book_row(U.book_sel); book_detail(); }
      break;
    }
    case S_SHOP: {
      if (back) { U.msg_ms = 0; U.screen = S_MENU; redraw(); break; }
      if (DOWN(p, eadk_key_left) || DOWN(p, eadk_key_right)) { U.shop_tab ^= 1; U.msg_ms = 0; draw_shop(); break; }
      if (U.shop_tab == 0) {
        int old = U.shop_sel;
        if (DOWN(p, eadk_key_up) && U.shop_sel > 0) U.shop_sel--;
        if (DOWN(p, eadk_key_down) && U.shop_sel < WARES - 1) U.shop_sel++;
        if (old != U.shop_sel) {
          ware_row(old);
          ware_row(U.shop_sel);
          if (U.stock[U.shop_sel] && item_tier(U.ware[U.shop_sel]) >= T_MYTHIC) glim_say(GL_MYTH);
          else if (U.glim_hold <= 0) glim_bubble();
        }
        if (ok) buy(U.shop_sel);
      } else {
        int ids[N_ITEMS], n = bag_ids(ids), old = U.bag_sel;
        if (DOWN(p, eadk_key_up) && U.bag_sel > 0) U.bag_sel--;
        if (DOWN(p, eadk_key_down) && U.bag_sel < n - 1) U.bag_sel++;
        if (old != U.bag_sel) draw_bag();
        if (ok && n) use_item(ids[U.bag_sel]);
      }
      break;
    }
    default:
      if (back || ok) { U.screen = S_MENU; redraw(); }
      break;
  }
  return true;
}

bool ui_frame(int ms, uint64_t hit, uint64_t held) {
  uint64_t press = hit;
  if (hit & 0xF) {
    U.rep_key = __builtin_ctzll(hit & 0xF);
    U.rep_ms = 0;
  } else if (U.rep_key >= 0 && DOWN(held, U.rep_key)) {
    U.rep_ms += ms;
    if (U.rep_ms >= 380) { press |= 1ull << U.rep_key; U.rep_ms -= 80; }
  } else U.rep_key = -1;
  /* holding OK on the roll view keeps rolling, a few a second */
  if (U.screen == S_ROLL && !U.anim_len && !hit && DOWN(held, eadk_key_ok)) {
    if ((U.tick += ms) >= 700) { U.tick = 0; press |= 1ull << eadk_key_ok; }
  } else if (hit) U.tick = 0;

  U.clock += (uint64_t)ms;
  if ((U.save_ms += ms) >= 10000) ui_save();   /* keeps the roll bank's refill */
  U.refill_ms += ms;
  while (U.refill_ms >= REFILL_MS) {
    U.refill_ms -= REFILL_MS;
    if (U.bank < MAX_BANK) U.bank++;
  }
  if (U.bank != U.drawn_bank) {
    if (U.screen == S_ROLL && !U.anim_len && (U.bank == 0) != (U.drawn_bank == 0)) roll_button();   /* OUT OF ROLLS <-> GENERATE */
    if (U.screen == S_MENU) menu_bank();
    else if (U.screen != S_HELP && U.screen != S_SHOP) {
      char t[48];
      if (U.screen == S_BULK) bulk_title(t);
      else if (U.screen == S_ROLL) roll_title(t);
      else snprintf(t, sizeof t, "%s", U.screen == S_RECORDS ? "RECORDS" : "BADGE BOOK");
      if (U.screen == S_BOOK) {
        int got = 0;
        for (int i = 0; i < N_BADGES; i++) got += U.st.found[i] > 0;
        snprintf(t, sizeof t, "BADGE BOOK  %d/%d", got, N_BADGES);
      }
      header(t);
    }
  }

  if (U.anim_len) {
    U.anim_ms += ms;
    if (U.anim_ms >= U.anim_len) {
      U.anim_len = 0;
      draw_roll();
    } else number_band();
  }
  if (U.screen == S_BULK && U.bulk_phase == BP_TURN) {   /* the first page of cards turns over one by one */
    U.bulk_ms += ms;
    while (U.bulk_ms >= U.bulk_step && U.bulk_shown < U.bulk_n) {
      U.bulk_ms -= U.bulk_step;
      grid_card(U.bulk_shown++);
      if (U.bulk_shown >= GRID_COLS * GRID_ROWS) U.bulk_shown = U.bulk_n;   /* the rest are off screen */
    }
    if (U.bulk_shown >= U.bulk_n) bulk_turned();
  }
  if ((U.shop_ms -= ms) <= 0) {
    restock();
    if (U.screen == S_SHOP) { U.glim_kind = GL_RESTOCK; U.glim_pick++; U.glim_talk = 1200; U.glim_hold = 3000; draw_shop(); }
  } else if (U.screen == S_SHOP && U.shop_tab == 0 && (U.shop_ms + 999) / 1000 != U.shop_sec) shop_timer();
  if (U.screen == S_SHOP) glim_tick(ms);
  if (U.msg_ms > 0 && (U.msg_ms -= ms) <= 0) {
    if (U.screen == S_ROLL) roll_footer();
    if (U.screen == S_SHOP) shop_footer();
  }
  return handle(press);
}
