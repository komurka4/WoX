/*
 * The falls (Phase 246, from game.c): 173A0 from 17264's kind-10/13 cells and 17F07, operation 52's event
 * fall, the Darkside and Clouds fall tables, the fall's 1B503 scene and its callers' continuations
 * (fall_return, by fall_caller), and 1BF78..1C0DB's fall animation (502B3's frames).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a fall cannot be completed; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    FALL_STOP_RECORD = 0x33435,              /* operation 52's record runs past the event script */
    FALL_STOP_TRAVEL = 0x17602,              /* the map fallen to did not load */
    FALL_STOP_EVENT_EDGE_COMMIT = 0x17621,   /* operation 52 past the map's edge: the wrapped position */
    FALL_STOP_STEP_EDGE_COMMIT = 0x17334,    /* a step's fall past the map's edge: the wrapped position */
    FALL_STOP_EDGE_RELOAD = 0x19e77          /* the neighbouring map did not load */
};
/* The event fall has landed: operation 52 resumes, and the interpreter restarts at the party's new cell. Original:
 * 173A0(1) returned to 33443; 313CE (phase 17). */
xeen_status fall_event_return(xeen_game *g)
{
    g->execution.phase = XEEN_EXEC_PHASE_FALL_DONE;
    g->dispatcher.execution.phase = XEEN_EXEC_PHASE_FALL_DONE;
    g->phase = XG_STOPPED;
    g->scene_argument = 1;
    drive(g);
    return XEEN_SERVICE_REQUIRED;
}
/* The edge step after an outdoor fall whose destination lies past the map's edge: the coordinates wrap by 16 and the
 * neighbouring map (from the loaded map's links) is loaded; without a link nothing is loaded. The result says which
 * step failed, so each caller keeps its own stop site.
 * Original: 17608's 17621 for operation 52, 17264's 17334 for a step's fall; x or y bit 4 with F608 set; 19D94 (the
 * links C0D2..C0D4, 37Ch per F46A slot), 19E82; DX 0: 19E73. */
enum fall_wrap {
    FALL_WRAP_DONE,          /* wrapped and loaded, or nothing to wrap */
    FALL_WRAP_COMMIT_FAILED, /* 19D94 (xeen_boundary_commit) */
    FALL_WRAP_RELOAD_FAILED  /* 19E82 of the neighbour (map_reload) */
};
static enum fall_wrap fall_edge_wrap(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t target;
    if (!(((m->ds[XEEN_DS_PARTY_X] | m->ds[XEEN_DS_PARTY_Y]) & 0x10u) && xeen_read16(m, XEEN_DS_OUTDOORS))) {
        return FALL_WRAP_DONE;
    }
    g->transition_active = 1;
    g->transition_target = 0;
    if (xeen_boundary_commit(m) != XEEN_OK) {
        g->transition_active = 0;
        return FALL_WRAP_COMMIT_FAILED;
    }
    g->transition_active = 0;
    target = g->transition_target;
    if (target) {
        if (map_reload(g, target) != XR_OK) {
            return FALL_WRAP_RELOAD_FAILED;
        }
        g->scene_selected_group = 0;
    }
    return FALL_WRAP_DONE;
}
/**
 * The fall is over: its caller goes on (fall_caller). An event fall that fell (and wrapped past an edge) shows the
 * landing's damage scene, then resumes the script; one stopped by Darkside's flag resumes it at once. A fall from a
 * command's tail goes back to that tail. A step's fall (also at Rest's wake and after Shoot) wraps past an edge,
 * shows its damage scene, or the defeat, or returns to its caller: Shoot's command tail, Rest's end or the step's
 * surface tail.
 */
