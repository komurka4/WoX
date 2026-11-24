/*
 * Navigation (Phase 246, from game.c): the cell's event (begin_event) and xeen_game_interact, the session's
 * gameplay services (the sky reload, the bar tick), a step's execution and the desert's surface, 17264's
 * surface scenes with their 30E2D damage, the door's scene (178F6), the navigation tail and finish after
 * a tick, xeen_game_navigate, and the world entry (its passes, start and resumption).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when this unit cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json, stops_variable.json). */
enum {
    SURFACE_STOP_FALL_DAMAGE = 0x1764b,      /* a fall's damage stopped without reporting where */
    SURFACE_STOP_DAMAGE = 0x17362,           /* a surface's damage stopped without reporting where */
    NAVIGATION_STOP_LOOP_EXIT = 0x17f28      /* the loop is to end (XEEN_TICK_LOOP_EXIT) */
};
/** Starts the event dispatcher on the current cell; returns 0 when no event begins there. */
int begin_event(xeen_game *g)
{
    g->acted = 1;
    if (g->lifecycle) {
        if (xeen_event_dispatch_begin(&g->dispatcher, g->movement.ds, sizeof g->movement.ds,
                                      g->continuous ? XEEN_DS_SEGMENT : XEEN_ANALYSIS_DS_SEGMENT) != XD_RUNNING) {
            return 0;
        }
        drive(g);
        return 1;
    }
    if (xeen_event_execution_begin(&g->execution, g->movement.ds, sizeof g->movement.ds) !=
        XEE_DONE) {
        return 0;
    }
    drive(g);
    return 1;
}
int xeen_game_interact(xeen_game *g)
{
    uint16_t eligible = 0;
    if (!g || g->phase != XG_NAVIGATION) {
        return 0;
    }
    g->acted = 1;
    if (xeen_slice_interaction_gate(&g->movement, &eligible) != XEEN_OK || !eligible) {
        g->event_status = XEE_EXTERNAL;
        g->execution.state.boundary = INTERACTION_BOUNDARY;
        if (g->lifecycle) {
            g->command_status = XGC_EXTERNAL;
            g->lifecycle_boundary = INTERACTION_BOUNDARY;
            g->phase = XG_STOPPED;
        }
        return 0;
    }
    return begin_event(g);
}
/* The sky's reload for the time of day, on the exploration sprite cache (only once the map's sprites are prepared).
 * Returns 1 when reloaded. Original: 30004 -> 2A0FB (8C92 and the F45C sky). */
static int reload_sky(void *user, xeen_movement *m)
{
    xeen_game *g = user;
    xeen_sprite_archives archives = sprite_archives(g, NULL);
    return g->map_sprites_prepared && xeen_sprites_reload_sky(&g->sprites, &archives, m) == XR_OK;
}
/** Clock hook: redraws the party bar. */
static void tick_bar(void *user)
{
    bar_redraw(user);
} /* 2FFE1 2ABB8(1) */
/* The clock and tick services of this session for the world's rules: the event random source, the sky's reload
 * and the party bar's redraw (each with the game as its user). */
xeen_gameplay_services session_services(xeen_game *g)
{
    xeen_gameplay_services s = {g->event_random, g->event_random_user, reload_sky, g, tick_bar, g};
    return s;
}
/* A desert cell crossed without a Navigator costs time: 170 minutes added through the session's clock services (a
 * stop there stops the step); then the surface goes on. Any other result passes through unchanged.
 * Original: 172CD (2BEF9(10) 0): 30CF3(170); then 172D5 onward. */
xeen_caller_result desert_surface(xeen_game *g, xeen_caller_result r)
{
    xeen_gameplay_services services;
    uint32_t stop;
    if (r.boundary != XEEN_SURFACE_DESERT_CLOCK) {
        return r;
    }
    services = session_services(g);
    if ((stop = xeen_gameplay_clock_add(&g->movement, 0xaa, &services)) != 0) {
        xeen_caller_result s = {XEEN_SERVICE_REQUIRED, stop};
        g->movement.status = XEEN_SERVICE_REQUIRED;
        return s;
    }
    return xeen_post_action_surface_after_desert_native(&g->movement);
}
/* The command's tick for the session's kind: a restored continuous session's with its services and the monster
 * database, a restored single-command session's extended tick, else the plain tick. */
