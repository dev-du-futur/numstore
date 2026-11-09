#ifndef UI_H
#define UI_H
#include <stdbool.h>
#include <stdint.h>
#include "roll.h"

#define START_ROLLS 100
#define REFILL_MS 2000     /* one roll back every 2 seconds */
#define MAX_BANK 99999
#define TOP 5

typedef struct {
  long rolls;                     /* rolls made */
  uint64_t total_ep;
  long tiers[N_TIERS];
  long found[N_BADGES];
  roll_t best;                    /* best roll so far (valid when rolls > 0) */
} stats_t;

/* now_ms: the calculator's clock (milliseconds since it started), to give back the rolls earned while away */
void ui_init(uint32_t seed, uint64_t now_ms);
void ui_save(void);                                   /* also done after every roll and every 10 seconds */
bool ui_frame(int ms, uint64_t hit, uint64_t held);   /* false: quit */

/* for the host test */
const stats_t *ui_stats(void);
const roll_t *ui_shown(void);
int ui_screen(void);
long ui_bank(void);
bool ui_busy(void);
bool ui_loaded(void);   /* a save was found at start */
bool ui_bulk_open(void); /* a bulk is still unfolding its rare rolls */
uint64_t ui_balance(void); /* EP to spend at the shop */
int ui_inv(int id);        /* how many of an item the bag holds */
int ui_luck_left(void);    /* rolls the luck potion at work still covers */
bool ui_armed(void);       /* a rarity potion waits for the next roll */
int ui_ware(int k);        /* the shop's ware k, -1: sold out */
#ifdef HOST
void ui_give_ep(uint64_t ep);
void ui_give_item(int id, int n);
#endif
enum { S_MENU, S_ROLL, S_BULK, S_BOOK, S_RECORDS, S_HELP, S_SHOP };

#endif
