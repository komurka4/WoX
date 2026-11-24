#include "game_location.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when the inn cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    INN_STOP_NO_ROSTER = 0x43e0b,
    INN_STOP_FACE_SPRITES = 0x2a084,         /* a member's face sprites */
    INN_STOP_DRAW = 0x43f99,                 /* the roster's rows could not be drawn */
    INN_STOP_REDRAW = 0x44115,
    INN_STOP_SPRITES = 0x43a62,              /* esc.icn */
    INN_STOP_DELETE_CONFIRM = 0x442b6,       /* the deletion's confirmation could not be shown */
    INN_STOP_ADD_SOUND = 0x4451a,            /* the refusal's effect */
    INN_STOP_EXIT_NO_MEMORY = 0x44553,       /* no memory for the state's copy on leaving */
    INN_STOP_NO_CUR_FILES = 0x19278,         /* no state archives or no roster to save */
    INN_STOP_ARCHIVE_COPY = 0x19278,         /* a state archive could not be copied */
    INN_STOP_SLOT_WRITE = 0x19629,           /* no file services to write the slot */
    INN_STOP_ARCHIVE_OPEN = 0x19278          /* the slot was saved but its fresh state archive does not open */
};

/* ------------------------------------------- the inn
 * The roster screen of the inn the party signed in at (reached as map 0). The roster is the saved maze.chr image
 * (30 character records; g->roster); the list shows, four at a time, the first 24 records signed in at this inn on
 * this side. Keys: R remove a member (then F1..F6), 1..4 add a listed character, C create one, D delete one,
 * E, X, Esc or Space leave, F1..F6 the character view, Up and Down scroll by four.
 * Original: 43E0B, from 43C37 with map 0 (ED99); the roster B41A (162h a record, read by 19953); the keys CS:1CA9;
 * Remove 43A49, Create 4063B. */
static xeen_status inn_stop(xeen_game *g, uint32_t boundary)
{
    g->panel_stage = 0;
    return panel_stop(g, boundary);
}
static xeen_status inn_redraw_poll(xeen_game *);
static xeen_status inn_delete(xeen_game *, uint16_t);
static xeen_status inn_redraw(xeen_game *);
/* The inn's list: the roster places (of the first 24) holding a character signed in at this inn (its inn map) on this
 * side of the world. Original: 43E96..43F15 (+154h = ED9D, +12h = 96E2). */
static void inn_list(xeen_game *g)
{
    const uint8_t *ds = g->movement.ds;
    unsigned i;
    memset(g->inn_list, 0xff, sizeof g->inn_list);
    g->inn_count = 0;
    for (i = 0; i < 24u; ++i) {
        const uint8_t *r = g->roster + XEEN_CHARACTER_BYTES * i;
        if (r[0] && (unsigned)xeen_get_le16(r + XEEN_CHARACTER_INN) == ds[XEEN_DS_INN_MAP] &&
            r[XEEN_CHARACTER_INN_SIDE] == ds[XEEN_DS_MAP_SIDE]) {
            g->inn_list[g->inn_count++] = (uint8_t)i;
        }
    }
}
enum {
    INN_ROW_BUTTON = 7u,     /* the rows' buttons follow Up, Down, D, R, C, X and Esc in the inn's set (keys 1..4) */
    INN_ROW_X_LEFT = 0x10,   /* rows 0 and 2 */
    INN_ROW_X_RIGHT = 0x75,  /* rows 1 and 3 */
    INN_ROW_END = 0xffff     /* the x that ends the rows after the last listed character */
};
/* The four rows from the list's top line: each row's button x (left or right column; FFFFh ends the rows after the
 * last listed character), its face record (the character's roster face, or none) and its text: the name ("In Party"
 * for a party member), race (5 letters), sex and class, the roster text into the text buffer; then the party bar,
 * presented only with ARG 1 (the first draw passes 0, a redraw 1). Returns 0 when the text does not fit.
 * Original: 4377A(top, list, arg): 1B82 + 2k, 2C18 + 0Ch x k, the 8D66 faces, the 7A81 text; 2ABB8(arg) at 43934
 * (the native once skipped the bar: no party portraits, owner report V2). */
