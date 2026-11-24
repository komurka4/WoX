/*
 * The forms an event script opens and their ends: the yes/no and the numeric choice (with the speaking NPC's face
 * form beside them), the window closes and restorations they share, the map's .hed faces of the party's cell, and
 * operation 7's teleport. The event services (game_events.c) and the UI presenter (game_event_ui.c) open them.
 * Original: the .hed copy 1A244, the face form 2F35B, yes/no 2F2FF, choice 2F5B1, teleport 3193B (Phase 248 moved
 * them from game_town.c).
 */
#include "game_event.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when the event's windows or operation 7's teleport cannot go on; each value is the
 * original address of the step (tools/xeen_replay/stops.json).
 */
enum {
    WINDOWS_STOP_RESTORE = 0x186f0,          /* an open window's screen could not be restored */
    WINDOWS_STOP_RESTORE_EVENTS = 0x18701,   /* the event script could not be restored */
    EVENT_TELEPORT_STOP_ROW = 0x31972,       /* the mirror row past the loaded table */
    EVENT_TELEPORT_STOP_SOUND = 0x319e8,
    EVENT_TELEPORT_STOP_TRAVEL = 0x31a33     /* the destination map did not load */
};
/* The party's cell in the map's .hed file: two bytes a cell, 64 a row (x * 2 + y * 64, x and y signed), or NULL
 * when the file is missing or the cell lies outside it. The file is read at the first use after a map load
 * (map_load_commit clears hed_valid), at most sizeof g->hed bytes; operation 36 edits the copy, and the map save does
 * not write it back, so the next load restores the shipped bytes. Original: 1A244 (the copy in DOS memory at
 * 50D4:5F7C), edited by 32E6D; 30F34 does not save it. */
uint8_t *hed_cell(xeen_game *g)
{
    uint8_t *ds = g->movement.ds, *hed;
    char name[16];
    size_t n = 0;
    long at;
    if (!g->hed_valid) {
        (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_MAP_HED_FORMAT),
                       ds[XEEN_DS_PARTY_MAP] <= 99u ? '0' : 'x', (unsigned)ds[XEEN_DS_PARTY_MAP]);
        if (!(hed = menu_extract(g, name, &n))) {
            return NULL;
        }
        if (n > sizeof g->hed) {
            n = sizeof g->hed;
        }
        memcpy(g->hed, hed, n);
        free(hed);
        g->hed_size = n;
        g->hed_valid = 1;
    }
    at = (long)(int8_t)ds[XEEN_DS_PARTY_Y] * 64L + (long)(int8_t)ds[XEEN_DS_PARTY_X] * 2L;
    if (at < 0 || (size_t)at + 1u >= g->hed_size) {
        return NULL;
    }
    return g->hed + at;
}
/* The two Clairvoyance faces' states from the party's .hed cell (left from its first byte, right from its second).
 * Returns 0, changing nothing, without a cell. Original: as 2F2FF, 2F9BA (2FA7F) and 3000D (3037F) set F467/F466. */
int hed_faces(xeen_game *g)
{
    uint8_t *ds = g->movement.ds, *cell = hed_cell(g);
    if (!cell) {
        return 0;
    }
    ds[XEEN_DS_CLAIRVOYANCE_LEFT_STATE] = cell[0];
    ds[XEEN_DS_CLAIRVOYANCE_RIGHT_STATE] = cell[1];
    return 1;
}
/* The speaking NPC's form beside a yes/no or choice (the speech window open), in the talking faces' face-only layout
 * (the control panel's Text off: XEEN_DS_TEXT_OFF set; with text on it does nothing): TEXT, the NPC's name, is
 * written into the speech window, the frame and the animated face are drawn when a face is loaded, then the window's
 * border, and the window is presented. The callers have moved the buttons' row 26 pixels down for this layout. Original: 2F35B (4E38C(28h), the frame 8D58 at (F8h, 0Ch), the face 8D56/8D32 at
 * (8D30, 8D2E), 4F6F8(28h), 50BF6(28h)); without the speech window 2F2FF goes to 2F428 instead. */
