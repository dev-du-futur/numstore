/* Built-in helpers, see helpers.h. Order of tries: maths, primes, units.
 * Each helper only answers when the whole question fits its pattern; anything
 * unknown goes to the model, so a helper never guesses. */
#include "helpers.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ text utils */
#define NORM_MAX 160

/* lowercase, apostrophes dropped ("what's" -> "whats"), other punctuation -> space,
 * spaces collapsed, no leading/trailing space */
static void normalize(const char *in, char *out) {
  int n = 0;
  for (const char *p = in; *p && n < NORM_MAX - 1; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == '\'') continue;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) c = ' ';
    if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = 0;
}

/* whole-word search; a pattern ending in '*' matches any word starting with it */
static const char *find_word(const char *s, const char *w) {
  int len = strlen(w), prefix = len && w[len - 1] == '*';
  if (prefix) len--;
  if (len == 0) return NULL;
  for (const char *p = s; *p; p++) {
    if (strncmp(p, w, len)) continue;
    if (p != s && p[-1] != ' ') continue;
    if (!prefix && p[len] && p[len] != ' ') continue;
    return p;
  }
  return NULL;
}

/* all space-separated words of `words` appear in s */
static int has_all(const char *s, const char *words) {
  char w[24];
  while (*words) {
    int n = 0;
    while (*words == ' ') words++;
    while (*words && *words != ' ' && n < 23) w[n++] = *words++;
    w[n] = 0;
    if (n && !find_word(s, w)) return 0;
  }
  return 1;
}

