#include "roll.h"
#include <math.h>
#include <string.h>

const badge_info_t BADGES[N_BADGES] = {
#define B(id, name, desc, fam, test) [B_##id] = {name, desc, fam},
#include "badges.def"
#undef B
};

const char *const FAMILY_NAMES[N_FAMILIES] = {
    "", "zero endings", "fives", "quarters", "nines", "zeros inside", "size", "lone digits", "pairs", "of a kind",
    "houses", "runs", "doubles", "sevens", "steps", "mirrors", "first and last", "first and last", "frames",
    "climbs", "falls", "climbs", "falls", "spacing", "peaks", "troughs", "zigzags", "skips", "parity", "primes",
    "powers", "bases", "splits", "chunks", "progressions", "42", "666", "1337", "7734", "911", "404", "1984", "67",
    "86", "101", "365", "pi", "e", "tau", "phi", "growth", "decimals"};

const char *const TIER_NAMES[N_TIERS] = {
    "Trashest", "Trash", "Common", "Uncommon", "Rare", "Epic", "Anomaly", "Mythic", "Unreal", "Aberration",
    "Otherworldly", "Cataclysmic", "Divine", "Transcendent", "Devilish", "Error"};

/* ---------------------------------------------------------------- numbers */

static void set_value(number_t *n) {
  n->v = 0;
  for (int i = 0; i < n->n; i++) n->v = n->v * 10 + (uint64_t)(n->digits[i] - '0');
}

static void put_uint(char *out, uint32_t v) {
  char t[12];
  int k = 0;
  do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
  for (int i = 0; i < k; i++) out[i] = t[k - 1 - i];
  out[k] = 0;
}

void roll_from_seed(number_t *n, uint32_t seed) {
  memset(n, 0, sizeof *n);
  put_uint(n->digits, seed);
  n->n = (uint8_t)strlen(n->digits);
  set_value(n);
}

/* RNGidle's draw: the seed, then every digit (new ones too) has one 1-in-100 chance to add a digit */
void roll_draw(number_t *n, rand_fn rnd) {
  bool neg = rnd() % 20 == 0;
  roll_from_seed(n, rnd() % 1000000);
  for (int t = 0; t < n->n; t++) {
    if (rnd() % 100) continue;
    if (n->n == 1 && n->digits[0] == '0') {
      n->digits[0] = (char)('1' + rnd() % 9);
      n->digits[1] = '0';
      n->n = 2;
    } else if (n->n < MAX_DIGITS) n->digits[n->n++] = (char)('0' + rnd() % 10);
    n->digits[n->n] = 0;
  }
  if (rnd() % 100 == 0) {
    n->frac[n->fn++] = (char)('0' + rnd() % 10);
    while (n->fn < MAX_FRAC && rnd() % 5 == 0) n->frac[n->fn++] = (char)('0' + rnd() % 10);
    while (n->fn && n->frac[n->fn - 1] == '0') n->fn--;   /* 2.50 is 2.5, 2.0 is 2 */
    n->frac[n->fn] = 0;
  }
  set_value(n);
  n->neg = neg && (n->v || n->fn);
}

void roll_fix(number_t *n) {
  n->digits[MAX_DIGITS] = n->frac[MAX_FRAC] = 0;
  int len = (int)strlen(n->digits), lead = 0;
  while (lead < len - 1 && n->digits[lead] == '0') lead++;   /* 023 is 23 */
  if (lead) memmove(n->digits, n->digits + lead, (size_t)(len - lead + 1));
  if (!n->digits[0]) strcpy(n->digits, "0");
  n->n = (uint8_t)strlen(n->digits);
  n->fn = (uint8_t)strlen(n->frac);
  while (n->fn && n->frac[n->fn - 1] == '0') n->fn--;          /* 2.50 is 2.5 */
  n->frac[n->fn] = 0;
  set_value(n);
  n->neg = n->neg && (n->v || n->fn);
}

