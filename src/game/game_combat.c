#include "game_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/*
 * Where native play stops when an exploration window (Info, Dismiss, the map, Quick Reference, the quick-fight
 * options) or a combat command cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    INFO_STOP_LINES = 0x29ee4,               /* the active effects' lines do not fit */
    INFO_STOP_TEXT = 0x29f99,                /* the window's text does not fit */
    INFO_STOP_WINDOW = 0x29fc1,              /* window 28 at the info's size */
    DISMISS_STOP_NO_PRESENTATION = 0x17d9b,
    DISMISS_STOP_SPRITES = 0x2b76a,          /* esc.icn */
    DISMISS_STOP_REFUSAL_SCROLL = 0x2b89d,   /* the refusal could not be shown */
    MAP_STOP_NO_PRESENTATION = 0x17dc4,
    MAP_STOP_BEGIN = 0x1da65,                /* the map view could not start */
    MAP_STOP_NO_MEMORY = 0x1db77,            /* no memory for the saved screen */
    MAP_STOP_DRAW = 0x1c680,                 /* the map could not be drawn */
    MAP_STOP_TEXT = 0x1e399,                 /* the map's caption does not fit */
    MAP_STOP_FRAME = 0x1e3b3,
    QUICK_REFERENCE_STOP_MEMBER_LINE = 0x46d65, /* a member's line (past the eight, or not formed) */
    QUICK_REFERENCE_STOP_NO_PARTY = 0x46d92,
    QUICK_REFERENCE_STOP_TEXT = 0x46df8,     /* the table does not fit */
    QUICK_FIGHT_STOP_TEXT = 0x3ea3b,         /* the quick-fight options' text does not fit */
    QUICK_FIGHT_STOP_NO_PRESENTATION = 0x3e9b6,
    QUICK_FIGHT_STOP_SPRITES = 0x3e9c4,      /* train.icn */
    QUICK_CAST_STOP_NO_HOST = 0x387f4,       /* combat's monsters, sprites, random source or presentation missing */
    QUICK_CAST_STOP_ACTOR = 0x3d8f5,         /* the actor's member past the six */
    COMBAT_STOP_MODAL_ACTOR = 0x3929c,       /* the actor's member past the six */
    COMBAT_UI_STOP_SCENE_STAGE = 0x1c1b3,     /* a scene ended in no stage of these windows */
    /* variable stop sites (stops_variable.json) */
    INFO_STOP_NO_PRESENTATION = 0x17dd1,     /* Info without a presentation (outside combat) */
    QREF_STOP_NO_PRESENTATION = 0x17e28      /* Quick Reference without a presentation (outside combat) */
};

/* ----------------------------- the combat commands that open a dialog
 * Its own translation unit (Phase 242), over game_internal.h. xeen_combat_command hands these commands to the game
 * (combat phase XC_AWAITING_CAST, combat.modal_site the handler) and xeen_combat_after_modal continues the round:
 * Tab (the shared control panel), Info, Options (the Quick Fight Options of the actor's record), Quick Reference,
 * F1..F6 (the shared character view) and Quick Fight's Cast. Exploration's I and Q share the Info and Quick Reference
 * dialogs. A dialog's scene pass inside combat is the combat scene.
 * Original: 3914E's table CS:2F7D: Tab 392C9 (297D9), Info 392BB (29EF0), Options 3929C (3E9B6), Quick Reference
 * 392C2 (46BA0), F1..F6 39322 (44AE6), Quick Fight's Cast 387F4 (3D8C1(1)); exploration's I 17DD1 and Q 17E28. */
enum {
    INFO_SCENE = COMBAT_UI_BASE,
    INFO_POLL,
    QREF_POLL,
    QFO_SCENE,
    QFO_POLL,
    MAP_SCENE,
    MAP_DRAW,
    MAP_POLL,
    DISMISS_SCENE,
    DISMISS_POLL
};
/* xeen_game.map_step: the next stage of an M-map pass (map_resume). Only the map's first pass takes a frame mark
 * first; every later pass (after a keyless poll) starts at the view. map_begin and map_end reset it.
 * Original: 1DA2D; the first pass's mark at 1DB56; the later passes enter at 4EA35 without 4EA0D. */
enum map_step {
    MAP_STEP_SAMPLE = 0,       /* the first pass's frame mark (Original: 1DB56's 4EA0D) */
    MAP_STEP_VIEW = 1,         /* the view, its frame and the party's arrow (map_view; 1DB69..1E359) */
    MAP_STEP_BLINK_WAIT = 2,   /* the arrow's blink: after more than 5 ticks (signed) it flips (1E35C's 4EA1C) */
    MAP_STEP_BLINK_SAMPLE = 3, /* a frame mark after a flip (1E36A) */
    MAP_STEP_POSITION = 4,     /* the position line, the map's window and the view shown (1E36F..1E3C2: 50AC,
                                * 50BF6(5), 50BF6(3)) */
    MAP_STEP_POLL = 5          /* drawn: the key poll (panel stage MAP_POLL; 1E3C7) */
};

/* A dialog combat opened (a modal at SITE) has finished with the answer AX: combat continues from it
 * (xeen_combat_after_modal) and the game follows its result (combat_route). When combat refuses the return (it was
 * not waiting for that site) the session stops there, unsupported, with the site as its boundary: a continuation
 * that did not happen, not a registered stop. */
xeen_status combat_modal_return(xeen_game *g, uint32_t site, uint16_t ax)
{
    uint32_t boundary = 0;
    xeen_status status;
    status = xeen_combat_after_modal(
            &g->combat, &g->movement, g->monster_database, g->monster_database_size,
            g->event_random, g->event_random_user, game_audio(g), site,
            ax, &boundary);
    if (status == XEEN_BAD_INPUT) {
        g->lifecycle_boundary = site;
        g->phase = XG_STOPPED;
        g->command_status = XGC_UNSUPPORTED;
        return status;
    }
    return combat_route(g, status, boundary);
}

/* --------------------------------------------------- 29EF0 Info */
/* The Info dialog's active effects: up to eight 30-byte lines, in a fixed place each, and how many are active:
 * Light (its value), the four protections (fire, electricity, cold, poison; their values), Clairvoyance, Levitate,
 * Walk on Water. Every line after Light leads with byte 01h, except the first one listed when Light is not, which
 * leads with 0Ah (a line feed); the column texts open and close each line. Walk on Water tests that count without
 * adding itself, and its format takes no closing text (as the original's).
 * Original: 29CA0; Light EFF8 (DS:5CCA), EFFC, EFFE, F000, F002 (DS:5CE0..5D0E), EDA1, ED9E, EDA2; the counter SI;
 * the far texts 56BC/56C0. */