void face_form(xeen_game *g, const uint8_t *text)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint8_t *p = g->presentation->framebuffer;
    xeen_sprite_draw draws[2] = {
            {xeen_read16(m, XEEN_DS_ANIMATION_SPRITE(1)), 0, 0xf8, 0x0c, 0, 0, 0},
            {xeen_read16(m, XEEN_DS_ANIMATION_SPRITES), xeen_read16(m, XEEN_DS_ANIMATION_FRAME),
             xeen_read16(m, XEEN_DS_ANIMATION_X), xeen_read16(m, XEEN_DS_ANIMATION_Y), 0, 0, 0}};
    if (!ds[XEEN_DS_TEXT_OFF]) {
        return;
    }
    if (g->text_window.loaded || xeen_text_window_load(&g->text_window, &g->assets) == XR_OK) {
        trace_event_text(0x28, text, 1000);
        page_layout(g, window40);
        (void)xeen_text_layout_run(&g->layout, &g->text_window, p, text, XEEN_TEXT_BUFFER_BYTES, 0);
    }
    if (xeen_read16(m, XEEN_DS_ANIMATION_SPRITES)) {
        (void)xeen_presentation_draw_sprites(g->presentation, draws, 2);
    }
    /* 4F6F8(28h) is the border alone (4F24A's tiles, the interior
     * kept). The native drew 1871B's frame (border and the 4F138 fill) here,
     * after the text and the face: the window was presented empty. */
    (void)xeen_text_border(p, XEEN_PRESENT_PIXELS, window40);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SPEECH);
}
/**
 * Opens an event's yes/no: loads confirm.icn onto the sprite handle stack, draws the icon frame and the two buttons
 * (beside the speaking NPC's face form when the speech window is open: 26 pixels lower with Text off), presents
 * the icon window, installs the yes/no button tables (the enabled table stays the caller's: the one saved by the
 * modal entry) and sets the Clairvoyance faces from the party's .hed cell. Returns 0, with nothing drawn, without a
 * presentation or a .hed cell, or when the sprites cannot be loaded (the handle depth is then taken back).
 * Original: 2F2FF(0) after 190ED (xeen_modal_begin); 190A3(confirm.icn), F458 frame 7 at (E8h, 4Ah), frames 0 and 2
 * at (EBh, 4Bh) and (104h, 4Bh), 50BF6(22h); the buttons 98AC..989C = 1C20..1C2C, 9898 kept, 9896 = 98D4.
 */
int yesno_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *cell;
    uint16_t h = 0;
    if (!g->presentation || !(cell = hed_cell(g))) {
        return 0;
    }
    if (!handle_push(g, ds_text(g, XEEN_TEXT_CONFIRM_SPRITES), &h)) {
        ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        return 0;
    }
    exit_draw(g, xeen_read16(m, XEEN_DS_GLOBAL_SPRITES), 7, 232, 74, 0);
    if (ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        uint8_t row = (uint8_t)(ds[XEEN_DS_TEXT_OFF] * 0x1au + 0x4bu);
        ds[XEEN_DS_YESNO_BUTTON_Y + 1u] = ds[XEEN_DS_YESNO_BUTTON_Y] = row;
        exit_draw(g, h, 0, 235, row, 0);
        exit_draw(g, h, 2, 260, row, 0);
        face_form(g, ds + XEEN_DS_TEXT_BUFFER_2);
    } else {
        exit_draw(g, h, 0, 235, 75, 0);
        exit_draw(g, h, 2, 260, 75, 0);
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ICONS);
    yesno_buttons_install(m);
    /* 190ED only saves the live pointers (the defaults are 1919B's,
     * which 2F2FF(0) does not call): 9898 stays the caller's (an event's
     * dispatcher defaults; the tavern's 1A72 at its food). */
    {
        int l = (int)(int8_t)ds[XEEN_DS_BUTTON_DEPTH];
        xeen_write16(m, XEEN_DS_BUTTON_ENABLED_TABLE,
                     xeen_read16(m, XEEN_DS_BUTTON_TABLE_SLOT(XEEN_DS_BUTTON_ENABLED_STACK, l)));
        xeen_write16(m, XEEN_DS_BUTTON_ENABLED_TABLE + 2u,
                     xeen_read16(m, (uint16_t)(XEEN_DS_BUTTON_TABLE_SLOT(
                                                       XEEN_DS_BUTTON_ENABLED_STACK, l) +
                                               2u)));
    }
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    ds[XEEN_DS_CLAIRVOYANCE_LEFT_STATE] = cell[0];
    ds[XEEN_DS_CLAIRVOYANCE_RIGHT_STATE] = cell[1];
    g->yesno = 1;
    return 1;
}
/* Closes window W when it is open (its flag and its window stack slot) and restores the screen beneath it through the
 * session's window service. Returns 1 when W was not open (W 40 and above never is) or the restore succeeded.
 * Original: 18765(n), the restore 4F57B. */