static int inn_rows(xeen_game *g, int arg)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char names[4][16], races[4][6], sexes[4][15], classes[4][15], out[1000];
    unsigned k, i;
    int n;
    memset(names, 0, sizeof names);
    memset(races, 0, sizeof races);
    memset(sexes, 0, sizeof sexes);
    memset(classes, 0, sizeof classes);
    for (k = 0; k < 4u; ++k) {
        unsigned who = g->inn_list[g->inn_top + k];
        int in_party = 0;
        const uint8_t *r;
        for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
            if (ds[XEEN_DS_PARTY_MEMBERS + i] == who) {
                in_party = 1;
            }
        }
        if (who == 0xffu) {
            xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_INN, INN_ROW_BUTTON + k), INN_ROW_END);
            break;
        }
        xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_INN, INN_ROW_BUTTON + k), (k & 1u) ? INN_ROW_X_RIGHT : INN_ROW_X_LEFT);
        r = g->roster + XEEN_CHARACTER_BYTES * who;
        if (in_party) {
            (void)snprintf(names[k], sizeof names[k], "%s", ds_text(g, XEEN_TEXT_INN_IN_PARTY));
        } else {
            memcpy(names[k], r, 15);
            names[k][15] = 0;
        }
        memcpy(races[k],
               ds_text(g, xeen_read16(m, (uint16_t)(XEEN_DS_RACE_NAMES + 4u * r[XEEN_CHARACTER_RACE]))), 5);
        (void)snprintf(
                sexes[k], sizeof sexes[k], "%s",
                ds_text(g, xeen_read16(m, (uint16_t)(XEEN_DS_SEX_NAMES + 4u * r[XEEN_CHARACTER_SEX]))));
        (void)snprintf(
                classes[k], sizeof classes[k], "%s",
                ds_text(g, xeen_read16(m, (uint16_t)(XEEN_DS_CLASS_NAMES + 4u * r[XEEN_CHARACTER_CLASS]))));
    }
    for (k = 0; k < 4u; ++k) {
        unsigned who = g->inn_list[g->inn_top + k];
        xeen_write16(m, (uint16_t)(XEEN_DS_INN_DRAW_LIST + XEEN_VIEW_RECORD_BYTES * k),
                     who == 0xffu ? 0xffffu : xeen_read16(m, XEEN_DS_ROSTER_FACE_SPRITE(who)));
    }
    if (g->presentation) {
        (void)xeen_world_entry_party_bar(m, g->presentation, arg); /* 43934 2ABB8(arg) */
    }
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_INN_ROSTER), names[0], races[0], sexes[0], classes[0],
                 names[1], races[1], sexes[1], classes[1], names[2], races[2], sexes[2], classes[2],
                 names[3], races[3], sexes[3], classes[3]);
    if (n < 0 || (size_t)n >= sizeof out) {
        return 0;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER, out, (size_t)n + 1u);
    return 1;
}
/* The inn's screen in the view window (FRAMED or text only): the roster text with the inn's buttons around it, the
 * four faces (the draw list's own positions, at (16, 16), (117, 16), (16, 59) and (117, 59) inside the window), and
 * inn.icn's six buttons along y 100. Presentation only; returns 0 when the text does not fit.
 * Original: 43F8D..4404C / 44109..441C8 (the 7BD2 text, window 0Bh, 4ADB1(0Bh, 2C18) placed by 4F1BE, six 4AC92 at
 * x 10h, 34h, 57h, 7Ah, 9Dh, C0h; an earlier native placed the faces 8 px off and the buttons at 10h + 23h x k). */
static int inn_draw(xeen_game *g, int framed)
{
    static const int16_t button_x[6] = {0x10, 0x34, 0x57, 0x7a, 0x9d, 0xc0};
    xeen_movement *m = &g->movement;
    char out[1200];
    int n;
    unsigned k;
    xeen_draw_list list;
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_INN_BUTTONS), ds_text(g, XEEN_DS_TEXT_BUFFER));
    if (n < 0 || (size_t)n >= sizeof out || n >= 1000) {
        return 0;
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    menu_window_text(g, XEEN_WINDOW_VIEW, framed, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    if (g->presentation && xeen_scene_collect_draw_list(m, XEEN_WINDOW_VIEW, XEEN_DS_INN_DRAW_LIST, &list) == XEEN_OK) {
        (void)xeen_presentation_submit(g->presentation, &list);
    }
    for (k = 0; k < 6u; ++k) {
        exit_draw(g, xeen_read16(m, XEEN_DS_INN_SPRITES), (uint16_t)(2u * k), button_x[k], 100, 0);
    }
    return 1;
}
/* Redraws the inn after a change: the party members' faces from their roster faces, the rows with the party bar
 * presented, the screen, then the inn's poll. Stops when a text does not fit. Original: 440C7 (9806 = 8D66 of the
 * record, A827 cleared, 4377A(top, list, 1)), the poll 441D3. */
static xeen_status inn_redraw(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(i),
                     xeen_read16(m, XEEN_DS_ROSTER_FACE_SPRITE(m->ds[XEEN_DS_PARTY_MEMBERS + i])));
    }
    m->ds[XEEN_DS_TEXT_BUFFER] = 0;
    if (!inn_rows(g, 1) || !inn_draw(g, 1)) {
        return inn_stop(g, INN_STOP_REDRAW);
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW);
    return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
}
/* Copies each party member's live record back to its roster place (a place past 29 is skipped). Original: 43BFE
 * (B41A + 162h x ED8E[i]). */