static unsigned info_effects(xeen_game *g, char lines[8][30])
{
    xeen_movement *m = &g->movement;
    const uint8_t *ds = m->ds;
    unsigned count = 0, listed = 0, i;
    const char *open = far_text(g, XEEN_DS_EFFECT_COLUMN_OPEN, 0),
               *close = far_text(g, XEEN_DS_EFFECT_COLUMN_CLOSE, 0);
    static const uint16_t words[4] = {XEEN_DS_PROTECTION_FIRE, XEEN_DS_PROTECTION_ELECTRICITY,
                                      XEEN_DS_PROTECTION_COLD, XEEN_DS_PROTECTION_POISON},
                          formats[4] = {XEEN_TEXT_INFO_FIRE, XEEN_TEXT_INFO_ELECTRICITY,
                                         XEEN_TEXT_INFO_COLD, XEEN_TEXT_INFO_POISON};
    memset(lines, 0, 8u * 30u);
    if (xeen_read16(m, XEEN_DS_PARTY_LIGHT)) {
        ++count;
        ++listed;
        (void)snprintf(lines[0], 30, ds_text(g, XEEN_TEXT_INFO_LIGHT),
                       (unsigned)xeen_read16(m, XEEN_DS_PARTY_LIGHT));
    }
    for (i = 0; i < 4u; ++i) {
        if (xeen_read16(m, words[i])) {
            ++count;
            (void)snprintf(lines[1u + i], 30, ds_text(g, formats[i]), listed++ ? 1 : 0x0a, open, close,
                           (unsigned)xeen_read16(m, words[i]));
        }
    }
    if (ds[XEEN_DS_EFFECT_CLAIRVOYANCE]) {
        ++count;
        (void)snprintf(lines[5], 30, ds_text(g, XEEN_TEXT_INFO_CLAIRVOYANCE), listed++ ? 1 : 0x0a, open, close);
    }
    if (ds[XEEN_DS_EFFECT_LEVITATE]) {
        ++count;
        (void)snprintf(lines[6], 30, ds_text(g, XEEN_TEXT_INFO_LEVITATE), listed++ ? 1 : 0x0a, open, close);
    }
    if (ds[XEEN_DS_EFFECT_WALK_ON_WATER]) {
        ++count;
        (void)snprintf(lines[7], 30, ds_text(g, XEEN_TEXT_INFO_WALK_ON_WATER), listed ? 1 : 0x0a, open);
    }
    return count;
}
/* The Info dialog's scene: the frame wait returns to info_after_scene. Original: 29FE7 4EA0D, 1B503(0). */
static xeen_status info_scene(xeen_game *g)
{
    return panel_scene(g, INFO_SCENE);
}
/* The Info dialog (I): the title ("World" when Clouds is installed, else "Darkside"), the day's name (of ten), the
 * 12-hour clock (hour 0 is 12; 'p' from hour 12), the day and the year, and the active effects (info_effects), in a
 * window at (88, 20), 160 wide and 92 high plus 9 per effect and 13 when any is active; then its scene. In
 * exploration the monsters are paused first; the buttons are saved and reset to the defaults.
 * Original: 29EF0: 4DB3B(F460, 0), 190ED, 1919B; the clock EFF4 / 60; AC0F = DS:5D67 (DS:95 the side, the 2934 day
 * names); 50B45 window 1Ch; 1871B, 4E38C, 50BF6(1Ch). Exploration's 17DD1 clears 96C4. */
xeen_status info_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char lines[8][30], all[8u * 30u], out[600];
    unsigned hour = xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) / 60u, count,
             day = xeen_read16(m, XEEN_DS_DAY);
    int n;
    if (!g->presentation) {
        return game_stop(g, g->combat_ui_site ? g->combat_ui_site : INFO_STOP_NO_PRESENTATION);
    }
    xeen_presentation_trace_extra("mark dialog-info"); /* Evidence: 29EF0 */
    if (!g->combat_ui_site) {
        ds[XEEN_DS_MONSTERS_ENABLED] = 0;
    }
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(m);
    count = info_effects(g, lines);
    n = snprintf(all, sizeof all, ds_text(g, XEEN_TEXT_INFO_EFFECTS), lines[0], lines[1], lines[2], lines[3],
                 lines[4], lines[5], lines[6], lines[7]);
    if (n < 0 || (size_t)n >= sizeof all) {
        return panel_stop(g, INFO_STOP_LINES);
    }
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_INFO_PAGE), ds_text(g, ds[XEEN_DS_CLOUDS_INSTALLED] ? XEEN_TEXT_INFO_CLOUDS_TITLE : XEEN_TEXT_INFO_DARKSIDE_TITLE),
                 far_text(g, XEEN_DS_NUMBER_WORDS, (unsigned)((int16_t)day % 10)),
                 hour > 12u ? (int)hour - 12
                 : hour     ? (int)hour
                            : 12,
                 (int)(xeen_read16(m, XEEN_DS_MINUTE_OF_DAY) % 60u), hour > 11u ? 'p' : 'a', day,
                 (unsigned)xeen_read16(m, XEEN_DS_YEAR), all);
    if (n < 0 || (size_t)n >= sizeof out || XEEN_DS_TEXT_BUFFER_2 + (unsigned)n + 1u > XEEN_PARTY_RECORDS) {
        return panel_stop(g, INFO_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    if (xeen_presentation_define_window(g->presentation, XEEN_WINDOW_DEFINED, 88, 20,
                                        (uint16_t)(92u + (count ? count * 9u + 13u : 0u)),
                                        160) != XEEN_OK) {
        return panel_stop(g, INFO_STOP_WINDOW);
    }
    panel_window(g, XEEN_WINDOW_DEFINED, 1);
    menu_window_text(g, XEEN_WINDOW_DEFINED, WINDOW_FRAMED, out);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_DEFINED);
    return info_scene(g);
}
/* Info after its scene: window 28's frame and text drawn again over the view, then the key poll.
 * Original: 29FF4 (4F6F8(1Ch), 4E38C(1Ch, AC0F), 50BF6(3)), the 2A012 poll. */