static xeen_status fall_return(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (g->fall_caller == SURFACE_FALL) {
        /* The event fall: the landing's damage (the record's signed byte) in a scene with the fall block drawn,
         * the member selector kept aside meanwhile (surface_after_scene); the fall that Darkside's flag 118 stopped
         * (no fall scene) has none of it.
         * Original: 17608 (SI = 1); the edge 1760C (19D94 with F608, below); F5DF flipped, 1B503(1) with 8C85 = 1,
         * 98D7 saved and cleared, 2E68D(CBW(8C81), 0, 0), 98D7 back; 173A8's return with 8C85 = 0. */
        if (m->ds[XEEN_DS_FALL_SCENE] != 1u) {
            return fall_event_return(g);
        }
        /* Past the edge (no shipped record has it: phase108 op52_census.py). Original: 17621. */
        switch (fall_edge_wrap(g)) {
            case FALL_WRAP_COMMIT_FAILED:
                return game_stop(g, FALL_STOP_EVENT_EDGE_COMMIT);
            case FALL_WRAP_RELOAD_FAILED:
                return game_stop(g, FALL_STOP_EDGE_RELOAD);
            default:
                break;
        }
        m->ds[XEEN_DS_STEP_PARITY] ^= 1;
        (void)surface_scene(g, 1, SURFACE_FALL);
        g->surface_damage = (uint32_t)(int32_t)(int8_t)m->ds[XEEN_DS_FALL_DAMAGE];
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->fall_caller != SURFACE_MOVEMENT && g->fall_caller != SURFACE_REST_WAKE &&
        g->fall_caller != SURFACE_SHOOT) {
        /* A command's tail: the defeat or the restart leave the loop, else the next command. Original: 17F15
         * (F60A or F64A -> 17F28, else 17ACC). */
        return command_finish(g);
    }
    /* A step across a map's edge onto a fall (Darkside's sky level: the neighbour's cell is read with the current
     * map's table) falls with x or y at 16 (or -1), which the sky rule keeps; from the fallen-to map the edge then
     * wraps (fall_edge_wrap), as for operation 52. The fall's own resume is needed: the generic transition resume
     * would drop the damage scene and leave the fall pending (a second fall).
     * Original: 1731F after 1731B's 173A0 (17277's 1C1C1(2)); 174B0, 19E82 of map - 58h; 17334 19D94; 17339 (SI);
     * the second fall at 17D50 with 8C85 = 1. */
    switch (fall_edge_wrap(g)) {
        case FALL_WRAP_COMMIT_FAILED:
            return game_stop(g, FALL_STOP_STEP_EDGE_COMMIT);
        case FALL_WRAP_RELOAD_FAILED:
            return game_stop(g, FALL_STOP_EDGE_RELOAD);
        default:
            break;
    }
    if (g->fall_damage) {
        /* The fall's damage in a scene with the fall block drawn (surface_after_scene); after Shoot or a cast it
         * returns to their command tail. Original: 1733D: F5DF flipped, 1B503(1) with 8C85 = 1, 2E68D(SI, 972E,
         * 0); owner 4 (17264 from 17EFA) returns to 17EFD, 17F07. */
        m->ds[XEEN_DS_STEP_PARITY] ^= 1;
        (void)surface_scene(g, 1, g->fall_caller == SURFACE_MOVEMENT ? SURFACE_MOVEMENT : g->fall_caller);
        g->surface_damage = g->fall_damage;
        return XEEN_SERVICE_REQUIRED;
    }
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        if (g->fall_caller == SURFACE_REST_WAKE) {
            g->shelter = 0;
        }
        return surface_scene(g, 2, g->fall_caller);
    }
    if (g->fall_caller == SURFACE_SHOOT) {
        return command_tail(g); /* Original: RETF to 17EFD, 17F07 */
    }
    if (g->fall_caller == SURFACE_REST_WAKE) {
        return rest_scene(g, 2); /* Original: RETF to 29124 */
    }
    return post_action_surface_tail(g);
}
/* Operation 52, FallToMap: the record's map, x, y and landing damage (its operand bytes; the damage byte is the next
 * byte of the resident buffer, so for a last record a stale byte of a longer script it held before, as the original's
 * buffer keeps), then the fall. Original: 3341C: 8C84..8C81 = record +6..+9 (9C62's buffer), 173A0(1). */