int window_close_restore(xeen_game *g, unsigned w)
{
    xeen_movement *m = &g->movement;
    uint16_t ax = 0;
    xeen_event_request q = {XEEN_REQUEST_WINDOW_RESTORE, (uint16_t)w, 0, 0, 0};
    if (w >= 40u || !m->ds[XEEN_DS_WINDOW_OPEN + w]) {
        return 1;
    }
    xeen_window_close(m, w);
    return event_service(g, &q, m->ds, sizeof m->ds, &ax) == 1;
}
/* The yes/no buttons' tables (the program data's set at 1C20) into the live button pointers, as 2F466 and 3006E
 * install them: x, y, width, height and keys (the enabled table and the sprites are each caller's). */
void yesno_buttons_install(xeen_movement *m)
{
    menu_far(m, XEEN_DS_BUTTON_X_TABLE, XEEN_DS_YESNO_BUTTON_X);
    menu_far(m, XEEN_DS_BUTTON_Y_TABLE, XEEN_DS_YESNO_BUTTON_Y);
    menu_far(m, XEEN_DS_BUTTON_WIDTH_TABLE, XEEN_DS_YESNO_BUTTON_WIDTH);
    menu_far(m, XEEN_DS_BUTTON_HEIGHT_TABLE, XEEN_DS_YESNO_BUTTON_HEIGHT);
    menu_far(m, XEEN_DS_BUTTON_KEY_TABLE, XEEN_DS_YESNO_BUTTON_KEYS);
}
/* The screen after an event's yes/no or an NPC's confirm: the event text window and window 12 closed with their
 * pixels restored, the exploration icons drawn over the buttons, the speech face's tail and the icon window
 * presented. Presentation only; the callers trace the executor's substitution around it. Original: 28D80 (18765 of
 * 26h and 0Ch, 28D96's 4ADB1(0, DS:2A5C), 28D9E, 50BF6(22h)). */
void restore_after_dialog(xeen_game *g)
{
    xeen_presentation_trace_extra("evclose 38");
    (void)window_close_restore(g, XEEN_WINDOW_EVENT_TEXT);
    xeen_presentation_trace_extra("evclose 12");
    (void)window_close_restore(g, XEEN_WINDOW_BOTTOM);
    /* 28D96: 4ADB1(0, DS:2A5C), the exploration icons over the
     * thumbs (the earlier native updated window 22h only). */
    explore_icons(g);
    speech_face_tail(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ICONS);
}
/* Ends a yes/no (and, through choice_end, a choice): the form's sprites are popped off the handle stack and the screen
 * restored (restore_after_dialog). Outside a location the restoration is traced as the executor's 28D80
 * substitution. The caller has already run xeen_modal_finish (the faces, the button stack, the modes and the button
 * row back). Original: 2F594's 190CF, then 28D80. */
