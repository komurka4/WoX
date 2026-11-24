/*
 * The treasure dialog: "The Party Found" in the right-hand column with the gold and gems, then each found item
 * handed to a member line by line (each with its short waits), the "BackPacks Full" scroll when no pack has room,
 * the closing key and the return to whoever opened it (a volley, a join of the step's tails, the combat cleanup or
 * a spell's volley). The rules (placing items, the settlement) are the treasure library's (treasure.c).
 * Original: 2EA68 (its loop 2F170 and keys 2F196; the item wait 30E05), entered from 17ACC, 17AF7, the tile event,
 * the movement post-action and 394C5 (Phase 246 moved it from game.c).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when a treasure cannot be taken; each value is the original address of the step
 * (tools/xeen_replay/stops.json).
 */
enum {
    TREASURE_STOP_NO_HOST = 0x2ea68,         /* no presentation, or a modal or dialogue already open */
    TREASURE_STOP_OPEN = 0x2ea68,            /* the treasure could not be opened */
    TREASURE_STOP_DIALOG_OPEN = 0x2ea68,     /* the event's text could not be kept for the dialog */
    TREASURE_STOP_TEXT = 0x2ebfb,            /* the treasure's message does not fit */
    TREASURE_STOP_PACKS_FULL_SCROLL = 0x2ec44,
    TREASURE_STOP_ITEM = 0x2ec54,            /* the next item could not be placed */
    TREASURE_STOP_ITEM_SOUND = 0x2eeab,
    TREASURE_STOP_FOUND_TEXT = 0x2f0fe,      /* "... found ..." does not fit */
    TREASURE_STOP_KEY_SOUND = 0x1880e,
    TREASURE_STOP_JOIN = 0x2ea8e,             /* a treasure to join without prepared monsters or without a dialog */
    /* variable stop sites (stops_variable.json) */
    TREASURE_STOP_COMBAT_SCENE = 0x30e14,    /* the combat scene after an item line */
    TREASURE_STOP_FINISH = 0x39430,          /* the fight's finish after its treasure did not end the fight */
    TREASURE_STOP_PREFIX = 0x2eb9c           /* the treasure's prefix (sound and counts) did not finish */
};
static xeen_status treasure_dialog_open(xeen_game *);
/* Opens the treasure dialog for OWNER (enum treasure_owner) once the treasure's prefix has run: the found text and
 * the treasure UI mode (xeen_treasure_open: combat keeps its mode), the right-hand column window closed and opened
 * again; then, when no member's pack has a free last row, the found text again and the "BackPacks Full" message in a
 * scroll (the button set pushed now and popped when the scroll closes, treasure_full_closed), else the dialog itself.
 * Each pass of its loop runs a scene before the key poll (combat's own scene in combat). The event's text in the
 * text buffer is kept and restored; only the settlement's item names replace it, as in the original.
 * Stops: no presentation or a modal or dialogue already open, the treasure not opening, a text that does not fit.
 * Original: 2EA68 after 2EB9C (owners: 1 the volley 287FF, resumed at 17EF4; 2 a join, at 4EA0D; 3 combat, mode 2
 * kept; 4 a spell's volley, at 3D8C1's 3E0A9); 2EBFB, 2EC30 (9A1C = 7), 2EC05/2EC0D (window 10), 2EC37..2EC49 the
 * full packs' 18F2F(DS:6183, 1); the loop 2F170, the poll 2F17D. */
xeen_status treasure_dialog(xeen_game *g, int owner)
{
    if (!g->presentation || g->modal.active || g->dialogue_window.active) {
        return game_stop(g, TREASURE_STOP_NO_HOST);
    }
    if (xeen_treasure_open(&g->movement) != XEEN_OK) {
        return game_stop(g, TREASURE_STOP_OPEN);
    }
    xeen_window_close(&g->movement, XEEN_WINDOW_RIGHT_COLUMN);
    xeen_window_open(&g->movement, XEEN_WINDOW_RIGHT_COLUMN);
    g->treasure.owner = owner;
    if (xeen_treasure_packs_full(&g->movement)) {
        /* 2EC37..2EC49: every pack full: 18F2F(DS:6183, 1), the
         * "BackPacks Full" scroll (the 4B7A frame), then the item loop
         * (whose 2EC59 checks discard the items). in combat
         * (owner 3, 9A1C 2 kept by 2EC29) the same 18F2F: 9A1C FFh during
         * its 18FA1 passes, each scene the combat-side 1B503(1) (the
         * treasure owner's scene branch), 9A1C 2 again at 18FF2. */
        char text[200];
        int n;
        xeen_movement *m = &g->movement;
        /* The original's AC0F still holds 2EBFB's "The Party Found" here (its "Hit a key" comes at 2F14A, after the
         * item loop), but xeen_treasure_open has written both of the dialog's texts at once: the found text is written
         * again, for the full-packs window to show. The window's 190ED push (the modal's, below) is already made:
         * pushed here, popped before the modal pushes it. */
        n = xeen_treasure_found_text(m);
        if (n < 0 || n >= 1000) {
            return game_stop(g, TREASURE_STOP_TEXT);
        }
        n = snprintf(text, sizeof text, ds_text(g, XEEN_DS_MESSAGE_FORMAT), ds_text(g, XEEN_TEXT_BACKPACKS_FULL));
        if (n >= 0 && (size_t)n < sizeof text) {
            treasure_full_window(g); /* 2EC0D..2EC23 first */
        }
        xeen_button_stack(m->ds, XEEN_BUTTON_PUSH, XEEN_DS_SEGMENT);
        if (n < 0 || (size_t)n >= sizeof text ||
            !open_scroll_message(g, SCROLL_PACKS_FULL, text)) {
            return game_stop(g, TREASURE_STOP_PACKS_FULL_SCROLL);
        }
        return XEEN_SERVICE_REQUIRED;
    }
    return treasure_dialog_open(g);
}
/* The "BackPacks Full" scroll closed: its button push popped (the dialog's modal pushes again), then the dialog.
 * Original: 2EC49. */
