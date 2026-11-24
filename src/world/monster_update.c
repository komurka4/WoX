#include "xeen/monster_update.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/map_objects.h"
#include "xeen/ranged.h"
#include "xeen/minimap.h"
#include <string.h>
xeen_status xeen_input_release_96c0(xeen_movement *m, uint32_t *boundary)
{
    if (!m || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    if (!m->ds[XEEN_DS_MONSTERS_ENABLED]) {
        return XEEN_OK;
    }
    m->ds[XEEN_DS_MONSTER_COUNTDOWN] = 0;
    if (m->ds[XEEN_DS_MISSILES_FLYING]) {
        m->ds[0x97f8] = 1;
        return XEEN_OK;
    }
    *boundary = XEEN_MONSTER_UPDATE_MOVES;
    return XEEN_SERVICE_REQUIRED;
}

/*
 * Whether map monster I may step onto outdoor terrain of wall kind WALL (0..14): never onto the mountains; onto water
 * and deep water only a flier or monster id 59 (in Clouds the Water Dragon), onto space only a flier, whose rows are
 * read from DATABASE (a missing row stops at that surface's boundary: XEEN_SERVICE_REQUIRED without a database,
 * XEEN_BAD_INPUT for a row past its end); onto any other surface any monster but the Clouds Water Dragon. The surface
 * is the classification of the cell surface word the lookup leaves, taken from the current map's record whatever
 * the cell's map (the original indexes it without the slot's offset). Original: 2A614's jump table, 2A61B..2A6CB
 * (F490 into C0EA unscaled).
 */
static xeen_status outdoor_passable(xeen_movement *m, unsigned i, uint16_t wall, const uint8_t *database,
                                    size_t database_size, int *passable, uint32_t *boundary)
{
    uint8_t surface, id;
    size_t row;
    if (xeen_outdoor_mountain_kind(wall)) {
        *passable = 0;
        return XEEN_OK;
    }
    surface = m->ds[(uint16_t)(XEEN_DS_MAP_FIELD(CLASSIFICATION) + xeen_read16(m, XEEN_DS_CELL_SURFACE))];
    id = m->ds[(uint16_t)(XEEN_MAP_MONSTER(i) + XEEN_MAP_MONSTER_ID)];
    row = (size_t)id * XEEN_MONSTER_RECORD_BYTES;
    if (surface == XEEN_SURFACE_WATER || surface == XEEN_SURFACE_DEEP_WATER || surface == XEEN_SURFACE_SPACE) {
        if (!database || row + XEEN_MONSTER_RECORD_BYTES > database_size) {
            *boundary = surface == XEEN_SURFACE_SPACE ? XEEN_MONSTER_UPDATE_SPACE_RECORD
                                                      : XEEN_MONSTER_UPDATE_WATER_RECORD;
            return database ? XEEN_BAD_INPUT : XEEN_SERVICE_REQUIRED;
        }
        *passable = database[row + XEEN_MONSTER_FLYING] != 0u ||
                    (surface != XEEN_SURFACE_SPACE && id == XEEN_CLOUDS_WATER_DRAGON);
    } else {
        *passable = m->ds[XEEN_DS_WORLD] != 0u || id != XEEN_CLOUDS_WATER_DRAGON;
    }
    return XEEN_OK;
}
/*
 * Whether map monster I at X, Y may step in DIRECTION (0..3; DX, DY its step): *PASSABLE set, or a stop as
 * outdoor_passable's. Indoors the test is the source cell's wall toward DIRECTION, outdoors the destination cell's
 * terrain (outdoor_passable). The threshold test (a wall kind passes when it is below the map's threshold, a signed
 * comparison) applies indoors, and outdoors to a kind above 14, which the original sends to the same test.
 * Original: 2A5D1 (the lookup 29C0 at 2A5F6; 2A605 and 2A60F both to the threshold test 2A6CF, DS C0FC).
 */
static xeen_status can_move(xeen_movement *m, int x, int y, uint8_t direction, int dx, int dy,
                            unsigned i, const uint8_t *database, size_t database_size,
                            int *passable, uint32_t *boundary)
{
    uint16_t wall = 0;
    xeen_status status;
    int outdoors;
    if (direction > 3u) {
        return XEEN_BAD_INPUT;
    }
    outdoors = xeen_read16(m, XEEN_DS_OUTDOORS) != 0;
    if (outdoors) {
        status = xeen_maze_lookup(m, (int16_t)(x + dx), (int16_t)(y + dy), 0xfu, 4u, &wall);
    } else {
        status = xeen_maze_lookup(m, (int16_t)x, (int16_t)y, 0xfu,
                                  xeen_read16(m, XEEN_DS_DIRECTION_WALL_SHIFT(direction)), &wall);
    }
    if (status != XEEN_OK) {
        return status;
    }
    if (outdoors && wall <= 14u) {
        return outdoor_passable(m, i, wall, database, database_size, passable, boundary);
    }
    *passable = (int)m->ds[XEEN_DS_MAP_FIELD(THRESHOLD)] > (int)(int16_t)wall;
    return XEEN_OK;
}
/* 2A532: occupancy below three, unconditioned record and 96C4. The
 * original has no range check: 2A548..2A562 form the 8DA6 cell offset in
 * DOS word arithmetic from the record bytes plus the step, so a step to
 * x or y = -1 or 32 (the party crossing a map edge, 2A564) addresses the
 * neighbouring row's word, and 2A5B9/2A5C5 store the low bytes back. */
static xeen_status move_monster(xeen_movement *m, unsigned i, int dx, int dy, uint32_t *boundary)
{
    uint16_t record = (uint16_t)XEEN_MAP_MONSTER(i), src, dst;
    int x = m->ds[record + XEEN_MAP_MONSTER_X], y = m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_Y)], nx = x + dx,
        ny = y + dy;
    (void)boundary;
    dst = (uint16_t)(XEEN_DS_MONSTER_CELL_COUNTS + (uint16_t)((unsigned)ny << 6) + (uint16_t)((unsigned)nx << 1));
    src = (uint16_t)(XEEN_DS_MONSTER_CELL_COUNTS + 64u * (unsigned)y + 2u * (unsigned)x);
    if ((int16_t)xeen_read16(m, dst) >= 3 || m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)] ||
        !m->ds[XEEN_DS_MONSTERS_ENABLED]) {
        return XEEN_OK;
    }
    xeen_write16(m, dst, (uint16_t)(xeen_read16(m, dst) + 1u));
    xeen_write16(m, src, (uint16_t)(xeen_read16(m, src) - 1u));
    m->ds[record + XEEN_MAP_MONSTER_X] = (uint8_t)nx;
    m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_Y)] = (uint8_t)ny;
    m->ds[(uint16_t)(XEEN_DS_MONSTER_MOVED + i)] = 1u;
    return XEEN_OK;
}
/* 2A908..2AA6E: the monster I at X, Y, at offset BOX of the 7 x 7 box around the party (row by row from 3 ahead,
 * 0..48), steps toward the party. Facings 0/2 try the 56C4/5726 step first and the 5788 single-axis step second;
 * facings 1/3 try them in the opposite order. Box row 21..27 is the party row, where the 5788 step is horizontal. */
