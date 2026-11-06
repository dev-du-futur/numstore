/* TinyChat: a chat screen for a tiny language model that runs on the calculator.
 * EXE / OK: send   Backspace: delete   Clear (shift+backspace): new chat
 * Back: stop the reply / quit   Up / Down: scroll */
#include <eadk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nwai.h"
#include "helpers.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "TinyChat";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define W EADK_SCREEN_WIDTH
#define H EADK_SCREEN_HEIGHT
#define CW 7          /* small font cell */
#define CH 14
#define LW 10         /* large font cell */
#define HEADER_H 34
#define INPUT_H 30
#define AREA_TOP HEADER_H
#define AREA_BOT (H - INPUT_H)
#define PADX 7
#define PADY 4
#define GAP 6
#define RADIUS 7
#define BOT_X 26                       /* bot bubbles start here (avatar on the left) */
#define BOT_MAXW (W - BOT_X - 30)
#define USER_MAXW (W - 60)
#define MAXIN 120
#define REPLY_MAX 80                   /* tokens per reply */
#define CTX 256

#define RGB(r, g, b) ((eadk_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const eadk_color_t
    C_BG = RGB(242, 243, 247),        /* chat background */
    C_HDR = RGB(30, 35, 52),          /* flat: text cells are drawn on a solid colour */
    C_HDR_LINE = RGB(92, 108, 255),
    C_TITLE = RGB(255, 255, 255), C_SUB = RGB(150, 158, 182),
    C_PILL = RGB(55, 63, 88),
    C_ACCENT = RGB(79, 91, 213),      /* user bubble, send button, avatar */
    C_ACCENT2 = RGB(124, 92, 230),
    C_USER_TXT = RGB(255, 255, 255),
    C_BOT_BG = RGB(255, 255, 255), C_BOT_TXT = RGB(31, 36, 48), C_BOT_EDGE = RGB(222, 225, 234),
    C_INFO = RGB(140, 146, 162),
    C_INBAR = RGB(255, 255, 255), C_FIELD = RGB(236, 238, 244), C_PLACE = RGB(150, 155, 170),
    C_GREEN = RGB(52, 199, 120), C_AMBER = RGB(255, 184, 0), C_RED = RGB(240, 80, 80);

/* ------------------------------------------------------------------ drawing */
static void fill(int x, int y, int w, int h, eadk_color_t c) {
  if (w <= 0 || h <= 0) return;
  eadk_display_push_rect_uniform((eadk_rect_t){x, y, w, h}, c);
}

/* Horizontal inset of a rounded corner at row `dy` (0 = top) for radius r. */
static int corner_inset(int dy, int r) {
  if (dy >= r) return 0;
  float d = r - dy - 0.5f;
  return (int)(r - sqrtf((float)r * r - d * d) + 0.5f);
}

/* Rounded rectangle, clipped vertically to [clip0, clip1). */
static void round_rect(int x, int y, int w, int h, int r, eadk_color_t c, int clip0, int clip1) {
  if (2 * r > h) r = h / 2;
  if (2 * r > w) r = w / 2;
  for (int dy = 0; dy < h; dy++) {
    int yy = y + dy;
    if (yy < clip0 || yy >= clip1) continue;
    int in = dy < r ? corner_inset(dy, r) : (dy >= h - r ? corner_inset(h - 1 - dy, r) : 0);
    if (in == 0 && dy >= r && dy < h - r) { /* straight middle: one big rect */
      int end = y + h - r;
      if (end > clip1) end = clip1;
      fill(x, yy, w, end - yy, c);
      dy = end - y - 1;
      continue;
    }
    fill(x + in, yy, w - 2 * in, 1, c);
  }
}

static void circle(int cx, int cy, int r, eadk_color_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    fill(cx - dx, cy + dy, 2 * dx + 1, 1, c);
  }
}

static eadk_color_t mix(eadk_color_t a, eadk_color_t b, int t, int n) {
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  return (eadk_color_t)(((ar + (br - ar) * t / n) << 11) | ((ag + (bg - ag) * t / n) << 5) |
                        (ab + (bb - ab) * t / n));
}

/* The bot's face: a gradient disc with two eyes (and a mouth when it talks).
 * Painted row by row with every pixel written once, so redrawing never flickers. */
typedef struct { int lx, ly, blink, mouth; } face_t;
static const face_t FACE_REST = {0, 0, 0, 0};

