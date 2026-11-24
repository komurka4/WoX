/*
 * The travel and time spells (Phase 246, from game_cast.c): the handlers cast_dispatch calls for
 * Etherealize, Teleport and its amount entry, Time Distortion, Town Portal and its amount entry, and Lloyd's
 * Beacon with its scene, keys and end; each refuses with 39641's "Spell Failed!" when its C0D6 bit is set
 * (handler listings: phase90/handlers.asm, CS base 39640).
 */
#include "game_cast.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a travel spell cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). A sound stop means the host's audio refused the spell's effect.
 */
enum {
    ETHEREALIZE_STOP_LOOKUP = 0x3b318,        /* the cell ahead */
    ETHEREALIZE_STOP_SOUND = 0x3b341,
    TELEPORT_STOP_PROMPT = 0x3acbf,           /* the prompt with the facing's name does not fit */
    TELEPORT_STOP_LOOKUP = 0x3ad42,           /* the destination cell */
    TELEPORT_STOP_SOUND = 0x3ae97,
    TIME_DISTORTION_STOP_SOUND = 0x3ac56,
    TOWN_PORTAL_STOP_TOWN_NAME = 0x3b01c,     /* a town's name from its map file */
    TOWN_PORTAL_STOP_PROMPT = 0x3b056,        /* the list of the five towns does not fit */
    TOWN_PORTAL_STOP_SOUND = 0x3b0ae,
    TOWN_PORTAL_STOP_TRAVEL = 0x3b0d8,        /* the town's map did not load */
    TOWN_PORTAL_STOP_SPRITES = 0x3b0e2,       /* cast.icn again */
    LLOYD_STOP_SPRITES = 0x3a5f7,             /* lloyds.icn */
    LLOYD_STOP_MAP_NAME = 0x3a6af,            /* the beacon's map name */
    LLOYD_STOP_TEXT = 0x3a728,                /* the beacon window's text does not fit */
    LLOYD_STOP_SET_SOUND = 0x3a8a5,           /* setting the beacon */
    LLOYD_STOP_RETURN_SOUND = 0x3a827,        /* returning to it */
    LLOYD_STOP_TRAVEL = 0x3a86f,              /* the beacon's map did not load */
    LLOYD_STOP_RESTORE_SPRITES = 0x3a879      /* cast.icn and lloyds.icn again */
};
/* ------------------------- the travel and time spells
 * Handler listings: phase90/handlers.asm (CS base 39640). Each refuses
 * with 39641's "Spell Failed!" (the cost kept) when its C0D6 bit is set:
 * Etherealize 40h, Town Portal 100h, Time Distortion 400h, Lloyd's Beacon
 * 800h, Teleport 1000h. */
/* A travel spell's map name: the file FILE (both worlds' map names are in DARK.CC, so Darkside's archive is selected
 * for the load and WORLD's afterwards) into OUT, which must hold it whole with its NUL (every original file fits in
 * 32 bytes). Returns 0 when it is missing or does not fit. Original: 14BBA into F5E8, then 18975 (8582), 191DE. */
static int travel_name(xeen_game *g, const char *file, uint8_t world, char out[32])
{
    xeen_movement *m = &g->movement;
    size_t n = 0;
    uint8_t *b;
    ds_put_text(g, XEEN_DS_FILE_NAME, file);
    xeen_select_archive(m, 1);
    b = menu_load(g, file, &n);
    xeen_select_archive(m, world);
    if (!b) {
        return 0;
    }
    if (n > 32u || !memchr(b, 0, n)) {
        free(b);
        return 0;
    }
    memcpy(out, b, n);
    free(b);
    return 1;
}
/* Etherealize: the party steps to the cell ahead through any wall. Refused (Spell Failed) where the map forbids it
 * and where the cell is outside the maps (8888h: past 31, or a missing link indoors; outdoors a missing link answers
 * 000Fh, which is not refused). Then the transport sound.
 * Original: 3B2D6: the step DS:0655/0715 by the facing (ED96), 1C680(x, y, FFFFh, 0), 39641, 1880E(33h). */
