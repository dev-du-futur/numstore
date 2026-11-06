/* Tiny Llama-style chat model inference: int8 weights read straight from the
 * model blob (flash on the calculator), float activations, half-size KV cache.
 * Portable C99: builds for the calculator and for the PC (for testing). */
#ifndef NWAI_H
#define NWAI_H
#include <stddef.h>
#include <stdint.h>

#ifdef __ARM_FP16_FORMAT_IEEE
typedef __fp16 kv_t;
#else
typedef float kv_t;
#endif

/* A quantized matrix, row-major, in groups of `group` values along each row.
 * 8-bit: a table of float32 scales (one per group), then the int8 values.
 * 4-bit: blocks of [float16 scale][group/2 bytes], two values per byte (stored +8, so 1..15).
 *        In each 4-byte word, low nibbles hold values 0-3 and high nibbles values 4-7 of a run of 8.
 *        (scale beside its values: reads stay sequential, which the calculator's flash needs) */
typedef struct {
  const uint8_t *scale; /* possibly unaligned in flash */
  const int8_t *q;
  uint8_t bits, group;
} qmat_t;

typedef struct {
  const uint8_t *n1, *n2; /* float32 arrays, possibly unaligned */
  qmat_t wq, wk, wv, wo, w1, w3, w2;
} layer_t;

typedef struct {
  int dim, hidden, layers, heads, kv_heads, vocab, seq, group, max_piece;
  int version, bits, emb_group; /* v1: all int8. v2: layers in `bits` (4), embedding int8 in emb_group */
  int ctx;                 /* tokens the KV cache holds (<= seq) */
  int tok_u, tok_b, tok_eos;
  /* tokenizer */
  const uint8_t **piece;   /* pointers into the blob */
  uint8_t *piece_len;
  float *score;
  uint16_t *hash;          /* open addressing, 0xffff = empty */
  int hash_size;
  int16_t byte_tok[256];
  /* weights */
  qmat_t emb;
  layer_t *L;
  const uint8_t *norm;
  uintptr_t blob_addr;
  /* run state */
  float *x, *xb, *xb2, *hb, *hb2, *q, *att, *logits;
  kv_t *kcache, *vcache;
  int pos;
} nwai_t;

/* Returns 0 on success. ctx = how many tokens of memory to keep (0 = model max).
 * Allocates with malloc; frees nothing (the app owns the process). */
int nwai_init(nwai_t *m, const void *blob, size_t size, int ctx);
size_t nwai_ram_needed(const void *blob, int ctx);
void nwai_reset(nwai_t *m);
/* Feed one token at the current position; returns logits (vocab floats). */
float *nwai_forward(nwai_t *m, int token);
/* Text -> tokens (no specials added). Returns count written (<= max). */
int nwai_encode(nwai_t *m, const char *text, int *out, int max);
/* Make recently used tokens less likely (penalty > 1), once per occurrence in `recent`. */
void nwai_penalize(float *logits, const int *recent, int n, float penalty);
/* Sample with temperature, top-k and min-p (drop pieces below min_p x the best one's probability).
 * rnd = uniform 32-bit random. temp <= 0 means greedy. */
/* Optional progress callback (debugging on the calculator): called with short labels during
 * nwai_forward when set. */
extern void (*nwai_trace)(const char *label);
int nwai_sample(nwai_t *m, float *logits, float temp, int topk, float min_p, uint32_t rnd);
#endif
