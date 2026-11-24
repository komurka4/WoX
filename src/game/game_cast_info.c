/*
 * The monster-information spells (Phase 246, from game_cast.c): Detect Monster and Identify Monster, their
 * scenes, windows and ends (handler listings: phase91/handlers.asm); the handlers cast_dispatch calls.
 */
#include "game_cast.h"
#include "xeen/monster_record.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The bytes of one line of Identify Monster's table (the original's three stack buffers). */
#define IDENTIFY_LINE_BYTES 50
/*
 * Where native play stops when a monster-information spell cannot go on; each value is the original address of the
 * step (tools/xeen_replay/stops.json). The record, name and table stops guard reads the original makes without a
 * bounds check.
 */
enum {
    DETECT_STOP_SPRITES = 0x3a9e8,          /* the map's sprites did not load */
    DETECT_STOP_SOUND = 0x3ab2e,            /* the spell's sound effect failed */
    IDENTIFY_STOP_MONSTER_RECORD = 0x3a2c5, /* a monster's type outside the monster database */
    IDENTIFY_STOP_LINE = 0x3a396,           /* a line the original would form past its buffers (a name past the
                                             * database, or a line reaching the frame's saved registers) */
    IDENTIFY_STOP_SPECIAL_NAME = 0x3a2e8,   /* a special attack past the 24 names (DS:27BE) */
    IDENTIFY_STOP_SOUND = 0x3a3a9,          /* the spell's sound effect failed */
    IDENTIFY_STOP_TABLE = 0x3a3c7           /* the table does not fit the text buffer */
};
/* ------------------------- the monster-information spells
 * Neither spell tests a spell-flag bit, draws a random number or charges more than the cast's own payment (39C16);
 * neither is an exploration-only spell (39AF6): both are cast in combat too. */
/* Draws FRAME of sprite HANDLE at (X, Y) in WINDOW, unscaled (the original's sprite draw 4AC92). */
static void info_draw(xeen_game *g, uint16_t handle, uint16_t frame, uint16_t x, uint16_t y,
                      uint16_t window)
{
    xeen_sprite_draw d = {handle, frame, x, y, 0, 0, window};
    if (xeen_presentation_draw_sprites(g->presentation, &d, 1) != XEEN_OK) {
        ++g->exit_skipped;
    }
}
/* Detect Monster's wait: each tick redraws the 3D view under the window until a key ends the spell.
 * Original: 3AB3C (4EA0D and 1B503(1) before each key poll 189E3 at 3AB49). */