void roll_text(const number_t *n, char *out) {
  int k = 0;
  if (n->neg) out[k++] = '-';
  memcpy(out + k, n->digits, n->n);
  k += n->n;
  if (n->fn) {
    out[k++] = '.';
    memcpy(out + k, n->frac, n->fn);
    k += n->fn;
  }
  out[k] = 0;
}

void fmt_int(uint64_t v, char *out) {
  char t[32];
  int k = 0, g = 0;
  do {
    if (g && g % 3 == 0) t[k++] = ',';
    t[k++] = (char)('0' + v % 10);
    v /= 10;
    g++;
  } while (v);
  for (int i = 0; i < k; i++) out[i] = t[k - 1 - i];
  out[k] = 0;
}

/* ---------------------------------------------------------------- arithmetic */

static uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) {   /* m < 2^62, no 128-bit type on the device */
  uint64_t r = 0;
  a %= m;
  while (b) {
    if (b & 1) { r += a; if (r >= m) r -= m; }
    a += a;
    if (a >= m) a -= m;
    b >>= 1;
  }
  return r;
}

static uint64_t powmod(uint64_t a, uint64_t e, uint64_t m) {
  uint64_t r = 1;
  for (a %= m; e; e >>= 1, a = mulmod(a, a, m))
    if (e & 1) r = mulmod(r, a, m);
  return r;
}

bool is_prime64(uint64_t v) {
  if (v < 2) return false;
  static const uint8_t small[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
  for (unsigned i = 0; i < sizeof small; i++) {
    if (v == small[i]) return true;
    if (v % small[i] == 0) return false;
  }
  if (v < 1369) return true;
  if (v < 4000000) {
    for (uint32_t d = 41; (uint64_t)d * d <= v; d += 6)
      if (v % d == 0 || v % (d + 2) == 0) return false;
    return true;
  }
  /* Miller-Rabin; these bases decide every 64-bit number */
  uint64_t d = v - 1;
  int s = 0;
  while (!(d & 1)) { d >>= 1; s++; }
  for (unsigned i = 0; i < sizeof small; i++) {
    uint64_t x = powmod(small[i], d, v);
    if (x == 1 || x == v - 1) continue;
    bool comp = true;
    for (int r = 1; r < s && comp; r++) {
      x = mulmod(x, x, v);
      if (x == v - 1) comp = false;
    }
    if (comp) return false;
  }
  return true;
}

static uint64_t isqrt(uint64_t v) {
  uint64_t r = 0;
  for (uint64_t bit = 1ull << 62; bit; bit >>= 2) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
  }
  return r;
}

static bool is_square(uint64_t v) { uint64_t r = isqrt(v); return r * r == v; }

static bool is_fib(uint64_t v) {
  uint64_t a = 1, b = 2;
  while (b < v) { uint64_t c = a + b; a = b; b = c; }
  return b == v;
}


/* ---------------------------------------------------------------- what the badges look at */

typedef struct {
  const char *d;
  int n, fn;
  bool neg, pal, prime;
  uint64_t v, prod;
  int cnt[10], distinct, maxcnt, pairs, maxrun, up, down, sum, odd, maxd, mind, shape_up, shape_down, maxstep;
  int nruns, run_len[MAX_DIGITS];
} feat_t;