static xeen_gameplay_tick begin_tick(xeen_game *g, uint16_t command, xeen_gameplay_services *services)
{
    if (!g->restored) {
        return xeen_gameplay_tick_begin(&g->movement, command);
    }
    if (!g->continuous) {
        return xeen_gameplay_tick_begin_extended(&g->movement, command);
    }
    return xeen_gameplay_tick_begin_services(&g->movement, command, g->monster_database, g->monster_database_size,
                                             services);
}
/**
 * Executes navigation COMMAND (a step, turn or strafe). With the lifecycle: the session's command tick (begin_tick),
 * then a continuous session's two suspensions, an unfed dawn that Rest's scroll takes over and a door's scene
 * (178E2..178F1: the door's stage waits at its own 4EA35), else the tick's tail (navigation_tail). Without it (the
 * slice API): the bare step (xeen_slice_navigate) and, after a move, a turn or a rejection, the cell's step event
 * (begin_event). Returns the navigation result (XSN_SERVICE_REQUIRED while the door's scene runs).
 */
xeen_slice_navigation_result execute_navigation(xeen_game *g, uint16_t command)
{
    g->acted = 1;
    g->navigation_command = command;
    if (g->lifecycle) {
        xeen_gameplay_services services = session_services(g);
        xeen_gameplay_tick tick = begin_tick(g, command, &services);
        if (g->continuous && tick.boundary == XEEN_TICK_UNFED_DAWN && rest_scroll(g, SCROLL_MOVE_REST, tick)) {
            return tick.navigation;
        }
        if (g->continuous && g->door_pending && tick.boundary == XEEN_TICK_MOVE_SERVICE) {
            /* 178E2..178F1: 9C6E = 1 and sound 2Fh are done; 1B503(1) runs at
             * its own 4EA35 and returns to door_after_scene (1C1B3 wait). */
            g->door_pending = 0;
            g->door_stage = 1;
            g->door_command = command;
            g->phase = XG_STOPPED;
            g->scene_argument = 1;
            g->lifecycle_boundary = XEEN_TIMER_SCENE_MARK;
            g->command_status = XGC_EXTERNAL;
            return XSN_SERVICE_REQUIRED;
        }
        g->door_pending = 0;
        return navigation_tail(g, tick);
    }
    g->navigation = xeen_slice_navigate(&g->movement, command);
    if (g->navigation == XSN_MOVED || g->navigation == XSN_TURNED || g->navigation == XSN_BLOCKED) {
        if (xeen_metadata(&g->movement) == XEEN_OK && xeen_read16(&g->movement, XEEN_DS_CELL_STEP_EVENT)) {
            (void)begin_event(g);
        }
    }
    return g->navigation;
}
/* A damaging surface's scene (lava: 100 fire damage; a fall onto the sky or the clouds: 100), the step parity already
 * flipped: one scene pass without a frame mark, whose frame wait returns to surface_after_scene. STAGE 1 is the
 * damage's scene, 2 a defeat's; OWNER is who goes on after it (enum surface_owner). Original: 17344, the 17264
 * damage branch (F5DF flipped); 1B503(1) entered at its own 4EA35; the 1C1B3 wait. */
xeen_status surface_scene(xeen_game *g, int stage, int owner)
{
    g->surface_stage = stage;
    g->surface_owner = owner;
    g->surface_damage = 100u;
    return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
}
/* The surface's scene has returned. After a defeat's scene, Rest's end or Shoot's tail goes on (else the movement's
 * tail). After the damage's scene the whole party takes the damage (an event fall's with damage type 0), with the
 * member selector cleared meanwhile, and the damage's own scene follows (damage_scene). The party can be defeated
 * there; the movement's tail then leaves the exploration loop.
 * Original: 17349..17375: 98D7 saved and cleared, 2E68D(SI, 972E, 0) (its 2F869 may set F60A), 98D7 restored, F5DF
 * flipped back; 17386 (no 17377 F60A scene) to the 17D50 movement tail, whose 17F07 exits at 17F28; 17380 (the F60A
 * scene returns at 17386, Rest at 29120). */
