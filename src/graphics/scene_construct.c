#include "xeen/scene_construct.h"
#include "xeen/ds_globals.h"
#include "xeen/raster.h"
#include "outdoor_terrain_tables.inc"

typedef struct {
    uint8_t query;
    uint16_t frames;
    uint8_t inverse, constant;
} terrain_slot;

/* Frame pair N of the program data's terrain_frame_pairs (DS:094E, ten [index 0, index 1] byte pairs). */
#define XEEN_TERRAIN_FRAME_PAIR(n) (0x94eu + 2u * (n))
/* Query order and frame tables are read directly from 22034..22B25.
 * Records are consecutive original12-byte renderer records at 0986.
 * The indoor ground (records 3..27): each slot's query is a step entry (XEEN_DS_STEP_X/Y), i.e. a cell at (side,
 * ahead) from the party, whose surface (F490) it draws. By depth, farthest first: the left cells from the outside in,
 * the right ones with the same frame pairs mirrored (INVERSE), then the party's column with a fixed frame. */
static const terrain_slot slots[] = {
        /* depth 4: (-3, 4) (-2, 4) (-1, 4); (3, 4) (2, 4) (1, 4); (0, 4) */
        {36, XEEN_TERRAIN_FRAME_PAIR(0), 0, 0}, {37, XEEN_TERRAIN_FRAME_PAIR(1), 0, 0},
        {38, XEEN_TERRAIN_FRAME_PAIR(2), 0, 0}, {43, XEEN_TERRAIN_FRAME_PAIR(0), 1, 0},
        {42, XEEN_TERRAIN_FRAME_PAIR(1), 1, 0}, {41, XEEN_TERRAIN_FRAME_PAIR(2), 1, 0},
        {39, 0, 0, 21},
        /* depth 3: (-3, 3) (-2, 3) (-1, 3); (3, 3) (2, 3) (1, 3); (0, 3) */
        {20, XEEN_TERRAIN_FRAME_PAIR(3), 0, 0}, {22, XEEN_TERRAIN_FRAME_PAIR(4), 0, 0},
        {24, XEEN_TERRAIN_FRAME_PAIR(5), 0, 0}, {33, XEEN_TERRAIN_FRAME_PAIR(3), 1, 0},
        {31, XEEN_TERRAIN_FRAME_PAIR(4), 1, 0}, {29, XEEN_TERRAIN_FRAME_PAIR(5), 1, 0},
        {26, 0, 0, 14},
        /* depth 2: (-2, 2) (-1, 2); (2, 2) (1, 2); (0, 2) */
        {10, XEEN_TERRAIN_FRAME_PAIR(6), 0, 0}, {11, XEEN_TERRAIN_FRAME_PAIR(7), 0, 0},
        {18, XEEN_TERRAIN_FRAME_PAIR(6), 1, 0}, {16, XEEN_TERRAIN_FRAME_PAIR(7), 1, 0},
        {13, 0, 0, 8},
        /* depth 1: (-1, 1); (1, 1); (0, 1) */
        {5, XEEN_TERRAIN_FRAME_PAIR(8), 0, 0}, {9, XEEN_TERRAIN_FRAME_PAIR(8), 1, 0}, {6, 0, 0, 4},
        /* the party's row: (-1, 0); (1, 0); its own cell (0, 0) */
        {0, XEEN_TERRAIN_FRAME_PAIR(9), 0, 0}, {4, XEEN_TERRAIN_FRAME_PAIR(9), 1, 0}, {1, 0, 0, 1}};

/* One 22034/246CD slot: handle F432[F490], then frame/flags in the
 * original order for special (the animated surface classes: indoors water, lava and sewer; outdoors deep water and
 * lava) and other surfaces. */
