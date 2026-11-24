#include "xeen/raster.h"
#include "xeen/byte_order.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Owner-approved bug fixes (NOT original behavior, off by default; xeen/bugfix/left_clip_colors.h,
 * xeen/bugfix/sprite_width_trim.h). Defined here, not in src/bugfix/, because tools compile this file on
 * its own; the bug-fix registry (src/bugfix/bugfixes.c) sets them. */
int xeen_bugfix_left_clip_colors_on;
int xeen_bugfix_sprite_width_trim_on;
static const uint16_t masks[16] = {0xffff, 0xffef, 0xefef, 0xefee, 0xeeee, 0xeeae, 0xaeae, 0xaeaa,
                                   0xaaaa, 0xaa8a, 0x8a8a, 0x8a88, 0x8888, 0x8880, 0x8080, 0x8000};
/* A sprite row's operations (the standard path 4B8xx..4BFxx; the enlarge path 4DD96 and the colour modes in
 * raster_modes.inc test the same opcode bit by bit). The opcode's top three bits are its kind, the low five K:
 *   0, 1  literal: the next OP + 1 bytes (1..64 pixels)
 *   2     run: one byte, K + 3 times
 *   3     copy: K + 4 bytes from BACK before the position after its LE16 BACK word
 *   4     pairs: two bytes alternating, (K + 2) x 2 pixels
 *   5     skip: K + 1 transparent pixels
 *   6, 7  steps: one byte, (OP & 7) + 3 pixels, each step's delta from step[] by (OP >> 2) & 14 */
enum {
    SPRITE_OP_LITERAL = 0,
    SPRITE_OP_RUN = 2,
    SPRITE_OP_COPY = 3,
    SPRITE_OP_PAIRS = 4,
    SPRITE_OP_SKIP = 5,
    SPRITE_OP_STEPS = 6
};
/* Driver CS:16E8 alternating pattern steps, indexed by (op>>2)&14. */
static const int step[16] = {0, 1, 1, 1, 2, 2, 3, 3, 0, -1, -1, -1, -2, -2, -3, -3};
/**
 * Counts how many of COUNT source pixels a scaled sprite keeps, rotating the 16-bit scale MASK one
 * bit per pixel (the original's scaling pattern); MASK keeps its rotation for the next row or
 * column.
 */
static unsigned scaled(unsigned count, uint16_t *mask)
{
    unsigned n = 0;
    while (count--) {
        unsigned bit = *mask >> 15;
        *mask = (uint16_t)((*mask << 1) | bit);
        n += bit;
    }
    return n;
}
/* 4B1CC adds its high carries (BX) only when the loop runs out (4B21F),
 * i.e. when the count is a multiple of 8; its early exits return AL. The
 * scaled scene-clipped heads count x offset and width with it and the y
 * offset with 4B224 (AL only), so offsets of sprites wider than 255
 * scaled pixels wrap modulo 256 there. */
static unsigned scaled_1cc(unsigned count, uint16_t *mask)
{
    unsigned n = scaled(count, mask);
    return count % 8u ? n & 0xffu : n;
}
#include "raster_modes.inc"
xeen_status xeen_raster_effect_cell(const uint8_t *sprite, size_t n, uint16_t cell,
                                    uint8_t *page, int16_t x, int16_t y, uint16_t scale,
                                    uint16_t flags, const uint16_t rows[2], const uint16_t rng[2],
                                    const uint8_t carry[2], uint16_t generator[2])
{
    uint8_t *src;
    rdrv r;
    int ok;
    if (!sprite || !page || !rows || !rng || !carry || !generator || (scale & XEEN_SCALE_ENLARGE)) {
        return XEEN_BAD_INPUT;
    }
    if (!(src = calloc(1, 65536u))) {
        return XEEN_BAD_INPUT;
    }
    memcpy(src, sprite, n < 65536u ? n : 65536u);
    memset(&r, 0, sizeof r);
    r.page = page;
    r.src = src;
    r.clip_top = rows[0];
    r.clip_bottom = rows[1];
    r.gen[0] = generator[0];
    r.gen[1] = generator[1];
    ok = rd_cell(&r, cell, (uint16_t)x, (uint16_t)y, scale, flags, rng, carry);
    free(src);
    if (!ok) {
        return XEEN_SERVICE_REQUIRED;
    }
    generator[0] = r.gen[0];
    generator[1] = r.gen[1];
    return XEEN_OK;
}
unsigned xeen_raster_effect_rng(uint16_t scale, uint16_t flags, uint16_t *ax)
{
    /* AX at the first 50525 call: the mode driver's own tables (raster_modes.inc): for mode 2 the
     * scale's entry of CS:190A (4CCC9, the mask it then stores in CS:1908), for mode 5 the thresholds
     * by timer. */
    unsigned mode = XEEN_DRAW_MODE_OF(flags);
    if ((scale & XEEN_SCALE_ENLARGE) || (mode != 2u && mode != 5u)) {
        return 0;
    }
    if (ax) {
        *ax = mode == 2u ? m2_190a[scale & 15u] : (flags & 0xffu) < 4u ? m5_e6[flags & 3u] : 0u;
    }
    return 2;
}
/*
 * The placement of one cell of the standard frame path (draw_scaled_cell): where its rows land and how
 * they are stored.
 */
typedef struct {
    uint8_t *out, *opaque; /* the page, and the written-pixel map (or NULL) */
    int base;              /* the first stored column (the row's x 0), mirrored or not */
    uint16_t start;        /* the column scale mask at the row's x 0 */
    unsigned w;            /* the stored columns: the cell's width, or a past_width row's extent */
    uint16_t flags;        /* 8000h mirrored, 2000h wrapping (the scene window's columns 8..223) */
    int flip, top, bottom; /* the row clip TOP..BOTTOM-1 */
} cell_place;
/* A literal's OP + 1 bytes from *POS (a row ending at END) into VALUES. */
static xeen_status decode_literal(const uint8_t *b, size_t *pos, size_t end, unsigned op, uint8_t *values,
                                  unsigned *count)
{
    *count = op + 1u;
    if (*count > end - *pos) {
        return XEEN_BAD_INPUT;
    }
    memcpy(values, b + *pos, *count);
    *pos += *count;
    return XEEN_OK;
}
/* A run: the byte at *POS, K + 3 times. */
static xeen_status decode_run(const uint8_t *b, size_t *pos, size_t end, unsigned k, uint8_t *values,
                              unsigned *count)
{
    *count = k + 3u;
    if (*pos >= end) {
        return XEEN_BAD_INPUT;
    }
    memset(values, b[(*pos)++], *count);
    return XEEN_OK;
}
/*
 * A copy: K + 4 bytes of the sprite (N bytes) from BACK (the LE16 word at *POS) before the position after that word;
 * a RESTART takes the byte before them too. A source before the sprite or past its end is XEEN_BAD_INPUT.
 */
static xeen_status decode_copy(const uint8_t *b, size_t n, size_t *pos, size_t end, unsigned k, int restart,
                               uint8_t *values, unsigned *count)
{
    unsigned back;
    size_t from;
    *count = k + 4u;
    if (end - *pos < 2u) {
        return XEEN_BAD_INPUT;
    }
    back = xeen_get_le16(b + *pos);
    *pos += 2;
    if (back > *pos) {
        return XEEN_BAD_INPUT;
    }
    from = *pos - back;
    if (*count > n - from) {
        return XEEN_BAD_INPUT;
    }
    if (restart) {
        if (!from) {
            return XEEN_BAD_INPUT;
        }
        --from;
        ++*count;
    }
    memcpy(values, b + from, *count);
    return XEEN_OK;
}
/*
 * Pairs: the two bytes at *POS alternating, (K + 2) x 2 pixels. A RESTART takes one pixel more (the first byte at
 * x = 223: 4BF2C, an odd remaining count). Unscaled in a wrapping row (XEEN_DRAW_CLIP_COLUMNS) the pattern restarts
 * at the scene window's edge: unmirrored where the run crosses column 8 (from there it begins again with the first
 * byte), mirrored where it crosses column 223 (from there the second byte leads). BASE, AT and FLIP place the run.
 */
