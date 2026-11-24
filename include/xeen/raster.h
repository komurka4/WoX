#ifndef XEEN_RASTER_H
#define XEEN_RASTER_H
#include "xeen/presentation.h"
#include "xeen/sprites.h"
/* A frame draw's flags (4AEB8): bit 15 mirrors it horizontally, bit 14 clips its rows to the 3D view's 8..140
 * (4AC92), bit 13 clips its columns to the scene window's 8..223, bits 8..11 select the colour mode (1..6,
 * raster_modes.inc) and the low byte that mode's table entry (its timer). Scale bit 15 is the 2x2 enlarge
 * path 4DD96, which ignores the flags. */
#define XEEN_DRAW_MIRRORED      0x8000u
#define XEEN_DRAW_CLIP_ROWS     0x4000u
#define XEEN_DRAW_CLIP_COLUMNS  0x2000u
#define XEEN_DRAW_COLOUR_MODE   0x0f00u
#define XEEN_DRAW_MODE_OF(flags) (((flags) >> 8) & 15u)
#define XEEN_SCALE_ENLARGE      0x8000u
/* Portable native adapter for the original indexed sprite stream. Standard
 * mode, shrink scales0..15, horizontal flip and clip flags 2000/4000, and the
 * scale-8000 2x2 enlarge path 4DD96 (which ignores flags, as the original).
 * Colour modes 1..6 (xeen_raster_effect_cell). Explicit
 * boundaries: scaled scene-clipped blank-row skips. The DOS mirrored unscaled right-clip continuation after a
 * one-visible-pixel literal (4BE8C retained clip parser) is reproduced.
 * Rejected draws leave pixels unchanged; resource storage is borrowed. */
xeen_status xeen_raster_frame(const uint8_t *, size_t, uint16_t frame, uint8_t *pixels, size_t size,
                              int16_t x, int16_t y, uint16_t scale, uint16_t flags);
/* Frame0 cursor, unscaled at its encoded offsets in caller's320x200 buffers;
 * opacity distinguishes transparent pixels from opaque palette index0. */
xeen_status xeen_raster_cursor(const uint8_t *, size_t, uint16_t frame, uint8_t *pixels,
                               uint8_t *opacity, size_t size);
/* The gameplay RNG of one 4AEB8 call (one frame cell). 4AEB8
 * sends a scale with bit 15 to the enlarge path 4DD96 first; otherwise
 * colour modes 2 and 5 ((flags >> 8) & 0Fh, mirrored or not) call 50525
 * twice at their entry, before any clipping: 2 is returned and *ax is AX at
 * the first call (CS:190A[scale & 0Fh] for mode 2, CS:1942[flags & 0FFh]
 * for mode 5, timers 0..3; the second call's AX is the first's result).
 * AX is bookkeeping only: 50525 overwrites AH and DOS clears AL. Else 0. */
unsigned xeen_raster_effect_rng(uint16_t scale, uint16_t flags, uint16_t *ax);
typedef struct {
    xeen_presentation *presentation;
    const xeen_sprite_cache *cache;
    const xeen_sprite_archives *archives;
    /* The driver's pixel generator CS:18D2/18D4 (0 at start), kept
     * across draws; modes 2 and 5 seed it from their 50525 draws. */
    uint16_t generator[2];
} xeen_raster;
/* One frame cell in 4AEB8's colour modes 1..6 (flags & 0F00h,
 * mirrored by bit 15; a mirrored mode above 6 is mode 6), transliterated
 * from the driver (raster_modes.inc). rows = the row clip CS:0C28/0C2A
 * (4ADB1: the list's window, 4AC92: 8..140 with flags 4000h, else 0..200);
 * rng/carry = the call's two 50525 results for modes 2 and 5; generator =
 * CS:18D2/18D4, updated. Pixels go to `page` below 64000. Rejects (no
 * change) a scale above 15, scale bit 15 or a table entry the driver's
 * tables do not hold (the flags' low byte past the mode's timers). */
xeen_status xeen_raster_effect_cell(const uint8_t *sprite, size_t n, uint16_t cell, uint8_t *page,
                                    int16_t x, int16_t y, uint16_t scale, uint16_t flags,
                                    const uint16_t rows[2], const uint16_t rng[2],
                                    const uint8_t carry[2], uint16_t generator[2]);
xeen_status xeen_raster_scene(void *, const xeen_draw_list *);
xeen_status xeen_raster_sprites(void *, const xeen_sprite_draw *, size_t);
/* Original raw768-byte DAC palette, values0..63. */
xeen_resource_status xeen_raster_palette(xeen_presentation *, const xeen_archive *, const char *);
#endif