static int terrain_slot_apply(xeen_movement *m, uint8_t query, uint16_t frames, uint8_t inverse,
                              uint8_t constant, uint16_t record, int outdoor)
{
    uint16_t ax, index, flags;
    uint8_t type;
    int special;
    if (xeen_navigation_query(m, query, &ax) != XEEN_OK) {
        return 0;
    }
    index = xeen_read16(m, XEEN_DS_CELL_SURFACE);
    xeen_write16(m, record, xeen_read16(m, (uint16_t)(XEEN_DS_SURFACE_SPRITES + 2u * index)));
    if (!frames) {
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), constant);
    }
    type = m->ds[(uint16_t)(XEEN_DS_MAP_FIELD(CLASSIFICATION) + index)];
    /* the animated surface classes */
    special = outdoor ? (type == XEEN_SURFACE_DEEP_WATER || type == XEEN_SURFACE_LAVA)
                      : (type == XEEN_SURFACE_WATER || type == XEEN_SURFACE_LAVA || type == XEEN_SURFACE_SEWER);
    flags = special ? xeen_read16(m, XEEN_DS_SURFACE_FLIP) : (m->ds[XEEN_DS_STEP_PARITY] ? XEEN_DRAW_MIRRORED : 0);
    /* Special terrain writes flags BEFORE selecting frame; other terrain
     * writes frame first. Preserve order, including original DS aliasing. */
    if (special) {
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS), flags);
    }
    if (frames) {
        uint16_t frame_index =
                special ? (uint16_t)(xeen_read16(m, XEEN_DS_SURFACE_FLIP) != 0) : m->ds[XEEN_DS_STEP_PARITY];
        if (inverse) {
            frame_index = (uint16_t)(frame_index == 0);
        }
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), m->ds[(uint16_t)(frames + frame_index)]);
    }
    if (!special) {
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS), flags);
    }
    return 1;
}

xeen_caller_result xeen_scene_construct_terrain(xeen_movement *m)
{
    xeen_caller_result result = {XEEN_BAD_INPUT, XEEN_SCENE_INDOOR_TERRAIN};
    unsigned i;
    if (!m) {
        return result;
    }
    for (i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        const terrain_slot *slot = &slots[i];
        if (!terrain_slot_apply(m, slot->query, slot->frames, slot->inverse, slot->constant,
                                (uint16_t)(XEEN_DS_INDOOR_VIEW_RECORD(3u) + XEEN_VIEW_RECORD_BYTES * i), 0)) {
            result.status = m->status;
            result.boundary = XEEN_NAVIGATION_QUERY;
            return result;
        }
    }
    m->status = XEEN_SERVICE_REQUIRED;
    result.status = m->status;
    result.boundary = XEEN_SCENE_INDOOR_GEOMETRY;
    return result;
}

xeen_caller_result xeen_scene_construct_outdoor(xeen_movement *m)
{
    xeen_caller_result result = {XEEN_BAD_INPUT, XEEN_SCENE_OUTDOOR_TERRAIN};
    unsigned i, k;
    if (!m || !xeen_read16(m, XEEN_DS_OUTDOORS)) {
        return result;
    }
    for (i = 0; i < sizeof(outdoor_terrain_slots) / sizeof(outdoor_terrain_slots[0]); ++i) {
        const outdoor_terrain_slot *slot = &outdoor_terrain_slots[i];
        if (!terrain_slot_apply(m, slot->query, slot->frames, slot->inverse, slot->constant,
                                (uint16_t)(XEEN_DS_OUTDOOR_VIEW_RECORD(3u) + XEEN_VIEW_RECORD_BYTES * i), 1)) {
            result.status = m->status;
            result.boundary = XEEN_NAVIGATION_QUERY;
            return result;
        }
    }
    if (xeen_scene_light(m) != XEEN_OK) {
        result.status = m->status;
        result.boundary = XEEN_METADATA_QUERY;
        return result;
    }
    for (i = 0; i < sizeof(outdoor_wall_slots) / sizeof(outdoor_wall_slots[0]); ++i) {
        const outdoor_wall_slot *wall = &outdoor_wall_slots[i];
        uint16_t ax, handle;
        if (xeen_navigation_query(m, wall->query, &ax) != XEEN_OK) {
            result.status = m->status;
            result.boundary = XEEN_NAVIGATION_QUERY;
            return result;
        }
        handle = xeen_read16(m, (uint16_t)(XEEN_DS_WALL_SPRITES + 2u * xeen_read16(m, XEEN_DS_CELL_WALLS)));
        for (k = 0; k < wall->count; ++k) {
            xeen_write16(m, wall->record[k], handle);
        }
    }
    xeen_write16(m, XEEN_DS_OUTDOOR_VIEW_RECORD(1u) + XEEN_VIEW_RECORD_FLAGS, xeen_read16(m, XEEN_DS_TURN_FLIP));
    xeen_write16(m, XEEN_DS_OUTDOOR_VIEW_RECORD(0u) + XEEN_VIEW_RECORD_FLAGS, xeen_read16(m, XEEN_DS_TURN_FLIP));
    xeen_write16(m, XEEN_DS_OUTDOOR_VIEW_RECORD(2u) + XEEN_VIEW_RECORD_FLAGS, xeen_read16(m, XEEN_DS_SURFACE_FLIP));
    m->status = XEEN_SERVICE_REQUIRED;
    result.status = m->status;
    result.boundary = XEEN_SCENE_OUTDOOR_RENDER;
    return result;
}