static xeen_status decode_pairs(const uint8_t *b, size_t *pos, size_t end, unsigned k, int restart, uint16_t scale,
                                uint16_t flags, int flip, int base, unsigned at, uint8_t *values, unsigned *count)
{
    uint8_t a, c;
    unsigned i;
    *count = (k + 2u) * 2u;
    if (end - *pos < 2u) {
        return XEEN_BAD_INPUT;
    }
    a = b[(*pos)++];
    c = b[(*pos)++];
    if (restart) {
        ++*count;
        for (i = 0; i < *count; i++) {
            values[i] = (i & 1u) ? c : a;
        }
        return XEEN_OK;
    }
    for (i = 0; i < *count; i++) {
        values[i] = (i & 1u) ? c : a;
    }
    if (!scale && (flags & XEEN_DRAW_CLIP_COLUMNS) && !flip && base + (int)at < 8 && base + (int)(at + *count) >= 8) {
        unsigned cut = (unsigned)(8 - base - (int)at);
        for (i = cut; i < *count; i++) {
            values[i] = ((i - cut) & 1u) ? c : a;
        }
    }
    if (!scale && (flags & XEEN_DRAW_CLIP_COLUMNS) && flip && base - (int)at >= 223 &&
        base - (int)(at + *count) < 223) {
        unsigned cut = (unsigned)(base - (int)at - 223);
        for (i = cut; i < *count; i++) {
            values[i] = (i & 1u) ? a : c;
        }
    }
    return XEEN_OK;
}
/* Steps: the byte at *POS, then (OP & 7) + 3 pixels, each the previous plus the pattern's alternating deltas
 * (step[], the pattern (OP >> 2) & 14). */
static xeen_status decode_steps(const uint8_t *b, size_t *pos, size_t end, unsigned op, uint8_t *values,
                                unsigned *count)
{
    unsigned pattern = (op >> 2) & 14u, i;
    int c;
    *count = (op & 7u) + 3u;
    if (*pos >= end) {
        return XEEN_BAD_INPUT;
    }
    c = b[(*pos)++];
    for (i = 0; i < *count; i++) {
        values[i] = (uint8_t)c;
        c += step[pattern + (i & 1u)];
    }
    return XEEN_OK;
}
/**
 * Decodes the run of operation OP (the byte before *AT_POS; not a SPRITE_OP_SKIP) of a row ending at END into
 * VALUES (at most 72) and *OUT_COUNT, advancing *AT_POS, by its kind (decode_literal, _run, _copy, _pairs, _steps;
 * kind 5 decodes as steps). RESTART is the retained clip mode's restart at x=223 (draw_scaled_cell): one preceding
 * source byte or pixel more (a literal's opcode byte, a run's byte again, a copy's or the pairs' own). SCALE, FLAGS,
 * FLIP, BASE and AT (the row position) select the unscaled wrapping pair patterns' column-8 and column-223
 * restarts. Returns XEEN_BAD_INPUT for a truncated operation (*AT_POS then unchanged).
 */
static xeen_status decode_operation(const uint8_t *b, size_t n, size_t *at_pos, size_t end,
                                    unsigned op, int restart, uint16_t scale, uint16_t flags,
                                    int flip, int base, unsigned at, uint8_t *values,
                                    unsigned *out_count)
{
    unsigned cmd = op >> 5, k = op & 31u, count = 0;
    size_t pos = *at_pos, opcode = pos - 1u;
    xeen_status s;
    switch (cmd) {
        case SPRITE_OP_LITERAL:
        case SPRITE_OP_LITERAL + 1:
            s = decode_literal(b, &pos, end, op, values, &count);
            break;
        case SPRITE_OP_RUN:
            s = decode_run(b, &pos, end, k, values, &count);
            break;
        case SPRITE_OP_COPY:
            s = decode_copy(b, n, &pos, end, k, restart, values, &count);
            break;
        case SPRITE_OP_PAIRS:
            s = decode_pairs(b, &pos, end, k, restart, scale, flags, flip, base, at, values, &count);
            break;
        default:
            s = decode_steps(b, &pos, end, op, values, &count);
            break;
    }
    if (s != XEEN_OK) {
        return s;
    }
    if (restart && cmd < SPRITE_OP_RUN) {
        memmove(values + 1, values, count);
        values[0] = b[opcode];
        ++count;
    } else if (restart && cmd == SPRITE_OP_RUN) {
        values[count] = values[0];
        ++count;
    }
    *at_pos = pos;
    *out_count = count;
    return XEEN_OK;
}
/**
 * Stores a decoded row LINE (-1 transparent) at screen row DY: the columns the scale mask keeps, mirrored or
 * not, inside the column window (8 + SHIFT .. 223 + SHIFT when wrapping, else the page) and when CLIP_Y lies
 * in the row clip; STALE (a wrapping skip's stale limit) drops the row.
 */
static void plot_row(const cell_place *c, const int *line, int dy, int clip_y, int stale, int shift)
{
    uint16_t sx = c->start;
    int dx = c->base;
    for (unsigned i = 0; i < c->w; i++) {
        if (scaled(1, &sx)) {
            int colour = line[i];
            int left = (c->flags & XEEN_DRAW_CLIP_COLUMNS) ? 8 + shift : 0,
                right = (c->flags & XEEN_DRAW_CLIP_COLUMNS) ? 224 + shift : 320;
            if (colour >= 0 && !stale && dx >= left && dx < right && clip_y >= c->top &&
                clip_y < c->bottom && dy >= 0 && dy * 320 + dx < 64000) {
                size_t dst = (size_t)dy * 320u + (unsigned)dx;
                c->out[dst] = (uint8_t)colour;
                if (c->opaque) {
                    c->opaque[dst] = 1;
                }
            }
            dx += c->flip ? -1 : 1;
        }
    }
}
/* What every row of a standard-path cell shares: the scale, the flags (FLIP 8000h mirrored, WRAPS: scaled with the
 * scene window's columns 8..223), the declared width W and where the rows land (BASE, the column mask START). */
typedef struct {
    uint16_t scale, flags;
    int flip, wraps, base;
    uint16_t start;
    unsigned w;
} cell_shape;
/* The vertical walk of a cell: MASK the row scale mask, DY the screen row of the next kept row, NEXT_Y the one after,
 * CLIP_Y the row the clip compares (it can run ahead of DY: see skip_blank_rows), and the wrapping skips' quirks
 * STALE (the next kept row dropped), POST_SKIP and SHIFT (a mirrored skip's next dropped row moves the following one
 * a page row on). */
typedef struct {
    uint16_t mask;
    int dy, next_y, clip_y;
    int stale, post_skip, shift;
} cell_rows;
/*
 * The cell's horizontal placement: BASE is X plus the scaled x offset XO (subtracted when mirrored: the driver's
 * negative encoded offset), plus half the columns the scale drops from the width W (the cell is centred on its
 * unscaled box) and, mirrored, the scaled width itself (the row is stored right to left from there). START is the
 * column scale mask after the offset (where the row's x 0 begins). A wrapping cell scales with the 1CC variant.
 */
