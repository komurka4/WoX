#ifndef XEEN_ADDON_CHARACTER_DETAILS_H
#define XEEN_ADDON_CHARACTER_DETAILS_H
#include <stdint.h>
/* Owner-approved add-on character_details (Phase 208; NOT original XEEN.DAT
 * behavior, off unless --addon character_details or wox.cfg [addons]
 * character_details = on). In the character view (44AE6), the stat
 * details popup (428F1, window 1Ch) explains what a value does:
 *   - Might, Intellect, Personality, Endurance, Speed, Accuracy, Luck, Age,
 *     Level and Armor Class: the popup grows by explanation lines under its
 *     title (the game's own formulas for this character, and the next bonus
 *     step); a popup that would pass the sheet's bottom is moved up (owner:
 *     popups stay inside the character sheet);
 *   - Skills and Conditions: a click on a skill line, a condition line or a
 *     party effect line (not "Good" or "None") highlights it (colour 15, as
 *     the original lists) and opens a detail window: skills at (8,20)
 *     169x124 over the stat grid, conditions at (177,20) 143x124 in the
 *     right column. Any other click or key closes the popup as the original.
 * Owner decisions (Phase 208): the skill window covers the stat grid; popups
 * stay inside the sheet; "Next bonus at N" stays; Age, Level and AC explained
 * too; conditions in the right column; "Good" not clickable; Arms Master and
 * Tracker (no effect in the original, Phase 94) get joke texts.
 *
 * Drawn by the host only, on a copy of the displayed 320x200 page; the
 * game's DS, framebuffer, RNG and saves are never written. The one input
 * change: at the popup's poll (43183) a click on a clickable line is kept by
 * the add-on instead of closing the popup. Depends on no other add-on and on
 * no bug fix.
 *
 * Code: src/addon/character_details/ (overlay.c, texts.c). */

/* Nonzero while the add-on is on. */
extern int xeen_character_details_on;

struct xeen_game;
/* Turns the add-on on. SPRITE_ARCHIVES is the host's xeen_sprite_archives
 * (borrowed): the game font (fnt) is read from it once. 0 (and off) when the
 * font is missing. */
int xeen_character_details_start(const void *sprite_archives);
void xeen_character_details_stop(void);
/* The game whose DS and character view the overlay reads (NULL: none). */
void xeen_character_details_bind(const struct xeen_game *);
/* The page to show instead of PAGE: PAGE itself while nothing is added,
 * otherwise a copy with the add-on's windows. */
const uint8_t *xeen_character_details_page(const uint8_t *page);
/* Hook (428F1's entry): a new popup opens, no line is selected. */
void xeen_character_details_reset(void);
/* Hook (the popup's poll 43183, a click: key 1): nonzero when the click is
 * on a clickable line of the Skills or Conditions popup and the add-on took
 * it (the popup stays open); 0 lets the original close the popup. */
int xeen_character_details_click(const struct xeen_game *);
#endif
