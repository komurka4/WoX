/*
 * The exploration commands (Phase 246, from game.c): 17C10's dispatch of F5FE through the CS:0EAC table
 * (table_command), Space's wait 17DA8, the unlock of a locked door or grate 170D4 (its roll, trap and
 * message) and Bash (its forward step past 177AE's gate).
 */
#include "game_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Where native play stops when an exploration command cannot go on; the value is the original address of the step
 * (tools/xeen_replay/stops.json). */
enum {
    UNLOCK_STOP_SOUND = 0x1880e,             /* the unlock's effect */
    UNLOCK_STOP_FACING = 0x171fd,            /* the party faces no compass direction (0..3) */
    BASH_STOP_WALL_QUERY = 0x17e5d,          /* the wall ahead could not be read */
    BASH_STOP_WALL = 0x3d689,                /* the bash against the wall could not be run */
    /* variable stop sites (stops_variable.json) */
    UNLOCK_STOP_TRAP_DAMAGE = 0x17155        /* a failed unlock's trap damage stopped without reporting where */
};
static xeen_status bash_finish(xeen_game *, int);
static xeen_status bash_forward(xeen_game *);
/* The exploration command table: a command from the poll, or the key that closed the last dialog of an event that
 * Space started (an NPC page's F1, '=', F8, S...), dispatched by its key: the moves, Bash, Shoot, Cast, Rest and the
 * others. (An earlier native brought only the moves back here from an event; the others stopped at their table
 * boundary.) Original: 17C10's table CS:0EAC on F5FE, after 17B34/17B59 or the event's return to 17C03 (310B4). */
xeen_status table_command(xeen_game *g, uint16_t command)
{
    if ((command >= XEEN_MOVE_TURN_LEFT && command <= XEEN_MOVE_BACK) || command == XEEN_MOVE_STRAFE_LEFT ||
        command == XEEN_MOVE_STRAFE_RIGHT) {
        xeen_slice_navigation_result nav = execute_navigation(g, command);
        if (nav == XSN_MOVED || nav == XSN_TURNED) {
            ++g->accepted_commands;
        }
        g->scene_argument = 1;
        return XEEN_SERVICE_REQUIRED;
    }
    if (command == 'B') { /* Bash */
        xeen_gameplay_tick tick;
        xeen_gameplay_services services = session_services(g);
        xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, command);
        tick = xeen_gameplay_tick_wait_services(&g->movement, g->monster_database,
                                                g->monster_database_size, &services);
        if (tick.boundary == XEEN_TICK_UNFED_DAWN && rest_scroll(g, SCROLL_BASH_REST, tick)) {
            return XEEN_SERVICE_REQUIRED;
        }
        return bash_after_tick(g, tick);
    }
    if (command == 'S') { /* Shoot */
        return shoot(g);
    }
    if (command == 'C') { /* Cast */
        g->cast_combat_call = 0;
        return cast_command(g);
    }
    if (command == 'R') { /* Rest */
        return rest(g);
    }
    if (command == '\t') { /* the control panel */
        xeen_write16(&g->movement, XEEN_DS_LOOP_EXIT, 0);
        return panel_begin(g);
    }
    if (command >= XEEN_KEY_F1 && command <= XEEN_KEY_F6) {
        return view_command(g, command);
    }
    if (command == 'I') { /* Info */
        g->combat_ui_site = 0;
        return info_begin(g);
    }
    if (command == 'Q') { /* Quick Reference */
        g->combat_ui_site = 0;
        return qref_begin(g);
    }
    if (command == 'M') { /* the map */
        g->combat_ui_site = 0;
        return map_begin(g);
    }
    if (command == 'V') { /* the Quests (View) dialog */
        return quests_begin(g, QUESTS_FROM_EXPLORATION);
    }
    if (command == 'D') { /* Dismiss */
        g->combat_ui_site = 0;
        return dismiss_begin(g);
    }
    /* 17E20, '=' or a click on the minimap (the disabled button
     * 16 of the 197C table, key 3Dh): ED9F ^= 1, 17F07. The next scene's
     * 1E467 draws the minimap or its cover. */
    if (command == '=') {
        g->movement.ds[XEEN_DS_MINIMAP_SHOWN] ^= 1u;
        return command_tail(g);
    }
    /* 17E10, F8 (scan 42h -> D0h through 4FEFF's CS:7AF8): 483C7
     * again, the current PlayCD tuple 84E0..84E5 (speech off or none: no
     * write), 17F07. */
    if (command == XEEN_KEY_F8) {
        cd_voice_start(g);
        return command_tail(g);
    }
    /* Reuse the recovered 17C10 table. Unknown keys follow 17F07/17ACC;
     * known unsupported handlers retain their actual entry boundary. */
    {
        xeen_caller_result next = xeen_caller_after_dispatch(&g->movement, 0);
        g->lifecycle_boundary = next.boundary;
        g->command_status = exit_or_external(next.boundary);
        g->scene_argument = 1;
        return next.status;
    }
}
/* Space with no event at the party's cell: the time passes as a wait (outdoors 10 minutes, indoors 1, with the
 * monsters' update when its countdown is set), the monsters update once more unconditionally (each update delivers
 * its own queued shots), the sign is cleared and the flips toggled; then the movement's tail (the cell's surface, the
 * step count, no step sound while the key is still Space, the flips again) and the command's end. A party that falls
 * asleep at dawn unfed gets the "needs rest" scroll first. Without a database or a random source the shared
 * services stop explicitly.
 * Original: 17DA8 (17B59's 310B4 found no event): 28178 (its 28195 2A701 when 96C0 is set, then 96C0 = 3), 2A701
 * (2AAB1), 3066 = 0, F5E4 / F5DF; 17D50: 17264, EFEE, F5FE 20h, 17F07; the stops 2A87B, 2FF6A / 2FFB8, 2FCAB. */