static void place_columns(cell_shape *c, int x, unsigned xo)
{
    uint16_t mx = masks[c->scale], z;
    unsigned sx = c->wraps ? scaled_1cc(xo, &mx) : scaled(xo, &mx), sw, cols;
    c->base = x + (c->flip ? -(int)sx : (int)sx);
    c->start = mx;
    z = c->flip ? masks[c->scale] : mx;
    cols = c->flip ? (c->w ? c->w - 1u : 0u) : c->w;
    sw = c->wraps ? scaled_1cc(cols, &z) : scaled(cols, &z);
    c->base += (int)((c->w - sw) / 2u) + (c->flip ? (int)sw : 0);
}
/*
 * A blank-row record (its length byte 0, consumed): its count byte + 1 rows are skipped, at most the H - *ROW
 * left. Scaled, the rows the scale keeps advance the walk and the clip row one further (the clip counts the skip as
 * a row of its own). Unscaled the walk simply moves on. A scaled wrapping skip leaves its quirks: plain, the next
 * kept row is dropped (STALE); mirrored, a row the scale drops next moves the following one a page row on
 * (POST_SKIP). Returns XEEN_BAD_INPUT for a truncated or overlong record.
 * Original: 4B914 (the clipped scaled skip advances 16E0/16E4 like 4B112 but keeps the head's 16E2 limit, so 4B9AE
 * drops the next kept line, 4B9FB); the mirrored skip 4C05A (4B27C -> 4B438 -> 4BFE6) sets 16E2 from the advanced
 * 16E0, but a line the scale drops afterwards (4C08C) steps DI back a row with that limit kept: the next kept line
 * is clipped against the row below, so its count x-327 (4C104) usually drops it and a start past 327 wraps into
 * columns 8.. of the next row.
 */
static xeen_status skip_blank_rows(const uint8_t *b, size_t n, size_t *pos, const cell_shape *c, unsigned h,
                                   unsigned *row, cell_rows *r)
{
    unsigned skip;
    if (c->scale && (c->flags & XEEN_DRAW_CLIP_COLUMNS)) {
        if (c->flip) {
            r->post_skip = 1;
            r->shift = 0;
        } else {
            r->stale = 1;
        }
    }
    if (*pos >= n) {
        return XEEN_BAD_INPUT;
    }
    skip = (unsigned)b[(*pos)++] + 1u;
    if (skip > h - *row) {
        return XEEN_BAD_INPUT;
    }
    if (c->scale) {
        unsigned kept = scaled(skip, &r->mask);
        r->clip_y += 1 + (int)kept;
        r->next_y += (int)kept;
        r->dy = r->next_y;
    } else {
        r->dy += (int)skip;
        r->next_y = r->dy + 1;
        r->clip_y = r->dy;
    }
    *row += skip;
    return XEEN_OK;
}
/* Whether the next operation of a row restarts at x = 223 one pixel early: only after a mirrored literal left the
 * clip parser retained (RETAINED, see retains_clip_parser), for a literal, repeat, pair or copy (a skip or a pattern
 * does not), with the row not at column 0 and that literal's last pixel at 223 (BASE - AT = 222). Original: 4BE8C. */
static int restarts_at_clip(const cell_shape *c, int retained, unsigned cmd, unsigned at)
{
    return retained && cmd != SPRITE_OP_SKIP && cmd < SPRITE_OP_STEPS && at != 0u && (int)(c->base - (int)at) == 222;
}
/* In a scaled wrapping row's clip-in phase (its operations left of the scene window's column 8), a copy whose kept
 * columns cross column 8 replays its pixels from the back word itself, an absolute source offset, where every other
 * copy reads pos - back: VALUES (COUNT bytes, decoded at column AT; the copy's operand ends at POS) are read again from
 * there. As for the effect modes' same idiom (raster_modes.inc), the source is the sprite's 64 KB segment: the offset
 * wraps at 16 bits and bytes past the sprite (N bytes at B) read 0. Callers skip this with the bug fix
 * left_clip_colors (pos - back kept). Original: 4BBCE..4BBDA (SI = [SI - 2]); the others 4BA6E..4BA71, 4B853, the
 * mirrored paths; 4C778/4D51C in the effect modes. */
static void replay_absolute_copy(const uint8_t *b, size_t n, size_t pos, const cell_shape *c, unsigned at,
                                 unsigned count, uint8_t *values)
{
    uint16_t clip_mask = c->start;
    int first = c->base + (int)scaled(at, &clip_mask);
    int past = first + (int)scaled(count, &clip_mask);
    if (first < 8 && past > 8) {
        uint16_t from = xeen_get_le16(b + pos - 2u);
        unsigned i;
        for (i = 0; i < count; i++, from++) {
            values[i] = from < n ? b[from] : 0u;
        }
    }
}
/* Whether a mirrored unscaled scene-clipped literal (CMD below SPRITE_OP_RUN, COUNT pixels from column AT) leaves
 * exactly one visible pixel (x = 223) and so returns to the clip parser: the next operation then restarts at x = 223
 * with one preceding source byte (a literal: its opcode; a copy: the byte before its source), one more repeat pixel,
 * or a pair pattern restarted at x = 223. Copies do not retain the clip parser; nor does a restarted operation.
 * Original: 4BEC6..4BEE0 (the JE uses SHR CX,1's flags); 4BF72 -> 4BF9A. */
static int retains_clip_parser(const cell_shape *c, unsigned cmd, int restart, unsigned at, unsigned count)
{
    return !c->scale && c->flip && (c->flags & XEEN_DRAW_CLIP_COLUMNS) && cmd < SPRITE_OP_RUN && !restart &&
           c->base - (int)at >= 223 && c->base - (int)(at + count) == 222;
}
/* The source columns a decoded row can hold (LINE's size): the past-width path stores up to here. */
#define ROW_COLUMNS 1024u
/* A row decode in progress: LINE the colours by source column, COLUMN the next one, FILLED how far LINE is
 * initialised (0..319 from the start, past-width columns as they are reached), RETAINED whether the previous
 * operation left the clip parser retained (retains_clip_parser), EXTENT the row's stored width so far, PAST_WIDTH
 * whether columns from W on are stored (stores_past_width); VALUES and COUNT the current operation's pixels. */
typedef struct {
    int *line;
    unsigned column, filled, extent;
    int retained, past_width;
    uint8_t values[72];
    unsigned count;
} row_decode;
/* Whether a row of cell C stores the columns its operations reach past the declared width W. The scaled, unmirrored
 * path without 2000h has no width clip: those source columns are stored too, at the next kept screen columns (e.g.
 * tower.vga rows of extent 321); the page's own clip still applies. The bug fix sprite_width_trim keeps only the
 * columns below W. Original: 4B028..4B1CB (Phase 237 class B). */
static int stores_past_width(const cell_shape *c)
{
    return c->scale && !(c->flags & XEEN_DRAW_CLIP_COLUMNS) && !c->flip && !xeen_bugfix_sprite_width_trim_on;
}
/*
 * The pixels of operation OP (not a skip; the byte before *POS) at the decode's column, into R's VALUES / COUNT.
 * The restart at x = 223 is decided from the previous operation before the operation is read; a wrapping unmirrored
 * copy replays its absolute source (replay_absolute_copy, unless the bug fix left_clip_colors); a restarted operation
 * then begins one column early, and whether this one retains the clip parser is recorded for the next. Returns
 * decode_operation's refusals, or XEEN_SERVICE_REQUIRED for a restart that would store left of column 8 (clip-mode
 * stores do not test the left limit; no operation is that long).
 */
