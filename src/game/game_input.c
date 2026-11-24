/*
 * The input polls (Phase 246, from game.c): xeen_game_poll_input and the owner of each wait it serves
 * (poll_wait_owner), the combat command poll (3912C) and the exploration loop's 17B09 poll and its interactions.
 * The modal waits' polls (the yes/no, the speech loop, the NPC dialogue, the modal loop) are game_modal_poll.c's.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Combat's wait for a command, without a key: the first poll marks the frame clock; then each tick without one
 * polls again, and a tick marks the clock anew and runs the monsters' idle scene (xeen_combat_idle with the idle
 * count since the loop's top; its fifth scene's sound). A stop of the idle scene is reported at its boundary.
 * Original: 39131's 4EA1C; 390D2: 4EA0D, 1B503(1) (the SIth idle scene).
 */
static xeen_status combat_idle_tick(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint32_t boundary;
    if (!g->combat_idle_armed) {
        status = frame_base(g, bios, user);
        if (status != XEEN_OK) {
            return status;
        }
        g->combat_idle_armed = 1;
        g->combat_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
        return XEEN_SERVICE_REQUIRED;
    }
    if ((status = timer_ticks(g, &g->combat_elapsed, bios, user)) != XEEN_OK) {
        return status;
    }
    g->combat_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
    if (!g->timer_registers.ax) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = frame_base(g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    status = xeen_combat_idle(&g->combat, &g->movement, g->monster_database,
                              g->monster_database_size, g->event_random, g->event_random_user,
                              game_audio(g), ++g->combat_idle_count,
                              &boundary);
    ++g->scene_returns;
    if (status != XEEN_OK) {
        g->lifecycle_boundary = boundary;
        g->command_status = XGC_UNSUPPORTED;
        return XEEN_SERVICE_REQUIRED;
    }
    return XEEN_SERVICE_REQUIRED;
}
/* Caller 17B09..17B32 owns input and timer ordering. The elapsed continuation
 * persists across unavailable timer providers; polling and palette effects
 * are never replayed while awaiting that sample. */
/**
 * The input poll while combat waits for a command: the idle count restarted at a new loop top, then the shared
 * poll (clicks hit-tested over combat's tables, the key buttons' feedback) and the command it selects; without one,
 * the idle tick. G is stopped at the command poll with combat awaiting a command.
 * Original: 3912C's 189E3 (18C0C over the 43244 tables, 18AC5).
 */
static xeen_status poll_combat_command(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    xeen_input_poll_result result;
    xeen_sprite_archives archives;
    uint16_t key = 0;
    uint32_t boundary;
    /* 3912C 189E3 (the shared poll: the 18C0C click hit test
     * over the 43244 combat tables, the 18AC5 key feedback). */
    if (g->combat.loop_tops != g->combat_idle_top) {
        g->combat_idle_top = g->combat.loop_tops;
        g->combat_idle_count = 1;
        g->combat_idle_armed = 0;
    }
    archives = sprite_archives(g, NULL);
    g->audio.cd_now = g->timer.clock;
    status = xeen_input_poll_run(&g->input_poll, &g->movement, &g->input_provider,
                                 game_audio(g), &archives,
                                 g->presentation, &result);
    if (status != XEEN_OK) {
        if (result.boundary) {
            g->lifecycle_boundary = result.boundary;
            g->command_status = XGC_UNSUPPORTED;
        }
        return status;
    }
    key = result.command;
    if (!key) {
        return combat_idle_tick(g, bios, user);
    }
    status = xeen_combat_command(&g->combat, &g->movement, g->monster_database,
                                 g->monster_database_size, g->event_random, g->event_random_user,
                                 game_audio(g), key, &boundary);
    /* 391E5, the shared Cast dialog 3D8C1(0) in combat. */
    if (boundary == XEEN_COMBAT_CAST_DIALOG && g->combat.phase == XC_AWAITING_CAST) {
        g->cast_combat_call = 1;
        return cast_command(g);
    }
    /* 39223, the shared items dialog 44F92(record, 7) in combat. */
    if (boundary == XEEN_COMBAT_USE_DIALOG && g->combat.phase == XC_AWAITING_CAST) {
        return combat_use(g);
    }
    /* Tab, Info, Options, Quick Reference, F1..F6, Quick Fight's Cast. */
    if (boundary && boundary == g->combat.modal_site && g->combat.phase == XC_AWAITING_CAST) {
        return combat_modal(g, boundary);
    }
    return combat_route(g, status, boundary);
}
/*
 * The wait a continuous session's poll serves first, by its owner: combat's command poll (3912C), the
 * yes/no confirmation, the speech loop, the NPC dialogue window (or a Who-will prompt over window 11), the
 * modal loop (a modal, a scroll, a page wait, a Who-will prompt). Returns 1 with *STATUS the owner's
 * result, 0 when no wait owns the poll (the exploration poll's).
 */
static int poll_wait_owner(xeen_game *g, xeen_timer_bios bios, void *user, xeen_status *status)
{
    if (!g || !g->continuous) {
        return 0;
    }
    if (g->phase == XG_STOPPED && g->lifecycle_boundary == XEEN_COMBAT_COMMAND_POLL && g->combat.phase == XC_AWAITING_COMMAND) {
        *status = poll_combat_command(g, bios, user);
    } else if (g->phase != XG_WAITING) {
        return 0;
    } else if (g->confirm_owner) {
        *status = poll_confirm(g, bios, user);
    } else if (g->speech_wait) {
        *status = poll_speech(g, bios, user);
    } else if (g->dialogue_waiting ||
               (g->who_wait && !g->scroll_owner && g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)])) {
        *status = poll_dialogue(g, bios, user);
    } else if (g->modal.active || g->scroll_owner || g->page_wait || g->who_wait) {
        *status = poll_modal(g, bios, user);
    } else {
        return 0;
    }
    return 1;
}
/*
 * Exploration's wait for a command: the input poll; with no key, the frame clock is read, and while no tick has
 * passed (and the party is not defeated) the session goes back to the command poll (XEEN_SERVICE_REQUIRED) to poll
 * again. Returns XEEN_OK with the key (or its absence) in the key code when the loop goes on; else the status to
 * return (a poll's failure keeps its call site's boundary).
 * Original: 17B09's 189E3 poll; 17B12's 4EA1C tick wait before 17B09 again; on to 17B24.
 */
