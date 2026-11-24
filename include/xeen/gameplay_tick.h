#ifndef XEEN_GAMEPLAY_TICK_H
#define XEEN_GAMEPLAY_TICK_H
#include <stddef.h>
#include "xeen/navigation.h"
#include "xeen/entities.h"
typedef enum {
    XT_EXTERNAL,
    XT_INPUT_ERROR,
    XT_INTERNAL
} xeen_tick_status;
/*
 * Where a tick ended (xeen_gameplay_tick.boundary). Each value is the original address the exploration loop goes
 * on at, so traces and the original-byte harness name the same place; the native tick has done the original's work
 * up to that point. "Stops" below mean the tick ends before a step that needs a service the caller did not give it
 * (the RNG, the sky reload, the monster update); the caller either supplies it and resumes or reports the stop.
 */
enum xeen_tick_boundary {
    XEEN_TICK_POST_ACTION = 0x17264,        /* the tick is done (the step or turn, its time charge, the monster
                                             * countdown); the post-action step, the party cell's terrain effects,
                                             * is next. Also a defeated party's wait, which charges no time */
    XEEN_TICK_LOOP_EXIT = 0x17f28,          /* the loop's exit: the command was refused because the loop is to end
                                             * or the party is defeated */
    XEEN_TICK_OFF_MAP = 0x17d4b,            /* the step left the map: the map edge's transition comes next. A result
                                             * of the entries without the native step (xeen_gameplay_tick_begin,
                                             * _begin_extended), which stop before committing the out-of-map
                                             * coordinate; the native entries (_begin_native, _database*, _services)
                                             * go on to the edge's transition instead (17334). Not a registered
                                             * stop (Phase 284 owner decision): an API result */
    XEEN_TICK_MOVE_SERVICE = 0x177ae,       /* the movement gate needs a service the host has not performed (a
                                             * door's opening scene, a skill or map query). A continuous session
                                             * runs a pending door's scene and goes on (game_navigation.c); a caller
                                             * that cannot perform the service ends there. Not a registered stop
                                             * (Phase 284 owner decision): an API result */
    XEEN_TICK_MOVE_REJECTED = 0x17acc,      /* the movement gate refused the step: the rejected move's cell recheck */
    XEEN_TICK_CONDITION_LOOP = 0x2fcab,     /* stops: a 480-minute boundary's poison and disease draws need the RNG */
    XEEN_TICK_CLOCK_TAIL = 0x30d27,         /* the minutes are added, the day's tail is next. Inside the entries that
                                             * run the time tail it is an intermediate value (their time_tail
                                             * consumes it); xeen_gameplay_tick_begin, which runs the step and the
                                             * clock only, returns it as its final boundary (a session on that entry
                                             * stops there: tests/test_lifecycle.c). Not a registered stop (Phase 284
                                             * owner decision): an API result */
    XEEN_TICK_DAY_CHANGE = 0x30d41,         /* stops: a new day's blacksmith wares need the RNG */
    XEEN_TICK_UNFED_DAWN = 0x30db3,         /* stops: dawn found the party unrested; the caller shows "The Party needs
                                             * rest!" and resumes (xeen_gameplay_tick_resume_rest) */
    XEEN_TICK_CONFUSION_ROLL = 0x2ff6a,     /* stops: a confused member's recovery draw needs the RNG */
    XEEN_TICK_PARALYSIS_ROLL = 0x2ffb8,     /* stops: a paralysed member's recovery draw needs the RNG */
    XEEN_TICK_SKY_RELOAD = 0x30004,         /* stops: night began or ended and the sky must be reloaded */
    XEEN_TICK_CHARGE_DONE = 0x28195,        /* internal: the time charge returned; the monster countdown is next */
    XEEN_TICK_MONSTER_UPDATE = 0x2a701      /* stops: the monster countdown ran out and the monsters must move */
};
typedef struct {
    xeen_slice_navigation_result navigation;
    xeen_tick_status status;
    uint32_t boundary;                      /* an enum xeen_tick_boundary, or the monster update's own stop */
    uint16_t amount;
    uint8_t dead; /* 2FC7E's [BP-2], 1 once its 2FCAB loop set a member dead
                   * (a stat below 1); 2 once 2FFE1's 2ABB8(1) ran (the services' bar) */
} xeen_gameplay_tick;
/* 17CCD, a strafe's (B0/B1) tail after its 28178 charge returned
 * (boundary 17264, the party moved): 3068 = 1, the facing masked to 0..3
 * and F5E6 flipped, as after a turn; then the 17D50 movement tail. No-op for
 * any other command or boundary. */
void xeen_gameplay_tick_strafe_tail(xeen_movement *, uint16_t command, const xeen_gameplay_tick *);
/* One verified prefix; deliberately never reports complete gameplay. Coordinates
 * and clock writes persist on suspension. Caller must not invoke again to resume.
 * Scene entry is external BEFORE BIOS4EA35, not a substituted scene prefix. */
xeen_gameplay_tick xeen_gameplay_tick_begin(xeen_movement *, uint16_t);
/* Opt-in continuation of restored sessions: executes only service-free original clock paths,
 * stops before every unresolved service and before post-action 17264. The same
 * suspension rule applies: a returned prefix cannot be restarted as a command. */