static xeen_status decode_pixels(const uint8_t *b, size_t n, size_t *pos, size_t end, const cell_shape *c,
                                 row_decode *r, unsigned op)
{
    unsigned cmd = op >> 5;
    int restart = restarts_at_clip(c, r->retained, cmd, r->column);
    xeen_status s;
    r->retained = 0;
    if ((s = decode_operation(b, n, pos, end, op, restart, c->scale, c->flags, c->flip, c->base, r->column, r->values,
                              &r->count)) != XEEN_OK) {
        return s;
    }
    if (cmd == SPRITE_OP_COPY && c->wraps && !c->flip && !xeen_bugfix_left_clip_colors_on) {
        replay_absolute_copy(b, n, *pos, c, r->column, r->count, r->values);
    }
    if (restart) {
        if (223 - (int)r->count + 1 < 8) {
            return XEEN_SERVICE_REQUIRED;
        }
        --r->column;
    }
    r->retained = retains_clip_parser(c, cmd, restart, r->column, r->count);
    return XEEN_OK;
}
/* Stores the current operation's pixels (R's VALUES / COUNT) from the decode's column on. Real assets occasionally
 * contain padding past the declared width W: within-row transparency is preserved; the padding is dropped except on
 * the past-width path, which stores it below ROW_COLUMNS and widens the extent. The column advances past every
 * pixel. */
static void store_pixels(const cell_shape *c, row_decode *r)
{
    for (unsigned i = 0; i < r->count; i++, r->column++) {
        if (r->column < c->w) {
            r->line[r->column] = r->values[i];
        } else if (r->past_width && r->column < ROW_COLUMNS) {
            while (r->filled <= r->column) {
                r->line[r->filled++] = -1;
            }
            r->line[r->column] = r->values[i];
            if (r->column + 1u > r->extent) {
                r->extent = r->column + 1u;
            }
        }
    }
}
/*
 * Decodes one row record (its operations from *POS, past the length byte, to END) into LINE (ROW_COLUMNS entries):
 * the colour of each source column, -1 transparent; the first byte is the row's indent (the first column). On
 * success *EXTENT is the row's stored width: W, or further on the past-width path. A skip only moves the column;
 * every other operation is decoded (decode_pixels), then stored (store_pixels). Returns XEEN_BAD_INPUT for a
 * truncated row, one whose column passes ROW_COLUMNS or whose last operation does not end at END, or
 * decode_pixels' refusals.
 */
static xeen_status decode_row(const uint8_t *b, size_t n, size_t *pos, size_t end, const cell_shape *c, int *line,
                              unsigned *extent)
{
    row_decode r = {line, 0, 320u, c->w, 0, stores_past_width(c), {0}, 0};
    for (unsigned i = 0; i < 320u; ++i) {
        line[i] = -1;
    }
    r.column = b[(*pos)++];
    while (*pos < end) {
        unsigned op = b[(*pos)++];
        xeen_status s;
        if (op >> 5 == SPRITE_OP_SKIP) {
            r.retained = 0;
            r.column += (op & 31u) + 1u;
            continue;
        }
        if ((s = decode_pixels(b, n, pos, end, c, &r, op)) != XEEN_OK) {
            return s;
        }
        store_pixels(c, &r);
        if (r.column > ROW_COLUMNS) {
            return XEEN_BAD_INPUT;
        }
    }
    *extent = r.extent;
    return *pos != end ? XEEN_BAD_INPUT : XEEN_OK;
}
/**
 * Draws one sprite cell (frame part at offset POS of the sprite bytes) into OUT at X, Y.
 *
 * Applies the original's scale mask, horizontal flip (flags 8000h) and wrapping (2000h) and clips the rows to
 * TOP..BOTTOM; OPAQUE records which pixels were written. Each row record is decoded into a line (decode_row) and
 * stored when the row scale keeps it (plot_row); a row the scale drops steps the walk back, except on the plain
 * wrapping path. Returns XEEN_BAD_INPUT for a truncated cell, XEEN_SERVICE_REQUIRED for a cell larger than the
 * page.
 */
static xeen_status draw_scaled_cell(const uint8_t *b, size_t n, size_t pos, uint8_t *out,
                                    uint8_t *opaque, int x, int y, uint16_t scale, uint16_t flags,
                                    int top, int bottom)
{
    cell_shape c = {scale, flags, (flags & XEEN_DRAW_MIRRORED) != 0, scale && (flags & XEEN_DRAW_CLIP_COLUMNS), 0,
                    0, 0};
    cell_rows r = {masks[scale], 0, 0, 0, 0, 0, 0};
    unsigned xo, yo, h, row;
    if (pos > n || n - pos < 8) {
        return XEEN_BAD_INPUT;
    }
    xo = xeen_get_le16(b + pos);
    c.w = xeen_get_le16(b + pos + 2);
    yo = xeen_get_le16(b + pos + 4);
    h = xeen_get_le16(b + pos + 6);
    pos += 8;
    if (c.w > 320u || h > 200u || xo > 320u || yo > 200u) {
        return XEEN_SERVICE_REQUIRED;
    }
    place_columns(&c, x, xo);
    r.dy = y + (int)(c.wraps ? scaled(yo, &r.mask) & 0xffu : scaled(yo, &r.mask));
    r.next_y = r.dy + 1;
    r.clip_y = r.dy;
    for (row = 0; row < h;) {
        int line[ROW_COLUMNS];
        unsigned len, extent;
        xeen_status s;
        if (pos >= n) {
            return XEEN_BAD_INPUT;
        }
        len = b[pos++];
        if (!len) {
            if ((s = skip_blank_rows(b, n, &pos, &c, h, &row, &r)) != XEEN_OK) {
                return s;
            }
            continue;
        }
        if (len > n - pos) {
            return XEEN_BAD_INPUT;
        }
        if ((s = decode_row(b, n, &pos, pos + len, &c, line, &extent)) != XEEN_OK) {
            return s;
        }
        if (scaled(1, &r.mask)) {
            cell_place place = {out, opaque, c.base, c.start, extent, flags, c.flip, top, bottom};
            plot_row(&place, line, r.dy, r.clip_y, r.stale, r.shift);
            r.clip_y++;
            r.dy = r.next_y;
            r.next_y++;
            r.stale = 0;
            r.post_skip = 0;
            r.shift = 0;
        } else if (!(flags & XEEN_DRAW_CLIP_COLUMNS) || c.flip) {
            r.dy = r.next_y - 1;
            if (r.post_skip) {
                r.shift = 320;
            }
        }
        row++;
    }
    return XEEN_OK;
}
/*
 * The enlarged path (scale bit 8000h, tested before any flag): every source pixel becomes a 2 x 2 block, at the
 * page offset DI and DI + 320. Offsets are 16-bit, as in the original, and wrap.
 *
 * Placement: the cell's origin is the fixed column 9. When x plus the cell's x offset (START) is not negative it
 * is added to the row base, and every row's column counter starts from it as well; a row's first byte (its
 * indent, doubled) is added to the counter, and the counter, when not negative, to DI. Rows lie two screen rows
 * apart from y plus the cell's y offset; drawing ends at the first row whose base passes AC80h (row 138).
 *
 * Clipping: a pixel is drawn only while the column counter lies in 0..D5h; a clipped pixel advances the counter
 * alone. A cell that starts above row 9 has its rows up to row 9 skipped (skip_rows_above_9) and restarts at
 * row 9. A pixel past the 320 x 200 page is not stored (a guard: the row limit keeps every drawn block inside).
 *
 * Original: 4DD96 (4DDD4 the start, 4DFF8 the top clip); the operations literal 4DE76, repeat 4DE40, copy 4DEAA,
 * pair 4DEF0 (its colour swap 4DF51), skip 4DF5A, pattern 4DF82.
 */
