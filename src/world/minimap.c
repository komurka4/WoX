#include "xeen/minimap.h"
#include "xeen/ds_globals.h"
#include <stdlib.h>
#include <string.h>
#define B(a)    (m->ds[(uint16_t)(a)])
#define W(a)    xeen_read16(m, (uint16_t)(a))
#define S(a, v) xeen_write16(m, (uint16_t)(a), (uint16_t)(v))
/* The class of the queried cell's surface kind (the party map's classification at the kind F490; reads both). */
#define CELL_CLASS() B(XEEN_DS_MAP_FIELD(CLASSIFICATION) + W(XEEN_DS_CELL_SURFACE))
/* The minimap's 7 x 7 cells around the party, 10 x 8 pixels each from (EDh, 0Ch); an indoor surface's tile sits 5
 * right and 5 down of its cell, and column or row -1 is the ring of cells drawn left of and above the grid. */
#define CELL_X(col)    ((uint16_t)(0xed + 10 * (col)))
#define CELL_Y(row)    ((uint16_t)(0x0c + 8 * (row)))
#define SURFACE_X(col) ((uint16_t)(0xf2 + 10 * (col)))
#define SURFACE_Y(row) ((uint16_t)(0x11 + 8 * (row)))
/* The indoor minimap's left edge walls (x DEh, beside column -1; one per row) and the party's arrow (x SURFACE_X(3),
 * the party's column; y 28h). */
#define LEFT_EDGE_X 0xdeu
#define ARROW_X     SURFACE_X(3)
#define ARROW_Y     0x28u

/* Cached map slot search over F46A (native bound: the 9 cache slots). */
static int find_slot(xeen_movement *m, uint16_t id)
{
    unsigned i;
    for (i = 0; i < 9; ++i) {
        if (W(XEEN_DS_MAP_SLOT_IDS + 2u * i) == id) {
            S(XEEN_DS_MAP_SLOT, i);
            return 1;
        }
    }
    return 0;
}
/**
 * The minimap's value for a cell of a missing neighbouring map: records SEEN in F492 and returns
 * 0Fh on a map with F608 set, else 8888h.
 */
static uint16_t missing_link(xeen_movement *m, uint16_t cell_seen)
{
    S(XEEN_DS_CELL_SEEN, cell_seen);
    return W(XEEN_DS_OUTDOORS) ? 0x000fu : XEEN_CELL_OUTSIDE_MAPS;
}
/* What one axis of a cell lookup found (neighbour_slot). */
enum neighbour {
    NEIGHBOUR_SAME,     /* the coordinate lies in the current map */
    NEIGHBOUR_CACHED,   /* in a neighbour, now the current slot */
    NEIGHBOUR_NONE,     /* in a neighbour the map does not have (its link 0) */
    NEIGHBOUR_UNCACHED  /* in a neighbour that is not in the cache */
};
/* One axis of a lookup: a coordinate with bit 4 set lies in the neighbouring map before (LOW: south or west, a
 * negative coordinate) or after (HIGH: north or east) the current slot's, and is brought back by 16; *MAP becomes the
 * neighbour's id (0: none) and, when it is cached, its slot the current one. */
