/*
 * The session's fights (Phase 246, from game.c): the combat services (windows, text, the nested icons, a
 * cell's event), a fight's begin and its nesting inside a 337D7 loop (nest_push / nest_pop),
 * xeen_game_resume_combat, the 39430 cleanup's return (after_combat, combat_tail), the combat route
 * after a round, the round charge's rest scroll, and the Cast and Use returns inside combat. The modal
 * commands of 3914E are game_combat.c's.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Where native play stops when combat's end cannot hand back to the event dispatcher; each value is the original
 * address of the step (tools/xeen_replay/stops.json).
 */
enum {
    AFTER_COMBAT_STOP_CELL_METADATA = 0x1c844,  /* the party cell's flags */
    AFTER_COMBAT_STOP_CLEANUP_NESTED = 0x39577, /* a dispatcher suspended without nest_push's frame */
    AFTER_COMBAT_STOP_CLEANUP_EVENT = 0x39577,  /* the cleanup's event could not begin */
    COMBAT_TAIL_STOP_CONTACT_NESTED = 0x1c18a,  /* a suspended dispatcher the second dispatch would discard */
    COMBAT_TAIL_STOP_CONTACT_EVENT = 0x1c18a,   /* the contact's event could not begin */
    AFTER_COMBAT_STOP_RESTORE = 0x39430,        /* the exploration resources could not be restored after the fight */
    COMBAT_USE_STOP_ACTOR = 0x39223,            /* the Use command's actor is not a listed party member */
    COMBAT_BEGIN_STOP_UNREPORTED = 0x38f5f,     /* the fight's start stopped without reporting where */
    /* A command's dialog closed in combat and the round could not go on from it (bad input to the round): */
    COMBAT_REST_STOP_RESUME = 0x30db8,          /* ... after the round's rest scroll */
    COMBAT_CAST_STOP_RESUME = 0x391ed,          /* ... after the Cast dialog */
    COMBAT_USE_STOP_RESUME = 0x39249            /* ... after the Use dialog */
};
/* Combat's window service: window N opened or closed with its pixels kept and restored (panel_window); a window
 * opened anew also gets its frame and fill drawn. Original: 1871B / 18765 with 4F2A1 / 4F57B; 4F138's fill. */
static void combat_window_service(void *u, unsigned n, int open)
{
    /* 1871B: a closed window's 4F2A1 saves the pixels under it and draws
     * its frame and the 4F138 fill (xeen_text_frame). */
    xeen_game *g = u;
    int was_open = g->movement.ds[(uint16_t)(XEEN_DS_WINDOW_OPEN + n)] != 0;
    uint16_t r[4];
    panel_window(g, n, open);
    if (open && !was_open && g->presentation && g->presentation->framebuffer &&
        xeen_presentation_window(g->presentation, (uint16_t)n, r)) {
        (void)xeen_text_frame(g->presentation->framebuffer, g->presentation->framebuffer_size, r);
    }
}
/** Combat's text service: writes TEXT into WINDOW when there is a frame buffer. */
static void combat_text_service(void *u, uint16_t window, const char *text)
{
    xeen_game *g = u;
    if (g->presentation && g->presentation->framebuffer) {
        menu_window_text(g, window, WINDOW_TEXT_ONLY, text);
    }
}
/* A nested fight's combat icons (one started inside a fight whose scene's contact fired: the party defeated): LOAD
 * pushes combat.icn on the handle stack over the current icons and makes it the icons' handle; the free pops it
 * (freeing it and every later load) and puts the outer icons back, as xeen_combat_begin and the restore after
 * combat do. Refused for another movement.
 * Original: a nested 38F5F's 190A3("combat.icn") (96C1 up, 95A8[96C1] = 98D4, 18931 with reload 0) and 39514's
 * 190CF (4AA3E(98D4), 98D4 = 95A8[96C1], 96C1 down). */
