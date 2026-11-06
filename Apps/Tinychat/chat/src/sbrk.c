/* nwlink's linker script reserves [_heap_start, _heap_end) for the app but
 * provides no _sbrk, so newlib's malloc always fails. This hands that area to
 * malloc, but never past the current stack pointer minus STACK_RESERVE:
 * the stack lives in the same RAM and grows down towards the heap. */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#define STACK_RESERVE (24 * 1024)

extern char _heap_start, _heap_end;
static uintptr_t brk_now;

static uintptr_t addr(const void *p) {
  uintptr_t a;
  __asm__ volatile("" : "=r"(a) : "0"(p)); /* hide the symbol from the optimiser */
  return a;
}

void *_sbrk(ptrdiff_t incr) {
  uintptr_t lo = addr(&_heap_start), hi = addr(&_heap_end), sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  if (sp > lo && sp < hi && sp - STACK_RESERVE < hi) hi = sp - STACK_RESERVE; /* stack inside the area */
  if (!brk_now) brk_now = lo;
  uintptr_t next = brk_now + (uintptr_t)incr;
  if (incr < 0 ? next < lo : (next > hi || next < brk_now)) {
    errno = ENOMEM;
    return (void *)-1;
  }
  uintptr_t old = brk_now;
  brk_now = next;
  return (void *)old;
}

/* For the probe: where things are. */
uintptr_t sbrk_info(int what) {
  char *sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  switch (what) {
    case 0: return (uintptr_t)&_heap_start;
    case 1: return (uintptr_t)&_heap_end;
    default: return (uintptr_t)sp;
  }
}