static xeen_status info_after_scene(xeen_game *g)
{
    uint16_t r[4];
    xeen_presentation *p = g->presentation;
    if (p && xeen_presentation_window(p, 28, r)) {
        (void)xeen_text_border(p->framebuffer, p->framebuffer_size, r);
    }
    menu_window_text(g, XEEN_WINDOW_DEFINED, WINDOW_TEXT_ONLY, ds_text(g, XEEN_DS_TEXT_BUFFER_2));
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_VIEW_FRAME);
    return panel_set(g, INFO_POLL, INFO_POLL_BOUNDARY);
}
/* Info ends at any key: its window closes, the routing is restored, and its caller goes on (combat's command
 * loop, or exploration's command end). Original: 2A02B (18765(1Ch), 189D7, 18FFF); combat's 392D9, exploration's
 * 17C48 (96C4 = 1) and 17F07. */
static xeen_status info_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint32_t site = g->combat_ui_site;
    panel_window(g, XEEN_WINDOW_DEFINED, 0);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    g->panel_stage = 0;
    g->combat_ui_site = 0;
    if (site) {
        return combat_modal_return(g, site, 0);
    }
    m->ds[XEEN_DS_MONSTERS_ENABLED] = 1;
    return command_tail(g);
}

/* --------------------------------------- 2B75D Dismiss (D) */
/* Dismiss's scene: the frame wait returns to dismiss_after_scene. Original: 2B7F8 4EA0D, 1B503(0). */
static xeen_status dismiss_scene(xeen_game *g)
{
    return panel_scene(g, DISMISS_SCENE);
}
/* Dismiss (D) opens: the monsters paused, the buttons saved, the Esc button's sprites loaded and its button tables
 * set (Esc at (225, 120); F1..F4 off), a portrait click made to give the member's F key (the old setting kept for
 * the end), the "Dismiss whom?" prompt framed with the Esc button drawn; then its scene.
 * Original: 17D9B 96C4 = 0; 2B75D: 190ED, 190A3(esc.icn) into the 98D4 push, the tables 1E10.., 9896 = 98D4,
 * [BP-0Ah] = 98D6 and 98D6 = 1 (a click gives C9h + n), 1871B(1Fh), 4E38C(1Fh, DS:5FF2), 4AC92, 50BF6(1Fh). */
xeen_status dismiss_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    if (!g->presentation) {
        return game_stop(g, DISMISS_STOP_NO_PRESENTATION);
    }
    xeen_presentation_trace_extra("mark dialog-dismiss"); /* Evidence: 2B75D */
    ds[XEEN_DS_MONSTERS_ENABLED] = 0;
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_DISMISS_SPRITES), &h)) {
        return panel_stop(g, DISMISS_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_ESC_1_4);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    g->dismiss_portrait_clicks = ds[XEEN_DS_PORTRAIT_CLICKS];
    ds[XEEN_DS_PORTRAIT_CLICKS] = 1;
    panel_window(g, XEEN_WINDOW_MEMBER_PROMPT, 1);
    menu_window_text(g, XEEN_WINDOW_MEMBER_PROMPT, WINDOW_FRAMED, ds_text(g, XEEN_TEXT_DISMISS_WHOM));
    exit_draw_in(g, h, 0, 225, 120, 31);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_MEMBER_PROMPT);
    return dismiss_scene(g);
}
/* After Dismiss's scene: the prompt's border again, its second text, the Esc button, the view's frame and the prompt
 * shown; then the key poll. Original: 2B805: 4F6F8(1Fh), 4E38C(1Fh, DS:6009), 50BF6(3), 50BF6(1Fh); the poll 2B844. */
static xeen_status dismiss_after_scene(xeen_game *g)
{
    uint16_t r[4];
    xeen_presentation *p = g->presentation;
    if (p && xeen_presentation_window(p, XEEN_WINDOW_MEMBER_PROMPT, r)) {
        (void)xeen_text_border(p->framebuffer, p->framebuffer_size, r);
    }
    menu_window_text(g, XEEN_WINDOW_MEMBER_PROMPT, WINDOW_TEXT_ONLY, ds_text(g, XEEN_TEXT_DISMISS_WHOM_LEFT));
    exit_draw_in(g, xeen_read16(&g->movement, XEEN_DS_SPRITE_HANDLE), 0, 225, 120, 31);
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_VIEW_FRAME);
    (void)xeen_presentation_update_window(p, XEEN_WINDOW_MEMBER_PROMPT);
    return panel_set(g, DISMISS_POLL, DISMISS_POLL_BOUNDARY);
}
/* Dismiss ends: its window and esc.icn go, the routing and the portrait clicks are restored, the party bar is
 * redrawn and the key cleared; exploration's command end follows. Original: 2B950 (18765(1Fh), 190CF with the 98D4
 * pop, 18FFF, 98D6 back, 2ABB8(1), F5FE = 0), RETF to 17DA5 -> 17C48 (96C4 = 1) and 17F07. */
static xeen_status dismiss_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    member_window_close(g, g->dismiss_portrait_clicks);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->panel_stage = 0;
    ds[XEEN_DS_MONSTERS_ENABLED] = 1;
    return command_tail(g);
}
/* Dismiss's refusal scroll closed (scroll_key): the prompt's window opens again (its pixels saved, no text), then
 * Dismiss ends. Original: 2B8A2's 18F2F (owner 14), 1871B(1Fh), 2B950. */
xeen_status dismiss_scroll_closed(xeen_game *g)
{
    panel_window(g, XEEN_WINDOW_MEMBER_PROMPT, 1);
    return dismiss_end(g);
}
/* The faces close up after a Dismiss emptied one: each of the first six face words that is empty takes the next
 * nonempty word after it (up to the eighth), which is emptied in turn; the scan goes on from the next position. The
 * eight words are the members' faces by party position (XEEN_DS_MEMBER_FACE_SPRITES).
 * Original: 4333F (9806[0..5] from [i+1..7], including 9812/9814). */