void inn_commit(xeen_game *g)
{
    const uint8_t *ds = g->movement.ds;
    unsigned i;
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        if (ds[XEEN_DS_PARTY_MEMBERS + i] < XEEN_ROSTER_MEMBERS) {
            memcpy(g->roster + XEEN_CHARACTER_BYTES * ds[XEEN_DS_PARTY_MEMBERS + i],
                   ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * i, XEEN_CHARACTER_BYTES);
        }
    }
}
/* Closes the party up after a member left: each empty member slot (FFh) takes the next member after it (its place and
 * record moved, the old slot emptied and its record zeroed). The caller lowers the party count; a removed last
 * member's record stays where it was. Original: 432CA. */
void inn_compact(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    unsigned to, from;
    for (to = 0; to < ds[XEEN_DS_PARTY_COUNT]; ++to) {
        if (ds[XEEN_DS_PARTY_MEMBERS + to] != XEEN_NO_MEMBER) {
            continue;
        }
        for (from = to + 1u; from < ds[XEEN_DS_PARTY_COUNT]; ++from) {
            if (ds[XEEN_DS_PARTY_MEMBERS + from] >= 0xffu) {
                continue;
            }
            ds[XEEN_DS_PARTY_MEMBERS + to] = ds[XEEN_DS_PARTY_MEMBERS + from];
            memcpy(ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * to,
                   ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * from, XEEN_CHARACTER_BYTES);
            ds[XEEN_DS_PARTY_MEMBERS + from] = XEEN_NO_MEMBER;
            memset(ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * from, 0, XEEN_CHARACTER_BYTES);
            break;
        }
    }
}
/* Saves at the inn (after Delete, Create and the exit): the roster, the party and the game's name into fresh copies of
 * the current state archives, the save slot files written, and the active world's copy adopted as the state.
 * Returns 0, or the stop of the step that could not be done (no state archives, no slot writer, a copy, the files).
 * Original: 19278 alone (into DARK.CUR, the slot files, the play time). */
uint32_t inn_save_files(xeen_game *g)
{
    uint8_t *active = NULL, *dark = NULL;
    size_t an = 0, dn = 0;
    uint32_t stop;
    xeen_archive cur;
    if (!g->cur[0].bytes || !g->cur[1].bytes || !g->roster) {
        return INN_STOP_NO_CUR_FILES;
    }
    if (!g->save_files->write || !g->save_files->clock) {
        return INN_STOP_SLOT_WRITE;
    }
    if (!(dark = cur_copy(g, 1, &dn))) {
        return INN_STOP_ARCHIVE_COPY;
    }
    /* On Darkside the active world's copy IS the DARK.CUR copy (one buffer, ACTIVE == DARK): the original has one
     * DARK.CUR, written in place. On Clouds they are two buffers. */
    if (g->world == 1u) {
        active = dark;
        an = dn;
    } else if (!(active = cur_copy(g, g->world, &an))) {
        free(dark);
        return INN_STOP_ARCHIVE_COPY;
    }
    if ((stop = save_slot_files(g, active, an, dark, dn, g->roster)) != 0 ||
        xeen_archive_open(&cur, active, an) != XR_OK) {
        if (dark != active) {
            free(dark);
        }
        free(active);
        if (!stop) {
            stop = INN_STOP_ARCHIVE_OPEN;
        }
        return stop;
    }
    state_adopt(g, active, an, cur); /* the state now owns ACTIVE */
    if (dark != active) {
        cur_replace(g, 1, dark, dn); /* Clouds: the archive-1 copy owns the saved DARK.CUR */
    } else {
        /* Darkside: the state owns the one buffer, so archive 1 takes a copy of it (cur_owned_sync: the Sign In and
         * the session hand-off read the saved DARK.CUR there); its only failure is the copy's memory. */
        return cur_owned_sync(g);
    }
    return 0;
}
/* The question's window top (4Ch), its height and the Delete prompt's offset (24h), its Esc button's y (54h, 1E1C). */
enum { INN_SELECT_Y = 0x4cu, INN_SELECT_STEP = 0x24u, INN_SELECT_ESC_Y = 0x54u };
/* Asks "Remove / Delete which character?" (MODE 0 Remove, 1 Delete): the portrait clicks saved, the button set and
 * esc.icn pushed, the question in a small window over the list (36 pixels lower for Delete) with its Esc button, the
 * Esc and 1..4 buttons installed (the Esc row moved with the window) and portrait clicks on only for Remove; then the
 * prompt's poll. Stops without esc.icn. Original: 43A49(mode, list, top): 98D6, 190ED, 190A3(esc.icn), "%s which
 * character?" (295C), 50B45 and 1871B(1Ch), the buttons 1E10..1E30 (1E1C = 24h x mode + 54h). */