static void avatar_rows(int cx, int cy, int r, face_t f, eadk_color_t bg, int bx0, int bx1, int y0, int y1) {
  int e = r >= 9 ? 2 : 1;
  int eh = f.blink ? 1 : e + 2, ew = e + 1;
  int ey = cy - r / 5 + f.ly + (f.blink ? (e + 2) / 2 : 0);
  int ex1 = cx - r / 3 - e / 2 + f.lx, ex2 = cx + r / 3 - e / 2 + f.lx;
  int mw = r / 2 < 3 ? 3 : r / 2, my = cy + r / 3 + 1, mx = cx - mw / 2 + f.lx;
  for (int yy = y0; yy < y1; yy++) {
    int dy = yy - cy;
    if (dy < -r || dy > r) { fill(bx0, yy, bx1 - bx0, 1, bg); continue; }
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    eadk_color_t c = mix(C_ACCENT2, C_ACCENT, dy + r, 2 * r);
    fill(bx0, yy, cx - dx - bx0, 1, bg);
    int sx[2], sw[2], ns = 0; /* white spans on this row, left to right */
    if (yy >= ey && yy < ey + eh) { sx[0] = ex1; sw[0] = ew; sx[1] = ex2; sw[1] = ew; ns = 2; }
    else if (f.mouth && yy >= my && yy < my + f.mouth) { sx[0] = mx; sw[0] = mw; ns = 1; }
    int x = cx - dx, end = cx + dx + 1;
    for (int k = 0; k < ns; k++) {
      fill(x, yy, sx[k] - x, 1, c);
      fill(sx[k], yy, sw[k], 1, 0xFFFF);
      x = sx[k] + sw[k];
    }
    fill(x, yy, end - x, 1, c);
    fill(end, yy, bx1 - end, 1, bg);
  }
}

static void text(const char *s, int x, int y, eadk_color_t fg, eadk_color_t bg) {
  eadk_display_draw_string(s, (eadk_point_t){x, y}, false, fg, bg);
}

/* ------------------------------------------------------------------ header */
#define STATUS_X0 (36 + 8 * LW + 6)    /* the status pill never reaches left of here */
static char status_text[24] = "ready";
static eadk_color_t status_dot = 0;

static void draw_status(void) {
  int tw = (int)strlen(status_text) * CW;
  int pw = tw + 22, ph = 18, px = W - 8 - pw, py = (HEADER_H - ph) / 2;
  /* clear everything right of the title: a longer previous pill must not leave its dot behind */
  fill(STATUS_X0, py, W - STATUS_X0, ph, C_HDR);
  round_rect(px, py, pw, ph, 9, C_PILL, 0, H);
  circle(px + 9, py + ph / 2, 3, status_dot);
  text(status_text, px + 16, py + 2, C_TITLE, C_PILL);
}

static void set_status(const char *s, eadk_color_t dot) {
  strncpy(status_text, s, sizeof status_text - 1);
  status_dot = dot;
  draw_status();
}

#define FACE_CX 18
#define FACE_CY (HEADER_H / 2 - 1)
static face_t face_now;
static void draw_face(face_t f) {
  face_now = f;
  avatar_rows(FACE_CX, FACE_CY, 10, f, C_HDR, FACE_CX - 12, FACE_CX + 13, FACE_CY - 11, FACE_CY + 12);
}

static void draw_header(void) {
  fill(0, 0, W, HEADER_H - 2, C_HDR);
  /* accent line fading from indigo to violet */
  for (int x = 0; x < W; x += 8) fill(x, HEADER_H - 2, 8, 2, mix(C_HDR_LINE, C_ACCENT2, x, W));
  draw_face(FACE_REST);
  eadk_display_draw_string("TinyChat", (eadk_point_t){36, 1}, true, C_TITLE, C_HDR);
  text("on-device AI", 37, 18, C_SUB, C_HDR);
  draw_status();
}

/* ------------------------------------------------------------------ messages */
enum { WHO_USER, WHO_BOT, WHO_INFO };
#define MAXMSG 32
#define ARENA 4096
static char arena[ARENA];
static int arena_used = 0;
typedef struct { int16_t start, len; uint8_t who; } msg_t;
static msg_t msgs[MAXMSG];
static int nmsg = 0;
static int scroll_px = 0;