static void close_up_member_faces(xeen_movement *m)
{
    unsigned i, j;
    for (i = 0; i < 6u; ++i) {
        if (xeen_read16(m, XEEN_DS_MEMBER_FACE_SPRITE(i))) {
            continue;
        }
        for (j = i + 1u; j < 8u; ++j) {
            if (xeen_read16(m, XEEN_DS_MEMBER_FACE_SPRITE(j))) {
                xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(i), xeen_read16(m, XEEN_DS_MEMBER_FACE_SPRITE(j)));
                xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(j), 0);
                break;
            }
        }
    }
}
/*
 * Dismiss's keys. Esc ends; F1..F6 of a present member dismisses: the last member refuses (the prompt closes, a
 * scroll, then the prompt again and the end); else the record is copied out, the member's slot emptied and their
 * face cleared, the faces close up (close_up_member_faces), the roster's places close up, the party shrinks by one and the dismissed record
 * goes to the slot after the party with its roster index; then the end. An absent member's key (kept as the member's
 * number) or any other key loops.
 * Original: 2B856.. (CS:3847, all to 2B874): 18765(1Fh), 18F2F(DS:6022, 1), 1871B(1Fh), 2B950; 50B71(162h), 155AF,
 * [BP-6] = ED8E[n], ED8E[n] = FFh, 9806[n] = 0, 4333F (9806[0..5] from [n+1..7], including 9812/9814), 432CA, ED8C - 1,
 * the record to slot ED8C, ED8E[ED8C] = [BP-6], 18693; 2B950; an absent member F5FE - C9h.
 */
static xeen_status dismiss_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, record[XEEN_CHARACTER_BYTES], saved;
    unsigned n;
    if (key == XEEN_KEY_ESCAPE) {
        return dismiss_end(g);
    }
    if (key < XEEN_KEY_F1 || key > XEEN_KEY_F6) {
        return dismiss_scene(g);
    }
    n = key - XEEN_KEY_F1;
    xeen_write16(m, XEEN_DS_INPUT_CODE, (uint16_t)n);
    if (ds[XEEN_DS_PARTY_COUNT] <= n) {
        return dismiss_scene(g); /* the key (now n) is not Esc: the loop (Original: 2B885 JMP 2B946) */
    }
    if (ds[XEEN_DS_PARTY_COUNT] == 1u) {
        char text[96];
        panel_window(g, XEEN_WINDOW_MEMBER_PROMPT, 0);
        (void)snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), ds_text(g, XEEN_TEXT_DISMISS_LAST));
        g->panel_stage = 0;
        if (!open_scroll_message(g, SCROLL_DISMISS_REFUSED, text)) {
            return game_stop(g, DISMISS_STOP_REFUSAL_SCROLL);
        }
        return XEEN_SERVICE_REQUIRED;
    }
    memcpy(record, ds + XEEN_PARTY_RECORD(n), sizeof record);
    saved = ds[XEEN_DS_PARTY_MEMBERS + n];
    ds[XEEN_DS_PARTY_MEMBERS + n] = XEEN_NO_MEMBER;
    xeen_write16(m, XEEN_DS_MEMBER_FACE_SPRITE(n), 0);
    close_up_member_faces(m);
    inn_compact(g);
    ds[XEEN_DS_PARTY_COUNT] = (uint8_t)(ds[XEEN_DS_PARTY_COUNT] - 1u);
    memcpy(ds + XEEN_PARTY_RECORD(ds[XEEN_DS_PARTY_COUNT]), record, sizeof record);
    ds[XEEN_DS_PARTY_MEMBERS + ds[XEEN_DS_PARTY_COUNT]] = saved;
    return dismiss_end(g);
}

#include "../addon/map_movement/map_modal.inc" /* add-on map_movement: map_pan* */
#include "../addon/world_map/map_modal.inc"    /* add-on world_map: world_map_* */
/* ------------------------------------------------- 1DA2D the map (M) */
/*
 * The M map: opens the map window (its frame and the pixels under it saved), selects the party's map slot and clamps
 * the view to the map (xeen_map_begin). Each pass then redraws the scene (not on the first pass), clears the window,
 * draws the map view and the party's arrow, flips the arrow's blink after more than 5 ticks, writes the position
 * line (the map's name, X, Y and the facing) and shows the map window and the view, then polls the keys: Esc or a
 * click closes the map (the window and the screen restored); any other answer, or none, runs the next pass. After
 * the map the command loop goes on (combat's own command continuation in a fight).
 * Original: 17DC4 (96C4 = 0, 1DA2D, then 17C48: 96C4 = 1, 17F07); 1DA2D: 190ED, 1919B, [BP-2] = F47C x 2, 1871B(5),
 * the F494 slot, the clamps, 4EA0D; each pass 1B503(0) entered at its 4EA35 (no 4EA0D), 4E38C(5, "\r"), the view,
 * 4F6F8(5), the arrow; 4EA1C above 5 flips [BP-8] and runs 4EA0D; the 50AC line (8C53, X, Y, the 17A2 direction),
 * 50BF6(5), 50BF6(3), the 1E3C7 poll (F600 a click; F5FE keeps the answer); the close 18765(5), 18FFF.
 */
static xeen_status map_scene(xeen_game *g)
{
    g->panel_stage = MAP_SCENE;
    return scene_wait(g, 0, XEEN_TIMER_SCENE_MARK);
}
/* A pass's drawing (no poll yet: the poll session starts at its 189E3). */
static xeen_status map_pass(xeen_game *g, uint32_t boundary)
{
    g->panel_stage = MAP_DRAW;
    return service_wait(g, boundary);
}
/** Opens the full map view (1DA2D): saves the screen, draws the map and waits for a key. */
xeen_status map_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t r[4];
    xeen_presentation *p = g->presentation;
    if (!p) {
        return game_stop(g, MAP_STOP_NO_PRESENTATION);
    }
    xeen_presentation_trace_extra(
            "mapmark %x", (unsigned)xeen_read16(m, XEEN_DS_INPUT_CODE)); /* Evidence: 1DA2D */
    m->ds[XEEN_DS_MONSTERS_ENABLED] = 0;
    xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    button_defaults(m);
    g->map_active = 1;
    panel_window(g, XEEN_WINDOW_MAP, 1);
    if (xeen_presentation_window(p, 5, r)) {
        (void)xeen_text_frame(p->framebuffer, p->framebuffer_size, r);
    }
    if (xeen_map_begin(m, &g->map_view) != XEEN_OK) {
        return panel_stop(g, MAP_STOP_BEGIN);
    }
    if (map_pan(g)) {
        xeen_map_pan_open(&g->addons->map_pan, m,
                          &g->map_view); /* add-on: every open at the original view */
    }
    g->map_step = MAP_STEP_SAMPLE;
    return map_pass(g, MAP_FIRST_BOUNDARY);
}
/* The presentation takes at most XEEN_SPRITE_DRAW_MAX sprites per
 * submission (a native buffer); the original's view has no such limit (map
 * 1Bh's first pass alone is 530 draws: rd176b-0182 stopped at "1C680").
 * The same draws, in order, in submissions of at most that many. */