static int starts_with(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static void put(char *out, int max, const char *s) {
  int n = strlen(s);
  if (n > max - 1) n = max - 1;
  memcpy(out, s, n);
  out[n] = 0;
}

/* ------------------------------------------------------------------ numbers */
static void fmt_uint(unsigned long long v, char *b) {
  char t[24];
  int n = 0;
  do { t[n++] = '0' + v % 10; v /= 10; } while (v);
  while (n) *b++ = t[--n];
  *b = 0;
}

/* Up to 10 significant digits, no trailing zeros, scientific for very big/small. */
static void fmt_num(double v, char *b) {
  if (v < 0) { *b++ = '-'; v = -v; }
  if (v == 0) { strcpy(b, "0"); return; }
  if (v >= 1e15 || v < 1e-6) {
    int e = (int)floor(log10(v));
    double m = v / pow(10, e);
    if (m >= 9.99999995) { m /= 10; e++; }
    fmt_num(m, b);
    b += strlen(b);
    *b++ = 'e';
    if (e < 0) { *b++ = '-'; e = -e; }
    fmt_uint(e, b);
    return;
  }
  int int_digits = v < 1 ? 1 : (int)floor(log10(v)) + 1;
  int dec = 10 - int_digits;
  if (dec < 0) dec = 0;
  if (dec > 9) dec = 9;
  double scale = pow(10, dec);
  unsigned long long all = (unsigned long long)llround(v * scale);
  unsigned long long ip = all / (unsigned long long)scale, fp = all % (unsigned long long)scale;
  fmt_uint(ip, b);
  b += strlen(b);
  if (fp) {
    char d[16];
    for (int i = dec - 1; i >= 0; i--) { d[i] = '0' + fp % 10; fp /= 10; }
    int n = dec;
    while (n > 0 && d[n - 1] == '0') n--;
    *b++ = '.';
    memcpy(b, d, n);
    b[n] = 0;
  }
}

/* ------------------------------------------------------------------ maths */
/* Tokens: numbers, + - * / ^ ( ), sqrt, pi, postfix squared/cubed/percent.
 * Words like "plus", "times", "divided by", "square root of" become symbols,
 * filler words ("what is", "calculate", "the") are skipped, anything else = not maths. */
enum { T_NUM, T_OP, T_SQRT, T_PI, T_SQ, T_CUBE, T_PCT, T_END };
typedef struct { int type; char op; double v; } tok_t;
#define MAXTOK 40
typedef struct { tok_t t[MAXTOK]; int n, i, err; } mexpr_t;

static int add_tok(mexpr_t *e, int type, char op, double v) {
  if (e->n >= MAXTOK - 1) return 0;
  e->t[e->n].type = type; e->t[e->n].op = op; e->t[e->n].v = v;
  e->n++;
  return 1;
}

static const char *const FILLER[] = {"what", "whats", "is", "how", "much", "calculate", "compute", "solve",
                                     "please", "equals", "equal", "the", "of", "to", "by", "can", "you",
                                     "tell", "me", "result", "answer", "value", "do", "does", "make", "give",
                                     "and", 0};

static int is_filler(const char *w, int n) {
  for (int i = 0; FILLER[i]; i++)
    if ((int)strlen(FILLER[i]) == n && !strncmp(FILLER[i], w, n)) return 1;
  return 0;
}

static int word_is(const char *w, int n, const char *x) { return (int)strlen(x) == n && !strncmp(w, x, n); }

static int tokenize(const char *s, mexpr_t *e) {
  e->n = e->i = e->err = 0;
  int ops = 0, nums = 0;
  const char *p = s;
  while (*p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == ' ' || c == '?' || c == '=' || c == '!' || c == ',' || c == '\'' || c == ':') { p++; continue; }
    if ((c >= '0' && c <= '9') || (c == '.' && p[1] >= '0' && p[1] <= '9')) {
      double v = 0, frac = 0.1;
      int dot = 0;
      while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
        if (*p == '.') dot = 1;
        else if (dot) { v += (*p - '0') * frac; frac /= 10; }
        else v = v * 10 + (*p - '0');
        p++;
      }
      if (*p == '.') p++; /* full stop after a number */
      if (!add_tok(e, T_NUM, 0, v)) return 0;
      nums++;
      continue;
    }
    if (c == '.') { p++; continue; }
    if (strchr("+-*/^()", c)) { if (!add_tok(e, T_OP, c, 0)) return 0; ops++; p++; continue; }
    if (c == '%') { if (!add_tok(e, T_PCT, 0, 0)) return 0; ops++; p++; continue; }
    if (c >= 'a' && c <= 'z') {
      char w[24];
      int n = 0;
      while (((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) && n < 23) {
        w[n++] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
        p++;
      }
      w[n] = 0;
      int type = T_OP;
      char op = 0;
      if (word_is(w, n, "plus") || word_is(w, n, "add") || word_is(w, n, "added")) op = '+';
      else if (word_is(w, n, "minus") || word_is(w, n, "subtract")) op = '-';
      else if (word_is(w, n, "times") || word_is(w, n, "x") || word_is(w, n, "multiplied")) op = '*';
      else if (word_is(w, n, "divided") || word_is(w, n, "over")) op = '/';
      else if (word_is(w, n, "power") || word_is(w, n, "pow")) op = '^';
      else if (word_is(w, n, "sqrt") || word_is(w, n, "root")) type = T_SQRT;
      else if (word_is(w, n, "square")) {
        /* "square root" -> handled by "root"; "5 square" unusual: skip the word */
        continue;
      } else if (word_is(w, n, "squared")) type = T_SQ;
      else if (word_is(w, n, "cubed")) type = T_CUBE;
      else if (word_is(w, n, "percent")) type = T_PCT;
      else if (word_is(w, n, "pi")) { if (!add_tok(e, T_PI, 0, 0)) return 0; nums++; continue; }
      else if (is_filler(w, n) || word_is(w, n, "s")) continue;
      else return 0; /* a word we don't know: not a maths question */
      if (!add_tok(e, type, op, 0)) return 0;
      ops++;
      continue;
    }
    return 0;
  }
  add_tok(e, T_END, 0, 0);
  int has_pi = 0;
  for (int i = 0; i < e->n; i++) has_pi |= e->t[i].type == T_PI;
  return nums >= 1 && (ops >= 1 || (has_pi && nums >= 2));
}

static double m_expr(mexpr_t *e);

static int starts_value(const tok_t *t) {
  return t->type == T_NUM || t->type == T_PI || t->type == T_SQRT || (t->type == T_OP && t->op == '(');
}

static double m_unary(mexpr_t *e);