xeen_status event_fall(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    size_t at = (uint16_t)g->execution.state.offset;
    if (at + 9u >= sizeof g->resident) {
        return game_stop(g, FALL_STOP_RECORD);
    }
    ds[XEEN_DS_FALL_MAP] = g->resident[at + 6u];
    ds[XEEN_DS_FALL_X] = g->resident[at + 7u];
    ds[XEEN_DS_FALL_Y] = g->resident[at + 8u];
    ds[XEEN_DS_FALL_DAMAGE] = g->resident[at + 9u];
    m->fall_caller = SURFACE_FALL;
    m->fall_damage = 0;
    return fall_begin(g);
}
/* The fall (a step onto a falling cell, a command's tail, or operation 52): the caller and its damage are taken from
 * the movement. On Darkside with its game flag 118 set nothing falls (the caller goes on at once). Else two scene
 * passes (the second keeps the page for the fall block), then fall_after_scene places the party and loads the
 * destination; the fall block is drawn in the caller's next scene.
 * Original: 173A0 (from 17264's kind-10/13 cells, 1731B keeping SI for 1733D, or 17F07's 17F11; 17F15 after it);
 * 173A8: 2F847(F01F, 176h), 8C85 = 0, 17665; 8C85 = 0 and 1B503(1); 8C85 = 2 and 1B503(0) (1C0E5 186B2(8C90));
 * 8C85 = 1 and 19E82. */
xeen_status fall_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    g->fall_caller = m->fall_caller;
    g->fall_damage = m->fall_damage;
    m->fall_caller = 0;
    m->fall_damage = 0;
    if (m->ds[XEEN_DS_WORLD] && xeen_ds_get_bit(m->ds, XEEN_DS_GAME_FLAGS, 256u + 118u)) {
        m->ds[XEEN_DS_FALL_SCENE] = 0;
        return fall_return(g);
    }
    m->ds[XEEN_DS_FALL_SCENE] = 0;
    g->fall_stage = FALL_STAGE_FIRST_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
}
/* A fall table row's destination: the party at the row's cell, its facing unless FFh, and the fall scene armed; the
 * caller reloads the row's map. Original: 175E0..175F7 (+0 map, +1 x, +2 y, +3 facing; 8C85 = 1). */
static void fall_place(uint8_t *ds, const uint8_t *row)
{
    ds[XEEN_DS_PARTY_X] = row[1];
    ds[XEEN_DS_PARTY_Y] = row[2];
    if (row[3] != 0xffu) {
        ds[XEEN_DS_PARTY_DIRECTION] = row[3];
    }
    ds[XEEN_DS_FALL_SCENE] = 1;
}
/* Generated by xeen_dat_evidence/phase86/fall_tables.py --c from the
 * original 174C9 jump table (CS 170D:0592, map - 19h for 0..68h; the
 * last row is the 175BD default): map, x, y, facing (FFh kept), 96E2 cleared. */