static enum neighbour neighbour_slot(xeen_movement *m, uint16_t *coordinate, unsigned low, unsigned high,
                                     uint16_t *map)
{
    uint16_t base;
    if (!(*coordinate & 0x10u)) {
        return NEIGHBOUR_SAME;
    }
    base = (uint16_t)(W(XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES);
    if ((int16_t)*coordinate < 0) {
        *coordinate = (uint16_t)(*coordinate + 16u);
        *map = W(base + low);
    } else {
        *coordinate = (uint16_t)(*coordinate - 16u);
        *map = W(base + high);
    }
    if (!*map) {
        return NEIGHBOUR_NONE;
    }
    return find_slot(m, *map) ? NEIGHBOUR_CACHED : NEIGHBOUR_UNCACHED;
}
xeen_status xeen_maze_lookup(xeen_movement *m, int16_t x0, int16_t y0, uint16_t mask,
                             uint16_t shift, uint16_t *result)
{
    uint16_t x = (uint16_t)x0, y = (uint16_t)y0, dx, base, cell;
    if (!m || !result) {
        return XEEN_BAD_INPUT;
    }
    if (x0 > 31 || y0 > 31) {
        S(XEEN_DS_CELL_WALLS, XEEN_CELL_OUTSIDE_MAPS);
        *result = XEEN_CELL_OUTSIDE_MAPS;
        return XEEN_OK;
    }
    dx = B(XEEN_DS_PARTY_MAP);
    if (!find_slot(m, dx)) {
        return XEEN_CACHE_MISS;
    }
    /* y first, then x from the map y led to (a diagonal neighbour). A missing neighbour differs by axis, as in the
     * original: past y it answers at once (seen 1); past x the map id 0 goes on to the test below (seen = the
     * outdoor flag). */
    switch (neighbour_slot(m, &y, XEEN_DS_MAP_FIELD(SOUTH), XEEN_DS_MAP_FIELD(NORTH), &dx)) {
        case NEIGHBOUR_UNCACHED:
            return XEEN_CACHE_MISS;
        case NEIGHBOUR_NONE:
            *result = missing_link(m, 1);
            return XEEN_OK;
        default:
            break;
    }
    if (neighbour_slot(m, &x, XEEN_DS_MAP_FIELD(WEST), XEEN_DS_MAP_FIELD(EAST), &dx) == NEIGHBOUR_UNCACHED) {
        return XEEN_CACHE_MISS;
    }
    /* 1C77C: DX still holds the map id when neither axis wrapped. */
    if (!dx) {
        *result = missing_link(m, W(XEEN_DS_OUTDOORS));
        return XEEN_OK;
    }
    base = (uint16_t)(W(XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES);
    cell = (uint16_t)(base + (uint16_t)(y * 32u) + (uint16_t)(x * 2u));
    if (W(XEEN_DS_OUTDOORS)) {
        S(XEEN_DS_CELL_SURFACE, W(cell + XEEN_DS_MAP_DATA) & 0x0fu);
    } else {
        uint16_t meta = (uint16_t)((unsigned)base + y * 16u + x + XEEN_DS_MAP_FIELD(METADATA));
        S(XEEN_DS_CELL_SURFACE, m->ds[meta] & 7u);
    }
    if (CELL_CLASS() == XEEN_SURFACE_SKY || CELL_CLASS() == XEEN_SURFACE_SPACE) {
        S(XEEN_DS_CELL_SEEN, 1);
    } else {
        /* CBW then AND with a single bit <= 80h: sign extension cannot reach
         * the result, so the byte AND is exact. */
        uint16_t index = (uint16_t)(y * 16u + x);
        S(XEEN_DS_CELL_SEEN, B(XEEN_DS_MAP_FIELD(SEEN) + base + (index >> 3)) & (0x80u >> (index & 7u)));
    }
    *result = (uint16_t)((uint16_t)(W(cell + XEEN_DS_MAP_DATA) >> (shift & 31u)) & mask);
    return XEEN_OK;
}
/**
 * Returns the offset of cell X, Y in the current map slot's data (row and column strides given).
 */
static uint16_t slot_cell(xeen_movement *m, int16_t x, int16_t y, unsigned row, unsigned column)
{
    return (uint16_t)(W(XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES + (uint16_t)((y & 15) * row) +
                      (uint16_t)((x & 15) * column));
}
xeen_status xeen_maze_set_wall(xeen_movement *m, int16_t x, int16_t y, unsigned side, uint16_t wall)
{
    uint16_t r, a;
    uint8_t count;
    xeen_status s;
    if (!m || side > 3u) {
        return XEEN_BAD_INPUT;
    }
    if ((s = xeen_maze_lookup(m, x, y, 0, 0, &r)) != XEEN_OK) {
        return s;
    }
    a = (uint16_t)(XEEN_DS_MAP_DATA + slot_cell(m, x, y, 32u, 2u));
    count = (uint8_t)(B(XEEN_DS_WALL_SHIFT(side, 2u)) & 31u); /* 2DE64: CL */
    S(a,
      (uint16_t)((W(a) & W(XEEN_DS_WALL_SIDE_MASKS + 2u * side)) | (count < 16u ? (uint16_t)(wall << count) : 0u)));
    return XEEN_OK;
}
xeen_status xeen_maze_set_cell_flag(xeen_movement *m, int16_t x, int16_t y, uint8_t flag)
{
    uint16_t r;
    xeen_status s;
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    if ((s = xeen_maze_lookup(m, x, y, 0, 0, &r)) != XEEN_OK) {
        return s;
    }
    B(XEEN_DS_MAP_FIELD(METADATA) + slot_cell(m, x, y, 16u, 1u)) |= flag;
    return XEEN_OK;
}

typedef struct {
    xeen_movement *m;
    xeen_minimap_draws *out;
    xeen_status status;
    xeen_minimap_driver_fn fatal;
    void *user;
    int stopped;
} ctx;
/** Appends one sprite draw to the minimap's draw list; a full list sets the context's error. */
static void push_draw(ctx *c, uint16_t handle, uint16_t frame, uint16_t x, uint16_t y)
{
    xeen_sprite_draw *d;
    if (c->status != XEEN_OK) {
        return;
    }
    if (c->out->count == XEEN_MINIMAP_MAX_DRAWS) {
        c->status = XEEN_BAD_INPUT;
        return;
    }
    d = &c->out->draws[c->out->count++];
    d->handle = handle;
    d->frame = frame;
    d->x = x;
    d->y = y;
    d->scale = 0;
    d->flags = 0;
    d->window = 1;
    /* 4AC92's own tests; a failed one does not return (4AD19). */
    if (c->fatal && c->fatal(c->user, d)) {
        c->stopped = 1;
        c->status = XEEN_SERVICE_REQUIRED;
    }
}
/**
 * Looks up the cell at offset DX, DY from the party through the maze lookup (MASK and SHIFT select
 * the field); errors are kept in the context.
 */
static uint16_t look_cell(ctx *c, int dx, int dy, uint16_t mask, uint16_t shift)
{
    xeen_movement *m = c->m;
    uint16_t r = 0;
    if (c->status != XEEN_OK) {
        return 0;
    }
    c->status =
            xeen_maze_lookup(m, (int16_t)(xeen_signed_byte(B(XEEN_DS_PARTY_X)) + dx),
                             (int16_t)(xeen_signed_byte(B(XEEN_DS_PARTY_Y)) + dy), mask, shift, &r);
    return r;
}
/**
 * Returns nonzero when the looked-up cell is drawn: it was seen (F492) or the whole map is shown
 * (EDA0, the Wizard Eye state).
 */
static int cell_seen(const xeen_movement *m)
{
    return W(XEEN_DS_CELL_SEEN) || B(XEEN_DS_EFFECT_WIZARD_EYE);
}

#define NO_WALL_FRAME 0xffffu /* wall_frame() and the edge table: no wall to draw */
/* Wall frame tables CS:1308, 12EA, 12CC, 12AE (value-1 index; FFFF none,
 * ANIM4/ANIM5 = animated frame [bp-6] + 4/+5). As in the view, kinds 5 and 8 share a frame (2/3) and so do 4 and 13
 * (16/17); kind 12 is the animated one, and the left edge draws no kind 2 (xeen/map_record.h's indoor wall kinds). */
#define ANIM4 0xfff4u
#define ANIM5 0xfff5u
static const uint16_t edge_left[15] = {18, NO_WALL_FRAME, 22, 16,    2,  30, 32, 2,
                                       20, 28,     14, ANIM4, 16, 24, 26};
static const uint16_t edge_odd[15] = {19, 35, 23, 17, 3, 31, 33, 3, 21, 29, 15, ANIM5, 17, 25, 27};
static const uint16_t cell_even[15] = {18, 34, 22, 16, 2, 30, 32, 2, 20, 28, 14, ANIM4, 16, 24, 26};
/**
 * Returns the wall sprite frame of wall VALUE (1..15) from TABLE, with the animated entries offset
 * by ANIM; NO_WALL_FRAME for no wall.
 */
/* The walls' animation phase base: F47C x 2 (F47C, deliberately numeric: written 0 at a location's entry 27855 and 5
 * by the guild's heads 3F8F8; read x 2 by the minimap 1E4D1 and the map 1DA43). */
static uint16_t anim_base(const xeen_movement *m)
{
    return (uint16_t)(W(0xf47c) * 2u);
}
/* An indoor pass's phase: (ANIM + 2) signed mod 8 (CWD/IDIV by 8: the minimap's and the map's 1DD27). */
static uint16_t anim_step(uint16_t anim)
{
    uint16_t a = (uint16_t)(anim + 2u);
    int32_t v = a < 0x8000u ? (int32_t)a : (int32_t)a - INT32_C(65536);
    return (uint16_t)(v % 8);
}
static uint16_t wall_frame(const uint16_t table[15], uint16_t value, uint16_t anim)
{
    uint16_t index = (uint16_t)(value - 1u), f;
    if (index > 14u) {
        return NO_WALL_FRAME;
    }
    f = table[index];
    return f == ANIM4 ? (uint16_t)(anim + 4u) : f == ANIM5 ? (uint16_t)(anim + 5u) : f;
}

/*
 * The outdoor maps (the minimap and the full map) draw a cell word in three layers, each a pass over every cell,
 * from the minimap tiles: the surface (bits 0..3, a kind whose class is the tile, drawn for every class, 0
 * included), the wall (bits 4..7, a kind whose wall class + 10h is the tile; class 0 none) and the high byte (bits
 * 8..15, the tile + 20h itself; 0 none). Each layer's lookup selects its bits (FIELD_MASK after FIELD_SHIFT), and
 * runs whether or not the cell is drawn. Original: the minimap 1E4E3..1E6A8 (the class reads 1E542, 1E5D0), the
 * full map from 1DB8E (1DBE4, 1DC5D).
 */
typedef struct {
    uint16_t field_mask, field_shift; /* the lookup's bits of the cell word */
    uint16_t class_table;             /* the map record's class bytes by kind, or 0: the value itself is the class */
    uint16_t first_tile;              /* the tile of class 0 */
    int class_0_drawn;                /* class 0 is a tile (the surface) rather than nothing */
} outdoor_layer;
static const outdoor_layer outdoor_layers[3] = {
        {0x0f, 0, XEEN_MAP_RECORD_CLASSIFICATION, 0x00, 1},
        {0x0f, 4, XEEN_MAP_RECORD_WALL_CLASSIFICATION, 0x10, 0},
        {0xff, 8, 0, 0x20, 0},
};
/*
 * The tile of VALUE (the lookup of LAYER's bits; a kind's class is read from the party map's slot) into *TILE;
 * returns 0 when the layer draws nothing for the cell.
 */
static int outdoor_tile(const xeen_movement *m, const outdoor_layer *layer, uint16_t value, uint16_t *tile)
{
    uint16_t kind_class =
            layer->class_table
                    ? B(W(XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES + value + XEEN_DS_MAP_DATA + layer->class_table)
                    : value;
    if (!kind_class && !layer->class_0_drawn) {
        return 0;
    }
    *tile = (uint16_t)(kind_class + layer->first_tile);
    return 1;
}
/*
 * Builds the outdoor minimap: the frame, then the three layers (outdoor_layers) over the 7 x 7 cells around the
 * party (row 0 the party's y + 3, column 0 its x - 3), a cell drawn when it is seen or the Wizard Eye shows the map.
 * The party's arrow follows, drawn by the minimap's caller.
 */
static void draw_outdoor_minimap(ctx *c)
{
    xeen_movement *m = c->m;
    const outdoor_layer *layer;
    int row, col;
    push_draw(c, W(XEEN_DS_GLOBAL_SPRITES), 0x0f, CELL_X(0), CELL_Y(0));
    for (layer = outdoor_layers; layer < outdoor_layers + 3; ++layer) {
        for (row = 0; row < 7; ++row) {
            for (col = 0; col < 7; ++col) {
                uint16_t v = look_cell(c, col - 3, 3 - row, layer->field_mask, layer->field_shift), tile;
                if (c->status == XEEN_OK && outdoor_tile(m, layer, v, &tile) && cell_seen(m)) {
                    push_draw(c, W(XEEN_DS_MINIMAP_TILES), tile, CELL_X(col), CELL_Y(row));
                }
            }
        }
    }
}
/*
 * The surface tile of the cell at DX, DY from the party (its classification's tile; the indoor surface tiles start
 * at frame 24h) at X, Y: drawn when the cell is on a map, has a surface (the lookup's CELL_SURFACE) and is seen.
 */
static void draw_surface(ctx *c, int dx, int dy, uint16_t x, uint16_t y)
{
    xeen_movement *m = c->m;
    uint16_t v = look_cell(c, dx, dy, 0xffff, 0);
    if (c->status == XEEN_OK && v != XEEN_CELL_OUTSIDE_MAPS && W(XEEN_DS_CELL_SURFACE) && cell_seen(m)) {
        push_draw(c, W(XEEN_DS_MINIMAP_TILES), (uint16_t)(CELL_CLASS() + 0x24u), x, y);
    }
}
/* Wall FRAME (wall_frame() of the cell just looked up) at X, Y, unless there is no wall or the cell is unseen. */
static void draw_wall(ctx *c, uint16_t frame, uint16_t x, uint16_t y)
{
    xeen_movement *m = c->m;
    if (c->status == XEEN_OK && frame != NO_WALL_FRAME && cell_seen(m)) {
        push_draw(c, W(XEEN_DS_MINIMAP_TILES), frame, x, y);
    }
}
/*
 * Builds the indoor minimap over the 7 x 7 cells around the party (row 0 the party's y + 3, column 0 its x - 3),
 * in layers, each in row order: the floor of every cell on a map and seen; the surfaces (water, lava and the like)
 * of the 7 x 7 cells, then of the cells left of and above them (the corner, the left column, the top row), offset
 * half a cell; the edges' walls (the left column's walls with field shift 12, the top row's with shift 0); each
 * cell's two walls (shift 12, then shift 0), the party's arrow (its facing + 1) drawn before cell 4, 4; the fog:
 * a cell off the maps, or unseen without the Wizard Eye. ANIM is the animated wall's phase (0..7).
 * Every lookup runs even when its draw is skipped, in this order: a lookup's side effects (CELL_SURFACE, CELL_SEEN)
 * are read by its own draw only.
 * Original: 1E467 (the edges 1E90C, the walls 1EA5F, the fog 1EC05).
 */
static void draw_indoor_minimap(ctx *c, uint16_t anim)
{
    xeen_movement *m = c->m;
    int row, col, i;
    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 7; ++col) { /* floor */
            uint16_t v = look_cell(c, col - 3, 3 - row, 0xffff, 0);
            if (c->status == XEEN_OK && v != XEEN_CELL_OUTSIDE_MAPS && cell_seen(m)) {
                push_draw(c, W(XEEN_DS_MINIMAP_TILES), 0, CELL_X(col), CELL_Y(row));
            }
        }
    }
    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 7; ++col) {
            draw_surface(c, col - 3, 3 - row, SURFACE_X(col), SURFACE_Y(row));
        }
    }
    draw_surface(c, -4, 4, SURFACE_X(-1), SURFACE_Y(-1)); /* the corner */
    for (row = 0; row < 7; ++row) {
        draw_surface(c, -4, 3 - row, SURFACE_X(-1), SURFACE_Y(row)); /* the left column */
    }
    for (col = 0; col < 7; ++col) {
        draw_surface(c, col - 3, 4, SURFACE_X(col), SURFACE_Y(-1)); /* the top row */
    }
    for (i = 0; i < 7; ++i) { /* the edges, bottom to top on the left */
        draw_wall(c, wall_frame(edge_left, look_cell(c, -4, i - 3, 0xffff, 12), anim), LEFT_EDGE_X,
                  (uint16_t)(CELL_Y(6) - 8 * i)); /* CELL_Y(6 - i): bottom to top */
        draw_wall(c, wall_frame(edge_odd, look_cell(c, i - 3, 4, 0x0f, 0), anim), CELL_X(i), CELL_Y(-1));
    }
    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 7; ++col) { /* the walls */
            /* The arrow goes in at cell (4, 4), not the party's (3, 3), so the walls of the cells before it overdraw
             * nothing of it and the later ones do; the outdoors test is the original's own (1EA7C..1EA8C: column 4,
             * row 4, F608 = 0, then 4AC92(F458, ED96 + 1, 110h, 28h)). */
            if (col == 4 && row == 4 && !W(XEEN_DS_OUTDOORS)) {
                push_draw(c, W(XEEN_DS_GLOBAL_SPRITES), (uint16_t)(B(XEEN_DS_PARTY_DIRECTION) + 1u), ARROW_X, ARROW_Y);
            }
            draw_wall(c, wall_frame(cell_even, look_cell(c, col - 3, 3 - row, 0xffff, 12), anim),
                      CELL_X(col), CELL_Y(row));
            draw_wall(c, wall_frame(edge_odd, look_cell(c, col - 3, 3 - row, 0x0f, 0), anim), CELL_X(col),
                      CELL_Y(row));
        }
    }
    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 7; ++col) { /* the fog */
            /* 1EC40: an off-map result (8888) jumps straight to the draw. */
            uint16_t v = look_cell(c, col - 3, 3 - row, 0xffff, 0);
            if (c->status == XEEN_OK &&
                (v == XEEN_CELL_OUTSIDE_MAPS || (!W(XEEN_DS_CELL_SEEN) && !B(XEEN_DS_EFFECT_WIZARD_EYE)))) {
                push_draw(c, W(XEEN_DS_MINIMAP_TILES), 1, CELL_X(col), CELL_Y(row));
            }
        }
    }
}

