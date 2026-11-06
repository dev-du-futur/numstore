#include "nwai.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HDR_INTS 10
void (*nwai_trace)(const char *label) = 0;
static void trace(const char *s, int l) {
  if (!nwai_trace) return;
  char b[24];
  int n = 0;
  if (l >= 0) { b[n++] = 'L'; b[n++] = '0' + l; b[n++] = ' '; }
  while (*s && n < 23) b[n++] = *s++;
  b[n] = 0;
  nwai_trace(b);
}
#define QX_MAX 1024 /* largest dim or hidden size the integer matvec buffers hold */

#ifdef __arm__
_Static_assert(sizeof(kv_t) == 2, "build with -mfp16-format=ieee so the KV cache uses half floats");
#endif

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static size_t align4(size_t x) { return (x + 3) & ~(size_t)3; }

/* Float load that works at any address (the model sits wherever the OS put it). */
static inline float ldf(const uint8_t *p) {
  uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  float f;
  memcpy(&f, &u, 4);
  return f;
}

static uint32_t fnv(const uint8_t *s, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ s[i]) * 16777619u;
  return h;
}

static void dims(const uint8_t *b, int *h) { for (int i = 0; i < HDR_INTS; i++) h[i] = rd32(b + 4 + 4 * i); }

size_t nwai_ram_needed(const void *blob, int ctx) {
  int h[HDR_INTS]; dims(blob, h);
  int dim = h[1], hidden = h[2], layers = h[3], heads = h[4], kvh = h[5], vocab = h[6], seq = h[7];
  if (ctx <= 0 || ctx > seq) ctx = seq;
  int kvdim = dim / heads * kvh;
  size_t f = (size_t)dim * 4 + (size_t)hidden * 2 + (size_t)heads * ctx + vocab;
  size_t tok = (size_t)vocab * (sizeof(void *) + 1 + 4) + 2 * 2048 * 2;
  return f * 4 + tok + (size_t)layers * sizeof(layer_t) +
         2 * (size_t)layers * ctx * kvdim * sizeof(kv_t);
}

static qmat_t take_q(const uint8_t *b, size_t *off, size_t n, int group, int bits) {
  qmat_t m;
  m.bits = bits;
  m.group = group;
  m.scale = b + *off;
  if (bits == 4) { /* blocks of [float16 scale][group/2 bytes] */
    m.q = (const int8_t *)(b + *off);
    *off = align4(*off + n / group * (2 + group / 2));
    return m;
  }
  *off += n / group * 4; /* float32 scale table, then int8 values */
  m.q = (const int8_t *)(b + *off);
  *off = align4(*off + n);
  return m;
}

static const uint8_t *take_f(const uint8_t *b, size_t *off, size_t n) {
  const uint8_t *p = b + *off;
  *off += n * 4;
  return p;
}

static int find_piece(nwai_t *m, const uint8_t *s, int n) {
  uint32_t i = fnv(s, n) & (m->hash_size - 1);
  while (m->hash[i] != 0xffff) {
    int t = m->hash[i];
    if (m->piece_len[t] == n && memcmp(m->piece[t], s, n) == 0) return t;
    i = (i + 1) & (m->hash_size - 1);
  }
  return -1;
}