static void drop_oldest(void) {
  int cut = msgs[0].start + msgs[0].len;
  memmove(arena, arena + cut, arena_used - cut);
  arena_used -= cut;
  memmove(msgs, msgs + 1, sizeof(msg_t) * (nmsg - 1));
  nmsg--;
  for (int i = 0; i < nmsg; i++) msgs[i].start -= cut;
}

static void msg_new(int who) {
  if (nmsg == MAXMSG) drop_oldest();
  msgs[nmsg++] = (msg_t){(int16_t)arena_used, 0, (uint8_t)who};
}

static void msg_append(const char *s, int n) {
  msg_t *m = &msgs[nmsg - 1];
  if (m->len == 0) while (n > 0 && *s == ' ') { s++; n--; } /* no leading space */
  while (arena_used + n > ARENA && nmsg > 1) { drop_oldest(); m = &msgs[nmsg - 1]; }
  if (arena_used + n > ARENA) n = ARENA - arena_used;
  if (n <= 0 || m->len + n > 600) return;
  memcpy(arena + arena_used, s, n);
  arena_used += n;
  m->len += n;
}

static int max_chars(int who) { return ((who == WHO_USER ? USER_MAXW : BOT_MAXW) - 2 * PADX) / CW; }

/* Word-wrap: returns number of lines, fills line starts/lengths (relative to text). */
#define MAXLINES 24
static int wrap(const char *t, int len, int width, int *ls, int *ll) {
  int n = 0, p = 0;
  if (len == 0) { ls[0] = 0; ll[0] = 0; return 1; }
  while (p < len && n < MAXLINES) {
    int take = len - p;
    if (take > width) {
      take = width;
      int sp = take;
      while (sp > 0 && t[p + sp] != ' ') sp--;
      if (sp > width / 3) take = sp;
    }
    ls[n] = p; ll[n] = take; n++;
    p += take;
    while (p < len && t[p] == ' ') p++;
  }
  return n;
}

typedef struct { int x, w, h, nl; int ls[MAXLINES], ll[MAXLINES]; } layout_t;

static void layout(int i, layout_t *L) {
  msg_t *m = &msgs[i];
  const char *t = arena + m->start;
  if (m->who == WHO_INFO) {
    L->nl = wrap(t, m->len, (W - 20) / CW, L->ls, L->ll);
    L->h = L->nl * CH;
    L->w = W; L->x = 0;
    return;
  }
  L->nl = wrap(t, m->len, max_chars(m->who), L->ls, L->ll);
  int longest = 0;
  for (int k = 0; k < L->nl; k++) if (L->ll[k] > longest) longest = L->ll[k];
  if (m->who == WHO_BOT && longest < 3) longest = 3; /* room for the typing dots */
  L->w = longest * CW + 2 * PADX;
  L->h = L->nl * CH + 2 * PADY;
  L->x = m->who == WHO_USER ? W - 8 - L->w : BOT_X;
}

static int round_inset(int dy, int h, int r) {
  if (2 * r > h) r = h / 2;
  if (dy < 0 || dy >= h) return 0;
  return dy < r ? corner_inset(dy, r) : (dy >= h - r ? corner_inset(h - 1 - dy, r) : 0);
}

/* Paint message i (top of its bubble at y) row by row, including the background to its left and
 * right, limited to rows [clip0, clip1). Every pixel is written once: no flicker. */