static void features(feat_t *f, const number_t *num) {
  memset(f, 0, sizeof *f);
  const char *d = f->d = num->digits;
  int n = f->n = num->n;
  f->v = num->v;
  f->fn = num->fn;
  f->neg = num->neg;
  f->prod = 1;
  f->mind = 9;
  int run = 0, up = 0, down = 0;
  for (int i = 0; i < n; i++) {
    int c = d[i] - '0';
    f->cnt[c]++;
    f->sum += c;
    f->odd += c & 1;
    if (f->prod <= UINT64_MAX / 10) f->prod *= (uint64_t)c;
    if (c > f->maxd) f->maxd = c;
    if (c < f->mind) f->mind = c;
    if (i && d[i] == d[i - 1]) run++;
    else {
      if (i) f->run_len[f->nruns++] = run;
      run = 1;
    }
    up = i && d[i] == d[i - 1] + 1 ? up + 1 : 1;
    down = i && d[i] == d[i - 1] - 1 ? down + 1 : 1;
    if (run > f->maxrun) f->maxrun = run;
    if (up > f->up) f->up = up;
    if (down > f->down) f->down = down;
    if (i) {
      int step = d[i] - d[i - 1];
      if (step > 0) f->shape_up++;
      if (step < 0) f->shape_down++;
      if (step < 0) step = -step;
      if (step > f->maxstep) f->maxstep = step;
    }
  }
  f->run_len[f->nruns++] = run;
  for (int c = 0; c < 10; c++) {
    if (f->cnt[c]) f->distinct++;
    if (f->cnt[c] >= 2) f->pairs++;
    if (f->cnt[c] > f->maxcnt) f->maxcnt = f->cnt[c];
  }
  f->pal = true;
  for (int i = 0; i < n / 2; i++)
    if (d[i] != d[n - 1 - i]) f->pal = false;
  f->prime = is_prime64(f->v);
}

static bool has(const feat_t *f, const char *s) { return strstr(f->d, s) != 0; }
static bool is(const feat_t *f, const char *s) { return !strcmp(f->d, s); }
static bool ends(const feat_t *f, const char *s) {
  int k = (int)strlen(s);
  return f->n >= k && !memcmp(f->d + f->n - k, s, k);
}
static bool only(const feat_t *f, const char *set) {
  for (int i = 0; i < f->n; i++)
    if (!strchr(set, f->d[i])) return false;
  return true;
}

static bool half_sums_equal(const feat_t *f) {
  int a = 0, b = 0;
  for (int i = 0; i < f->n / 2; i++) { a += f->d[i]; b += f->d[f->n - 1 - i]; }
  return a == b;
}

static bool full_house(const feat_t *f) {
  for (int a = 0; a < 10; a++)
    for (int b = 0; b < 10; b++)
      if (a != b && f->cnt[a] >= 3 && f->cnt[b] >= 2) return true;
  return false;
}

static bool packed_house(const feat_t *f) {
  for (int i = 0; i + 1 < f->nruns; i++) {
    int a = f->run_len[i], b = f->run_len[i + 1];
    if ((a == 3 && b == 2) || (a == 2 && b == 3)) return true;
  }
  return false;
}

static bool adj_pairs(const feat_t *f, int k) {
  int row = 0;
  for (int i = 0; i < f->nruns; i++) {
    row = f->run_len[i] == 2 ? row + 1 : 0;
    if (row >= k) return true;
  }
  return false;
}

static bool xyxy(const feat_t *f) {
  for (int i = 0; i + 3 < f->n; i++)
    if (f->d[i] != f->d[i + 1] && f->d[i] == f->d[i + 2] && f->d[i + 1] == f->d[i + 3]) return true;
  return false;
}

static bool repeated_chunk(const feat_t *f) {
  for (int i = 0; i + 1 < f->n; i++)
    for (int j = i + 2; j + 1 < f->n; j++)
      if (f->d[i] == f->d[j] && f->d[i + 1] == f->d[j + 1]) return true;
  return false;
}

static bool sandwich(const feat_t *f, int at, int len) {
  char c = f->d[at];
  for (int i = at; i < at + len; i++)
    if (f->d[i] != c) return false;
  return f->d[0] != c && f->d[f->n - 1] != c;
}

static bool layer_cake(const feat_t *f) {
  const char *d = f->d;
  return d[1] == d[2] && d[3] == d[4] && d[2] != d[3] && d[0] != d[1] && d[5] != d[4];
}

static bool window_straight(const char *d, int len) {
  int seen = 0, lo = 9, hi = 0;
  for (int i = 0; i < len; i++) {
    int c = d[i] - '0';
    if (seen & (1 << c)) return false;
    seen |= 1 << c;
    if (c < lo) lo = c;
    if (c > hi) hi = c;
  }
  return hi - lo == len - 1;
}