int nwai_init(nwai_t *m, const void *blob, size_t size, int ctx) {
  const uint8_t *b = blob;
  if (size < 48 || memcmp(b, "NWAI", 4) != 0) return 1;
  int h[HDR_INTS]; dims(b, h);
  if (h[0] != 1 && h[0] != 2) return 2;
  if (h[1] > QX_MAX || h[2] > QX_MAX || h[8] < 16 || h[8] % 8) return 6;
  memset(m, 0, sizeof *m);
  m->blob_addr = (uintptr_t)b;
  m->version = h[0];
  m->dim = h[1]; m->hidden = h[2]; m->layers = h[3]; m->heads = h[4]; m->kv_heads = h[5];
  m->vocab = h[6]; m->seq = h[7]; m->group = h[8]; m->max_piece = h[9];
  m->ctx = (ctx <= 0 || ctx > m->seq) ? m->seq : ctx;
  size_t off = 4 + 4 * HDR_INTS;
  m->bits = 8;
  m->emb_group = m->group;
  if (m->version == 2) { /* two more ints: embedding group, layer bits */
    m->emb_group = rd32(b + off);
    m->bits = rd32(b + off + 4);
    off += 8;
    if (m->bits != 4 && m->bits != 8) return 2;
    if (m->emb_group < 16 || m->emb_group % 8) return 6;
  }

  /* tokenizer */
  int V = m->vocab;
  m->piece = malloc(V * sizeof *m->piece);
  m->piece_len = malloc(V);
  m->score = malloc(V * sizeof(float));
  m->hash_size = 2048;
  while (m->hash_size < 2 * V) m->hash_size *= 2;
  m->hash = malloc(m->hash_size * 2);
  if (!m->piece || !m->piece_len || !m->score || !m->hash) return 3;
  memset(m->hash, 0xff, m->hash_size * 2);
  for (int i = 0; i < 256; i++) m->byte_tok[i] = -1;
  m->tok_u = m->tok_b = m->tok_eos = -1;
  for (int t = 0; t < V; t++) {
    memcpy(&m->score[t], b + off, 4);
    int n = b[off + 4];
    m->piece[t] = b + off + 5;
    m->piece_len[t] = n;
    off += 5 + n;
    if (n == 0) continue;
    if (n == 1) { /* best-scoring single-byte piece wins (real pieces beat byte fallback) */
      int c = m->piece[t][0];
      if (m->byte_tok[c] < 0 || m->score[t] > m->score[m->byte_tok[c]]) m->byte_tok[c] = t;
      if (m->score[t] <= -1e5f) continue; /* byte-fallback pieces stay out of the merge table */
    }
    if (n == 3 && memcmp(m->piece[t], "<u>", 3) == 0) m->tok_u = t;
    if (n == 3 && memcmp(m->piece[t], "<b>", 3) == 0) m->tok_b = t;
    if (find_piece(m, m->piece[t], n) < 0) {
      uint32_t i = fnv(m->piece[t], n) & (m->hash_size - 1);
      while (m->hash[i] != 0xffff) i = (i + 1) & (m->hash_size - 1);
      m->hash[i] = t;
    }
  }
  m->tok_eos = 2;
  if (m->tok_u < 0 || m->tok_b < 0) return 4;
  off = align4(off);

  /* weights */
  int D = m->dim, H = m->hidden, hd = D / m->heads, kvd = hd * m->kv_heads, G = m->group, B = m->bits;
  m->emb = take_q(b, &off, (size_t)V * D, m->emb_group, 8);
  m->L = malloc(m->layers * sizeof(layer_t));
  if (!m->L) return 3;
  for (int l = 0; l < m->layers; l++) {
    layer_t *L = &m->L[l];
    L->n1 = take_f(b, &off, D);
    L->wq = take_q(b, &off, (size_t)D * D, G, B);
    L->wk = take_q(b, &off, (size_t)kvd * D, G, B);
    L->wv = take_q(b, &off, (size_t)kvd * D, G, B);
    L->wo = take_q(b, &off, (size_t)D * D, G, B);
    L->n2 = take_f(b, &off, D);
    L->w1 = take_q(b, &off, (size_t)H * D, G, B);
    L->w3 = take_q(b, &off, (size_t)H * D, G, B);
    L->w2 = take_q(b, &off, (size_t)D * H, G, B);
  }
  m->norm = take_f(b, &off, D);
  if (off > size) return 5;

  /* run state */
  m->x = malloc(D * 4); m->xb = malloc(D * 4); m->xb2 = malloc(D * 4); m->q = malloc(D * 4);
  m->hb = malloc(H * 4); m->hb2 = malloc(H * 4);
  m->att = malloc((size_t)m->heads * m->ctx * 4);
  m->logits = malloc(V * 4);
  size_t kvn = (size_t)m->layers * m->ctx * kvd;
  m->kcache = malloc(kvn * sizeof(kv_t));
  m->vcache = malloc(kvn * sizeof(kv_t));
  if (!m->x || !m->xb || !m->xb2 || !m->q || !m->hb || !m->hb2 || !m->att || !m->logits ||
      !m->kcache || !m->vcache) return 3;
  m->pos = 0;
  return 0;
}