static void paint_msg(int i, const layout_t *L, int y, int clip0, int clip1) {
  msg_t *m = &msgs[i];
  const char *t = arena + m->start;
  char line[64];
  if (m->who == WHO_INFO) {
    for (int k = 0; k < L->nl; k++) {
      int ly = y + k * CH, tw = L->ll[k] * CW, tx = (W - tw) / 2;
      if (ly >= clip0 && ly + CH <= clip1) {
        fill(0, ly, tx, CH, C_BG);
        fill(tx + tw, ly, W - tx - tw, CH, C_BG);
        memcpy(line, t + L->ls[k], L->ll[k]);
        line[L->ll[k]] = 0;
        text(line, tx, ly, C_INFO, C_BG);
      } else {
        int a = ly < clip0 ? clip0 : ly, b = ly + CH > clip1 ? clip1 : ly + CH;
        fill(0, a, W, b - a, C_BG);
      }
    }
    return;
  }
  int bot = m->who == WHO_BOT;
  eadk_color_t bg = bot ? C_BOT_BG : C_ACCENT, fg = bot ? C_BOT_TXT : C_USER_TXT;
  int ox = bot ? L->x - 1 : L->x, oy = bot ? y - 1 : y, ow = bot ? L->w + 2 : L->w, oh = bot ? L->h + 2 : L->h;
  int ro = bot ? RADIUS + 1 : RADIUS;
  int acx = BOT_X - 13, acy = y + 9, ar = 8;
  int y0 = oy < clip0 ? clip0 : oy, y1 = oy + oh > clip1 ? clip1 : oy + oh;
  for (int yy = y0; yy < y1; yy++) {
    int io = round_inset(yy - oy, oh, ro);
    int lend = ox + io, rbeg = ox + ow - io;
    if (bot && yy >= acy - ar && yy <= acy + ar)
      avatar_rows(acx, acy, ar, FACE_REST, C_BG, 0, lend, yy, yy + 1);
    else
      fill(0, yy, lend, 1, C_BG);
    int a = lend, b = rbeg; /* inside the outer shape */
    if (bot) {
      if (yy < y || yy >= y + L->h) { /* top / bottom border row */
        fill(a, yy, b - a, 1, C_BOT_EDGE);
        fill(rbeg, yy, W - rbeg, 1, C_BG);
        continue;
      }
      int ii = round_inset(yy - y, L->h, RADIUS);
      a = L->x + ii;
      b = L->x + L->w - ii;
      fill(lend, yy, a - lend, 1, C_BOT_EDGE);
    }
    /* a text line passes through this row? leave its cells for draw_string */
    int rel = yy - (y + PADY), k = rel >= 0 ? rel / CH : -1;
    int band_top = y + PADY + k * CH;
    int has_text = k >= 0 && k < L->nl && L->ll[k] > 0 && band_top >= clip0 && band_top + CH <= clip1;
    if (has_text) {
      int tx = L->x + PADX, tw = L->ll[k] * CW;
      fill(a, yy, tx - a, 1, bg);
      fill(tx + tw, yy, b - tx - tw, 1, bg);
      if (yy == band_top + CH - 1) {
        memcpy(line, t + L->ls[k], L->ll[k]);
        line[L->ll[k]] = 0;
        text(line, tx, band_top, fg, bg);
      }
    } else {
      fill(a, yy, b - a, 1, bg);
    }
    if (bot) fill(b, yy, rbeg - b, 1, C_BOT_EDGE);
    fill(rbeg, yy, W - rbeg, 1, C_BG);
  }
  /* text bands cut by the bottom clip were filled above; ones cut at the top too */
}

/* y of the newest message's bottom when the view is anchored at the bottom (scrolled up by scroll_px). */
static int content_bottom(void) { return AREA_BOT - GAP + scroll_px; }

static int total_height(void) {
  layout_t L;
  int h = GAP;
  for (int i = 0; i < nmsg; i++) { layout(i, &L); h += L.h + GAP; }
  return h;
}

static void draw_chat(void) {
  /* find the visible messages, bottom-up */
  int vis[MAXMSG], vy[MAXMSG], nv = 0;
  int yb = content_bottom();
  layout_t L;
  for (int i = nmsg - 1; i >= 0 && yb > AREA_TOP; i--) {
    layout(i, &L);
    int y = yb - L.h;
    vis[nv] = i;
    vy[nv] = y;
    nv++;
    yb = y - GAP;
  }
  /* paint top-down: the gap above each message, then the message with its side margins */
  int cur = AREA_TOP;
  for (int v = nv - 1; v >= 0 && cur < AREA_BOT; v--) {
    int i = vis[v], y = vy[v];
    layout(i, &L);
    int bot = msgs[i].who == WHO_BOT;
    int top = bot ? y - 1 : y, end = bot ? y + L.h + 1 : y + L.h;
    if (top > cur) fill(0, cur, W, (top < AREA_BOT ? top : AREA_BOT) - cur, C_BG);
    paint_msg(i, &L, y, top > cur ? top : cur, AREA_BOT);
    if (end > cur) cur = end;
  }
  if (cur < AREA_BOT) fill(0, cur, W, AREA_BOT - cur, C_BG);
}

/* While a reply streams in: repaint only the last bubble if it kept its height. */
static int last_lines = -1;
static void update_last(void) {
  layout_t L;
  layout(nmsg - 1, &L);
  if (L.nl != last_lines || scroll_px) {
    last_lines = L.nl;
    scroll_px = 0;
    draw_chat();
    return;
  }
  paint_msg(nmsg - 1, &L, content_bottom() - L.h, AREA_TOP, AREA_BOT);
}

