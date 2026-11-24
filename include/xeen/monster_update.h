#ifndef XEEN_MONSTER_UPDATE_H
#define XEEN_MONSTER_UPDATE_H
/*
 * The monster update that follows a party action (original 2A701): the 96C0 release guard, the
 * occupancy rebuild, both spatial scan passes with their wall/terrain movement checks and the
 * ranged scheduling. It is game logic, not keyboard or mouse input.
 *
 * The exported names keep their historical xeen_input_release_96c0 prefix: historical evidence
 * runners load them by symbol from shared objects (xeen_dat_evidence/phase13, phase21, phase32).
 */
#include <stddef.h>
#include "xeen/movement.h"
#include "xeen/entities.h"
/* Where a monster update (2A701) stopped (its *boundary): the original address of the step it could not take. */
enum xeen_monster_update_boundary {
    XEEN_MONSTER_UPDATE_MOVES = 0x2a727,      /* the bounded release: the monsters' moves are not run by it */
    XEEN_MONSTER_UPDATE_BOX = 0x2a908,        /* a monster's movement box past the 49, or the party facing past 3 */
    XEEN_MONSTER_UPDATE_RECORD = 0x2a87b,     /* a moving monster's database row is missing */
    XEEN_MONSTER_UPDATE_WATER_RECORD = 0x2a635, /* a monster stepping onto water: its row (can it fly?) is missing */
    XEEN_MONSTER_UPDATE_SPACE_RECORD = 0x2a67b, /* ... onto space */
    XEEN_MONSTER_UPDATE_RECOVERY = 0x2b612,   /* a monster's condition-recovery draw needs the RNG */
    XEEN_MONSTER_UPDATE_RANGED = 0x2aab1      /* monsters queued ranged attacks: the game owner delivers them (4128F)
                                               * once, then the update's caller goes on */
};
/* Original 2A701 guarded release path; this is not keyboard polling. A zero
 * 96C4 or nonzero 97F9 completes the original routine's early-return path.
 * Otherwise prior 96C0 clearing persists and boundary=2A727 is unresolved. */
xeen_status xeen_input_release_96c0(xeen_movement *, uint32_t *boundary);
/* Extended 2A701 monster-update path: rebuild occupancy, clear transient
 * movement/attack lists, and execute both spatial scan passes. Completes when
 * nearby records are inactive (except combat state 5), or none are nearby,
 * and condition recovery / 97F6 encounter processing is unnecessary. Active
 * candidates, condition recovery and encounter calls retain exact boundaries.
 * Earlier writes persist on failure; a failed call must not be blindly retried.
 * This is game logic, not a native keyboard or mouse polling service. */
xeen_status xeen_input_release_96c0_extended(xeen_movement *, uint32_t *boundary);
/* Same 2A701 scan with caller-owned original database. Includes verified
 * indoor wall/outdoor terrain movement and 41CA9 ranged scheduling. A pending
 * action queue stops at 2AAB1; the game owner must run 4128F delivery exactly
 * once before completing the caller. Condition recovery remains explicit. */
xeen_status xeen_input_release_96c0_database(xeen_movement *, const uint8_t *, size_t,
                                             uint32_t *boundary);
/* The same with the session RNG, which 2B5E9's condition recovery
 * (38ADB per conditioned record, in record order) draws from; NULL keeps
 * the 2B612 stop. */
xeen_status xeen_input_release_96c0_database_rng(xeen_movement *, const uint8_t *, size_t,
                                                 xeen_entity_random_fn, void *, uint32_t *boundary);
#endif