static xeen_status exploration_poll(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    if (g->lifecycle_boundary != XEEN_LOOP_IDLE_WAIT) {
        xeen_input_poll_result result;
        xeen_sprite_archives archives = sprite_archives(g, NULL);
        g->audio.cd_now = g->timer.clock;
        status = xeen_input_poll_run(&g->input_poll, &g->movement, &g->input_provider,
                                     game_audio(g), &archives,
                                     g->presentation, &result);
        if (status != XEEN_OK) {
            /* A modal/platform continuation cannot be restarted as a fresh
             * poll. Its original call site remains visible to the caller. */
            if (result.boundary) {
                g->lifecycle_boundary = result.boundary;
            }
            return status;
        }
        if (!result.command) {
            g->idle_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_FRAME_ELAPSED};
            g->lifecycle_boundary = XEEN_LOOP_IDLE_WAIT;
        }
    }
    if (g->lifecycle_boundary == XEEN_LOOP_IDLE_WAIT) {
        if ((status = timer_ticks(g, &g->idle_elapsed, bios, user)) != XEEN_OK) {
            return status;
        }
        if (!g->timer_registers.ax && !g->movement.ds[XEEN_DS_PARTY_DEFEATED]) {
            g->lifecycle_boundary = XEEN_LOOP_COMMAND_POLL;
            g->command_status = XGC_PENDING_INPUT;
            return XEEN_SERVICE_REQUIRED;
        }
    }
    return XEEN_OK;
}
/* Whether a mouse press is inside the 3D view (8..224 x 8..140). Original: 17B34's test. */
static int viewport_press(const xeen_game *g)
{
    return xeen_read16(&g->movement, XEEN_DS_MOUSE_CLICK) && xeen_read16(&g->movement, XEEN_DS_MOUSE_X) >= 8 &&
           xeen_read16(&g->movement, XEEN_DS_MOUSE_X) <= 224 &&
           xeen_read16(&g->movement, XEEN_DS_MOUSE_Y) >= 8 &&
           xeen_read16(&g->movement, XEEN_DS_MOUSE_Y) <= 140;
}
/*
 * A press in the view or Space: after the interaction gate, the wall in front of the party is read on the cache-
 * reduced cell (a missing neighbour reads no wall); indoors a locked door or grate (walls 1, 6, 9, 13) begins the
 * unlock; otherwise the cell's event runs (origin Space).
 * Original: 17B34 / 17B59; 17B63's gate; 1C680; the unlock 170D4.
 */