static bool small_straight(const feat_t *f) {
  for (int len = 3; len <= f->n && len <= 10; len++)
    for (int i = 0; i + len <= f->n; i++)
      if (window_straight(f->d + i, len)) return true;
  return false;
}

static bool close_call(const feat_t *f) {
  for (int c = 0; c < 9; c++)
    if (f->cnt[c] && f->cnt[c + 1]) return true;
  return false;
}

static bool hidden_palindrome(const feat_t *f) {
  for (int len = 4; len <= f->n; len++)
    for (int i = 0; i + len <= f->n; i++) {
      bool p = true;
      for (int k = 0; k < len / 2 && p; k++)
        if (f->d[i + k] != f->d[i + len - 1 - k]) p = false;
      if (p) return true;
    }
  return false;
}

static bool upside_down(const feat_t *f) {
  static const char ROT[10] = {'0', '1', 0, 0, 0, 0, '9', 0, '8', '6'};
  if (f->n < 2) return false;
  for (int i = 0; i < f->n; i++)
    if (ROT[f->d[f->n - 1 - i] - '0'] != f->d[i]) return false;
  return true;
}

static bool bookends(const feat_t *f) {
  if (f->n < 3 || f->d[0] != f->d[f->n - 1]) return false;
  for (int i = 1; i < f->n - 1; i++)
    if (f->d[i] != f->d[0]) return true;
  return false;
}

static bool arithmetic_digits(const feat_t *f, bool absolute) {
  int s0 = f->d[1] - f->d[0];
  if (absolute && s0 == 0) return false;
  for (int i = 2; i < f->n; i++) {
    int s = f->d[i] - f->d[i - 1];
    if (absolute ? (s != s0 && s != -s0) : s != s0) return false;
  }
  return true;
}

/* climbs then falls (dir 1) or falls then climbs (dir -1); strict: no flat step at all */
static bool peak(const feat_t *f, int dir, bool strict) {
  if (f->n < 3) return false;
  int phase = 0, first = 0, second = 0;
  for (int i = 1; i < f->n; i++) {
    int s = (f->d[i] > f->d[i - 1]) - (f->d[i] < f->d[i - 1]);
    if (s == 0) { if (strict) return false; continue; }
    if (phase == 0) { if (s != dir) return false; first++; phase = 1; }
    else if (phase == 1) { if (s == dir) first++; else { second++; phase = 2; } }
    else { if (s == dir) return false; second++; }
  }
  return first && second;
}

static bool zigzag(const feat_t *f, bool strict) {
  if (f->n < 3) return false;
  int last = 0, steps = 0;
  for (int i = 1; i < f->n; i++) {
    int s = (f->d[i] > f->d[i - 1]) - (f->d[i] < f->d[i - 1]);
    if (s == 0) { if (strict) return false; continue; }
    if (s == last) return false;
    last = s;
    steps++;
  }
  return steps >= (strict ? 2 : 3);
}

static bool checker(const feat_t *f) {
  for (int i = 1; i < f->n; i++)
    if (((f->d[i] ^ f->d[i - 1]) & 1) == 0) return false;
  return true;
}

static bool ping_pong(const feat_t *f) {
  if (f->d[0] == f->d[1]) return false;
  for (int i = 2; i < f->n; i++)
    if (f->d[i] != f->d[i - 2]) return false;
  return true;
}

static bool skip(const feat_t *f, int times) {
  for (int i = 0; i + 2 * (times - 1) < f->n; i++) {
    bool ok = true;
    for (int k = 1; k < times && ok; k++)
      if (f->d[i + 2 * k] != f->d[i] || f->d[i + 2 * k - 1] == f->d[i]) ok = false;
    if (ok) return true;
  }
  return false;
}

