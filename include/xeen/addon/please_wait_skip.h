#ifndef XEEN_ADDON_PLEASE_WAIT_SKIP_H
#define XEEN_ADDON_PLEASE_WAIT_SKIP_H
#include <stddef.h>
#include <stdint.h>
/* Owner-approved add-on please_wait_skip (Phase 310; NOT original XEEN.DAT
 * behavior, off unless --addon please_wait_skip or wox.cfg [addons]
 * please_wait_skip = on): the map load's "Please Wait..." box is never
 * shown. Off (the default) the box is the original's.
 *
 * The original (19E82's head 19E8A, 28850 with DS:9A1C set): 1871B(9) opens
 * window 9 (103,156 114x30) with its frame, 4E38C writes "Please Wait..."
 * (DS:5856) and 50BF6(9) shows it at the next vertical retrace; the load
 * runs with no clock read, poll or wait, and 1B0CE's 18765(9) puts the saved
 * pixels back for the next flip. The new game's 2B4AE draws the same box.
 *
 * With the add-on on, the box's text and frame are drawn as the original
 * draws them (so the carried text state is the same) and its pixels are put
 * back at once, before 50BF6(9)'s flip; the flip happens as without the
 * add-on, only the display of that frame (and the native 200 ms hold of the
 * box, XEEN_DISPLAY_WAIT_BOX, which exists only to show it) is skipped. Every DS write, the
 * window's open and close, the map load, the RNG, input, audio and timing are
 * the same as with it off. Only "Please Wait..." is concerned: a fall's
 * "Oops..." in the same window (8C85) is always shown and held (owner decision,
 * Phase 310), and the new game's "Creating World of Xeen files" is not touched.
 *
 * Code: src/addon/please_wait_skip/please_wait_skip.c (the hide hooks); the
 * calls are one-line hooks in src/game/game.c and src/game/game_newgame.c. */

/* Nonzero while the add-on is on (set by the host from --addon / wox.cfg). */
extern int xeen_please_wait_skip_on;

/* Whether window TEXT (a DS text offset) is the box the add-on hides now: on, and TEXT is "Please Wait...". */
int xeen_please_wait_skip_hides(uint16_t text);
/* The pixels of a 320-wide FRAMEBUFFER's rectangle R (x, y, w, h) before the box is drawn, and put back after it
 * (SAVED: at least w * h bytes). Out-of-screen rectangles do nothing (0). */
int xeen_please_wait_skip_save(const uint8_t *framebuffer, size_t size, const uint16_t r[4], uint8_t *saved,
                               size_t saved_size);
void xeen_please_wait_skip_restore(uint8_t *framebuffer, size_t size, const uint16_t r[4], const uint8_t *saved);
#endif