static xeen_status inn_select(xeen_game *g, unsigned mode)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[160];
    uint16_t h = 0;
    g->inn_select_mode = (uint8_t)mode;
    g->inn_select_portrait_clicks = ds[XEEN_DS_PORTRAIT_CLICKS];
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_INN_ESC_SPRITES), &h)) {
        return inn_stop(g, INN_STOP_SPRITES);
    }
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_INN_WHICH_CHARACTER),
                   far_text(g, XEEN_DS_INN_MEMBER_ACTIONS, mode));
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, out);
    /* 43A87..43AA8: 50B45(32h, 4Ch + 24h*mode, D8h, 24h, 1Ch) (x, y, width, height), then 1871B(1Ch) (the pixels
     * under it saved, the frame); define_window takes (x, y, height, width) */
    (void)xeen_presentation_define_window(g->presentation, XEEN_WINDOW_DEFINED, 50,
                                          (uint16_t)(INN_SELECT_Y + INN_SELECT_STEP * mode), INN_SELECT_STEP,
                                          216); /* constant rects, always valid */
    panel_window(g, XEEN_WINDOW_DEFINED, 1);
    menu_window_text(g, XEEN_WINDOW_DEFINED, WINDOW_FRAMED, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    exit_draw(g, h, 0, 225, (int16_t)(mode * INN_SELECT_STEP + INN_SELECT_ESC_Y), 0);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_DEFINED);
    button_tables(m, XEEN_BUTTONS_ESC_1_4);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    ds[XEEN_BUTTON_Y(XEEN_BUTTONS_ESC_1_4, 0)] = (uint8_t)(mode * INN_SELECT_STEP + INN_SELECT_ESC_Y);
    ds[XEEN_DS_PORTRAIT_CLICKS] = (uint8_t)(mode == 0u);
    return panel_set(g, INN_SELECT, INN_SELECT_BOUNDARY);
}
/* Ends the prompt: its window closed (the pixels beneath restored, nothing presented), the button set and esc.icn
 * popped, the shared Esc 1..4 set's first row put back (y 78h, the second x 10h) and the portrait clicks restored.
 * Original: 43BA8 (18765(1Ch), 18FFF, 190CF, 1E1C, 1E12, 98D6). */
static void inn_select_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    panel_window(g, XEEN_WINDOW_DEFINED, 0);
    xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    handle_pop(g);
    ds[XEEN_BUTTON_Y(XEEN_BUTTONS_ESC_1_4, 0)] = 0x78;
    xeen_write16(m, XEEN_BUTTON_X(XEEN_BUTTONS_ESC_1_4, 1), 0x10);
    ds[XEEN_DS_PORTRAIT_CLICKS] = g->inn_select_portrait_clicks;
}
/* Delete chose listed line KEY ('1'..'4'): a character holding the Xeen Slayer Sword cannot be deleted (a message,
 * then the inn's poll); otherwise "Are you sure you want to delete NAME the CLASS?" (the name copied as strncpy's 16
 * bytes) in the yes/no box, answered by inn_delete_answer. Stops when the box cannot open.
 * Original: 4422C..442C2 (3CF35, 44269 in the 18F2F scroll 44526, 15616(name, 10h), 2937A). */