void nwai_reset(nwai_t *m) { m->pos = 0; }

static void rmsnorm(float *o, const float *x, const uint8_t *w, int n) {
  float ss = 0;
  for (int i = 0; i < n; i++) ss += x[i] * x[i];
  ss = 1.0f / sqrtf(ss / n + 1e-5f);
  for (int i = 0; i < n; i++) o[i] = x[i] * ss * ldf(w + 4 * i);
}

/* Matrix-vector products in integers.
 * The calculator is limited by how fast it can read weights from flash (about 51 MB/s), and the
 * DSP instruction SMLAD (two 16-bit multiply-adds at once) keeps up with that, where float maths
 * reached only 70% of it. So the input vector is rounded to int8 too, in groups of `group` values
 * with one float scale each, and stored pre-split into even/odd 16-bit pairs for SMLAD.
 * The PC build runs the exact same integer arithmetic in plain C, so results match bit for bit. */
static uint32_t qx_even[QX_MAX / 4], qx_odd[QX_MAX / 4];
static float qx_scale[QX_MAX / 8];
static int32_t qx_sum[QX_MAX / 8]; /* sum of each group's int8 values: 4-bit weights are stored +8 */

static void quantize(const float *x, int n, int group) {
  for (int g = 0; g < n / group; g++) {
    const float *xg = x + g * group;
    float mx = 0;
    for (int i = 0; i < group; i++) { float a = fabsf(xg[i]); if (a > mx) mx = a; }
    float sc = mx / 127.0f, inv = mx > 0 ? 127.0f / mx : 0;
    qx_scale[g] = sc;
    int32_t gs = 0;
    for (int i = 0; i < group; i += 4) {
      int32_t v[4];
      for (int k = 0; k < 4; k++) {
        float f = xg[i + k] * inv;
        v[k] = (int32_t)(f + (f >= 0 ? 0.5f : -0.5f));
        gs += v[k];
      }
      int j = (g * group + i) / 4;
      qx_even[j] = (uint16_t)(int16_t)v[0] | ((uint32_t)(uint16_t)(int16_t)v[2] << 16);
      qx_odd[j] = (uint16_t)(int16_t)v[1] | ((uint32_t)(uint16_t)(int16_t)v[3] << 16);
    }
    qx_sum[g] = gs;
  }
}

/* IEEE half -> float, at any address */
static inline float ldh(const uint8_t *p) {
  uint16_t h = (uint16_t)(p[0] | (p[1] << 8));
#ifdef __ARM_FP16_FORMAT_IEEE
  __fp16 f;
  memcpy(&f, &h, 2);
  return (float)f;
#else
  uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 0x1f, mnt = h & 0x3ff, u;
  if (e == 0) { /* zero or subnormal */
    float f = mnt * (1.0f / 16777216.0f); /* mnt * 2^-24 */
    return s ? -f : f;
  }
  if (e == 31) u = s | 0x7f800000 | (mnt << 13);
  else u = s | ((e + 112) << 23) | (mnt << 13);
  float f;
  memcpy(&f, &u, 4);
  return f;
#endif
}