/* Typing indicator: three dots in the (still empty) bot bubble. */
static void typing_dots(int phase) {
  layout_t L;
  layout(nmsg - 1, &L);
  int y = content_bottom() - L.h + PADY + CH / 2;
  for (int k = 0; k < 3; k++) {
    eadk_color_t c = (k == phase % 3) ? C_ACCENT : C_BOT_EDGE;
    circle(L.x + PADX + 3 + k * 7, y, 2, c);
  }
}

/* ------------------------------------------------------------------ input bar */
static char input[MAXIN + 1];
static int in_len = 0;

static void draw_input(void) {
  int y = AREA_BOT;
  fill(0, y, W, 1, C_BOT_EDGE);
  fill(0, y + 1, W, INPUT_H - 1, C_INBAR);
  int fx = 6, fw = W - 6 - 34, fy = y + 5, fh = INPUT_H - 10;
  round_rect(fx, fy, fw, fh, fh / 2, C_FIELD, 0, H);
  int maxc = (fw - 20) / CW;
  char buf[64];
  if (in_len == 0) {
    text("Ask TinyChat anything...", fx + 10, fy + (fh - CH) / 2, C_PLACE, C_FIELD);
    fill(fx + 9, fy + 4, 1, fh - 8, C_ACCENT);
  } else {
    int shown = in_len > maxc ? maxc : in_len;
    memcpy(buf, input + in_len - shown, shown);
    buf[shown] = 0;
    text(buf, fx + 10, fy + (fh - CH) / 2, C_BOT_TXT, C_FIELD);
    fill(fx + 10 + shown * CW + 1, fy + 4, 1, fh - 8, C_ACCENT);
  }
  /* send button */
  int bx = W - 6 - 12, by = y + INPUT_H / 2;
  circle(bx, by, 11, in_len ? C_ACCENT : C_BOT_EDGE);
  for (int k = 0; k < 5; k++) fill(bx - 2 + k, by - 4 + k, 2, 9 - 2 * k, 0xFFFF); /* arrow > */
  fill(bx - 6, by - 1, 5, 2, 0xFFFF);
}

/* ------------------------------------------------------------------ keyboard */
static int event_char(eadk_event_t e) {
  if (e >= eadk_event_lower_a && e <= eadk_event_lower_z) {
    static const char lo[] = "abcdefghijklmnopq?rstuv?wxyz";
    char c = lo[e - eadk_event_lower_a];
    return c == '?' ? 0 : c;
  }
  if (e >= eadk_event_upper_a && e <= eadk_event_upper_z) {
    static const char up[] = "ABCDEFGHIJKLMNOPQ?RSTUV?WXYZ";
    char c = up[e - eadk_event_upper_a];
    return c == '?' ? 0 : c;
  }
  switch (e) {
    case eadk_event_space: return ' ';
    case eadk_event_question: return '?';
    case eadk_event_exclamation: return '!';
    case eadk_event_dot: return '.';
    case eadk_event_comma: return ',';
    case eadk_event_colon: return ':';
    case eadk_event_semicolon: return ';';
    case eadk_event_double_quotes: return '"';
    case eadk_event_percent: return '%';
    case eadk_event_equal: return '=';
    case eadk_event_plus: return '+';
    case eadk_event_minus: return '-';
    case eadk_event_multiplication: return '*';
    case eadk_event_division: return '/';
    case eadk_event_left_parenthesis: return '(';
    case eadk_event_right_parenthesis: return ')';
    case eadk_event_underscore: return '_';
    case eadk_event_zero: return '0';
    case eadk_event_one: return '1';
    case eadk_event_two: return '2';
    case eadk_event_three: return '3';
    case eadk_event_four: return '4';
    case eadk_event_five: return '5';
    case eadk_event_six: return '6';
    case eadk_event_seven: return '7';
    case eadk_event_eight: return '8';
    case eadk_event_nine: return '9';
    default: return 0;
  }
}

/* ------------------------------------------------------------------ model glue */
static nwai_t M;
static int hist[CTX]; /* tokens currently in the model's memory */
static int nhist = 0;
static int dots_phase = 0;
static int show_dots = 0;

/* Debugging aid: set nwai_trace = show_trace to see each step of the model on screen. */
static void __attribute__((unused)) show_trace(const char *label) {
  fill(10, 60, W - 20, 24, C_RED);
  eadk_display_draw_string(label, (eadk_point_t){16, 64}, true, 0xFFFF, C_RED);
  eadk_timing_msleep(250);
}

