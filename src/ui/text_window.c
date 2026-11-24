#include "xeen/text_window.h"
#include "xeen/byte_order.h"
#include "xeen/windows.h"
#include <string.h>
#include "text_frame.inc"
/* The text controls (4E38C; the layout comment below): 0D clears and homes, 01/02 the normal/reduced font,
 * 03 c|r|l the justification, 04nnn a background box, 05 nothing, 06 a space, 07nnn the background, 08 an
 * overstrike, 09nnn x, 0A a new line, 0Bnnn y, 0Cnn/0Cd the colour. */
enum {
    TEXT_NORMAL_FONT = 1,
    TEXT_REDUCED_FONT = 2,
    TEXT_JUSTIFY = 3,
    TEXT_BOX = 4,
    TEXT_NOTHING = 5,
    TEXT_SPACE = 6,
    TEXT_BACKGROUND = 7,
    TEXT_OVERSTRIKE = 8,
    TEXT_X = 9,
    TEXT_NEWLINE = 10,
    TEXT_Y = 11,
    TEXT_COLOUR = 12,
    TEXT_HOME = 13
};
/* A cursor's or layout's justification (03l, 03c, 03r). */
enum { TEXT_LEFT = 0, TEXT_CENTRE = 1, TEXT_RIGHT = 2 };
/* The font (fnt): 256 glyphs of 16 bytes (8 rows of 2-bit pixels), the reduced font's at +80h, then their 256
 * advance widths. */
#define FONT_GLYPH_BYTES 16u
#define FONT_REDUCED     0x80u
#define FONT_WIDTHS      0x1000u
#define FONT_BYTES       (FONT_WIDTHS + 256u) /* 4352: the glyphs, then the widths */
/* A glyph's cell: 8 x 8 pixels (g, p, q and y one row lower), so its top left stays within these. */
#define GLYPH_SIZE       8u
#define GLYPH_MAX_X      (XEEN_PRESENT_WIDTH - GLYPH_SIZE)       /* 312 */
#define GLYPH_MAX_Y      (XEEN_PRESENT_HEIGHT - GLYPH_SIZE - 1u) /* 191 */
/**
 * Loads the game font (fnt, 4352 bytes) from archive A into the text window; returns XR_BAD_INPUT
 * for an active window and the archive's status when the font is missing or has another size.
 */
xeen_resource_status xeen_text_window_load(xeen_text_window *w, const xeen_archive *a)
{
    xeen_resource_info info;
    size_t n = 0;
    xeen_resource_status s;
    uint8_t font[FONT_BYTES];
    if (!w || !a || w->active) {
        return XR_BAD_INPUT;
    }
    s = xeen_archive_find_name(a, "fnt", &info);
    if (s != XR_OK) {
        return s;
    }
    if (info.size != sizeof font) {
        return XR_BAD_INPUT;
    }
    s = xeen_archive_extract(a, XR_BANK_ASSETS_XOR35, &info, font, sizeof font, &n);
    if (s != XR_OK) {
        return s;
    }
    memcpy(w->font, font, sizeof font);
    w->loaded = 1;
    return XR_OK;
}
/**
 * Draws character CH (33..126) of FONT at X, Y with the four COLOURS, in the reduced font when
 * REDUCED is set; g, p, q and y sit one row lower as in the original. Stores the character's
 * advance in *ADVANCE.
 */
static xeen_status draw_glyph(const uint8_t *font, size_t n, uint8_t *pixels, size_t pn, unsigned x,
                              unsigned y, unsigned ch, int reduced, const uint8_t colours[4],
                              unsigned *advance)
{
    unsigned row, col;
    if (!font || n != FONT_BYTES || !pixels || pn < XEEN_PRESENT_PIXELS || !colours || !advance || ch < 33 ||
        ch > 126 || x > GLYPH_MAX_X || y > GLYPH_MAX_Y) {
        return XEEN_BAD_INPUT;
    }
    if (ch == 'g' || ch == 'p' || ch == 'q' || ch == 'y') {
        ++y;
    }
    if (reduced) {
        ch |= FONT_REDUCED;
    }
    for (row = 0; row < 8; ++row) {
        unsigned bits = xeen_get_le16(font + ch * FONT_GLYPH_BYTES + row * 2u);
        for (col = 0; col < 8; ++col) {
            unsigned c = bits & 3u;
            bits >>= 2;
            if (c) {
                pixels[(y + row) * XEEN_PRESENT_WIDTH + x + col] = colours[c];
            }
        }
    }
    *advance = font[FONT_WIDTHS + ch];
    return XEEN_OK;
}
xeen_status xeen_text_glyph(const uint8_t *font, size_t n, uint8_t *pixels, size_t pn, unsigned x,
                            unsigned y, unsigned ch, const uint8_t colours[4], unsigned *advance)
{
    return draw_glyph(font, n, pixels, pn, x, y, ch, 0, colours, advance);
}
/** Draws 8x8 window-frame tile INDEX at X, Y, leaving its transparent (0) pixels unchanged. */
static void draw_frame_tile(uint8_t *p, unsigned x, unsigned y, unsigned index)
{
    for (unsigned row = 0; row < 8; ++row) {
        for (unsigned col = 0; col < 8; ++col) {
            uint8_t c = frame_tiles[index * 64u + row * 8u + col];
            if (c) {
                p[(y + row) * XEEN_PRESENT_WIDTH + x + col] = c;
            }
        }
    }
}
/**
 * Marks with 1 the pixels that frame tile INDEX would draw at X, Y (the opacity mask of a frame).
 */
static void mark_frame_tile(uint8_t *p, unsigned x, unsigned y, unsigned index)
{
    for (unsigned row = 0; row < 8; ++row) {
        for (unsigned col = 0; col < 8; ++col) {
            if (frame_tiles[index * 64u + row * 8u + col]) {
                p[(y + row) * XEEN_PRESENT_WIDTH + x + col] = 1;
            }
        }
    }
}
/* 4F6F8/4F2A1's tile layout (4F24A per tile: one stosb per nonzero tile
 * byte at 4F281). Incomplete eight-pixel extents retain the original gaps
 * and overlap. MARK (for the visual evidence) marks the opaque pixels instead. */