/* A guard term fires when its byte(s) are nonzero: VT_BOTH needs both bytes
 * nonzero; VT_BITAND is the original AL=a AND b test (240DF), so 1 and 2
 * do not fire. A rule is skipped when any guard term fires; otherwise the
 * first nonzero choice byte writes its value (or word F47C+value). */
enum {
    VT_BYTE,
    VT_BOTH,
    VT_BITAND
};
typedef struct {
    uint16_t a, b;
    uint8_t kind;
} vis_term;
typedef struct {
    uint16_t flag, value;
    uint8_t relative;
} vis_choice;
typedef struct {
    uint32_t original;
    uint16_t dest;
    uint16_t term, terms, choice, choices;
} vis_rule;
/* The rules' view flags, by the layout of XEEN_DS_VIEW_WALLS (its draw positions in ds_globals.h): VW(p) and VW2(p)
 * by draw position p, VF(kind, f) by face index f; the positions and faces by place (FACE/LEFT/RIGHT, FACE_AT). */
#define VW(position)     XEEN_DS_VIEW_WALL(position)
#define VW2(position)    XEEN_DS_VIEW_WALL_2(position)
#define VF(kind, face)   XEEN_DS_VIEW_FACE(kind##u, face)
#define VFRAME(record)   XEEN_VIEW_FIELD(XEEN_DS_INDOOR_VIEW_LIST, record, FRAME)
#define FACE(ahead, side)    XEEN_VIEW_POS_FACE(ahead, side)
#define LEFT(ahead, side)    XEEN_VIEW_POS_LEFT(ahead, side)
#define RIGHT(ahead, side)   XEEN_VIEW_POS_RIGHT(ahead, side)
#define FACE_AT(ahead, side) XEEN_VIEW_FACE_AT(ahead, side)
#include "scene_visibility.inc"
#undef VW
#undef VW2
#undef VF
#undef VFRAME
#undef FACE
#undef LEFT
#undef RIGHT
#undef FACE_AT

/** Returns nonzero when visibility term T holds (both, either or one of its view flags set). */
static int term_fires(const xeen_movement *m, const vis_term *t)
{
    switch (t->kind) {
        case VT_BOTH:
            return m->ds[t->a] && m->ds[t->b];
        case VT_BITAND:
            return (m->ds[t->a] & m->ds[t->b]) != 0;
        default:
            return m->ds[t->a] != 0;
    }
}
/*
 * Applies the indoor view's 39 visibility rules (scene_visibility.inc) in order. A rule is skipped when any of its
 * guard terms fires (term_fires); otherwise its first choice whose view flag is set writes the rule's destination,
 * the frame word of an indoor view record (VFRAME): the choice's value, or word F47C plus the value for a relative
 * choice. No choice set leaves the frame word as it was; no view flag is changed.
 * Original: 22B25..243EC (each rule's own address in vis_rules[].original).
 */
static void visibility_rules(xeen_movement *m)
{
    unsigned r, i;
    for (r = 0; r < sizeof(vis_rules) / sizeof(vis_rules[0]); ++r) {
        const vis_rule *rule = &vis_rules[r];
        int skip = 0;
        for (i = 0; i < rule->terms && !skip; ++i) {
            skip = term_fires(m, &vis_terms[rule->term + i]);
        }
        if (skip) {
            continue;
        }
        for (i = 0; i < rule->choices; ++i) {
            const vis_choice *c = &vis_choices[rule->choice + i];
            if (!m->ds[c->flag]) {
                continue;
            }
            xeen_write16(m, rule->dest,
                         c->relative ? (uint16_t)(xeen_read16(m, 0xf47c) + c->value) : c->value);
            break;
        }
    }
}
/** Returns nonzero when any of the N DS bytes listed in BYTES is set. */
static int any_byte(const xeen_movement *m, const uint16_t *bytes, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) {
        if (m->ds[bytes[i]]) {
            return 1;
        }
    }
    return 0;
}
/* 243EC..24494: side surfaces. Frame word is written before surface word. */
static void side_surface(xeen_movement *m, uint16_t blocker, const uint16_t near[6],
                         uint16_t far_flag, uint16_t record)
{
    if (m->ds[blocker]) {
        return;
    }
    if (any_byte(m, near, 6)) {
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), 0);
        xeen_write16(m, record, xeen_read16(m, XEEN_DS_INDOOR_FWL_SPRITE(1u)));
    } else if (m->ds[far_flag]) {
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), 9);
        xeen_write16(m, record, xeen_read16(m, XEEN_DS_INDOOR_FWL_SPRITE(2u)));
    }
}
/* The surface records 143..147: the left surface (143, its frame word also 144's), the wall ahead (145), the right
 * surface (147, 146's frame word). */