#if defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
static inline int32_t dot4(uint32_t w, uint32_t xe, uint32_t xo, int32_t acc) {
  acc = __smlad(__sxtb16(w), xe, acc);                     /* bytes 0 and 2 */
  return __smlad(__sxtb16((w >> 8) | (w << 24)), xo, acc); /* bytes 1 and 3 */
}
/* UXTB16 with its built-in rotate (GCC won't fold a separate ROR into it) */
static inline uint32_t uxtb16_ror8(uint32_t x) {
  uint32_t r;
  __asm__("uxtb16 %0, %1, ror #8" : "=r"(r) : "r"(x));
  return r;
}
/* 8 unsigned nibbles (values 0-3 in the low nibbles, 4-7 in the high ones) times 8 int8 inputs */
static inline int32_t dot8u4(uint32_t w, const uint32_t *xe, const uint32_t *xo, int32_t acc) {
  uint32_t lo = w & 0x0F0F0F0Fu, hi = (w >> 4) & 0x0F0F0F0Fu;
  acc = __smlad(__uxtb16(lo), xe[0], acc);      /* values 0 and 2 */
  acc = __smlad(uxtb16_ror8(lo), xo[0], acc); /* values 1 and 3 (rotate is free in UXTB16) */
  acc = __smlad(__uxtb16(hi), xe[1], acc);      /* values 4 and 6 */
  return __smlad(uxtb16_ror8(hi), xo[1], acc); /* values 5 and 7 */
}
#else
static inline int32_t dot4(uint32_t w, uint32_t xe, uint32_t xo, int32_t acc) {
  int8_t b0 = (int8_t)w, b1 = (int8_t)(w >> 8), b2 = (int8_t)(w >> 16), b3 = (int8_t)(w >> 24);
  return acc + b0 * (int16_t)xe + b2 * (int16_t)(xe >> 16) + b1 * (int16_t)xo + b3 * (int16_t)(xo >> 16);
}
static inline int32_t dot8u4(uint32_t w, const uint32_t *xe, const uint32_t *xo, int32_t acc) {
  int v[8];
  for (int k = 0; k < 4; k++) { v[k] = (w >> (8 * k)) & 15; v[4 + k] = (w >> (8 * k + 4)) & 15; }
  return acc + v[0] * (int16_t)xe[0] + v[2] * (int16_t)(xe[0] >> 16) + v[1] * (int16_t)xo[0] +
         v[3] * (int16_t)(xo[0] >> 16) + v[4] * (int16_t)xe[1] + v[6] * (int16_t)(xe[1] >> 16) +
         v[5] * (int16_t)xo[1] + v[7] * (int16_t)(xo[1] >> 16);
}
#endif

/* 4-bit version: half the bytes to read from flash. sum((u-8)*x) = sum(u*x) - 8*sum(x). */
static void matvec4(float *out, const qmat_t *W, int rows, int cols) {
  const uint8_t *q = (const uint8_t *)W->q;
  int group = W->group, ng = cols / group, gw = group / 8;
  for (int r = 0; r < rows; r++) {
    float sum = 0;
    const uint32_t *xe = qx_even, *xo = qx_odd;
    for (int g = 0; g < ng; g++) {
      const uint8_t *s = q; /* this block's scale, then its values */
      q += 2;
      int32_t acc = 0;
      if (gw == 4) { /* groups of 32: fully unrolled, fixed offsets */
        uint32_t w[4];
        memcpy(w, q, 16);
        acc = dot8u4(w[0], xe, xo, acc);
        acc = dot8u4(w[1], xe + 2, xo + 2, acc);
        acc = dot8u4(w[2], xe + 4, xo + 4, acc);
        acc = dot8u4(w[3], xe + 6, xo + 6, acc);
      } else {
        for (int j = 0; j < gw; j++) {
          uint32_t w;
          memcpy(&w, q + 4 * j, 4);
          acc = dot8u4(w, xe + 2 * j, xo + 2 * j, acc);
        }
      }
      sum += (float)(acc - 8 * qx_sum[g]) * ldh(s) * qx_scale[g];
      q += group / 2;
      xe += group / 4;
      xo += group / 4;
    }
    out[r] = sum;
  }
}

/* out[rows] = W[rows x cols] * x, where x was given to quantize() (in W's group size) just before. */
static void matvec8(float *out, const qmat_t *W, int rows, int cols, int group);
static void matvec(float *out, const qmat_t *W, int rows, int cols, int group) {
  if (W->bits == 4) matvec4(out, W, rows, cols);
  else matvec8(out, W, rows, cols, group);
}

