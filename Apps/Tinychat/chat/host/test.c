/* PC check of the calculator inference code.
 * test model.bin "prompt"         -> greedy reply token ids + text (compare with ref.txt)
 * test model.bin "prompt" chat    -> sampled reply, like on the calculator */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/nwai.h"

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: test model.bin prompt [chat]\n"); return 1; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  char *raw = malloc(size + 4);
  void *blob = raw + (getenv("MISALIGN") ? atoi(getenv("MISALIGN")) : 0);
  if (fread(blob, 1, size, f) != (size_t)size) return 1;
  fclose(f);
  nwai_t m;
  int err = nwai_init(&m, blob, size, 0);
  if (err) { fprintf(stderr, "init error %d\n", err); return 1; }
  printf("dim %d hidden %d layers %d vocab %d ctx %d, RAM %u bytes\n", m.dim, m.hidden, m.layers,
         m.vocab, m.ctx, (unsigned)nwai_ram_needed(blob, 0));
  int ids[512], n = 0, prev_a = 0, prev_b = 0; /* the last bot turn in NW_HIST: ids[prev_a..prev_b) */
  /* NW_HIST="question|answer|question|answer": earlier turns of the chat */
  if (getenv("NW_HIST")) {
    char h[1024];
    strncpy(h, getenv("NW_HIST"), sizeof h - 1);
    h[sizeof h - 1] = 0;
    int who = 0;
    for (char *s = strtok(h, "|"); s; s = strtok(NULL, "|"), who ^= 1) {
      ids[n++] = who ? m.tok_b : m.tok_u;
      int a = n;
      n += nwai_encode(&m, s, ids + n, 100);
      if (who) prev_a = a, prev_b = n;
    }
  }
  ids[n++] = m.tok_u;
  n += nwai_encode(&m, argv[2], ids + n, 400);
  ids[n++] = m.tok_b;
  float *logits = NULL;
  for (int i = 0; i < n; i++) logits = nwai_forward(&m, ids[i]);
  if (getenv("NW_DUMP")) { /* first logits after the prompt, to compare with PyTorch */
    for (int i = 0; i < 8; i++) printf("%.4f ", logits[i]);
    printf("\n");
  }
  int sample = argc > 3;
  srand(getenv("NW_SEED") ? (unsigned)atoi(getenv("NW_SEED")) : (unsigned)time(NULL));
  int start = n;
  while (n < 500) {
    float temp = getenv("NW_TEMP") ? atof(getenv("NW_TEMP")) : 0.7f;
    int topk = getenv("NW_TOPK") ? atoi(getenv("NW_TOPK")) : 20;
    float minp = getenv("NW_MINP") ? atof(getenv("NW_MINP")) : 0.0f;
    float rep = getenv("NW_REP") ? atof(getenv("NW_REP")) : 1.0f;
    if (sample && rep > 1.0f) nwai_penalize(logits, ids + start, n - start, rep);
    /* like the app: the first words of a reply shy away from the previous reply */
    float pp = getenv("NW_PREV") ? atof(getenv("NW_PREV")) : 1.0f;
    if (sample && pp > 1.0f && n - start < 3) nwai_penalize(logits, ids + prev_a, prev_b - prev_a, pp);
    uint32_t rnd = ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ ((uint32_t)rand() >> 13);
    int t = sample ? nwai_sample(&m, logits, temp, topk, minp, rnd) : nwai_sample(&m, logits, 0, 1, 0, 0);
    if (t == m.tok_eos || t == m.tok_u) break;
    ids[n++] = t;
    logits = nwai_forward(&m, t);
    if (n - start >= 40 && !sample) break;
  }
  for (int i = 0; i < n; i++) printf("%d ", ids[i]);
  printf("\n");
  for (int i = start; i < n; i++) fwrite(m.piece[ids[i]], 1, m.piece_len[ids[i]], stdout);
  printf("\n");
  return 0;
}