static double m_primary(mexpr_t *e) {
  tok_t *t = &e->t[e->i];
  if (t->type == T_NUM) { e->i++; return t->v; }
  if (t->type == T_PI) { e->i++; return 3.14159265358979323846; }
  if (t->type == T_SQRT) {
    e->i++;
    double v = m_unary(e);
    if (v < 0) { e->err = 2; return 0; }
    return sqrt(v);
  }
  if (t->type == T_OP && t->op == '(') {
    e->i++;
    double v = m_expr(e);
    if (e->t[e->i].type == T_OP && e->t[e->i].op == ')') e->i++;
    else if (e->t[e->i].type != T_END) e->err = 1; /* a missing ")" at the very end is forgiven */
    return v;
  }
  e->err = 1;
  return 0;
}

static double m_postfix(mexpr_t *e) {
  double v = m_primary(e);
  for (;;) {
    int ty = e->t[e->i].type;
    if (ty == T_SQ) { e->i++; v = v * v; }
    else if (ty == T_CUBE) { e->i++; v = v * v * v; }
    else if (ty == T_PCT) {
      e->i++;
      v /= 100;
      if (starts_value(&e->t[e->i])) v *= m_postfix(e); /* "15% of 80" ("of" was skipped) */
    } else return v;
  }
}

static double m_power(mexpr_t *e) {
  double v = m_postfix(e);
  if (e->t[e->i].type == T_OP && e->t[e->i].op == '^') {
    e->i++;
    double x = m_unary(e); /* right-associative */
    v = pow(v, x);
  }
  return v;
}

static double m_unary(mexpr_t *e) {
  if (e->t[e->i].type == T_OP && e->t[e->i].op == '-') { e->i++; return -m_unary(e); }
  if (e->t[e->i].type == T_OP && e->t[e->i].op == '+') { e->i++; return m_unary(e); }
  return m_power(e);
}

static double m_term(mexpr_t *e) {
  double v = m_unary(e);
  for (;;) {
    tok_t *t = &e->t[e->i];
    if (t->type == T_OP && (t->op == '*' || t->op == '/')) {
      e->i++;
      double r = m_unary(e);
      if (t->op == '*') v *= r;
      else if (r == 0) { e->err = 3; return 0; }
      else v /= r;
    } else if (starts_value(t) && !(t->type == T_OP)) {
      v *= m_unary(e); /* implicit product: "2 pi", "3 sqrt 4" */
    } else if (t->type == T_OP && t->op == '(') {
      v *= m_unary(e); /* "2(3+4)" */
    } else return v;
  }
}

static double m_expr(mexpr_t *e) {
  double v = m_term(e);
  for (;;) {
    tok_t *t = &e->t[e->i];
    if (t->type == T_OP && (t->op == '+' || t->op == '-')) {
      e->i++;
      double r = m_term(e);
      v = t->op == '+' ? v + r : v - r;
    } else return v;
  }
}

/* The typed expression, trimmed of "what is" and "?", if it is only symbols (for "12*7 = 84"). */
static int symbolic_echo(const char *q, char *b, int max) {
  const char *s = q;
  char low[NORM_MAX];
  normalize(q, low);
  static const char *const pre[] = {"what is ", "whats ", "calculate ", "compute ", "how much is ", 0};
  for (int i = 0; pre[i]; i++) {
    int n = strlen(pre[i]);
    if (!strncmp(low, pre[i], n)) {
      /* skip the same number of non-space words in the original */
      int words = 0;
      for (int k = 0; k < n; k++) words += pre[i][k] == ' ';
      while (words && *s) { if (*s == ' ' && s[1] != ' ') words--; s++; }
      while (*s == ' ') s++;
      break;
    }
  }
  int n = 0;
  for (; *s && n < max - 1; s++) {
    char c = *s;
    if (c == '?' || c == '=') continue;
    if (!strchr("0123456789.+-*/^()% sqrtpi", c)) return 0;
    if (c == ' ' && (n == 0 || b[n - 1] == ' ')) continue;
    b[n++] = c;
  }
  while (n && (b[n - 1] == ' ' || b[n - 1] == '.')) n--;
  b[n] = 0;
  return n > 0;
}