static xeen_status inn_delete(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *r;
    unsigned who = g->inn_list[key - '1' + g->inn_top];
    char out[160];
    g->inn_delete_who = (uint8_t)who;
    r = g->roster + XEEN_CHARACTER_BYTES * who;
    if (xeen_character_has_xeen_slayer_sword(r)) {
        return scroll0_open(g, MESSAGE_INN, ds_text(g, XEEN_TEXT_INN_KEEP_XEEN_SLAYER));
    }
    {
        size_t n = 0;
        while (n < 16u && r[n]) {
            ++n;
        }
        memcpy(ds + XEEN_DS_TEXT_BUFFER_0, r, n);
        memset(ds + XEEN_DS_TEXT_BUFFER_0 + n, 0, 16u - n);
    }
    (void)snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_INN_DELETE_CONFIRM), ds_text(g, XEEN_DS_TEXT_BUFFER_0),
                   ds_text(g, xeen_read16(m, (uint16_t)(XEEN_DS_CLASS_NAMES + 4u * r[XEEN_CHARACTER_CLASS]))));
    memcpy(ds + XEEN_DS_TEXT_BUFFER, out, strlen(out) + 1u);
    g->panel_stage = INN_POLL;
    return open_confirm(g, CONFIRM_INN_DELETE, XEEN_DS_TEXT_BUFFER) ? XEEN_SERVICE_REQUIRED : inn_stop(g, INN_STOP_DELETE_CONFIRM);
}
/* The delete's answer: no polls again; yes zeroes the roster place, rebuilds the list (by the inn map only: unlike
 * inn_list it does not test the side, an original quirk), takes the character out of the party if there (the slot
 * emptied, the party closed up and one smaller), copies the members back, saves (inn_save_files) and redraws.
 * Original: the yes/no 2937A (owner 6); 442D8 (no 96E2 test), 432CA, ED8C - 1, 43BFE, 19278. */
xeen_status inn_delete_answer(xeen_game *g, int answer)
{
    uint8_t *ds = g->movement.ds;
    unsigned i, who = g->inn_delete_who;
    uint32_t stop;
    g->panel_stage = 0;
    if (!answer) {
        return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
    }
    memset(g->roster + XEEN_CHARACTER_BYTES * who, 0, XEEN_CHARACTER_BYTES);
    memset(g->inn_list, 0xff, sizeof g->inn_list);
    g->inn_count = 0;
    for (i = 0; i < 24u; ++i) {
        const uint8_t *r = g->roster + XEEN_CHARACTER_BYTES * i;
        if (r[0] && (unsigned)xeen_get_le16(r + XEEN_CHARACTER_INN) == ds[XEEN_DS_INN_MAP]) {
            g->inn_list[g->inn_count++] = (uint8_t)i;
        }
    }
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        if (ds[XEEN_DS_PARTY_MEMBERS + i] == who) {
            ds[XEEN_DS_PARTY_MEMBERS + i] = XEEN_NO_MEMBER;
            inn_compact(g);
            --ds[XEEN_DS_PARTY_COUNT];
        }
    }
    inn_commit(g);
    if ((stop = inn_save_files(g)) != 0) {
        return inn_stop(g, stop);
    }
    ++g->saves;
    return inn_redraw(g);
}
/* The "which character?" prompt's keys: Esc returns 0; 1..4 choose a listed record (Delete); F1..F6 a present
 * member (Remove: the member's slot emptied, the roster places closed up, the party one smaller, the redraw); any
 * other key polls again. Original: 43B35..43BA6 (CS:12E2); Remove 4437A: 43BFE, ED8E[n] = FFh, 432CA, ED8C - 1. */