xeen_status space_wait(xeen_game *g)
{
    xeen_gameplay_services services = session_services(g);
    xeen_gameplay_tick tick;
    /* Without a database or RNG the shared services keep their explicit
     * stops (2A87B, 2FF6A/2FFB8, 2FCAB). */
    tick = xeen_gameplay_tick_wait_services(&g->movement, g->monster_database,
                                            g->monster_database_size, &services);
    if (tick.boundary == XEEN_TICK_UNFED_DAWN && rest_scroll(g, SCROLL_SPACE_REST, tick)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return space_after_tick(g, tick);
}
/* Space's wait after its tick: the monsters' ranged attacks the tick queued are delivered (ranged_tick; any other end
 * of the tick stops there), the monsters' update runs (run_monster_update), the sign goes, the step's flip and parity
 * turn, the command counts as accepted with the navigation result "blocked" (no step); then the surface's effect (a
 * desert's "needs rest" first) and space_after_surface. Original: 28178 returned to 17DAD: the 2AAB1 tail, 2A701,
 * 17DB2..17D50. */
xeen_status space_after_tick(xeen_game *g, xeen_gameplay_tick tick)
{
    xeen_movement *m = &g->movement;
    xeen_status status;
    xeen_caller_result next;
    tick = ranged_tick(g, tick);
    if (tick.boundary != XEEN_TICK_POST_ACTION || tick.status == XT_INPUT_ERROR) {
        return game_stop(g, tick.boundary);
    }
    g->service_boundary = 0;
    status = run_monster_update(g);
    if (status != XEEN_OK) {
        return game_stop(g, g->service_boundary ? g->service_boundary : RELEASE_STOP_MONSTER_UPDATE);
    }
    m->status = XEEN_OK;
    m->ds[XEEN_DS_SIGN_SHOWN] = 0;
    xeen_write16(m, XEEN_DS_STEP_FLIP, (uint16_t)(xeen_read16(m, XEEN_DS_STEP_FLIP) ^ 0x8000u));
    m->ds[XEEN_DS_STEP_PARITY] ^= 1;
    ++g->accepted_commands;
    g->navigation = XSN_BLOCKED;
    next = xeen_post_action_surface_native(m);
    next = desert_surface(g, next);
    if (next.boundary == XEEN_TICK_UNFED_DAWN && desert_scroll(g, DESERT_SPACE, tick)) {
        return XEEN_SERVICE_REQUIRED;
    }
    return space_after_surface(g, next);
}
/* The surface's effect returned to the Space wait's movement tail: its damage or defeat scene runs, any other
 * failure stops, else the post-action tail. Original: 17264 returned to 17D56; the scenes 17344 and 17380 (F60A). */
xeen_status space_after_surface(xeen_game *g, xeen_caller_result next)
{
    if (next.status != XEEN_OK) {
        if (next.boundary == XEEN_SURFACE_DAMAGE_SCENE || next.boundary == XEEN_SURFACE_DEFEAT_SCENE) {
            return surface_scene(g, next.boundary == XEEN_SURFACE_DAMAGE_SCENE ? 1 : 2, SURFACE_MOVEMENT);
        }
        return game_stop(g, next.boundary);
    }
    return post_action_surface_tail(g);
}
/*
 * The unlock: Space at an indoor door or grate (the wall kinds in xeen/map_record.h). Locking a door needs the cell's
 * flag 80h, and closing a Darkside grate in a tower map does nothing: both keep the key, so the cell's event runs.
 * Closing a grate, or a flagged cell, opens at once. Otherwise "Who will try?" (with one member, without a window);
 * Esc ends. For the member: a roll of 1 on 1..4 springs the trap (party damage of a random kind 0..6); then the
 * thievery plus 1..20 below the lock's difficulty fails, with no message; else the member gains their level x the
 * difficulty x 10 experience (16-bit products carried into the 32-bit total) and the lock opens: the cell's flag and
 * wall, the same for the cell beyond on its opposite side, the sound; then a scene pass, the key cleared and the
 * command tail. The random calls receive AX as the original's: the member, 1, the thievery.
 * Original: 170D4(new wall, title); 170DA (13, F48A; 9 with C106 = 2), the event 17BFE; 171B9; 2F9BA(13, title, 0);
 * Esc at 17249; 504DA(1, 4), 2E68D(C105, 504DA(0, 6), member); 3CF5B + 504DA(1, 20) below C0FE; 2C909 x C0FE x 10;
 * 2DE91(x, y, 80h), 2DE1D(x, y, facing, wall), 1880E(0Ah); 17249: 1B503(1), F5FE = 0, 17C10 -> 17F07.
 */
static uint16_t unlock_random(xeen_game *g, uint16_t ax, uint16_t low, uint16_t high)
{
    uint16_t v = 0;
    (void)xeen_entity_rng_range(g->event_random(g->event_random_user, ax), low, high, &v);
    return v;
}
/* Marks the cell at X, Y (flag 80h) and gives its SIDE the new WALL. Original: 2DE91(x, y, flag) and 2DE1D(x, y,
 * side, wall), each through 1C680(x, y, 0, 0) for its F494 (F490, F492), then the cell byte or the wall nibble. */
static void unlock_write(xeen_game *g, int16_t x, int16_t y, unsigned side, uint16_t wall)
{
    (void)xeen_maze_set_cell_flag(&g->movement, x, y, 0x80u);
    (void)xeen_maze_set_wall(&g->movement, x, y, side, wall);
}
/*
 * Opens the lock the party faces: its cell is marked (flag 80h) and given the new wall on the facing side, the cell
 * beyond the same on its opposite side; the unlock sound plays; then the attempt's scene pass. Stops for a facing past
 * west and when the sound is refused. Original: 2DE91 / 2DE1D on both cells (unlock_write), 1880E(0Ah), 17249's
 * 1B503(1).
 */
static xeen_status unlock_open(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    unsigned facing = m->ds[XEEN_DS_PARTY_DIRECTION], side;
    int16_t x = (int16_t)xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]),
            y = (int16_t)xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    int dx = 0, dy = 0;
    if (facing > 3u) {
        return game_stop(g, UNLOCK_STOP_FACING);
    }
    (void)xeen_direction_step(facing, &dx, &dy);
    unlock_write(g, x, y, facing, g->unlock_wall);
    side = facing ^ 2u; /* the cell beyond sees the lock from the opposite side */
    unlock_write(g, (int16_t)(x + dx), (int16_t)(y + dy), side, g->unlock_wall);
    if (sound_effect(g, 10u) != XEEN_OK) {
        return game_stop(g, UNLOCK_STOP_SOUND);
    }
    g->unlock_stage = UNLOCK_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
}
/* The end of an attempt that did not open the lock: the same scene pass, nothing changed. Original: 17249's 1B503(1)
 * (entered at 4EA35). */