#define LEFT_SURFACE   XEEN_DS_INDOOR_VIEW_RECORD(143u)
#define CENTRE_SURFACE XEEN_DS_INDOOR_VIEW_RECORD(145u)
#define RIGHT_SURFACE  XEEN_DS_INDOOR_VIEW_RECORD(147u)
#define FACE_AHEAD(kind) XEEN_DS_VIEW_FACE(kind, XEEN_VIEW_FACE_AHEAD)
/*
 * 24494..245C1: the wall ahead of the party (view slot 2), drawn into record 145. A face of kind 4 or 13 takes
 * f<style>2.fwl at frame 0, or while XEEN_DS_DOOR_OPENING at 1 (10 in a town map); otherwise the first of these kinds
 * present gives the file and frame: kind 2 (file 2, frame 9), 12 (file 1, F47C + 1), 1 (2, 1), 9 (2, 2), 3 (2, 3), 5 or
 * 8 (1, 0), 7 (2, 8), 6 (2, 7), 11 (1, 6), 14 (2, 4), 15 (2, 5), 10 (2, 6).
 */
static void centre_surface(xeen_movement *m)
{
    static const struct {
        uint16_t flag, frame;
        uint8_t relative, near;
    } chain[] = {{XEEN_DS_VIEW_WALL_2(XEEN_VIEW_POSITION_AHEAD), 9, 0, 0}, {FACE_AHEAD(12u), 1, 1, 1},
                 {FACE_AHEAD(1u), 1, 0, 0}, {FACE_AHEAD(9u), 2, 0, 0}, {FACE_AHEAD(3u), 3, 0, 0},
                 {XEEN_DS_VIEW_WALL(XEEN_VIEW_POSITION_AHEAD), 0, 0, 1}, {FACE_AHEAD(7u), 8, 0, 0},
                 {FACE_AHEAD(6u), 7, 0, 0}, {FACE_AHEAD(11u), 6, 0, 1}, {FACE_AHEAD(14u), 4, 0, 0},
                 {FACE_AHEAD(15u), 5, 0, 0}, {FACE_AHEAD(10u), 6, 0, 0}};
    unsigned i;
    if (m->ds[FACE_AHEAD(4u)]) {
        xeen_write16(m, CENTRE_SURFACE + XEEN_VIEW_RECORD_FRAME,
                     m->ds[XEEN_DS_DOOR_OPENING] ? (m->ds[XEEN_DS_MAP_FIELD(STYLE)] != XEEN_MAP_STYLE_TOWN ? 1u : 10u)
                                                 : 0u);
        xeen_write16(m, CENTRE_SURFACE, xeen_read16(m, XEEN_DS_INDOOR_FWL_SPRITE(2u)));
        return;
    }
    for (i = 0; i < sizeof(chain) / sizeof(chain[0]); ++i) {
        if (!m->ds[chain[i].flag]) {
            continue;
        }
        xeen_write16(m, CENTRE_SURFACE + XEEN_VIEW_RECORD_FRAME,
                     chain[i].relative ? (uint16_t)(xeen_read16(m, 0xf47c) + chain[i].frame)
                                       : chain[i].frame);
        xeen_write16(m, CENTRE_SURFACE,
                     xeen_read16(m, chain[i].near ? XEEN_DS_INDOOR_FWL_SPRITE(1u) : XEEN_DS_INDOOR_FWL_SPRITE(2u)));
        return;
    }
}
/* The face kinds side_surface draws from f<style>1.fwl: 4 or 13, 5 or 8, 12, 1, 3, 9 (at face index F). */
#define NEAR_FACES(position, face)                                                                                   \
    {XEEN_DS_VIEW_FACE(4u, face), XEEN_DS_VIEW_WALL(position), XEEN_DS_VIEW_FACE(12u, face),                        \
     XEEN_DS_VIEW_FACE(1u, face),  XEEN_DS_VIEW_FACE(3u, face),  XEEN_DS_VIEW_FACE(9u, face)}
