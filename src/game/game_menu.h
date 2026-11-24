/*
 * The title menu family's private interface (Phase 250): the menu's frames and headers, the load dialog, the
 * CUR/slot pairs, the save-file copies and the new game's slot, which only the five menu units call among
 * themselves (game_menu.c, game_menu_load.c, game_pairs.c, game_newgame.c, game_save.c). Only these units
 * include it. The session sees the menu's entry and resume, the panel's Save and the shared name entry's
 * newgame_preference (game_internal.h).
 */
#ifndef XEEN_GAME_MENU_H
#define XEEN_GAME_MENU_H
#include "game_internal.h"

/* game_menu.c: DOS free memory, the roster buffer, save headers and slot names, the title entry and exit, menu sprites */
void dos_free_memory(xeen_movement *m);
/* The roster buffer B41C:B41A before a roster/save read (19A0D): F42C:0 (the VOC memory as scratch) when F42C is
 * set, else the static 50D4:1C7E. */
void roster_buffer(xeen_movement *m);
int menu_header(xeen_game *g, const char *name, int full, xeen_save_header *h);
void menu_name(xeen_game *g, uint16_t format, const char *prefix, unsigned number);
xeen_status menu_title_entry(xeen_game *g);
uint32_t menu_exit(xeen_game *g);
int menu_sprite(xeen_game *g, uint16_t word, const char *name);

/* game_menu_load.c: the load dialog (2B117) and its slots, the menu tail, no saves */
uint32_t dialog_slots(xeen_game *g);
xeen_status menu_tail(xeen_game *g);
xeen_status dialog_begin(xeen_game *g);
xeen_status dialog_key(xeen_game *g);
xeen_status menu_no_saves(xeen_game *g);

/* game_pairs.c: the CUR/slot pair check, scan and continuation, the CUR creation */
uint32_t menu_pair_check(xeen_game *g);
int cur_create(xeen_game *g);
xeen_status pair_continue(xeen_game *g);
uint32_t pair_scan(xeen_game *g);

/* game_newgame.c: the new game and its slot */
xeen_status menu_new(xeen_game *g);
xeen_status newgame_slot(xeen_game *g);

/* game_save.c: a CUR image into a slot */
int cur_to_slot(xeen_game *g, const uint8_t *cur, size_t n);

#endif