static xeen_status map_draw_all(xeen_presentation *p, const xeen_sprite_draw *d, size_t n)
{
    xeen_status s = XEEN_OK;
    while (s == XEEN_OK && n) {
        size_t k = n > XEEN_SPRITE_DRAW_MAX ? XEEN_SPRITE_DRAW_MAX : n;
        s = xeen_presentation_draw_sprites(p, d, k);
        d += k;
        n -= k;
    }
    return s;
}
/* Draws the map view: its window cleared, the map's draws (the framed part, the window's border, then the rest:
 * the party's arrow), the town icons and the world map passes of their add-ons. Stops without memory or when a draw
 * is refused. The map_movement add-on draws a panned view instead. Original: 1DB69..1E359. */
static xeen_status map_view(xeen_game *g)
{
    xeen_presentation *p = g->presentation;
    xeen_map_draws *d = malloc(sizeof *d);
    uint16_t r[4];
    xeen_status status;
    xeen_map_view shown;
    if (!d) {
        return panel_stop(g, MAP_STOP_NO_MEMORY);
    }
    menu_window_text(g, XEEN_WINDOW_MAP, WINDOW_TEXT_ONLY, ds_text(g, XEEN_TEXT_MAP_WINDOW_CLEAR));
    if (xeen_map_pan_shifted(map_pan(g))) {
        status = map_pan_scene(g, d, &shown); /* add-on: a panned view */
    } else {
        status = xeen_scene_map(&g->movement, &g->map_view, d);
        shown = g->map_view;
    }
    if (status == XEEN_OK && d->framed) {
        status = map_draw_all(p, d->draws, d->framed);
    }
    if (status == XEEN_OK && xeen_presentation_window(p, XEEN_WINDOW_MAP, r)) {
        (void)xeen_text_border(p->framebuffer, p->framebuffer_size, r);
    }
    if (status == XEEN_OK && d->count > d->framed) {
        status = map_draw_all(p, d->draws + d->framed, d->count - d->framed);
    }
    if (status == XEEN_OK && xeen_town_icons_on) {
        xeen_town_icons_map(&g->movement, shown.x, shown.y, p->framebuffer,
                            p->framebuffer_size); /* add-on town_icons */
    }
    if (status == XEEN_OK) {
        world_map_pass(g); /* add-on world_map */
    }
    free(d);
    return status == XEEN_OK ? XEEN_OK : panel_stop(g, MAP_STOP_DRAW);
}
/** Closes the map view: restores window 5 and pops the button set (18765(5), 18FFF). */
static xeen_status map_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    world_map_end(g); /* add-on world_map */
    panel_window(g, XEEN_WINDOW_MAP, 0);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    xeen_presentation_trace_extra("mapend");
    g->map_active = 0;
    g->panel_stage = 0;
    g->map_step = MAP_STEP_SAMPLE;
    if (map_pan(g)) {
        xeen_map_pan_close(map_pan(g)); /* add-on: the pan ends with the map */
    }
    m->ds[XEEN_DS_MONSTERS_ENABLED] = 1;
    return command_tail(g);
}
/** Resumes the map view's steps: its frame clock marked, the view drawn, the blink wait (above 5 ticks, compared
 * signed, the blink toggles and the clock is marked again), then the caption (the map's name, the party's x, y and
 * facing) shown, and the map's key poll. Stops when the caption does not fit or cannot be shown.
 * Original: 1DB56 (4EA0D), 1E35C (4EA1C; JLE), 1E36A (4EA0D). */
static xeen_status map_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    if (g->map_step == MAP_STEP_SAMPLE) { /* 1DB56 4EA0D */
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->map_step = MAP_STEP_VIEW;
    }
    if (g->map_step == MAP_STEP_VIEW) {
        if ((status = map_view(g)) != XEEN_OK) {
            return status;
        }
        g->map_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        g->map_step = MAP_STEP_BLINK_WAIT;
    }
    if (g->map_step == MAP_STEP_BLINK_WAIT) { /* 1E35C 4EA1C */
        if ((status = timer_ticks(g, &g->map_elapsed, bios, user)) != XEEN_OK) {
            return status;
        }
        ++g->location_tick_reads;
        g->location_ticks =
                g->timer_registers.ax; /* evidence: the 'loc-tick' log (adapter map-blink) */
        if ((int16_t)g->timer_registers.ax > 5) {
            g->map_view.blink ^= 1u;
            g->map_step = MAP_STEP_BLINK_SAMPLE;
        } /* JLE: signed */
        else {
            g->map_step = MAP_STEP_POSITION;
        }
    }
    if (g->map_step == MAP_STEP_BLINK_SAMPLE) { /* 1E36A 4EA0D */
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->map_step = MAP_STEP_POSITION;
    }
    if (g->map_step == MAP_STEP_POSITION) {
        char text[300];
        uint8_t *ds = m->ds;
        int n;
        xeen_presentation *p = g->presentation;
        n = snprintf(text, sizeof text, ds_text(g, XEEN_TEXT_MAP_CAPTION), ds_text(g, XEEN_DS_MAP_TEXTS),
                     xeen_signed_byte(ds[XEEN_DS_PARTY_X]), xeen_signed_byte(ds[XEEN_DS_PARTY_Y]),
                     far_text(g, XEEN_DS_DIRECTION_NAMES, ds[XEEN_DS_PARTY_DIRECTION]));
        if (n < 0 || (size_t)n >= sizeof text) {
            return panel_stop(g, MAP_STOP_TEXT);
        }
        menu_window_text(g, XEEN_WINDOW_MAP, WINDOW_TEXT_ONLY, text);
        if (xeen_presentation_update_window(p, XEEN_WINDOW_MAP) != XEEN_OK ||
            xeen_presentation_update_window(p, XEEN_WINDOW_VIEW_FRAME) != XEEN_OK) {
            return panel_stop(g, MAP_STOP_FRAME);
        }
        g->map_step = MAP_STEP_POLL;
    }
    return panel_set(g, MAP_POLL, MAP_POLL_BOUNDARY);
}
/** Polls a key for the map view (1E3C7), panning the map when the map_movement add-on is on. */
static xeen_status map_poll(xeen_game *g)
{
    uint16_t key = panel_poll(g);         /* 1E3C7 189E3 */
    int world = world_map_answer(g, key); /* add-on world_map: 0 = not its answer */
    if (world == 2) {
        return map_end(g);
    }
    if (world) {
        g->map_step = MAP_STEP_VIEW;
        return map_scene(g);
    }
    if (map_pan(g) && map_pan_answer(g, key)) {
        g->map_step = MAP_STEP_VIEW;
        return map_scene(g);
    }
    if (key == XEEN_KEY_ESCAPE || xeen_read16(&g->movement, XEEN_DS_MOUSE_CLICK)) {
        return map_end(g);
    }
    g->map_step = MAP_STEP_VIEW;
    return map_scene(g); /* 1E3D8 -> 1DB5B, SI = 0 */
}