xeen_status cast_etherealize(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned dir = ds[XEEN_DS_PARTY_DIRECTION];
    uint16_t r = 0;
    int8_t dx = (int8_t)ds[XEEN_DS_STEP_X(dir, XEEN_STEP_AHEAD)],
           dy = (int8_t)ds[XEEN_DS_STEP_Y(dir, XEEN_STEP_AHEAD)];
    if (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_ETHEREALIZE) {
        return cast_failed(g);
    }
    if (xeen_maze_lookup(m, (int16_t)(xeen_signed_byte(ds[XEEN_DS_PARTY_X]) + dx),
                         (int16_t)(xeen_signed_byte(ds[XEEN_DS_PARTY_Y]) + dy), 0xffffu, 0,
                         &r) != XEEN_OK) {
        return panel_stop(g, ETHEREALIZE_STOP_LOOKUP);
    }
    if (r == XEEN_CELL_OUTSIDE_MAPS) {
        return cast_failed(g);
    }
    ds[XEEN_DS_PARTY_X] = (uint8_t)(ds[XEEN_DS_PARTY_X] + (uint8_t)dx);
    ds[XEEN_DS_PARTY_Y] = (uint8_t)(ds[XEEN_DS_PARTY_Y] + (uint8_t)dy);
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return panel_stop(g, ETHEREALIZE_STOP_SOUND);
    }
    return cast_handled(g);
}
/* Teleport: refused (Spell Failed) where the map forbids it; else the keys go to a prompt, "How many squares <facing>
 * (1-9)", in the scroll box and a one-digit amount entry whose answer is teleport_amount's.
 * Original: 3AC88: 190ED/1919B, DS:69CD with the 17A2 direction name in window 6, 28ABA(1, C8h, 6). */
xeen_status cast_teleport(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[160];
    if (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_TELEPORT) {
        return cast_failed(g);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(m);
    if (snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_TELEPORT_PROMPT),
                 far_text(g, XEEN_DS_DIRECTION_NAMES, ds[XEEN_DS_PARTY_DIRECTION])) < 0) {
        return panel_stop(g, TELEPORT_STOP_PROMPT);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    message_window(g, out);
    g->amount_window = 6;
    g->amount_width = 0xc8;
    g->amount_max = 1;
    g->amount_owner = AMOUNT_TELEPORT;
    return amount_begin(g);
}
/* The step of N cells along DIRECTION (north +y, east +x, south -y, west -x) as 16-bit deltas; 0, 0 for a facing
 * past west. */
static void teleport_step(unsigned direction, uint16_t n, int16_t *dx, int16_t *dy)
{
    *dx = direction == XEEN_EAST ? (int16_t)n : direction == XEEN_WEST ? (int16_t)-(int16_t)n : 0;
    *dy = direction == XEEN_NORTH ? (int16_t)n : direction == XEEN_SOUTH ? (int16_t)-(int16_t)n : 0;
}
/* Whether Teleport refuses a destination whose maze lookup answered CELL: indoors a cell outside the maps (8888h);
 * outdoors the surface kinds 0 and 8 (on every shipped outdoor map kind 0 is water, and kind 8 deep water on 23
 * maps and water on 53: phase257/maze census). A missing link's unmasked 000Fh is not refused. */
static int teleport_refused(uint16_t cell, int outdoors)
{
    return outdoors ? cell == 0u || cell == 8u : cell == XEEN_CELL_OUTSIDE_MAPS;
}
/*
 * Teleport's answer, N cells (the amount entry's value) along the party's facing. Esc, 0 or nothing ends the
 * spell as it is. Else the destination is looked up (outdoors through the surface mask 0Fh) and the spell fails
 * where teleport_refused says so; otherwise the party moves there (its byte coordinates wrap; an edge is the
 * surface's to commit later), the sign text is cleared and the transport sound plays. A facing past west moves
 * nothing but still clears the sign and plays the sound.
 * Original: 3ACF4 after 28ABA: 18765(6), 18FFF; ED96 by CS:1860; 1C680(x, y, F608 ? 0Fh : FFFFh, 0); 13FE7 (98DA,
 * 96h bytes); 1880E(33h).
 */