static xeen_status inn_select_key(xeen_game *g, uint16_t key)
{
    uint8_t *ds = g->movement.ds;
    if (key == XEEN_KEY_ESCAPE) {
        xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
        inn_select_end(g);
        return inn_redraw_poll(g);
    }
    if (key >= '1' && key <= '4') {
        if (!g->inn_select_mode || g->inn_list[key - '1' + g->inn_top] == 0xffu) {
            return panel_set(g, INN_SELECT, INN_SELECT_BOUNDARY);
        }
        inn_select_end(g);
        return inn_delete(g, key);
    }
    if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6 && !g->inn_select_mode &&
        ds[XEEN_DS_PARTY_COUNT] > key - XEEN_KEY_F1) {
        inn_select_end(g);
        inn_commit(g);
        ds[XEEN_DS_PARTY_MEMBERS + (key - XEEN_KEY_F1)] = 0xff;
        inn_compact(g);
        --ds[XEEN_DS_PARTY_COUNT];
        return inn_redraw(g);
    }
    return panel_set(g, INN_SELECT, INN_SELECT_BOUNDARY);
}
/**
 * E, X, Esc or Space: leaves the inn with the chosen party. With no member it refuses ("You have no one to adventure
 * with", then the inn's poll again). Unless the UI mode is 0 the view darkens step by step (light 4 down to 0); then
 * the party's size is kept, the party goes to the inn's map, the roster is stored and the save files written, the
 * active world's state becomes the map load's copy, the roster's sprites are freed and the inn's music flag
 * cleared, and the game goes back to the map (outer_world).
 * Original: 44401 (4440A: the 18F2F mode-0 scroll, the poll 441D3); 9A1C nonzero: F60C 4..0 (4EA0D, 4A68A, 26306,
 * 50BF6(0Bh), a 4EA1C tick); 18765(0Bh), ED8D = ED8C, ED99 = ED9D, 43BFE, 19278; 44553: 33D36 (DOS memory),
 * 4AA3E(98CE + 1), 8D66 cleared, 8D28 = 0, 18765(0Bh); 43C37 returns to 16169: 1791F.
 */
static xeen_status inn_exit(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint32_t stop;
    int k;
    if (!ds[XEEN_DS_PARTY_COUNT]) {
        return scroll0_open(g, MESSAGE_INN, ds_text(g, XEEN_TEXT_INN_NO_ONE));
    }
    if (ds[XEEN_DS_UI_MODE]) {
        for (k = 4; k >= 0; --k) {
            xeen_write16(m, XEEN_DS_VIEW_LIGHT_LEVEL, (uint16_t)k);
            location_border(g);
            (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_VIEW);
        }
        xeen_write16(m, XEEN_DS_VIEW_LIGHT_LEVEL, 0xffff);
    }
    xeen_window_close(m, XEEN_WINDOW_VIEW);
    ds[XEEN_DS_SAVED_PARTY_COUNT] = ds[XEEN_DS_PARTY_COUNT];
    ds[XEEN_DS_PARTY_MAP] = ds[XEEN_DS_INN_MAP];
    inn_commit(g);
    if ((stop = inn_save_files(g)) != 0) {
        return inn_stop(g, stop);
    }
    ++g->saves;
    /* The map load reads the active world's state as the files hold it (the tavern saved this map into it): the
     * hand-off's copy. Original: the tavern's 30F34. */
    {
        uint8_t *c = bytes_copy(g->state.bytes, g->state.size);
        if (!c) {
            return inn_stop(g, INN_STOP_EXIT_NO_MEMORY);
        }
        cur_replace(g, g->world, c, g->state.size);
    }
    free(g->roster);
    g->roster = NULL;
    g->panel_stage = 0;
    (void)xeen_sprite_free_from(&g->sprites, (uint16_t)(xeen_read16(m, XEEN_DS_CHARPOW_SPRITES) + 1u));
    memset(ds + XEEN_DS_ROSTER_FACE_SPRITES, 0, XEEN_ROSTER_FACE_BYTES);
    ds[0x8d28] = 0;
    xeen_window_close(m, XEEN_WINDOW_VIEW);
    return outer_world(g);
}
/* 1..4: the listed character on that line joins the party, unless already in it; with six members already the
 * refusal sound and "Your party is full!" (then the inn's poll). The character's record is copied from the roster
 * into the next party slot, and the inn redraws. (The input code is left as the key minus 2Ah, as the original's
 * SUB leaves it.) Original: 44473 (SUB [F5FE], 2Ah); 44518's 1880E(15h), the 18F2F mode-0 scroll, the poll 441D3;
 * ED8E[ED8C] = the record, its 162h bytes to B41E + 162h x ED8C, ED8C + 1. */