/* --------------------------------------- 46BA0 Quick Reference */
/* One Quick Reference line for the member at POSITION (in combat the combat party's order, else the party's): its
 * row (10 pixels apart from 24), the number, the name, the class's first three letters, the level, HP, SP and armour
 * class each coloured against its base (the level against the unboosted level, HP and SP against their maxima, the
 * armour class against its base), and the worst condition's colour and first four letters.
 * Original: 46BDB: DS:82F1 per member (FAF8 2: 971F through 9720; else ED8C); SI x 10 + 24; 286A; 2C909 against +23h
 * (46B68); 2AAC2; 46470; 4660F (4660F(1) the base); 2AB96. */
static int qref_line(xeen_game *g, unsigned position, char *out, size_t size)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    unsigned index = ds[XEEN_DS_COMBAT_MODE] == 2u ? ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + position)] : position;
    const uint8_t *ch = ds + XEEN_PARTY_RECORDS + index * XEEN_CHARACTER_BYTES;
    unsigned cond = xeen_character_worst_condition(ch);
    const char *cls = far_text(g, XEEN_DS_CLASS_NAMES, ch[XEEN_CHARACTER_CLASS]),
               *cn = far_text(g, XEEN_DS_CONDITION_NAMES, cond);
    int level = ch[XEEN_CHARACTER_LEVEL] + ch[XEEN_CHARACTER_LEVEL_BONUS],
        hp = (int16_t)xeen_get_le16(ch + XEEN_CHARACTER_HP), ac = xeen_combat_armor_class(m, ch, 0);
    unsigned sp = (uint16_t)xeen_get_le16(ch + XEEN_CHARACTER_SP);
    int n;
    if (level < 0) {
        level = 0;
    }
    n = snprintf(out, size, ds_text(g, XEEN_TEXT_QUICK_REFERENCE_LINE), (int)(position * 10u + 24u), position + 1u, (const char *)ch,
                 cls[0], cls[1], cls[2], view_colour(level, ch[XEEN_CHARACTER_LEVEL]),
                 (unsigned)level, view_colour(hp, xeen_combat_max_hp(m, ch)), hp,
                 view_colour((int)sp, xeen_combat_max_sp(m, ch)), sp,
                 view_colour(ac, xeen_combat_armor_class(m, ch, 1)), (unsigned)ac,
                 (unsigned)ds[XEEN_DS_CONDITION_COLOURS + cond], cn[0], cn[1], cn[2], cn[3]);
    return n >= 0 && (size_t)n < size;
}
/* Quick Reference: the button tables saved, the cursor, the default buttons; the members' lines, then the party's
 * gold, gems and food in days (the food / the party size / 3, "day" or "days"); the upper screen opened if it is not
 * open, the text written and shown; then its key poll (no scene).
 * Original: 46BA0: 190ED, 4DB3B(F460, 0), the 800-byte line area, 1919B; AC0F = DS:8358 with F00A, F00E and EFF6 /
 * ED8C / 3; 1871B(18h) when 9884 (window 18h's open flag) is clear, 4E38C(18h), 50BF6(18h), 189D7; the poll 46E31. */
xeen_status qref_begin(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char lines[8][100], out[1200];
    unsigned position, count, food;
    int n;
    if (!g->presentation) {
        return game_stop(g, g->combat_ui_site ? g->combat_ui_site : QREF_STOP_NO_PRESENTATION);
    }
    xeen_presentation_trace_extra("mark dialog-qref"); /* Evidence: 46BA0 */
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    (void)xeen_presentation_select_cursor(g->presentation, xeen_read16(m, XEEN_DS_MOUSE_SPRITES), 0);
    memset(lines, 0, sizeof lines);
    button_defaults(m);
    count = ds[XEEN_DS_COMBAT_MODE] == 2u ? ds[XEEN_DS_COMBAT_PARTY_COUNT] : ds[XEEN_DS_PARTY_COUNT];
    for (position = 0; position < count; ++position) {
        if (position >= 8u) {
            return panel_stop(g, QUICK_REFERENCE_STOP_MEMBER_LINE);
        }
        if (!qref_line(g, position, lines[position], sizeof lines[position])) {
            return panel_stop(g, QUICK_REFERENCE_STOP_MEMBER_LINE);
        }
    }
    if (!ds[XEEN_DS_PARTY_COUNT]) {
        return panel_stop(g, QUICK_REFERENCE_STOP_NO_PARTY);
    }
    food = (unsigned)xeen_read16(m, XEEN_DS_PARTY_FOOD) / ds[XEEN_DS_PARTY_COUNT] / 3u;
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_QUICK_REFERENCE_CHART), lines[0], lines[1], lines[2], lines[3],
                 lines[4], lines[5], lines[6], lines[7],
                 (unsigned long)xeen_ds_read32(m, XEEN_DS_PARTY_GOLD),
                 (unsigned long)xeen_ds_read32(m, XEEN_DS_PARTY_GEMS), food,
                 ds_text(g, food == 1u ? XEEN_TEXT_EMPTY : XEEN_TEXT_PLURAL));
    if (n < 0 || (size_t)n >= sizeof out || XEEN_DS_TEXT_BUFFER_2 + (unsigned)n + 1u > XEEN_PARTY_RECORDS) {
        return panel_stop(g, QUICK_REFERENCE_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    g->qref_opened = !ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_UPPER_SCREEN)];
    if (g->qref_opened) {
        panel_window(g, XEEN_WINDOW_UPPER_SCREEN, 1);
    }
    menu_window_text(g, XEEN_WINDOW_UPPER_SCREEN, g->qref_opened, out);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_UPPER_SCREEN);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    return panel_set(g, QREF_POLL, QREF_POLL_BOUNDARY);
}
/* Quick Reference ends at any key: its window closes when it opened it, the input is cleared, the buttons come
 * back, and it returns to whoever asked: combat (its modal's site), the character view (the view's interface mode
 * and its redraw; the view's window was already open, so it stays), or exploration's command tail.
 * Original: 46E3A: 18765(18h) when 46BA0 opened it, 189BC, 18FFF; then combat's 392D9, exploration's 17F07, or the
 * character view's 44DFB (9A1C = 0Ah, 44BB6; 9884 set: 46BA0 neither opened nor closes window 18h). */
