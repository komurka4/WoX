/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Phase 290 (owner-approved host start-up, not original behaviour): while the SC-55 engine boots on its worker
 * (xeen_audio_sc55_boot_start), the window is open and shows one centred line in the game's own font and palette;
 * the game is created only after the boot has finished. Nothing here reads or writes game state. */
#ifndef XEEN_HOST_STARTUP_H
#define XEEN_HOST_STARTUP_H
#include "xeen/resources.h"
#include <stddef.h>
#include <stdint.h>

/* The owner's exact text. */
#define HOST_STARTUP_SC55_TEXT "Starting Nuked-SC55, please wait..."
/* The text's colours: the original text colour table's entry 4 (the 0Cnn code 04: mm4.pal index 1, white 63 63 63,
 * for all three glyph bits) on palette index 0 (mm4.pal's black). Entry 0 (index 25, 15 15 15) is too dark on black;
 * the table's values in mm4.pal are phase290_startup_frame's XEEN_TEST_COLOURS output. */
#define HOST_STARTUP_TEXT_COLOUR 4u
#define HOST_STARTUP_BACKGROUND  0u

/* The loading frame: a 320x200 page cleared to HOST_STARTUP_BACKGROUND with TEXT (printable ASCII, one line no
 * wider than the page) centred horizontally and vertically, drawn by the game's glyph routine (xeen_text_glyph) in
 * XEEN.CC's "fnt", and mm4.pal (6-bit DAC) of the same archive. Returns 0 when a resource is missing or malformed
 * or the text does not fit (PIXELS then HOST_STARTUP_BACKGROUND, PALETTE black); 1 otherwise. X, Y and WIDTH (each may be
 * NULL) give the text's origin and advance width. */
int host_startup_loading_frame(const xeen_archive *xeen_cc, const char *text, uint8_t pixels[64000],
                               uint8_t palette[768], unsigned *x, unsigned *y, unsigned *width);

/* The progress bar under the text (owner-requested): a 240 x 10 outline with its corners cut, in mm4.pal index 1
 * (the text's white), at x 40, y 112 (the text's rows are 96..103); inside it a black gap of one pixel, then the
 * fill, 236 x 6 at x 42, y 114, of which PERMILLE (0..1000, more counts as 1000) per mille from the left is white,
 * the rest black. Only the bar's rectangle is drawn. */
#define HOST_STARTUP_BAR_X      40u
#define HOST_STARTUP_BAR_Y      112u
#define HOST_STARTUP_BAR_WIDTH  240u
#define HOST_STARTUP_BAR_HEIGHT 10u
#define HOST_STARTUP_BAR_COLOUR 1u
void host_startup_progress_bar(uint8_t pixels[64000], unsigned permille);

/* The wait. Each pass: pump the window's events; a close request ends the wait (CANCELLED, checked first); a
 * finished boot ends it (READY); else wait WAIT_MS (the window keeps its events, the shown frame is repainted by the
 * platform on expose and resize). No step is skipped and nothing spins: every pass waits unless it ends the wait.
 * TRACE, when set, receives each decision ("pump", "close", "ready", "wait") in order (tests). */
typedef enum { HOST_STARTUP_READY, HOST_STARTUP_CANCELLED } host_startup_outcome;
#define HOST_STARTUP_WAIT_MS 10u
typedef struct {
    void *user;
    int (*finished)(void *);
    int (*should_close)(void *);
    void (*pump)(void *);
    void (*wait)(void *, unsigned milliseconds);
    void (*trace)(void *, const char *event);
    /* Optional (may be NULL), untraced: called once per pass after the pump unless the pass ends the wait (the
     * window's progress bar is redrawn there). */
    void (*refresh)(void *);
} host_startup_wait_ops;
host_startup_outcome host_startup_wait(const host_startup_wait_ops *);
#endif
