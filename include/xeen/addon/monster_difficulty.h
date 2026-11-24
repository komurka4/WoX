#ifndef XEEN_ADDON_MONSTER_DIFFICULTY_H
#define XEEN_ADDON_MONSTER_DIFFICULTY_H
#include <stddef.h>
#include <stdint.h>
/* Owner-approved add-on monster_difficulty (Phase 201; NOT original XEEN.DAT
 * behavior, off unless --addon monster_difficulty or wox.cfg [addons]
 * monster_difficulty = on): a small icon over every monster in the 3D view
 * shows how dangerous it is for the current party.
 *
 * Owner choices (Phase 201): variant D of the mockups (a 9 px icon at depth 0,
 * the adjacent cell and combat; a 7 px icon at depth 1; a 3 px coloured dot at
 * depths 2-3), the plain icon with a one-pixel black outline, one tier per
 * monster, no Identify Monster needed, Trivial shows nothing, the party's
 * offense is its weapons only:
 *   Easy      green sword
 *   Moderate  gold crossed swords
 *   Hard      orange shield with "!"
 *   Deadly    red skull
 * The icon has one fixed place per monster sprite and draw position, from all
 * the frames of its standing sprite together (owner choice after the first
 * test), so it does not move while the monster animates: 2 px above the
 * sprite's top, centred on its head; when that leaves the 3D view (a big
 * monster in combat), beside the head where it covers the fewest of the
 * sprite's pixels. The attack sprite the game swaps in during combat may
 * briefly reach it.
 *
 * The tier (assess.c) is an estimate from the game's own combat formulas,
 * as expected values (nothing is rolled, the game's random numbers are never
 * touched): the rounds the party's weapons need to kill the monster against
 * the share of the party's HP it takes meanwhile, with overrides for deadly
 * specials, immunity to weapons and one-round threats. Both sides at full
 * health without temporary effects (owner choice after the first test): a
 * fight never changes a monster's icon.
 *
 * It is drawn by the host only, on a copy of the displayed 320x200 page (the
 * game's framebuffer, DS and saves are never written). An icon is drawn only
 * when the monster's sprite is really on the displayed page (sampled pixels
 * of its frame), outside every open window, so dialogs, locations, menus and
 * other screens show none. It depends on no other add-on.
 *
 * Code: src/addon/monster_difficulty/ (assess.c the tier, overlay.c the
 * placement and drawing, icons.inc the pixel art). */

/* Nonzero while the add-on is on. */
extern int xeen_monster_difficulty_on;

struct xeen_game;
/* Turns the add-on on. SPRITE_ARCHIVES is the host's xeen_sprite_archives
 * (borrowed): the monsters' sprite bytes for their outlines. */
int xeen_monster_difficulty_start(const void *sprite_archives);
void xeen_monster_difficulty_stop(void);
/* The game whose DS, monster database and sprite cache the overlay reads. */
void xeen_monster_difficulty_bind(const struct xeen_game *);
/* The page to show instead of PAGE: PAGE itself while no icon is shown,
 * otherwise a copy with the icons. */
const uint8_t *xeen_monster_difficulty_page(const uint8_t *page);

/* The tier names, 0 Trivial .. 4 Deadly (developer log). */
enum { XEEN_MD_TRIVIAL, XEEN_MD_EASY, XEEN_MD_MODERATE, XEEN_MD_HARD, XEEN_MD_DEADLY };
#endif
