/* Saving in the calculator's own file list (where Python scripts live), as one file per app.
 *
 * The file has a fixed size, chosen once: it is created at the end of the list the first time, then always
 * rewritten in place. Nothing else in the list ever moves, so Epsilon's own files are safe. Each save carries a
 * checksum; a damaged or missing file reads as "no save". The file lives in RAM like scripts do: it survives
 * leaving the app and turning the calculator off, and is lost on a reset. */
#ifndef SAVE_H
#define SAVE_H
#include <stdbool.h>
#include <stdint.h>

/* name like "rollmark.sav"; size = the file's fixed content size, at least len + 12 (12 bytes of framing).
 * A load fills `data` with what was saved (zeros past a shorter, older save). */
bool save_load(const char *name, uint32_t size, void *data, uint32_t len);
bool save_store(const char *name, uint32_t size, const void *data, uint32_t len);
bool save_available(void);   /* false where the file list can't be found (then nothing is ever saved) */

#ifdef HOST
/* the PC test: the file list is a buffer kept in memory, with a few other files in it */
void save_host_reset(void);
uint8_t *save_host_list(uint32_t *size);
#endif

#endif