#define ENLARGED_COLUMNS 0xd6
#define ENLARGED_ROW_LIMIT 0xac80u
#define ENLARGED_TOP_ROW 9
/* Skips the operations of one row record from *POS to END (a top-clipped row); returns 0 for a truncated one. */
static int skip_row_ops(const uint8_t *b, size_t n, size_t *pos, size_t end)
{
    while (*pos != end) {
        unsigned op;
        if (*pos >= n) {
            return 0;
        }
        op = b[(*pos)++];
        switch (op >> 5) { /* each kind's operand bytes (the kinds above) */
            case SPRITE_OP_LITERAL:
            case SPRITE_OP_LITERAL + 1:
                *pos += op + 1u; /* the K + 1 bytes (op below 40h: K = op) */
                break;
            case SPRITE_OP_RUN:
            case SPRITE_OP_STEPS:
            case SPRITE_OP_STEPS + 1:
                *pos += 1u; /* the byte */
                break;
            case SPRITE_OP_COPY:
            case SPRITE_OP_PAIRS:
                *pos += 2u; /* the BACK word; the two bytes */
                break;
            default: /* SPRITE_OP_SKIP: no operand */
                break;
        }
        if (*pos > end) {
            return 0;
        }
    }
    return 1;
}
/*
 * A cell placed above row 9 (its top row ROW): the row records of the rows above row 9 are skipped, a blank-row
 * record counting as one row whatever its count. *ROWS counts down the cell's rows, and is also taken for the
 * row that reaches row 9 (which is then drawn as the first row). Returns XEEN_BAD_INPUT for a truncated record.
 * Original: 4DFF8.
 */
static xeen_status skip_rows_above_9(const uint8_t *b, size_t n, size_t *pos, int16_t row, int16_t *rows)
{
    while (--*rows >= 0 && ++row != ENLARGED_TOP_ROW) {
        size_t end;
        if (*pos >= n) {
            return XEEN_BAD_INPUT;
        }
        if (!b[*pos]) { /* a blank-row record: its count is not read */
            if (n - *pos < 2) {
                return XEEN_BAD_INPUT;
            }
            *pos += 2;
            continue;
        }
        end = *pos + 1u + b[*pos];
        ++*pos;
        if (end > n || *pos >= end) {
            return XEEN_BAD_INPUT;
        }
        ++*pos; /* the indent */
        if (!skip_row_ops(b, n, pos, end)) {
            return XEEN_BAD_INPUT;
        }
    }
    return XEEN_OK;
}
/* Where the next pixel of an enlarged row goes: the page offset DI and the column counter. */
typedef struct {
    uint8_t *out, *opaque; /* the page, and the written-pixel map (or NULL) */
    uint16_t di;
    int16_t column;
} enlarged_pen;
/* Whether the pen's column is inside the drawn columns 0..D5h. */
static int pen_visible(const enlarged_pen *p)
{
    return p->column >= 0 && p->column < ENLARGED_COLUMNS;
}
/* One page pixel of COLOUR at offset AT, when it is inside the page. */
static void pen_store(const enlarged_pen *p, uint16_t at, unsigned colour)
{
    if (at < XEEN_PRESENT_PIXELS) {
        p->out[at] = (uint8_t)colour;
        if (p->opaque) {
            p->opaque[at] = 1;
        }
    }
}
/* Half of a source pixel: the column COLOUR at the pen and the row below it, when visible (the pen then moves one
 * pixel right); the column counter moves in any case. Every source pixel is two plots. */
static void pen_plot(enlarged_pen *p, unsigned colour)
{
    if (pen_visible(p)) {
        pen_store(p, p->di, colour);
        pen_store(p, (uint16_t)(p->di + 320u), colour);
        p->di = (uint16_t)(p->di + 1u);
    }
    p->column = (int16_t)(p->column + 1);
}
/* One source pixel of COLOUR: two plots. */
static void pen_pixel(enlarged_pen *p, unsigned colour)
{
    pen_plot(p, colour);
    pen_plot(p, colour);
}
/* COUNT source pixels of one COLOUR. */
static void pen_run(enlarged_pen *p, unsigned colour, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        pen_pixel(p, colour);
    }
}
/* The pair operation's COUNT pairs: the colour word AX's low byte as a whole pixel, then its high byte as two
 * plots. The original exchanges its two colour words only when a pair's last plot is drawn: with the column still
 * visible, the next pair starts from the other word (so the byte swap is undone every second time). */
static void pen_pairs(enlarged_pen *p, uint16_t ax, unsigned count)
{
    uint16_t bx = ax;
    for (unsigned i = 0; i < count; i++) {
        int swap;
        pen_pixel(p, ax & 255u);
        ax = (uint16_t)((ax >> 8) | (ax << 8));
        pen_plot(p, ax & 255u);
        swap = pen_visible(p);
        pen_plot(p, ax & 255u);
        if (swap) {
            uint16_t t = ax;
            ax = bx;
            bx = t;
        }
    }
}
/* COUNT transparent source pixels: the column moves two per pixel; once it is not negative, DI moves by the pixels'
 * width less the part that lay left of column 0. */
static void pen_skip(enlarged_pen *p, unsigned count)
{
    int16_t old = p->column;
    p->column = (int16_t)(p->column + 2 * (int16_t)count);
    if (p->column >= 0) {
        p->di = (uint16_t)(p->di + 2u * count + (old < 0 ? (uint16_t)old : 0u));
    }
}
/* COUNT source pixels from COLOUR on, the colour stepping by the pattern's two steps (STEPS[0], STEPS[1]) in turn. */
static void pen_steps(enlarged_pen *p, uint8_t colour, const int *steps, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        pen_pixel(p, colour);
        colour = (uint8_t)(colour + (unsigned)steps[i & 1u]);
    }
}
/* One operation of an enlarged row, read and bound-checked: KIND its SPRITE_OP_* kind (a literal's two codes as
 * SPRITE_OP_LITERAL), COUNT its source pixels, and its operand: SOURCE the COUNT colour bytes of a literal (in the
 * row) or a copy (anywhere in the sprite), COLOUR a repeat's colour or a pattern's first, WORD a pair's two colours,
 * STEPS a pattern's two steps. */
typedef struct {
    unsigned kind, count;
    const uint8_t *source;
    uint8_t colour;
    uint16_t word;
    const int *steps;
} enlarged_op;
/*
 * Reads the operation at *POS of a row ending at END (sprite bytes B, N long) into O and moves *POS past it.
 * Returns XEEN_BAD_INPUT for an operand past the row's end, or a copy whose source (BACK bytes before the
 * operand's end) begins before the sprite or runs past its end. Nothing is drawn: every check precedes the pixels.
 */