static xeen_status inn_add(xeen_game *g, uint16_t key)
{
    uint8_t *ds = g->movement.ds;
    unsigned at = (unsigned)(key - '1') + g->inn_top, who, i;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE,
                 (uint16_t)(key - 0x2au));
    if (g->inn_count <= at) {
        return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
    }
    who = g->inn_list[at];
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        if (ds[XEEN_DS_PARTY_MEMBERS + i] == who) {
            return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
        }
    }
    if (ds[XEEN_DS_PARTY_COUNT] >= 6u) {
        if (sound_effect(g, XEEN_CUE_REFUSED) != XEEN_OK) {
            return inn_stop(g, INN_STOP_ADD_SOUND);
        }
        return scroll0_open(g, MESSAGE_INN, ds_text(g, XEEN_TEXT_INN_PARTY_FULL));
    }
    ds[XEEN_DS_PARTY_MEMBERS + ds[XEEN_DS_PARTY_COUNT]] = (uint8_t)who;
    memcpy(ds + XEEN_PARTY_RECORDS + XEEN_CHARACTER_BYTES * ds[XEEN_DS_PARTY_COUNT],
           g->roster + XEEN_CHARACTER_BYTES * who, XEEN_CHARACTER_BYTES);
    ++ds[XEEN_DS_PARTY_COUNT];
    return inn_redraw(g);
}
/**
 * Handles a key in the inn (Escape or Space leaves; the roster commands select, add, remove and
 * create characters).
 */
static xeen_status inn_key(xeen_game *g, uint16_t key)
{
    switch (key) {
        case XEEN_KEY_ESCAPE:
        case ' ':
        case 'E':
        case 'X':
            return inn_exit(g);
        case '1':
        case '2':
        case '3':
        case '4':
            return inn_add(g, key);
        case 'C':
            return create_begin(g);
        case 'D':                   /* 44217 */
            if (!g->inn_count) {
                return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
            }
            return inn_select(g, 1);
        case 'R':
            if (!g->movement.ds[XEEN_DS_PARTY_COUNT]) {
                return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
            }
            return inn_select(g, 0);
        case XEEN_KEY_UP:
            if (!g->inn_top) {
                return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
            }
            g->inn_top -= 4u;
            return inn_redraw(g);
        case XEEN_KEY_DOWN:
            if ((int)g->inn_count - 5 < (int)g->inn_top) {
                return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
            }
            g->inn_top += 4u;
            return inn_redraw(g);
        default:
            if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) { /* 44531: SUB [F5FE], C9h */
                xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, (uint16_t)(key - XEEN_KEY_F1));
                if (g->movement.ds[XEEN_DS_PARTY_COUNT] <= key - XEEN_KEY_F1) {
                    return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
                }
                return view_begin(g, key - XEEN_KEY_F1, VIEW_FROM_INN); /* 44547: 44AE6, then 441D3 */
            }
            return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
    }
}
/* The "which character?" prompt's Esc: the inn polls again without a draw (the original's draw trace has none
 * between the two polls); the prompt window's restore is the only presentation. Original: 43A49's Esc (43B5D, AX 0),
 * the poll 441D3; 43BA8's 18765(1Ch). */
static xeen_status inn_redraw_poll(xeen_game *g)
{
    return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
}
/* The faces' tables once the 24 roster faces are loaded into the members' table: the roster's 30 handles are copied
 * from the 30 words there (the 24 just loaded and the 6 words after them, as they are), then each party member's
 * handle is taken from its roster record's.
 * Original: 2A040(18h)'s 8D66 copies. */
static void inn_face_copies(xeen_movement *m)
{
    const uint8_t *ds = m->ds;
    unsigned i;
    for (i = 0; i < XEEN_ROSTER_MEMBERS; ++i) {
        xeen_write16(m, XEEN_DS_ROSTER_FACE_SPRITE(i), xeen_read16(m, XEEN_DS_MEMBER_FACE_SPRITE(i)));
    }
    for (i = 0; i < ds[XEEN_DS_PARTY_COUNT]; ++i) {
        xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(i),
                     xeen_read16(m, XEEN_DS_ROSTER_FACE_SPRITE(ds[XEEN_DS_PARTY_MEMBERS + i])));
    }
}
/* The view window's frame, drawn before the roster's rows and the party bar (an earlier native framed it with the
 * text after the bar: pixel-neutral, the bar lies below the window): a frame not yet open is reset first, the window
 * is opened and its frame drawn. Original: 1871B(0Bh)'s 4F2A1 (ending with C0C = C0D = 0), before 4377A's rows and
 * 2ABB8; the bar at y 149 and below. */