xeen_status detect_scene(xeen_game *g)
{
    g->panel_stage = DETECT_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/*
 * Draws the monsters of Detect Monster's cell COL, ROW (the party's cell at 3, 3): every map record standing at the
 * party's coordinate plus the cell's offset draws the cell's count so far (frame 1, 2, then 3 for any further one)
 * at the cell's place in the grid. A record counts when its x and y bytes equal that coordinate (a 16-bit compare of
 * the zero-extended bytes against the sign-extended coordinate): there is no test of HP, condition or visibility,
 * and a killed monster's record lies off the map at (80h, 80h).
 * Original: 3AA43..3AAEF (the count in the cell's byte of the grid [BP-3Ah], capped at 3; 4AC92 after each record).
 */
static void detect_cell(xeen_game *g, uint16_t h, unsigned row, unsigned col)
{
    uint8_t *ds = g->movement.ds;
    uint16_t x = (uint16_t)(xeen_signed_byte(ds[XEEN_DS_PARTY_X]) + (int)col - 3),
             y = (uint16_t)(xeen_signed_byte(ds[XEEN_DS_PARTY_Y]) + 3 - (int)row);
    unsigned count = 0;
    for (unsigned i = 0; i < ds[XEEN_DS_MAP_MONSTER_COUNT]; ++i) {
        uint16_t e = (uint16_t)XEEN_MAP_MONSTER(i);
        if (ds[e + XEEN_MAP_MONSTER_X] != x || ds[(uint16_t)(e + XEEN_MAP_MONSTER_Y)] != y) {
            continue;
        }
        if (count < 3u) {
            ++count;
        }
        info_draw(g, h, (uint16_t)count, (uint16_t)(244u + 9u * col), (uint16_t)(81u + 7u * row),
                  XEEN_WINDOW_DETECT_MONSTERS);
    }
}
/*
 * Detect Monster: a map of the 7 x 7 cells around the party, each showing how many monsters stand there (up to 3:
 * detect_cell, row by row from the north), with the party's facing in the middle.
 * Original: 3A9C1 (the sprites "detectmn.icn" on Darkside, "detctmon.icn" on Clouds, in a handle of its own).
 */
xeen_status cast_detect(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    xeen_presentation_trace("mark spell-begin 15"); /* Evidence */
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(m);
    if (!sprite_load(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_DETECT_SPRITES_DARKSIDE : XEEN_TEXT_DETECT_SPRITES_CLOUDS),
                     &h)) {
        return panel_stop(g, DETECT_STOP_SPRITES);
    }
    g->detect_handle = h;
    panel_window(g, XEEN_WINDOW_DETECT_MONSTERS, 1);
    menu_window_text(g, XEEN_WINDOW_DETECT_MONSTERS, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_DETECT_TITLE));
    info_draw(g, h, 0, 243, 80, XEEN_WINDOW_DETECT_MONSTERS); /* the grid */
    for (unsigned row = 0; row < 7u; ++row) {
        for (unsigned col = 0; col < 7u; ++col) {
            detect_cell(g, h, row, col);
        }
    }
    /* the party's facing arrow at the centre cell */
    info_draw(g, xeen_read16(m, XEEN_DS_GLOBAL_SPRITES), (uint16_t)(ds[XEEN_DS_PARTY_DIRECTION] + 1u), 270, 101,
              XEEN_WINDOW_DETECT_MONSTERS);
    if (sound_effect(g, 20u) != XEEN_OK) {
        return panel_stop(g, DETECT_STOP_SOUND);
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_DETECT_MONSTERS);
    return detect_scene(g);
}
/* Detect Monster's end: the window closes, its sprites (and any loaded after them) are freed and the buttons are
 * popped. Original: 3AB5B. */
xeen_status detect_end(xeen_game *g)
{
    panel_window(g, XEEN_WINDOW_DETECT_MONSTERS, 0);
    (void)xeen_sprite_free_from(&g->sprites, g->detect_handle);
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_presentation_trace("mark spell-end 15"); /* Evidence */
    return cast_handled(g);
}
/* Identify Monster's wait: each tick redraws the 3D view and the window's frame (the view's redraw covers the
 * window's top rows; the text stays) until a key ends the spell. Original: 3A3ED (4EA0D, 1B503(0), 4F6F8(11h),
 * 50BF6(3), the key poll 189E3 at 3A40A). */
xeen_status identify_scene(xeen_game *g)
{
    g->panel_stage = IDENTIFY_SCENE;
    return scene_wait(g, 0, XEEN_TIMER_FRAME_MARK);
}
/* Copies the monster database row ROW's name (the row's leading string, END bytes left in the database) into NAME
 * (CAP >= 1 bytes; the copy stops one byte short of it) and terminates it; returns how many bytes were copied: when
 * that reaches END or the row's byte there is not the terminator, the name ran past the database or did not fit (the
 * caller's stop). The original passes the row itself to sprintf (3A2AD..3A2D9: 50D4:0000 + id * 3Ch). */
static size_t identify_name(const uint8_t *row, size_t end, char *name, size_t cap)
{
    size_t k;
    for (k = 0; k < end && k + 1u < cap && row[k]; ++k) {
        name[k] = (char)row[k];
    }
    name[k] = 0;
    return k;
}
/* The current HP column: "%u", or "%uk" in thousands above 999 (DS 522C / 5228). Original: 2692E(HP, 0, [BP-0Ch]). */
static void identify_hp(xeen_game *g, uint16_t v, char *hp, size_t size)
{
    (void)snprintf(hp, size, ds_text(g, v > 999u ? XEEN_TEXT_NUMBER_THOUSANDS : XEEN_TEXT_NUMBER),
                   (unsigned)(v > 999u ? v / 1000u : v));
}
/*
 * Identify Monster: a table of the monsters in the party's cell (the three nearest attack slots): each line holds
 * the name, the current HP ("%uk" in thousands above 999), the armor class, the attacks and the special attack's
 * name. With all three slots empty the spell fails ("Spell Failed!", the cost kept).
 * Quirk kept: the three 50-byte lines lie next to each other in the original's stack frame, so a line longer than
 * 49 characters runs on into the next one (the overflow survives where that slot is empty), and past the last line
 * into the number buffer; a line that would reach the frame's saved registers stops explicitly.
 * Original: 3A232 (the lines [BP-A2h], [BP-70h], [BP-3Eh] from DS:68C9 by the database row, the table DS:68EB).
 */
