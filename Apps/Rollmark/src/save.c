/* See save.h. Device side: finds Epsilon's file list through the header of the running software.
 *
 * Facts about Epsilon this relies on (its source is public):
 * - the userland header sits at the start of a slot (0x90010000 or 0x90020000 for slot A, +0x400000 for slot B),
 *   starts and ends with 0xDEC0EDFE, and gives the file list's RAM address and size, and the app area;
 * - the file list is a buffer framed by 0xEE0BDDBA; each file is a 16-bit total size, its name ending in a zero,
 *   then its content; a size of 0 ends the list. */
#include "save.h"
#include <string.h>

#define HEADER_MAGIC 0xDEC0EDFEu
#define LIST_MAGIC 0xEE0BDDBAu

typedef struct {
  uint32_t magic;
  char version[8];
  uint32_t list_ram, list_size;
  uint32_t apps_start, apps_end;
  uint32_t apps_ram_start, apps_ram_end;
  uint32_t name_start, name_end;
  uint32_t footer;
} header_t;

static uint32_t rd32(const volatile uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

#ifndef HOST
/* the header of the software this app runs under: the one whose app area holds this code */
static const header_t *header(void) {
  static const uint32_t at[] = {0x90010000u, 0x90020000u, 0x90410000u, 0x90420000u};
  uint32_t self = (uint32_t)(uintptr_t)&header;
  const header_t *found = 0;
  for (int pass = 0; pass < 2 && !found; pass++)   /* pass 1: apps placed past the area (Numstore's extend memory) */
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++) {
      const volatile header_t *h = (const volatile header_t *)(uintptr_t)at[i];
      if (h->magic != HEADER_MAGIC || h->footer != HEADER_MAGIC) continue;
      if (self < h->apps_start || self >= (pass ? 0x90800000u : h->apps_end)) continue;
      if (found) return 0;   /* two candidates: don't guess */
      found = (const header_t *)h;
    }
  return found;
}

static uint8_t *list(uint32_t *size) {
  const header_t *h = header();
  if (!h) return 0;
  uint32_t ram = h->list_ram, n = h->list_size;
  bool ok = (ram >= 0x20000000u && ram + n + 8 <= 0x20040000u) || (ram >= 0x24000000u && ram + n + 8 <= 0x24040000u);
  if (!ok || (ram & 3) || n < 1024 || n > 0x10000) return 0;
  const volatile uint8_t *b = (const volatile uint8_t *)(uintptr_t)ram;
  if (rd32(b) != LIST_MAGIC || rd32(b + 4 + n) != LIST_MAGIC) return 0;
  *size = n;
  return (uint8_t *)(uintptr_t)ram + 4;
}

#else
static uint8_t host_buf[8 + 16384];
static uint8_t *list(uint32_t *size) { *size = 16384; return host_buf + 4; }
uint8_t *save_host_list(uint32_t *size) { return list(size); }
/* a fresh list holding two other files, like scripts the user wrote */
void save_host_reset(void) {
  memset(host_buf, 0, sizeof host_buf);
  uint8_t *b = host_buf + 4;
  const char *names[2] = {"hello.py", "maths.py"};
  int p = 0;
  for (int i = 0; i < 2; i++) {
    uint32_t nl = (uint32_t)strlen(names[i]) + 1, total = 2 + nl + 100;
    wr16(b + p, (uint16_t)total);
    memcpy(b + p + 2, names[i], nl);
    memset(b + p + 2 + nl, 'A' + i, 100);
    p += total;
  }
}
#endif

/* the end of the list (where the 0 size is), or -1 if it doesn't look right */
static int list_end(const uint8_t *b, uint32_t size) {
  uint32_t p = 0;
  while (p + 2 <= size) {
    uint16_t n = rd16(b + p);
    if (!n) return (int)p;
    if (n < 4 || p + n > size) return -1;
    p += n;
  }
  return -1;
}

static int find(const uint8_t *b, int end, const char *name) {
  uint32_t len = (uint32_t)strlen(name) + 1;
  for (int p = 0; p < end; p += rd16(b + p))
    if (rd16(b + p) >= 2 + len && !memcmp(b + p + 2, name, len)) return p;
  return -1;
}

static uint32_t checksum(const uint8_t *d, uint32_t len) {   /* FNV-1a */
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < len; i++) h = (h ^ d[i]) * 16777619u;
  return h;
}

bool save_available(void) {
  uint32_t size;
  uint8_t *b = list(&size);
  return b && list_end(b, size) >= 0;
}

/* content: "RMSV", data length, data, padding, checksum of everything before it (in the last 4 bytes) */
bool save_load(const char *name, uint32_t size, void *data, uint32_t len) {
  uint32_t n;
  uint8_t *b = list(&n);
  if (!b || len + 12 > size) return false;
  int end = list_end(b, n);
  if (end < 0) return false;
  int at = find(b, end, name);
  uint32_t nl = (uint32_t)strlen(name) + 1;
  if (at < 0 || rd16(b + at) != 2 + nl + size) return false;
  const uint8_t *c = b + at + 2 + nl;
  if (memcmp(c, "RMSV", 4) || rd32(c + size - 4) != checksum(c, size - 4)) return false;
  uint32_t have = rd32(c + 4);
  memset(data, 0, len);
  memcpy(data, c + 8, have < len ? have : len);   /* an older, shorter save fills the start */
  return true;
}

bool save_store(const char *name, uint32_t size, const void *data, uint32_t len) {
  uint32_t n;
  uint8_t *b = list(&n);
  uint32_t nl = (uint32_t)strlen(name) + 1, total = 2 + nl + size;
  if (!b || len + 12 > size || total > 0xFFFF) return false;
  int end = list_end(b, n);
  if (end < 0) return false;
  int at = find(b, end, name);
  if (at >= 0 && rd16(b + at) != total) return false;   /* a file of another size: leave it alone */
  if (at < 0) {   /* new: added at the end, nothing else moves */
    if ((uint32_t)end + total + 2 > n) return false;
    at = end;
    memset(b + at + 2, 0, total - 2);
    wr16(b + at + total, 0);
    memcpy(b + at + 2, name, nl);
    wr16(b + at, (uint16_t)total);
  }
  uint8_t *c = b + at + 2 + nl;
  memcpy(c, "RMSV", 4);
  c[4] = (uint8_t)len; c[5] = (uint8_t)(len >> 8); c[6] = (uint8_t)(len >> 16); c[7] = (uint8_t)(len >> 24);
  memcpy(c + 8, data, len);
  memset(c + 8 + len, 0, size - 8 - len - 4);
  uint32_t s = checksum(c, size - 4);
  c[size - 4] = (uint8_t)s; c[size - 3] = (uint8_t)(s >> 8); c[size - 2] = (uint8_t)(s >> 16); c[size - 1] = (uint8_t)(s >> 24);
  return true;
}