void yesno_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    handle_pop(g);
    /* Evidence: the executor substitutes 28D80 (not after
     * a location's 2F2FF, 9A1C 11h: the harness runs it there; 
     * the block is tagged, as the event-yesno-tail adapter runs it after an
     * event's yes/no too, which the original's npc observer records). */
    if (ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_LOCATION) {
        xeen_presentation_trace_extra("npcsub begin 28d80");
    }
    restore_after_dialog(g);
    if (ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_LOCATION) {
        xeen_presentation_trace_extra("npcsub end");
    }
    g->yesno = 0;
}
/* The numeric choice's key table: the original builds the keys '1'.. on its stack (SS:BP-10h) and points the live key
 * table there; the native gives the poll the same far pointer and the keys (xeen_input_poll_stack_keys).
 * Original: 2F5B1..2F76E (operation 48's 33329). */
#define CHOICE_SS   0x57f5u /* the MZ header's SS (file value, as 4853 is DS's) */
#define CHOICE_KEYS 0xffeeu /* a native stack offset: the original's BP-10h is its own */
static const uint16_t choice_bytes[5] = {XEEN_DS_EXPLORATION_BUTTON_Y, XEEN_DS_EXPLORATION_BUTTON_WIDTH,
                                         XEEN_DS_EXPLORATION_BUTTON_HEIGHT, XEEN_DS_EXPLORATION_BUTTON_KEYS,
                                         XEEN_DS_EXPLORATION_BUTTON_ENABLED};
/**
 * Opens an event's choice of N (at most 9) numbered buttons: enters the modal (UI mode 0Dh, the Clairvoyance faces
 * still), loads choose.icn, draws the icon frame, the speaking NPC's face form with Text off and the speech window open
 * (the buttons' first row then 26 pixels lower), and button i (frame 2i) at the exploration buttons' place i,
 * presents the icon window, ends the exploration button table after entry N (its old entry kept for choice_end) and
 * installs it with keys '1'..'0' + N. Returns 0 without a presentation, for N above 9, or when the modal or the
 * sprites cannot be had (the modal is then finished again). Original: 2F5B1(n): 190ED, 190A3(choose.icn), 43287,
 * 189BC.
 */
int choice_begin(xeen_game *g, unsigned n)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, keys[9];
    uint16_t h = 0;
    unsigned i;
    if (!g->presentation || n > 9u) {
        return 0;
    }
    if (xeen_modal_begin(&g->modal, m, XEEN_DS_SEGMENT) != XEEN_OK) {
        return 0;
    }
    xeen_presentation_trace("mark choice-begin %u", n);
    ds[XEEN_DS_UI_MODE] = 0x0d;
    clairvoyance_still(ds);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_CHOOSE_SPRITES), &h)) {
        ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        (void)xeen_modal_finish(&g->modal, m, XEEN_DS_SEGMENT);
        return 0;
    }
    exit_draw(g, xeen_read16(m, XEEN_DS_GLOBAL_SPRITES), 7, 232, 74, 0);
    if (ds[XEEN_DS_TEXT_OFF] && ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        /* 2F60D, the face form: 19A6..19A8 = F384 x 1Ah + 4Bh
         * (the first row's buttons), 4E38C(28h, FA92: the name 3000D kept),
         * the frame and face when 8D56 is set, 4F6F8(28h), 50BF6(28h). */
        ds[XEEN_DS_EXPLORATION_BUTTON_Y] = ds[XEEN_DS_EXPLORATION_BUTTON_Y + 1u] =
            ds[XEEN_DS_EXPLORATION_BUTTON_Y + 2u] =
                (uint8_t)(ds[XEEN_DS_TEXT_OFF] * 0x1au + 0x4bu);
        face_form(g, ds + XEEN_DS_SPEECH_NAME);
    }
    for (i = 0; i < n; ++i) {
        exit_draw(g, h, (uint16_t)(2u * i), (int16_t)xeen_read16(m, (uint16_t)(XEEN_DS_EXPLORATION_BUTTON_X + 2u * i)),
                  ds[XEEN_DS_EXPLORATION_BUTTON_Y + i], 0);
        keys[i] = (uint8_t)('1' + i);
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_ICONS);
    g->choice_x = xeen_read16(m, (uint16_t)(XEEN_DS_EXPLORATION_BUTTON_X + 2u * n));
    for (i = 0; i < 5u; ++i) {
        g->choice_saved[i] = ds[choice_bytes[i] + n];
    }
    xeen_write16(m, (uint16_t)(XEEN_DS_EXPLORATION_BUTTON_X + 2u * n), 0xffffu);
    for (i = 0; i < 5u; ++i) {
        ds[choice_bytes[i] + n] = 0xff;
    }
    xeen_combat_buttons(m, 0);
    xeen_write16(m, XEEN_DS_BUTTON_KEY_TABLE, CHOICE_KEYS);
    xeen_write16(m, XEEN_DS_BUTTON_KEY_TABLE + 2u, CHOICE_SS);
    xeen_input_poll_stack_keys(&g->input_poll, CHOICE_SS, CHOICE_KEYS, keys, n);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->choice = n;
    return 1;
}
/* Ends a choice: the button stack popped, the portrait clicks and the UI mode back, the first row's y back to 4Bh,
 * the exploration button table's entry N restored, the stack keys withdrawn, then yesno_end's sprites pop and screen
 * restoration. Unlike the yes/no's end it leaves the Clairvoyance faces and the yes/no buttons' row as they are.
 * Original: 2F7DA (18FFF, 98D6, 19A6..19A8, 9A1C; then 190CF and 28D80); not 2F2FF's 2F577. */
