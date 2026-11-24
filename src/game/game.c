#include "xeen/map_objects.h"
#include "xeen/game.h"
#include "xeen/ds_globals.h"
#include "xeen/program_data.h"
#include "xeen/party.h"
#include "xeen/save.h"
#include "xeen/caller.h"
#include "xeen/post_action.h"
#include "xeen/scene.h"
#include "xeen/scene_construct.h"
#include "xeen/entities.h"
#include "xeen/sprites.h"
#include "xeen/minimap.h"
#include "xeen/interface.h"
#include "xeen/input_poll.h"
#include "xeen/monster_update.h"
#include "xeen/world_entry.h"
#include "xeen/text_window.h"
#include "xeen/modal.h"
#include "xeen/character_record.h"
#include "xeen/byte_order.h"
#include "xeen/town.h"
#include "xeen/treasure.h"
#include "xeen/bash.h"
#include "xeen/projectile.h"
#include "xeen/main_menu.h"
#include "xeen/raster.h"
#include "xeen/items.h"
#include "xeen/spells.h"
#include "xeen/addons.h"
#include "xeen/bugfixes.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_internal.h"
enum {
    RESUME_STOP_OUTER_FIGHT = 0x393f5,            /* the outer fight could not go on after a fight nested in its
                                                   * no-attackers scene had ended */
    SCENE_STOP_EVENT_REFRESH = 0x1c19b,           /* a scene ended while an event script runs: the event refresh
                                                   * (4A1B7) before its frame wait is not native here */
    GAME_STOP_LOOP_EXIT = 0x17f28                 /* the loop is to end (XEEN_TICK_LOOP_EXIT): the game's exit */
};
int minimap_driver_test(void *cache, const xeen_sprite_draw *d)
{
    return d->scale != 0xffffu && xeen_sprite_driver_check(cache, d->handle, d->frame) != 0;
}
/* The map load's box for DS (a session's): "Oops..." during a fall (8C85), else "Please Wait..." when a UI mode is set
 * (9A1C), else none (0). Original: 19E8A, 28850. */
static uint16_t wait_box_text(const uint8_t *ds)
{
    return ds[XEEN_DS_FALL_SCENE] ? XEEN_TEXT_FALL_OOPS : ds[XEEN_DS_UI_MODE] ? XEEN_TEXT_PLEASE_WAIT : 0;
}
/* Draws window 9's TEXT framed and presents it (50BF6(9)); then, as the original shows that flip's frame from its
 * vertical retrace until the load's next flip, the window shows the page (XEEN_DISPLAY_WAIT_BOX; display only, no
 * clock): the box stays on screen while the load runs, until the next display. The original has no timer (its load's
 * disk time); the native load takes milliseconds, so the window holds the box XEEN_DISPLAY_WAIT_BOX_MS (200 ms,
 * owner decisions Phase 310/310B: a native-only minimum, "Please Wait..." and "Oops..." alike).
 * Add-on please_wait_skip ("Please Wait..." only): the box is drawn and its pixels put back before the same present,
 * and the frame is not displayed; the DS, the text state, the flip and the load are the same.
 * Original: 1871B(9), 4E38C(9, TEXT), 50BF6(9) (28857..2886F; 19E91..19EAB for "Oops..."; 2B4AE's 28850). */
void wait_box_present(xeen_game *g, uint16_t text)
{
    xeen_presentation *p = g->presentation;
    uint16_t r[4];
    uint8_t under[320u * 40u];
    int skip = xeen_please_wait_skip_hides(text); /* add-on please_wait_skip */
    int saved = skip && xeen_presentation_window(p, XEEN_WINDOW_WAIT, r) &&
                xeen_please_wait_skip_save(p->framebuffer, p->framebuffer_size, r, under, sizeof under);
    menu_window_text(g, XEEN_WINDOW_WAIT, WINDOW_FRAMED, ds_text(g, text));
    if (saved) {
        xeen_please_wait_skip_restore(p->framebuffer, p->framebuffer_size, r, under); /* add-on please_wait_skip */
    }
    if (xeen_presentation_update_window(p, XEEN_WINDOW_WAIT) == XEEN_OK && !skip) {
        xeen_presentation_display(p, XEEN_DISPLAY_WAIT_BOX); /* 50BF6(9); owner's 200 ms hold */
    }
}
/* Opens the map load's wait window in M (the DS that gets it: a transition's next state; G owns the presentation):
 * "Oops..." during a fall, else "Please Wait..." when a UI mode is set, else nothing. The pixels under it are saved
 * first (once), then the text is drawn and shown (wait_box_present). Original: 19E8A (19E82's head): 1871B (4F2A1)
 * with DS:4E84 when 8C85 is set, else 28850 with DS:5856 when 9A1C is set; 4E38C, 50BF6. */
void reload_wait_open(xeen_game *g, xeen_movement *m)
{
    uint16_t text = wait_box_text(m->ds);
    if (!text) {
        return;
    }
    xeen_presentation_trace_extra("evopen 9"); /* Evidence: 1871B(9) */
    if (g->presentation && !m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_WAIT)]) {
        panel_under(g, XEEN_WINDOW_WAIT, 0);
        g->wait_under = 1;
    }
    xeen_window_open(m, XEEN_WINDOW_WAIT);
    if (!g->presentation) {
        return;
    }
    wait_box_present(g, text);
}
/* The box of the session S's map load (19E82's head, which the new session runs in xeen_game_create_loaded before any
 * presentation is bound there), shown on screen by G, the session that ends (its presentation and texts): drawn
 * framed as wait_box_present draws it, the displayed page with that window displayed (held 200 ms), then the pixels
 * put back; the draw trace records the 50BF6(9) as "update 9" (the original presents the box before 1791F's view and
 * icon updates). G's pages, flips and DS are not used further; the new session opens window 9 in its own DS and its
 * load's tail closes it there (1B0CE, DS only: create_seeded). Nothing without a box for S's DS or a presentation (no
 * display hook: drawn and put back, not shown); add-on please_wait_skip: "Please Wait..." drawn and put back, not
 * shown (as wait_box_present).
 * Original: 19E8A..2886F (1871B(9), 4E38C, 50BF6(9)) of the inn exit's 179D1 19E82 (9A1C = 1). */