static int try_math(const char *q, char *out, int max) {
  mexpr_t e;
  if (!tokenize(q, &e)) return 0;
  double v = m_expr(&e);
  if (e.err == 1 || e.t[e.i].type != T_END) return 0;
  if (e.err == 3) { put(out, max, "You can't divide by zero: that has no answer."); return 1; }
  if (e.err == 2 || isnan(v)) { put(out, max, "That has no real-number answer."); return 1; }
  if (isinf(v)) { put(out, max, "That number is too big for me to show."); return 1; }
  char num[40], echo[64];
  fmt_num(v, num);
  if (symbolic_echo(q, echo, sizeof echo) && strlen(echo) + strlen(num) + 5 < (size_t)max)
    snprintf(out, max, "%s = %s", echo, num);
  else {
    char ex[96];
    int k = 0;
    for (int i = 0; i < e.n && k < 80; i++) {
      tok_t *t = &e.t[i];
      char nb[40];
      switch (t->type) {
        case T_NUM: fmt_num(t->v, nb); k += snprintf(ex + k, sizeof ex - k, "%s", nb); break;
        case T_PI: k += snprintf(ex + k, sizeof ex - k, "%spi", k && ex[k - 1] != ' ' && ex[k - 1] != '(' ? " " : ""); break;
        case T_SQRT: k += snprintf(ex + k, sizeof ex - k, "sqrt "); break;
        case T_SQ: k += snprintf(ex + k, sizeof ex - k, "^2"); break;
        case T_CUBE: k += snprintf(ex + k, sizeof ex - k, "^3"); break;
        case T_PCT: k += snprintf(ex + k, sizeof ex - k, starts_value(&e.t[i + 1]) ? "%% of " : "%%"); break;
        case T_OP:
          if (strchr("+-*/", t->op) && i > 0) k += snprintf(ex + k, sizeof ex - k, " %c ", t->op);
          else k += snprintf(ex + k, sizeof ex - k, "%c", t->op);
          break;
      }
    }
    if (k < 80) snprintf(out, max, "%s = %s", ex, num);
    else snprintf(out, max, "That's %s.", num);
  }
  return 1;
}

/* ------------------------------------------------------------------ primes */
static int first_number(const char *s, unsigned long long *v) {
  while (*s && !(*s >= '0' && *s <= '9')) s++;
  if (!*s) return 0;
  unsigned long long x = 0;
  int d = 0;
  while (*s >= '0' && *s <= '9') { if (++d > 15) return 0; x = x * 10 + (*s++ - '0'); }
  *v = x;
  return 1;
}

static int try_prime(const char *n, char *out, int max) {
  unsigned long long v;
  int is_q = has_all(n, "is prime") || has_all(n, "prime number") || has_all(n, "a prime");
  int fac = find_word(n, "factor*") || find_word(n, "factori*") || find_word(n, "decompos*");
  if (!(is_q || fac) || !first_number(n, &v)) return 0;
  /* the number must be the only number in the question */
  const char *p = n;
  int groups = 0;
  for (int in = 0; *p; p++) { int dg = *p >= '0' && *p <= '9'; if (dg && !in) groups++; in = dg; }
  if (groups != 1) return 0;
  char num[24], b[200];
  fmt_uint(v, num);
  if (v < 2) { snprintf(out, max, "%s is not prime: primes start at 2.", num); return 1; }
  if (v > 1000000000000ULL) { put(out, max, "That number is too big for me to check quickly."); return 1; }
  /* trial division: at most 10^6 steps */
  int len = 0, nf = 0;
  unsigned long long x = v;
  b[0] = 0;
  for (unsigned long long d = 2; d * d <= x; d += (d == 2 ? 1 : 2)) {
    while (x % d == 0) {
      char t[24];
      fmt_uint(d, t);
      len += snprintf(b + len, sizeof b - len, "%s%s", nf ? " x " : "", t);
      nf++;
      x /= d;
      if (len > 150) break;
    }
  }
  if (x > 1 && nf) {
    char t[24];
    fmt_uint(x, t);
    snprintf(b + len, sizeof b - len, " x %s", t);
  }
  if (nf == 0) snprintf(out, max, "Yes, %s is a prime number.", num);
  else if (is_q && !fac) snprintf(out, max, "No, %s is not prime: %s = %s.", num, num, b);
  else snprintf(out, max, "%s = %s", num, b);
  return 1;
}

