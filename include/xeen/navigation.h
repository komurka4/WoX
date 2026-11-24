#ifndef XEEN_NAVIGATION_H
#define XEEN_NAVIGATION_H
#include "xeen/movement.h"
/* A bounded exploration session, not the complete DOS movement controller.
 * Reuses its gate, exact byte deltas, and turn prefix. Time, party effects,
 * scene/dispatcher continuations are outside this contract. The caller owns
 * the one logical DS and supplies explicit services to the existing gate.
 * No ability result is supplied by this adapter. */
typedef enum {
    XSN_MOVED,
    XSN_TURNED,
    XSN_BLOCKED,
    XSN_BOUNDARY,
    XSN_SERVICE_REQUIRED,
    XSN_INVALID
} xeen_slice_navigation_result;
/* Commands are original F0/F1 (turn), F2/F3 (forward/backward), B0/B1
 * (strafe left/right, facing unchanged; the caller owns their
 * 17CCD tail after the 28178 charge, xeen_gameplay_tick_strafe_tail).
 * BOUNDARY is a host session limit: no coordinate commit or inferred map
 * wrap. A gate rejection is BLOCKED, separately from unavailable services.
 * Query scratch and explicit gate-service effects remain observable.
 * MOVED stops immediately after the original delta, before 28178. TURNED
 * stops immediately before 17264. Neither claims gameplay-loop completion. */
xeen_slice_navigation_result xeen_slice_navigate(xeen_movement *, uint16_t);
/* The turn and strafe commands' shared tail 17CCD..17CDD (the turns jump to it after their facing INC/DEC, the strafes
 * fall into it after 28178): 3068 = 1, ED96 &= 3, F5E6 ^= 8000h, in that order. */
void xeen_navigation_turn_tail(xeen_movement *);
const char *xeen_slice_navigation_name(xeen_slice_navigation_result);
/* Local Space interaction prefix 17B59..17BFE: 1C680 on the party's cell
 * with the facing's wall shift (17B63), then 17B85's table CS:0F14. In the
 * original every wall nibble reaches the dispatcher 17BFE except, indoors
 * (F608 = 0), walls 1, 6, 9 and 13, which first run the unlock 170D4. This
 * slice is narrower: indoors eligible=1 only for wall 0 (the other walls, the
 * doors' unlock included, are left to the caller: xeen_game_interact stops at
 * its interaction boundary; exploration_interact recomputes the wall itself);
 * outdoors, past the maps and on a missing neighbour eligible=1. The lookup is
 * xeen_maze_lookup (1C680): F48E, F490, F492 and F494 as the original writes
 * them, F492 = F608 for a missing east/west neighbour or a party map id 0
 * (Phase 330 original-byte kit). A map missing from the cache is
 * XEEN_CACHE_MISS; a wall-shift word above 15 (program data: 0..12) is
 * XEEN_BAD_INPUT after the lookup's writes. */
xeen_status xeen_slice_interaction_gate(xeen_movement *, uint16_t *eligible);
#endif