xeen_status treasure_full_closed(xeen_game *g)
{
    xeen_button_stack(g->movement.ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    return treasure_dialog_open(g);
}
/* The dialog proper: its modal opened through the event service (the text buffer kept around it), then the first
 * item line when an item is pending (item_next), else "Hit a key" and the wait for the closing key. Stops when the
 * modal does not open. Original: 2EC4C on (the event service 2EA68), 2F14A, the loop 2F170. */
static xeen_status treasure_dialog_open(xeen_game *g)
{
    xeen_event_request request = {0};
    uint16_t ax = 0;
    int owner = g->treasure.owner;
    request.address = XEEN_REQUEST_TREASURE_DIALOG;
    memcpy(g->event_text, g->movement.ds + XEEN_DS_TEXT_BUFFER, sizeof g->event_text);
    g->treasure.staged = 1;
    if (event_service(g, &request, g->movement.ds, sizeof g->movement.ds, &ax) != 0 ||
        !g->ui.pending || !g->modal.active) {
        g->treasure.staged = 0;
        memcpy(g->movement.ds + XEEN_DS_TEXT_BUFFER, g->event_text, sizeof g->event_text);
        return game_stop(g, TREASURE_STOP_DIALOG_OPEN);
    }
    memcpy(g->movement.ds + XEEN_DS_TEXT_BUFFER, g->event_text, sizeof g->event_text);
    g->treasure.staged = 0;
    g->treasure.owner = owner;
    g->modal_scene_ready = 0;
    g->modal_poll_pending = MODAL_POLL_START;
    g->treasure.items_staged = 0;
    if (xeen_treasure_has_items(&g->movement)) {
        g->treasure.items_staged = 1;
        g->treasure.item_cursor = 0;
        return item_next(g);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, ds_text(g, XEEN_TEXT_HIT_A_KEY));
    treasure_window(g);                       /* 2F15A 4E38C(AC0F, 0Ah), 50BF6(0Ah) */
    return input_wait(g, TREASURE_KEY_POLL_BOUNDARY);
}
/* Writes the text scratch into the right-hand column (text only, no frame) and presents it. Original: 4E38C(AC0F,
 * 0Ah), 50BF6(0Ah). */
void treasure_window(xeen_game *g)
{
    menu_window_text(g, XEEN_WINDOW_RIGHT_COLUMN, WINDOW_TEXT_ONLY, (const char *)g->movement.ds + XEEN_DS_TEXT_BUFFER_2);
    (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_RIGHT_COLUMN);
}
/* Starts the item lines' wait of TICKS timer ticks, then STAGE: each pass polls a key (which ends it) and draws a
 * scene; xeen_game_resume_item_wait runs it. Original: 30E05(n): 4EA0D, then 189E3, 1B503(1) and 4EA1C. */
static xeen_status item_wait(xeen_game *g, int stage, int ticks)
{
    g->treasure.item_stage = stage;
    g->treasure.item_wait = ticks;
    g->treasure.item_elapsed = (xeen_timer_elapsed_state){0, 0};
    return service_wait(g, ITEM_WAIT_BOUNDARY);
}
/* The next found item: placed with the member the player chose preferred (xeen_treasure_place_next); with none left,
 * "Hit a key" and the wait for the closing key. Otherwise sound 20 plays, the six blank item lines are shown and,
 * after a one-tick wait, the item's "found" line (item_found). Stops: a bad party, the sound.
 * Original: 2EC54..2F0E8 (2ED15's preference 57C9:0008; FX 14h at 2EEA9, DS:61BD in window 10, 30E05(1)); 2F14A
 * DS:61FC and the loop 2F170. */
xeen_status item_next(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    int r;
    r = xeen_treasure_place_next(m, g->who_will_member, &g->treasure.item_cursor, &g->treasure.item_member,
                                 &g->treasure.item_category); /* 2ED15's 57C9:0008 */
    if (r < 0) {
        return game_stop(g, TREASURE_STOP_ITEM);
    }
    if (!r) {
        ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, ds_text(g, XEEN_TEXT_HIT_A_KEY));
        treasure_window(g);
        g->treasure.item_stage = ITEM_LINE_NONE;
        return input_wait(g, TREASURE_KEY_POLL_BOUNDARY);
    }
    if (sound_effect(g, 20u) != XEEN_OK) {
        return game_stop(g, TREASURE_STOP_ITEM_SOUND);
    }
    ds_put_text(g, XEEN_DS_TEXT_BUFFER_2, ds_text(g, XEEN_TEXT_TREASURE_ITEM_LINES));
    treasure_window(g);
    return item_wait(g, ITEM_LINE_BLANKS, 1);
}
/* "%s found: %s" (the recipient and the item) in the right-hand column, the recipient's items compacted, then a
 * ten-tick wait. Original: 2F0EC..2F131 (AC0F = DS:61E5 of the name and A827, window 10, 43428(recipient,
 * category), 30E05(10)). */
