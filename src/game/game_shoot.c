/*
 * Shoot (Phase 246, from game.c): the 287FF volley's presentation, 17EF4's charge tail (28178, 17264, 17F07;
 * also a paid spell's), the tick and surface continuations, the volley itself and the melee prefix.
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when Shoot cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). */
enum {
    SHOOT_STOP_NO_HOST = 0x17e98,            /* combat's monsters, sprites or random source missing */
    SHOOT_STOP_REFUSED_SOUND = 0x1880e,      /* no one able to shoot: the refusal's effect */
    SHOOT_STOP_NO_PRESENTATION = 0x17ead,
    SHOOT_STOP_MELEE_NO_PRESENTATION = 0x17eea,
    /* variable stop sites (stops_variable.json) */
    SHOOT_STOP_VOLLEY = 0x2a2d4              /* the shot's volley stopped without reporting where */
};
/* An exploration volley's scenes draw through the game's presentation, as combat's do (a surviving monster's hit
 * effect in colour mode 2 or 5 makes its draws there): the combat state gets the presentation when it has none
 * (xeen_combat_begin binds it for a fight). Original: 281A7 / 2A2D4's 1B503(1) scenes (50525). */
void volley_presentation(xeen_game *g)
{
    if (!g->combat.presentation) {
        g->combat.presentation = g->presentation;
    }
}
/* Shoot's volley and what follows it: the volley presented and shot (xeen_combat_shoot; a treasure it drops opens
 * the treasure dialog, any other refusal stops at its boundary), then shoot_after_volley. Also the return from a
 * fight the volley started (entered at 17EEA), once its cleanup has restored exploration.
 * Original: 17EEF..17F07: 2A2D4, then 28178 (its 28195 update may deliver queued monster shots), 17264, 17F07;
 * 38F5F, 39430. */
xeen_status shoot_tail(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    uint32_t boundary = 0;
    volley_presentation(g);
    if (xeen_combat_shoot(&g->combat, m, g->monster_database, g->monster_database_size, &g->sprites,
                          &archives, g->event_random, g->event_random_user,
                          game_audio(g), &boundary) != XEEN_OK) {
        return boundary == XEEN_TREASURE_CALL ? treasure_dialog(g, TREASURE_VOLLEY)
                                    : game_stop(g, boundary ? boundary : SHOOT_STOP_VOLLEY);
    }
    return shoot_after_volley(g);
}
/* After the volley: one more shot counted, then the time charge's tail (charge_tail). Original: 2A2D4 returned to
 * 17EF4. */
xeen_status shoot_after_volley(xeen_game *g)
{
    ++g->shots;
    return charge_tail(g);
}
/* The time a volley or a paid spell takes: one tick with its services (monsters, delivery); a party that needs rest
 * at that dawn gets the "needs rest" scroll first. Then shoot_after_tick (the delivery's tail, the surface, the
 * command tail). Original: 17EF4: the 28178 charge, 17264 and 17F07. */