static xeen_status combat_nested_icons(void *user, xeen_movement *m, int load)
{
    xeen_game *g = user;
    uint16_t h = 0;
    if (!g || m != &g->movement) {
        return XEEN_BAD_INPUT;
    }
    if (load) {
        xeen_sprite_archives a = sprite_archives(g, NULL);
        m->ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(m->ds[XEEN_DS_HANDLE_DEPTH] + 1u);
        xeen_write16(m, XEEN_DS_HANDLE_SLOT(m->ds), xeen_read16(m, XEEN_DS_SPRITE_HANDLE));
        if (xeen_sprite_load(&g->sprites, &a, m->ds[XEEN_DS_WORLD], ds_text(g, XEEN_TEXT_COMBAT_SPRITES), 0, &h) != XR_OK) {
            return XEEN_BAD_INPUT;
        }
        xeen_write16(m, XEEN_DS_SPRITE_HANDLE, h);
        return XEEN_OK;
    }
    h = xeen_read16(m, XEEN_DS_SPRITE_HANDLE);
    if (!h || !xeen_sprite_get(&g->sprites, h)) {
        return XEEN_BAD_INPUT;
    }
    (void)xeen_sprite_free_from(&g->sprites, h);
    xeen_write16(m, XEEN_DS_SPRITE_HANDLE, xeen_read16(m, XEEN_DS_HANDLE_SLOT(m->ds)));
    m->ds[XEEN_DS_HANDLE_DEPTH] = (uint8_t)(m->ds[XEEN_DS_HANDLE_DEPTH] - 1u);
    return XEEN_OK;
}
/* The step's deferred event inside a fight's scene: when a defeated party's nested fight ends in a scene whose step
 * had its event deferred (a monster was in range when the step's script ran), the original runs the whole
 * dispatcher for the party's cell there. Natively a fresh dispatcher runs to its return over the same DS with the
 * game's services, its windows the fight's own (combat_cell). Anything else is the stop 1C18A (XEEN_SERVICE_REQUIRED
 * here): an event already suspended (its stash and windows), no monster in view (the cell's script would run inside
 * the fight), an object at the cell ("Nothing here." inside the fight), a request for input, or a return without
 * the deferral.
 * Original: 1C18A's 310B4 after the nested 38F5F (1C183 found 9756; phase159 orig159 run orig-1c18a, 7Ch): the head
 * 190ED/1919B and the pending treasure's stash, 313FD with 98B4 not FFh (31404: 9756 = 1 again), 33690's epilogue
 * (186C0 closes every open window, 2F869, the stash back, 18FFF), back to 1C18F; 98D9 the object (337D7). */
static xeen_status combat_cell_event(void *user, xeen_movement *m)
{
    xeen_game *g = user;
    xeen_event_dispatch *d;
    xeen_dispatch_status s;
    int ok;
    if (!g || m != &g->movement || !g->lifecycle) {
        return XEEN_BAD_INPUT;
    }
    if (g->nest.active || g->dispatcher.status == XD_INPUT_OR_SERVICE) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (m->ds[XEEN_DS_VIEW_MONSTER(0)] == XEEN_VIEW_NO_MONSTER || m->ds[XEEN_DS_EVENT_OBJECT]) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (!(d = malloc(sizeof *d))) {
        return XEEN_BAD_INPUT;
    }
    xeen_presentation_trace_extra("evmark combat-cell-event 1C18A");
    s = xeen_event_dispatch_begin(d, m->ds, sizeof m->ds, g->continuous ? XEEN_DS_SEGMENT : XEEN_ANALYSIS_DS_SEGMENT);
    g->combat_cell = 1;
    if (s == XD_RUNNING) {
        s = xeen_event_dispatch_run(d, g->resident, g->resources.record_bytes, &g->index, m->ds,
                                    sizeof m->ds, 256, xeen_event_resource_service, &g->resources);
    }
    g->combat_cell = 0;
    ok = (s == XD_RETURNED || s == XD_RETURNED_EXIT) && m->ds[XEEN_DS_EVENT_DEFERRED] == 1u;
    free(d);
    return ok ? XEEN_OK : XEEN_SERVICE_REQUIRED;
}
/* Starts a fight (a scene's contact or Shoot's melee): combat's callbacks bound, then the fight begun. Waiting for a
 * command, the session waits at combat's command poll; a party defeated at the entry goes straight to the fight's
 * end (after_combat); any other stop is reported at the boundary combat gave (or its own unreported-stop site).
 * Original: 38F5F for a contact owned by the scene suffix (1C17E) or by Shoot (17EEA). */
