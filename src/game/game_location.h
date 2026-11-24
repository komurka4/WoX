/*
 * The location loop's private interface (Phase 249): the entries that only 277E5's location units call among
 * themselves: the loop (game_location.c), its presentation parts (game_location_frames.c) and the services it
 * dispatches by kind (game_bank.c, game_guild.c, game_tavern.c, game_town.c, game_inn.c, game_arena.c). Only these
 * units include it. The session sees the loop's begin, resume, redraw, return and window, the location message,
 * the services' other entries (the amount entry, the guild's Cast list, the inn) and the add-on glue's hooks
 * (game_internal.h).
 */
#ifndef XEEN_GAME_LOCATION_H
#define XEEN_GAME_LOCATION_H
#include "game_internal.h"

/* game_location.c: the location loop's stop */
xeen_status location_stop(xeen_game *g, uint32_t boundary);

/* game_location_frames.c: a location's amounts, music, texts and the keeper's frames */
void location_amount(const xeen_game *g, char *out, size_t n, uint32_t v);
void location_music(xeen_game *g, const char *name, uint16_t flag);
void location_first_frame(xeen_game *g);
void location_frame_cues(xeen_game *g, unsigned kind, int dark, uint16_t f);
void location_keeper_animation(xeen_game *g, unsigned kind, int dark);
void location_keeper_portrait(xeen_game *g, unsigned kind, int dark);
uint32_t location_map_save(xeen_game *g);
uint16_t location_next_frame(xeen_game *g, int dark);
int location_text(xeen_game *g);

/* game_inn.c: the inn's resume */
xeen_status inn_resume(xeen_game *g, xeen_timer_bios bios, void *user);

/* game_bank.c: the bank's setup, text, keys and closed message */
xeen_status bank_begin(xeen_game *g, unsigned source);
int bank_text(xeen_game *g, char *out, size_t n);
xeen_status bank_key(xeen_game *g, uint16_t key);
xeen_status bank_message_closed(xeen_game *g, unsigned owner);

/* game_guild.c: the guild's text and keys in the location loop */
int guild_text(xeen_game *g, char *out, size_t n);
xeen_status guild_loc_key(xeen_game *g, uint16_t key);

/* game_town.c: the training grounds' and temple's texts and keys, the location messages' close, the refusals,
 * the payments and the voices */
xeen_status location_message_closed(xeen_game *g);
xeen_status temple_key(xeen_game *g, uint16_t key);
int temple_text(xeen_game *g, char *out, size_t n);
int train_text(xeen_game *g, char *out, size_t n);
int location_refused(xeen_game *g, xeen_status *status);
int town_pay(xeen_game *g, uint32_t cost, int owner, xeen_status *status);
void town_voice(xeen_game *g, uint16_t name);

/* game_tavern.c: the tavern's services */
xeen_status tavern_drink(xeen_game *g);
xeen_status tavern_food(xeen_game *g);
xeen_status tavern_held(xeen_game *g);
xeen_status tavern_rumour(xeen_game *g);
xeen_status tavern_tip(xeen_game *g);

/* game_arena.c: the arena's entry and amount */
xeen_status arena_amount(xeen_game *g, uint32_t value);
xeen_status arena_begin(xeen_game *g);

#endif