/* The global sprites' frames the minimap draws around itself (F458): */
enum {
    MINIMAP_OFF_FRAME = 5,       /* 1E4A0: instead of the minimap (it and Wizard Eye off), at (E8h, 9) */
    MINIMAP_OFF_X = 0xe8,
    MINIMAP_OFF_Y = 9,
    MINIMAP_ARROW_X = 0x10b,     /* 1EC8C: outdoors, the party's direction + 1 at (10Bh, 24h) */
    MINIMAP_ARROW_Y = 0x24,
    MINIMAP_TOP_FRAME = 6,       /* 1ECB0: over the shown minimap, at (DFh, 3) */
    MINIMAP_TOP_X = 0xdf,
    MINIMAP_TOP_Y = 3
};
/** Builds the minimap's draws for the current position into OUT without a driver. */
xeen_status xeen_scene_minimap(xeen_movement *real, xeen_minimap_draws *out)
{
    return xeen_scene_minimap_driver(real, out, NULL, NULL, NULL);
}
/*
 * Builds the minimap's draws (1E467) into OUT on a copy of REAL, as xeen_scene_map does: nothing while the combat
 * (2), right-column (10) or speech (40) window is open; with the minimap and Wizard Eye both off, the "off" frame;
 * else the outdoor or indoor minimap (Wizard Eye hidden while the minimap is shown; indoors the phase steps,
 * anim_step), the outdoor arrow and the top frame. FATAL(USER, draw) nonzero for a draw 4AC92 would not return from
 * (4AD19) ends the pass at that draw, the last listed: REAL takes the copy as it stands there (Wizard Eye included),
 * *STOPPED = 1, XEEN_OK. Otherwise REAL takes the copy only when the whole pass succeeded; on a failure OUT is
 * emptied. Returns XEEN_OK, a lookup's status, or XEEN_BAD_INPUT (no arguments, no memory for the copy, or more than
 * XEEN_MINIMAP_MAX_DRAWS draws).
 */