static void matvec8(float *out, const qmat_t *W, int rows, int cols, int group) {
  const uint8_t *q = (const uint8_t *)W->q;
  const uint8_t *s = W->scale;
  int ng = cols / group, gw = group / 4;
  for (int r = 0; r < rows; r++) {
    float sum = 0;
    const uint32_t *xe = qx_even, *xo = qx_odd;
    for (int g = 0; g < ng; g++) {
      int32_t acc = 0;
      for (int j = 0; j < gw; j++) {
        uint32_t w;
        memcpy(&w, q + 4 * j, 4); /* a single unaligned load on Cortex-M7 */
        acc = dot4(w, xe[j], xo[j], acc);
      }
      sum += (float)acc * ldf(s) * qx_scale[g];
      s += 4;
      q += group;
      xe += gw;
      xo += gw;
    }
    out[r] = sum;
  }
}

static void rope(float *v, int nheads, int hd, int pos) {
  for (int i = 0; i < hd / 2; i++) {
    float freq = 1.0f / powf(10000.0f, (float)(2 * i) / hd);
    float a = pos * freq, c = cosf(a), s = sinf(a);
    for (int h = 0; h < nheads; h++) {
      float *p = v + h * hd + 2 * i;
      float x1 = p[0], x2 = p[1];
      p[0] = x1 * c - x2 * s;
      p[1] = x1 * s + x2 * c;
    }
  }
}

float *nwai_forward(nwai_t *m, int token) {
  int D = m->dim, H = m->hidden, G = m->group, hd = D / m->heads, kvd = hd * m->kv_heads;
  int pos = m->pos, rep = m->heads / m->kv_heads;
  /* embedding row */
  const int8_t *er = m->emb.q + (size_t)token * D;
  int EG = m->emb_group;
  const uint8_t *es = m->emb.scale + (size_t)token * D / EG * 4;
  for (int i = 0; i < D; i++) m->x[i] = er[i] * ldf(es + 4 * (i / EG));

  trace("embedded", -1);
  float kv[64 * 8];
  for (int l = 0; l < m->layers; l++) {
    layer_t *L = &m->L[l];
    trace("norm", l);
    kv_t *kc = m->kcache + (size_t)l * m->ctx * kvd;
    kv_t *vc = m->vcache + (size_t)l * m->ctx * kvd;
    rmsnorm(m->xb, m->x, L->n1, D);
    quantize(m->xb, D, G);
    trace("wq", l);
    matvec(m->q, &L->wq, D, D, G);
    trace("wk", l);
    matvec(kv, &L->wk, kvd, D, G);
    rope(m->q, m->heads, hd, pos);
    rope(kv, m->kv_heads, hd, pos);
    for (int i = 0; i < kvd; i++) kc[(size_t)pos * kvd + i] = (kv_t)kv[i];
    matvec(kv, &L->wv, kvd, D, G);
    for (int i = 0; i < kvd; i++) vc[(size_t)pos * kvd + i] = (kv_t)kv[i];

    trace("attention", l);
    float inv = 1.0f / sqrtf((float)hd);
    for (int h = 0; h < m->heads; h++) {
      const float *qh = m->q + h * hd;
      float *att = m->att + h * m->ctx;
      int ko = (h / rep) * hd;
      float mx = -1e30f;
      for (int t = 0; t <= pos; t++) {
        const kv_t *k = kc + (size_t)t * kvd + ko;
        float s = 0;
        for (int i = 0; i < hd; i++) s += qh[i] * (float)k[i];
        s *= inv;
        att[t] = s;
        if (s > mx) mx = s;
      }
      float sum = 0;
      for (int t = 0; t <= pos; t++) { att[t] = expf(att[t] - mx); sum += att[t]; }
      float *o = m->xb + h * hd;
      for (int i = 0; i < hd; i++) o[i] = 0;
      for (int t = 0; t <= pos; t++) {
        const kv_t *v = vc + (size_t)t * kvd + ko;
        float a = att[t] / sum;
        for (int i = 0; i < hd; i++) o[i] += a * (float)v[i];
      }
    }
    trace("wo", l);
    quantize(m->xb, D, G);
    matvec(m->xb2, &L->wo, D, D, G);
    for (int i = 0; i < D; i++) m->x[i] += m->xb2[i];

    rmsnorm(m->xb, m->x, L->n2, D);
    quantize(m->xb, D, G);
    trace("w1", l);
    matvec(m->hb, &L->w1, H, D, G);
    matvec(m->hb2, &L->w3, H, D, G);
    for (int i = 0; i < H; i++) {
      float v = m->hb[i];
      m->hb[i] = v / (1.0f + expf(-v)) * m->hb2[i];
    }
    trace("w2", l);
    quantize(m->hb, H, G);
    matvec(m->xb, &L->w2, D, H, G);
    for (int i = 0; i < D; i++) m->x[i] += m->xb[i];
  }
  rmsnorm(m->x, m->x, m->norm, D);
  trace("logits", -1);
  quantize(m->x, D, EG);
  matvec(m->logits, &m->emb, m->vocab, D, EG);
  trace("token done", -1);
  m->pos++;
  return m->logits;
}