void xeen_game_show_session_wait(xeen_game *g, const xeen_game_session *s)
{
    xeen_presentation *p = g ? g->presentation : NULL;
    uint16_t r[4], text = s ? wait_box_text(s->ds) : 0;
    uint8_t under[320u * 40u];
    if (!text || !p || !xeen_presentation_window(p, XEEN_WINDOW_WAIT, r) ||
        !xeen_please_wait_skip_save(p->framebuffer, p->framebuffer_size, r, under, sizeof under)) {
        return;
    }
    menu_window_text(g, XEEN_WINDOW_WAIT, WINDOW_FRAMED, ds_text(g, text));
    xeen_presentation_trace("update %u", XEEN_WINDOW_WAIT); /* the trace of 50BF6(9), shown below as a composed page */
    if (!xeen_please_wait_skip_hides(text)) { /* add-on please_wait_skip */
        xeen_presentation_display_window(p, XEEN_WINDOW_WAIT, XEEN_DISPLAY_WAIT_BOX); /* 50BF6(9); 200 ms hold */
    }
    xeen_please_wait_skip_restore(p->framebuffer, p->framebuffer_size, r, under);
}
/**
 * Movement service callback of the game: answers the services the movement code requests (the 96C0
 * input release, map loads, abilities and the others) from the game's own state and resources;
 * returns 0 for a service the session cannot serve.
 */
int movement_service(void *u, xeen_movement *m, xeen_service s, uint16_t a, uint16_t b,
                            uint16_t *ax)
{
    xeen_game *g = u;
    (void)b;
    if (s == XEEN_96C0 && g->continuous) {
        xeen_status status = xeen_input_release_96c0_database_rng(
                m, g->monster_database, g->monster_database_size, g->event_random,
                g->event_random_user, &g->service_boundary);
        if (status == XEEN_SERVICE_REQUIRED && g->service_boundary == XEEN_MONSTER_UPDATE_RANGED) {
            status = ranged_delivery(g);
        }
        return status == XEEN_OK;
    }
    /* The gate's ability service is original 2BEF9, which reads only party
     * skill bytes (reconstructed in interface.c). */
    if (s == XEEN_ABILITY && ax) {
        *ax = (uint16_t)xeen_party_skill(m, a);
        return 1;
    }
    if (s == XEEN_LOAD_MAP && g->transition_active && ax) {
        g->transition_target = a;
        *ax = 0;
        return 1;
    }
    /* 178EF: the forward step's door scene; execute_navigation runs it. */
    if (s == XEEN_SCENE && a == 1 && g->continuous && m->ds[XEEN_DS_DOOR_OPENING] == 1 && !g->door_stage) {
        g->door_pending = 1;
        return 0;
    }
    /* Effect results are ignored by movement callers. Other gameplay
     * services retain their explicit unsupported boundary. */
    return s == XEEN_SOUND &&
           xeen_native_audio_effect(game_audio(g), m, a) == XEEN_OK;
}
/* Runs the monsters' update the countdown owes (2A701), with the monster database and the session RNG; a monster's
 * ranged attack it queues (the stop at 2AAB1) is delivered at once (ranged_delivery). Any other status other than OK
 * leaves the stop's boundary in service_boundary, which the callers clear first and report at their own sites.
 * Original: 2A701 (96C0's pending release; the exported xeen_input_release_96c0_* keep that historical name). */
xeen_status run_monster_update(xeen_game *g)
{
    xeen_status status = xeen_input_release_96c0_database_rng(&g->movement, g->monster_database,
                                                              g->monster_database_size, g->event_random,
                                                              g->event_random_user, &g->service_boundary);
    if (status == XEEN_SERVICE_REQUIRED && g->service_boundary == XEEN_MONSTER_UPDATE_RANGED) {
        status = ranged_delivery(g);
    }
    return status;
}
/*
 * A monster's ranged attack in exploration, delivered at once: every projectile tier and every queued action runs
 * natively and owns the input until they finish. An action that is not reconstructed stops at its exact boundary,
 * keeping the draws and changes made before it (nothing is retried). The hits' scenes draw the 3D view in
 * exploration too (a monster drawn in colour mode 2 or 5 takes its random draws there, as in the volley spells), so
 * the delivery draws through the game's presentation.
 * Original: 2AAB1 -> 4128F; the hits' scenes 37B6D; the colour modes' draws 50525.
 */
xeen_status ranged_delivery(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    unsigned i;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    xeen_native_audio *audio = game_audio(g);
    volley_presentation(g);
    g->projectile = (xeen_projectile_delivery){0};
    g->ranged_queue_index = 0;
    status = xeen_projectile_setup(&g->projectile, m, g->monster_database, g->monster_database_size,
                                   &g->sprites, &archives, audio, &g->service_boundary);
    if (status != XEEN_OK) {
        return status;
    }
    xeen_presentation_trace_extra("mark delivery-begin"); /* Evidence: 4128F */
    while (!g->projectile.complete) {
        /* One whole scene per frame: the object frames, the effects, the attacker selection, the view rebuilt from
         * the current monster records (a shooter that moved is drawn once, at its new cell), the view's draw (with a
         * colour mode 2 or 5 cell's random draws), the projectiles moved one tier on, and the minimap. The delivery
         * ends when no projectile is left in flight (DS 97F7: some shooting row still holds a tier).
         * Original: 414B0 1B503(1) (246CD/22034 the view, 2538B/2469A its draw, 50525, 1B0F6 the tiers); 414BD. */
        xeen_presentation_trace_extra("mark delivery-frame"); /* Evidence: 414B0 */
        status = xeen_combat_scene(&g->combat, m, g->monster_database, g->monster_database_size,
                                   &g->service_boundary);
        if (status != XEEN_OK) {
            return status;
        }
        ++g->projectile.scenes;
        g->projectile.complete = (uint8_t)(m->ds[0x97f7] == 0u);
        /* The key poll after every frame finds no key (the delivery does not consume host keys): the input code is
         * cleared. Original: 414B8 189E3. */
        xeen_write16(m, XEEN_DS_INPUT_CODE, 0);
    }
    xeen_presentation_trace_extra("mark delivery-end");
    status = xeen_projectile_finish(&g->projectile, m, &g->service_boundary);
    if (status != XEEN_OK) {
        return status;
    }
    if (m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_COMBAT) {
        for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
            m->ds[XEEN_DS_COMBAT_PARTY + i] = (uint8_t)i;
        }
        m->ds[XEEN_DS_COMBAT_PARTY_COUNT] = m->ds[XEEN_DS_PARTY_COUNT];
    }
    for (; g->ranged_queue_index < XEEN_MONSTER_SHOOTERS; ++g->ranged_queue_index) {
        unsigned id = m->ds[XEEN_DS_MONSTER_SHOOTERS + g->ranged_queue_index];
        if (id == 255u) {
            continue;
        }
        status = xeen_combat_ranged_action(&g->combat, m, g->monster_database,
                                           g->monster_database_size, id, g->event_random,
                                           g->event_random_user, audio, &g->service_boundary);
        if (status != XEEN_OK) {
            return status;
        }
    }
    return xeen_combat_ranged_complete(m, &g->service_boundary);
}
/* The same delivery for a continuous session only (the movement and command ticks): a bounded session keeps the
 * stop at the ranged attack's boundary. */