xeen_status charge_tail(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_gameplay_tick tick;
    xeen_gameplay_services services = session_services(g);
    tick = xeen_gameplay_tick_wait_services(m, g->monster_database, g->monster_database_size,
                                            &services);
    if (tick.boundary == XEEN_TICK_UNFED_DAWN && rest_scroll(g, SCROLL_SHOOT_REST, tick)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return shoot_after_tick(g, tick);
}
/* After a volley's or a paid spell's tick: the monsters' ranged attacks it queued are delivered (ranged_tick; any other
 * end of the tick stops there), then the surface's effect (a desert's "needs rest" first) and shoot_after_surface.
 * Original: 28178 returned to 17EF4: the 2AAB1 delivery tail, 17264, 17F07. */
xeen_status shoot_after_tick(xeen_game *g, xeen_gameplay_tick tick)
{
    xeen_movement *m = &g->movement;
    xeen_caller_result surface;
    tick = ranged_tick(g, tick);
    if (tick.boundary != XEEN_TICK_POST_ACTION || tick.status == XT_INPUT_ERROR) {
        return game_stop(g, tick.boundary);
    }
    surface = xeen_post_action_surface_native(m);
    surface = desert_surface(g, surface);
    if (surface.boundary == XEEN_TICK_UNFED_DAWN && desert_scroll(g, DESERT_SHOOT, tick)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return shoot_after_surface(g, surface);
}
/* The party's cell surface after a Shoot (or a paid spell's tick): when it asks for its damage or defeat scene, that
 * scene runs first; a fall begun here is marked as Shoot's (its tail is the command's, never the step's); any other
 * request stops at its boundary (an edge transition remembered as the tail). Otherwise the command's tail follows.
 * Original: 17264 returned to Shoot's 17EFF; its 1733D damage scene or 17377 (F60A), RETF 17388 to 17EFD, 17F07. */
xeen_status shoot_after_surface(xeen_game *g, xeen_caller_result surface)
{
    if (surface.status != XEEN_OK) {
        /* 17264's damage (17344) and F60A (17380) scenes, then 17F07. */
        if (g->continuous && (surface.boundary == XEEN_SURFACE_DAMAGE_SCENE || surface.boundary == XEEN_SURFACE_DEFEAT_SCENE)) {
            return surface_scene(g, surface.boundary == XEEN_SURFACE_DAMAGE_SCENE ? 1 : 2, 4);
        }
        g->transition_tail = surface.boundary == XEEN_SURFACE_EDGE;
        /* 1731B's 173A0 inside the 17264 that Shoot/Cast's 17EFA
         * called: 17264 still runs its 1733D damage scene (or 17377 F60A),
         * then RETF 17388 to 17EFD, JMP 17F07: the command tail, never the
         * movement's 17D50 step tail (EFEE, 9775, the step sound). */
        if (surface.boundary == XEEN_LOOP_FALL && g->movement.fall_caller == SURFACE_MOVEMENT) {
            g->movement.fall_caller = SURFACE_SHOOT;
        }
        return game_stop(g, surface.boundary);
    }
    return command_tail(g);
}
/* Exploration's Shoot: without a member able to shoot, the refusal sound and the command's end without time;
 * otherwise the volley (its monsters' update when the countdown is set, its scene), then the melee when a monster
 * stands in the party's cell (shoot_melee), and Shoot's tail. Stops without the session's monsters, sprites or
 * random source. Original: 17E98: 28149 (39721; FX 21), the 96C0-gated 2A701 and 1B503(1), 38F5F (97F6, 97F9),
 * 17EEF. */
xeen_status shoot(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned i, able = 0;
    xeen_status status;
    if (!play_prepared(g)) {
        return game_stop(g, SHOOT_STOP_NO_HOST);
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, 0x53u);
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        if (xeen_combat_has_missile(m->ds + XEEN_PARTY_RECORDS + i * XEEN_CHARACTER_BYTES)) {
            able = 1;
            break;
        }
    }
    if (!able) {
        if (sound_effect(g, XEEN_CUE_REFUSED) !=
            XEEN_OK) {
            return game_stop(g, SHOOT_STOP_REFUSED_SOUND);
        }
        return command_tail(g);
    }
    if (m->ds[XEEN_DS_MONSTER_COUNTDOWN]) {
        g->service_boundary = 0;
        status = run_monster_update(g);
        if (status != XEEN_OK) {
            return game_stop(g, g->service_boundary ? g->service_boundary : RELEASE_STOP_MONSTER_UPDATE);
        }
        /* 17EAD's 1B503(1) is the whole scene (entered at 4EA35);
         * its own contact test (1C17E) can start 38F5F; 17EB5 follows. */
        if (!g->presentation) {
            return game_stop(g, SHOOT_STOP_NO_PRESENTATION);
        }
        g->shoot_prefix = 1;
        return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
    }
    return shoot_melee(g);
}
/* Shoot's melee: a monster in the party's cell, exploring or resting, with no monster shot queued and no missile in
 * flight, starts a fight (begin_combat; its cleanup runs Shoot's tail); otherwise Shoot's tail follows. Stops without
 * a presentation. Original: 17EB5: 38F5F (97F6, 97F9), then 17EEF. */
xeen_status shoot_melee(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    if (xeen_party_cell_fight_due(m->ds)) { /* no monsters-enabled test here, unlike the scene's */
        if (!g->presentation) {
            return game_stop(g, SHOOT_STOP_MELEE_NO_PRESENTATION);
        }
        g->shoot_after_combat = 1;
        g->phase = XG_STOPPED;
        status = begin_combat(g);
        if (status != XEEN_SERVICE_REQUIRED) {
            g->shoot_after_combat = 0;
        }
        return status;
    }
    return shoot_tail(g);
}
