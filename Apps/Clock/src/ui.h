/* The two screens: setting the time, and the clock itself. `now` is the calculator's millisecond counter. */
#ifndef UI_H
#define UI_H
#include <stdbool.h>
#include <stdint.h>

void ui_init(uint64_t now);
bool ui_frame(uint64_t now, uint64_t hit, uint64_t held);   /* false: quit */

/* for the host test */
bool ui_setting(void);
int64_t ui_wall(uint64_t now);   /* the time shown, or -1 while none is set */
const char *ui_note(int line);   /* the note lines at the bottom of the clock screen */

#endif