static bool pow_le(uint64_t r, int k, uint64_t v, bool *eq) {   /* r^k <= v, and whether equal */
  uint64_t p = 1;
  for (int i = 0; i < k; i++)
    if (__builtin_mul_overflow(p, r, &p)) return false;
  *eq = p == v;
  return p <= v;
}

static bool is_power(uint64_t v, int k) {
  uint64_t r = (uint64_t)llround(pow((double)v, 1.0 / k));
  for (uint64_t c = r > 1 ? r - 1 : 1; c <= r + 1; c++) {
    bool eq = false;
    if (pow_le(c, k, v, &eq) && eq) return true;
  }
  return false;
}

static bool power_of(uint64_t v, uint64_t b) {
  if (v < b) return false;
  while (v % b == 0) v /= b;
  return v == 1;
}

static bool self_power(uint64_t v) {
  for (uint64_t k = 1; k <= 15; k++) {
    bool eq = false;
    if (!pow_le(k, (int)k, v, &eq)) return false;
    if (eq) return true;
  }
  return false;
}

static bool factorial(uint64_t v) {
  uint64_t p = 1;
  for (uint64_t k = 1; p <= v; k++) {
    if (p == v) return true;
    if (__builtin_mul_overflow(p, k + 1, &p)) return false;
  }
  return false;
}

static bool pronic(uint64_t v) {
  uint64_t k = isqrt(v);
  return k * (k + 1) == v;
}

/* the digits d[a..b) as a number; false if it has a leading zero (a lone 0 is fine) */
static bool chunk(const char *d, int a, int b, uint64_t *out) {
  if (b - a > 1 && d[a] == '0') return false;
  uint64_t v = 0;
  for (int i = a; i < b; i++) v = v * 10 + (uint64_t)(d[i] - '0');
  *out = v;
  return true;
}

static bool op_ok(uint64_t a, uint64_t b, uint64_t c) {   /* a op b == c for one of + - x / */
  uint64_t p;
  if (a + b == c) return true;
  if (a >= b && a - b == c) return true;
  if (!__builtin_mul_overflow(a, b, &p) && p == c) return true;
  if (b && a % b == 0 && a / b == c) return true;
  return false;
}

static bool equation(const feat_t *f) {
  if (f->n < 3 || f->n > 12) return false;
  for (int i = 1; i < f->n - 1; i++)
    for (int j = i + 1; j < f->n; j++) {
      uint64_t a, b, c;
      if (!chunk(f->d, 0, i, &a) || !chunk(f->d, i, j, &b) || !chunk(f->d, j, f->n, &c)) continue;
      if (op_ok(a, b, c) || op_ok(b, c, a)) return true;   /* a op b = c, or a = b op c */
    }
  return false;
}

/* the whole number cut into `parts` numbers that are consecutive integers.
 * mode 0: any order, 1: in increasing order, 2: not in increasing order */
static bool consecutive_split(const feat_t *f, int parts, int mode) {
  if (f->n < parts || f->n > 16) return false;
  int pos[4] = {0};   /* pos[k]: where part k starts (pos[0] = 0) */
  for (int k = 1; k < parts; k++) pos[k] = k;
  for (;;) {
    uint64_t val[4];
    bool ok = true;
    for (int k = 0; k < parts && ok; k++) {
      int a = pos[k], b = k + 1 < parts ? pos[k + 1] : f->n;
      ok = chunk(f->d, a, b, &val[k]);
    }
    if (ok) {
      uint64_t lo = val[0];
      bool inc = true;
      for (int k = 1; k < parts; k++) {
        if (val[k] < lo) lo = val[k];
        if (val[k] != val[k - 1] + 1) inc = false;
      }
      int seen = 0;
      bool cons = true;
      for (int k = 0; k < parts && cons; k++) {
        uint64_t o = val[k] - lo;
        if (o >= (uint64_t)parts || (seen & (1 << o))) cons = false;
        else seen |= 1 << o;
      }
      if (cons && (mode == 0 || (mode == 1) == inc)) return true;
    }
    int k = parts - 1;
    while (k >= 1 && pos[k] == f->n - (parts - k)) k--;
    if (k < 1) break;
    pos[k]++;
    for (int j = k + 1; j < parts; j++) pos[j] = pos[j - 1] + 1;
  }
  return false;
}