xeen_status teleport_amount(xeen_game *g, uint32_t value)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t n = (uint16_t)value, r = 0;
    unsigned dir = ds[XEEN_DS_PARTY_DIRECTION];
    int16_t x = (int16_t)xeen_signed_byte(ds[XEEN_DS_PARTY_X]),
            y = (int16_t)xeen_signed_byte(ds[XEEN_DS_PARTY_Y]);
    int outdoors = xeen_read16(m, XEEN_DS_OUTDOORS) != 0;
    panel_window(g, XEEN_WINDOW_SCROLL, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    if (!n) {
        return cast_handled(g);
    }
    if (dir <= 3u) {
        int16_t dx, dy;
        teleport_step(dir, n, &dx, &dy);
        if (xeen_maze_lookup(m, (int16_t)(x + dx), (int16_t)(y + dy), outdoors ? 0x0fu : 0xffffu, 0, &r) !=
            XEEN_OK) {
            return panel_stop(g, TELEPORT_STOP_LOOKUP);
        }
        if (teleport_refused(r, outdoors)) {
            return cast_failed(g);
        }
        ds[XEEN_DS_PARTY_X] = (uint8_t)(ds[XEEN_DS_PARTY_X] + (uint8_t)dx);
        ds[XEEN_DS_PARTY_Y] = (uint8_t)(ds[XEEN_DS_PARTY_Y] + (uint8_t)dy);
    }
    memset(ds + XEEN_DS_SIGN_TEXT, 0, XEEN_SIGN_TEXT_BYTES);
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return panel_stop(g, TELEPORT_STOP_SOUND);
    }
    return cast_handled(g);
}
/* Time Distortion (also in combat): refused (Spell Failed) where the map forbids it; else the party goes back to the
 * map's run position, facing kept, with the transport sound, then one scene pass (no frame mark before it).
 * Original: 3AC41 (not one of 39AF6's combat refusals): 2125F (C0FB, C104), 1880E(33h), 1B503(1) at its 4EA35. */
xeen_status cast_time_distortion(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_TIME_DISTORTION) {
        return cast_failed(g);
    }
    ds[XEEN_DS_PARTY_X] = ds[XEEN_DS_MAP_FIELD(RUN_X)];
    ds[XEEN_DS_PARTY_Y] = ds[XEEN_DS_MAP_FIELD(RUN_Y)];
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return panel_stop(g, TIME_DISTORTION_STOP_SOUND);
    }
    g->panel_stage = TRAVEL_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
}
/* Town Portal: refused (Spell Failed) where the map forbids it. Else the keys go to the list (the UI mode kept and set
 * to none), the five towns of the portal's side are named from their map texts ("xeen0028.txt" and so on, both
 * worlds' in DARK.CC), the list is shown in window 20 and a one-digit amount entry asks for the town (answered by
 * portal_amount). Stops when a town's name or the list cannot be formed.
 * Original: 3AF94: 190ED/1919B, 9A1C, [BP-4] = DS 94; 191DE(1), "%s%04u.txt" (DS:69F3) of 67C4[FB00 x 5 + n],
 * 191DE([BP-4]); the DS:69FE list in window 14h; 28ABA(1, A0h, 14h). */
xeen_status cast_portal(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char names[5][32], file[16], out[400];
    unsigned i;
    uint8_t side = ds[XEEN_DS_TOWN_PORTAL_SIDE], world = ds[XEEN_DS_WORLD];
    if (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_TOWN_PORTAL) {
        return cast_failed(g);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(m);
    g->travel_mode = ds[XEEN_DS_UI_MODE];
    ds[XEEN_DS_UI_MODE] = 0xff;
    for (i = 0; i < 5u; ++i) {
        xeen_select_archive(m, 1);
        (void)snprintf(
                file, sizeof file, ds_text(g, XEEN_TEXT_TOWN_PORTAL_TEXTS_FILE),
                ds_text(g, side ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX),
                (unsigned)(int)(int8_t)ds[(uint16_t)(XEEN_DS_TOWN_PORTAL_MAPS + (unsigned)((int)(int8_t)side * 5 + (int)i))]);
        if (!travel_name(g, file, 1, names[i])) {
            xeen_select_archive(m, world);
            return panel_stop(g, TOWN_PORTAL_STOP_TOWN_NAME);
        }
    }
    xeen_select_archive(m, world);
    if (snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_TOWN_PORTAL_MENU), names[0], names[1], names[2], names[3],
                 names[4]) < 0) {
        return panel_stop(g, TOWN_PORTAL_STOP_PROMPT);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    panel_window(g, XEEN_WINDOW_TOWN_PORTAL, 1);
    menu_window_text(g, XEEN_WINDOW_TOWN_PORTAL, WINDOW_FRAMED, out);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_TOWN_PORTAL);
    g->amount_window = 0x14;
    g->amount_width = 0xa0;
    g->amount_max = 1;
    g->amount_owner = AMOUNT_TOWN_PORTAL;
    return amount_begin(g);
}
/*
 * Town Portal's answer, town N (1..5) of the listed side. Above 5 (as a signed word) the list asks again; 0 ends the
 * spell as it is. Else the transport sound plays, the cast sprites are let go, the portal's side becomes the map side
 * and the world, and the map loads: the side's byte N of the table at 67C3, read 10 bytes a side. On Clouds those
 * are the listed towns themselves; on Darkside the read runs past the side's five names, to the outdoor areas
 * outside its towns (maps 4, 13h, 16h, 6, 62h) - an original quirk, kept. The cast sprites come back, and the party
 * stands at the new map's run position (Clouds) or at the town's landing (Darkside, darkside_landing).
 * Original: 3B08B after 28ABA: 3B07C (ask again); 18765(14h), 18FFF, 9A1C; 1880E(33h), 190CF (cast.icn), 96E2 and
 * DS 94 = FB00, 96E1 = 1, 19E82(67C3[FB00 x 10 + n]), 190A3(cast.icn); Clouds 2125F, Darkside CS:1B1B.
 */