xeen_status begin_combat(xeen_game *g)
{
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    xeen_status status;
    g->combat.ranged_delivery = combat_ranged_delivery;
    g->combat.ranged_user = g;
    g->combat.nested_icons = combat_nested_icons;
    g->combat.cell_event = combat_cell_event;
    g->combat.windows = (xeen_combat_windows){g, combat_window_service, combat_text_service};
    xeen_presentation_trace("mark combat-begin");
    status = xeen_combat_begin(&g->combat, &g->movement, g->monster_database,
                               g->monster_database_size, &g->sprites, &archives, g->presentation,
                               g->event_random, g->event_random_user,
                               game_audio(g));
    if (status == XEEN_SERVICE_REQUIRED) {
        if (g->combat.phase == XC_AWAITING_COMMAND) {
            g->lifecycle_boundary = XEEN_COMBAT_COMMAND_POLL;
            g->command_status = XGC_PENDING_INPUT;
        } else if (g->combat.phase == XC_INACTIVE && g->combat.last_boundary == XEEN_COMBAT_DEFEATED_AT_ENTRY) {
            return after_combat(g, status); /* defeated at entry */
        } else if (g->combat.last_boundary) {
            g->lifecycle_boundary = g->combat.last_boundary; /* where combat stopped */
            g->command_status = XGC_UNSUPPORTED;
        } else {
            g->lifecycle_boundary = COMBAT_BEGIN_STOP_UNREPORTED;
            g->command_status = XGC_UNSUPPORTED;
        }
    }
    return status;
}
static xeen_status combat_tail(xeen_game *, xeen_status);
/**
 * A fight that starts from the scene of the "Nothing here." wait (the key loop's scene can make contact) runs while
 * the event that showed the message is still waiting for its key. The original keeps that event on its stack while
 * the fight, its treasure and a nested event run. Natively the waiting event is the dispatcher, its execution copy
 * and the wait's pending request with its flags: nest_push keeps them aside, so the live ones are free for the
 * nested treasure and dispatch, and nest_pop puts them back. Returns 1 when pushed, 0 when the state is not that
 * wait (nothing to suspend), and -1 when a nested event is already active (a second level: an explicit stop).
 * Original: 337D7's key loop 3380E..33832 (33815 1B503(1) -> 1C17E 38F5F); the outer 310B4 suspended in its phase 9
 * call; 394C5's treasure; 1C18A's nested 310B4.
 */
static int nest_push(xeen_game *g)
{
    if (!g->lifecycle || !g->nothing_wait || !g->ui.pending ||
        g->dispatcher.status != XD_INPUT_OR_SERVICE || g->dispatcher.boundary != XEEN_REQUEST_NOTHING_HERE) {
        return 0;
    }
    if (g->nest.active) {
        return -1;
    }
    g->nest.dispatcher = g->dispatcher;
    g->nest.execution = g->execution;
    g->nest.ui_active = g->ui.active;
    g->nest.ui_pending = g->ui.pending;
    g->nest.event_origin = g->event_origin;
    g->nest.nothing_wait = g->nothing_wait;
    g->nest.page_wait = g->page_wait;
    g->nest.active = 1;
    g->nest.armed = 0;
    g->ui.pending = 0;
    g->event_origin = EVENT_ORIGIN_NONE;
    g->nothing_wait = 0;
    g->page_wait = 0;
    xeen_presentation_trace_extra("evmark nest-push 337d7");
    return 1;
}
/* The contact scene has ended: the event kept by nest_push waits for its key again. The wait's request carries no
 * text in the original (its key loop reads none); the native copy of the event text is refreshed, as the nested
 * events may have changed it. Original: 1C1B3's RETF 1C1C0 to 3381A, the 3381B poll; A827. */
