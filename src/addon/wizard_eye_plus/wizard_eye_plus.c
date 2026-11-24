/* wizard_eye_plus (Phase 318 add-on, NOT original behavior): the minimap's unseen cells through a checkerboard while
 * Wizard Eye and the = automap are both on (include/xeen/addon/wizard_eye_plus.h). The game's DS, framebuffer and
 * traces are read, never written: Wizard Eye's whole minimap is built on a private copy of the game state and drawn
 * into a private page; the effect is shown on a copy of the displayed page. Self-contained: no other add-on. */
#include <stdlib.h>
#include <string.h>
#include "xeen/addon/wizard_eye_plus.h"
#include "xeen/ds_globals.h"
#include "xeen/minimap.h"
#include "xeen/movement.h"
int xeen_wizard_eye_plus_on;
/* {world, map} of the sky maps (make_table.py). */
static const struct {
    uint8_t world, map;
} sky_maps[] = {
#include "sky_maps.inc"
};
#define PAGE_W 320u
#define BOX_PIXELS (XEEN_WEP_BOX_W * XEEN_WEP_BOX_H)
#define RECORDS 4        /* the latest passes kept: a page shown around a pass can still be an earlier pass's */
#define CELLS_MATCHED 4  /* a record serves a page when at least this many of its cells are on it unchanged */
/* One pass's box: the original's pixels and the effect's. */
typedef struct {
    uint8_t masked[BOX_PIXELS], effect[BOX_PIXELS];
} wep_record;
static struct {
    int started, begun, armed;
    uint8_t styles[XEEN_WEP_KINDS];
    xeen_movement *scratch;    /* the whole map's pass runs on this copy */
    xeen_minimap_draws *draws; /* its draws */
    uint8_t *before;           /* the page under the minimap (before the pass's draws) */
    uint8_t *full;             /* the whole map drawn over it */
    uint8_t *page;             /* the page shown with the effect */
    unsigned count;            /* records[0] the latest pass's, then the earlier ones */
    wep_record records[RECORDS];
} wep;