static xeen_status spatial_move(xeen_movement *m, unsigned i, int x, int y, unsigned box,
                                const uint8_t *database, size_t database_size, uint32_t *boundary)
{
    int first_dx, first_dy, alt_dx, alt_dy, passable = 0, order;
    uint8_t first_direction, alt_direction;
    xeen_status status;
    if (box >= 49u || m->ds[XEEN_DS_PARTY_DIRECTION] > 3u) {
        *boundary = XEEN_MONSTER_UPDATE_BOX;
        return XEEN_SERVICE_REQUIRED;
    }
    first_dx = (int16_t)xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_STEP_DX + 2u * box));
    first_dy = (int16_t)xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_STEP_DY + 2u * box));
    first_direction = m->ds[XEEN_DS_MONSTER_STEP_WALL + box];
    alt_direction = m->ds[XEEN_DS_MONSTER_OTHER_STEP_WALL + box];
    /* the other step is horizontal on the party's row (21..27), vertical elsewhere */
    alt_dx = box >= 21u && box <= 27u ? (int16_t)xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_OTHER_STEP + 2u * box)) : 0;
    alt_dy = box >= 21u && box <= 27u ? 0 : (int16_t)xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_OTHER_STEP + 2u * box));
    for (order = 0; order < 2; ++order) {
        int use_first = (m->ds[XEEN_DS_PARTY_DIRECTION] & 1u) ? order == 1 : order == 0;
        status = can_move(m, x, y, use_first ? first_direction : alt_direction, use_first ? first_dx : alt_dx,
                          use_first ? first_dy : alt_dy, i, database, database_size, &passable,
                          boundary);
        if (status != XEEN_OK) {
            if (!*boundary) {
                *boundary = XEEN_MAZE_QUERY;
            }
            return status == XEEN_CACHE_MISS ? XEEN_SERVICE_REQUIRED : status;
        }
        if (passable) {
            return move_monster(m, i, use_first ? first_dx : alt_dx, use_first ? first_dy : alt_dy,
                                boundary);
        }
    }
    return XEEN_OK;
}

