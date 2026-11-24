/*
 * The host's evidence writers (src/app/host_evidence.c): the binary snapshots, the evidence record line, the
 * page audit and the refresh capture. Every writer observes the game and the presentation; none changes them.
 */
#ifndef XEEN_APP_HOST_EVIDENCE_H
#define XEEN_APP_HOST_EVIDENCE_H
#include "host_files.h"
#include "xeen/game.h"
#include "xeen/presentation.h"
#include <stddef.h>
#include <stdint.h>

/* Writes the N bytes B to PREFIX-LABEL.bin (nothing, and 1, without a PREFIX); 0 when the path does not fit
 * or the file cannot be written. */
int host_write_snapshot(const char *prefix, const char *label, const uint8_t *b, size_t n);
/* Both in-memory CUR images as PREFIX-TAG-xeen-cur / -dark-cur and, when FULL, the event store and every file
 * of D's save directory; 0 when a snapshot fails. */
int host_snapshot_saves(const char *prefix, const char *tag, xeen_game *g, const save_directory *d, int full);
/* Beside the start snapshot BEFORE ("LABEL-before"), the driver's resident sprites as LABEL-sprites. */
int host_snapshot_sprites(const char *prefix, const char *before, const xeen_game *g);
/* Prints one evidence record of the game's state with TAG (and RNG_CALLS) when --diagnostic-state is on (Phase
 * 272; off by default: nothing printed), then appends the page audit when XEEN_PAGE_AUDIT is set (either way). */
void host_print_record(xeen_game *g, xeen_presentation *p, const char *tag, unsigned rng_calls);
/* --diagnostic-state: the state-summary records of host_print_record (and the host's game-over summary) on. */
void host_set_diagnostic_state(int on);
int host_diagnostic_state(void);
/* Sets the refresh capture's directory (NULL: off). */
void host_refresh_capture_set(const char *dir);
/* Writes the displayed page and its palette as one refresh dump named by BOUNDARY (nothing while off). */
void host_refresh_dump(xeen_presentation *p, uint32_t boundary);
/* The shown-page capture (XEEN_HOST_PAGE_CAPTURE=DIR, headless, Phase 329; developer evidence): the page a window
 * would show, after the owner-approved add-ons' overlays (explore.c's HOST_PAGE), which the refresh and display
 * captures (the game's page) never contain. Sets its directory (NULL: off). */
void host_shown_capture_set(const char *dir);
int host_shown_capture_on(void);
/* Returns SHOWN (HOST_PAGE's result for GAME_PAGE) unchanged after writing it and PALETTE as DIR/shown-NNNN.bin and
 * .pal when the pair differs from the last one written, named shown-NNNN-overlay.bin/.pal when an overlay changed
 * the game's page (at most 2000; nothing while off). Observation only. */
const uint8_t *host_shown_page(const uint8_t *game_page, const uint8_t *shown, const uint8_t *palette);
#endif