/* ------------------------------------------------------------------ units */
typedef struct { const char *names; char dim; double f; } unit_t; /* f = value in base unit */
static const unit_t UNITS[] = {
    {"km kilometer kilometre kilometers kilometres", 'L', 1000},
    {"m meter metre meters metres", 'L', 1},
    {"cm centimeter centimetre centimeters centimetres", 'L', 0.01},
    {"mm millimeter millimetre millimeters millimetres", 'L', 0.001},
    {"mile miles mi", 'L', 1609.344},
    {"yard yards yd", 'L', 0.9144},
    {"foot feet ft", 'L', 0.3048},
    {"inch inches", 'L', 0.0254},
    {"kg kilo kilos kilogram kilograms kilogramme", 'M', 1},
    {"g gram grams gramme", 'M', 0.001},
    {"mg milligram milligrams", 'M', 0.000001},
    {"t ton tons tonne tonnes", 'M', 1000},
    {"lb lbs pound pounds", 'M', 0.45359237},
    {"oz ounce ounces", 'M', 0.028349523125},
    {"l liter litre liters litres", 'V', 1},
    {"ml milliliter millilitre milliliters millilitres", 'V', 0.001},
    {"cl centiliter centilitre centiliters centilitres", 'V', 0.01},
    {"gallon gallons gal", 'V', 3.785411784},
    {"s sec second seconds secs", 'T', 1},
    {"min minute minutes mins", 'T', 60},
    {"h hr hour hours hrs", 'T', 3600},
    {"day days", 'T', 86400},
    {"week weeks", 'T', 604800},
    {"month months", 'T', 2629800},
    {"year years yr", 'T', 31557600},
    {"century centuries", 'T', 3155760000.0},
    {"c celsius centigrade degc", 'C', 0},
    {"f fahrenheit degf", 'C', 1},
    {"k kelvin kelvins", 'C', 2},
};
#define NUNITS (int)(sizeof UNITS / sizeof UNITS[0])

static int unit_at(const char *w, int n) {
  for (int u = 0; u < NUNITS; u++) {
    const char *p = UNITS[u].names;
    while (*p) {
      int k = 0;
      while (p[k] && p[k] != ' ') k++;
      if (k == n && !strncmp(p, w, n)) return u;
      p += k;
      while (*p == ' ') p++;
    }
  }
  return -1;
}

/* reads the next word of s at *p into w; returns its length (0 at the end) */
static int next_word(const char **p, char *w) {
  while (**p == ' ') (*p)++;
  int n = 0;
  while (**p && **p != ' ' && n < 23) w[n++] = *(*p)++;
  w[n] = 0;
  return n;
}

static double to_base(int u, double v) {
  if (UNITS[u].dim != 'C') return v * UNITS[u].f;
  if (UNITS[u].f == 1) return (v - 32) * 5 / 9;
  if (UNITS[u].f == 2) return v - 273.15;
  return v;
}

static double from_base(int u, double v) {
  if (UNITS[u].dim != 'C') return v / UNITS[u].f;
  if (UNITS[u].f == 1) return v * 9 / 5 + 32;
  if (UNITS[u].f == 2) return v + 273.15;
  return v;
}

static const char *unit_name(int u, double v) {
  static const char *const NAME1[] = {"km", "m", "cm", "mm", "mile", "yard", "foot", "inch", "kg", "g", "mg",
                                      "tonne", "pound", "ounce", "liter", "ml", "cl", "gallon", "second",
                                      "minute", "hour", "day", "week", "month", "year", "century",
                                      "degrees C", "degrees F", "kelvin"};
  static const char *const NAMES[] = {"km", "m", "cm", "mm", "miles", "yards", "feet", "inches", "kg", "g",
                                      "mg", "tonnes", "pounds", "ounces", "liters", "ml", "cl", "gallons",
                                      "seconds", "minutes", "hours", "days", "weeks", "months", "years",
                                      "centuries", "degrees C", "degrees F", "kelvin"};
  return v == 1 ? NAME1[u] : NAMES[u];
}

/* "10 km in miles", "convert 5 feet to cm", "100 f to c", "how many cm in an inch",
 * "how many seconds are in a day" */