static void border_layout(uint8_t *p, const uint16_t r[4], int mark)
{
    unsigned x = r[0], y = r[1], w = r[2], h = r[3];
    void (*t)(uint8_t *, unsigned, unsigned, unsigned) = mark ? mark_frame_tile : draw_frame_tile;
    t(p, x, y, 0);
    for (unsigned i = 0; i < (w - 9u) / 8u; ++i) {
        t(p, x + 8u + i * 8u, y, 1u + i % 4u);
    }
    t(p, x + w - 8u, y, 5);
    for (unsigned i = 0; i < (h - 9u) / 8u; ++i) {
        t(p, x, y + 8u + i * 8u, 6u + i % 4u);
        t(p, x + w - 8u, y + 8u + i * 8u, 10u + i % 4u);
    }
    t(p, x, y + h - 8u, 14);
    for (unsigned i = 0; i < (w - 9u) / 8u; ++i) {
        t(p, x + 8u + i * 8u, y + h - 8u, 15u + i % 4u);
    }
    t(p, x + w - 8u, y + h - 8u, 19);
}
/* evidence (XEEN_DRAW_TRACE): "draw KIND X Y W H N HASH", KIND
 * frame (4F6F8) or openframe (1871B's 4F2A1): N opaque tile pixels and the
 * FNV-1a hash of their (offset lo, offset hi, value) triples in ascending
 * offset order, the values READ BACK from the framebuffer after the raster
 * (the original side hashes the bytes 4F281 stores). A frame that is only
 * counted, misplaced or overdrawn before this point cannot match. */
static void border_trace(const char *kind, const uint8_t *fb, const uint16_t r[4])
{
    static uint8_t mask[XEEN_PRESENT_PIXELS];
    uint32_t h = 2166136261u;
    unsigned n = 0;
    if (!xeen_presentation_tracing()) {
        return;
    }
    memset(mask, 0, sizeof mask);
    border_layout(mask, r, 1);
    for (unsigned i = 0; i < XEEN_PRESENT_PIXELS; ++i) {
        if (mask[i]) {
            const uint8_t b[3] = {(uint8_t)(i & 255u), (uint8_t)(i >> 8), fb[i]};
            for (unsigned k = 0; k < 3u; ++k) {
                h ^= b[k];
                h *= 16777619u;
            }
            ++n;
        }
    }
    xeen_presentation_trace("%s %u %u %u %u %u %08x", kind, (unsigned)r[0], (unsigned)r[1],
                            (unsigned)r[2], (unsigned)r[3], n, (unsigned)h);
}
/* The two windows this file opens itself, as 4F6F8 frames them (x, y, width, height): window 38 (26h), an event's
 * text in the corner below the icons, and window 11 (0Bh), the 3D view the NPC dialogue covers. Their text and their
 * cleared inside start WINDOW_BORDER pixels in. */
static const uint16_t window38_rect[4] = {225, 140, 94, 59};
static const uint16_t window11_rect[4] = {8, 8, 216, 132};
#define WINDOW_BORDER 8u
#define TEXT_DEFAULT_BACKGROUND 153u /* the windows' inside, and 07nnn's background when its value is invalid */
/* Clears RECT's inside (inset by its border) in PIXELS to the default background. */
static void clear_inside(uint8_t *pixels, const uint16_t rect[4])
{
    for (unsigned row = rect[1] + WINDOW_BORDER; row < rect[1] + rect[3] - WINDOW_BORDER; ++row) {
        memset(pixels + row * XEEN_PRESENT_WIDTH + rect[0] + WINDOW_BORDER, TEXT_DEFAULT_BACKGROUND, rect[2] - 2u * WINDOW_BORDER);
    }
}
/* Copies RECT's pixels of the 320-wide FRAME into SAVED (row after row, RECT's width each), or back when RESTORE. */
static void save_rect(uint8_t *frame, uint8_t *saved, const uint16_t rect[4], int restore)
{
    for (unsigned row = 0; row < rect[3]; ++row) {
        uint8_t *at = frame + (rect[1] + row) * XEEN_PRESENT_WIDTH + rect[0], *keep = saved + row * rect[2];
        memcpy(restore ? at : keep, restore ? keep : at, rect[2]);
    }
}
/** Draws the border of text window 38 (225,140 94x59; 4F6F8(26h)). */
static void draw_window38_frame(uint8_t *p)
{
    border_layout(p, window38_rect, 0);
    border_trace("frame", p, window38_rect);
}
/**
 * Opens text window 38 with TEXT: saves the covered pixels, draws the frame and lays the text out
 * with the original's control codes, then presents the window. Returns XEEN_BAD_INPUT without a
 * loaded font or a frame buffer.
 */