static xeen_status qref_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint32_t site = g->combat_ui_site;
    if (g->qref_opened) {
        panel_window(g, XEEN_WINDOW_UPPER_SCREEN, 0);
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    xeen_button_stack(m->ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    g->panel_stage = 0;
    g->combat_ui_site = 0;
    g->qref_opened = 0;
    if (g->qref_view) { /* The view's 44DFB */
        g->qref_view = 0;
        g->combat_ui_site = g->qref_view_site;
        m->ds[XEEN_DS_UI_MODE] = 0x0a;
        return view_redraw(g);
    }
    if (site) {
        return combat_modal_return(g, site, 0);
    }
    return command_tail(g);
}

/* --------------------------------- 3E9B6 Quick Fight Options */
/* The Quick Fight Options dialog's scene: the frame wait returns to combat_ui_after_scene.
 * Original: 3EAA8 4EA0D, 1B503(1). */
static xeen_status qfo_scene(xeen_game *g)
{
    g->panel_stage = QFO_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/* The Quick Fight Options page: the member's name and the name of their quick-fight option in the right-hand
 * window (framed), its buttons, shown; then its scene.
 * Original: 3EA16: AC0F = DS:7170 of the name and the 2964 option names (record +A5h), 4E38C(0Ah); the buttons
 * 98AC.. with 9898 set (4AC92 of 9896, frame 2n), 50BF6(0Ah). */
static xeen_status qfo_redraw(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    char out[300];
    int n;
    xeen_presentation_trace_extra("mark dialog-options"); /* Evidence: 3EA16 */
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_QUICKFIGHT_OPTIONS), ds_text(g, g->qfo_record),
                 far_text(g, XEEN_DS_QUICK_FIGHT_NAMES, ds[(uint16_t)(g->qfo_record + XEEN_CHARACTER_QUICK_FIGHT)]));
    if (n < 0 || (size_t)n >= sizeof out) {
        return panel_stop(g, QUICK_FIGHT_STOP_TEXT);
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_FRAMED, out);
    location_buttons(g);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
    return qfo_scene(g);
}
/* Quick Fight Options for member RECORD: the routing pushed, train.icn loaded as the buttons' sprites, the Esc/T
 * button tables, the right-hand window opened, then its frames. Original: 3E9B6(record) (190ED, 190A3(train.icn):
 * 96C1 up, 95A8, 98D4; the buttons 98AC.. = DS:1A3C, 1A42, 1A44, 1A46, 1A48, 1A4A; 9896 = 98D4; 1871B(0Ah)),
 * then 3EA16. */
static xeen_status qfo_begin(xeen_game *g, uint16_t record)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint16_t h = 0;
    if (!g->presentation) {
        return game_stop(g, QUICK_FIGHT_STOP_NO_PRESENTATION);
    }
    xeen_button_stack(ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
    if (!handle_push(g, ds_text(g, XEEN_TEXT_QUICKFIGHT_SPRITES), &h)) {
        ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(ds[XEEN_DS_HANDLE_DEPTH] - 1u);
        return panel_stop(g, QUICK_FIGHT_STOP_SPRITES);
    }
    button_tables(m, XEEN_BUTTONS_ESC_T);
    xeen_write16(m, XEEN_DS_BUTTON_SPRITES, h);
    g->qfo_record = record;
    panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
    return qfo_redraw(g);
}
/* Quick Fight Options end at Enter or Esc: the window and train.icn go, the routing is restored, the key cleared;
 * combat's command loop follows. Original: 3EB47 (18765(0Ah), 190CF, 18FFF, F5FE = 0), combat's 392D9. */
static xeen_status qfo_end(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint32_t site = g->combat_ui_site;
    window_pop(g, XEEN_WINDOW_RIGHT_COLUMN);
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    g->panel_stage = 0;
    g->combat_ui_site = 0;
    return combat_modal_return(g, site, 0);
}
/* The 3EAC7 keys (CS:0923): Enter, Esc end; N and T advance +A5 ((+A5 + 1)
 * mod 4, signed) and redraw; F1..F6 below 971F take the ROSTER member F5FE
 * - C9h (not 9720's: the original indexes B41E directly), 2A1F6 of it,
 * and redraw; any other key loops. */
static xeen_status qfo_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (key == XEEN_KEY_ENTER || key == XEEN_KEY_ESCAPE) {
        return qfo_end(g);
    }
    if (key == 'N' || key == 'T') {
        uint8_t *v = ds + (uint16_t)(g->qfo_record + XEEN_CHARACTER_QUICK_FIGHT);
        *v = (uint8_t)(*v + 1u);
        *v = (uint8_t)((int)(int8_t)*v % 4);
        return qfo_redraw(g);
    }
    if (key >= XEEN_KEY_F1 && key <= XEEN_KEY_F6) {
        unsigned n = key - XEEN_KEY_F1; /* 3EB17: SI - C9h; F5FE keeps the key */
        if (ds[XEEN_DS_COMBAT_PARTY_COUNT] > n) {
            g->qfo_record = XEEN_PARTY_RECORD(n);
            portrait_highlight(g, n);
        }
        return qfo_redraw(g);
    }
    return qfo_scene(g);
}

/* ------------------------------- 387F4 Quick Fight's 3D8C1(1) */
/* The combat quick cast's end: the cast's state restored (the monsters enabled, the UI mode and the countdown back,
 * the key cleared; there is no window or icon to free), then combat's quick-fight return with AX whether a spell
 * was cast. Original: 3E12F with argument 1: 96C4 = 1, 9A1C and 96C0 back, F5FE = 0, AX = [BP-6]. */