void nest_pop(xeen_game *g)
{
    size_t k = 0;
    g->dispatcher = g->nest.dispatcher;
    g->execution = g->nest.execution;
    g->ui.active = g->nest.ui_active;
    g->ui.pending = g->nest.ui_pending;
    while (k < 999u && g->movement.ds[XEEN_DS_TEXT_BUFFER + k]) {
        g->ui.active.primary[k] = g->movement.ds[XEEN_DS_TEXT_BUFFER + k];
        ++k;
    }
    g->ui.active.primary[k] = 0;
    g->ui.active.primary_size = k + 1u;
    g->event_origin = g->nest.event_origin;
    g->nothing_wait = g->nest.nothing_wait;
    g->page_wait = g->nest.page_wait;
    g->nest.active = 0;
    g->nest.armed = 0;
    xeen_presentation_trace_extra("evmark nest-pop 337d7");
}
xeen_status xeen_game_resume_combat(xeen_game *g)
{
    int nested;
    /* After the cleanup's own event (after_combat) the fight's tail goes on, back to the contact scene's frame wait.
     * Original: 3957C after 39577's 310B4 (event origin 4). */
    if (g && g->continuous && g->phase == XG_STOPPED && g->lifecycle_boundary == COMBAT_CLEANUP_TAIL_BOUNDARY &&
        g->cleanup_event && g->presentation) {
        g->cleanup_event = 0;
        g->lifecycle_boundary = XEEN_SCENE_FRAME_WAIT;
        g->command_status = XGC_EXTERNAL;
        return combat_tail(g, XEEN_SERVICE_REQUIRED);
    }
    if (!g || !g->continuous || g->phase != XG_STOPPED || g->lifecycle_boundary != COMBAT_CONTACT_BOUNDARY ||
        !g->presentation) {
        return XEEN_BAD_INPUT;
    }
    g->shoot_after_combat = 0; /* a scene contact returns to its 1C1B3 frame wait */
    if ((nested = nest_push(g)) < 0) {
        g->command_status = XGC_UNSUPPORTED;
        return XEEN_SERVICE_REQUIRED;
    }
    return begin_combat(g);
}
/* The fight's cleanup is done: the exploration resources come back; when the fight began under a sign, the party's
 * cell's event runs again (it may show the sign again and open its own waits), and the fight's tail follows its
 * return; otherwise the tail runs now (combat_tail). Original: after 39430; 3957C. */
xeen_status after_combat(xeen_game *g, xeen_status status)
{
    if (!restore_after_combat(g)) {
        g->lifecycle_boundary = AFTER_COMBAT_STOP_RESTORE;
        g->command_status = XGC_UNSUPPORTED;
        return XEEN_SERVICE_REQUIRED;
    }
    /* The fight began while a sign was up (operations 2, 4 and 39 keep their text until a step clears it): when the
     * party's cell has a step event, the dispatcher runs again for that cell (the combat party map still in effect,
     * the UI mode exploration's). It can defer
     * the scene's event again, show the sign again and open its own waits.
     * Original: 39558; 38F6D's [bp-4] = 3066 (17D88 clears it); 3955E 1C844(ED97, ED98) (F482); 39577 310B4 after
     * 190CF, 43287, 28D80 and 306A = FFh, FAF8 still 2 and 9A1C 1; its 313FD writes 9756 (1C183 sees it); 3957C
     * follows its return (event origin 4). */
    if (g->combat.entry_3066) {
        xeen_movement *m = &g->movement;
        if (xeen_metadata(m) != XEEN_OK) {
            return game_stop(g, AFTER_COMBAT_STOP_CELL_METADATA);
        }
        if (xeen_read16(m, XEEN_DS_CELL_STEP_EVENT)) {
            /* explicit: a dispatcher suspended without nest_push's frame (a contact in ops 2/4/39's own 1B503) */
            if (g->lifecycle && !g->nest.active && g->dispatcher.status == XD_INPUT_OR_SERVICE) {
                return game_stop(g, AFTER_COMBAT_STOP_CLEANUP_NESTED);
            }
            g->cleanup_event = 1;
            g->event_origin = EVENT_ORIGIN_CLEANUP;
            g->phase = XG_STOPPED;
            xeen_presentation_trace_extra("evmark cleanup-event 39577");
            if (!begin_event(g)) {
                g->event_origin = EVENT_ORIGIN_NONE;
                g->cleanup_event = 0;
                return game_stop(g, AFTER_COMBAT_STOP_CLEANUP_EVENT);
            }
            return XEEN_SERVICE_REQUIRED;
        }
    }
    return combat_tail(g, status);
}
/* The fight's tail: after the panel's Mr Wizard the party is moved (wizard_move); then the fight's starter goes on:
 * Shoot's tail, or the contact scene, which first runs the step's deferred event when there is one, else waits for
 * its frame. Original: 3957C..395CB (the move with DI = 1 after 392D3, FAF8 = 1); 38F5F's callers 17EEF and
 * 1C183. */