static float *feed(const int *t, int n) {
  float *lg = NULL;
  for (int i = 0; i < n; i++) {
    hist[nhist++] = t[i];
    lg = nwai_forward(&M, t[i]);
    /* hand control back to the system after every token: computing too long without a pause
     * makes the calculator reset (seen with the bigger 4-bit model) */
    eadk_timing_msleep(1);
    if (show_dots) {
      typing_dots(dots_phase++);
      static const int look[4] = {-2, 0, 2, 0};
      draw_face((face_t){look[(dots_phase / 2) & 3], -1, 0, 0});
    }
  }
  return lg;
}

/* Make room for `need` more tokens: keep only the most recent whole turns. */
static void make_room(int need) {
  if (nhist + need <= M.ctx) return;
  int keep_from = nhist;
  for (int i = nhist - 1; i >= 0; i--) {
    if (hist[i] == M.tok_u) {
      if (nhist - i + need > M.ctx / 2) break;
      keep_from = i;
    }
  }
  int n = nhist - keep_from;
  int tmp[CTX];
  memcpy(tmp, hist + keep_from, n * sizeof(int));
  nhist = 0;
  nwai_reset(&M);
  feed(tmp, n);
}

static void reply(void) {
  int toks[MAXIN + 2], n = 0;
  toks[n++] = M.tok_u;
  n += nwai_encode(&M, input, toks + 1, MAXIN);
  toks[n++] = M.tok_b;
  if (n > M.ctx / 2) n = M.ctx / 2, toks[n - 1] = M.tok_b;

  char question[MAXIN + 1];
  memcpy(question, input, in_len + 1);
  msg_new(WHO_USER);
  msg_append(input, in_len);
  in_len = 0;
  input[0] = 0;
  msg_new(WHO_BOT);
  scroll_px = 0;
  last_lines = -1;
  draw_chat();
  draw_input();
  /* Built-in helpers first: maths, units, primes and known facts are answered exactly.
   * The model doesn't see these turns; it only handles what the helpers don't know. */
  char ans[256];
  if (helper_reply(question, ans, sizeof ans, eadk_random())) {
    set_status("answer", C_GREEN);
    int len = strlen(ans);
    for (int i = 0; i < len;) { /* word by word, quick, so it reads like the AI writing */
      int j = i;
      while (j < len && ans[j] != ' ') j++;
      while (j < len && ans[j] == ' ') j++;
      msg_append(ans + i, j - i);
      update_last();
      draw_face((face_t){0, 0, 0, ((i / 3) & 1) ? 2 : 1});
      eadk_timing_msleep(35);
      i = j;
    }
    set_status("calculated", C_GREEN);
    draw_face(FACE_REST);
    draw_chat();
    return;
  }
  set_status("thinking", C_AMBER);

  uint64_t t0 = eadk_timing_millis();
  show_dots = 1;
  /* Every message starts from a clean memory: this small model can't use earlier turns, it
   * just copies their pattern ("tell a joke" after a capital -> "Paris is the capital of..."). */
  nhist = 0;
  nwai_reset(&M);
  make_room(n + REPLY_MAX);
  float *lg = feed(toks, n);
  int reply_start = nhist;
  show_dots = 0;
  set_status("writing", C_ACCENT2);
  int count = 0, stopped = 0;
  for (; count < REPLY_MAX; count++) {
    if (eadk_keyboard_key_down(eadk_keyboard_scan(), eadk_key_back)) { stopped = 1; break; }
    /* low temperature + min-p: no invented words; a very light penalty on what this reply already
     * said (stronger ones broke long learnt answers, which reuse "the" and ",") */
    nwai_penalize(lg, hist + reply_start, nhist - reply_start, 1.05f);
    int t = nwai_sample(&M, lg, 0.4f, 40, 0.2f, eadk_random());
    if (t == M.tok_eos || t == M.tok_u || t == M.tok_b) break;
    msg_append((const char *)M.piece[t], M.piece_len[t]);
    update_last();
    draw_face((face_t){0, 0, 0, (count & 1) ? 2 : 1});
    if (nhist >= M.ctx) break;
    lg = feed(&t, 1);
  }
  if (msgs[nmsg - 1].len == 0) { msg_append("...", 3); update_last(); }
  uint64_t ms = eadk_timing_millis() - t0;
  char st[24];
  if (stopped) snprintf(st, sizeof st, "stopped");
  else snprintf(st, sizeof st, "%d.%d tok/s", ms ? (int)((n + count) * 1000 / ms) : 0,
                ms ? (int)((n + count) * 10000 / ms % 10) : 0);
  set_status(st, stopped ? C_AMBER : C_GREEN);
  draw_face(FACE_REST);
  draw_chat();
  /* wait for Back to be released so it doesn't also quit the app */
  while (eadk_keyboard_key_down(eadk_keyboard_scan(), eadk_key_back)) eadk_timing_msleep(20);
}

