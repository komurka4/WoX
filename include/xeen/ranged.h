#ifndef XEEN_RANGED_H
#define XEEN_RANGED_H
#include "xeen/movement.h"
/* Original 419F7: indoor cardinal LOS stops at a traversed cell whose side
 * facing the party has a wall kind with bit 3 (XEEN_INDOOR_WALL_STOPS_SHOTS:
 * kinds 8..15), not by movement passability. *RESULT: 0 blocked, 1 clear and
 * off the screen, 2..4 visible at distance 1..3. Outdoor LOS uses the original
 * terrain dispatch on the cells before the attacker. A failed cell query returns
 * its status with *BOUNDARY XEEN_MAZE_QUERY. No movement or damage occurs here. */
xeen_status xeen_ranged_distance(xeen_movement *, int16_t, int16_t, uint16_t *, uint32_t *);
/* Original 41CA9: enqueue a database ID in 97D0[36], optional projectile row
 * in 9744[8]/97C8[8], set 97F6. No RNG, movement, damage or clock advancement.
 * Caller 2A701 owns ranged eligibility and F9BA per-runtime moved marker. */
xeen_status xeen_ranged_schedule(xeen_movement *, uint8_t, int16_t, int16_t, uint32_t *);
#endif