/* 2A727..2A7D7, the update's start: its work arrays cleared (the cell counts, the twelve hit slots' frames and
 * elements, the moved and shot-scheduled marks, the shooter list to FFh), MONSTER_NEARBY cleared, then every map
 * monster (active or not) below row 32 counted on its cell. */
static void update_begin(xeen_movement *m)
{
    unsigned i;
    memset(m->ds + XEEN_DS_MONSTER_CELL_COUNTS, 0, XEEN_MONSTER_CELL_COUNT_BYTES);
    memset(m->ds + XEEN_DS_HIT_FRAMES, 0, XEEN_HIT_SLOTS);
    memset(m->ds + XEEN_DS_HIT_ELEMENT, 0, XEEN_HIT_SLOTS);
    memset(m->ds + XEEN_DS_MONSTER_MOVED, 0, XEEN_MONSTER_UPDATE_SLOTS);
    memset(m->ds + XEEN_DS_MONSTER_SHOT_SCHEDULED, 0, XEEN_MONSTER_UPDATE_SLOTS);
    memset(m->ds + XEEN_DS_MONSTER_SHOOTERS, 0xff, XEEN_MONSTER_SHOOTERS);
    m->ds[XEEN_DS_MONSTER_NEARBY] = 0;
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; ++i) {
        uint16_t record = (uint16_t)XEEN_MAP_MONSTER(i);
        unsigned x = m->ds[record + XEEN_MAP_MONSTER_X], y = m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_Y)];
        if (y < 32u) {
            uint16_t cell = (uint16_t)(XEEN_DS_MONSTER_CELL_COUNTS + 64u * y + 2u * x);
            uint16_t n = (uint16_t)(m->ds[cell] | ((uint16_t)m->ds[(uint16_t)(cell + 1u)] << 8));
            ++n;
            m->ds[cell] = (uint8_t)n;
            m->ds[(uint16_t)(cell + 1u)] = (uint8_t)(n >> 8);
        }
    }
}
/* Map monster I is one of the party's own cell's (the view slots 0..2: 2A8BC..2A8D8): in contact, not a shooter. */
static int in_party_cell(const xeen_movement *m, unsigned i)
{
    return m->ds[XEEN_DS_VIEW_MONSTER(0)] == i || m->ds[XEEN_DS_VIEW_MONSTER(1)] == i ||
           m->ds[XEEN_DS_VIEW_MONSTER(2)] == i;
}
/*
 * Map monster I, standing on cell X, Y at offset BOX of the 7 x 7 box around the party (IN_LINE: the cell shares
 * the party's row or column). Any monster there marks the party's surroundings as occupied (MONSTER_NEARBY). An
 * inactive monster is left alone (except while the party rests), and so is one that has already moved in this
 * update. A monster in line with the party that has a ranged attack, is not yet scheduled, is not in the party's
 * own cell and has no condition is scheduled to shoot (xeen_ranged_schedule, with its database id); then every
 * remaining monster takes its step toward the party (spatial_move).
 *
 * Stops: the monster database not supplied (XEEN_MONSTER_UPDATE_RECORD, SERVICE_REQUIRED) or too short for the
 * monster's row (the same boundary, BAD_INPUT); a refused schedule or step returns its status and boundary.
 * Original: 2A7D7..2AA6E's body for one record (2A87B resolves the 60-byte row; 2A8A8..2A908 schedules a shot;
 * 2A908 steps with the same tables indoors and outdoors, 2A5D1 supplying the wall or terrain test).
 */