xeen_gameplay_tick delivery_tail(xeen_game *g, xeen_gameplay_tick tick)
{
    return g->continuous ? ranged_tick(g, tick) : tick;
}
/* A tick that stopped at the monsters' ranged attack (after Space, Shoot or a Rest step): the attack is delivered
 * (ranged_delivery); then the monster countdown restarts at 3 and the tick goes on at its post-action step, or it
 * stops at the delivery's boundary. Any other tick is returned unchanged.
 * Original: 28178's tick at 2AAB1; 96C0 = 3, then 17264. */
xeen_gameplay_tick ranged_tick(xeen_game *g, xeen_gameplay_tick tick)
{
    xeen_movement *m = &g->movement;
    if (tick.boundary == XEEN_MONSTER_UPDATE_RANGED) {
        xeen_status delivered = ranged_delivery(g);
        tick.boundary = g->service_boundary;
        if (delivered == XEEN_OK) {
            m->ds[XEEN_DS_MONSTER_COUNTDOWN] = 3;
            m->status = XEEN_OK;
            tick.boundary = XEEN_TICK_POST_ACTION;
        }
    }
    return tick;
}

/* Combat's monster updates end with the same delivery of queued ranged attacks as exploration's (ranged_delivery),
 * *BOUNDARY its stop. Refused for another movement. Original: combat's direct 2A701 calls end at 2AAB1. */
xeen_status combat_ranged_delivery(void *user, xeen_movement *m, uint32_t *boundary)
{
    xeen_game *g = user;
    xeen_status status;
    if (!g || m != &g->movement || !boundary) {
        return XEEN_BAD_INPUT;
    }
    status = ranged_delivery(g);
    *boundary = g->service_boundary;
    return status;
}

/* The party bar drawn again (each member's face by condition, the HP gems, the highlight) and its window updated;
 * nothing without a presentation. Original: 2ABB8(1), 50BF6(21h). */
void bar_redraw(xeen_game *g)
{
    if (g->presentation) {
        (void)xeen_world_entry_party_bar(&g->movement, g->presentation, 1);
    }
}
void explore_icons(xeen_game *g)
{
    xeen_draw_list list;
    if (g->presentation && xeen_scene_collect_draw_list(&g->movement, 0, XEEN_DS_COMMAND_ICONS, &list) == XEEN_OK) {
        (void)xeen_presentation_submit(g->presentation, &list);
    }
}

/* Evidence: a copy of the DS each time a modal dialog starts waiting for a key (where the original-byte replay takes
 * its snapshot), and the count the host reads to see a new one. Original: a modal 189E3 entry. */
void modal_poll_mark(xeen_game *g)
{
    memcpy(g->modal_poll_ds, g->movement.ds, sizeof g->modal_poll_ds);
    ++g->modal_polls;
}
/* Combat's end, back to exploration's screen: the sprite handle on top of the handle stack (combat.icn, and every
 * load after it) is freed and popped, so the handle below it (the exploration icons) becomes the current one and
 * every command icon's; the exploration buttons and icons come back; the combat party is the whole party again in
 * roster order. The session stops at the scene's frame wait. 0 (nothing changed) when the current handle is 0 or
 * holds no loaded sprite. Original: 39514 190CF: 4AA3E(98D4); 98D4 = 95A8[96C1], 96C1 down; 39519 (the 2A68 records),
 * 39549 43287, 3954E 28D80. */
int restore_after_combat(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint16_t handle = xeen_read16(m, XEEN_DS_SPRITE_HANDLE), i;
    if (!handle || !xeen_sprite_get(&g->sprites, handle)) {
        return 0;
    }
    (void)xeen_sprite_free_from(&g->sprites, handle);
    handle = xeen_read16(m, XEEN_DS_HANDLE_SLOT(m->ds));
    m->ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(m->ds[XEEN_DS_HANDLE_DEPTH] - 1u);
    xeen_write16(&g->movement, XEEN_DS_SPRITE_HANDLE, handle);
    for (i = 0; i < 15u; ++i) {
        xeen_write16(&g->movement, XEEN_VIEW_FIELD(XEEN_DS_COMMAND_ICONS + XEEN_VIEW_RECORD_BYTES, i, HANDLE),
                     handle);
    }
    xeen_combat_buttons(&g->movement, 0);                              /* 39549 43287 */
    xeen_combat_main_icons(&g->combat, &g->movement, g->presentation); /* 3954E 28D80 */
    xeen_presentation_trace("mark combat-end");
    for (i = 0; i < g->movement.ds[XEEN_DS_PARTY_COUNT]; ++i) {
        g->movement.ds[XEEN_DS_COMBAT_PARTY + i] = (uint8_t)i;
    }
    g->movement.ds[XEEN_DS_COMBAT_PARTY_COUNT] = g->movement.ds[XEEN_DS_PARTY_COUNT];
    g->combat.icon_handle = handle;
    g->combat.cleanup_complete = 1u;
    g->lifecycle_boundary = XEEN_SCENE_FRAME_WAIT;
    g->command_status = XGC_EXTERNAL;
    g->phase = XG_STOPPED;
    return 1;
}
/**
 * Continues the caller of an event that has finished: the combat cleanup, the step, the surface
 * callers or a location, depending on where the event was started.
 */
