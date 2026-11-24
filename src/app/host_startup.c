/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Phase 290 (owner-approved host start-up, not original behaviour): the SC-55 loading frame and the window's wait
 * for the boot (host_startup.h). */
#include "host_startup.h"
#include "xeen/text_window.h"
#include <stdlib.h>
#include <string.h>

#define FONT_BYTES  4352u
#define FONT_WIDTHS 0x1000u /* the font's advance table (as the text writer and the add-ons read it) */

/** Extracts the XEEN.CC member NAME of exactly SIZE bytes into OUT; 1 on success. */
static int extract_exact(const xeen_archive *a, const char *name, uint8_t *out, size_t size)
{
    xeen_resource_info info;
    size_t got = 0;
    return a && xeen_archive_find_name(a, name, &info) == XR_OK && info.size == size &&
           xeen_archive_extract(a, XR_BANK_ASSETS_XOR35, &info, out, size, &got) == XR_OK && got == size;
}
/* The 320 x 200 page and its 256-colour palette of 6-bit DAC values. */
#define PAGE_WIDTH    320u
#define PAGE_HEIGHT   200u
#define PAGE_BYTES    (PAGE_WIDTH * PAGE_HEIGHT)
#define PALETTE_BYTES 768u
#define DAC_MAX       63u
/* A refused frame: the palette black (and the page, once glyphs may have been drawn, the background); returns 0. */
static int frame_refused(uint8_t pixels[64000], uint8_t palette[768], int clear_page)
{
    if (clear_page) {
        memset(pixels, HOST_STARTUP_BACKGROUND, PAGE_BYTES);
    }
    memset(palette, 0, PALETTE_BYTES);
    return 0;
}
int host_startup_loading_frame(const xeen_archive *xeen_cc, const char *text, uint8_t pixels[64000],
                               uint8_t palette[768], unsigned *x, unsigned *y, unsigned *width)
{
    uint8_t font[FONT_BYTES], colours[4];
    unsigned w = 0, at, top = (PAGE_HEIGHT - 8u) / 2u; /* glyph rows 0..7 centred: 96..103 */
    xeen_text_cursor cursor;
    const char *c;
    memset(pixels, HOST_STARTUP_BACKGROUND, PAGE_BYTES);
    memset(palette, 0, PALETTE_BYTES);
    if (!text || !extract_exact(xeen_cc, "fnt", font, sizeof font) ||
        !extract_exact(xeen_cc, "mm4.pal", palette, PALETTE_BYTES)) {
        return frame_refused(pixels, palette, 0);
    }
    for (at = 0; at < PALETTE_BYTES; ++at) {
        if (palette[at] > DAC_MAX) {
            return frame_refused(pixels, palette, 0);
        }
    }
    for (c = text; *c; ++c) {
        if ((unsigned char)*c < 32u || (unsigned char)*c > 126u) {
            return frame_refused(pixels, palette, 0);
        }
        w += font[FONT_WIDTHS + (unsigned char)*c];
    }
    if (w > PAGE_WIDTH) {
        return frame_refused(pixels, palette, 0);
    }
    memset(&cursor, 0, sizeof cursor);
    xeen_text_cursor_set_colour(&cursor, HOST_STARTUP_TEXT_COLOUR);
    memcpy(colours, cursor.colours, sizeof colours);
    at = (PAGE_WIDTH - w) / 2u;
    for (c = text; *c; ++c) {
        unsigned ch = (unsigned char)*c, advance = font[FONT_WIDTHS + ch];
        if (ch != ' ' &&
            xeen_text_glyph(font, sizeof font, pixels, PAGE_BYTES, at, top, ch, colours, &advance) != XEEN_OK) {
            return frame_refused(pixels, palette, 1);
        }
        at += advance;
    }
    if (x) {
        *x = (PAGE_WIDTH - w) / 2u;
    }
    if (y) {
        *y = top;
    }
    if (width) {
        *width = w;
    }
    return 1;
}
void host_startup_progress_bar(uint8_t pixels[64000], unsigned permille)
{
    const unsigned x0 = HOST_STARTUP_BAR_X, y0 = HOST_STARTUP_BAR_Y, w = HOST_STARTUP_BAR_WIDTH,
                   h = HOST_STARTUP_BAR_HEIGHT, fill_w = w - 4u, fill = (permille > 1000u ? 1000u : permille) * fill_w / 1000u;
    unsigned x, y;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            int edge = x == 0 || y == 0 || x == w - 1u || y == h - 1u,
                corner = (x == 0 || x == w - 1u) && (y == 0 || y == h - 1u),
                filled = x >= 2u && y >= 2u && x < w - 2u && y < h - 2u && x - 2u < fill;
            pixels[(y0 + y) * 320u + x0 + x] =
                    (uint8_t)((edge && !corner) || filled ? HOST_STARTUP_BAR_COLOUR : HOST_STARTUP_BACKGROUND);
        }
    }
}
host_startup_outcome host_startup_wait(const host_startup_wait_ops *o)
{
    for (;;) {
        if (o->trace) {
            o->trace(o->user, "pump");
        }
        o->pump(o->user);
        if (o->should_close(o->user)) {
            if (o->trace) {
                o->trace(o->user, "close");
            }
            return HOST_STARTUP_CANCELLED;
        }
        if (o->finished(o->user)) {
            if (o->trace) {
                o->trace(o->user, "ready");
            }
            return HOST_STARTUP_READY;
        }
        if (o->refresh) {
            o->refresh(o->user);
        }
        if (o->trace) {
            o->trace(o->user, "wait");
        }
        o->wait(o->user, HOST_STARTUP_WAIT_MS);
    }
}