static xeen_status update_near_monster(xeen_movement *m, unsigned i, int x, int y, unsigned box, int in_line,
                                       const uint8_t *database, size_t database_size, uint32_t *boundary)
{
    uint16_t record = (uint16_t)XEEN_MAP_MONSTER(i);
    uint8_t id = m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)];
    m->ds[XEEN_DS_MONSTER_NEARBY] = 1;
    if (!m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ACTIVE)] && m->ds[XEEN_DS_UI_MODE] != XEEN_UI_MODE_REST) {
        return XEEN_OK;
    }
    if (m->ds[(uint16_t)(XEEN_DS_MONSTER_MOVED + i)]) {
        return XEEN_OK;
    }
    if (in_line) {
        size_t row = (size_t)id * XEEN_MONSTER_RECORD_BYTES;
        if (!database) {
            *boundary = XEEN_MONSTER_UPDATE_RECORD;
            return XEEN_SERVICE_REQUIRED;
        }
        if (row + XEEN_MONSTER_RECORD_BYTES > database_size) {
            *boundary = XEEN_MONSTER_UPDATE_RECORD;
            return XEEN_BAD_INPUT;
        }
        if (database[row + XEEN_MONSTER_RANGED] && !m->ds[(uint16_t)(XEEN_DS_MONSTER_SHOT_SCHEDULED + i)] &&
            !in_party_cell(m, i) && !m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)]) {
            xeen_status status = xeen_ranged_schedule(m, id, (int16_t)x, (int16_t)y, boundary);
            if (status != XEEN_OK) {
                return status;
            }
            m->ds[(uint16_t)(XEEN_DS_MONSTER_SHOT_SCHEDULED + i)] = 1;
        }
    }
    return spatial_move(m, i, x, y, box, database, database_size, boundary);
}
/*
 * After the moves, every map monster (inactive and off-map ones too), in record order, with a condition other than
 * none or Dragon Sleep tries to shake it off: a draw of random(1, type + 50) at most its type clears it. The draw
 * equal to the type clears (type 0 never recovers: the smallest draw is 1); the bound is a word (305 for type 255);
 * the world plays no part. The comparison is signed as the original's, which for these values (draws 1..305,
 * types 0..255) is the unsigned one. Without the session's random generator (RANDOM NULL) the draw cannot be made:
 * XEEN_MONSTER_UPDATE_RECOVERY, at the first such monster. CTest phase273_monster_recovery holds the boundary.
 * Original: 2B5E9 (the condition +7, the spell's 972E; the type +6); 38ADB (504DA(1, type + 32h), then
 * CMP AX,SI / JG: kept above the type).
 */
static xeen_status recover_conditions(xeen_movement *m, xeen_entity_random_fn random, void *random_user,
                                      uint32_t *boundary)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; ++i) {
        uint16_t record = (uint16_t)XEEN_MAP_MONSTER(i);
        uint8_t condition = m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)],
                id = m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_ID)];
        uint16_t roll = 0;
        if (condition == 0u || condition == XEEN_DAMAGE_DRAGON_SLEEP) {
            continue;
        }
        if (!random) {
            *boundary = XEEN_MONSTER_UPDATE_RECOVERY;
            return XEEN_SERVICE_REQUIRED;
        }
        (void)xeen_entity_rng_range(random(random_user, (uint16_t)(id + 50u)), 1u, (uint16_t)(id + 50u), &roll);
        if ((int16_t)roll <= (int16_t)id) {
            m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_CONDITION)] = 0;
        }
    }
    return XEEN_OK;
}
/* One cell of the update's box (X, Y; BOX its 0..48 index): every map monster standing there, in record order, is
 * updated (update_near_monster; ON_LINE: the cell shares the party's row or column). Stops at the first update that
 * does not end XEEN_OK and returns its status. */