xeen_status xeen_scene_minimap_driver(xeen_movement *real, xeen_minimap_draws *out,
                                      xeen_minimap_driver_fn fatal, void *user, int *stopped)
{
    xeen_movement *m;
    ctx c;
    uint8_t saved;
    if (!real || !out) {
        return XEEN_BAD_INPUT;
    }
    out->count = 0;
    if (real->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_COMBAT)] || real->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_RIGHT_COLUMN)] ||
        real->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        return XEEN_OK;
    }
    m = malloc(sizeof *m);
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    *m = *real;
    c.m = m;
    c.out = out;
    c.status = XEEN_OK;
    c.fatal = fatal;
    c.user = user;
    c.stopped = 0;
    if (stopped) {
        *stopped = 0;
    }
    if (!B(XEEN_DS_MINIMAP_SHOWN) && !B(XEEN_DS_EFFECT_WIZARD_EYE)) {
        push_draw(&c, W(XEEN_DS_GLOBAL_SPRITES), MINIMAP_OFF_FRAME, MINIMAP_OFF_X, MINIMAP_OFF_Y);
    } else {
        saved = B(XEEN_DS_EFFECT_WIZARD_EYE);
        if (B(XEEN_DS_MINIMAP_SHOWN)) {
            B(XEEN_DS_EFFECT_WIZARD_EYE) = 0;
        }
        if (W(XEEN_DS_OUTDOORS)) {
            draw_outdoor_minimap(&c);
        } else {
            draw_indoor_minimap(&c, anim_step(anim_base(m)));
        }
        if (c.status == XEEN_OK) {
            if (W(XEEN_DS_OUTDOORS)) {
                push_draw(&c, W(XEEN_DS_GLOBAL_SPRITES), (uint16_t)(B(XEEN_DS_PARTY_DIRECTION) + 1u), MINIMAP_ARROW_X,
                          MINIMAP_ARROW_Y);
            }
            push_draw(&c, W(XEEN_DS_GLOBAL_SPRITES), MINIMAP_TOP_FRAME, MINIMAP_TOP_X, MINIMAP_TOP_Y);
            B(XEEN_DS_EFFECT_WIZARD_EYE) = saved;
        }
    }
    if (c.stopped) { /* the DS as at the failed draw (the last one listed) */
        *real = *m;
        free(m);
        if (stopped) {
            *stopped = 1;
        }
        return XEEN_OK;
    }
    if (c.status == XEEN_OK) {
        *real = *m;
    } else {
        out->count = 0;
    }
    free(m);
    return c.status;
}