static xeen_status combat_tail(xeen_game *g, xeen_status status)
{
    uint32_t stop;
    if (g->combat.entry_3066) {
        g->movement.ds[XEEN_DS_COMBAT_MODE] = 1; /* 395C3 after 3955E (FAF8 for the others: cleanup) */
    }
    if (g->combat.wizard) {
        g->combat.wizard = 0;
        if ((stop = wizard_move(g, WIZARD_COMBAT_LOAD_SITE)) != 0) {
            return game_stop(g, stop);
        }
        g->movement.ds[XEEN_DS_COMBAT_MODE] = 1;
    }
    /* A live nested fight's cleanup returns into the outer fight's scene: no Shoot tail there (the outer fight
     * keeps it). Original: 393F0's 1C183. */
    if (g->shoot_after_combat && !g->combat.outer_active) {
        g->shoot_after_combat = 0;
        return shoot_tail(g);
    }
    /* The contact scene continues: an event the step deferred (a monster was in view) runs now, through the
     * dispatcher again (event_return continues); then the action flag is cleared and the scene's frame wait follows.
     * Original: 1C183 (9756 set at 313FD/31404 while 98B4 was not FFh); 1C18A 310B:0004 (event origin 3); 1C18F
     * 3067 = 0; the 1C1B3 frame wait. */
    if (g->movement.ds[XEEN_DS_EVENT_DEFERRED]) {
        /* A suspended event that nest_push did not keep would be lost to this second dispatch: an explicit stop. */
        if (g->lifecycle && !g->nest.active && g->dispatcher.status == XD_INPUT_OR_SERVICE) {
            return game_stop(g, COMBAT_TAIL_STOP_CONTACT_NESTED);
        }
        g->event_origin = EVENT_ORIGIN_CONTACT;
        g->phase = XG_STOPPED;
        if (!begin_event(g)) {
            g->event_origin = EVENT_ORIGIN_NONE;
            return game_stop(g, COMBAT_TAIL_STOP_CONTACT_EVENT);
        }
        return XEEN_SERVICE_REQUIRED;
    }
    g->movement.ds[XEEN_DS_PARTY_ACTED] = 0;
    if (g->nest.active && !g->combat.outer_active) {
        g->nest.armed = 1; /* 1C18F, the contact scene's tail */
    }
    return status;
}
/* Where a combat step's result goes: the sky reload at a night change (then combat's step after it), the unrested
 * morning's rest message, the cleanup's treasure dialog, the cleanup's return to exploration, else the next command
 * poll or the exact unsupported boundary. Original: 30004, 30DB3, 2EA8E, 39430, 3912C. */
xeen_status combat_route(xeen_game *g, xeen_status status, uint32_t boundary)
{
    xeen_sprite_archives archives;
    if (boundary == XEEN_TICK_SKY_RELOAD) {
        archives = sprite_archives(g, NULL);
        if (xeen_sprites_reload_sky(&g->sprites, &archives, &g->movement) == XR_OK) {
            status = xeen_combat_after_sky_rng(
                    &g->combat, &g->movement, g->monster_database, g->monster_database_size,
                    g->event_random, g->event_random_user,
                    game_audio(g), &boundary);
        }
    }
    /* The round charge's 30DB3 (an unrested morning in combat):
     * 18F2F(DS:629B, 1) with combat's 1B503(1) scenes (scroll owner 13). */
    if (boundary == XEEN_TICK_UNFED_DAWN && g->combat.phase == XC_UNSUPPORTED_CONTINUATION &&
        rest_scroll(g, SCROLL_COMBAT_REST, g->combat.rest_tick)) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (boundary == XEEN_TREASURE_FOUND && g->combat.phase == XC_AWAITING_TREASURE) {
        return combat_treasure(g);
    }
    if (boundary == XEEN_COMBAT_OVER && g->combat.phase == XC_INACTIVE) {
        return after_combat(g, status);
    }
    g->lifecycle_boundary = boundary;
    g->phase = XG_STOPPED;
    g->command_status = boundary == XEEN_COMBAT_COMMAND_POLL ? XGC_PENDING_INPUT : XGC_UNSUPPORTED;
    return status;
}
/* The rest's message in combat was dismissed (its UI mode back to combat's): the round charge's rest goes on, then
 * combat's route (the sky reload, the treasure, the cleanup or the next command poll); a resume combat cannot take
 * stops at the rest's resume site. Original: 30DB3's scroll (18FF2 restored 9A1C 2), 30DC2..; 30004, 3912C. */