int xeen_wizard_eye_plus_sky_map(unsigned world, unsigned map)
{
    for (size_t i = 0; i < sizeof sky_maps / sizeof *sky_maps; ++i) {
        if (sky_maps[i].world == world && sky_maps[i].map == map) {
            return 1;
        }
    }
    return 0;
}
unsigned xeen_wizard_eye_plus_kind(const uint8_t *ds)
{
    if (xeen_wizard_eye_plus_sky_map(ds[XEEN_DS_WORLD], ds[XEEN_DS_PARTY_MAP])) {
        return XEEN_WEP_SKY;
    }
    return ds[XEEN_DS_OUTDOORS] || ds[XEEN_DS_OUTDOORS + 1u] ? XEEN_WEP_OUTDOOR : XEEN_WEP_INDOOR;
}
void xeen_wizard_eye_plus_compose(const uint8_t *full, const uint8_t *masked, uint8_t *out, unsigned style)
{
    for (unsigned y = XEEN_WEP_BOX_Y; y < XEEN_WEP_BOX_Y + XEEN_WEP_BOX_H; ++y) {
        for (unsigned x = XEEN_WEP_BOX_X; x < XEEN_WEP_BOX_X + XEEN_WEP_BOX_W; ++x) {
            size_t i = y * PAGE_W + x;
            int content = style == XEEN_WEP_2PX ? !(((x >> 1) + (y >> 1)) & 1u) : !((x + y) & 1u);
            if (full[i] == masked[i]) {
                out[i] = masked[i]; /* the original's: a seen cell, the frame, the arrow */
            } else if (content) {
                out[i] = full[i]; /* what Wizard Eye shows there */
            } else {
                out[i] = style == XEEN_WEP_BLACK ? 0 : masked[i]; /* black, or the original's fog */
            }
        }
    }
}
/* Copies the box of PAGE into BOX. */
static void box_copy(const uint8_t *page, uint8_t *box)
{
    for (unsigned y = 0; y < XEEN_WEP_BOX_H; ++y) {
        memcpy(box + y * XEEN_WEP_BOX_W, page + (XEEN_WEP_BOX_Y + y) * PAGE_W + XEEN_WEP_BOX_X, XEEN_WEP_BOX_W);
    }
}
int xeen_wizard_eye_plus_start(const uint8_t styles[XEEN_WEP_KINDS])
{
    xeen_wizard_eye_plus_stop();
    wep.scratch = malloc(sizeof *wep.scratch);
    wep.draws = malloc(sizeof *wep.draws);
    wep.before = malloc(XEEN_PRESENT_PIXELS);
    wep.full = malloc(XEEN_PRESENT_PIXELS);
    wep.page = malloc(XEEN_PRESENT_PIXELS);
    if (!wep.scratch || !wep.draws || !wep.before || !wep.full || !wep.page) {
        xeen_wizard_eye_plus_stop();
        return 0;
    }
    for (unsigned k = 0; k < XEEN_WEP_KINDS; ++k) {
        wep.styles[k] = styles && styles[k] < XEEN_WEP_STYLES ? styles[k] : XEEN_WEP_BLACK;
    }
    wep.started = 1;
    xeen_wizard_eye_plus_on = 1;
    return 1;
}
void xeen_wizard_eye_plus_stop(void)
{
    xeen_wizard_eye_plus_on = 0;
    free(wep.scratch);
    free(wep.draws);
    free(wep.before);
    free(wep.full);
    free(wep.page);
    memset(&wep, 0, sizeof wep);
}
void xeen_wizard_eye_plus_minimap_begin(const xeen_movement *m, const xeen_presentation *p)
{
    wep.begun = 1;
    wep.armed = wep.started && m && p && p->framebuffer && p->framebuffer_size >= XEEN_PRESENT_PIXELS &&
                m->ds[XEEN_DS_EFFECT_WIZARD_EYE] && m->ds[XEEN_DS_MINIMAP_SHOWN];
    if (wep.armed) {
        memcpy(wep.before, p->framebuffer, XEEN_PRESENT_PIXELS);
    }
}
/* The pass's record, newest first (the oldest dropped). */
static wep_record *push_record(void)
{
    wep_record last = wep.records[RECORDS - 1u];
    memmove(wep.records + 1, wep.records, (RECORDS - 1u) * sizeof *wep.records);
    wep.records[0] = last; /* overwritten by the caller */
    if (wep.count < RECORDS) {
        ++wep.count;
    }
    return &wep.records[0];
}
void xeen_wizard_eye_plus_minimap_end(const xeen_movement *m, xeen_presentation *p, int drawn)
{
    uint8_t *framebuffer;
    size_t size;
    xeen_status s;
    wep_record *r;
    int differs = 0;
    if (!wep.begun) {
        return;
    }
    wep.begun = 0;
    if (!drawn) {
        return; /* no minimap drawn (a window open, a combat window): the one on screen keeps its records */
    }
    if (!wep.armed || !m || !p || !p->raster_sprites || !p->framebuffer || p->framebuffer_size < XEEN_PRESENT_PIXELS) {
        wep.count = 0; /* a minimap without the effect (Wizard Eye or the automap off): none of the old records */
        return;
    }
    /* Wizard Eye's whole map: the same pass with the = automap off (1E467 then ignores nothing). */
    *wep.scratch = *m;
    wep.scratch->ds[XEEN_DS_MINIMAP_SHOWN] = 0;
    if (xeen_scene_minimap(wep.scratch, wep.draws) != XEEN_OK || !wep.draws->count ||
        wep.draws->count > XEEN_SPRITE_DRAW_MAX) {
        wep.count = 0;
        return;
    }
    /* Drawn by the presentation's own renderer into the private page: its framebuffer pointer is lent for the
     * call (minimap draws carry no colour mode: no effect entry, no generator, no trace is used). */
    memcpy(wep.full, wep.before, XEEN_PRESENT_PIXELS);
    framebuffer = p->framebuffer;
    size = p->framebuffer_size;
    p->framebuffer = wep.full;
    p->framebuffer_size = XEEN_PRESENT_PIXELS;
    s = p->raster_sprites(p->raster_user, wep.draws->draws, wep.draws->count);
    p->framebuffer = framebuffer;
    p->framebuffer_size = size;
    if (s != XEEN_OK) {
        wep.count = 0;
        return;
    }
    memcpy(wep.page, framebuffer, XEEN_PRESENT_PIXELS);
    xeen_wizard_eye_plus_compose(wep.full, framebuffer, wep.page, wep.styles[xeen_wizard_eye_plus_kind(m->ds)]);
    for (unsigned y = 0; y < XEEN_WEP_BOX_H && !differs; ++y) {
        size_t at = (XEEN_WEP_BOX_Y + y) * PAGE_W + XEEN_WEP_BOX_X;
        differs = memcmp(framebuffer + at, wep.page + at, XEEN_WEP_BOX_W) != 0;
    }
    if (!differs) {
        wep.count = 0; /* every cell seen: nothing to show */
        return;
    }
    r = push_record(); /* kept with the earlier passes of the effect: a page shown now can still be theirs */
    box_copy(framebuffer, r->masked);
    box_copy(wep.page, r->effect);
}
/* The box coordinates of the minimap's cell COL, ROW: 10 x 8 pixels at EDh + 10 col, 0Ch + 8 row on the page. */
static void cell_origin(unsigned col, unsigned row, unsigned *x0, unsigned *y0)
{
    *x0 = 0xedu + 10u * col - XEEN_WEP_BOX_X;
    *y0 = 0x0cu + 8u * row - XEEN_WEP_BOX_Y;
}
/* Whether the page's cell COL, ROW is record R's original pixels. */
static int cell_matches(const uint8_t *page, const wep_record *r, unsigned col, unsigned row)
{
    unsigned x0, y0;
    cell_origin(col, row, &x0, &y0);
    for (unsigned y = y0; y < y0 + 8u; ++y) {
        if (memcmp(page + (XEEN_WEP_BOX_Y + y) * PAGE_W + XEEN_WEP_BOX_X + x0, r->masked + y * XEEN_WEP_BOX_W + x0,
                   10u)) {
            return 0;
        }
    }
    return 1;
}
/* Copies the effect of record R in the box rectangle X0..X1, Y0..Y1 (box coordinates) into OUT where the page PAGE
 * still has R's original pixel. */