static xeen_status unlock_tail(xeen_game *g)
{
    g->unlock_stage = UNLOCK_SCENE;
    return scene_wait(g, 1, XEEN_TIMER_SCENE_MARK);
}
/* The lock-picking roll of MEMBER: its Thievery plus a 1..20 roll (unlock_random) against the map's door lock, compared
 * signed. Success earns the member 10 x level x lock experience (32-bit) and opens the door (unlock_open); failure is
 * unlock_tail's. Original: 1715D: 3CF5B + 504DA(1, 20) against C0FE; 2C909. */
xeen_status unlock_roll(xeen_game *g, uint16_t member)
{
    xeen_movement *m = &g->movement;
    uint16_t record = XEEN_PARTY_RECORD(member), roll;
    int16_t thievery = xeen_combat_thievery(m, m->ds + record);
    roll = unlock_random(g, (uint16_t)thievery, 1, 20);
    if ((int16_t)((uint16_t)thievery + roll) >= (int16_t)m->ds[XEEN_DS_MAP_FIELD(DOOR_LOCK)]) {
        uint16_t gain = (uint16_t)((uint16_t)(xeen_character_current_level(m->ds + record) *
                                              m->ds[XEEN_DS_MAP_FIELD(DOOR_LOCK)]) *
                                   10u);
        uint32_t xp = (uint32_t)xeen_read16(m, (uint16_t)(record + XEEN_CHARACTER_EXPERIENCE)) |
                      ((uint32_t)xeen_read16(m, (uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + 2u))
                       << 16);
        xp += gain;
        xeen_write16(m, (uint16_t)(record + XEEN_CHARACTER_EXPERIENCE), (uint16_t)xp);
        xeen_write16(m, (uint16_t)(record + XEEN_CHARACTER_EXPERIENCE + 2u), (uint16_t)(xp >> 16));
        return unlock_open(g);
    }
    return unlock_tail(g);
}
/*
 * Continues the unlock with the member chosen in AX (the Who-will answer: member + 1; 0 cancels, ending the attempt
 * through unlock_tail). The trap comes first, independent of the lock: on a 1 of 1..4 it springs (the map's trap
 * damage of a random type 0..6 on the member, then the damage scene, whose end goes on to the pick); otherwise the
 * pick (unlock_roll) follows at once. Stops when the trap's damage cannot be applied.
 */
