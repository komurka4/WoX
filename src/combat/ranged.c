#include "xeen/ranged.h"
#include "xeen/ds_globals.h"
#include "xeen/minimap.h"
/* A ranged monster's distance past the four cells the original's shot table covers (the caller scans at most three):
 * reported, not read past the table. */
enum {
    RANGED_DISTANCE_PAST_TABLE = 0x41cf7
};

xeen_status xeen_ranged_distance(xeen_movement *m, int16_t dx, int16_t dy, uint16_t *result,
                                 uint32_t *boundary)
{
    int step, first, last, px, py, k, outdoor;
    uint16_t mask, wall;
    unsigned facing;
    if (!m || !result || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    outdoor = xeen_read16(m, XEEN_DS_OUTDOORS) != 0;
    px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]);
    py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    /* The original chooses X whenever nonzero, irrespective of Y. Production
     * caller 2A701 admits cardinal alignments only. Preserve the helper rule.
     * Indoors each cell's side facing the party is tested for the shot bit of its wall kind (41B53..41C05: the cell
     * word's nibbles at shift 12 north, 8 east, 4 south, 0 west). */
    if (dx) {
        step = dx > 0 ? 1 : -1;
        first = dx > 0 ? 1 : dx;
        last = dx > 0 ? dx : -1;
        mask = dx > 0 ? XEEN_INDOOR_WALL_STOPS_SHOTS << 0 /* west */ : XEEN_INDOOR_WALL_STOPS_SHOTS << 8 /* east */;
        facing = dx > 0 ? XEEN_EAST : XEEN_WEST;
    } else {
        step = dy > 0 ? 1 : -1;
        first = dy > 0 ? 1 : dy;
        last = dy > 0 ? dy : -1;
        mask = dy > 0 ? XEEN_INDOOR_WALL_STOPS_SHOTS << 4 /* south */ : XEEN_INDOOR_WALL_STOPS_SHOTS << 12 /* north */;
        facing = dy > 0 ? XEEN_NORTH : XEEN_SOUTH;
    }
    /* Outdoors the cells from the party's own up to the one before the attacker are read, each cell word's wall
     * nibble (shift 4, mask 0Fh: its terrain feature); indoors the cells up to the attacker's, the side MASK selects. */
    for (k = first; k <= last; ++k) {
        int offset = outdoor ? k - step : k;
        xeen_status status = xeen_maze_lookup(m, (int16_t)(px + (dx ? offset : 0)),
                                              (int16_t)(py + (dx ? 0 : offset)),
                                              outdoor ? 15u : mask, outdoor ? 4u : 0u, &wall);
        if (status != XEEN_OK) {
            *boundary = XEEN_MAZE_QUERY;
            return status;
        }
        if (outdoor) {
            /* Original four CS jump tables admit the same wall kinds: 0x6935 is every kind 0..14 but the mountains
             * (xeen_outdoor_mountain_kind: 1, 7, 9, 10, 12) and the dense forests 3 and 6, the kinds the party's
             * gate crosses only with Mountaineer or Path Finder; anything above 14 stops the shot. */
            if (wall > 14u || !(0x6935u & (1u << wall))) {
                *result = 0;
                return XEEN_OK;
            }
        } else if (wall) {
            *result = 0;
            return XEEN_OK;
        }
    }
    *result = 1;
    if (m->ds[XEEN_DS_PARTY_DIRECTION] == facing) {
        *result = (uint16_t)((dx ? dx : dy) * step + 1);
    }
    return XEEN_OK;
}
xeen_status xeen_ranged_schedule(xeen_movement *m, uint8_t id, int16_t x, int16_t y,
                                 uint32_t *boundary)
{
    static const uint8_t shooting[4] = {1, 1, 2, 3};
    unsigned i, j;
    uint16_t distance;
    xeen_status status;
    if (!m || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    for (i = 0; i < XEEN_MONSTER_SHOOTERS && m->ds[XEEN_DS_MONSTER_SHOOTERS + i] != 255u; ++i) {
    }
    if (i == XEEN_MONSTER_SHOOTERS) {
        return XEEN_OK;
    }
    status = xeen_ranged_distance(m, (int16_t)(x - xeen_signed_byte(m->ds[XEEN_DS_PARTY_X])),
                                  (int16_t)(y - xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y])),
                                  &distance, boundary);
    if (status != XEEN_OK || !distance) {
        return status;
    }
    /* Caller scans at most three cells. Larger off-contract distances must
     * not read past the recovered original table. */
    if (distance > 4u) {
        *boundary = RANGED_DISTANCE_PAST_TABLE;
        return XEEN_SERVICE_REQUIRED;
    }
    m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED] = 1;
    m->ds[XEEN_DS_MONSTER_SHOOTERS + i] = id;
    if (distance != 1u) {
        for (j = 0; j < 8u; ++j) {
            if (!m->ds[XEEN_DS_MISSILE_SLOTS + j]) {
                m->ds[XEEN_DS_MISSILE_SLOTS + j] = shooting[distance - 1u];
                m->ds[0x97c8u + j] = id;
                break;
            }
        }
    }
    return XEEN_OK;
}