static xeen_status read_enlarged_op(const uint8_t *b, size_t n, size_t *pos, size_t end, enlarged_op *o)
{
    unsigned op = b[(*pos)++], k = op & 31u;
    o->kind = op >> 5;
    switch (o->kind) {
        case SPRITE_OP_LITERAL:
        case SPRITE_OP_LITERAL + 1: /* op + 1 pixels, each its own colour */
            o->kind = SPRITE_OP_LITERAL;
            o->count = op + 1u;
            if (o->count > end - *pos) {
                return XEEN_BAD_INPUT;
            }
            o->source = b + *pos;
            *pos += o->count;
            return XEEN_OK;
        case SPRITE_OP_RUN: /* k + 3 pixels of one colour */
            if (*pos >= end) {
                return XEEN_BAD_INPUT;
            }
            o->count = k + 3u;
            o->colour = b[(*pos)++];
            return XEEN_OK;
        case SPRITE_OP_COPY: { /* k + 4 pixels copied from BACK bytes before the operation's end */
            unsigned back;
            size_t from;
            if (end - *pos < 2u) {
                return XEEN_BAD_INPUT;
            }
            back = xeen_get_le16(b + *pos);
            *pos += 2;
            if (back > *pos) {
                return XEEN_BAD_INPUT;
            }
            from = *pos - back;
            o->count = k + 4u;
            if (o->count > n - from) {
                return XEEN_BAD_INPUT;
            }
            o->source = b + from;
            return XEEN_OK;
        }
        case SPRITE_OP_PAIRS: /* k + 2 times two colours */
            if (end - *pos < 2u) {
                return XEEN_BAD_INPUT;
            }
            o->count = k + 2u;
            o->word = xeen_get_le16(b + *pos);
            *pos += 2;
            return XEEN_OK;
        case SPRITE_OP_SKIP: /* k + 1 transparent pixels */
            o->count = k + 1u;
            return XEEN_OK;
        default: /* SPRITE_OP_STEPS: (op & 7) + 3 pixels, the colour stepping by pattern (op >> 2) & 14's steps */
            if (*pos >= end) {
                return XEEN_BAD_INPUT;
            }
            o->count = (op & 7u) + 3u;
            o->colour = b[(*pos)++];
            o->steps = step + ((op >> 2) & 14u);
            return XEEN_OK;
    }
}
/* Draws the read operation O with PEN, each source pixel a 2 x 2 block (the pen_* rules). */
static void draw_enlarged_op(enlarged_pen *pen, const enlarged_op *o)
{
    switch (o->kind) {
        case SPRITE_OP_LITERAL:
        case SPRITE_OP_COPY:
            for (unsigned i = 0; i < o->count; i++) {
                pen_pixel(pen, o->source[i]);
            }
            break;
        case SPRITE_OP_RUN:
            pen_run(pen, o->colour, o->count);
            break;
        case SPRITE_OP_PAIRS:
            pen_pairs(pen, o->word, o->count);
            break;
        case SPRITE_OP_SKIP:
            pen_skip(pen, o->count);
            break;
        default:
            pen_steps(pen, o->colour, o->steps, o->count);
            break;
    }
}
/*
 * Draws the operations of one row record from *AT to END with PEN: the operations of the normal decoder
 * (decode_operation's SPRITE_OP_* kinds, the operation's top three bits), each read whole (read_enlarged_op) before
 * it is drawn (draw_enlarged_op). *AT moves past the row only when every operation was read. Returns XEEN_BAD_INPUT
 * for read_enlarged_op's refusals (the operations before it stay drawn), or when the last operation does not end at
 * END.
 */
static xeen_status enlarged_row(const uint8_t *b, size_t n, size_t *at, size_t end, enlarged_pen *pen)
{
    size_t pos = *at;
    while (pos < end) {
        enlarged_op o;
        if (read_enlarged_op(b, n, &pos, end, &o) != XEEN_OK) {
            return XEEN_BAD_INPUT;
        }
        draw_enlarged_op(pen, &o);
    }
    *at = pos;
    return pos != end ? XEEN_BAD_INPUT : XEEN_OK;
}
/**
 * Draws one sprite cell (frame part at offset POS of the sprite bytes) at double size into OUT at X, Y (the
 * enlarged path above); returns XEEN_BAD_INPUT for a truncated cell. A row record is a length byte (0: a blank-row
 * record whose next byte counts the blank rows), an indent byte, then the row's operations.
 */
static xeen_status enlarged_cell(const uint8_t *b, size_t n, size_t pos, uint8_t *out,
                                 uint8_t *opaque, int x, int y)
{
    uint16_t xo, yo, h, start, top, base;
    int16_t rows;
    if (pos > n || n - pos < 8) {
        return XEEN_BAD_INPUT;
    }
    xo = xeen_get_le16(b + pos);
    yo = xeen_get_le16(b + pos + 4);
    h = xeen_get_le16(b + pos + 6);
    pos += 8;
    start = (uint16_t)(x + xo);
    top = (uint16_t)(y + yo);
    base = (uint16_t)((uint16_t)(top * 320u) + 9u - 640u);
    rows = (int16_t)h;
    if ((int16_t)top < ENLARGED_TOP_ROW) {
        if (skip_rows_above_9(b, n, &pos, (int16_t)top, &rows) != XEEN_OK) {
            return XEEN_BAD_INPUT;
        }
        base = (uint16_t)(ENLARGED_TOP_ROW * 320u + 9u - 640u);
    }
    if ((int16_t)start >= 0) {
        base = (uint16_t)(base + start);
    }
    while (--rows >= 0) {
        enlarged_pen pen = {out, opaque, 0, (int16_t)start};
        unsigned len;
        base = (uint16_t)(base + 640u);
        if (base > ENLARGED_ROW_LIMIT) {
            break;
        }
        if (pos >= n) {
            return XEEN_BAD_INPUT;
        }
        len = b[pos++];
        if (!len) { /* a blank-row record: COUNT more rows without pixels */
            uint16_t count;
            if (pos >= n) {
                return XEEN_BAD_INPUT;
            }
            count = b[pos++];
            rows = (int16_t)(rows - (int16_t)count);
            base = (uint16_t)(base + (uint16_t)(count * 640u));
            continue;
        }
        if (pos + len > n) {
            return XEEN_BAD_INPUT;
        }
        pen.di = base;
        pen.column = (int16_t)(pen.column + 2 * (int16_t)b[pos++]);
        if (pen.column >= 0) {
            pen.di = (uint16_t)(pen.di + (uint16_t)pen.column);
        }
        if (enlarged_row(b, n, &pos, pos - 1u + len, &pen) != XEEN_OK) {
            return XEEN_BAD_INPUT;
        }
    }
    return XEEN_OK;
}
/* The row clip CS:0C28/0C2A a draw is held to: the 3D view's rows (window 3, or a single draw's flag 4000h) or the
 * whole page. */
enum { VIEW_ROW_TOP = 8, VIEW_ROW_END = 140, PAGE_ROW_END = 200 };
/* The row clip CS:0C28/0C2A that 4AEB8's paths compare every row with
 * (4AF68, 4B0B5, 4B318, 4B4D7, 4B66E, 4BD06, 4C0D7): a single 4AC92 draw
 * sets 8..140 with flags 4000h, else 0..200 (4AD5B..4AD75); a 4ADB1 list
 * sets the window's rows 0BF8/0BFA from 4F1BE (4AE2E..4AE4F), whatever
 * the record's flags (window 3, rows 8..140 in the image's
 * table CS:01EE+02AE / 036E). top < 0 selects the flag rule. */
