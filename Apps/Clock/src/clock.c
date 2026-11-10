#include "clock.h"

const char *const WEEKDAYS[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const char *const MONTHS[12] = {"January", "February", "March", "April", "May", "June",
                                "July", "August", "September", "October", "November", "December"};

/* Howard Hinnant's algorithms (public domain), proleptic Gregorian calendar */
int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int *y, int *m, int *d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = (int)(yoe + era * 400 + (*m <= 2));
}

int month_days(int y, int m) {
  static const uint8_t n[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : n[m - 1];
}

int64_t wall_from(int y, int m, int d, int hh, int mm, int ss) {
  return days_from_civil(y, m, d) * DAY_MS + ((int64_t)hh * 3600 + mm * 60 + ss) * 1000;
}

when_t when_of(int64_t wall) {
  when_t w;
  int64_t days = wall >= 0 ? wall / DAY_MS : (wall - DAY_MS + 1) / DAY_MS;
  int64_t s = (wall - days * DAY_MS) / 1000;
  civil_from_days(days, &w.year, &w.month, &w.day);
  w.hour = (int)(s / 3600);
  w.minute = (int)(s / 60 % 60);
  w.second = (int)(s % 60);
  w.weekday = (int)(((days + 3) % 7 + 7) % 7);   /* 1970-01-01 was a Thursday */
  return w;
}