/**
 * Builds the indoor view's surfaces (243EC..245C1): unless the party's own left (right) wall is one of a kind other
 * than 2, the left (right) surface shows the face of the cell to that side (view slots 0 and 4: the faces of the
 * left and right cells). Then the wall ahead, and the frame of records 144 and 146: 0 (1) under such a wall, 18h (19h)
 * under a kind-2 one.
 */
static void surface_tail(xeen_movement *m)
{
    static const uint16_t left[6] = NEAR_FACES(XEEN_VIEW_POSITION_LEFT_FACE, XEEN_VIEW_FACE_LEFT);
    static const uint16_t right[6] = NEAR_FACES(XEEN_VIEW_POSITION_RIGHT_FACE, XEEN_VIEW_FACE_RIGHT);
    side_surface(m, XEEN_DS_VIEW_WALL(XEEN_VIEW_POSITION_LEFT), left,
                 XEEN_DS_VIEW_WALL_2(XEEN_VIEW_POSITION_LEFT_FACE), LEFT_SURFACE);
    side_surface(m, XEEN_DS_VIEW_WALL(XEEN_VIEW_POSITION_RIGHT), right,
                 XEEN_DS_VIEW_WALL_2(XEEN_VIEW_POSITION_RIGHT_FACE), RIGHT_SURFACE);
    centre_surface(m);
    if (m->ds[XEEN_DS_VIEW_WALL(XEEN_VIEW_POSITION_LEFT)]) {
        xeen_write16(m, LEFT_SURFACE + XEEN_VIEW_RECORD_BYTES + XEEN_VIEW_RECORD_FRAME, 0);
    } else if (m->ds[XEEN_DS_VIEW_WALL_2(XEEN_VIEW_POSITION_LEFT)]) {
        xeen_write16(m, LEFT_SURFACE + XEEN_VIEW_RECORD_BYTES + XEEN_VIEW_RECORD_FRAME, 0x18);
    }
    if (m->ds[XEEN_DS_VIEW_WALL(XEEN_VIEW_POSITION_RIGHT)]) {
        xeen_write16(m, RIGHT_SURFACE - XEEN_VIEW_RECORD_BYTES + XEEN_VIEW_RECORD_FRAME, 1);
    } else if (m->ds[XEEN_DS_VIEW_WALL_2(XEEN_VIEW_POSITION_RIGHT)]) {
        xeen_write16(m, RIGHT_SURFACE - XEEN_VIEW_RECORD_BYTES + XEEN_VIEW_RECORD_FRAME, 0x19);
    }
}
/* Table F45C indexed by arithmetic F480>>3 (signed word, 16-bit wrap). */
static uint16_t ground_handle(const xeen_movement *m)
{
    uint16_t word = xeen_read16(m, 0xf480);
    /* SAR 3: arithmetic shift keeps the sign bit. */
    uint16_t index = (uint16_t)((word >> 3) | ((word & 0x8000u) ? 0xe000u : 0u));
    return xeen_read16(m, (uint16_t)(0xf45cu + index * 2u));
}
/* The scene construction's geometry stage (contract in include/xeen/scene_construct.h): the view records'
 * handles and flags, then the service boundary. */
