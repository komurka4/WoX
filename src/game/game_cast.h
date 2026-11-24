/*
 * The Cast family's private interface (Phase 249): the spell handlers and their prompts, scenes and keys, which
 * only the five Cast units call among themselves (game_cast.c, game_cast_party.c, game_cast_attack.c,
 * game_cast_travel.c, game_cast_info.c). Only these units include it. The session sees the dialog's entries,
 * the costs and payment, cast_dispatch/cast_handled, the volleys and the travel spells' amount entries
 * (game_internal.h).
 */
#ifndef XEEN_GAME_CAST_H
#define XEEN_GAME_CAST_H
#include "game_internal.h"

/* game_cast.c: the spell's sound (the handlers' first effect) */

/* game_cast_party.c: the party-target spells: the healing family and its flash, the 399FA Whom prompt, the aid
 * handlers (cast_aid)
 */
xeen_status cast_failed(xeen_game *g);
xeen_status cast_flash_frame(xeen_game *g);
xeen_status cast_heal(xeen_game *g, unsigned member, uint16_t amount);
xeen_status cast_whom(xeen_game *g, unsigned spell);
xeen_status cast_whom_key(xeen_game *g, uint16_t key);
xeen_status cast_whom_scene(xeen_game *g);

/* game_cast_attack.c: the monster-target spells: the 39817 element prompt, Moon Ray and the attack handlers
 * (cast_attack)
 */
xeen_status moon_ray_next(xeen_game *g);
xeen_status cast_attack(xeen_game *g, unsigned spell);
xeen_status cast_element(xeen_game *g);
xeen_status cast_element_key(xeen_game *g, uint16_t key);
xeen_status cast_element_scene(xeen_game *g);

/* game_cast_travel.c: the travel and time spells (Etherealize, Teleport, Time Distortion, Town Portal, Lloyd's
 * Beacon) and Lloyd's scene and keys
 */
xeen_status cast_etherealize(xeen_game *g);
xeen_status cast_lloyd(xeen_game *g);
xeen_status cast_portal(xeen_game *g);
xeen_status cast_teleport(xeen_game *g);
xeen_status cast_time_distortion(xeen_game *g);
xeen_status lloyd_end(xeen_game *g);
xeen_status lloyd_key(xeen_game *g, uint16_t key);
xeen_status lloyd_scene(xeen_game *g);

/* game_cast_info.c: the monster-information spells (Detect Monster, Identify Monster): their scenes, windows and
 * ends
 */
xeen_status cast_detect(xeen_game *g);
xeen_status cast_identify(xeen_game *g);
xeen_status detect_end(xeen_game *g);
xeen_status detect_scene(xeen_game *g);
xeen_status identify_end(xeen_game *g);
xeen_status identify_scene(xeen_game *g);

#endif