xeen_status xeen_text_window_show(xeen_text_window *w, xeen_presentation *p, const uint8_t *text,
                                  size_t size)
{
    uint8_t trial[XEEN_PRESENT_PIXELS];
    xeen_text_layout s;
    if (!w || !p || !text || !size || !w->loaded || !p->framebuffer ||
        p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    /* Selected handler decoration: CR,03,c (315F4's DS:62FA, 337D7's CR-headed form). */
    if (size < 4 || text[0] != TEXT_HOME || text[1] != TEXT_JUSTIFY || text[2] != 'c' || text[size - 1] != 0) {
        return XEEN_SERVICE_REQUIRED;
    }
    memcpy(trial, p->framebuffer, sizeof trial);
    if (!w->active) {
        draw_window38_frame(trial);
    }
    clear_inside(trial, window38_rect);
    /* The text is 4E38C(26h)'s own layout (xeen_text_layout_run on
     * window 26h = 225, 140, 94, 59, border 8: the image's static table), from
     * a neutral carried state (normal font, colour 0, as before). 315F4 ignores
     * 4E38C's return: a text needing a fifth line stops after the fourth (4E73C
     * CF), e.g. Clouds map 15's bottle "The bottle had a scroll of Enchant
     * Item ..."; a line wrapped at a double
     * space is centred without its trailing spaces (4E44A), e.g. "Roc Nest.
     * Destroy?" (4E427's mid-word break is part of
     * the port).
     * Original-byte census: xeen_dat_evidence/phase153/textcensus (all 2,400
     * shipped op 1 texts, pixel-equal). */
    xeen_text_layout_init(&s, window38_rect, WINDOW_BORDER);
    (void)xeen_text_layout_run(&s, w, trial, text, size, 0);
    if (!w->active) {
        save_rect(p->framebuffer, w->saved, window38_rect, 0);
    }
    memcpy(p->framebuffer, trial, sizeof trial);
    w->active = 1;
    return XEEN_OK;
}
/**
 * Closes text window 38, restoring the pixels it covered on both display pages; returns
 * XEEN_SERVICE_REQUIRED for any other window or when it is not open.
 */
xeen_status xeen_text_window_close(xeen_text_window *w, xeen_presentation *p, uint16_t window)
{
    if (!w || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    if (window != XEEN_WINDOW_EVENT_TEXT || !w->active) {
        return XEEN_SERVICE_REQUIRED;
    }
    save_rect(p->framebuffer, w->saved, window38_rect, 1);
    xeen_presentation_restore_rect(p, window38_rect[0], window38_rect[1], window38_rect[2],
                                   window38_rect[3]); /* Both display pages */
    w->active = 0;
    return XEEN_OK;
}

/* The scene overlay's control stream (window 3, drawn by 4E38C): 02 the reduced font, 0C "08" colour 08, 03 c
 * centred, 09 xxx and 0B yyy the position, the text, then 03 l, 0C d, 01 and the NUL. */
static const uint8_t overlay_prefix[] = {TEXT_REDUCED_FONT, TEXT_COLOUR, '0', '8', TEXT_JUSTIFY, 'c', TEXT_X};
static const uint8_t overlay_suffix[] = {TEXT_JUSTIFY, 'l', TEXT_COLOUR, 'd', TEXT_NORMAL_FONT, 0};
enum {
    OVERLAY_X_DIGITS = 7,   /* after the prefix */
    OVERLAY_Y_CONTROL = 10, /* the 0B */
    OVERLAY_Y_DIGITS = 11,
    OVERLAY_TEXT = 14,      /* the text's first byte */
    OVERLAY_MIN_SIZE = 20   /* the prefix, x, y, the suffix and its NUL around at least nothing */
};
#define OVERLAY_COLOUR 8u     /* the prefix's "08": text_colours[8] */
#define OVERLAY_RIGHT  230u   /* the overlay's area: x 0..230, y 0..139 */
#define OVERLAY_BOTTOM 139u

/* The three decimal digits at T as a number; -1 when one is not a digit. */
static int three_digits(const uint8_t *t)
{
    int n = 0;
    for (unsigned i = 0; i < 3u; ++i) {
        if (t[i] < '0' || t[i] > '9') {
            return -1;
        }
        n = n * 10 + (t[i] - '0');
    }
    return n;
}
/* TEXT (SIZE bytes, the NUL included) as an overlay stream: its x and y, and the end of its text (the suffix's
 * start); 0 when it is not one. */
static int overlay_parse(const uint8_t *text, size_t size, unsigned *x, unsigned *y, size_t *end)
{
    int vx, vy;
    if (size < OVERLAY_MIN_SIZE || text[size - 1] || memcmp(text, overlay_prefix, sizeof overlay_prefix) ||
        text[OVERLAY_Y_CONTROL] != TEXT_Y ||
        memcmp(text + size - sizeof overlay_suffix, overlay_suffix, sizeof overlay_suffix) ||
        (vx = three_digits(text + OVERLAY_X_DIGITS)) < 0 || (vy = three_digits(text + OVERLAY_Y_DIGITS)) < 0) {
        return 0;
    }
    *x = (unsigned)vx;
    *y = (unsigned)vy;
    *end = size - sizeof overlay_suffix;
    return 1;
}
/* The text TEXT[FROM..TO)'s width in the reduced font (a space measures 4) into *WIDTH; 0 for a byte outside
 * printable ASCII. */
static int overlay_width(const uint8_t *font, const uint8_t *text, size_t from, size_t to, unsigned *width)
{
    *width = 0;
    for (size_t at = from; at < to; ++at) {
        unsigned ch = text[at];
        if (ch < 32 || ch > 126) {
            return 0;
        }
        *width += ch == 32 ? 4u : font[FONT_WIDTHS + FONT_REDUCED + ch];
    }
    return 1;
}
static const uint8_t text_colours[40][4];
/**
 * Draws a scene overlay stream (the accepted form above) centred on its x at its y over the framebuffer, as 4E38C
 * draws it in window 3 (Phase 24's original-byte differential): the centre's rounding (2x + 1 - width) / 2, a space
 * measured 4 but advanced 3 when drawn. Returns XEEN_SERVICE_REQUIRED, the framebuffer untouched, for any other
 * stream, a text outside printable ASCII or one that leaves the area; XEEN_BAD_INPUT without a loaded font or a
 * framebuffer, or when a glyph cannot be drawn (nothing committed). No modal input.
 */
xeen_status xeen_text_overlay_show(xeen_text_window *w, xeen_presentation *p, const uint8_t *text,
                                   size_t size)
{
    uint8_t trial[XEEN_PRESENT_PIXELS];
    unsigned width, x, y;
    size_t end;
    if (!w || !p || !text || !w->loaded || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    if (!overlay_parse(text, size, &x, &y, &end) || !overlay_width(w->font, text, OVERLAY_TEXT, end, &width)) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (width >= 2u * x || x > OVERLAY_RIGHT || y > OVERLAY_BOTTOM) {
        return XEEN_SERVICE_REQUIRED;
    }
    x = (2u * x + 1u - width) / 2u; /* centred at the explicit x, with the original's rounding */
    if (x + width + 7u > OVERLAY_RIGHT) {
        return XEEN_SERVICE_REQUIRED;
    }
    memcpy(trial, p->framebuffer, sizeof trial);
    for (size_t at = OVERLAY_TEXT; at < end; ++at) {
        unsigned advance = 0;
        if (text[at] == 32) {
            x += 3;
            continue;
        }
        if (draw_glyph(w->font, sizeof w->font, trial, sizeof trial, x, y, text[at], 1, text_colours[OVERLAY_COLOUR],
                       &advance) != XEEN_OK) {
            return XEEN_BAD_INPUT;
        }
        x += advance;
    }
    memcpy(p->framebuffer, trial, sizeof trial);
    return XEEN_OK;
}

/* Text colours selected by "\fNN" (NN = 00..39, "\fd" = 00): the driver
 * table at 4853:1702 (4E780 copies entry [BEA]*4 to 16FE). */
static const uint8_t text_colours[40][4] = {
        {0, 25, 25, 25},    {0, 8, 8, 8},       {0, 15, 15, 15},    {0, 21, 21, 21},
        {0, 1, 1, 1},       {0, 33, 33, 33},    {0, 38, 38, 38},    {0, 43, 43, 43},
        {0, 49, 49, 49},    {0, 54, 54, 54},    {0, 61, 61, 61},    {0, 65, 65, 65},
        {0, 70, 70, 70},    {0, 76, 76, 76},    {0, 80, 80, 80},    {0, 85, 85, 85},
        {0, 93, 93, 93},    {0, 96, 96, 96},    {0, 101, 101, 101}, {0, 108, 108, 108},
        {0, 112, 112, 112}, {0, 117, 117, 117}, {0, 123, 123, 123}, {0, 128, 128, 128},
        {0, 133, 133, 133}, {0, 141, 141, 141}, {0, 144, 144, 144}, {0, 151, 151, 151},
        {0, 157, 157, 157}, {0, 164, 164, 164}, {0, 171, 171, 171}, {0, 176, 176, 176},
        {0, 182, 182, 182}, {0, 189, 189, 189}, {0, 192, 192, 192}, {0, 198, 198, 198},
        {0, 205, 205, 205}, {0, 208, 208, 208}, {0, 214, 214, 214}, {0, 219, 219, 219}};
xeen_status xeen_dialogue_show_layout(xeen_dialogue_window *w, const xeen_text_window *font,
                                      xeen_presentation *p, const uint8_t *text, size_t size,
                                      xeen_text_layout *s, size_t *rest)
{
    uint8_t trial[XEEN_PRESENT_PIXELS];
    xeen_text_layout carried;
    size_t r;
    if (!w || !font || !font->loaded || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS ||
        !text || !size || text[size - 1] || !s) {
        return XEEN_BAD_INPUT;
    }
    memcpy(trial, p->framebuffer, sizeof trial);
    if (!w->active) {
        border_layout(trial, window11_rect, 0);
        border_trace("openframe", trial, window11_rect);
    }
    clear_inside(trial, window11_rect);
    /* The carried state keeps its cursor, modes and colour; the
     * bounds are window 11's (4F1BE). */
    carried = *s;
    xeen_text_layout_init(s, window11_rect, WINDOW_BORDER);
    xeen_text_layout_carry(s, &carried, 1);
    r = xeen_text_layout_run(s, font, trial, text, size, 0);
    if (rest) {
        *rest = r;
    }
    if (!w->active) {
        save_rect(p->framebuffer, w->saved, window11_rect, 0);
    }
    memcpy(p->framebuffer, trial, sizeof trial);
    w->active = 1;
    return XEEN_OK;
}
/** Closes dialogue window 11, restoring the pixels it covered on both display pages. */
xeen_status xeen_dialogue_close(xeen_dialogue_window *w, xeen_presentation *p)
{
    if (!w || !w->active || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    save_rect(p->framebuffer, w->saved, window11_rect, 1);
    xeen_presentation_restore_rect(p, window11_rect[0], window11_rect[1], window11_rect[2],
                                   window11_rect[3]); /* Both display pages */
    w->active = 0;
    return XEEN_OK;
}
/** Closes a text box, restoring the pixels it covered on both display pages. */
xeen_status xeen_text_box_close(xeen_text_box *b, xeen_presentation *p)
{
    if (!b || !b->active || !p || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    for (unsigned row = 0; row < b->h; ++row) {
        memcpy(p->framebuffer + (b->y + row) * XEEN_PRESENT_WIDTH + b->x, b->saved + row * b->w, b->w);
    }
    xeen_presentation_restore_rect(p, b->x, b->y, b->w, b->h); /* Both display pages */
    b->active = 0;
    return XEEN_OK;
}

/* Bounds are x, y, width, height of the text area (callers pass the window
 * rectangle inset by the 8-pixel frame). */
void xeen_text_cursor_init(xeen_text_cursor *c, const uint16_t b[4])
{
    if (!c || !b) {
        return;
    }
    memset(c, 0, sizeof *c);
    c->x = b[0];
    c->y = b[1];
    c->background = TEXT_DEFAULT_BACKGROUND;
    memcpy(c->colours, text_colours[0], sizeof c->colours);
}
unsigned xeen_text_cursor_colour(const xeen_text_cursor *c)
{
    unsigned i;
    if (!c) {
        return 0;
    }
    for (i = 0; i < 40u; ++i) {
        if (!memcmp(c->colours, text_colours[i], sizeof c->colours)) {
            return i;
        }
    }
    return 0;
}
void xeen_text_cursor_set_colour(xeen_text_cursor *c, unsigned index)
{
    if (c) {
        memcpy(c->colours, text_colours[index < 40u ? index : 0u], sizeof c->colours);
    }
}
/**
 * Reads a COUNT-digit decimal argument of a text control code at *AT (a space counts as 0); sets
 * *BAD and stops at any other non-digit.
 */
static unsigned read_digits(const char *t, size_t *at, unsigned count, int *bad)
{
    unsigned v = 0, i;
    for (i = 0; i < count; ++i) {
        char ch = t[*at] == ' ' ? '0' : t[*at];
        if (ch < '0' || ch > '9') {
            if (t[*at]) {
                ++*at;
            }
            *bad = 1;
            return 0;
        }
        v = v * 10u + (unsigned)(ch - '0');
        ++*at;
    }
    return v;
}
/**
 * Returns the advance of character CH in the cursor's normal or reduced font (a space is 4 or 3).
 */
static unsigned char_width(const xeen_text_window *f, const xeen_text_cursor *c, unsigned ch)
{
    return ch == 32 ? (c->reduced ? 3u : 4u) : f->font[FONT_WIDTHS + (ch | (c->reduced ? FONT_REDUCED : 0u))];
}
/* xeen_text_write's bounds as edges: the right and bottom ones exclusive (x + width, y + height). */
typedef struct {
    int left, top, right, bottom;
} text_bounds;
/* The cursor's line height (10 rows, 9 in the reduced font) and a space's advance (4, 3 reduced). */
static int line_height(const xeen_text_cursor *c)
{
    return c->reduced ? 9 : 10;
}
static int space_advance(const xeen_text_cursor *c)
{
    return c->reduced ? 3 : 4;
}
/* The run of text from AT that the cursor's line can take: up to the next control (a colour code and its argument
 * belong to the run) or, when the next character would pass the bounds' RIGHT, to the run's last space after AT
 * (*WRAPPED; right-justified text never wraps). The line starts at the left bound for centred text, else at the
 * cursor. Returns the run's end; *WIDTH its width in pixels. */
static size_t text_run(const xeen_text_window *f, const xeen_text_cursor *c, const char *t, size_t at,
                       const text_bounds *b, int *width, int *wrapped)
{
    size_t end = at, last_space = (size_t)-1;
    int space_width = 0, origin = c->justify == TEXT_CENTRE ? b->left : c->x;
    *width = 0;
    *wrapped = 0;
    for (;;) {
        unsigned ch = (unsigned char)t[end];
        if (ch == TEXT_COLOUR) {
            ++end;
            if (t[end] && t[end] != 'd') {
                ++end;
                if (t[end]) {
                    ++end;
                }
            } else if (t[end]) {
                ++end;
            }
            continue;
        }
        if (ch < 32) {
            return end;
        }
        if (ch == 32) {
            last_space = end;
            space_width = *width;
        }
        if (origin + *width + (int)char_width(f, c, ch) > b->right && c->justify != TEXT_RIGHT) {
            *wrapped = 1;
            if (last_space != (size_t)-1 && last_space > at) {
                end = last_space;
                *width = space_width;
            }
            return end;
        }
        *width += (int)char_width(f, c, ch);
        ++end;
    }
}
/* Places the cursor for the run [AT, END) of WIDTH: right-justified, the run ends one pixel left of the cursor
 * (of the bounds' RIGHT when the cursor is at the line's start); centred, around the bounds' centre (at the line's
 * start) or the cursor, without a trailing space (4 or 5 pixels: the reduced or normal space and one more). Left
 * justification leaves the cursor. */
static void justify_line(xeen_text_cursor *c, const char *t, size_t at, size_t end, int width, const text_bounds *b)
{
    if (c->justify == TEXT_RIGHT) {
        if (c->x == b->left) {
            c->x = b->right;
        }
        c->x -= width + 1;
    } else if (c->justify == TEXT_CENTRE) {
        int w = width;
        if (end > at && t[end - 1] == ' ') {
            w -= c->reduced ? 4 : 5;
        }
        c->x = c->x == b->left ? (b->left + b->right + 1 - w) / 2 : (c->x * 2 - w) / 2;
    }
}
/* Writes the run [*AT, END) at the cursor: a colour code sets the colours (an invalid or out-of-table index: entry
 * 0), a space advances, and a glyph is drawn when its line fits above the bounds' BOTTOM and x is on the page (4E76E's
 * bound, y + the line height - 1 <= bottom: a reduced line fits one row lower than a normal one, as the bank's
 * Dep/With/ESC labels on its last line); a glyph not drawn still advances by its width. */
static void write_run(const xeen_text_window *f, uint8_t *p, size_t n, xeen_text_cursor *c, const char *t, size_t *at,
                      size_t end, int bottom)
{
    while (*at < end) {
        unsigned ch = (unsigned char)t[(*at)++], advance = 0;
        if (ch == TEXT_COLOUR) {
            int bad = 0;
            unsigned index = read_digits(t, at, 2, &bad);
            memcpy(c->colours, text_colours[bad || index >= 40u ? 0u : index], sizeof c->colours);
            continue;
        }
        if (ch == 32) {
            c->x += space_advance(c);
            continue;
        }
        if (c->y + line_height(c) - 1 <= bottom && c->x >= 0 && c->x <= (int)GLYPH_MAX_X &&
            draw_glyph(f->font, sizeof f->font, p, n, (unsigned)c->x, (unsigned)c->y, ch, c->reduced, c->colours,
                       &advance) == XEEN_OK) {
            c->x += (int)advance;
        } else {
            c->x += (int)char_width(f, c, ch);
        }
    }
}
/* The bounds B on page P cleared to the cursor's background (control 0D). */
static void clear_bounds(const xeen_text_cursor *c, uint8_t *p, const text_bounds *b)
{
    for (int y = b->top; y < b->bottom; ++y) {
        memset(p + (size_t)y * XEEN_PRESENT_WIDTH + (size_t)b->left, c->background, (size_t)(b->right - b->left));
    }
}
/* A box of the cursor's background, W pixels wide and one line high, at the cursor (ending there when
 * right-justified), clipped to the page (control 04). */
static void background_box(const xeen_text_cursor *c, uint8_t *p, unsigned w)
{
    int x = c->x - (c->justify == TEXT_RIGHT ? (int)w : 0);
    for (int y = c->y; y < c->y + line_height(c) && y < 200; ++y) {
        for (int i = x; i < x + (int)w && i < 320; ++i) {
            if (i >= 0) {
                p[(size_t)y * XEEN_PRESENT_WIDTH + (size_t)i] = c->background;
            }
        }
    }
}
/* The control code CH that ended a run (its arguments read from *AT) inside the bounds B:
 * 0D clears the bounds to the background and homes the cursor; 01/02 the normal and the reduced font; 03 and a
 * letter the justification ('r', 'c', else left); 04nnn a box of the background nnn pixels wide (ending at the cursor
 * when right-justified), clipped to the page; 05 nothing; 06 a space; 07nnn the background (153 when invalid or
 * above 255); 09nnn and 0Bnnn x and y from the bounds' origin, at most the right or bottom bound; 0A a new line.
 * A control's digits are read (read_digits) even when they are not valid. Returns 0 for any other control, which ends
 * the text. */
static int text_control(xeen_text_cursor *c, uint8_t *p, const char *t, size_t *at, unsigned ch, const text_bounds *b)
{
    int bad = 0;
    unsigned v;
    switch (ch) {
        case TEXT_HOME:
            clear_bounds(c, p, b);
            c->x = b->left;
            c->y = b->top;
            return 1;
        case TEXT_NORMAL_FONT:
            c->reduced = 0;
            return 1;
        case TEXT_REDUCED_FONT:
            c->reduced = 1;
            return 1;
        case TEXT_JUSTIFY: {
            char j = t[*at];
            if (j) {
                ++*at;
            }
            c->justify = (uint8_t)(j == 'r' ? TEXT_RIGHT : j == 'c' ? TEXT_CENTRE : TEXT_LEFT);
            return 1;
        }
        case TEXT_BOX:
            background_box(c, p, read_digits(t, at, 3, &bad));
            return 1;
        case TEXT_NOTHING:
            return 1;
        case TEXT_SPACE:
            c->x += space_advance(c);
            return 1;
        case TEXT_BACKGROUND:
            v = read_digits(t, at, 3, &bad);
            c->background = (uint8_t)(bad || v > 255u ? TEXT_DEFAULT_BACKGROUND : v);
            return 1;
        case TEXT_X:
            v = read_digits(t, at, 3, &bad);
            c->x = b->left + (int)v > b->right ? b->right : b->left + (int)v;
            return 1;
        case TEXT_NEWLINE:
            c->x = b->left;
            c->y += line_height(c);
            return 1;
        case TEXT_Y:
            v = read_digits(t, at, 3, &bad);
            c->y = b->top + (int)v > b->bottom ? b->bottom : b->top + (int)v;
            return 1;
        default:
            return 0;
    }
}
/* Writes T inside the bounds B (x, y, width, height) of the 320x200 page P (N bytes, at least 64,000), line by line:
 * each line's run (text_run), its placement (justify_line) and its glyphs (write_run); a run cut at a space continues
 * on the next line at the bounds' left, its leading spaces skipped; a control after a run is applied (text_control)
 * and the text ends at its NUL or an unknown control. CURSOR carries the position, colours and modes from call to
 * call (NULL: a fresh cursor at the bounds' origin). XEEN_BAD_INPUT for a missing argument, an unloaded font or
 * bounds past the page. */
xeen_status xeen_text_write(const xeen_text_window *f, uint8_t *p, size_t n, const uint16_t b[4],
                            const char *t, xeen_text_cursor *cursor)
{
    xeen_text_cursor local;
    xeen_text_cursor *c = cursor ? cursor : &local;
    size_t at = 0;
    text_bounds edges;
    if (!f || !f->loaded || !p || n < XEEN_PRESENT_PIXELS || !b || !t || (unsigned)b[0] + b[2] > XEEN_PRESENT_WIDTH ||
        (unsigned)b[1] + b[3] > 200u) {
        return XEEN_BAD_INPUT;
    }
    edges = (text_bounds){b[0], b[1], b[0] + b[2], b[1] + b[3]};
    if (!cursor) {
        xeen_text_cursor_init(&local, b);
    }
    for (;;) {
        int width, wrapped;
        size_t end = text_run(f, c, t, at, &edges, &width, &wrapped);
        unsigned ch;
        justify_line(c, t, at, end, width, &edges);
        write_run(f, p, n, c, t, &at, end, edges.bottom);
        if (wrapped) {
            while (t[at] == ' ') {
                ++at;
            }
            c->x = edges.left;
            c->y += line_height(c);
            continue;
        }
        ch = (unsigned char)t[at];
        if (!ch) {
            return XEEN_OK;
        }
        ++at;
        if (!text_control(c, p, t, &at, ch, &edges)) {
            return XEEN_OK;
        }
    }
}
void xeen_text_cursor_erase(uint8_t *p, size_t n, const xeen_text_cursor *c)
{
    int x, y;
    if (!p || n < XEEN_PRESENT_PIXELS || !c) {
        return;
    }
    for (y = c->y; y < c->y + 8 && y < 200; ++y) {
        for (x = c->x; x < c->x + 8 && x < 320; ++x) {
            if (x >= 0 && y >= 0) {
                p[(size_t)y * XEEN_PRESENT_WIDTH + (size_t)x] = c->background;
            }
        }
    }
}
xeen_status xeen_text_cursor_draw(const xeen_text_window *f, uint8_t *p, size_t n,
                                  const xeen_text_cursor *c, unsigned *frame)
{
    /* The cursor's blink glyphs (CS:17CE), one per call: *FRAME counts down 5..0 (CS:0C10). */
    static const uint8_t frames[6] = {32, 124, 126, 127, 126, 124};
    unsigned advance = 0;
    if (!f || !f->loaded || !p || n < XEEN_PRESENT_PIXELS || !c || !frame) {
        return XEEN_BAD_INPUT;
    }
    *frame = *frame ? *frame - 1u : 5u;
    xeen_text_cursor_erase(p, n, c);
    if (frames[*frame] == 32 || c->x < 0 || c->y < 0 || c->x > (int)GLYPH_MAX_X || c->y > (int)GLYPH_MAX_Y) {
        return XEEN_OK;
    }
    return draw_glyph(f->font, sizeof f->font, p, n, (unsigned)c->x, (unsigned)c->y,
                      frames[*frame], c->reduced, c->colours, &advance);
}
/* A frame rectangle R (x, y, width, height) on a 64000-byte page P: at least two 8-pixel tiles each way, on
 * the screen. */
static int frame_rect_ok(const uint8_t *p, size_t n, const uint16_t r[4])
{
    return p && n >= 64000u && r && r[2] >= 16u && r[3] >= 16u && (unsigned)r[0] + r[2] <= 320u &&
           (unsigned)r[1] + r[3] <= 200u;
}
xeen_status xeen_text_border(uint8_t *p, size_t n, const uint16_t r[4])
{
    if (!frame_rect_ok(p, n, r)) {
        return XEEN_BAD_INPUT;
    }
    border_layout(p, r, 0);
    border_trace("frame", p, r);
    return XEEN_OK;
}
xeen_status xeen_text_frame(uint8_t *p, size_t n, const uint16_t r[4])
{
    if (!frame_rect_ok(p, n, r)) {
        return XEEN_BAD_INPUT;
    }
    border_layout(p, r, 0);
    border_trace("openframe", p, r);
    for (unsigned row = r[1] + 8u; row < (unsigned)r[1] + r[3] - 8u; ++row) {
        memset(p + row * XEEN_PRESENT_WIDTH + r[0] + 8u, TEXT_DEFAULT_BACKGROUND, r[2] - 16u);
    }
    return XEEN_OK;
}

/* 4E38C, one call (the layout routine itself, from the listing
 * 4E38C..4E7A5). Per line (4E3BC) the run is measured from x (from the
 * left bound when centred) with 4E6E6: a glyph's width (the font's 1000h
 * table), a space 4 in either font, 08 followed by a space backs up 2,
 * 0C skips its colour digits, any other control stops the run before it;
 * reaching the right bound backs up one character and marks a wrap. A
 * wrapped left or centred run ends at its last space (trailing spaces
 * dropped); a run without one keeps what fits unless a left-justified
 * line already holds text, which moves to the next line first. Centred
 * runs start at ceil(((x == left ? left + right + 1 : 2x) - width) / 2)
 * (a trailing space counts 4, 3 in the reduced font, less), right-justified
 * ones end at the right bound (or x). Drawing advances x by the glyph
 * width (a space 4, 3 reduced); controls: 0D clears the bounds and homes,
 * 01/02 the normal/reduced font, 03 c|r|l, 04nnn a background box, 05
 * nothing, 06 a space, 07nnn the background, 08 the overstrike, 09nnn x,
 * 0A a new line, 0Bnnn y, 0Cnn/0Cd the colour; NUL ends (the NULL return).
 * A new line (4E73C) skips spaces, homes x and moves y by 10 (9 reduced);
 * when its last row passes the bottom bound the rest of the text is
 * returned. */
void xeen_text_layout_carry(xeen_text_layout *s, const xeen_text_layout *was, int cursor)
{
    if (cursor) {
        s->x = was->x;
        s->y = was->y;
    }
    s->justify = was->justify;
    s->reduced = was->reduced;
    s->background = was->background;
    s->colour = was->colour;
}
static unsigned layout_width(const xeen_text_window *f, const xeen_text_layout *s, unsigned ch)
{
    return f->font[FONT_WIDTHS + ((ch & 0x7fu) | (s->reduced ? FONT_REDUCED : 0u))];
}
/** Returns the 7-bit character at AT of the text, or 0 past its end. */
static unsigned layout_char(const uint8_t *t, size_t n, size_t at)
{
    return at < n ? (unsigned)(t[at] & 0x7fu) : 0u;
}
/**
 * Reads a COUNT-digit argument of a text control code like the original's 4E7A6 (a space reads as
 * 0, another non-digit ends the number with CF set and that character's value minus 30h in AX).
 */
static unsigned layout_digits(const uint8_t *t, size_t n, size_t *at, unsigned count, int *bad)
{
    /* 4E7A6: a space reads as 0; another non-digit ends the number (CF).
     * AX is then that character's AL - 30h (4E7B5; AH stays 0),
     * not the digits so far: 04, 09 and 0B use AX without testing CF
     * (4E584, 4E678, 4E6A2), only 07 and 0C do (4E5E6, 4E6C0). E.g.
     * "\tABC" moves x to left + 11h ('A' - 30h), then "BC" is text. */
    unsigned v = 0;
    *bad = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned c = layout_char(t, n, *at);
        ++*at;
        if (c == ' ') {
            c = '0';
        }
        if (c < '0' || c > '9') {
            *bad = 1;
            return (c - 0x30u) & 0xffu;
        }
        v = v * 10u + (c - '0');
    }
    return v;
}
/**
 * Initializes a text layout for WINDOW (x, y, width, height) inset by BORDER pixels, with the
 * cursor at its top left.
 */
void xeen_text_layout_init(xeen_text_layout *s, const uint16_t window[4], unsigned border)
{
    if (!s || !window) {
        return;
    }
    memset(s, 0, sizeof *s);
    s->left = window[0] + (int)border;
    s->right = s->left + window[2] - 2 * (int)border;
    s->top = window[1] + (int)border;
    s->bottom = s->top + window[3] - 2 * (int)border;
    s->x = s->left;
    s->y = s->top;
    s->background = TEXT_DEFAULT_BACKGROUND;
    s->colour = 0;
}
/**
 * Moves the layout to the start of the next line (10 pixels, 9 in the reduced font), skipping the
 * spaces at AT; returns nonzero when that line no longer fits the window.
 */
static int layout_newline(xeen_text_layout *s, const uint8_t *t, size_t n, size_t *at)
{
    int dy = s->reduced ? 9 : 10;
    while (layout_char(t, n, *at) == ' ') {
        ++*at;
    }
    s->x = s->left;
    s->y += dy;
    return s->y + dy - 1 > s->bottom;
}
/**
 * Draws character CH at the layout's cursor when it is on screen (the cursor is advanced by the
 * caller).
 */
static void layout_glyph(const xeen_text_window *f, const xeen_text_layout *s, uint8_t *p,
                         unsigned ch)
{
    unsigned advance = 0;
    if (p && s->x >= 0 && s->x <= 312 && s->y >= 0 && s->y + 9 <= 200) {
        (void)draw_glyph(f->font, sizeof f->font, p, 64000u, (unsigned)s->x, (unsigned)s->y, ch,
                         s->reduced, text_colours[s->colour < 40u ? s->colour : 0u], &advance);
    }
}
/**
 * One character of a measured run (4E6E6): adds a glyph's width (the font's 1000h table), a space's
 * 4 in either font, or -2 for an overstrike 08 followed by a space to *WIDTH; 0C skips its colour
 * digits ('d' alone). Returns 1 at any other control, which ends the run: AT is left on it (an 08
 * without a space, two back). *CH is the character read.
 */
static int measure_step(const xeen_text_window *f, const xeen_text_layout *s, const uint8_t *t,
                        size_t n, size_t *at, int *width, unsigned *ch)
{
    unsigned c = layout_char(t, n, (*at)++);
    *ch = c;
    if (c > ' ') {
        *width += (int)layout_width(f, s, c);
    } else if (c == ' ') {
        *width += 4;
    } else if (c == TEXT_OVERSTRIKE) {
        if (layout_char(t, n, (*at)++) == ' ') {
            *width -= 2;
        } else {
            *at -= 2;
            return 1;
        }
    } else if (c == TEXT_COLOUR) {
        if (layout_char(t, n, (*at)++) != 'd') {
            ++*at;
        }
    } else {
        --*at;
        return 1;
    }
    return 0;
}
/* The run of a line (4E3BC): measured from the cursor (from the left bound when centred) until a
 * control ends it (*STOPPED) or it reaches the right bound (*WRAP: the last character is backed off).
 * Returns the offset after the run; *WIDTH is the cursor position the run reached. */
static size_t measure_run(const xeen_text_window *f, const xeen_text_layout *s, const uint8_t *t,
                          size_t n, size_t at, int *width, int *stopped, int *wrap)
{
    unsigned ch;
    *width = s->justify == TEXT_CENTRE ? s->left : s->x;
    *stopped = *wrap = 0;
    for (;;) {
        if (measure_step(f, s, t, n, &at, width, &ch)) {
            *stopped = 1;
            return at;
        }
        if (*width >= s->right) {
            *wrap = 1;
            return at - 1u;
        }
    }
}
/* A wrapped left or centred run's break (4E416..4E459): the scan walks back from END to the run's last
 * space, then back over the spaces before it. Returns 1 with *STOP one past the last character to draw,
 * or 0 with *STOP at END (the run without its last character) when no space lies after START.
 * The original compares DS pointers after each DEC CX (4E423, 4E455: JB while START < CX). A run's
 * pointer is never 0 (its texts are DS strings and buffers), so START - 1 lies below START: a run whose
 * first character already reaches the bound (START == END) ends the first scan at once, and when that
 * character is a space the second scan stops before it (*STOP = START: nothing drawn). The offsets here
 * are 0-based, so each test is made before the decrement it guards. */
static int find_break(const uint8_t *t, size_t n, size_t start, size_t end, size_t *stop)
{
    size_t cx = end;
    while (layout_char(t, n, cx) != ' ') {
        if (cx <= start + 1u) {
            *stop = end;
            return 0;
        }
        --cx;
    }
    while (layout_char(t, n, cx) == ' ') {
        if (cx <= start + 1u) {
            *stop = cx;
            return 1;
        }
        --cx;
    }
    *stop = cx + 1u;
    return 1;
}
/* A centred or right-justified run's start (4E44B..4E4C6): the run from AT up to (not including) STOP is measured
 * again (a trailing space counts 4, 3 in the reduced font, less); centred runs start at
 * ceil(((x == left ? left + right + 1 : 2x) - width) / 2) (16-bit, unsigned halving), right-justified
 * ones end at the right bound (or at x when it is not at the left bound). */
static void justify_run(const xeen_text_window *f, xeen_text_layout *s, const uint8_t *t, size_t n,
                        size_t at, size_t stop)
{
    int width = 0;
    size_t q = at;
    unsigned ch;
    for (;;) {
        if (measure_step(f, s, t, n, &q, &width, &ch) || q >= stop) {
            break;
        }
    }
    if (ch == ' ') {
        width -= 4;
        if (s->reduced) {
            ++width;
        }
    }
    if (s->justify == TEXT_CENTRE) {
        int base = s->x == s->left ? s->left + s->right + 1 : 2 * s->x;
        int v = base - width;
        s->x = (int)(((unsigned)(v & 0xffff) >> 1) + ((unsigned)v & 1u));
    } else {
        ++width;
        if (s->x == s->left) {
            s->x = s->right;
        }
        s->x -= width;
    }
}
/* 0D (4E52C): the bounds cleared to the background (rows top..bottom - 1 on the screen, columns left
 * .. right - 1, unclipped: every window lies on the screen) and the cursor homed. */
static void layout_clear(xeen_text_layout *s, uint8_t *p)
{
    if (p) {
        for (int y = s->top; y < s->bottom && y < 200; ++y) {
            if (y >= 0) {
                memset(p + (size_t)y * XEEN_PRESENT_WIDTH + (size_t)s->left, s->background,
                       (size_t)(s->right - s->left));
            }
        }
    }
    s->x = s->left;
    s->y = s->top;
}
/* 04nnn: a background box WIDTH pixels wide and one line high at the cursor (ending at it when
 * right-justified), clipped to the screen; the cursor stays. */
static void layout_box(const xeen_text_layout *s, uint8_t *p, unsigned width)
{
    int x0 = s->x - (s->justify == TEXT_RIGHT ? (int)width : 0);
    if (!p) {
        return;
    }
    for (int y = s->y; y < s->y + (s->reduced ? 8 : 9) && y < 200; ++y) {
        for (int i = x0; i < x0 + (int)width && i < 320; ++i) {
            if (i >= 0 && y >= 0) {
                p[(size_t)y * XEEN_PRESENT_WIDTH + (size_t)i] = s->background;
            }
        }
    }
}
/* 08c (4E5FA): back over the previous glyph (a space 3), never past the left bound, and draw C there
 * in place (06 stands for a space). 4E641..4E662 draw it with 16FF and 1700/1701 = BE8 (the
 * background: the glyph's ink is erased), then restore the colours and the cursor. Before, it was
 * drawn in the text colour (the Quests window's "\b \b*" marks). */
static void layout_overstrike(const xeen_text_window *f, xeen_text_layout *s, uint8_t *p,
                              unsigned c)
{
    if (c == ' ') {
        c = 0;
        s->x -= 3;
    } else {
        if (c == TEXT_SPACE) {
            c = 0x20;
        }
        s->x -= (int)layout_width(f, s, c);
    }
    if (s->x < s->left) {
        s->x = s->left;
    }
    if (c > ' ' && p && s->x >= 0 && s->x <= 312 && s->y >= 0 && s->y + 9 <= 200) {
        unsigned advance = 0;
        uint8_t ink[4];
        memcpy(ink, text_colours[s->colour < 40u ? s->colour : 0u], sizeof ink);
        ink[1] = ink[2] = ink[3] = s->background;
        (void)draw_glyph(f->font, sizeof f->font, p, 64000u, (unsigned)s->x, (unsigned)s->y, c,
                         s->reduced, ink, &advance);
    }
}
/*
 * The control CH (01..0D) read at *AT - 1 while drawing a run, with its arguments. Returns 0 to go on,
 * or 1 when the call returns *RESULT: the NUL (XEEN_TEXT_LAYOUT_END) or a new line past the bottom
 * bound (the rest's offset).
 */
static int layout_control(const xeen_text_window *f, xeen_text_layout *s, uint8_t *p,
                          const uint8_t *t, size_t n, size_t *at, unsigned ch, size_t *result)
{
    int bad = 0;
    unsigned v;
    switch (ch) {
        case TEXT_HOME:
            layout_clear(s, p);
            return 0;
        case TEXT_NORMAL_FONT:
            s->reduced = 0;
            return 0;
        case TEXT_REDUCED_FONT:
            s->reduced = 1;
            return 0;
        case TEXT_JUSTIFY:
            v = layout_char(t, n, (*at)++);
            s->justify = v == 'r' ? TEXT_RIGHT : v == 'c' ? TEXT_CENTRE : TEXT_LEFT;
            return 0;
        case TEXT_BOX:
            layout_box(s, p, layout_digits(t, n, at, 3, &bad));
            return 0;
        case TEXT_NOTHING:
            return 0;
        case TEXT_SPACE:
            s->x += (int)layout_width(f, s, 0x20);
            return 0;
        case TEXT_BACKGROUND:
            v = layout_digits(t, n, at, 3, &bad);
            s->background = (uint8_t)(bad ? TEXT_DEFAULT_BACKGROUND : v);
            return 0;
        case TEXT_OVERSTRIKE:
            layout_overstrike(f, s, p, layout_char(t, n, (*at)++));
            return 0;
        case TEXT_X: {
            int x = (int)layout_digits(t, n, at, 3, &bad) + s->left;
            s->x = x > s->right ? s->right : x;
            return 0;
        }
        case TEXT_NEWLINE:
            if (layout_newline(s, t, n, at)) {
                *result = *at;
                return 1;
            }
            return 0;
        case TEXT_Y: {
            int y = (int)layout_digits(t, n, at, 3, &bad) + s->top;
            s->y = y > s->bottom ? s->bottom : y;
            return 0;
        }
        case TEXT_COLOUR:
            v = layout_digits(t, n, at, 2, &bad);
            s->colour = (uint8_t)(bad ? 0u : v);
            return 0;
        default: /* the NUL */
            *result = XEEN_TEXT_LAYOUT_END;
            return 1;
    }
}
/* Draws a run from *AT up to (not including) STOP: glyphs advance the cursor by their width (a space 4, 3 in the
 * reduced font), 0E..1F are skipped, controls go to layout_control. Returns as layout_control. */
static int draw_run(const xeen_text_window *f, xeen_text_layout *s, uint8_t *p, const uint8_t *t,
                    size_t n, size_t *at, size_t stop, size_t *result)
{
    while (*at < stop) {
        unsigned ch = layout_char(t, n, (*at)++);
        if (ch > ' ') {
            layout_glyph(f, s, p, ch);
            s->x += (int)layout_width(f, s, ch);
        } else if (ch == ' ') {
            s->x += s->reduced ? 3 : 4;
        } else if (ch <= 13 && layout_control(f, s, p, t, n, at, ch, result)) {
            return 1;
        }
    }
    return 0;
}
/**
 * Lays out text T from offset AT in the window of S, drawing into P (or only measuring when P is
 * NULL), with the original's control codes: colours, justification, tabs, line breaks and the word
 * wrap of 4E38C. Line by line: the run is measured; a wrapped left or centred run breaks at its last
 * space (a run without one keeps what fits, unless a left-justified line already holds text, which
 * moves to the next line first); a centred or right-justified run is placed; the run is drawn; a
 * wrapped run that is not right-justified ends its line. Returns the offset where a full window
 * stopped the layout, or XEEN_TEXT_LAYOUT_END.
 */
size_t xeen_text_layout_run(xeen_text_layout *s, const xeen_text_window *f, uint8_t *p,
                            const uint8_t *t, size_t n, size_t at)
{
    if (!s || !f || !f->loaded || !t) {
        return XEEN_TEXT_LAYOUT_END;
    }
    for (;;) {
        int width, stopped, wrap;
        size_t end = measure_run(f, s, t, n, at, &width, &stopped, &wrap), stop = end + 1u, result;
        if (!stopped && s->justify <= 1 && width >= s->right && !find_break(t, n, at, end, &stop) &&
            !s->justify && s->x != s->left) {
            if (layout_newline(s, t, n, &at)) {
                return at;
            }
            continue;
        }
        if (s->justify >= 1) {
            justify_run(f, s, t, n, at, stop);
        }
        if (draw_run(f, s, p, t, n, &at, stop, &result)) {
            return result;
        }
        if (s->justify == 2 || !wrap) {
            continue;
        }
        if (layout_newline(s, t, n, &at)) {
            return at;
        }
    }
}