xeen_status quick_cast_end(xeen_game *g)
{
    cast_restore(g);
    g->cast_quick = 0;
    g->combat_ui_site = 0;
    return combat_modal_return(g, XEEN_COMBAT_MODAL_QUICK_FIGHT, (uint16_t)g->cast_done);
}
/*
 * The quick fight's spell (the combat modal XEEN_COMBAT_MODAL_QUICK_FIGHT): the acting member casts the ready spell
 * at once, without the Cast dialog. The ready spell is
 * looked up in the member's class spell list without the dialog's checks (no "none ready" test, no refusal of a
 * member who cannot cast): a ready spell of FFh indexes past the list, as in the original. Payment follows the
 * dialog's: a member short of spell points or gems ends the cast silently (AX 0, no message). The spells that only
 * work outside combat are refunded and the cast ends, also silently; any other spell runs its handler.
 * Original: 3D8C1(1) (96C4 = 96E1 = 0, [BP-4] = 9A1C, [BP-8] = 96C0, 96C0 = 0); the caster 9720[97FF], the class row
 * CS:28EF and 3DB9D at once (no FFh test, no 28CFC refusal); the ready spell DS:201E[row x 28h + A4h]; 39C16's
 * payment (a lack: 3E07F(1), AX 0); [BP-6] = 1, 9800, 9A1C and the handler; the refund 39C8B and AX 0 at 3DCA4.
 */
static xeen_status quick_cast(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint8_t actor = ds[XEEN_DS_TURN_ACTOR];
    uint16_t rec;
    unsigned spell;
    if (!play_hosted(g)) {
        return game_stop(g, QUICK_CAST_STOP_NO_HOST);
    }
    if (actor >= ds[XEEN_DS_COMBAT_PARTY_COUNT] || ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + actor)] >= 6u) {
        return game_stop(g, QUICK_CAST_STOP_ACTOR);
    }
    cast_entry_reset(g);
    rec = XEEN_PARTY_RECORD(ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + actor)]);
    g->cast_record = rec;
    g->cast_index = actor;
    g->cast_active = 1;
    g->cast_quick = 1;
    spell = ds[(uint16_t)(XEEN_DS_CLASS_SPELL_LISTS +
                          XEEN_CLASS_SPELL_LIST * cast_category(ds[(uint16_t)(rec + XEEN_CHARACTER_CLASS)]) +
                          ds[(uint16_t)(rec + XEEN_CHARACTER_READY_SPELL)])];
    if (cast_pay(g, rec, spell, 0)) {
        return quick_cast_end(g);
    }
    cast_engage(g, rec, spell);
    /* In combat these eight spells are refunded (cast_pay with refund, 39C8B) and the quick cast ends. */
    if (g->cast_mode == XEEN_UI_MODE_COMBAT && exploration_only_spell(spell)) {
        (void)cast_pay(g, rec, spell, 1);
        g->cast_done = 0;
        return quick_cast_end(g);
    }
    return cast_dispatch(g, spell);
}

/* ------------------------------------------------- dispatch */
/**
 * Opens the combat dialog the command poll asked for at SITE (enum xeen_combat_modal): the control panel, Info, Quick
 * Reference, the acting member's Options (3E9B6 with 9720[97FF]'s record; no such member: the native boundary), a
 * member's view, or the quick cast; another site stops the game there. The dialog's end returns through
 * combat_modal_return.
 */
xeen_status combat_modal(xeen_game *g, uint32_t site)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    uint8_t actor = ds[XEEN_DS_TURN_ACTOR];
    g->combat_ui_site = site;
    switch (site) {
        case XEEN_COMBAT_MODAL_CONTROL_PANEL:
            g->panel_combat = 1;
            return panel_begin(g);
        case XEEN_COMBAT_MODAL_INFO:
            return info_begin(g);
        case XEEN_COMBAT_MODAL_QUICK_REFERENCE:
            return qref_begin(g);
        case XEEN_COMBAT_MODAL_OPTIONS: /* 3E9B6(9720[97FF]'s record) */
            if (actor >= ds[XEEN_DS_COMBAT_PARTY_COUNT] ||
                ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + actor)] >= 6u) {
                return game_stop(g, COMBAT_STOP_MODAL_ACTOR);
            }
            return qfo_begin(g, XEEN_PARTY_RECORD(ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + actor)]));
        case XEEN_COMBAT_MODAL_VIEW_MEMBER:
            return view_begin(g, xeen_read16(m, XEEN_DS_INPUT_CODE), VIEW_FROM_COMBAT);
        case XEEN_COMBAT_MODAL_QUICK_FIGHT:
            return quick_cast(g);
        default:
            g->combat_ui_site = 0;
            return game_stop(g, site);
    }
}
/** Continues a combat dialog's stage after its scene was shown. */
xeen_status combat_ui_after_scene(xeen_game *g)
{
    switch (g->panel_stage) {
        case INFO_SCENE:
            return info_after_scene(g);
        case QFO_SCENE:
            return panel_set(g, QFO_POLL, QFO_POLL_BOUNDARY);
        case MAP_SCENE:
            return map_pass(g, MAP_PASS_BOUNDARY); /* RETF to 1DB66 */
        case DISMISS_SCENE:
            return dismiss_after_scene(g); /* RETF to 2B804 */
        default:
            return panel_stop(g, COMBAT_UI_STOP_SCENE_STAGE);
    }
}
/** Resumes a combat dialog's poll or wait at its current stage. */
xeen_status combat_ui_resume(xeen_game *g, xeen_timer_bios bios, void *user)
{
    uint16_t key;
    switch (g->panel_stage) {
        case INFO_POLL: /* 2A012 */
            key = panel_poll(g);
            return key ? info_end(g) : info_scene(g);
        case QREF_POLL: /* 46E31 */
            key = panel_poll(g);
            return key ? qref_end(g) : XEEN_SERVICE_REQUIRED;
        case QFO_POLL: /* 3EAB5 */
            key = panel_poll(g);
            return key ? qfo_key(g, key) : qfo_scene(g);
        case MAP_DRAW:
            return map_resume(g, bios, user); /* 1DB56/1DB69..1E3C2 */
        case MAP_POLL:
            return map_poll(g);
        case DISMISS_POLL:      /* 2B844 */
            key = panel_poll(g);
            return key ? dismiss_key(g, key) : dismiss_scene(g);
        default:
            return XEEN_BAD_INPUT;
    }
}