static const uint8_t fall_darkside_table[106][5] = {
        {89, 2, 14, 0xff, 0},  {109, 13, 14, 0xff, 0}, {112, 13, 3, 0xff, 0}, {92, 2, 3, 0xff, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {14, 10, 2, 0xff, 0},  {14, 10, 2, 0xff, 0},   {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {4, 5, 14, 0xff, 0},   {4, 5, 14, 0xff, 0},    {4, 5, 14, 0xff, 0},   {29, 25, 21, 0, 0},
        {21, 9, 11, 0xff, 0},  {21, 9, 11, 0xff, 0},   {21, 9, 11, 0xff, 0},  {29, 25, 21, 0, 0},
        {1, 10, 4, 0xff, 0},   {1, 10, 4, 0xff, 0},    {1, 10, 4, 0xff, 0},   {29, 25, 21, 0, 0},
        {26, 12, 10, 0xff, 0}, {26, 12, 10, 0xff, 0},  {26, 12, 10, 0xff, 0}, {26, 12, 10, 0xff, 0},
        {3, 4, 9, 0xff, 0},    {3, 4, 9, 0xff, 0},     {3, 4, 9, 0xff, 0},    {3, 4, 9, 0xff, 0},
        {16, 2, 7, 0xff, 0},   {16, 2, 7, 0xff, 0},    {16, 2, 7, 0xff, 0},   {16, 2, 7, 0xff, 0},
        {23, 10, 9, 0xff, 0},  {23, 10, 9, 0xff, 0},   {23, 10, 9, 0xff, 0},  {23, 10, 9, 0xff, 0},
        {13, 2, 10, 0xff, 0},  {13, 2, 10, 0xff, 0},   {13, 2, 10, 0xff, 0},  {13, 2, 10, 0xff, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},
        {29, 25, 21, 0, 0},    {29, 25, 21, 0, 0},     {29, 25, 21, 0, 0},    {8, 11, 15, 0, 1},
        {8, 11, 15, 0, 1},     {29, 25, 21, 0, 0},
};
/* Generated by xeen_dat_evidence/phase115/fall_tables.py --c from the
 * original 173F7 jump table (CS 170D:0664, map - 19h for 0..3Ch; the last
 * row is the 1749E default): map, x, y, facing (FFh kept). */
static const uint8_t fall_clouds_table[62][4] = {
        {24, 11, 9, 0xff}, {12, 6, 15, 0xff}, {15, 4, 12, 0xff}, {23, 12, 10, 0},
        {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {23, 12, 10, 0},   {24, 11, 9, 0xff}, {24, 11, 9, 0xff},
        {24, 11, 9, 0xff}, {24, 11, 9, 0xff}, {12, 6, 15, 0xff}, {12, 6, 15, 0xff},
        {12, 6, 15, 0xff}, {12, 6, 15, 0xff}, {15, 4, 12, 0xff}, {15, 4, 12, 0xff},
        {15, 4, 12, 0xff}, {15, 4, 12, 0xff}, {23, 12, 10, 0},   {23, 12, 10, 0},
        {14, 8, 3, 2},     {14, 8, 3, 2},     {23, 12, 10, 0},   {23, 12, 10, 0},
        {1, 8, 7, 0},      {1, 8, 7, 0},      {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {12, 11, 13, 2},   {23, 12, 10, 0},   {15, 4, 12, 0xff},
        {15, 4, 12, 0xff}, {15, 4, 12, 0xff}, {23, 12, 10, 0},   {23, 12, 10, 0},
        {23, 12, 10, 0},   {5, 12, 7, 0},     {5, 12, 7, 0},     {5, 12, 7, 0},
        {6, 12, 3, 0},     {23, 12, 10, 0},
};
/* The tables' default rows (their last): maps past the table land there. */
#define FALL_CLOUDS_DEFAULT   (sizeof fall_clouds_table / sizeof fall_clouds_table[0] - 1u)
#define FALL_DARKSIDE_DEFAULT (sizeof fall_darkside_table / sizeof fall_darkside_table[0] - 1u)
/*
 * After the fall's scene: the first pass shows the second scene (the fall scene byte 2); the second lands the party
 * and the fall returns (fall_return). An event fall (SURFACE_FALL; on Darkside only with a record map) lands at the
 * record's map, x and y; a step's fall by the map fallen from (below). Stops (17602) when the map fallen to does not
 * load.
 */
xeen_status fall_after_scene(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    unsigned map = ds[XEEN_DS_PARTY_MAP];
    if (g->fall_stage == FALL_STAGE_FIRST_SCENE) {
        ds[XEEN_DS_FALL_SCENE] = 2;
        g->fall_stage = FALL_STAGE_SECOND_SCENE;
        return scene_wait(g, 0, XEEN_TIMER_SCENE_MARK);
    }
    g->fall_stage = FALL_STAGE_NONE;
    g->scene_argument = 1;
    if (g->fall_caller == SURFACE_FALL) {
        /* The event fall lands at the record's map, x and y, the facing kept (on Darkside a record's map 0 takes the
         * step's rule below). Original: 175D3 (SI = 1): ED99/ED97/ED98 = 8C84/8C83/8C82; 175F7: 8C85 = 1, 19E82. */
        if (!ds[XEEN_DS_WORLD] || ds[XEEN_DS_FALL_MAP]) {
            ds[XEEN_DS_PARTY_X] = ds[XEEN_DS_FALL_X];
            ds[XEEN_DS_PARTY_Y] = ds[XEEN_DS_FALL_Y];
            ds[XEEN_DS_FALL_SCENE] = 1;
            if (map_reload(g, ds[XEEN_DS_FALL_MAP]) != XR_OK) {
                return game_stop(g, FALL_STOP_TRAVEL);
            }
            g->scene_selected_group = 0;
            return fall_return(g);
        }
    }
    /* Where a step's fall lands, by the map fallen from: on Clouds a table row per map from 25 (the destination map,
     * x, y and a facing, or the facing kept; maps past the table: map 23 at (12, 10) facing north), e.g. map 26 to
     * map 12 (6, 15). On Darkside the sky level over the outdoor maps (maps 89..112) falls to the map 88 numbers
     * lower, with x, y and the facing kept; its other maps take their own table (maps past it: map 29 at (25, 21)
     * facing north),
     * e.g. the Fire plane (map 25) to map 89 (2, 14); the rows of maps 127 and 128 also clear the map side and land
     * on Clouds' map 8 (11, 15) facing north, a world change.
     * Original: 173E4 (SI = 0); Clouds 173F7, the table CS:0664 (17409: map - 19h, unsigned, up to 3Ch; else 1749E);
     * Darkside 174B0 (maps 59h..70h, map - 58h) and the table CS:0592 (174C9: up to 68h; else 175BD); 175A7 clears
     * 96E2, 175CC; then 175F7. */
    if (!ds[XEEN_DS_WORLD]) {
        unsigned k = (uint16_t)(map - 25u);
        const uint8_t *row = fall_clouds_table[k < FALL_CLOUDS_DEFAULT ? k : FALL_CLOUDS_DEFAULT];
        fall_place(ds, row);
        if (map_reload(g, row[0]) != XR_OK) {
            return game_stop(g, FALL_STOP_TRAVEL);
        }
        g->scene_selected_group = 0;
        return fall_return(g);
    }
    if (map <= 88u || map >= 113u) {
        unsigned k = (uint16_t)(map - 25u);
        const uint8_t *row = fall_darkside_table[k < FALL_DARKSIDE_DEFAULT ? k : FALL_DARKSIDE_DEFAULT];
        if (row[4]) {
            ds[XEEN_DS_MAP_SIDE] = 0;
        }
        fall_place(ds, row);
        if (map_reload(g, row[0]) != XR_OK) {
            return game_stop(g, FALL_STOP_TRAVEL);
        }
        g->scene_selected_group = 0;
        return fall_return(g);
    }
    ds[XEEN_DS_FALL_SCENE] = 1;
    /* 19E82(ED99): ED99 is written first in the original; 30F34 saves the
     * loaded maze (C0CC), and map_reload writes ED99 after that save. */
    if (map_reload(g, (uint16_t)(map - 88u)) != XR_OK) {
        return game_stop(g, FALL_STOP_TRAVEL);
    }
    g->scene_selected_group = 0;
    return fall_return(g);
}
/* The 3D view the fall scrolls (window 11: 216 x 132 at (8, 8)), and its offset in the 320-wide page. */
#define FALL_VIEW_W 216u
#define FALL_VIEW_H 132u
#define FALL_VIEW_AT(row) ((8u + (row)) * XEEN_PRESENT_WIDTH + 8u)
/* One frame of the fall's scroll: the page before the fall's view (216 x 132 at 8, 8) moved up by ROWS, the new
 * view's first rows below it; then the border (with its animation counters) and the view shown.
 * Original: 502B3(rows) (501D4 packs the rows), 26306, 50BF6(3). */
static xeen_status fall_frame(xeen_game *g, const uint8_t *old_view, const uint8_t *new_view,
                              unsigned rows)
{
    uint8_t *p = g->presentation->framebuffer;
    xeen_border_ops ops;
    xeen_status status;
    size_t i;
    unsigned y;
    for (y = 0; y < FALL_VIEW_H; ++y) {
        memcpy(p + FALL_VIEW_AT(y),
               y + rows < FALL_VIEW_H ? old_view + (size_t)(y + rows) * FALL_VIEW_W
                                      : new_view + (size_t)(y + rows - FALL_VIEW_H) * FALL_VIEW_W,
               FALL_VIEW_W);
    }
    status = xeen_interface_border(&g->movement, &ops);
    for (i = 0; status == XEEN_OK && i < ops.count; i++) {
        const xeen_sprite *sp = xeen_sprite_get(&g->sprites, ops.draws[i].handle);
        if (!sp || ops.draws[i].frame >= sp->frames) {
            status = XEEN_BAD_INPUT;
        }
    }
    if (status == XEEN_OK) {
        status = xeen_presentation_draw_sprites(g->presentation, ops.draws, ops.count);
    }
    if (status == XEEN_OK && ops.has_text) {
        status = xeen_presentation_text(g->presentation, ops.text_window, ops.text);
        interface_text_cursor(g, ops.text);
    }
    if (status == XEEN_OK && ops.has_frame) {
        status = xeen_presentation_frame(g->presentation, ops.frame_window);
    }
    if (status == XEEN_OK) {
        status = xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW_FRAME);
    }
    ++g->fall_frames;
    return status;
}
/* The fall's animation: the members' faces drawn in their fall frame and the bar shown, the scream (the voice
 * loaded for the sink; its return not kept) and the fall sound; the view scrolls from the page before the fall to the
 * new one in growing steps (0, 3, 7, 12, ... below 133 rows, then the bounce 132, 127, 132, 129, 132), the "unnh"
 * voice and the landing sound between; the screen shake is the presentation's. Then the fall scene flag is cleared
 * and the party bar redrawn.
 * Original: 1BF78..1C0DB (8C85 = 1): 18975(scream.voc), 187E9; 501D4(8C90); face frame 4 at (17B2[i], 96h),
 * 50BF6(21h); 1880E(0Bh); 502B3(SI) for SI = 0, 3, 7, 12, ... below 85h, then 84h; unnh.voc, 1880E(1Fh); 7Fh, 84h,
 * 81h, 84h; 50C64(0Ah); 8C85 = 0; 2ABB8(1). */
xeen_status fall_animation(xeen_game *g)
{
    /* the view before and after the fall: 28 KiB each, static (too large for the stack; the fall is not re-entered) */
    static uint8_t old_view[FALL_VIEW_H * FALL_VIEW_W], new_view[FALL_VIEW_H * FALL_VIEW_W];
    static const unsigned bounce[5] = {0x84, 0x7f, 0x84, 0x81, 0x84};
    uint8_t *ds = g->movement.ds, *p = g->presentation->framebuffer;
    xeen_native_audio *audio = game_audio(g);
    xeen_status status = XEEN_OK;
    unsigned i, rows, step;
    for (i = 0; i < FALL_VIEW_H; ++i) {
        memcpy(old_view + i * FALL_VIEW_W, g->fall_screen + FALL_VIEW_AT(i), FALL_VIEW_W);
        memcpy(new_view + i * FALL_VIEW_W, p + FALL_VIEW_AT(i), FALL_VIEW_W);
    }
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT] && i < 6u; ++i) {
        /* the member's face in its fall frame (4) on the party bar's face row (96h) */
        xeen_sprite_draw d = {xeen_read16(&g->movement, XEEN_DS_MEMBER_FACE_SPRITE(i)),
                              4,
                              ds[XEEN_DS_PORTRAIT_X + i],
                              0x96,
                              0,
                              0,
                              0};
        (void)xeen_presentation_draw_sprites(g->presentation, &d, 1);
    }
    sink_named(g, XEEN_DS_FILE_BUFFER_SEGMENT, ds_text(g, XEEN_TEXT_FALL_SCREAM_VOICE)); /* Sink */
    status = xeen_presentation_update_window(g->presentation, XEEN_WINDOW_PARTY_BAR);
    if (status == XEEN_OK) {
        status = xeen_native_audio_effect(audio, &g->movement, 0x0b); /* the fall sound */
    }
    for (rows = 0, step = 2; status == XEEN_OK && rows < 133u; rows += ++step) {
        status = fall_frame(g, old_view, new_view, rows);
    }
    if (status == XEEN_OK) {
        status = fall_frame(g, old_view, new_view, bounce[0]);
    }
    if (status == XEEN_OK) {
        sink_named(g, XEEN_DS_FILE_BUFFER_SEGMENT, ds_text(g, XEEN_TEXT_FALL_LANDING_VOICE)); /* Sink */
    }
    if (status == XEEN_OK) {
        status = xeen_native_audio_effect(audio, &g->movement, 0x1f); /* the landing */
    }
    for (i = 1; status == XEEN_OK && i < 5u; ++i) {
        status = fall_frame(g, old_view, new_view, bounce[i]);
    }
    if (status != XEEN_OK) {
        return status;
    }
    ds[XEEN_DS_FALL_SCENE] = 0;
    return xeen_world_entry_party_bar(&g->movement, g->presentation, 1);
}