xeen_status surface_after_scene(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t saved = m->ds[XEEN_DS_MEMBER_SELECTOR];
    uint32_t boundary = 0;
    xeen_native_audio *audio = game_audio(g);
    int stage = g->surface_stage;
    g->surface_stage = 0;
    if (stage == 2 && g->surface_owner == SURFACE_REST_WAKE) {
        return rest_scene(g, 2);
    }
    if (stage == 2 && g->surface_owner == SURFACE_SHOOT) {
        return command_tail(g);
    }
    if (stage == 1) {
        m->ds[XEEN_DS_MEMBER_SELECTOR] = 0;
        /* The members' damage and their hit frames; then one scene pass (damage_scene). Original: 173A0(1)'s own
         * 2E68D (1764B) has type 0; 2E8B3 30E2D(2): 4EA0D, one 1B503(1) (its frame wait makes 4EA1C at least 2). */
        if (xeen_combat_party_damage_hits(
                    &g->combat, m, g->monster_database, g->monster_database_size, g->surface_damage,
                    g->surface_owner == SURFACE_FALL ? 0u : xeen_read16(m, XEEN_DS_DAMAGE_TYPE), 0, g->event_random,
                    g->event_random_user, audio, g->presentation, &boundary) != XEEN_OK) {
            /* 3 is SURFACE_FALL: the literal stays, stops_variable.json keys this site by its text. */
            return game_stop(g, boundary ? boundary : g->surface_owner == 3 ? SURFACE_STOP_FALL_DAMAGE : SURFACE_STOP_DAMAGE);
        }
        return damage_scene(g, saved);
    }
    return post_action_surface_tail(g);
}
/* The party damage's own scene pass, for a surface or a trap (SAVED: the member selector as it was before the
 * damage): a frame mark and one scene; damage_after_scene continues. Original: 2E68D's 30E2D(2) (30E3C) after the
 * damage; 98D7 as 17349 saved it; 1B503(1) after 4EA0D. */