void choice_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i, n = g->choice;
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    ds[XEEN_DS_PORTRAIT_CLICKS] = g->modal.faces;
    ds[XEEN_DS_EXPLORATION_BUTTON_Y + 2u] = ds[XEEN_DS_EXPLORATION_BUTTON_Y + 1u] =
            ds[XEEN_DS_EXPLORATION_BUTTON_Y] = 0x4b;
    ds[XEEN_DS_UI_MODE] = g->modal.mode;
    xeen_write16(m, (uint16_t)(XEEN_DS_EXPLORATION_BUTTON_X + 2u * n), g->choice_x);
    for (i = 0; i < 5u; ++i) {
        ds[choice_bytes[i] + n] = g->choice_saved[i];
    }
    g->modal.active = 0;
    xeen_input_poll_stack_keys(&g->input_poll, 0, 0, NULL, 0);
    yesno_end(g);
    xeen_presentation_trace("mark choice-end");
    g->choice = 0;
}
/* Closes every open window on the window stack, top down, each restored through the session's window service, then
 * empties the stack (depth FFFFh); in the event UI mode the event script is restored too. Returns 0, or the stop of
 * the step that failed (a window's restore or the event script's), the windows closed so far staying
 * closed. Original: 186C0 (the stack 9843, 4F57B, 3064; 186FA's 4A1B7). */
uint32_t windows_close_all(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    int slot;
    for (slot = 0x28; slot >= 0; --slot) {
        uint8_t w = ds[XEEN_DS_WINDOW_STACK + (unsigned)slot];
        uint16_t ax = 0;
        xeen_event_request q = {XEEN_REQUEST_WINDOW_RESTORE, w, 0, 0, 0};
        if (!ds[(uint16_t)(XEEN_DS_WINDOW_OPEN + w)]) {
            continue;
        }
        ds[(uint16_t)(XEEN_DS_WINDOW_OPEN + w)] = 0;
        if (event_service(g, &q, ds, sizeof m->ds, &ax) != 1) {
            return WINDOWS_STOP_RESTORE;
        }
    }
    xeen_write16(m, XEEN_DS_WINDOW_DEPTH, 0xffffu);
    if (ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EVENT) {
        uint16_t ax = 0;
        xeen_event_request q = {XEEN_REQUEST_RESTORE_EVENTS, 0, 0, 0, 0};
        if (xeen_event_resource_service(&g->resources, &q, ds, sizeof m->ds, &ax) != 1) {
            return WINDOWS_STOP_RESTORE_EVENTS;
        }
    }
    return 0;
}
/* Operation 7, the teleport: the script record's map (+6) and signed x, y (+7, +8). Every open window is closed
 * first. Map 0 is a mirror destination: the row of the mirror table chosen by the script's AX (one-based) gives x, y,
 * the map and a facing (FFh keeps the party's); the map becomes AX; a row whose x and y are both 0 means the new
 * map's run position; the transport sound plays. The party is marked as having acted. A different map is loaded
 * (after the sound of a portal object at the party's cell: object 2Fh plays 2Dh, 30h 2Ch). The party is placed (x -1:
 * the map's run position) and the session stops for the interpreter to end the script on the new map. Stops: the
 * windows' restore, a mirror row past the loaded table, the sound, the map load.
 * Original: 3193B (186C0; 3067 = 1; 1880E by 98D9; 19E82(map); 2125F the run position C0FB/C104; 189D7; the script
 * ends at 33690); map 0 at 3196B: the row F42C:(AX - 1) x 32 (16-bit) of the B416 table, x +1Dh, y +1Eh, facing
 * +1Fh, map +1Ch, 1880E(33h). */