/* ------------------------------------------------ 1DA2D, the map */
xeen_status xeen_map_begin(xeen_movement *m, xeen_map_view *v)
{
    int px, py;
    if (!m || !v) {
        return XEEN_BAD_INPUT;
    }
    memset(v, 0, sizeof *v);
    v->anim = anim_base(m);
    if (!find_slot(m, B(XEEN_DS_PARTY_MAP))) {
        return XEEN_CACHE_MISS;
    }
    px = xeen_signed_byte(B(XEEN_DS_PARTY_X));
    py = xeen_signed_byte(B(XEEN_DS_PARTY_Y));
    v->x = (int16_t)px;
    v->y = (int16_t)py;
    /* The arrow's x is px x 10 + 4 near the west edge or + 100 near the east one, kept to a byte (the original's
     * AL), and the view's column 7 or 23; else the arrow stays at 74. Original: 1DA96 ([BP-0Bh], [BP-4]). */
    if (px < 8 && !W(XEEN_DS_MAP_FIELD(WEST))) {
        v->arrow_x = (uint8_t)(px * 10 + 4);
        v->x = 7;
    } else if (px > 23) {
        v->arrow_x = (uint8_t)(px * 10 + 100);
        v->x = 23;
    } else if (px > 8 && !W(XEEN_DS_MAP_FIELD(EAST))) {
        v->arrow_x = (uint8_t)(px * 10 + 4);
        v->x = 7;
    } else {
        v->arrow_x = 74;
    }
    /* The arrow's y is 8 x the row table's byte for py plus 13, kept to a byte, and the view's row 8 or 24; else 69.
     * Original: 1DAF6 ([BP-0Ch] from 4F9E/4F8E, [BP-6]). */
    if (py < 8 && !W(XEEN_DS_MAP_FIELD(SOUTH))) {
        v->arrow_y = (uint8_t)((B((uint16_t)(XEEN_DS_MINIMAP_ROWS + (unsigned)py)) << 3) + 13);
        v->y = 8;
    } else if (py > 24) {
        v->arrow_y = (uint8_t)((B((uint16_t)(XEEN_DS_MINIMAP_ROWS - 16u + (unsigned)py)) << 3) + 13);
        v->y = 24;
    } else if (py > 7 && !W(XEEN_DS_MAP_FIELD(NORTH))) {
        v->arrow_y = (uint8_t)((B((uint16_t)(XEEN_DS_MINIMAP_ROWS + (unsigned)py)) << 3) + 13);
        v->y = 8;
    } else {
        v->arrow_y = 69;
    }
    return XEEN_OK;
}
typedef struct {
    xeen_movement *m;
    xeen_map_draws *out;
    xeen_status status;
} map_ctx;
/** Appends one sprite draw to the full map's draw list; a full list sets the context's error. */
static void map_draw(map_ctx *c, uint16_t handle, uint16_t frame, uint16_t x, uint16_t y)
{
    xeen_sprite_draw *d;
    if (c->status != XEEN_OK) {
        return;
    }
    if (c->out->count == XEEN_MAP_MAX_DRAWS) {
        c->status = XEEN_BAD_INPUT;
        return;
    }
    d = &c->out->draws[c->out->count++];
    d->handle = handle;
    d->frame = frame;
    d->x = x;
    d->y = y;
    d->scale = 0;
    d->flags = 0;
    d->window = 0;
}
/* 1C680(x, y, mask, shift) with the view's absolute coordinates. */
static uint16_t map_look(map_ctx *c, int x, int y, uint16_t mask, uint16_t shift)
{
    uint16_t r = 0;
    if (c->status != XEEN_OK) {
        return 0;
    }
    c->status = xeen_maze_lookup(c->m, (int16_t)x, (int16_t)y, mask, shift, &r);
    return r;
}
/* A 16x16 pass: rows top (y + 7) down, columns left (x - 7) right; screen
 * (50h + 10 col, 26h + 8 row). */