static xeen_status update_cell(xeen_movement *m, int x, int y, unsigned box, int on_line, const uint8_t *database,
                               size_t database_size, uint32_t *boundary)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; ++i) {
        uint16_t record = (uint16_t)XEEN_MAP_MONSTER(i);
        xeen_status status;
        if ((int)m->ds[(uint16_t)(record + XEEN_MAP_MONSTER_Y)] != y || (int)m->ds[record + XEEN_MAP_MONSTER_X] != x) {
            continue;
        }
        status = update_near_monster(m, i, x, y, box, on_line, database, database_size, boundary);
        if (status != XEEN_OK) {
            return status;
        }
    }
    return XEEN_OK;
}
/*
 * The extended monster update, once the release has reached XEEN_MONSTER_UPDATE_MOVES: the work arrays reset
 * and the cells counted (update_begin), then two sweeps of the 7 x 7 box around the party, row by row from the
 * party's y + 3 down to y - 3, each row from x - 3 to x + 3, each cell visiting every map monster standing there
 * (update_near_monster; a monster that moved may be met again at a later cell, but does not move twice), then
 * the conditions' recovery. Ends XEEN_OK, or with XEEN_MONSTER_UPDATE_RANGED when shots were queued (the game
 * delivers them). Address arithmetic is DOS word arithmetic; the cell counts deliberately include inactive
 * records.
 * Original: 2A727..2AAB9, restricted at the first candidate that needs the monster database or a service.
 */
static xeen_status release_extended(xeen_movement *m, const uint8_t *database, size_t database_size,
                                    xeen_entity_random_fn random, void *random_user,
                                    uint32_t *boundary)
{
    xeen_status status = xeen_input_release_96c0(m, boundary);
    unsigned pass;
    int px, py, dx, dy;
    if (status != XEEN_SERVICE_REQUIRED || *boundary != XEEN_MONSTER_UPDATE_MOVES) {
        return status;
    }
    *boundary = 0;
    update_begin(m);
    px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]); /* 80h..FFh negative, for the box arithmetic */
    py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    for (pass = 0; pass < 2u; ++pass) {
        for (dy = 3; dy > -4; --dy) {
            for (dx = -3; dx < 4; ++dx) {
                int x = px + dx, y = py + dy;
                unsigned box = (unsigned)(3 - dy) * 7u + (unsigned)(dx + 3);
                status = update_cell(m, x, y, box, px == x || py == y, database, database_size, boundary);
                if (status != XEEN_OK) {
                    return status;
                }
            }
        }
    }
    status = recover_conditions(m, random, random_user, boundary);
    if (status != XEEN_OK) {
        return status;
    }
    if (m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED]) {
        *boundary = XEEN_MONSTER_UPDATE_RANGED;
        return XEEN_SERVICE_REQUIRED;
    }
    return XEEN_OK;
}
xeen_status xeen_input_release_96c0_extended(xeen_movement *m, uint32_t *boundary)
{
    return release_extended(m, NULL, 0, NULL, NULL, boundary);
}
xeen_status xeen_input_release_96c0_database(xeen_movement *m, const uint8_t *database,
                                             size_t database_size, uint32_t *boundary)
{
    return release_extended(m, database, database_size, NULL, NULL, boundary);
}
xeen_status xeen_input_release_96c0_database_rng(xeen_movement *m, const uint8_t *database,
                                                 size_t database_size, xeen_entity_random_fn random,
                                                 void *random_user, uint32_t *boundary)
{
    return release_extended(m, database, database_size, random, random_user, boundary);
}