int nwai_encode(nwai_t *m, const char *text, int *out, int max) {
  int n = 0;
  uint8_t buf[64];
  for (const uint8_t *s = (const uint8_t *)text; *s && n < max; s++) {
    /* export.py turned SentencePiece's U+2581 back into ' ', so spaces are plain bytes here */
    int t = m->byte_tok[*s];
    if (t >= 0) out[n++] = t;
  }
  /* greedy BPE: repeatedly merge the adjacent pair with the best score */
  for (;;) {
    float best = -1e30f;
    int bi = -1, bt = -1;
    for (int i = 0; i + 1 < n; i++) {
      int la = m->piece_len[out[i]], lb = m->piece_len[out[i + 1]];
      if (la + lb > (int)sizeof buf || la + lb > m->max_piece) continue;
      memcpy(buf, m->piece[out[i]], la);
      memcpy(buf + la, m->piece[out[i + 1]], lb);
      int t = find_piece(m, buf, la + lb);
      if (t >= 0 && m->score[t] > best) { best = m->score[t]; bi = i; bt = t; }
    }
    if (bi < 0) break;
    out[bi] = bt;
    memmove(out + bi + 1, out + bi + 2, (n - bi - 2) * sizeof(int));
    n--;
  }
  return n;
}

void nwai_penalize(float *logits, const int *recent, int n, float penalty) {
  for (int i = 0; i < n; i++) {
    float *l = &logits[recent[i]];
    *l = *l > 0 ? *l / penalty : *l * penalty; /* applied once per occurrence */
  }
}

int nwai_sample(nwai_t *m, float *logits, float temp, int topk, float min_p, uint32_t rnd) {
  enum { K = 40 };
  int idx[K];
  float val[K];
  if (topk > K) topk = K;
  if (topk < 1) topk = 1;
  int n = 0;
  for (int t = 0; t < m->vocab; t++) {
    float v = logits[t];
    if (n < topk) {
      int j = n++;
      while (j > 0 && val[j - 1] < v) { val[j] = val[j - 1]; idx[j] = idx[j - 1]; j--; }
      val[j] = v; idx[j] = t;
    } else if (v > val[n - 1]) {
      int j = n - 1;
      while (j > 0 && val[j - 1] < v) { val[j] = val[j - 1]; idx[j] = idx[j - 1]; j--; }
      val[j] = v; idx[j] = t;
    }
  }
  if (temp <= 0) return idx[0];
  /* min-p: keep only pieces at least min_p as likely as the best one (judged before temperature),
   * so a rare piece can't slip into the middle of a word and spell nonsense */
  float sum = 0;
  int keep = 0;
  for (int i = 0; i < n; i++) {
    if (i > 0 && expf(val[i] - val[0]) < min_p) break;
    keep = i + 1;
  }
  for (int i = 0; i < keep; i++) { val[i] = expf((val[i] - val[0]) / temp); sum += val[i]; }
  float r = (rnd >> 8) * (1.0f / 16777216.0f) * sum;
  for (int i = 0; i < keep; i++) { r -= val[i]; if (r <= 0) return idx[i]; }
  return idx[keep - 1];
}
