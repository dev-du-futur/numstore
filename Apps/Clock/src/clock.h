/* Calendar maths, nothing calculator-specific. Days count from 1970-01-01; wall time is local time in
 * milliseconds since 1970-01-01 00:00, with no time zone or daylight saving (the user sets what they see). */
#ifndef CLOCK_H
#define CLOCK_H
#include <stdbool.h>
#include <stdint.h>

#define DAY_MS 86400000LL

typedef struct { int year, month, day, hour, minute, second, weekday; } when_t;   /* month 1..12, weekday 0 = Monday */

int64_t days_from_civil(int y, int m, int d);
void civil_from_days(int64_t days, int *y, int *m, int *d);
int month_days(int y, int m);
int64_t wall_from(int y, int m, int d, int hh, int mm, int ss);
when_t when_of(int64_t wall_ms);

extern const char *const WEEKDAYS[7];
extern const char *const MONTHS[12];

#endif