xeen_status combat_rest_closed(xeen_game *g)
{
    uint32_t boundary = 0;
    xeen_status status;
    status = xeen_combat_after_rest_rng(&g->combat, &g->movement, g->monster_database,
                                        g->monster_database_size, g->event_random,
                                        g->event_random_user,
                                        game_audio(g), &boundary);
    if (status == XEEN_BAD_INPUT) {
        g->lifecycle_boundary = COMBAT_REST_STOP_RESUME;
        g->phase = XG_STOPPED;
        g->command_status = XGC_UNSUPPORTED;
        return status;
    }
    return combat_route(g, status, boundary);
}
/* The Cast dialog opened from combat has closed, PAID telling whether a spell was paid: combat's after-cast step
 * runs, then its route (combat_route); a resume combat cannot take stops at the cast's resume site.
 * Original: 3D8C1 returned to 391ED (AX: paid). */
xeen_status combat_cast_return(xeen_game *g, int paid)
{
    uint32_t boundary = 0;
    xeen_status status;
    status = xeen_combat_after_cast(&g->combat, &g->movement, g->monster_database,
                                    g->monster_database_size, g->event_random, g->event_random_user,
                                    game_audio(g), paid, &boundary);
    if (status == XEEN_BAD_INPUT) {
        g->lifecycle_boundary = COMBAT_CAST_STOP_RESUME;
        g->phase = XG_STOPPED;
        g->command_status = XGC_UNSUPPORTED;
        return status;
    }
    return combat_route(g, status, boundary);
}
/**
 * Combat's Use command: opens the items dialog for the acting member (for the dead party's poll,
 * the record that 9720[-1] selects, as in the original); the dialog ends at combat_use_return.
 * Original: 39223 Use: 44F92(the actor's record, 7) (owner 4); the return 39249.
 */
xeen_status combat_use(xeen_game *g)
{
    uint8_t *ds = g->movement.ds;
    uint8_t actor = ds[XEEN_DS_TURN_ACTOR];
    /* The dead party's poll after a nested 38F5F (97FF FFh, F60A;
     * established by Phase 159's original-byte runs): 39232 reads 9720[CBW 97FF] =
     * 9720[-1] = 971F (the party count, at most 6): 44F92 gets that record
     * (BC6A, the scratch record, for six members). */
    if (actor == XEEN_NO_TURN && ds[XEEN_DS_PARTY_DEFEATED] && g->combat.nested_entries &&
        ds[XEEN_DS_COMBAT_PARTY_COUNT] <= XEEN_PARTY_SLOTS) {
        g->use_done = 0;
        return items_begin(g, XEEN_PARTY_RECORD(ds[XEEN_DS_COMBAT_PARTY_COUNT]), ITEMS_COMBAT, ITEMS_OWNER_COMBAT);
    }
    if (actor >= ds[XEEN_DS_COMBAT_PARTY_COUNT] || ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + actor)] >= 6u) {
        return game_stop(g, COMBAT_USE_STOP_ACTOR);
    }
    g->use_done = 0;
    return items_begin(g, XEEN_PARTY_RECORD(ds[(uint16_t)(XEEN_DS_COMBAT_PARTY + actor)]), ITEMS_COMBAT,
                       ITEMS_OWNER_COMBAT);
}
/*
 * Returns from the combat Use dialog (39249): xeen_combat_after_use continues the round, told
 * whether an item was used. Bad input stops the game at 39249; any other result is routed like
 * every combat step (combat_route).
 */
xeen_status combat_use_return(xeen_game *g)
{
    uint32_t boundary = 0;
    xeen_status status;
    int used = g->use_done;
    g->use_done = 0;
    status = xeen_combat_after_use(&g->combat, &g->movement, g->monster_database,
                                   g->monster_database_size, g->event_random, g->event_random_user,
                                   game_audio(g), used, &boundary);
    if (status == XEEN_BAD_INPUT) {
        g->lifecycle_boundary = COMBAT_USE_STOP_RESUME;
        g->phase = XG_STOPPED;
        g->command_status = XGC_UNSUPPORTED;
        return status;
    }
    return combat_route(g, status, boundary);
}