static bool chunks_one_apart(const feat_t *f, bool adjacent) {
  if (f->n < 4 || f->n > 12) return false;
  for (int i = 0; i < f->n; i++)
    for (int j = i + 2; j <= f->n; j++) {
      uint64_t a, b;
      if (!chunk(f->d, i, j, &a)) continue;
      for (int k = adjacent ? j : j + 1; k < f->n && (!adjacent || k == j); k++)
        for (int l = k + 2; l <= f->n; l++) {
          if (!chunk(f->d, k, l, &b)) continue;
          if (a + 1 == b || b + 1 == a) return true;
        }
    }
  return false;
}

/* the whole number cut into 3+ numbers with the same gap (not 0) or the same ratio (not 1) */
static bool progression_split(const feat_t *f, bool geometric) {
  if (f->n < 3 || f->n > 12) return false;
  for (uint32_t mask = 0; mask < (1u << (f->n - 1)); mask++) {
    if (__builtin_popcount(mask) < 2) continue;
    uint64_t val[MAX_DIGITS];
    int k = 0, a = 0;
    bool ok = true;
    for (int i = 1; i <= f->n && ok; i++)
      if (i == f->n || ((mask >> (i - 1)) & 1)) { ok = chunk(f->d, a, i, &val[k++]); a = i; }
    if (!ok) continue;
    bool good = val[1] != val[0];
    for (int i = 2; i < k && good; i++) {
      if (geometric) {
        uint64_t x, y;
        good = val[0] && val[i] && val[i - 1] && !__builtin_mul_overflow(val[i - 2], val[i], &x) &&
               !__builtin_mul_overflow(val[i - 1], val[i - 1], &y) && x == y;
      } else good = (val[1] > val[0]) == (val[i] > val[i - 1]) &&
                    (val[1] > val[0] ? val[i] - val[i - 1] == val[1] - val[0] : val[i - 1] - val[i] == val[0] - val[1]);
    }
    if (good) return true;
  }
  return false;
}

/* ---------------------------------------------------------------- badges */

void roll_badges(roll_t *r) {
  feat_t F, *f = &F;
  features(f, &r->num);
  r->nb = 0;
#define B(id, name, desc, fam, test) \
  if (r->nb < MAX_EARNED && (test)) r->badges[r->nb++] = B_##id;
#include "badges.def"
#undef B
}

void roll_from_text(number_t *n, const char *digits) {
  memset(n, 0, sizeof *n);
  n->n = (uint8_t)strlen(digits);
  memcpy(n->digits, digits, n->n);
  set_value(n);
}

/* ---------------------------------------------------------------- scoring */

/* each family pays only its best badge; badges without a family all pay. Sums saturate. */
void roll_ep_with(roll_t *r, const uint64_t *price) {
  memset(r->paid, 0, sizeof r->paid);
  r->ep = 0;
  int best[N_FAMILIES];
  for (int i = 0; i < N_FAMILIES; i++) best[i] = -1;
  for (int i = 0; i < r->nb; i++) {
    int b = r->badges[i], fam = BADGES[b].family;
    if (fam == F_NONE) { SETBIT(r->paid, i); continue; }
    if (best[fam] < 0 || price[b] > price[r->badges[best[fam]]]) best[fam] = i;
  }
  for (int i = 0; i < N_FAMILIES; i++)
    if (best[i] >= 0) SETBIT(r->paid, best[i]);
  for (int i = 0; i < r->nb; i++)
    if (BIT(r->paid, i)) {
      uint64_t p = price[r->badges[i]];
      r->ep = r->ep + p < r->ep ? UINT64_MAX : r->ep + p;
    }
}
