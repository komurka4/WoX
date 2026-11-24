#ifndef XEEN_ADDON_SPELL_SHORT_DESCRIPTION_H
#define XEEN_ADDON_SPELL_SHORT_DESCRIPTION_H
#include <stdint.h>
/* Owner-approved add-on spell_short_description (Phase 203; NOT original
 * XEEN.DAT behavior, off unless --addon spell_short_description or wox.cfg
 * [addons] spell_short_description = on): while the Cast Spell dialog
 * (3D8C1, window 0Ah) is open, in exploration or in combat, a small framed
 * box at (226,146) 93x54 under it, over the movement arrows, shows a short
 * description of a spell in the game's reduced font (at most 4 lines):
 *   - the caster's ready spell;
 *   - while the New list (3F05D mode 1) is open: the highlighted line's
 *     spell, or the ready spell when no line is highlighted; after Exit the
 *     ready spell again, after Select the newly chosen one.
 * Owner choices (Phase 203): also in combat; no box for a member who is not a
 * spell caster (46470's maximum SP 0, the list's "Not a spell caster..."
 * test); plain facts in the window's own colour.
 *
 * The 76 texts (texts.inc) state facts of the spells' handlers: the
 * spldesc.bin text condensed where Phase 202 found it right, the bug fix
 * spell_description's corrected text for the 17 it corrects (copied as
 * facts; this add-on does not use the bug fix), the target scope from each
 * attack spell's 281A7 mode (972C), and "Not in combat" for the 8 spells the
 * combat dialog refuses (39AF6).
 *
 * Drawn by the host only, on a copy of the displayed 320x200 page (the
 * game's framebuffer, DS, RNG and saves are never written), and only while
 * window 0Ah is open and no other open window overlaps the box (e.g. the
 * Teleport/Town Portal number entry in window 6 or a bottom message). It
 * depends on no other add-on and on no bug fix.
 *
 * Code: src/addon/spell_short_description/ (overlay.c, texts.inc). */

/* Nonzero while the add-on is on. */
extern int xeen_spell_short_description_on;

struct xeen_game;
/* Turns the add-on on. SPRITE_ARCHIVES is the host's xeen_sprite_archives
 * (borrowed): the game font (fnt) is read from it once. 0 (and off) when the
 * font is missing. */
int xeen_spell_short_description_start(const void *sprite_archives);
void xeen_spell_short_description_stop(void);
/* The game whose DS and Cast dialog the overlay reads (NULL: none). */
void xeen_spell_short_description_bind(const struct xeen_game *);
/* The page to show instead of PAGE: PAGE itself while no box is shown,
 * otherwise a copy with the box. */
const uint8_t *xeen_spell_short_description_page(const uint8_t *page);
#endif