xeen_status teleport(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const uint8_t *p = g->resident + (uint16_t)g->execution.state.offset;
    uint16_t map = p[6];
    int x = (int8_t)p[7], y = (int8_t)p[8];
    uint32_t stop;
    if ((stop = windows_close_all(g)) != 0) {
        return game_stop(g, stop);
    }
    if (!map) {
        uint16_t at = (uint16_t)((g->dispatcher.execution.word_ax - 1u) << 5); /* x MIRROR_ROW_BYTES */
        const uint8_t *row = g->f42c + at; /* BP-E */
        /* The row is the F42C block's (f42c): its unknown bytes are a stop. */
        if ((uint32_t)at + MIRROR_ROW_BYTES > g->f42c_known) {
            return game_stop(g, EVENT_TELEPORT_STOP_ROW);
        }
        x = row[MIRROR_ROW_X];
        y = row[MIRROR_ROW_Y];
        if (row[MIRROR_ROW_DIRECTION] != 0xffu) {
            ds[XEEN_DS_PARTY_DIRECTION] = row[MIRROR_ROW_DIRECTION];
        }
        map = row[MIRROR_ROW_MAP];
        g->dispatcher.execution.word_ax = map;
        g->execution.word_ax = map;
        if (!x && !y) {
            x = -1;
        }
        if (sound_effect(g, XEEN_CUE_TRANSPORTED) !=
            XEEN_OK) {
            return game_stop(g, EVENT_TELEPORT_STOP_SOUND);
        }
    }
    ds[XEEN_DS_PARTY_ACTED] = 1;
    if (ds[XEEN_DS_PARTY_MAP] != map) {
        uint8_t object = ds[(uint16_t)(XEEN_DS_NUMBERED_OBJECTS + XEEN_MAP_OBJECT_SPRITE +
                                       XEEN_MAP_OBJECT_BYTES * ds[XEEN_DS_EVENT_OBJECT])];
        if (object == 0x2fu || object == 0x30u) {
            (void)sound_effect(g, object == 0x2fu ? 0x2du : 0x2cu);
        }
        if (map_reload(g, map) != XR_OK) {
            return game_stop(g, EVENT_TELEPORT_STOP_TRAVEL);
        }
        m = &g->movement;
        ds = m->ds;
    }
    if (x == -1) {
        ds[XEEN_DS_PARTY_X] = ds[XEEN_DS_MAP_FIELD(RUN_X)];
        ds[XEEN_DS_PARTY_Y] = ds[XEEN_DS_MAP_FIELD(RUN_Y)];
    }
    else {
        ds[XEEN_DS_PARTY_X] = (uint8_t)x;
        ds[XEEN_DS_PARTY_Y] = (uint8_t)y;
    }
    xeen_navigation_set(m, (xeen_navigation){ds[XEEN_DS_PARTY_DIRECTION], ds[XEEN_DS_PARTY_X],
                                             ds[XEEN_DS_PARTY_Y], ds[XEEN_DS_PARTY_MAP]});
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->phase = XG_STOPPED;
    drive(g);
    return XEEN_SERVICE_REQUIRED;
}