static void info(const char *s) {
  msg_new(WHO_INFO);
  msg_append(s, strlen(s));
}

static void welcome(void) {
  nmsg = 0; arena_used = 0; scroll_px = 0;
  info("TinyChat runs fully on this calculator.");
  msg_new(WHO_BOT);
  const char *hi = "Hi! I'm TinyChat, a tiny AI living in your calculator. Ask me anything!";
  msg_append(hi, strlen(hi));
  info("EXE send  -  Clear new chat  -  Back quit");
}

static int idle_ticks = 0;

/* While you type, the face glances down at the input, following the cursor. */
static void face_follow_input(void) {
  int lx = in_len * 5 / MAXIN - 2;
  if (lx > 2) lx = 2;
  draw_face((face_t){lx, 2, 0, 0});
}

int main(void) {
  draw_header();
  fill(0, AREA_TOP, W, H - AREA_TOP, C_BG);
  int err = nwai_init(&M, eadk_external_data, eadk_external_data_size, CTX);
  if (err) {
    char b[80];
    set_status("error", C_RED);
    snprintf(b, sizeof b, "Model failed to load (error %d).", err);
    info(b);
    snprintf(b, sizeof b, "Needs %u KB of RAM.", (unsigned)(nwai_ram_needed(eadk_external_data, CTX) / 1024));
    info(b);
    info("Reinstall the app with its model file.");
    draw_chat();
    while (!eadk_keyboard_key_down(eadk_keyboard_scan(), eadk_key_back)) eadk_timing_msleep(50);
    return 0;
  }
  welcome();
  set_status("ready", C_GREEN);
  draw_chat();
  draw_input();

  for (;;) {
    int32_t timeout = 250;
    eadk_event_t e = eadk_event_get(&timeout);
    if (timeout <= 0) { /* no key: idle animation */
      idle_ticks++;
      if (idle_ticks == 4 && (face_now.ly || face_now.lx)) draw_face(FACE_REST);   /* stop looking at the input */
      if (idle_ticks % 16 == 15) draw_face((face_t){0, 0, 1, 0});                 /* blink... */
      else if (idle_ticks % 16 == 0 && face_now.blink) draw_face(FACE_REST);      /* ...and open */
      continue;
    }
    idle_ticks = 0;
    if (e == eadk_event_back) return 0;
    if (e == eadk_event_exe || e == eadk_event_ok) {
      if (in_len > 0) reply();
      continue;
    }
    if (e == eadk_event_backspace) {
      if (in_len > 0) input[--in_len] = 0;
      draw_input();
      face_follow_input();
      continue;
    }
    if (e == eadk_event_clear) {
      nhist = 0;
      nwai_reset(&M);
      welcome();
      set_status("ready", C_GREEN);
      draw_chat();
      continue;
    }
    if (e == eadk_event_up || e == eadk_event_down) {
      int maxs = total_height() - (AREA_BOT - AREA_TOP);
      if (maxs < 0) maxs = 0;
      scroll_px += e == eadk_event_up ? 3 * CH : -3 * CH;
      if (scroll_px > maxs) scroll_px = maxs;
      if (scroll_px < 0) scroll_px = 0;
      draw_chat();
      continue;
    }
    /* maths keys type their text form, which the built-in maths helper reads */
    const char *keys = e == eadk_event_power ? "^" : e == eadk_event_sqrt ? "sqrt(" :
                       e == eadk_event_square ? "^2" : e == eadk_event_pi ? "pi" : NULL;
    char one[2] = {(char)event_char(e), 0};
    if (!keys && one[0]) keys = one;
    if (keys && in_len + (int)strlen(keys) <= MAXIN) {
      strcpy(input + in_len, keys);
      in_len += strlen(keys);
      draw_input();
      face_follow_input();
    }
  }
}