#define MAP_X(v, col) ((v)->x - 7 + (col))
#define MAP_Y(v, row) ((v)->y + 7 - (row))
#define MAP_PX(col)   ((uint16_t)(0x50 + 10 * (col)))
#define MAP_PY(row)   ((uint16_t)(0x26 + 8 * (row)))
/* An indoor surface's tile sits 5 right and 5 down of its cell; column or row -1 is the ring left of and above the
 * grid (its surfaces and edges). */
#define MAP_SX(col) ((uint16_t)(MAP_PX(col) + 5))
#define MAP_SY(row) ((uint16_t)(MAP_PY(row) + 5))
/* The party's arrow: the view's arrow position (in the minimap's units) + 51h, 1Dh (1E0C9). */
#define MAP_ARROW_DX 0x51
#define MAP_ARROW_DY 0x1d
/*
 * Builds the full map's outdoor cells: the three layers (outdoor_layers) over the 16 x 16 cells around the view's
 * origin, a cell drawn when it is seen (the full map ignores the Wizard Eye).
 */
static void map_outdoor(map_ctx *c, const xeen_map_view *v)
{
    xeen_movement *m = c->m;
    const outdoor_layer *layer;
    int row, col;
    for (layer = outdoor_layers; layer < outdoor_layers + 3; ++layer) {
        for (row = 0; row < 16; ++row) {
            for (col = 0; col < 16; ++col) {
                uint16_t r = map_look(c, MAP_X(v, col), MAP_Y(v, row), layer->field_mask, layer->field_shift), tile;
                if (c->status == XEEN_OK && outdoor_tile(m, layer, r, &tile) && W(XEEN_DS_CELL_SEEN)) {
                    map_draw(c, W(XEEN_DS_MINIMAP_TILES), tile, MAP_PX(col), MAP_PY(row));
                }
            }
        }
    }
}
/*
 * The surface tile of the indoor cell just looked up (R, the lookup's result) at X, Y: drawn when the cell is on a
 * map, has a surface and is seen; the tile is the class of the surface kind in the DS's own classification bytes
 * (not the cell's slot) + 24h. Original: 1DDE5.
 */