xeen_gameplay_tick xeen_gameplay_tick_begin_extended(xeen_movement *, uint16_t);
/* Extended native monster-update service for the connected caller lifecycle. */
xeen_gameplay_tick xeen_gameplay_tick_begin_native(xeen_movement *, uint16_t);
/* Connected variant with the current map's caller-owned monster database. */
xeen_gameplay_tick xeen_gameplay_tick_begin_database(xeen_movement *, uint16_t, const uint8_t *,
                                                     size_t);
/*28178 clock/conditions without navigation, used by default Space.*/
xeen_gameplay_tick xeen_gameplay_tick_wait_native(xeen_movement *);
xeen_gameplay_tick xeen_gameplay_tick_wait_database(xeen_movement *, const uint8_t *, size_t);
/* Original 2FC7E(amount) called directly (combat round reset 393D3 and
 * victory 393F0): 480-minute bucket guard, 30CF3 clock, 30D27 tail, 2FF59
 * timed conditions and the night test, without 28178's 2A701/96C0 step.
 * Completion reports status INTERNAL with boundary 0; every unresolved
 * branch keeps its exact boundary and earlier writes. */
xeen_gameplay_tick xeen_gameplay_time_conditions(xeen_movement *, uint16_t amount);
/* Variants with the session RNG: a 480-minute bucket then runs the 2FCAB
 * poison/disease draws (504DA, 37A02) instead of stopping at 2FCAB, and
 * every charge runs the 2FF59 confusion/paralysis draws instead of stopping
 * at 2FF6A/2FFB8. A NULL random keeps those stops. */
xeen_gameplay_tick xeen_gameplay_tick_begin_database_rng(xeen_movement *, uint16_t, const uint8_t *,
                                                         size_t, xeen_entity_random_fn, void *);
xeen_gameplay_tick xeen_gameplay_tick_wait_database_rng(xeen_movement *, const uint8_t *, size_t,
                                                        xeen_entity_random_fn, void *);
xeen_gameplay_tick xeen_gameplay_time_conditions_rng(xeen_movement *, uint16_t amount,
                                                     xeen_entity_random_fn, void *);
/* Session services of a time charge. random: the session RNG; with it a
 * 480-minute bucket runs the 2FCAB draws, every charge the 2FF59 draws, and
 * a day ending in 1 (or a charge over 1440) the 30D41 blacksmith wares 41D20
 * and bank interest 30DD2 instead of stopping. sky: the 2A0FB sky reload;
 * it writes 8C92 and reloads the F45C sky resource, returning nonzero only
 * on success. A NULL member keeps the corresponding explicit stop (2FCAB,
 * 2FF6A, 2FFB8, 30D41 or 30004). The morning step 30D73 resets the
 * temporaries; an unrested party stops at 30DB3 (the 18F2F scroll) after
 * the weak increments. */
typedef struct {
    xeen_entity_random_fn random;
    void *random_user;
    int (*sky)(void *user, xeen_movement *);
    void *sky_user;
    void (*bar)(void *user);
    void *bar_user; /* 2FFE1 2ABB8(1), presentation (may be NULL) */
} xeen_gameplay_services;
xeen_gameplay_tick xeen_gameplay_tick_begin_services(xeen_movement *, uint16_t, const uint8_t *,
                                                     size_t, const xeen_gameplay_services *);
xeen_gameplay_tick xeen_gameplay_tick_wait_services(xeen_movement *, const uint8_t *, size_t,
                                                    const xeen_gameplay_services *);
xeen_gameplay_tick xeen_gameplay_time_conditions_services(xeen_movement *, uint16_t amount,
                                                          const xeen_gameplay_services *);
/* Continues a begin/wait tick that stopped at 30DB3 once its owner has shown
 * and dismissed the 18F2F scroll: 30DCA 9842 = 0, 2FF59, the night test and
 * 28195. The suspended tick's earlier writes are never repeated. */
xeen_gameplay_tick xeen_gameplay_tick_resume_rest(xeen_movement *, xeen_gameplay_tick suspended,
                                                  const uint8_t *, size_t,
                                                  const xeen_gameplay_services *);
/* The same for a direct 2FC7E call (combat's 393D3/393F0) whose
 * morning step stopped at 30DB3: 30DCA 9842 = 0, the 2FF55 timed conditions
 * and the 2FFE7 night test (30004 without sky services); boundary 0 when
 * 2FC7E returned to its caller. */
xeen_gameplay_tick xeen_gameplay_time_conditions_resume_rest(xeen_movement *,
                                                             xeen_gameplay_tick suspended,
                                                             xeen_entity_random_fn, void *);
/* 29168: the temporaries reset shared by the morning step and Rest. */
void xeen_gameplay_reset_temps(xeen_movement *);
/* 30CF3(amount) called on its own (the tavern's Sign In, 25FF0):
 * the clock arithmetic and the 30D27..30DD1 tail (the day ending in 1 or a
 * charge over 1440: 41D20 and 30DD2; 9842; at 05:00 of a new day, outside
 * modes 9 and 11h, 29168 and the rested flag or the weak increments), not
 * 2FC7E's bucket loop nor its 2FF55 tail. Returns 0, or the boundary of an
 * explicit stop after the writes before it: 30D41 (no RNG for the wares)
 * or 30DB3 (the "needs rest" scroll). */
uint32_t xeen_gameplay_clock_add(xeen_movement *, uint16_t amount, const xeen_gameplay_services *);
#endif