static void apply_effect(uint8_t *out, const uint8_t *page, const wep_record *r, unsigned x0, unsigned y0,
                         unsigned x1, unsigned y1)
{
    for (unsigned y = y0; y < y1; ++y) {
        size_t at = (XEEN_WEP_BOX_Y + y) * PAGE_W + XEEN_WEP_BOX_X;
        for (unsigned x = x0; x < x1; ++x) {
            size_t i = y * XEEN_WEP_BOX_W + x;
            if (r->masked[i] != r->effect[i] && page[at + x] == r->masked[i]) {
                out[at + x] = r->effect[i];
            }
        }
    }
}
/*
 * The page shown: each of the minimap's 49 cells takes the effect of the newest record whose original pixels it still
 * shows (the page can be an earlier pass's than the latest record: a turn, an animated wall), a record being used
 * only when at least CELLS_MATCHED of its cells are on the page (another screen: no stray pixels); a cell covered
 * (a window over part of the minimap) keeps the page's pixels. Around the cells (the half-cell ring, the edge
 * walls, partly covered by the border drawn after the minimap) the newest such record's effect where the page still
 * has its original pixel.
 */
/* How many of the 49 cells of PAGE show each record's original pixels: USED[k] for record k. */
static void count_matched_cells(const uint8_t *page, int used[RECORDS])
{
    for (unsigned k = 0; k < wep.count; ++k) {
        used[k] = 0;
        for (unsigned row = 0; row < 7u; ++row) {
            for (unsigned col = 0; col < 7u; ++col) {
                used[k] += cell_matches(page, &wep.records[k], col, row);
            }
        }
    }
}
/* The record each cell of PAGE takes, -1 for none: the newest record in use (at least CELLS_MATCHED cells on the
 * page, USED) whose original pixels the cell shows. Returns 1 when any cell has one. */
static int choose_cells(const uint8_t *page, const int used[RECORDS], int chosen[7][7])
{
    int any = 0;
    for (unsigned row = 0; row < 7u; ++row) {
        for (unsigned col = 0; col < 7u; ++col) {
            chosen[row][col] = -1;
            for (unsigned k = 0; k < wep.count && chosen[row][col] < 0; ++k) {
                if (used[k] >= CELLS_MATCHED && cell_matches(page, &wep.records[k], col, row)) {
                    chosen[row][col] = (int)k;
                    any = 1;
                }
            }
        }
    }
    return any;
}
const uint8_t *xeen_wizard_eye_plus_page(const uint8_t *page)
{
    int used[RECORDS] = {0}, chosen[7][7];
    if (!wep.started || !wep.count || !page) {
        return page;
    }
    count_matched_cells(page, used);
    if (!choose_cells(page, used, chosen)) {
        return page;
    }
    memcpy(wep.page, page, XEEN_PRESENT_PIXELS);
    for (unsigned k = 0; k < wep.count; ++k) { /* the ring: the newest record in use */
        if (used[k] >= CELLS_MATCHED) {
            apply_effect(wep.page, page, &wep.records[k], 0, 0, XEEN_WEP_BOX_W, XEEN_WEP_BOX_H);
            break;
        }
    }
    for (unsigned row = 0; row < 7u; ++row) { /* the cells: each its own record's, or none */
        for (unsigned col = 0; col < 7u; ++col) {
            unsigned x0, y0;
            cell_origin(col, row, &x0, &y0);
            for (unsigned y = y0; y < y0 + 8u; ++y) { /* the ring's pass over this cell undone first */
                size_t at = (XEEN_WEP_BOX_Y + y) * PAGE_W + XEEN_WEP_BOX_X + x0;
                memcpy(wep.page + at, page + at, 10u);
            }
            if (chosen[row][col] >= 0) {
                apply_effect(wep.page, page, &wep.records[chosen[row][col]], x0, y0, x0 + 10u, y0 + 8u);
            }
        }
    }
    return wep.page;
}