static void map_surface(map_ctx *c, uint16_t r, uint16_t x, uint16_t y)
{
    xeen_movement *m = c->m;
    if (c->status == XEEN_OK && r != XEEN_CELL_OUTSIDE_MAPS && W(XEEN_DS_CELL_SURFACE) && W(XEEN_DS_CELL_SEEN)) {
        map_draw(c, W(XEEN_DS_MINIMAP_TILES), (uint16_t)(CELL_CLASS() + 0x24u), x, y);
    }
}
/* Wall FRAME (wall_frame() of the cell just looked up) at X, Y, unless there is no wall or the cell is unseen. */
static void map_wall(map_ctx *c, uint16_t frame, uint16_t x, uint16_t y)
{
    xeen_movement *m = c->m;
    if (c->status == XEEN_OK && frame != NO_WALL_FRAME && W(XEEN_DS_CELL_SEEN)) {
        map_draw(c, W(XEEN_DS_MINIMAP_TILES), frame, x, y);
    }
}
/*
 * Builds the full map's indoor cells over the 16 x 16 cells around the view's origin, in layers, each in row
 * order: the floor of every cell on a map and seen; the surfaces of the ring left of and above the grid (the left
 * column, then the corner, then the top row: an order unlike the minimap's) and then of the grid's cells, offset half
 * a cell; the edges' walls; each cell's two walls with the party's arrow (blinking, at the view's arrow position)
 * before its cell; the fog (a cell off the maps or unseen). A cell is seen by its own seen bit only (the full map
 * ignores the Wizard Eye). Every lookup runs even when its draw is skipped. Original: the left column 1DDB4, the
 * edges 1DF7D, the walls 1E0C9, the fog 1E282.
 */