static int try_units(const char *n, char *out, int max) {
  char w[24];
  const char *p = n;
  double v = 1;
  int from = -1, to = -1;
  if (starts_with(n, "how many ")) {
    p = n + 9;
    int k = next_word(&p, w);
    if ((to = unit_at(w, k)) < 0) return 0;
    k = next_word(&p, w);
    if (word_is(w, k, "are") || word_is(w, k, "is") || word_is(w, k, "there")) k = next_word(&p, w);
    if (word_is(w, k, "are")) k = next_word(&p, w);
    if (!word_is(w, k, "in")) return 0;
    k = next_word(&p, w);
    if (word_is(w, k, "a") || word_is(w, k, "an") || word_is(w, k, "one")) k = next_word(&p, w);
    else if (k && w[0] >= '0' && w[0] <= '9') { v = 0; for (int i = 0; i < k; i++) v = v * 10 + (w[i] - '0'); k = next_word(&p, w); }
    if ((from = unit_at(w, k)) < 0) return 0;
    if (next_word(&p, w)) return 0;
  } else {
    /* skip a lead-in: "convert", "what is", "how much is" */
    static const char *const lead[] = {"convert ", "what is ", "whats ", "how much is ", "how many is ", 0};
    for (int i = 0; lead[i]; i++)
      if (starts_with(p, lead[i])) { p += strlen(lead[i]); break; }
    int k = next_word(&p, w);
    if (!k || !(w[0] >= '0' && w[0] <= '9')) return 0;
    /* number: normalize() turned "1.5" into "1 5" — re-read from the raw digits is not possible
     * here, so decimals are handled by the caller passing a dotted form (see helper_reply) */
    v = 0;
    double frac = 0;
    int i = 0;
    for (; i < k && w[i] >= '0' && w[i] <= '9'; i++) v = v * 10 + (w[i] - '0');
    if (i < k && w[i] == '.') {
      double f = 0.1;
      for (i++; i < k && w[i] >= '0' && w[i] <= '9'; i++) { frac += (w[i] - '0') * f; f /= 10; }
    }
    v += frac;
    if (i < k) { /* "10km" */
      if ((from = unit_at(w + i, k - i)) < 0) return 0;
    } else {
      k = next_word(&p, w);
      if (word_is(w, k, "degrees") || word_is(w, k, "degree")) k = next_word(&p, w);
      if ((from = unit_at(w, k)) < 0) return 0;
    }
    k = next_word(&p, w);
    if (!(word_is(w, k, "in") || word_is(w, k, "to") || word_is(w, k, "into") || word_is(w, k, "as"))) return 0;
    k = next_word(&p, w);
    if (word_is(w, k, "degrees") || word_is(w, k, "degree")) k = next_word(&p, w);
    if ((to = unit_at(w, k)) < 0) return 0;
    if (next_word(&p, w)) return 0;
  }
  if (UNITS[from].dim != UNITS[to].dim) {
    snprintf(out, max, "I can't convert %s into %s: they measure different things.", unit_name(from, 2),
             unit_name(to, 2));
    return 1;
  }
  double r = from_base(to, to_base(from, v));
  char a[40], b[40];
  fmt_num(v, a);
  fmt_num(fabs(r) < 1e-9 ? 0 : r, b);
  snprintf(out, max, "%s %s = %s %s", a, unit_name(from, v), b, unit_name(to, r));
  return 1;
}

/* ------------------------------------------------------------------ entry */
int helper_reply(const char *q, char *out, int max, uint32_t rnd) {
  (void)rnd;
  char n[NORM_MAX];
  normalize(q, n);
  if (!n[0]) return 0;
  if (try_math(q, out, max)) return 1;
  if (try_prime(n, out, max)) return 1;
  /* units read numbers from a copy that keeps decimal points */
  char u[NORM_MAX];
  int k = 0;
  for (const char *p = q; *p && k < NORM_MAX - 1; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == '\'') continue;
    int keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               (c == '.' && k && u[k - 1] >= '0' && u[k - 1] <= '9' && p[1] >= '0' && p[1] <= '9');
    if (!keep) c = ' ';
    if (c == ' ' && (k == 0 || u[k - 1] == ' ')) continue;
    u[k++] = c;
  }
  while (k && u[k - 1] == ' ') k--;
  u[k] = 0;
  if (try_units(u, out, max)) return 1;
  return 0;
}
