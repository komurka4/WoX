#include "xeen/navigation.h"
#include "xeen/ds_globals.h"
#include "xeen/input_poll.h"
#include "xeen/minimap.h"

/* The Space interaction's prefix (contract in navigation.h): 17B63's 1C680 on the party's cell, then the wall test. */
xeen_status xeen_slice_interaction_gate(xeen_movement *m, uint16_t *eligible)
{
    unsigned facing;
    uint16_t shift, wall;
    if (!m || !eligible) {
        return XEEN_BAD_INPUT;
    }
    *eligible = 0;
    m->status = XEEN_OK;
    facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    if (facing > 3) {
        return m->status = XEEN_BAD_INPUT;
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, XEEN_KEY_SPACE);
    /* 17B63: 1C680(signed ED97, signed ED98, 0Fh, the facing's wall shift), with all of its scratch writes; a map
     * missing from the 3x3 cache stays XEEN_CACHE_MISS. */
    shift = xeen_read16(m, XEEN_DS_WALL_SHIFT(facing, 2u));
    m->status = xeen_maze_lookup(m, (int16_t)xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]),
                                 (int16_t)xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]), 15u, shift, &wall);
    if (m->status != XEEN_OK) {
        return m->status;
    }
    /* A wall shift above 15 (the program data holds 0..12; 1C680 would shift by CL) stays a native rejection, after
     * the lookup's writes. Past the maps and, indoors, on a missing neighbour 1C680 answers 8888h; outdoors every
     * value (0Fh for a missing neighbour) reaches the dispatcher 17BFE. */
    if (shift > 15) {
        return m->status = XEEN_BAD_INPUT;
    }
    *eligible = (uint16_t)(wall == 0 || wall == XEEN_CELL_OUTSIDE_MAPS || xeen_read16(m, XEEN_DS_OUTDOORS));
    return m->status;
}

void xeen_navigation_turn_tail(xeen_movement *m)
{
    m->ds[XEEN_DS_TURNED] = 1;
    m->ds[XEEN_DS_PARTY_DIRECTION] &= 3u;
    xeen_write16(m, XEEN_DS_TURN_FLIP, (uint16_t)(xeen_read16(m, XEEN_DS_TURN_FLIP) ^ 0x8000u));
}
/* One movement command at the exploration poll (contract in navigation.h): its gate, then the original's byte step. */
xeen_slice_navigation_result xeen_slice_navigate(xeen_movement *m, uint16_t command)
{
    unsigned limit;
    int x, y, dx = 0, dy = 0;
    uint16_t allowed = 0;
    if (!m) {
        return XSN_INVALID;
    }
    m->status = XEEN_OK;
    /* Indoors the original steps (17CFD..17D47) inside the 3x3 maze cache
     * without a bound (map 41; a new game's map 28 at
     * 18,4); outdoors a position past 15 is a transition (17334). Cells
     * below 0 stay an explicit boundary. */
    limit = !xeen_read16(m, XEEN_DS_OUTDOORS) ? 31u : 15u;
    if (m->ds[XEEN_DS_PARTY_DIRECTION] > 3 || m->ds[XEEN_DS_PARTY_X] > limit ||
        m->ds[XEEN_DS_PARTY_Y] > limit ||
        ((command < XEEN_MOVE_TURN_LEFT || command > XEEN_MOVE_BACK) && command != XEEN_MOVE_STRAFE_LEFT &&
         command != XEEN_MOVE_STRAFE_RIGHT)) {
        m->status = XEEN_BAD_INPUT;
        return XSN_INVALID;
    }
    xeen_write16(m, XEEN_DS_INPUT_CODE, command);
    if (command == XEEN_MOVE_TURN_LEFT || command == XEEN_MOVE_TURN_RIGHT) {
        /* 17C50 DEC / 17C8E INC of the facing byte, then the tail 17CCD. */
        m->ds[XEEN_DS_PARTY_DIRECTION] =
                (uint8_t)(m->ds[XEEN_DS_PARTY_DIRECTION] + (command == XEEN_MOVE_TURN_LEFT ? 0xffu : 1u));
        xeen_navigation_turn_tail(m);
        return XSN_TURNED;
    }
    if (m->gate_open && command == XEEN_MOVE_FORWARD) {
        m->gate_open = 0; /* 178FC after the door scene: the step was allowed */
        allowed = 1;
    } else {
        xeen_translation_gate(m, command, &allowed);
        if (m->status != XEEN_OK) {
            return m->status == XEEN_SERVICE_REQUIRED ? XSN_SERVICE_REQUIRED : XSN_INVALID;
        }
    }
    if (!allowed) {
        return XSN_BLOCKED;
    }
    /* A step back moves to the opposite of the facing; the strafes 17C56 (B0) and 17C94 (B1) step through the 0EA4
     * and 0E9C tables of the unrotated facing: to its left and right. The facing was checked (0..3) above. */
    (void)xeen_direction_step(xeen_step_direction(m->ds[XEEN_DS_PARTY_DIRECTION], command), &dx, &dy);
    x = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]) + dx;
    y = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]) + dy;
    if (x < 0 || x > (int)limit || y < 0 || y > (int)limit) {
        return XSN_BOUNDARY;
    }
    /* 17CFD..17D13, 17D35..17D47 (17C74..17C86, 17CB2..17CC4 for the
     * strafes): byte INC/DEC, before 28178. */
    m->ds[XEEN_DS_PARTY_X] = (uint8_t)x;
    m->ds[XEEN_DS_PARTY_Y] = (uint8_t)y;
    return XSN_MOVED;
}
const char *xeen_slice_navigation_name(xeen_slice_navigation_result r)
{
    switch (r) {
        case XSN_MOVED:
            return "moved";
        case XSN_TURNED:
            return "turned";
        case XSN_BLOCKED:
            return "blocked by original movement gate";
        case XSN_BOUNDARY:
            return "map transition outside bounded exploration session";
        case XSN_SERVICE_REQUIRED:
            return "original movement service unavailable";
        default:
            return "invalid navigation state";
    }
}