static xeen_status item_found(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    char out[1000];
    int n;
    n = snprintf(out, sizeof out, ds_text(g, XEEN_TEXT_TREASURE_FOUND),
                 ds_text(g, XEEN_PARTY_RECORD(g->treasure.item_member)),
                 ds_text(g, XEEN_DS_TEXT_BUFFER));
    if (n < 0 || (size_t)n >= sizeof out) {
        return game_stop(g, TREASURE_STOP_FOUND_TEXT);
    }
    memcpy(m->ds + XEEN_DS_TEXT_BUFFER_2, out, (size_t)n + 1u);
    treasure_window(g);
    xeen_treasure_compact(m, g->treasure.item_member, g->treasure.item_category);
    return item_wait(g, ITEM_LINE_FOUND, 10);
}
xeen_status xeen_game_resume_item_wait(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    xeen_movement *m;
    if (!g || g->phase != XG_STOPPED || !g->treasure.item_stage || !bios ||
        (g->lifecycle_boundary != ITEM_WAIT_BOUNDARY && g->lifecycle_boundary != ITEM_WAIT_POLL_BOUNDARY &&
         g->lifecycle_boundary != ITEM_WAIT_TICK_BOUNDARY)) {
        return XEEN_BAD_INPUT;
    }
    m = &g->movement;
    if (g->lifecycle_boundary == ITEM_WAIT_BOUNDARY) { /* 30E08: 4EA0D */
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->lifecycle_boundary = ITEM_WAIT_POLL_BOUNDARY;
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->lifecycle_boundary == ITEM_WAIT_POLL_BOUNDARY) {
        /* 30E0D: 189E3 (F5FE = the key or 0), then 1B503(1): the combat-side
         * semantic scene in combat (as the 2F170 loop's), else the whole
         * scene entered at its 4EA35, back at 30E1A through 1C1B3. */
        uint16_t key = 0;
        xeen_presentation_trace("poll");
        if (!g->input_provider.keyboard ||
            !g->input_provider.keyboard(g->input_provider.user, &key)) {
            return XEEN_BAD_INPUT;
        }
        xeen_write16(m, XEEN_DS_INPUT_CODE, key);
        if (m->ds[XEEN_DS_COMBAT_MODE] == 2u) {
            uint32_t boundary = 0;
            if (xeen_combat_scene(&g->combat, m, g->monster_database, g->monster_database_size,
                                  &boundary) != XEEN_OK) {
                return game_stop(g, boundary ? boundary : TREASURE_STOP_COMBAT_SCENE);
            }
            ++g->scene_returns;
            g->lifecycle_boundary = ITEM_WAIT_TICK_BOUNDARY;
            return XEEN_SERVICE_REQUIRED;
        }
        g->treasure.item_scene = 1;
        g->scene_argument = 1;
        g->lifecycle_boundary = XEEN_TIMER_SCENE_MARK;
        return XEEN_SERVICE_REQUIRED;
    }
    /* 30E1A: 4EA1C against the tick target (JGE: signed), and F5FE. */
    if (!g->treasure.item_elapsed.entry) {
        g->treasure.item_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    }
    if ((status = timer_ticks(g, &g->treasure.item_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    g->treasure.item_elapsed = (xeen_timer_elapsed_state){0, 0};
    ++g->treasure.item_passes;
    g->treasure.item_ticks = g->timer_registers.ax;
    if ((int16_t)g->timer_registers.ax < g->treasure.item_wait && !xeen_read16(m, XEEN_DS_INPUT_CODE)) {
        g->lifecycle_boundary = ITEM_WAIT_POLL_BOUNDARY;
        return XEEN_SERVICE_REQUIRED;
    }
    return g->treasure.item_stage == ITEM_LINE_BLANKS ? item_found(g) : item_next(g);
}
/* The closing key COMMAND: the settlement through the event service (the gold and gems added, any item still pending
 * collected, the window closed), sound 20 once for each item placed, the UI mode back to exploration unless in
 * combat, then the owner's continuation: the volley's or the spell volley's end, the rest of the fight's cleanup, or
 * the join's scene wait. A settlement the service refuses keeps the event's text and waits at the treasure call.
 * Stops: the sound, a fight the treasure did not end. Original: 2F196 (2EA68's service, xeen_treasure_collect;
 * 2EEA9's FX 20 per recipient), 2F19D (9A1C = 1 unless 2); the combat cleanup's rest of 39430 and 38F5F. */
xeen_status treasure_key(xeen_game *g, uint16_t command)
{
    xeen_event_request request = {0};
    uint16_t ax = 0;
    int handled, owner = g->treasure.owner;
    unsigned i;
    request.address = XEEN_REQUEST_TREASURE_DIALOG;
    g->response = command;
    g->response_ready = 1;
    handled = event_service(g, &request, g->movement.ds, sizeof g->movement.ds, &ax);
    if (handled != 1) {
        memcpy(g->movement.ds + XEEN_DS_TEXT_BUFFER, g->event_text, sizeof g->event_text);
    }
    /* 2EEA9 requests FX 20 for every recipient pass (a discarded item at
     * 2EC7E plays none). */
    if (handled == 1) {
        for (i = 0; i < g->treasure.placed; ++i) {
            if (sound_effect(g, 20u) != XEEN_OK) {
                return game_stop(g, TREASURE_STOP_KEY_SOUND);
            }
        }
    }
    if (handled != 1) {
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = XEEN_TREASURE_CALL;
        g->command_status = XGC_INTERNAL;
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->movement.ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_COMBAT) {
        g->movement.ds[XEEN_DS_UI_MODE] = XEEN_UI_MODE_EXPLORATION;
    }
    g->treasure.owner = TREASURE_NONE;
    if (owner == TREASURE_VOLLEY) {
        return shoot_after_volley(g);
    }
    if (owner == TREASURE_SPELL_VOLLEY) {
        return cast_volley_done(g);
    }
    if (owner == TREASURE_COMBAT) {
        uint32_t boundary = 0;
        if (xeen_combat_finish_treasure(&g->combat, &g->movement, &boundary) !=
                    XEEN_SERVICE_REQUIRED ||
            boundary != XEEN_COMBAT_OVER) {
            return game_stop(g, boundary ? boundary : TREASURE_STOP_FINISH);
        }
        return after_combat(g, XEEN_SERVICE_REQUIRED);
    }
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/* The treasure dialog from a join of the step's tails when xeen_treasure_check says so: the prefix (its sound and
 * counts), then the dialog. Stops without prepared monsters or a treasure to show, or when the prefix fails.
 * Original: 2EA68 from 17ACC, 17AF7, the tile-event return or the movement post-action: 2EB9C; on at 4EA0D. */
xeen_status join_treasure(xeen_game *g)
{
    uint32_t boundary = 0;
    if (!g->monsters_prepared || xeen_treasure_check(&g->movement) != XEEN_TREASURE_DIALOG) {
        return game_stop(g, TREASURE_STOP_JOIN);
    }
    if (xeen_combat_treasure_prefix(
                &g->combat, &g->movement, g->monster_database, g->monster_database_size,
                game_audio(g), &boundary) != XEEN_OK) {
        return game_stop(g, boundary ? boundary : TREASURE_STOP_PREFIX);
    }
    return treasure_dialog(g, TREASURE_JOIN);
}
/* The treasure dialog from the combat cleanup (whose scene and treasure gate have run): the prefix, then the dialog
 * in combat's mode. Original: 394C5, 2EA68 in mode 2: 2EB9C. */
xeen_status combat_treasure(xeen_game *g)
{
    uint32_t boundary = 0;
    if (xeen_combat_treasure_prefix(
                &g->combat, &g->movement, g->monster_database, g->monster_database_size,
                game_audio(g), &boundary) != XEEN_OK) {
        return game_stop(g, boundary ? boundary : TREASURE_STOP_PREFIX);
    }
    return treasure_dialog(g, TREASURE_COMBAT);
}