xeen_status portal_amount(xeen_game *g, uint32_t value)
{
    /* Where Town Portal sets the party down on Darkside, town by town: its x, y and facing. Original: CS:1B1B. */
    static const struct {
        uint8_t x, y, facing;
    } darkside_landing[5] = {{14, 11, 2}, {5, 12, 3}, {2, 15, 1}, {8, 11, 0}, {2, 6, 0}};
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t n = (uint16_t)value;
    uint8_t side;
    if ((int16_t)n > 5) {
        return amount_begin(g);
    }
    panel_window(g, XEEN_WINDOW_TOWN_PORTAL, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_UI_MODE] = g->travel_mode;
    if (!n) {
        return cast_handled(g);
    }
    /* From combat's Use (449B5) the same code runs: 3B0B4's 190CF
     * pops Use's 44F92 push (4AA3E of the items buffer 7: every later handle
     * freed), 19E82 loads the town into handles 7.. inside the engaged combat
     * (9A1C 2 again at 3B0A2), 190A3(cast.icn); 449BA then pops cast.icn and
     * Use's 2ABB8 draws through the driver's tests (driver_check). */
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return panel_stop(g, TOWN_PORTAL_STOP_SOUND);
    }
    handle_pop(g);
    side = ds[XEEN_DS_TOWN_PORTAL_SIDE];
    ds[XEEN_DS_MAP_SIDE] = side;
    ds[XEEN_DS_WORLD] = side;
    ds[XEEN_DS_MAP_RELOADED] = 1;
    if (map_reload(g, ds[(uint16_t)(XEEN_DS_TOWN_PORTAL_LOAD_BASE + (unsigned)((int)(int8_t)side * 10 + (int)n))]) != XR_OK) {
        return panel_stop(g, TOWN_PORTAL_STOP_TRAVEL);
    }
    m = &g->movement;
    ds = m->ds;
    g->scene_selected_group = 0;
    if (!handle_push_or_undo(g, XEEN_TEXT_TRAVEL_CAST_SPRITES)) {
        return panel_stop(g, TOWN_PORTAL_STOP_SPRITES);
    }
    if (!ds[XEEN_DS_WORLD]) {
        ds[XEEN_DS_PARTY_X] = ds[XEEN_DS_MAP_FIELD(RUN_X)];
        ds[XEEN_DS_PARTY_Y] = ds[XEEN_DS_MAP_FIELD(RUN_Y)];
    } else if (n >= 1u && n <= 5u) {
        ds[XEEN_DS_PARTY_X] = darkside_landing[n - 1u].x;
        ds[XEEN_DS_PARTY_Y] = darkside_landing[n - 1u].y;
        ds[XEEN_DS_PARTY_DIRECTION] = darkside_landing[n - 1u].facing;
    }
    return cast_handled(g);
}
/* Enters Lloyd's Beacon's scene wait. Its prompt: the caster's beacon (map, x, y and side; one never set becomes the
 * current world's default: Darkside map 1Dh at (25, 21), Clouds 1Ch at (18, 4)), the beacon map's name looked up
 * in Darkside's archive, and the set / return buttons (Escape's button key becomes R) with the beacon's text in the
 * Cast dialog's window; then the key loop.
 * Original: 3A5DA: 190ED, 190A3(lloyds.icn); +A0 map, +A1 x, +A2 y, +136h side; [BP-2] = DS 94; 191DE(1), DS:6925,
 * 191DE([BP-2]); the buttons 1A3C.. (1A48); DS:6932 in window 0Ah (already open: 1871B does nothing; 4F6F8 frames
 * it), 50BF6(0Ah). */
