#ifndef XEEN_ADDON_MOUSE_SCROLL_H
#define XEEN_ADDON_MOUSE_SCROLL_H
#include <stdint.h>
#include "xeen/input_poll.h"
/* Owner-approved add-on mouse_scroll (Phase 204; NOT original XEEN.DAT
 * behavior, the original reads no mouse wheel; off unless --addon
 * mouse_scroll or wox.cfg [addons] mouse_scroll = on): the mouse wheel
 * scrolls the game's scrolling lists, one line (one Up or Down) per notch:
 *   - the Cast dialog's New list (exploration and combat), the guild's Buy
 *     Spells and Spell Info lists (3F05D, window 19h);
 *   - the Quests panel: Items, Quests, Auto Notes (3E24F, window 1Eh);
 *   - a character's Awards (41581, window 1Eh);
 *   - the inn's roster (43E0B, window 0Bh; there the original Up and Down
 *     move by four).
 * A notch up is the original Up key (F2h), a notch down the original Down
 * key (F3h), given to the game at that list's own key poll, only while the
 * pointer is over the list's window: the game then does exactly what the
 * key does (its limits, its redraw, its button feedback). Every other
 * notch (another screen, the pointer elsewhere) is dropped, never kept for
 * later. With the add-on off the wheel is no event (the platform drops it).
 *
 * Host input only: a wrapper around the host's input provider; no hook in
 * the game code (one read-only view, xeen_game_scroll_list). It depends on
 * no other add-on and on no bug fix.
 *
 * Code: src/addon/mouse_scroll/ (mouse_scroll.c). */

/* Nonzero while the add-on is on. */
extern int xeen_mouse_scroll_on;

struct xeen_game;
/* The host's wheel: one pending notch (+1 up, -1 down, 0 none) and the
 * logical point where it happened (xeen_platform_take_wheel). */
typedef int (*xeen_mouse_scroll_take)(void *user, uint16_t *x, uint16_t *y);
/* Turns the add-on on: *PROVIDER (the host's, copied) is replaced by the
 * wrapper. GAME points to the host's current game pointer (read at each
 * key request; a Load's new game is followed). 0 (and off) for a missing
 * argument. */
int xeen_mouse_scroll_start(xeen_input_provider *provider, xeen_mouse_scroll_take take, void *take_user,
                            struct xeen_game *const *game);
void xeen_mouse_scroll_stop(void);
#endif