static void map_indoor(map_ctx *c, const xeen_map_view *v)
{
    xeen_movement *m = c->m;
    int row, col, i;
    uint16_t r;
    for (row = 0; row < 16; ++row) {
        for (col = 0; col < 16; ++col) { /* floor */
            r = map_look(c, MAP_X(v, col), MAP_Y(v, row), 0xffff, 0);
            if (c->status == XEEN_OK && r != XEEN_CELL_OUTSIDE_MAPS && W(XEEN_DS_CELL_SEEN)) {
                map_draw(c, W(XEEN_DS_MINIMAP_TILES), 0, MAP_PX(col), MAP_PY(row));
            }
        }
    }
    for (row = 0; row < 16; ++row) { /* 1DDB4: the left column (x 4Bh) */
        r = map_look(c, v->x - 8, MAP_Y(v, row), 0xffff, 0);
        map_surface(c, r, MAP_SX(-1), MAP_SY(row));
    }
    r = map_look(c, v->x - 8, v->y + 8, 0xffff, 0); /* 1DE20: the corner */
    map_surface(c, r, MAP_SX(-1), MAP_SY(-1));
    for (col = 0; col < 16; ++col) { /* 1DE71: the top row (y 23h) */
        r = map_look(c, MAP_X(v, col), v->y + 8, 0xffff, 0);
        map_surface(c, r, MAP_SX(col), MAP_SY(-1));
    }
    for (row = 0; row < 16; ++row) {
        for (col = 0; col < 16; ++col) {
            r = map_look(c, MAP_X(v, col), MAP_Y(v, row), 0xffff, 0);
            map_surface(c, r, MAP_SX(col), MAP_SY(row));
        }
    }
    for (i = 0; i < 17; ++i) { /* 1DF7D: the edges, the left one bottom (row 15) to top (row -1), then the top one */
        map_wall(c, wall_frame(edge_left, map_look(c, v->x - 8, v->y - 8 + i, 0xffff, 12), v->anim), MAP_PX(-1),
                 MAP_PY(15 - i));
        map_wall(c, wall_frame(edge_odd, map_look(c, v->x - 7 + i, v->y + 8, 0x0f, 0), v->anim), MAP_PX(i),
                 MAP_PY(-1));
    }
    for (row = 0; row < 16; ++row) {
        for (col = 0; col < 16; ++col) { /* 1E0C9: the walls */
            /* The arrow's cell: its x / 10 is the column; its y is divided by 10 too (1E113: BX still 10 from the
             * x division) and mapped to the row by the signed bytes at 4F9F (MINIMAP_ROWS + 1). */
            if (v->arrow_x / 10 == col && (int)(int8_t)B(XEEN_DS_MINIMAP_ROWS + 1u + v->arrow_y / 10) == row &&
                v->blink) {
                map_draw(c, W(XEEN_DS_GLOBAL_SPRITES), (uint16_t)(B(XEEN_DS_PARTY_DIRECTION) + 1u),
                         (uint16_t)(v->arrow_x + MAP_ARROW_DX), (uint16_t)(v->arrow_y + MAP_ARROW_DY));
            }
            map_wall(c, wall_frame(cell_even, map_look(c, MAP_X(v, col), MAP_Y(v, row), 0xffff, 12), v->anim),
                     MAP_PX(col), MAP_PY(row));
            map_wall(c, wall_frame(edge_odd, map_look(c, MAP_X(v, col), MAP_Y(v, row), 0x0f, 0), v->anim),
                     MAP_PX(col), MAP_PY(row));
        }
    }
    for (row = 0; row < 16; ++row) {
        for (col = 0; col < 16; ++col) { /* 1E282: the fog */
            r = map_look(c, MAP_X(v, col), MAP_Y(v, row), 0xffff, 0);
            if (c->status == XEEN_OK && (r == XEEN_CELL_OUTSIDE_MAPS || !W(XEEN_DS_CELL_SEEN))) {
                map_draw(c, W(XEEN_DS_MINIMAP_TILES), 1, MAP_PX(col), MAP_PY(row));
            }
        }
    }
}
/*
 * Builds the draws of the full map view V (the 1DA2D map) into OUT. The pass runs on a copy of REAL: its maze lookups
 * write their scratch words (the cell's surface and seen words, the map slot) as the original's do, and the copy
 * replaces REAL only when the whole pass succeeded; on a failure REAL is unchanged and OUT is emptied. Indoors V's
 * animation phase steps first (anim + 2, signed mod 8, 1DD27's CWD/IDIV): it is kept on success and restored on a
 * failure. Returns XEEN_OK, a lookup's failure status, or XEEN_BAD_INPUT (no arguments, no memory for the copy, or
 * more than XEEN_MAP_MAX_DRAWS draws).
 */
xeen_status xeen_scene_map(xeen_movement *real, xeen_map_view *v, xeen_map_draws *out)
{
    xeen_movement *m;
    map_ctx c;
    uint16_t anim;
    if (!real || !v || !out) {
        return XEEN_BAD_INPUT;
    }
    out->count = out->framed = 0;
    m = malloc(sizeof *m);
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    *m = *real;
    c.m = m;
    c.out = out;
    c.status = XEEN_OK;
    anim = v->anim;
    if (W(XEEN_DS_OUTDOORS)) {
        map_outdoor(&c, v);
    } else {
        v->anim = anim_step(v->anim); /* 1DD27 */
        map_indoor(&c, v);
    }
    out->framed = out->count; /* 1E300 4F6F8(5) */
    if (!W(XEEN_DS_OUTDOORS)) {
        map_draw(&c, W(XEEN_DS_MINIMAP_TILES), 0x34, 0x4c, 0x1e);
    } else if (v->blink) {
        map_draw(&c, W(XEEN_DS_GLOBAL_SPRITES), (uint16_t)(B(XEEN_DS_PARTY_DIRECTION) + 1u),
                 (uint16_t)(v->arrow_x + 0x4c), (uint16_t)(v->arrow_y + 0x19));
    }
    if (c.status == XEEN_OK) {
        *real = *m;
    } else {
        out->count = out->framed = 0;
        v->anim = anim;
    }
    free(m);
    return c.status;
}