xeen_status damage_scene(xeen_game *g, uint8_t saved)
{
    g->damage_member_selector = saved;
    g->damage_stage = 1;
    g->scene_site = 0x30e3cu;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/* The damage's scene has returned: the party bar redrawn and the defeat test made; a trap goes back to its lock
 * roll; else the member selector and the step parity are restored and the damage's owner goes on: an event fall's
 * script, Shoot's command tail, Rest's end, or the movement's tail. Original: 30E3C (4EA1C at least 2); 2E8BB 2ABB8(1),
 * 2E8C5 2F869; 2E68D's RETF to 1715A (the trap), 17367 or 1764B's caller; 98D7, F5DF; 17386. */
xeen_status damage_after_scene(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    g->damage_stage = 0;
    bar_redraw(g);
    xeen_combat_party_check(m);
    if (g->unlock_stage == UNLOCK_TRAP) {
        g->unlock_stage = UNLOCK_NONE;
        return unlock_roll(g, g->unlock_member);
    }
    m->ds[XEEN_DS_MEMBER_SELECTOR] = g->damage_member_selector;
    m->ds[XEEN_DS_STEP_PARITY] ^= 1;
    if (g->surface_owner == SURFACE_FALL) {
        return fall_event_return(g);
    }
    if (g->surface_owner == SURFACE_SHOOT) {
        return command_tail(g); /* Original: 17386 RETF to 17EFD, 17F07 */
    }
    if (g->surface_owner == SURFACE_REST_WAKE) {
        return rest_scene(g, 2); /* Original: 17386 RETF to Rest's 29124 */
    }
    return post_action_surface_tail(g);
}
/**
 * The movement's tail after the cell's surface (xeen_post_action_after_surface_native): after a surface's damage
 * scene, after Space's surface, and after a fall's landing (fall_return); the session stops at its next boundary
 * (the original's exit, 17F28, counts as one). Original: 17D56 after 17264.
 */
xeen_status post_action_surface_tail(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_caller_result next;
    xeen_native_audio *audio = game_audio(g);
    next = xeen_post_action_after_surface_native(m, audio);
    g->lifecycle_boundary = next.boundary;
    g->phase = XG_STOPPED;
    g->scene_argument = 1;
    g->command_status = next.boundary == XEEN_TICK_LOOP_EXIT              ? XGC_ORIGINAL_EXIT
                        : next.status == XEEN_SERVICE_REQUIRED ? XGC_EXTERNAL
                                                               : XGC_INPUT_ERROR;
    return next.status == XEEN_SERVICE_REQUIRED ? XEEN_SERVICE_REQUIRED : next.status;
}
/* An opening door's scene has run: the door flag cleared, the step taken through the open gate (counted when it
 * moved or turned), and the scene's return argument 1.
 * Original: 178F7: 9C6E = 0, the gate's AX = 1, then 17CFD (the 28178 charge and the movement tail). */
xeen_status door_after_scene(xeen_game *g)
{
    xeen_slice_navigation_result nav;
    g->door_stage = 0;
    g->movement.ds[XEEN_DS_DOOR_OPENING] = 0;
    g->movement.gate_open = 1;
    nav = execute_navigation(g, g->door_command);
    g->movement.gate_open = 0;
    if (nav == XSN_MOVED || nav == XSN_TURNED) {
        ++g->accepted_commands;
    }
    g->scene_argument = 1;
    return XEEN_SERVICE_REQUIRED;
}
/* The step's time charge has returned: its monster delivery's tail (never the navigation again), then for a step
 * that moved (or, in a continuous session, turned or crossed an edge) the strafe's tail and the surface under the
 * party, the desert's clock with its possible rest message; then navigation_finish.
 * Original: 28178 returned; 28195 suspended at 2A701's last delivery (2AAB1); 17264; 30CF3(170) to 30DB3, 18F2F. */
xeen_slice_navigation_result navigation_tail(xeen_game *g, xeen_gameplay_tick tick)
{
    tick = delivery_tail(g, tick);
    if (g->restored &&
        (tick.navigation == XSN_MOVED ||
         (g->continuous &&
          (tick.navigation == XSN_TURNED || tick.navigation == XSN_BOUNDARY))) &&
        tick.boundary == XEEN_TICK_POST_ACTION) {
        xeen_caller_result continuation;
        xeen_gameplay_tick_strafe_tail(&g->movement, g->navigation_command, &tick);
        continuation = xeen_post_action_after_movement_native(
                &g->movement, game_audio(g));
        if (continuation.boundary == XEEN_SURFACE_DESERT_CLOCK) {
            continuation = desert_surface(g, continuation);
            /* The desert's clock reached an unrested party's morning: its message scroll, then
             * desert_rest_closed. */
            if (continuation.boundary == XEEN_TICK_UNFED_DAWN && desert_scroll(g, DESERT_MOVEMENT, tick)) {
                g->navigation = tick.navigation;
                return tick.navigation;
            }
            if (continuation.status == XEEN_OK) {
                continuation = xeen_post_action_after_surface_native(
                        &g->movement, game_audio(g));
            }
        }
        tick.boundary = continuation.boundary;
        if (continuation.status != XEEN_SERVICE_REQUIRED) {
            tick.status = XT_INPUT_ERROR;
        }
    }
    return navigation_finish(g, tick);
}
/* The command's end after the surface: a refused step goes to the command tail (a pending fall, the defeat or the
 * restart, else the rejected-move join); a damaging surface or a defeat shows its scene (surface_scene); then the
 * command status and the session stopped at its boundary.
 * Original: the 17ACC join; the 17344/17380 scenes. */
xeen_slice_navigation_result navigation_finish(xeen_game *g, xeen_gameplay_tick tick)
{
    if (g->continuous && tick.navigation == XSN_BLOCKED && tick.boundary == XEEN_TICK_MOVE_REJECTED) {
        /* A step, back step or strafe that the gate refuses goes to the command tail: a pending fall falls; the
         * defeat or the restart leave the loop; else the rejected-move join. A fight that defeated the party
         * returns to the post-defeat poll with the defeat set: a refused step there ends the session like any
         * other command (joining 17ACC there would let a defeated party explore on).
         * Original: the 177AE gate JMPs 17F07 (17CE7, 17D1F, 17C5E, 17C9C): 8C85 -> 173A0(0); F60A or F64A ->
         * 17F28; else 17ACC; the post-defeat 17B09 poll. */
        if (g->movement.ds[XEEN_DS_FALL_SCENE]) {
            tick.boundary = XEEN_LOOP_FALL;
        } else if (g->movement.ds[XEEN_DS_PARTY_DEFEATED] ||
                   xeen_read16(&g->movement, XEEN_DS_LOOP_EXIT)) {
            tick.boundary = NAVIGATION_STOP_LOOP_EXIT;
        } else {
            xeen_caller_result rejected = xeen_caller_after_rejected_move(&g->movement);
            tick.boundary = rejected.boundary;
            if (rejected.status != XEEN_SERVICE_REQUIRED) {
                tick.status = XT_INPUT_ERROR;
            }
        }
    }
    g->navigation = tick.navigation;
    g->lifecycle_boundary = tick.boundary;
    if (g->continuous && (tick.boundary == XEEN_SURFACE_DAMAGE_SCENE || tick.boundary == XEEN_SURFACE_DEFEAT_SCENE) &&
        tick.status != XT_INPUT_ERROR) {
        (void)surface_scene(g, tick.boundary == XEEN_SURFACE_DAMAGE_SCENE ? 1 : 2, SURFACE_MOVEMENT);
        return g->navigation;
    }
    g->command_status = tick.status == XT_INPUT_ERROR ? XGC_INPUT_ERROR
                        : tick.status == XT_INTERNAL  ? XGC_INTERNAL
                                                      : XGC_EXTERNAL;
    if (tick.boundary == XEEN_TICK_LOOP_EXIT) {
        g->command_status = XGC_ORIGINAL_EXIT;
    }
    if (tick.status != XT_INPUT_ERROR || (g->restored && tick.navigation != XSN_INVALID)) {
        g->phase = XG_STOPPED;
    }
    return g->navigation;
}
xeen_slice_navigation_result xeen_game_navigate(xeen_game *g, uint16_t command)
{
    if (!g || g->phase != XG_NAVIGATION || g->continuous) {
        return XSN_INVALID;
    }
    return execute_navigation(g, command);
}
/* The passes a loaded game's world entry makes before its first scene: the map's animations, then the monsters and
 * the objects in view (indoor or outdoor). The monster pass sees the previous view's visibility flags, so a monster
 * it finds visible keeps its visible flag set. The entry's background and palette are the host's presentation.
 * Original: 179F1..17A13 (1791F after the 19E82 map load): 1B0F6; indoors 1ED46 and 20011, outdoors 2126C and 219E4;
 * then 17A1A's 1B503(0); the 1C9D4 flags (F4AB..), +4 = 1; 179D2's 188F6/188BB (9A1C 0: back.raw, mm4.pal). */
static int entry_passes(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_caller_result r;
    if (!g->monsters_prepared || !g->scene_resources_prepared) {
        return 0;
    }
    r = xeen_scene_animate(m, g->monster_database, g->monster_database_size);
    if (r.status != XEEN_SERVICE_REQUIRED || r.boundary != XEEN_SCENE_DRAW) {
        return 0;
    }
    if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
        return xeen_scene_outdoor_monsters(m, g->monster_database, g->monster_database_size) ==
                       XEEN_OK &&
               xeen_scene_outdoor_objects(m) == XEEN_OK;
    }
    return xeen_scene_monsters(m, g->monster_database, g->monster_database_size) == XEEN_OK &&
           xeen_scene_objects(m) == XEEN_OK;
}
int xeen_game_start_world_entry(xeen_game *g, const xeen_input_provider *provider)
{
    if (!g || !provider || !provider->keyboard || !provider->mouse || !g->restored ||
        !g->program_data || !g->map_sprites_prepared || !g->presentation || g->acted ||
        g->continuous || g->phase != XG_NAVIGATION) {
        return 0;
    }
    if (g->loaded_session && !entry_passes(g)) {
        return 0;
    }
    g->input_provider = *provider;
    g->continuous = 1;
    g->acted = 1;
    g->scene_argument = 0;
    g->phase = XG_STOPPED;
    g->lifecycle_boundary = XEEN_TIMER_SCENE_MARK;
    g->command_status = XGC_EXTERNAL;
    return 1;
}
xeen_status xeen_game_resume_world_entry(xeen_game *g)
{
    xeen_caller_result result;
    if (!g || !g->continuous || g->entry_complete || g->phase != XG_STOPPED ||
        g->lifecycle_boundary != XEEN_ENTRY_FINISH) {
        return XEEN_BAD_INPUT;
    }
    xeen_native_audio_speech_hold(&g->audio, g->movement.ds); /* Phase 286: speech = off (TEXT_OFF read below) */
    result = xeen_world_entry_finish(&g->movement, g->presentation, &g->world_entry_stage);
    if (result.boundary == XEEN_ENTRY_DONE) {
        /*17AF7 calls 2EA68 before the shared timer/scene(1) loop. */
        g->entry_complete = 1;
        g->scene_argument = 1;
        if (xeen_treasure_check(&g->movement) == XEEN_TREASURE_DIALOG) {
            g->lifecycle_boundary = XEEN_TREASURE_FOUND;
        } else {
            g->lifecycle_boundary = XEEN_TIMER_FRAME_MARK;
        }
    } else if (result.status != XEEN_OK) {
        g->lifecycle_boundary = result.boundary;
    }
    return result.status == XEEN_OK ? XEEN_SERVICE_REQUIRED : result.status;
}