xeen_status lloyd_scene(xeen_game *g)
{
    g->panel_stage = LLOYD_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/* A beacon never set (its map 0) becomes the current WORLD's default: Darkside map 1Dh at (25, 21), side 1; Clouds
 * map 1Ch at (18, 4), side 0. Original: 3A603 (+A0 tested), 3A615..3A645 (+A0 map, +A1 x, +A2 y, +136h side). */
static void beacon_default(uint8_t *rec, uint8_t world)
{
    if (rec[XEEN_CHARACTER_BEACON_MAP]) {
        return;
    }
    if (world) {
        rec[XEEN_CHARACTER_BEACON_MAP] = 0x1d;
        rec[XEEN_CHARACTER_BEACON_X] = 0x19;
        rec[XEEN_CHARACTER_BEACON_Y] = 0x15;
        rec[XEEN_CHARACTER_BEACON_SIDE] = 1;
    } else {
        rec[XEEN_CHARACTER_BEACON_MAP] = 0x1c;
        rec[XEEN_CHARACTER_BEACON_X] = 0x12;
        rec[XEEN_CHARACTER_BEACON_Y] = 4;
        rec[XEEN_CHARACTER_BEACON_SIDE] = 0;
    }
}
/** Opens Lloyd's Beacon (3A5DA): shows the character's beacon and its set/return choice. */
xeen_status cast_lloyd(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *rec;
    char name[32], file[16], out[300];
    if (xeen_read16(m, XEEN_DS_MAP_RESTRICTIONS) & XEEN_MAP_NO_LLOYDS_BEACON) {
        return cast_failed(g);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push_or_undo(g, XEEN_TEXT_BEACON_SPRITES)) {
        return panel_stop(g, LLOYD_STOP_SPRITES);
    }
    rec = ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER);
    beacon_default(rec, ds[XEEN_DS_WORLD]);
    g->travel_world = ds[XEEN_DS_WORLD];
    (void)snprintf(file, sizeof file, ds_text(g, XEEN_TEXT_BEACON_TEXTS_FILE),
                   ds_text(g, rec[XEEN_CHARACTER_BEACON_SIDE] ? XEEN_TEXT_DARKSIDE_PREFIX : XEEN_TEXT_CLOUDS_PREFIX),
                   rec[XEEN_CHARACTER_BEACON_MAP] > 99u ? 'x' : '0', (unsigned)rec[XEEN_CHARACTER_BEACON_MAP]);
    if (!travel_name(g, file, g->travel_world, name)) {
        return panel_stop(g, LLOYD_STOP_MAP_NAME);
    }
    button_tables(m, XEEN_BUTTONS_ESC_T);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ESC_T, 0)] = 'R';
    if (snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_BEACON_MENU), name, (int)rec[XEEN_CHARACTER_BEACON_X],
                 (int)rec[XEEN_CHARACTER_BEACON_Y]) < 0) {
        return panel_stop(g, LLOYD_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, strlen(out) + 1u);
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_FRAMED, out);
    location_buttons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
    return lloyd_scene(g);
}
/* Lloyd's Beacon's end: the travel's world archive selected back, Escape back as the first button's key, the beacon's
 * window popped (with its sprites and button set), then the Cast handler's end.
 * Original: 3A8CF: 191DE([BP-2]), 1A48, 18765(0Ah) (the Cast dialog's own close finds it closed), 190CF
 * (lloyds.icn), 18FFF; the RETF. */
xeen_status lloyd_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    g->lloyd_tail = 0;
    xeen_select_archive(m, g->travel_world);
    ds[XEEN_BUTTON_KEY(XEEN_BUTTONS_ESC_T, 0)] = 0x1b;
    window_pop(g, 10);
    return cast_handled(g);
}
/* S / T: the beacon becomes the party's map, x and y, on the travel's side (the world the dialog opened in).
 * Original: 3A8A3 after its sound (ED99, ED97, ED98, [BP-2]). */
