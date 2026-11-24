/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Shared archive, image, voice and platform types for the cinematic player. The cc, sprite,
 * VOC and platform entry points connect game assets to runtime playback; modules with larger
 * contracts have their own headers.
 */
#ifndef XEEN_H
#define XEEN_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define XEEN_PIXELS 64000
/* Fixed original indexed framebuffer and DAC geometry (bytes unless named pixels). */
#define XEEN_WIDTH_PIXELS 320
#define XEEN_HEIGHT_PIXELS 200
#define XEEN_PALETTE_BYTES 768
#define XEEN_PALETTE_FULL_LEVEL 128

/* Process outcomes for the standalone native player. */
enum {
    XEEN_COMPLETED = 0,
    XEEN_SKIPPED = 2,
    XEEN_FAILED = 1
};

/* Phase 308: Blob, Voice, CC, cc_read, voc_decode, voice_free and voice_playable_count are the game's audio modules
 * (src/audio/audio_archive.h, voc.c, audio_engine.c's cc_read over xeen_archive): the same code as this player's
 * former cc.c/media.c decoders (byte-identical decode paths; only its trace hooks differ). */
#include "audio/audio_archive.h"

/* Phase 308: bind CC to the host-supplied archive whose file name is PATH's last component (INTRO.CC, XEEN.CC,
 * DARK.CC, case-insensitively; player_host.c). Return one when the archive is available, zero (CC cleared) when
 * not. Nothing is opened: the archives belong to the host (in memory, from the data directory or a CD image).
 */
int cc_open(CC *cc, const char *path);
/* Clear CC (the archive stays the host's). */
void cc_close(CC *cc);
/* Draw one unscaled frame at its stored origin into borrowed indexed pixels. Return one on
 * success or zero for a malformed sprite or frame; an error can leave partial drawing.
 */
int sprite_draw(const Blob *b, unsigned frame, uint8_t *dst);
/* Draw one frame at the supplied signed offsets and scale-table index into borrowed pixels.
 * The sprite remains caller-owned. Return one on success, zero for invalid coordinates, scale,
 * frame or packet data; partial drawing can remain on error.
 */
int sprite_draw_scaled(const Blob *b, unsigned frame, int x, int y, unsigned scale, uint8_t *dst);
/* Draw one frame inside the ending window, from row 17 to the caller-supplied bottom. Borrow
 * sprite and destination storage. Return one on success, zero on invalid frame or data;
 * partially drawn pixels remain on error.
 */
int sprite_draw_windowed(const Blob *b, unsigned frame, int x, int y, unsigned bottom,
                         uint8_t *dst);
/* Draw a normal sprite or the clipped cinematic effect into borrowed pixels. Nonzero effect
 * flags accept only the supported clipping effect with scale zero. Return one on success, zero
 * for unsupported inputs or malformed data; errors do not roll back drawing.
 */
int sprite_draw_effect(const Blob *b, unsigned frame, int x, int y, unsigned scale, unsigned flags,
                       uint8_t *dst);
/* Phase 308: voc_decode (the game's) plus an owned copy of the encoded bytes in VOICE (media.c). */
int player_voc_decode(const Blob *b, Voice *v);
/* Return the BIOS tick containing a PIT-cycle timestamp, using the fixed BIOS reload divisor.
 * This arithmetic helper allocates nothing and has no failure result.
 */
uint64_t tick_at(uint64_t cycles);
/* Return the BIOS tick boundary ticks after the tick containing reset. Inputs and result use
 * PIT cycles; zero ticks can precede reset within its current tick. Arithmetic is unsigned and
 * has no failure result.
 */
uint64_t tick_deadline(uint64_t reset_cycles, unsigned ticks);
/* Convert the borrowed 768-byte DAC palette to caller-owned RGB bytes at the supplied
 * brightness, where 128 means full level. Inputs must have palette-sized storage; no
 * validation or failure status is provided.
 */
void palette_rgb(const uint8_t *pal, unsigned level, uint8_t *rgb);
/* Write one indexed cinematic frame and its borrowed RGB palette as a P6 image at path. Own
 * and close the opened output stream. Return one on success, zero on open or write failure; a
 * partial output file can remain.
 */
int ppm_write(const char *path, const uint8_t *pixels, const uint8_t *rgb);
/* Phase 308: the platform services of one movie, served by the xeen_movie_host the game supplies (player_host.c).
 * They keep the former SDL platform's contracts. */
/* 1: the host is bound (live playback); 0: none (a dump run: virtual clock, no window). */
int platform_open(void);
void platform_close(void);
/* Monotonic PIT cycles since platform_open. */
uint64_t platform_cycles(void);
/* Wait until the PIT-cycle deadline (input stays queued). */
void platform_wait(uint64_t deadline);
/* Drain input: 0 nothing, 1 Escape, 2 the global abort (Ctrl+Q, the window closed), which takes priority. */
int platform_escape(void);
/* Drain input: 0 nothing, 1 a fresh key, 2 the global abort, which takes priority. */
int platform_pending_key(void);
/* Show the indexed frame through RGB (8-bit components; each is a 6-bit DAC value as palette_rgb expands it).
 * 1 on success, 0 on display failure. */
int platform_present(const uint8_t *pixels, const uint8_t *rgb);
#endif