static xeen_status exploration_interact(xeen_game *g)
{
    uint16_t eligible = 0;
    unsigned facing = g->movement.ds[XEEN_DS_PARTY_DIRECTION], wall, shift, slot;
    uint16_t retained = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    g->lifecycle_boundary = INTERACTION_BOUNDARY;
    if (xeen_slice_interaction_gate(&g->movement, &eligible) != XEEN_OK) {
        return XEEN_SERVICE_REQUIRED;
    }
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, retained);
    slot = xeen_read16(&g->movement, XEEN_DS_MAP_SLOT);
    shift = xeen_read16(&g->movement, XEEN_DS_WALL_SHIFT(facing, 2u));
    { /* 1C680 on the cache-reduced cell (a missing neighbour reads no wall). */
        int cx = xeen_signed_byte(g->movement.ds[XEEN_DS_PARTY_X]),
            cy = xeen_signed_byte(g->movement.ds[XEEN_DS_PARTY_Y]);
        uint16_t id = g->movement.ds[XEEN_DS_PARTY_MAP];
        if (cx <= 31 && cy <= 31 && xeen_cache_traverse(&g->movement, &cx, &cy, &id) >= 0) {
            wall = ((uint32_t)xeen_read16(&g->movement,
                                          (uint16_t)(XEEN_DS_MAP_DATA + slot * XEEN_MAP_RECORD_BYTES +
                                                     (unsigned)cy * 32u + (unsigned)cx * 2u)) >>
                    shift) &
                   15u;
        } else {
            wall = 0;
        }
    }
    if (!xeen_read16(&g->movement, XEEN_DS_OUTDOORS) &&
        (wall == XEEN_INDOOR_WALL_DOOR || wall == XEEN_INDOOR_WALL_GRATE || wall == XEEN_INDOOR_WALL_LOCKED_GRATE ||
         wall == XEEN_INDOOR_WALL_LOCKED_DOOR)) {
        /* 17B8F by CS:0F14: a door or a grate (xeen/map_record.h's indoor wall kinds) asks 170D4 for its other
         * state: 13 -> 170D4(1, "Open Door"), 1 -> (13, "Open Door"), 9 -> (6, "Open Grate"), 6 -> (9, "Open
         * Grate"); 17BA6: F5FE kept is the event. */
        static const uint8_t next[14] = {[XEEN_INDOOR_WALL_DOOR] = XEEN_INDOOR_WALL_LOCKED_DOOR,
                                         [XEEN_INDOOR_WALL_GRATE] = XEEN_INDOOR_WALL_LOCKED_GRATE,
                                         [XEEN_INDOOR_WALL_LOCKED_GRATE] = XEEN_INDOOR_WALL_GRATE,
                                         [XEEN_INDOOR_WALL_LOCKED_DOOR] = XEEN_INDOOR_WALL_DOOR};
        return unlock_begin(g, next[wall],
                            wall == XEEN_INDOOR_WALL_DOOR || wall == XEEN_INDOOR_WALL_LOCKED_DOOR ? UNLOCK_TITLE_DOOR
                                                                                                  : UNLOCK_TITLE_GRATE);
    }
    g->event_origin = EVENT_ORIGIN_SPACE;
    return begin_event(g) ? XEEN_SERVICE_REQUIRED : XEEN_BAD_INPUT;
}
/*
 * The session's input poll: the owner of a wait first (poll_wait_owner); else the exploration loop's: the poll
 * (exploration_poll), the idle scene when there is no key, a press in the view or Space (exploration_interact), then
 * the command table (table_command). Refused outside the loop's command poll or idle wait.
 * Original: 17B09 (17B12's idle wait, 18A22's mouse release), 17B24, 17B34 / 17B59, 17C10.
 */
xeen_status xeen_game_poll_input(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    uint16_t command;
    if (poll_wait_owner(g, bios, user, &status)) {
        return status;
    }
    if (!g || !g->continuous || !g->entry_complete || g->phase != XG_STOPPED ||
        (g->lifecycle_boundary != XEEN_LOOP_COMMAND_POLL && g->lifecycle_boundary != XEEN_LOOP_IDLE_WAIT &&
         !(g->lifecycle_boundary == XEEN_POLL_MOUSE_RELEASE && g->input_poll.awaiting_mouse_release))) {
        return XEEN_BAD_INPUT;
    }
    /* A speech face's window 40 closed since (186C0 or 18765(28h)):
     * its covered pixels come back (presentation only). */
    if (g->face_active && !g->movement.ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)]) {
        face_restore(g);
    }
    status = exploration_poll(g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    /*17B24: only the original zero-key/no-exit branch starts an idle scene. */
    command = xeen_read16(&g->movement, XEEN_DS_INPUT_CODE);
    if (!command && !g->movement.ds[XEEN_DS_PARTY_DEFEATED]) {
        g->scene_argument = 1;
        g->lifecycle_boundary = XEEN_TIMER_FRAME_MARK;
        g->command_status = XGC_EXTERNAL;
        ++g->idle_scenes;
        return XEEN_SERVICE_REQUIRED;
    }
    /*17B34 viewport mouse interaction and 17B59 Space have event semantics. */
    if (viewport_press(g) || command == 0x20) {
        return exploration_interact(g);
    }
    return table_command(g, command);
}