static void beacon_set(xeen_game *g, uint8_t *rec)
{
    const uint8_t *ds = g->movement.ds;
    rec[XEEN_CHARACTER_BEACON_MAP] = ds[XEEN_DS_PARTY_MAP];
    rec[XEEN_CHARACTER_BEACON_X] = ds[XEEN_DS_PARTY_X];
    rec[XEEN_CHARACTER_BEACON_Y] = ds[XEEN_DS_PARTY_Y];
    rec[XEEN_CHARACTER_BEACON_SIDE] = g->travel_world;
}
/* R's refusal: on Clouds, a beacon in Xeen's castle (maps 4Bh..4Eh) once the endgame is reached (EFE8).
 * Original: 3A7EA (DS 94), 3A7F7..3A80F (+A0 against 4Bh, 4Ch, 4Dh, 4Eh), 3A817 (EFE8). */
static int beacon_in_castle_after_endgame(xeen_movement *m, const uint8_t *rec)
{
    return !m->ds[XEEN_DS_WORLD] && rec[XEEN_CHARACTER_BEACON_MAP] >= 75u && rec[XEEN_CHARACTER_BEACON_MAP] <= 78u &&
           xeen_read16(m, XEEN_DS_ENDGAME_REACHED);
}
/* A key at Lloyd's Beacon: Escape ends it; S or T sets the beacon to the party's place and side (the sound); R
 * returns to it: on Clouds a beacon in Xeen's castle (maps 4Bh..4Eh) after the endgame refuses ("Spell Failed!");
 * else the sound, the beacon's side selected, its map loaded when another map or side (the party's facing kept),
 * and the beacon's x and y. Any other key polls again.
 * Original: 3A7B2 (CS:12B3): R 3A7EA (EFE8; 39641; 1880E(33h), 190CF twice, [BP-2] = 96E2, 96E1 = 1, 19E82(map),
 * 190A3 twice); S / T 3A8A3 (1880E(14h); ED99, ED97, ED98, [BP-2]). */
xeen_status lloyd_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *rec = ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER);
    if (key == XEEN_KEY_ESCAPE) {
        return lloyd_end(g);
    }
    if (key == 'S' || key == 'T') {
        if (sound_effect(g, 20u) != XEEN_OK) {
            return panel_stop(g, LLOYD_STOP_SET_SOUND);
        }
        beacon_set(g, rec);
        return lloyd_end(g);
    }
    if (key != 'R') {
        return lloyd_scene(g);
    }
    if (beacon_in_castle_after_endgame(m, rec)) {
        g->lloyd_tail = 1;
        return cast_failed(g);
    }
    if (sound_effect(g, XEEN_CUE_TRANSPORTED) != XEEN_OK) {
        return panel_stop(g, LLOYD_STOP_RETURN_SOUND);
    }
    handle_pop(g);
    handle_pop(g);
    g->travel_world = rec[XEEN_CHARACTER_BEACON_SIDE];
    ds[XEEN_DS_MAP_SIDE] = rec[XEEN_CHARACTER_BEACON_SIDE];
    if (rec[XEEN_CHARACTER_BEACON_MAP] != ds[XEEN_DS_PARTY_MAP] ||
        rec[XEEN_CHARACTER_BEACON_SIDE] != ds[XEEN_DS_WORLD]) {
        uint8_t map = rec[XEEN_CHARACTER_BEACON_MAP];
        ds[XEEN_DS_MAP_RELOADED] = 1;
        if (map_reload(g, map) != XR_OK) {
            return panel_stop(g, LLOYD_STOP_TRAVEL);
        }
        m = &g->movement;
        ds = m->ds;
        rec = ds + xeen_read16(m, XEEN_DS_ACTING_CHARACTER);
        g->scene_selected_group = 0;
    }
    if (!handle_push_or_undo(g, XEEN_TEXT_TRAVEL_CAST_SPRITES) || !handle_push_or_undo(g, XEEN_TEXT_BEACON_SPRITES)) {
        return panel_stop(g, LLOYD_STOP_RESTORE_SPRITES);
    }
    ds[XEEN_DS_PARTY_X] = rec[XEEN_CHARACTER_BEACON_X];
    ds[XEEN_DS_PARTY_Y] = rec[XEEN_CHARACTER_BEACON_Y];
    return lloyd_end(g);
}