static void event_return(xeen_game *g)
{
    xeen_caller_result next;
    if (g->event_origin == EVENT_ORIGIN_CLEANUP) {
        /* The combat cleanup's event (a fight entered with a sign up) returned; its result is not tested: the
         * cleanup's tail follows (xeen_game_resume_combat). Original: 39577's 310B4, then 3957C. */
        g->event_origin = EVENT_ORIGIN_NONE;
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = COMBAT_CLEANUP_TAIL_BOUNDARY;
        g->command_status = XGC_EXTERNAL;
        return;
    }
    if (g->event_origin == EVENT_ORIGIN_CONTACT) {
        /* The contact scene's event (deferred while the contact was pending) returned; its result is not tested. The
         * party's "acted" flag is cleared; with the event UI mode still set (a restore the native does not serve) it
         * stops; else the scene's frame wait follows and the scene's caller continues.
         * Original: 1C18A's 310B4 (313FD deferred); 1C18F 3067 = 0; 1C194 with 9A1C 9 (4A1B7); the 1C1B3 frame wait. */
        g->event_origin = EVENT_ORIGIN_NONE;
        g->movement.ds[XEEN_DS_PARTY_ACTED] = 0;
        g->phase = XG_STOPPED;
        if (g->nest.active && !g->combat.outer_active) {
            g->nest.armed = 1; /* the contact scene's tail */
        }
        if (g->movement.ds[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EVENT) {
            g->lifecycle_boundary = SCENE_STOP_EVENT_REFRESH;
            g->command_status = XGC_UNSUPPORTED;
            return;
        }
        g->lifecycle_boundary = XEEN_SCENE_FRAME_WAIT;
        g->command_status = XGC_EXTERNAL;
        return;
    }
    if (g->event_origin == EVENT_ORIGIN_STEP) {
        xeen_write16(&g->movement, XEEN_DS_LOOP_EXIT, g->dispatcher.return_ax);
        if (g->dispatcher.return_ax) {
            g->lifecycle_boundary = GAME_STOP_LOOP_EXIT;
            return;
        }
        /* The world entry's cell event returned without a loop exit: the entry goes on (a new game's start cell on
         * map 28 has one). Original: 17AED (the event 17ADE, 1C844) with F64A = 0; on at 17AF7. */
        if (g->continuous && !g->entry_complete) {
            g->entry_complete = 1;
        }
        if (xeen_treasure_check(&g->movement) == XEEN_TREASURE_DIALOG) {
            g->lifecycle_boundary = XEEN_TREASURE_FOUND;
        } else {
            g->lifecycle_boundary = XEEN_TIMER_FRAME_MARK;
        }
    } else {
        next = xeen_caller_after_dispatch(&g->movement, g->dispatcher.return_ax);
        g->lifecycle_boundary = next.boundary;
        /* The command table redispatches the key the modal kept, as the original does: Enter matches nothing and joins
         * the loop's tail, Space reaches its wait, a direction really moves. Every table target but Space's wait (its
         * own resume) comes back through table_command. Original: 17C10; Enter joins 17ACC; Space 17DA8. */
        if (xeen_caller_is_command_target(g->lifecycle_boundary) &&
            g->lifecycle_boundary != EVENT_SPACE_WAIT_BOUNDARY) {
            g->lifecycle_boundary = EVENT_TABLE_COMMAND_BOUNDARY;
        }
    }
    g->event_origin = EVENT_ORIGIN_NONE;
    g->scene_argument = 1;
    g->command_status = exit_or_external(g->lifecycle_boundary);
}
/* The event returned to its exploration caller (not a continuous session's event origin, which event_return
 * continues): the caller's table decides the boundary from the dispatcher's result. Original: 310B4 and its AX. */
static void dispatch_caller_return(xeen_game *g)
{
    if (g->restored && !(g->continuous && g->event_origin)) {
        xeen_caller_result caller = xeen_caller_after_dispatch(&g->movement, g->dispatcher.return_ax);
        g->lifecycle_boundary = caller.boundary;
        g->command_status = exit_or_external(caller.boundary);
    }
}
/* The session's command status (and its phase, boundary) for the dispatcher's status S. */
static void dispatch_command_status(xeen_game *g, xeen_dispatch_status s)
{
    if (s == XD_RETURNED) {
        g->command_status = XGC_EXTERNAL;
        g->lifecycle_boundary = XEEN_LOOP_AFTER_DISPATCH;
        dispatch_caller_return(g);
    } else if (s == XD_RETURNED_EXIT) {
        g->command_status = XGC_ORIGINAL_EXIT;
        /* 310B4's callers test only its AX (17AED, 17C06):
         * F60A set by the event (33698's 2F869) leaves the forward step's
         * 17AF7 scene and 17B09 poll, or the Space path's 17C10 dispatch,
         * to run first; event_return continues there. */
        dispatch_caller_return(g);
    } else if (s == XD_UNSUPPORTED) {
        g->command_status = XGC_UNSUPPORTED;
    } else if (s == XD_INPUT_ERROR) {
        g->command_status = XGC_INPUT_ERROR;
    } else if (s == XD_INTERNAL) {
        g->command_status = XGC_INTERNAL;
    } else if (g->ui.pending) {
        g->command_status = XGC_PENDING_INPUT;
        g->phase = XG_WAITING;
    } else {
        g->command_status = XGC_EXTERNAL;
    }
}
/*
 * A service the dispatcher stopped at that a continuous session's own routine runs, by the dispatcher's boundary,
 * each resuming the interpreter itself: operation 17's location, the treasure dialog with every pack full or with
 * item lines, operation 7's teleport, operation 21's word entry, operation 52's fall, an event's message scroll
 * (operation 20's lock-pick) and operation 12's refused take. Returns 1 when its owner took it.
 * Original: XD_INPUT_OR_SERVICE: 3245F, 2EA68, 3193B, 2E8E5, 3341C, 18F2F(A827, 1), 291FB.
 */
static int dispatch_service_owner(xeen_game *g)
{
    switch (g->dispatcher.boundary) {
    case XEEN_EVENT_ENTER_LOCATION:
        (void)location_begin(g);
        return 1;
    case XEEN_REQUEST_TREASURE_DIALOG:
        /* The dispatcher's 2EA68 with items: the item lines; the
         * 2F170 key then resumes the dispatcher (phase 5) for the settlement. */
        if (g->treasure.dispatch_full == DISPATCH_FULL_TO_OPEN) {
            dispatch_full_open(g);
            return 1;
        }
        if (g->treasure.dispatch_items) {
            g->treasure.dispatch_items = 0;
            (void)item_next(g);
            return 1;
        }
        return 0;
    case XEEN_EVENT_TELEPORT:
        (void)teleport(g);
        return 1;
    case XEEN_EVENT_WORD_ENTRY:
        (void)word_begin(g);
        return 1;
    case XEEN_EVENT_FALL:
        (void)event_fall(g);
        return 1;
    case XEEN_REQUEST_SCROLL:
        event_scroll_open(g);
        return 1;
    case XEEN_REQUEST_PAYMENT_REFUSAL:
        event_payment_open(g);
        return 1;
    default:
        return 0;
    }
}
/* Starts an event's requested scene when the session waits at a scene call: an operation's scene, a portrait
 * flash's (its scene argument 0, the others 1), the treasure dialog's opening, or a party damage's with a damage
 * event (which marks the frame clock itself first; the others run from the scene clock's mark).
 * Original: 1B503(1) (2B9C7's, 2EA68's head, 30E2D(2)'s own 4EA0D); 4EA35. */
static void dispatch_scene_request(xeen_game *g)
{
    if (g->event_scene == EVENT_SCENE_REQUESTED &&
        (g->lifecycle_boundary == SCENE_CALL_BOUNDARY || g->lifecycle_boundary == PORTRAIT_FLASH_BOUNDARY ||
         (g->lifecycle_boundary == XEEN_TREASURE_CALL && g->treasure.head) ||
         (g->lifecycle_boundary == PARTY_DAMAGE_BOUNDARY && g->damage_event))) {
        /* 30E2D(2) begins with its own 4EA0D. */
        g->event_scene = EVENT_SCENE_RUNNING;
        g->scene_argument = g->lifecycle_boundary == PORTRAIT_FLASH_BOUNDARY ? 0 : 1;
        g->lifecycle_boundary = g->lifecycle_boundary == PARTY_DAMAGE_BOUNDARY ? XEEN_TIMER_FRAME_MARK : XEEN_TIMER_SCENE_MARK;
    }
}
/* The event interpreter's steps per call (a budget return resumes it) and drive_interpreter's resumes. */
#define EVENT_STEP_BUDGET 256
#define EVENT_RESUME_LIMIT 256u
/* A bounded session without the lifecycle: the interpreter alone, resumed across its budget returns. */
static void drive_interpreter(xeen_game *g)
{
    unsigned calls = 0; /* A budget return resumes (event_dispatch.c, phase 2) */
    do {
        g->event_status = xeen_event_execute_interactive(
                g->resident, g->resources.record_bytes, &g->index, g->movement.ds,
                sizeof g->movement.ds, &g->execution, EVENT_STEP_BUDGET, xeen_event_resource_service,
                &g->resources, NULL, NULL);
    } while (g->event_status == XEE_BUDGET && ++calls < EVENT_RESUME_LIMIT);
    if (g->event_status == XEE_EXTERNAL && g->ui.pending) {
        g->phase = XG_WAITING;
    } else if (g->event_status == XEE_EXTERNAL && g->execution.state.boundary == XEEN_EVENT_SCRIPT_END) {
        g->phase = XG_NAVIGATION; /* explicit native test-session boundary */
    } else {
        g->phase = XG_STOPPED;
    }
}
/* The session's event status after a dispatcher run that ended with S: the event's own result when it returned, the
 * dispatcher's failure kind, else XEE_EXTERNAL (waiting outside the interpreter). */
static xeen_exec_status dispatch_event_status(const xeen_game *g, xeen_dispatch_status s)
{
    switch (s) {
        case XD_UNSUPPORTED:
            return XEE_UNSUPPORTED;
        case XD_INTERNAL:
            return XEE_INTERNAL;
        case XD_INPUT_ERROR:
            return XEE_MALFORMED;
        case XD_RETURNED:
        case XD_RETURNED_EXIT:
            return g->execution.state.logical_result;
        default:
            return XEE_EXTERNAL;
    }
}
/**
 * Continues the game after the event UI answered: resumes the waiting owner (the arena, the tavern
 * food, the event interpreter or the lifecycle) until it needs input again. With the lifecycle the
 * dispatcher runs, its status becomes the session's (dispatch_command_status), a returned event of a
 * continuous session continues at its origin (event_return), a service it stopped at goes to its owner
 * (dispatch_service_owner) and an operation's scene is requested (dispatch_scene_request).
 */
void drive(xeen_game *g)
{
    xeen_dispatch_status s;
    xeen_native_audio_speech_hold(&g->audio, g->movement.ds); /* speech = off keeps the panel's Speech off */
    /* The arena's 3000D answered: 360C4 continues (not the
     * interpreter, which waits in operation 17 for the location). */
    if (g->arena_stage && g->continuous) {
        (void)arena_resume(g);
        return;
    }
    /* The tavern food's 2F2FF answered: 25BFE continues. */
    if (g->tavern_food_step == TAVERN_FOOD_ASKING && g->continuous) {
        (void)tavern_resume(g);
        return;
    }
    if (!g->lifecycle) {
        drive_interpreter(g);
        return;
    }
    s = xeen_event_dispatch_run(&g->dispatcher, g->resident, g->resources.record_bytes, &g->index,
                                g->movement.ds, sizeof g->movement.ds, EVENT_STEP_BUDGET, xeen_event_resource_service,
                                &g->resources);
    g->execution = g->dispatcher.execution;
    g->lifecycle_boundary = g->dispatcher.boundary;
    g->phase = XG_STOPPED;
    dispatch_command_status(g, s);
    if ((s == XD_RETURNED || s == XD_RETURNED_EXIT) && g->continuous && g->event_origin) {
        event_return(g);
    }
    if (s == XD_INPUT_OR_SERVICE && g->continuous && dispatch_service_owner(g)) {
        return;
    }
    dispatch_scene_request(g);
    g->event_status = dispatch_event_status(g, s);
}
/**
 * Port-output hook of the timer polls: passes a VGA palette write to the presentation and records a
 * failure.
 */
void palette_port(void *user, uint16_t port, uint8_t value)
{
    xeen_game *g = user;
    if (xeen_presentation_port(g->presentation, port, value) != XEEN_OK) {
        g->port_error = 1;
    }
}
xeen_status original_exit(xeen_game *g, uint32_t boundary)
{
    g->phase = XG_STOPPED;
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_ORIGINAL_EXIT;
    return XEEN_SERVICE_REQUIRED;
}
xeen_status service_wait(xeen_game *g, uint32_t boundary)
{
    g->phase = XG_STOPPED;
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_EXTERNAL;
    return XEEN_SERVICE_REQUIRED;
}
/* Waits at BOUNDARY for the scene the host runs with ARGUMENT (the original's 1B503 argument). */
xeen_status scene_wait(xeen_game *g, uint16_t argument, uint32_t boundary)
{
    g->scene_argument = argument;
    return service_wait(g, boundary);
}
xeen_status sound_effect(xeen_game *g, uint16_t effect)
{
    return xeen_native_audio_effect(game_audio(g), &g->movement, effect);
}
xeen_status input_wait(xeen_game *g, uint32_t boundary)
{
    g->phase = XG_WAITING;
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_PENDING_INPUT;
    return XEEN_SERVICE_REQUIRED;
}
xeen_status frame_base(xeen_game *g, xeen_timer_bios bios, void *user)
{
    return xeen_timer_sample(&g->timer, XEEN_TIMER_FRAME_MARK, &g->timer_registers, bios, user);
}
xeen_status timer_ticks(xeen_game *g, xeen_timer_elapsed_state *state, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    g->port_error = 0;
    status = xeen_timer_elapsed(&g->timer, state, &g->timer_registers, palette_port, g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    if (g->port_error) {
        return XEEN_BAD_INPUT;
    }
    return XEEN_OK;
}
/*
 * One sample of the scene's frame wait: the scene clock's ticks since its mark (the palette poll's writes go to the
 * presentation). Returns XEEN_OK once the scene has returned (2 ticks or more, compared signed), XEEN_SERVICE_REQUIRED
 * while it keeps waiting, or the sample's failure (XEEN_BAD_INPUT for a palette write the presentation refused).
 * Original: 1C1B3's 4EA44 (the 4A607 palette poll, the BIOS ticks since 4EA35's base; JL).
 */
static xeen_status frame_wait_sample(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_timer_registers r = g->timer_registers;
    xeen_status status;
    g->port_error = 0;
    if (!g->frame_elapsed.entry) {
        g->frame_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_SCENE_ELAPSED};
    }
    status = xeen_timer_elapsed(&g->timer, &g->frame_elapsed, &r, palette_port, g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    if (g->port_error) {
        return XEEN_BAD_INPUT;
    }
    g->timer_registers = r;
    ++g->frame_waits;
    g->frame_elapsed = (xeen_timer_elapsed_state){0, XEEN_TIMER_SCENE_ELAPSED};
    if ((r.ax < 0x8000u ? (int32_t)r.ax : (int32_t)r.ax - 65536) < 2) {
        return XEEN_SERVICE_REQUIRED; /* JL: signed */
    }
    return XEEN_OK;
}
/*
 * The returned scene's innermost frames that are not a caller stage: the found-item wait's scene and a scroll the
 * combat Cast dialog opened (its refusal or "Spell Failed!"). Returns 1 when one took the return (the session waits
 * at its boundary), else 0. Original: the item wait's RETF to 30E19; the scroll's owners 6 and 7.
 */
static int frame_wait_inner_frame(xeen_game *g)
{
    if (g->treasure.item_scene) {
        g->treasure.item_scene = 0;
        ++g->scene_returns;
        g->phase = XG_STOPPED;
        g->lifecycle_boundary = ITEM_WAIT_TICK_BOUNDARY;
        g->command_status = XGC_EXTERNAL;
        return 1;
    }
    /* A scroll the combat Cast dialog opened runs its own scene pass inside the fight, and the fight may itself run
     * inside an outer caller's scene (a Rest step whose contact began it). That pass returns to the scroll's poll,
     * never to the outer caller: the outer stage waits for the fight's own end. (An earlier native let Rest's stage
     * take the return and resume, which then met the open scroll.)
     * Original: 28CFC (owner 6), 39641 (7); the 18F2F loop's 1B503; Rest's 28EFA step, 1C17E -> 38F5F; the 1C1B3
     * return to 18FAE (RETF 18FAD), not 28EFF; 29157. */
    if (!g->panel_stage && g->cast_active && g->cast_mode == XEEN_UI_MODE_COMBAT &&
        (g->scroll_owner == SCROLL_CAST_REFUSED || g->scroll_owner == SCROLL_SPELL_FAILED)) {
        g->phase = XG_WAITING;
        g->lifecycle_boundary = SCROLL_POLL_BOUNDARY;
        g->modal_scene_ready = 1;
        g->command_status = XGC_EXTERNAL;
        ++g->scene_returns;
        return 1;
    }
    return 0;
}
/*
 * The caller stage whose scene pass just returned, the first set in this order: the control panel, Rest, the damage
 * loop, a surface caller, a fall, a door, Bash, the unlock, the Cast and Shoot prefixes (each prefix is cleared as it
 * is taken). Returns 1 with *STATUS the stage's continuation, 0 when no stage waits (the return is the exploration
 * loop's).
 * Original: 1C1C0's RETF to the caller: the panel 298DD, 2B12E or 288DB; Rest 28EFF or 2912B; the damage loop
 * (30E2D) 30E3C; a surface 17349; a fall 173D6/173E3; a door 178F6; Bash (30E2D) 3D89C; the unlock 17250; Cast
 * 17E43; Shoot 17EB4.
 */
static int frame_wait_caller_stage(xeen_game *g, xeen_status *status)
{
    xeen_status (*stage)(xeen_game *);
    if (g->panel_stage) {
        stage = panel_after_scene;
    } else if (g->rest_stage) {
        stage = rest_after_scene;
    } else if (g->damage_stage) {
        stage = damage_after_scene;
    } else if (g->surface_stage) {
        stage = surface_after_scene;
    } else if (g->fall_stage) {
        stage = fall_after_scene;
    } else if (g->door_stage) {
        stage = door_after_scene;
    } else if (g->bash_stage) {
        stage = bash_after_scene;
    } else if (g->unlock_stage == UNLOCK_SCENE) {
        stage = unlock_after_scene;
    } else if (g->cast_prefix) {
        g->cast_prefix = 0;
        stage = cast_open;
    } else if (g->shoot_prefix) {
        g->shoot_prefix = 0;
        stage = shoot_melee;
    } else {
        return 0;
    }
    ++g->scene_returns;
    *status = stage(g);
    return 1;
}
/*
 * The event scene's completion (the scene drive() requested for an operation returned): drive() runs the
 * interpreter on. Returns 1 when that re-drive reached a frame of its own that takes the return (the dispatcher's
 * treasure item lines, or the treasure window after its opening scene), else 0 with *FRESH set when the re-drive
 * opened the scroll or page wait the session waits on next. Original: 2EA68's item lines at 30E05.
 */
static int frame_wait_event_scene(xeen_game *g, int *fresh)
{
    int head = g->treasure.head, idle = !g->scroll_owner && !g->page_wait;
    g->event_scene = EVENT_SCENE_COMPLETED;
    drive(g);
    /* A scroll or a page wait that this re-drive opened runs its own loop's frame mark and scene pass before its first
     * poll: the event's scene was not the loop's. Original: 18F2F mode 1 (18FA1); 316DC's 31786, 337D7's 3380E; the
     * loops' 4EA0D and 1B503(1); the event's 316EB, operation 20's 2E013, 30E2D(2). */
    *fresh = idle && (g->scroll_owner || g->page_wait);
    /* The dispatcher's treasure dialog it reached began its item lines. */
    if (g->phase == XG_STOPPED && g->lifecycle_boundary == ITEM_WAIT_BOUNDARY) {
        ++g->scene_returns;
        return 1;
    }
    /* 2EA68's head scene returned (2EBB8) and the window is
     * open: the 2F170 loop's own scene comes before its poll. */
    if (head && g->phase == XG_WAITING && g->ui.pending && g->ui.active.kind == XUI_TREASURE) {
        g->modal_scene_ready = 0;
        g->modal_poll_pending = MODAL_POLL_START;
        ++g->scene_returns;
        return 1;
    }
    return 0;
}
/*
 * The wait the session goes on with after the scene, the first open in this order: an NPC's speech loop, the modal
 * (the yes/no or the numeric choice), a scroll's poll, a page wait (or the "Nothing here." key loop), the Who-will
 * prompt; with none the boundary stays the exploration loop's. FRESH: the scroll or page wait was opened by this
 * return's event re-drive, so its loop's own scene comes first.
 * Original: 3000D's loop; 2F2FF, the choice 2F5B1; 18FAE; 31793, 3381B; 2F9BA.
 */
static void frame_wait_next_wait(xeen_game *g, int fresh)
{
    if (g->speech_wait == SPEECH_FACE) {
        g->phase = XG_WAITING;
        g->lifecycle_boundary = SPEECH_LOOP_BOUNDARY;
    } /* The speech loop */
    else if (g->modal.active) {
        /* A yes/no that this scene's own event just opened (after an event scene: the portrait flashes) has not
         * run its first step yet: that scene was the operation's, not the modal's. Original: 2F2FF, 2F517. */
        g->phase = XG_WAITING;
        g->lifecycle_boundary = g->choice ? MODAL_CHOICE_BOUNDARY : MODAL_YES_NO_BOUNDARY;
        g->modal_scene_ready = !(g->event_modal_open && g->modal_fresh);
    } else if (g->scroll_owner) {
        g->phase = XG_WAITING;
        g->lifecycle_boundary = SCROLL_POLL_BOUNDARY;
        g->modal_scene_ready = !fresh;
    } else if (g->page_wait) {
        g->phase = XG_WAITING;
        g->lifecycle_boundary = g->nothing_wait ? NOTHING_HERE_POLL_BOUNDARY : PAGE_WAIT_BOUNDARY;
        g->modal_scene_ready = !fresh;
    } else if (g->who_wait) {
        /* The scene just drew the 3D view's corner sprite (at (219, 134)) over the Who-will window's left border;
         * the window's frame is drawn again over it before the face form. Original: 2FB0F 4F6F8(24h) after the
         * pass's 1B503(0), before 2FB84's 50BF6(3); the sprite 973A. */
        if (who_plain(g)) {
            (void)xeen_presentation_frame(g->presentation, XEEN_WINDOW_WHO_WILL);
        }
        g->phase = XG_WAITING;
        g->lifecycle_boundary = WHO_WILL_POLL_BOUNDARY;
        g->modal_scene_ready = 1;
    }
}
/*
 * The scene's frame wait: one timer sample per call (frame_wait_sample) until the scene is over, then the scene
 * returns to whoever called it, searched innermost first: a live nested fight's outer combat, the nested event
 * key loop (its frame is popped and the search goes on), the found-item wait and combat's Cast scroll
 * (frame_wait_inner_frame), the stage that ran the scene (frame_wait_caller_stage; not while the combat cleanup's
 * event runs, whose scenes return to the event dispatcher), the event scene's re-drive, and finally the wait that
 * follows (frame_wait_next_wait); with none, the exploration loop's command poll (or a continuous session's world
 * entry end when the scene had no argument).
 * Original: 1C1B3, then RETF 1C1C0 to the calling frame; the exploration loop 17B09 (17A20 at a world entry).
 */
xeen_status xeen_game_resume_frame_wait(xeen_game *g, xeen_timer_bios bios, void *user)
{
    xeen_status status;
    int fresh = 0;
    if (!g || g->phase != XG_STOPPED || g->lifecycle_boundary != XEEN_SCENE_FRAME_WAIT || !g->presentation) {
        return XEEN_BAD_INPUT;
    }
    if (!bios) {
        return XEEN_SERVICE_REQUIRED;
    }
    status = frame_wait_sample(g, bios, user);
    if (status != XEEN_OK) {
        return status;
    }
    /* The live nested fight's contact scene returned: the outer combat goes on. Original: the outer round tail's
     * 393F0 1B503, RETF 393F5. */
    if (g->combat.outer_active && g->combat.phase == XC_INACTIVE) {
        uint32_t boundary = 0;
        xeen_status st;
        st = xeen_combat_resume_outer(&g->combat, &g->movement, g->monster_database,
                                      g->monster_database_size, g->event_random,
                                      g->event_random_user,
                                      game_audio(g), &boundary);
        if (st == XEEN_BAD_INPUT) {
            g->lifecycle_boundary = RESUME_STOP_OUTER_FIGHT;
            g->command_status = XGC_UNSUPPORTED;
            return XEEN_SERVICE_REQUIRED;
        }
        ++g->scene_returns;
        return combat_route(g, st, boundary);
    }
    if (g->nest.armed) {
        nest_pop(g); /* back to the outer event key loop. Original: RETF 1C1C0 to 3381A (337D7) */
    }
    if (frame_wait_inner_frame(g)) {
        return XEEN_SERVICE_REQUIRED;
    }
    /* The combat cleanup's event runs inside the stage whose contact started the fight (e.g. a Rest step): a scene
     * its records request returns to the event dispatcher (the innermost frame), not to that stage.
     * Original: 39577's 310B4 (event origin 4), Rest's 28EFA; ops 2/4/39's 1B503(1). */
    if (!(g->event_origin == EVENT_ORIGIN_CLEANUP && g->event_scene == EVENT_SCENE_RUNNING) &&
        frame_wait_caller_stage(g, &status)) {
        return status;
    }
    g->lifecycle_boundary =
            g->continuous && !g->entry_complete && !g->scene_argument ? XEEN_ENTRY_FINISH : XEEN_LOOP_COMMAND_POLL;
    if (g->event_scene == EVENT_SCENE_RUNNING && frame_wait_event_scene(g, &fresh)) {
        return XEEN_SERVICE_REQUIRED;
    }
    frame_wait_next_wait(g, fresh);
    g->command_status = XGC_EXTERNAL;
    ++g->scene_returns;
    return XEEN_SERVICE_REQUIRED;
}
/* Rest's return to whoever started it: the Rest command ends as every command does (command_tail); Super Shelter's
 * Rest gives back the UI mode it saved and ends the spell's handler (cast_handled).
 * Original: 28E35's return to 17F07, or to 3AF57 (9A1C restored). */
xeen_status rest_return(xeen_game *g)
{
    if (!g->shelter) {
        return command_tail(g);
    }
    g->shelter = 0;
    g->movement.ds[XEEN_DS_UI_MODE] = g->shelter_mode;
    return cast_handled(g);
}
/** Stops the lifecycle at unsupported BOUNDARY: records it and returns XEEN_SERVICE_REQUIRED. */
xeen_status game_stop(xeen_game *g, uint32_t boundary)
{
    xeen_archive_miss_keep(boundary); /* diagnostics only: a missing CC member behind the stop */
    g->lifecycle_boundary = boundary;
    g->command_status = XGC_UNSUPPORTED;
    g->phase = XG_STOPPED;
    return XEEN_SERVICE_REQUIRED;
}
/* Stops the command's tail at its boundary: the original's exit (17F28) or the host's continuation (exit_or_external),
 * with the next scene's argument 1. */
static xeen_status command_stop(xeen_game *g)
{
    g->command_status = exit_or_external(g->lifecycle_boundary);
    g->phase = XG_STOPPED;
    g->scene_argument = 1;
    return XEEN_SERVICE_REQUIRED;
}
/* The end of an exploration command once no fall is pending (also the end of a fall's own scene): a defeated party
 * or the exploration loop's exit flag leaves the loop (XEEN_TICK_LOOP_EXIT: the program's exit, or the death scene
 * for a defeated party); otherwise the session goes back to its caller as after a rejected move.
 * Original: 17F15 (F60A or F64A -> 17F28, else 17ACC). */
xeen_status command_finish(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    if (m->ds[XEEN_DS_PARTY_DEFEATED] || xeen_read16(m, XEEN_DS_LOOP_EXIT)) {
        g->lifecycle_boundary = GAME_STOP_LOOP_EXIT;
    } else {
        g->lifecycle_boundary = xeen_caller_after_rejected_move(m).boundary;
    }
    return command_stop(g);
}
/* The end of an accepted exploration command, shared with Bash: the command is counted, then a fall the command
 * began (XEEN_DS_FALL_SCENE) stops at XEEN_LOOP_FALL for its scene; otherwise command_finish.
 * Original: 17F07 (8C85 -> 173A0, else 17F15). */
xeen_status command_tail(xeen_game *g)
{
    ++g->accepted_commands;
    if (g->movement.ds[XEEN_DS_FALL_SCENE]) {
        g->lifecycle_boundary = XEEN_LOOP_FALL;
        return command_stop(g);
    }
    return command_finish(g);
}
xeen_status xeen_game_resume_event(xeen_game *g)
{
    if (!g || !g->continuous || g->phase != XG_STOPPED) {
        return XEEN_BAD_INPUT;
    }
    if (g->lifecycle_boundary == EVENT_SPACE_WAIT_BOUNDARY) {
        return space_wait(g);
    }
    if (g->lifecycle_boundary == XEEN_LOOP_STEP_EVENT) {
        g->event_origin = EVENT_ORIGIN_STEP;
        return begin_event(g) ? XEEN_SERVICE_REQUIRED : XEEN_BAD_INPUT;
    }
    if (g->lifecycle_boundary == EVENT_TABLE_COMMAND_BOUNDARY) {
        return table_command(g, xeen_read16(&g->movement, XEEN_DS_INPUT_CODE));
    }
    return XEEN_BAD_INPUT;
}
const xeen_ui_request *xeen_game_pending(const xeen_game *g)
{
    return g && (g->phase == XG_WAITING || g->modal.active) && g->ui.pending ? &g->ui.active : NULL;
}
int xeen_game_respond(xeen_game *g, uint16_t input)
{
    const xeen_ui_request *q = xeen_game_pending(g);
    if (!q || g->continuous) {
        return 0; /* connected sessions own provider input */
    }
    if (q->kind == XUI_CONFIRM && input != 'Y' && input != 'N' && input != XEEN_KEY_ESCAPE) {
        return 0;
    }
    if (q->kind == XUI_DISMISS && !input) {
        return 0;
    }
    if ((q->kind == XUI_PRESENT || q->kind == XUI_TEXT) && input) {
        return 0;
    }
    g->response = input;
    g->response_ready = 1;
    drive(g);
    return 1;
}
void xeen_game_leave_event(xeen_game *g)
{
    if (!g || g->phase == XG_NAVIGATION || g->lifecycle) {
        return;
    }
    g->ui.pending = 0;
    g->response_ready = 0;
    g->phase = XG_NAVIGATION;
}
