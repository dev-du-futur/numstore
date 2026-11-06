/* Built-in helpers: answers TinyChat can get exactly right without the model.
 * Maths ("what is 12*7", "15% of 80", "square root of 2"), unit conversions
 * ("10 km in miles", "how many seconds in a day") and primes ("is 97 prime").
 * Facts are left to the model.
 * Pure C, no EADK: also builds on the PC (host/helpers_test.c). */
#ifndef HELPERS_H
#define HELPERS_H
#include <stdint.h>

/* If the question is one a helper knows, writes the answer to out and returns 1.
 * Returns 0 to let the model answer. rnd is unused (kept for answers with variants). */
int helper_reply(const char *question, char *out, int max, uint32_t rnd);
#endif