static xeen_status unlock_member(xeen_game *g, uint16_t ax)
{
    xeen_movement *m = &g->movement;
    uint16_t member = (uint16_t)(ax - 1u); /* 0 (cancelled) wraps to FFFFh */
    if (member == 0xffffu) {
        return unlock_tail(g);
    }
    if (unlock_random(g, member, 1, 4) == 1u) {
        /* 17155 2E68D(C105, 504DA(0, 6), member), the surface
         * callers' 2E68D: the member's damage and pow frames, then 2E8B3
         * 30E2D(2): 4EA0D, one full exploration 1B503(1) (a contact in it
         * stops at 1C17E), continued by damage_after_scene (2ABB8(1),
         * 2F869) and the RETF to 1715A (unlock_roll). 98D7 is untouched. */
        uint32_t boundary = 0;
        uint16_t type = unlock_random(g, 1, 0, 6);
        if (xeen_combat_party_damage_hits(&g->combat, m, g->monster_database,
                                          g->monster_database_size, m->ds[XEEN_DS_MAP_FIELD(TRAP_DAMAGE)], type, member,
                                          g->event_random, g->event_random_user,
                                          game_audio(g),
                                          g->presentation, &boundary) != XEEN_OK) {
            return game_stop(g, boundary ? boundary : UNLOCK_STOP_TRAP_DAMAGE);
        }
        g->unlock_member = member;
        g->unlock_stage = UNLOCK_TRAP;
        return damage_scene(g, m->ds[XEEN_DS_MEMBER_SELECTOR]);
    }
    return unlock_roll(g, member);
}
/*
 * Space at a locked door or grate (WALL, its title TEXT): a door locked without the cell's unlocked flag, or a
 * Darkside tower's closed grate, runs the cell's event instead; another grate, or a cell already unlocked, opens at
 * once; a one-member party tries without asking; otherwise "Who will try?" asks (the unlock's answer continues).
 * Original: 170D4(WALL, TEXT)'s entry (described above unlock_random): 170DA's two event cases (F48A; C106 = 2),
 * 171B9, 2F9BA(13, TEXT).
 */
