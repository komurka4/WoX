/*
 * The character view family's private interface (Phase 250): the view (44AE6), a statistic's details (428F1),
 * the items dialog (44F92) and its options (3BAEE), which only these four units call among themselves
 * (game_view.c, game_details.c, game_shop.c, game_item_options.c). Only these units include it. The session
 * sees the dialog's entry items_begin with its modes and owners, the view's entry and the shop's keys
 * (game_internal.h).
 */
#ifndef XEEN_GAME_ITEMS_H
#define XEEN_GAME_ITEMS_H
#include "game_internal.h"

/* game_shop.c: an item row's removal, sort and proficiency text; the dialog's reset */
void item_clear(xeen_game *g, uint16_t record, unsigned cat, unsigned i);
void item_not_proficient(xeen_game *g, unsigned cls, unsigned id, unsigned cat, char *out, size_t n);
void item_sort(xeen_game *g, uint16_t record, unsigned cat);
xeen_status items_reset(xeen_game *g);

/* Which option's 2937A confirm is open (g->option_owner, written by option_confirm and read by option_bought when
 * it is answered): the native's dispatch of the answer; 3BAEE's action (option_action) is a separate value. */
enum option_owner {
    OPTION_OWNER_BUY = 7,      /* 3C266 */
    OPTION_OWNER_SELL = 8,     /* 3BE64 */
    OPTION_OWNER_FIX = 9,      /* 3BF9D */
    OPTION_OWNER_DISCARD = 10, /* 3B958 */
    OPTION_OWNER_IDENTIFY = 11 /* 3C199 */
};

/* game_item_options.c: an option's start and Who-will key, its return, Identify's close */
xeen_status identify_closed(xeen_game *g);
xeen_status items_after_option(xeen_game *g, int ax);
xeen_status option_begin(xeen_game *g, int action);
xeen_status option_which_key(xeen_game *g, uint16_t key);

/* game_view.c: the view's keys (and the exchange's), its tick, the items dialog's return */
xeen_status exchange_key(xeen_game *g, uint16_t key);
xeen_status view_after_items(xeen_game *g, uint16_t record);
void view_buttons(xeen_game *g);
uint16_t view_age(const xeen_movement *m, const uint8_t *ch, int natural);
xeen_status view_key(xeen_game *g, uint16_t key);
xeen_status view_tick(xeen_game *g, xeen_timer_bios bios, void *user);

/* game_details.c: a statistic's details and their end, the awards' keys */
xeen_status awards_key(xeen_game *g, uint16_t key);
xeen_status details_end(xeen_game *g);
xeen_status details_begin(xeen_game *g, uint16_t record, unsigned stat);

#endif