static void inn_view_frame(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t r[4];
    if (!m->ds[XEEN_DS_WINDOW_OPEN + XEEN_WINDOW_VIEW]) {
        text_frame_reset(g);
    }
    xeen_window_open(m, XEEN_WINDOW_VIEW);
    if (g->presentation && xeen_presentation_window(g->presentation, XEEN_WINDOW_VIEW, r)) {
        (void)xeen_text_frame(g->presentation->framebuffer, g->presentation->framebuffer_size, r);
    }
}
/* The inn's screen revealed once drawn, then the inn's poll. A first visit: Darkside fades to black (4A75A(4)) and
 * shows the screen (50BF6(0)), then mm4.pal at once (4A74F(81h), 188BB); Clouds then opens with the book (15647(0,0)),
 * Darkside shows the cursor (4E06B) and fades in (4A74F(4): from black, though the palette is already full: the
 * original's flash). After Create ([BP-6] 1, 440AF): 50BF6(0), 4E06B, 4A74F(4) (owner report V7).
 * Original: 4404F..440C5. */
static xeen_status inn_reveal(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (!g->inn_visited) {
        if (ds[XEEN_DS_WORLD]) {
            xeen_presentation_fade(g->presentation, 0, 4);
            (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
        }
        xeen_presentation_fade(g->presentation, 1, 0x81);
        if (!ds[XEEN_DS_WORLD]) {
            g->book_from_inn = 1;
            return book_begin(g);
        }
        (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
        xeen_presentation_fade(g->presentation, 1, 4);
        return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
    }
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    xeen_presentation_fade(g->presentation, 1, 4); /* 440A5 4A74F(4) */
    return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
}
/* The inn: its music (once), the roster's 24 faces (loaded once), the exploration UI mode, the roster list and the
 * screen; a first visit opens with the book (Clouds), later visits (after Create) redraw at once; then the inn's
 * poll. Original: 43E0B: 8D28; 2A040(18h) and the 8D66 copies; 9A1C = 1; [BP-6] = 0 for 15647(0, 0); the poll
 * 441D3. */
xeen_status inn_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned i;
    char name[16];
    if (!g->roster) {
        return inn_stop(g, INN_STOP_NO_ROSTER);
    }
    if (!ds[0x8d28]) {
        location_music(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_INN_SONG_DARKSIDE : XEEN_TEXT_INN_SONG_CLOUDS), 0x80);
        ds[0x8d28] = 1;
    }
    if (!xeen_read16(m, XEEN_DS_ROSTER_FACE_SPRITES)) {
        for (i = 0; i < 24u; ++i) {
            uint16_t h = 0;
            (void)snprintf(name, sizeof name, ds_text(g, XEEN_TEXT_FACE_FILE_FORMAT), (int)(i + 1u));
            if (!sprite_load(g, name, &h)) {
                return inn_stop(g, INN_STOP_FACE_SPRITES);
            }
            xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(i), h);
        }
        inn_face_copies(m);
    }
    ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
    inn_list(g);
    ds[XEEN_DS_TEXT_BUFFER] = 0;
    g->inn_top = 0;
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    button_tables(m, XEEN_BUTTONS_INN);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, xeen_read16(m, XEEN_DS_INN_SPRITES));
    (void)menu_raw(g, ds_text(g, XEEN_TEXT_INN_BACKGROUND)); /* 188F6 back.raw */
    location_border(g);
    inn_view_frame(g);
    if (!inn_rows(g, 0) || !inn_draw(g, 0)) {
        return inn_stop(g, INN_STOP_DRAW);
    }
    return inn_reveal(g);
}
/* After the inn's opening book: the cursor shown, then the inn's poll. Original: 15647(0,0)'s 15915 (the saved
 * screen, 4AA3E, 18FFF), 4E06B, the poll 441D3. */
xeen_status inn_after_book(xeen_game *g)
{
    g->book_from_inn = 0;
    g->exit_stage = EXIT_NONE;
    g->inn_visited = 0;
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(&g->movement, XEEN_DS_MOUSE_SPRITES), 0);
    return panel_set(g, INN_POLL, INN_POLL_BOUNDARY);
}
xeen_status inn_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    uint16_t key;
    switch (g->panel_stage) {
        case INN_POLL: /* 441D3: 189E3 until a key */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return inn_key(g, key);
        case INN_SELECT: /* 43B35 */
            key = panel_poll(g);
            if (!key) {
                return XEEN_SERVICE_REQUIRED;
            }
            return inn_select_key(g, key);
        default:
            return create_resume(g, bios, user);
    }
}