xeen_status unlock_begin(xeen_game *g, uint16_t wall, uint16_t text)
{
    xeen_movement *m = &g->movement;
    if ((wall == XEEN_INDOOR_WALL_LOCKED_DOOR && !xeen_read16(m, XEEN_DS_CELL_UNLOCKED)) ||
        (m->ds[XEEN_DS_WORLD] && m->ds[XEEN_DS_MAP_FIELD(STYLE)] == XEEN_MAP_STYLE_TOWER &&
         wall == XEEN_INDOOR_WALL_LOCKED_GRATE)) {
        g->event_origin = EVENT_ORIGIN_SPACE;
        return begin_event(g) ? XEEN_SERVICE_REQUIRED : XEEN_BAD_INPUT;
    }
    g->unlock_wall = wall;
    g->unlock_text = text;
    if (wall == XEEN_INDOOR_WALL_LOCKED_GRATE || xeen_read16(m, XEEN_DS_CELL_UNLOCKED)) {
        return unlock_open(g);
    }
    if (m->ds[XEEN_DS_PARTY_COUNT] == 1u) {
        return unlock_member(g, 1);
    }
    {
        xeen_event_request request = {XEEN_REQUEST_WHO_WILL, 13, text, 0, 0};
        uint16_t ax = 0;
        int handled;
        handled = event_service(g, &request, m->ds, sizeof m->ds, &ax);
        if (handled == 1) {
            return unlock_member(g, ax);
        }
        if (handled != 0 || !g->ui.pending) {
            g->lifecycle_boundary = WHO_WILL_BOUNDARY;
            g->command_status = XGC_INTERNAL;
            g->phase = XG_STOPPED;
            return XEEN_SERVICE_REQUIRED;
        }
    }
    g->unlock_stage = UNLOCK_WHO;
    return input_wait(g, WHO_WILL_POLL_BOUNDARY);
}
/* A key at the unlock's "Who will try?": F1..F6 or Escape answer it and the chosen member tries (unlock_member;
 * none for Escape); a pending prompt waits; any other key keeps the prompt's poll. Original: 2F9BA's key loop
 * (2FB8D). */
xeen_status unlock_key(xeen_game *g, uint16_t command)
{
    xeen_event_request request = {XEEN_REQUEST_WHO_WILL, 13, g->unlock_text, 0, 0};
    uint16_t ax = 0;
    int handled;
    g->response = command;
    g->response_ready = 1;
    handled = event_service(g, &request, g->movement.ds, sizeof g->movement.ds, &ax);
    if (handled == 0 && g->ui.pending) {
        return XEEN_SERVICE_REQUIRED;
    }
    if (handled != 1) {
        g->unlock_stage = UNLOCK_NONE;
        g->lifecycle_boundary = WHO_WILL_POLL_BOUNDARY;
        g->command_status = XGC_INTERNAL;
        g->phase = XG_STOPPED;
        return XEEN_SERVICE_REQUIRED;
    }
    g->unlock_stage = UNLOCK_NONE;
    return unlock_member(g, ax);
}
/**
 * Finishes a lock-picking attempt after its scene: the unlock stage and the input code cleared, then the exploration
 * command tail. Original: the thievery roll 3CF5B against C0FE; 17251; 17BA6 -> 17C10 -> 17F07.
 */
xeen_status unlock_after_scene(xeen_game *g)
{
    g->unlock_stage = UNLOCK_NONE;
    xeen_write16(&g->movement, XEEN_DS_INPUT_CODE, 0);
    return command_tail(g);
}
/**
 * Bash's continuation after its tick: a monster's shot the tick delivered runs its tail first; then the wall ahead is
 * read. Outdoors the bash ends at once (bash_finish); indoors a wall below the map's bash threshold lets the forward
 * step move on (bash_forward); otherwise the wall is bashed (xeen_bash_wall), the party checked and the two-tick
 * scene begins (bash_after_scene continues). Stops on a tick or query the native does not serve, or a failed bash.
 * Original: 17E56..17E8F (28178/28195, 1C1C1(2) against C0FC, 3D689, 2F869, 30E2D(2)).
 */