static xeen_status render(const uint8_t *b, size_t n, uint16_t frame, uint8_t *p, uint8_t *opacity,
                          int16_t x, int16_t y, uint16_t scale, uint16_t flags, int top, int bottom)
{
    uint16_t first, second, count;
    xeen_status s;
    if (top < 0) {
        top = (flags & XEEN_DRAW_CLIP_ROWS) ? VIEW_ROW_TOP : 0;
        bottom = (flags & XEEN_DRAW_CLIP_ROWS) ? VIEW_ROW_END : PAGE_ROW_END;
    }
    if (!b || n < 2) {
        return XEEN_BAD_INPUT;
    }
    count = xeen_get_le16(b);
    if (frame >= count || (size_t)count * 4u + 2u > n) {
        return XEEN_BAD_INPUT;
    }
    first = xeen_get_le16(b + 2u + 4u * frame);
    second = xeen_get_le16(b + 4u + 4u * frame);
    if (scale & XEEN_SCALE_ENLARGE) { /* 4AEB8 JNS */
        s = enlarged_cell(b, n, first, p, opacity, x, y);
        if (s == XEEN_OK && second) {
            s = enlarged_cell(b, n, second, p, opacity, x, y);
        }
        return s;
    }
    if (scale > 15u || (flags & 0x1fffu)) {
        return XEEN_SERVICE_REQUIRED;
    }
    s = draw_scaled_cell(b, n, first, p, opacity, x, y, scale, flags, top, bottom);
    if (s == XEEN_OK && second) {
        s = draw_scaled_cell(b, n, second, p, opacity, x, y, scale, flags, top, bottom);
    }
    return s;
}
xeen_status xeen_raster_frame(const uint8_t *b, size_t n, uint16_t frame, uint8_t *pixels,
                              size_t size, int16_t x, int16_t y, uint16_t scale, uint16_t flags)
{
    uint8_t *trial;
    xeen_status s;
    if (!pixels || size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    trial = malloc(XEEN_PRESENT_PIXELS);
    if (!trial) {
        return XEEN_BAD_INPUT;
    }
    memcpy(trial, pixels, XEEN_PRESENT_PIXELS);
    s = render(b, n, frame, trial, NULL, x, y, scale, flags, -1, -1);
    if (s == XEEN_OK) {
        memcpy(pixels, trial, XEEN_PRESENT_PIXELS);
    }
    free(trial);
    return s;
}
xeen_status xeen_raster_cursor(const uint8_t *b, size_t n, uint16_t frame, uint8_t *pixels,
                               uint8_t *opacity, size_t size)
{
    uint8_t *p, *o;
    xeen_status s;
    if (!pixels || !opacity || pixels == opacity || size < XEEN_PRESENT_PIXELS) {
        return XEEN_BAD_INPUT;
    }
    p = calloc(1, XEEN_PRESENT_PIXELS);
    o = calloc(1, XEEN_PRESENT_PIXELS);
    if (!p || !o) {
        free(p);
        free(o);
        return XEEN_BAD_INPUT;
    }
    s = render(b, n, frame, p, o, 0, 0, 0, 0, -1, -1);
    if (s == XEEN_OK) {
        memcpy(pixels, p, XEEN_PRESENT_PIXELS);
        memcpy(opacity, o, XEEN_PRESENT_PIXELS);
    }
    free(p);
    free(o);
    return s;
}
/**
 * The row clip of a draw in colour modes 1..6: a list's window rows (4ADB1 through 4F1BE: window 3 the view,
 * window 0 the page), a single draw's by its flag 4000h (4AC92). Returns 0 for a list on another window, whose
 * 4F1BE rows the native does not hold (not exercised).
 */
static int effect_row_clip(const xeen_sprite_draw *d, int list, uint16_t rows[2])
{
    int view;
    if (list) {
        if (d->window && d->window != 3) {
            return 0;
        }
        view = d->window == 3;
    } else {
        view = (d->flags & XEEN_DRAW_CLIP_ROWS) != 0;
    }
    rows[0] = view ? VIEW_ROW_TOP : 0;
    rows[1] = view ? VIEW_ROW_END : PAGE_ROW_END;
    return 1;
}
/**
 * Draws request D in colour modes 1..6 (4ADB1/4AC92 -> 4AEB8): each cell of its frame (the second when its table
 * word is non-zero) through the mode driver, which carries the pixel generator CS:18D2/18D4 from cell to cell.
 * The raster's generator is updated only when every cell is drawn.
 */
static xeen_status effect_draw(xeen_raster *r, const uint8_t *b, size_t n, uint8_t *pixels,
                               const xeen_sprite_draw *d, int list, const xeen_effect_rng *e)
{
    uint16_t rows[2], cells[2], gen[2], rng[2] = {0, 0};
    uint8_t carry[2] = {0, 0};
    unsigned cell, cell_count;
    xeen_status s;
    if (!effect_row_clip(d, list, rows)) {
        return XEEN_SERVICE_REQUIRED;
    }
    /* The frame's table words; the driver does not test the frame against the sprite's count. */
    if (n < 6u + 4u * (size_t)d->frame) {
        return XEEN_SERVICE_REQUIRED;
    }
    cells[0] = xeen_get_le16(b + 2u + 4u * d->frame);
    cells[1] = xeen_get_le16(b + 4u + 4u * d->frame);
    cell_count = cells[1] ? 2u : 1u;
    gen[0] = r->generator[0];
    gen[1] = r->generator[1];
    for (cell = 0; cell < cell_count; cell++) {
        /* A cell's two 50525 results (modes 2 and 5 take them). An entry that holds none for this cell passes
         * the previous cell's results on, zero at the first. */
        if (e && e->count >= 2u * (cell + 1u)) {
            rng[0] = e->raw[2u * cell];
            rng[1] = e->raw[2u * cell + 1u];
            carry[0] = e->carry[2u * cell];
            carry[1] = e->carry[2u * cell + 1u];
        }
        s = xeen_raster_effect_cell(b, n, cells[cell], pixels, (int16_t)d->x, (int16_t)d->y, d->scale,
                                    d->flags, rows, rng, carry, gen);
        if (s != XEEN_OK) {
            return s;
        }
    }
    r->generator[0] = gen[0];
    r->generator[1] = gen[1];
    ++r->presentation->effect_draws;
    if (getenv("XEEN_RASTER_TRACE_ALL") || getenv("XEEN_RASTER_TRACE_EFFECTS")) {
        fprintf(stderr, "raster-effect %u %04x %u %u\n", d->frame, d->flags, gen[0], gen[1]);
    }
    return XEEN_OK;
}
/* How a loaded request is drawn: the native adapter of 4AEB8's dispatch on the colour mode (flags & 0F00h). */
typedef enum {
    DRAW_EFFECT,       /* modes 1..6, and a mirrored 7..15 (the driver's mode 6): effect_draw */
    DRAW_LIST_FRAME,   /* a list's record of mode 0 at scale 0..15: the standard frame path */
    DRAW_LIST_SKIPPED, /* a list's record of an unmirrored mode 7..15 at scale 0..15 (4DAE4): skipped, counted */
    DRAW_FRAME         /* the rest through render() with the flags' own row clip: the single draws, the enlarge
                        * path (scale bit 15, any flags) and the refusals (scale 16..7FFFh; a single draw's flags
                        * with bits 0..12 set) */
} draw_path;
static draw_path route_draw(uint16_t scale, uint16_t flags, int list)
{
    unsigned mode = flags & XEEN_DRAW_COLOUR_MODE;
    if (!(scale & XEEN_SCALE_ENLARGE) && mode && (mode < 0x0700u || (flags & XEEN_DRAW_MIRRORED))) {
        return DRAW_EFFECT;
    }
    if (list && scale <= 15u) {
        return mode ? DRAW_LIST_SKIPPED : DRAW_LIST_FRAME;
    }
    return DRAW_FRAME;
}
/**
 * A single 4AC92 draw's flags as the renderer takes them. 4AEB8 sends an unmirrored colour mode 7..15 at scale 0
 * to 4DAE4: a plain draw clipped to the selected window, y from its top, the other low bits unused. Decoded for
 * window 0 (the whole screen) only, where that is the plain draw (flags 0).
 */
static uint16_t single_draw_flags(const xeen_sprite_draw *d)
{
    if (!d->window && !d->scale && !(d->flags & XEEN_DRAW_MIRRORED) &&
        (d->flags & XEEN_DRAW_COLOUR_MODE) >= 0x0700u) {
        return 0;
    }
    return d->flags;
}
/** Draws request D with its sprite's bytes B (N bytes) into PIXELS along its draw_path. */
static xeen_status draw_loaded_sprite(xeen_raster *r, uint8_t *pixels, const uint8_t *b, size_t n,
                                      const xeen_sprite_draw *d, int list, const xeen_effect_rng *e)
{
    uint16_t flags = list ? d->flags : single_draw_flags(d);
    switch (route_draw(d->scale, flags, list)) {
    case DRAW_EFFECT:
        return effect_draw(r, b, n, pixels, d, list, e);
    case DRAW_LIST_FRAME:
        /* 4AEB8 dispatches on AH & 0Fh only (4AEC8, mirrored 4B27C) and its mode-0 paths load AX from the sprite
         * at once (4AF14 lodsw, 4B028 lodsw), so a record's low byte and bit 12 are ignored: the record is drawn.
         * The sky records carry F5E6's bit 0, which a combat turn sets (391C5 XOR [F5E6],1); skipping such a
         * record would leave the view's upper band with the earlier frames' pixels (the owner's Draw Test
         * report). Window 3 clips to the view's rows; another window's rows follow the record's flag 4000h. */
        return render(b, n, d->frame, pixels, NULL, (int16_t)d->x, (int16_t)d->y, d->scale,
                      (uint16_t)(flags & (XEEN_DRAW_MIRRORED | XEEN_DRAW_CLIP_ROWS | XEEN_DRAW_CLIP_COLUMNS)),
                      d->window == 3 ? VIEW_ROW_TOP : -1, d->window == 3 ? VIEW_ROW_END : -1);
    case DRAW_LIST_SKIPPED:
        ++r->presentation->effect_mode_draws;
        return XEEN_OK;
    case DRAW_FRAME:
        break;
    }
    return render(b, n, d->frame, pixels, NULL, (int16_t)d->x, (int16_t)d->y, d->scale, flags,
                  -1, -1);
}
/* The archive holding a cached sprite's bytes: the current state archive (XSP_CUR) or a CC archive; NULL for an
 * archive index past XSP_CUR or an archive the raster was not given. */
static const xeen_archive *sprite_archive(const xeen_sprite_archives *archives, const xeen_sprite *sp)
{
    if (sp->archive > XSP_CUR) {
        return NULL;
    }
    return sp->archive == XSP_CUR ? archives->cur : archives->cc[sp->archive];
}
/* The XEEN_RASTER_TRACE_ALL line (stderr) of a loaded draw: the request as given, with its status. */
static void trace_draw(const xeen_sprite *sp, const xeen_sprite_draw *d, xeen_status s)
{
    if (getenv("XEEN_RASTER_TRACE_ALL")) {
        fprintf(stderr, "raster-draw %u %s %u %d %d %u %04x %d\n", sp->archive, sp->name, d->frame, (int16_t)d->x,
                (int16_t)d->y, d->scale, d->flags, (int)s);
    }
}
/**
 * Draws one sprite draw request D into PIXELS: looks up the sprite in the cache, reads its bytes from its archive
 * into a buffer of this draw (a state-archive sprite as stored, a CC sprite decrypted) and draws them along its
 * draw_path. A request with an FFFFh handle, frame or scale is skipped as the original does. The trace sees
 * every draw whose bytes were read, the failed ones included.
 */
static xeen_status draw_sprite(xeen_raster *r, uint8_t *pixels, const xeen_sprite_draw *d, int list,
                               const xeen_effect_rng *e)
{
    const xeen_sprite *sp;
    const xeen_archive *archive;
    xeen_resource_info info;
    uint8_t *bytes;
    size_t size;
    xeen_status s;
    if (d->handle == 0xffffu || d->frame == 0xffffu || d->scale == 0xffffu) {
        return XEEN_OK;
    }
    sp = xeen_sprite_get(r->cache, d->handle);
    if (!sp) {
        /* 4ADB1 lists index the handle table unchecked (stale memory); 4AC92 tests the handle against the last
         * one and exits fatally. */
        if (!list) {
            return XEEN_BAD_INPUT;
        }
        ++r->presentation->stale_sprite_draws;
        return XEEN_OK;
    }
    /* 4ADB1 does not check the frame either. A loaded game enters its first scene with F47C = 7 (16FC6), so an
     * animated wall asks for a frame past its table (ftown1.fwl frame 8 of 0..7); the driver reads table garbage
     * for that one frame. List draws skip it (counted). */
    if (list && sp->frames && d->frame >= sp->frames) {
        ++r->presentation->invalid_frame_draws;
        return XEEN_OK;
    }
    archive = sprite_archive(r->archives, sp);
    if (!archive || xeen_archive_find_name(archive, sp->name, &info) != XR_OK) {
        return XEEN_BAD_INPUT;
    }
    bytes = malloc(info.size);
    if (!bytes) {
        return XEEN_BAD_INPUT;
    }
    if (xeen_archive_extract(archive, sp->archive == XSP_CUR ? XR_BANK_STATE : XR_BANK_ASSETS_XOR35, &info, bytes,
                             info.size, &size) == XR_OK) {
        s = draw_loaded_sprite(r, pixels, bytes, size, d, list, e);
    } else {
        s = XEEN_BAD_INPUT;
    }
    trace_draw(sp, d, s);
    free(bytes);
    return s;
}
/* Whether R holds everything a draw call needs: a presentation with a whole framebuffer, a cache and archives. */
static int raster_ready(const xeen_raster *r)
{
    return r && r->presentation && r->cache && r->archives && r->presentation->framebuffer &&
           r->presentation->framebuffer_size >= XEEN_PRESENT_PIXELS;
}
/**
 * Sprite-drawing service of the presentation: draws the N requests D (a draw list when LIST is set) in order onto a
 * trial copy of the framebuffer, and commits the copy only when every draw succeeds: a refused call leaves the
 * framebuffer as it was (the presentation's atomic contract). Draw I takes the effect RNG entry I.
 */
static xeen_status draw_sprite_list(void *user, const xeen_sprite_draw *d, size_t n, int list)
{
    xeen_raster *r = user;
    uint8_t *page;
    size_t i;
    xeen_status s = XEEN_OK;
    if (!raster_ready(r) || (!d && n) || n > XEEN_SPRITE_DRAW_MAX) {
        return XEEN_BAD_INPUT;
    }
    page = malloc(XEEN_PRESENT_PIXELS);
    if (!page) {
        return XEEN_BAD_INPUT;
    }
    memcpy(page, r->presentation->framebuffer, XEEN_PRESENT_PIXELS);
    for (i = 0; i < n && s == XEEN_OK; i++) {
        s = draw_sprite(r, page, &d[i], list, &r->presentation->effects[i]);
    }
    if (s == XEEN_OK) {
        memcpy(r->presentation->framebuffer, page, XEEN_PRESENT_PIXELS);
    }
    free(page);
    return s;
}
xeen_status xeen_raster_sprites(void *user, const xeen_sprite_draw *d, size_t n)
{
    return draw_sprite_list(user, d, n, 0);
}
/* A scene list's records as draw requests, every one on the list's window. */
xeen_status xeen_raster_scene(void *user, const xeen_draw_list *list)
{
    xeen_sprite_draw draws[XEEN_DRAW_LIST_MAX];
    size_t i;
    if (!list || list->count > XEEN_DRAW_LIST_MAX) {
        return XEEN_BAD_INPUT;
    }
    for (i = 0; i < list->count; i++) {
        const xeen_draw_command *c = &list->commands[i];
        draws[i] = (xeen_sprite_draw){c->handle, c->frame, c->x, c->y, c->scale, c->flags, list->window};
    }
    return draw_sprite_list(user, draws, list->count, 1);
}
xeen_resource_status xeen_raster_palette(xeen_presentation *p, const xeen_archive *a,
                                         const char *name)
{
    xeen_resource_info info;
    uint8_t bytes[768];
    size_t got, i;
    xeen_resource_status s;
    if (!p || !a || !name) {
        return XR_BAD_INPUT;
    }
    s = xeen_archive_find_name(a, name, &info);
    if (s != XR_OK) {
        return s;
    }
    if (info.size != 768u) {
        return XR_BAD_INPUT;
    }
    s = xeen_archive_extract(a, XR_BANK_ASSETS_XOR35, &info, bytes, sizeof bytes, &got);
    if (s != XR_OK) {
        return s;
    }
    for (i = 0; i < sizeof bytes; i++) {
        if (bytes[i] > 63u) {
            return XR_BAD_INPUT;
        }
    }
    memcpy(p->palette, bytes, sizeof bytes);
    return XR_OK;
}