xeen_caller_result xeen_scene_construct_geometry(xeen_movement *m)
{
    xeen_caller_result result = {XEEN_BAD_INPUT, XEEN_SCENE_INDOOR_GEOMETRY};
    if (!m) {
        return result;
    }
    visibility_rules(m);
    surface_tail(m);
    if (xeen_metadata(m) != XEEN_OK) {
        result.status = m->status;
        result.boundary = XEEN_METADATA_QUERY;
        return result;
    }
    xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(0), ground_handle(m));
    xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(0) + XEEN_VIEW_RECORD_FLAGS, xeen_read16(m, XEEN_DS_TURN_FLIP));
    if (m->ds[XEEN_DS_DOOR_OPENING]) {
        /* The cell ahead: entry 7 of the facing's steps (XEEN_DS_STEP_X/Y; 0655/0715 + direction x 48, wrapping). */
        uint16_t row = (uint16_t)(m->ds[XEEN_DS_PARTY_DIRECTION] * 0x30u);
        int16_t x = (int16_t)(xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]) +
                              xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_STEP_X(0u, 7u) + row)]));
        int16_t y = (int16_t)(xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]) +
                              xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_STEP_Y(0u, 7u) + row)]));
        if (xeen_metadata_at(m, x, y) != XEEN_OK) {
            result.status = m->status;
            result.boundary = XEEN_METADATA_QUERY;
            return result;
        }
        xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(1), ground_handle(m));
    } else {
        xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(1), xeen_read16(m, XEEN_DS_INDOOR_VIEW_RECORD(0)));
    }
    xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(1) + XEEN_VIEW_RECORD_FLAGS, xeen_read16(m, XEEN_DS_TURN_FLIP));
    xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(2) + XEEN_VIEW_RECORD_FLAGS, xeen_read16(m, XEEN_DS_STEP_FLIP));
    xeen_write16(m, XEEN_DS_INDOOR_VIEW_RECORD(28) + XEEN_VIEW_RECORD_FRAME, 7); /* 0AB4: record 28's frame */
    m->status = XEEN_SERVICE_REQUIRED;
    result.status = m->status;
    result.boundary = XEEN_SCENE_INDOOR_RENDER;
    return result;
}

xeen_status xeen_scene_collect_draw_list(const xeen_movement *m, uint16_t window, uint16_t list,
                                         xeen_draw_list *out)
{
    uint32_t at;
    if (!m || !out) {
        return XEEN_BAD_INPUT;
    }
    out->window = window;
    out->list = list;
    out->records = 0;
    out->count = 0;
    for (at = list; at + XEEN_VIEW_RECORD_BYTES <= 0x10000u; at += XEEN_VIEW_RECORD_BYTES) {
        uint16_t record = (uint16_t)at, handle = xeen_read16(m, record);
        xeen_draw_command *c;
        if (!handle) {
            return XEEN_OK;
        }
        ++out->records;
        if ((handle & 0x8000u) || xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME)) == XEEN_VIEW_RECORD_UNUSED ||
            xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_SCALE)) == 0xffffu) {
            continue;
        }
        if (out->count == XEEN_DRAW_LIST_MAX) {
            return XEEN_BAD_INPUT;
        }
        c = &out->commands[out->count++];
        c->record = record;
        c->handle = handle;
        c->frame = xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME));
        c->x = xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_X));
        c->y = xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_Y));
        c->scale = xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_SCALE));
        c->flags = xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS));
    }
    return XEEN_BAD_INPUT;
}

/** Sets 97F7 (and its copy 97F9) to 1 when any member's byte at 9744+i is set, else 0. */
static void party_scan(xeen_movement *m)
{
    int16_t i;
    m->ds[0x97f7] = 0;
    for (i = 0; (int16_t)m->ds[XEEN_DS_PARTY_COUNT] > i; ++i) {
        if (m->ds[(uint16_t)(XEEN_DS_MISSILE_SLOTS + (uint16_t)i)]) {
            m->ds[0x97f7] = 1;
            break;
        }
    }
    m->ds[XEEN_DS_MISSILES_FLYING] = m->ds[0x97f7];
}
xeen_caller_result xeen_scene_construct_finish(xeen_movement *m)
{
    xeen_caller_result result = {XEEN_BAD_INPUT, 0x2469f};
    if (!m) {
        return result;
    }
    party_scan(m);
    m->status = XEEN_SERVICE_REQUIRED;
    result.status = m->status;
    result.boundary = XEEN_SCENE_INDOOR_RESTORE;
    return result;
}
xeen_caller_result xeen_scene_construct_outdoor_finish(xeen_movement *m)
{
    xeen_caller_result result = {XEEN_BAD_INPUT, 0x25390};
    if (!m || !xeen_read16(m, XEEN_DS_OUTDOORS)) {
        return result;
    }
    party_scan(m);
    m->status = XEEN_SERVICE_REQUIRED;
    result.status = m->status;
    result.boundary = XEEN_SCENE_OUTDOOR_RESTORE;
    return result;
}