xeen_status bash_after_tick(xeen_game *g, xeen_gameplay_tick tick)
{
    uint16_t wall = 0;
    /* Bash's tick may deliver a monster's shot: its tail runs first, as after Shoot's and a step's ticks (an earlier
     * native stopped there). Original: 17E56's 28178 -> 28195's 2A701 (97F6), its 2AAB1 tail, then 17E5B. */
    tick = delivery_tail(g, tick);
    if (tick.boundary != XEEN_TICK_POST_ACTION || tick.status == XT_INPUT_ERROR) {
        return game_stop(g, tick.boundary);
    }
    /* The wall ahead, against the map's bash threshold. Original: 17E5B 1C1C1(2), C0FC. */
    if (xeen_navigation_query(&g->movement, 2u, &wall) != XEEN_OK) {
        return game_stop(g, BASH_STOP_WALL_QUERY);
    }
    /* Outdoors the bash returns at once: no sound, no damage, no party check, no scene, no party bar; then the
     * command tail (an earlier native stopped here). Original: F608; 17E87's 3D689 (3D68F..3D696 JMP 3D8A4); 17E8F
     * JMP 17F07. */
    if (xeen_read16(&g->movement, XEEN_DS_OUTDOORS)) {
        return bash_finish(g, 0);
    }
    /* Indoors with the wall below C0FC, 17E74 JMP 17CEA: the
     * forward step's own move past its 177AE gate (bash_forward). */
    if ((int16_t)wall < (int16_t)g->movement.ds[XEEN_DS_MAP_FIELD(THRESHOLD)]) {
        return bash_forward(g);
    }
    if (xeen_bash_wall(&g->movement, g->event_random, g->event_random_user,
                       game_audio(g), &g->bash) != XEEN_OK) {
        return game_stop(g, BASH_STOP_WALL);
    }
    /* The bash may have hurt the last able member: the party check marks a defeat, and the command tail exits the
     * loop after the scene. Original: 3D88F 2F869 (2A152; F60A; 17F07 -> 17F28). */
    xeen_combat_party_check(&g->movement);
    /* The bash's two-tick scene: a frame mark, then scene passes until two ticks have passed (one pass);
     * bash_after_scene continues. Original: 3D894 30E2D(2): 4EA0D, 1B503(1); on at 3D89C. */
    g->bash_stage = 1;
    return scene_wait(g, 1, XEEN_TIMER_FRAME_MARK);
}
/* After the bash's scene: the party bar redrawn and the command tail. Original: 3D89C 2ABB8(1), the RETF to 17E8F,
 * its 17F07 tail. */
xeen_status bash_after_scene(xeen_game *g)
{
    return bash_finish(g, 1);
}
/* A bash through a wall that gave way: the forward step, past its gate (one cell ahead: north y + 1, east x + 1,
 * south y - 1, west x - 1; a facing above 3 none), then a second tick (Bash's own came first) and the movement's
 * tail (the surface, the step's sound with the command still 'B', the sign) to the command tail, as a step's.
 * Original: 17CEA..17D15, Bash's JMP past 177AE (CS:0E94 by ED96); 17D4B's 28178; the 17D50 tail (17264, EFEE,
 * 1880E(7 + 9775) as F5FE is 42h, 3066, F5E4/F5DF) to 17F07. */
static xeen_status bash_forward(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_gameplay_services services = session_services(g);
    xeen_gameplay_tick tick;
    xeen_forward_delta(m); /* the step one cell ahead (above 3 none) */
    g->acted = 1;
    g->navigation_command = 'B';
    tick = xeen_gameplay_tick_wait_services(m, g->monster_database, g->monster_database_size,
                                            &services);
    tick.navigation = XSN_MOVED;
    ++g->accepted_commands;
    if (tick.boundary == XEEN_TICK_UNFED_DAWN && rest_scroll(g, SCROLL_MOVE_REST, tick)) {
        return XEEN_SERVICE_REQUIRED;
    }
    (void)navigation_tail(g, tick);
    g->scene_argument = 1;
    return XEEN_SERVICE_REQUIRED;
}
static xeen_status bash_finish(xeen_game *g, int bar)
{
    g->bash_stage = 0;
    if (bar) {
        bar_redraw(g); /* 3D89C 2ABB8(1) */
    }
    g->navigation = XSN_BLOCKED;
    return command_tail(g);
}