xeen_status cast_identify(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    /* three lines, then the number buffer the third line's overflow reaches */
    char lines[3 * IDENTIFY_LINE_BYTES + 12], hp[12], name[64], out[1000];
    unsigned slot;
    int n;
    xeen_presentation_trace("mark spell-begin 34"); /* Evidence */
    if (xeen_view_party_cell_empty(ds)) {
        return cast_failed(g);
    }
    lines[0] = lines[IDENTIFY_LINE_BYTES] = lines[2 * IDENTIFY_LINE_BYTES] = 0;
    /* the screen's buttons stay live behind the window */
    xeen_button_stack_save(ds);
    for (slot = 0; slot < 3u; ++slot) {
        uint8_t at = ds[(uint16_t)(XEEN_DS_VIEW_MONSTERS + slot)];
        uint16_t e = (uint16_t)XEEN_MAP_MONSTER(at), v;
        const uint8_t *row;
        size_t k, end;
        if (at == XEEN_VIEW_NO_MONSTER) {
            continue;
        }
        if ((size_t)ds[(uint16_t)(e + XEEN_MAP_MONSTER_ID)] * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_RECORD_BYTES >
            g->monster_database_size) {
            return panel_stop(g, IDENTIFY_STOP_MONSTER_RECORD);
        }
        row = g->monster_database + (size_t)ds[(uint16_t)(e + XEEN_MAP_MONSTER_ID)] * XEEN_MONSTER_RECORD_BYTES;
        end = g->monster_database_size - (size_t)(row - g->monster_database);
        k = identify_name(row, end, name, sizeof name);
        if (k >= end || row[k]) {
            return panel_stop(g, IDENTIFY_STOP_LINE); /* the name runs past the database */
        }
        if (row[XEEN_MONSTER_SPECIAL] >= XEEN_SPECIAL_NAMES) {
            return panel_stop(g, IDENTIFY_STOP_SPECIAL_NAME);
        }
        v = xeen_read16(m, (uint16_t)(e + XEEN_MAP_MONSTER_HP));
        identify_hp(g, v, hp, sizeof hp);
        n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_IDENTIFY_LINE), name, hp, (unsigned)row[XEEN_MONSTER_ARMOR],
                     (unsigned)row[XEEN_MONSTER_ATTACKS], far_text(g, XEEN_DS_MONSTER_SPECIAL_NAMES, row[XEEN_MONSTER_SPECIAL]));
        /* past the number buffer lies the frame's saved BP: an explicit stop */
        if (n < 0 || IDENTIFY_LINE_BYTES * slot + (size_t)n + 1u > sizeof lines) {
            return panel_stop(g, IDENTIFY_STOP_LINE);
        }
        memcpy(lines + IDENTIFY_LINE_BYTES * slot, out, (size_t)n + 1u);
    }
    if (sound_effect(g, 20u) != XEEN_OK) {
        return panel_stop(g, IDENTIFY_STOP_SOUND);
    }
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_IDENTIFY_TABLE), lines, lines + IDENTIFY_LINE_BYTES,
                 lines + 2 * IDENTIFY_LINE_BYTES);
    if (n < 0 || (size_t)n >= sizeof out) {
        return panel_stop(g, IDENTIFY_STOP_TABLE);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    panel_window(g, XEEN_WINDOW_IDENTIFY_MONSTER, 1);
    menu_window_text(g, XEEN_WINDOW_IDENTIFY_MONSTER, WINDOW_FRAMED, out);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_IDENTIFY_MONSTER);
    return identify_scene(g);
}
/* Identify Monster's end: the window closes and the buttons are popped. Original: 3A423. */
xeen_status identify_end(xeen_game *g)
{
    panel_window(g, XEEN_WINDOW_IDENTIFY_MONSTER, 0);
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_presentation_trace("mark spell-end 34"); /* Evidence */
    return cast_handled(g);
}
