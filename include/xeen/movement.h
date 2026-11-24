#ifndef XEEN_MOVEMENT_H
#define XEEN_MOVEMENT_H
#include <stdint.h>
/* The movement commands: the exploration poll's codes (F5FE) that 17C10's table sends to the turns 17C50/17C8E, the
 * steps and the strafes 17C56/17C94. A step back keeps the facing (the step is facing ^ 2); a strafe steps to the
 * unrotated facing's left or right. */
/* The party's facing (ED96), in the order of the DS:17A2 direction names; a step north is y + 1. */
enum xeen_direction { XEEN_NORTH = 0, XEEN_EAST = 1, XEEN_SOUTH = 2, XEEN_WEST = 3 };
#define XEEN_MOVE_TURN_LEFT    0xf0u /* the facing + 3 */
#define XEEN_MOVE_TURN_RIGHT   0xf1u /* the facing + 1 */
#define XEEN_MOVE_FORWARD      0xf2u
#define XEEN_MOVE_BACK         0xf3u
#define XEEN_MOVE_STRAFE_LEFT  0xb0u
#define XEEN_MOVE_STRAFE_RIGHT 0xb1u
/* The direction a party facing FACING moves with the step COMMAND: its opposite (FACING ^ 2) for a step back, its
 * left ((FACING + 3) & 3) and right ((FACING + 1) & 3) for the strafes, FACING itself for any other command (a
 * facing above 3 stays above 3 unless a strafe masks it). Original: 17C10's step targets (the strafes 17C56/17C94). */
static inline unsigned xeen_step_direction(unsigned facing, unsigned command)
{
    if (command == XEEN_MOVE_BACK) {
        return facing ^ 2u;
    }
    if (command == XEEN_MOVE_STRAFE_LEFT) {
        return (facing + 3u) & 3u;
    }
    if (command == XEEN_MOVE_STRAFE_RIGHT) {
        return (facing + 1u) & 3u;
    }
    return facing;
}
/* The one-cell step towards DIRECTION added to *DX and *DY (north y + 1, east x + 1, south y - 1, west x - 1, the
 * original's byte INC/DEC); returns 0, adding nothing, for a direction above 3. */
static inline int xeen_direction_step(unsigned direction, int *dx, int *dy)
{
    switch (direction) {
        case XEEN_NORTH:
            ++*dy;
            return 1;
        case XEEN_EAST:
            ++*dx;
            return 1;
        case XEEN_SOUTH:
            --*dy;
            return 1;
        case XEEN_WEST:
            --*dx;
            return 1;
        default:
            return 0;
    }
}
/* Byte-addressed logical DS, never a cast of a DOS structure. Navigation bytes
 * reside at ED96..ED99. All other offsets retain their evidence names. */
typedef struct {
    uint8_t direction, x, y, map;
} xeen_navigation;
typedef enum {
    XEEN_OK,
    XEEN_BAD_INPUT,
    XEEN_CACHE_MISS,
    XEEN_SERVICE_REQUIRED
} xeen_status;
typedef enum {
    XEEN_SOUND,
    XEEN_ABILITY,
    XEEN_SCENE,
    XEEN_EVENT,
    XEEN_COMBAT,
    XEEN_LOAD_MAP,
    XEEN_RELOCATE,
    XEEN_WORLD_UPDATE,
    XEEN_FLAG_QUERY,
    XEEN_TILE_EFFECT,
    XEEN_TIME_CONDITIONS,
    XEEN_CLOCK_TAIL,
    XEEN_96C0
} xeen_service;
struct xeen_movement;
/* Required dispatcher: return nonzero when handled; write AX to result.
 * May mutate state. Missing/unhandled services fail explicitly. LOAD_MAP owns
 * ED99/cache/F608; SCENE owns combat/event eligibility and all scene effects.
 * TIME_CONDITIONS implements 2FC7E before its clock call, CLOCK_TAIL implements
 * 30D27 onward (arg=amount, arg2=old EFF0). Neither repeats clock arithmetic.
 * TILE_EFFECT arguments are SI and 972E (remaining two words are zero).
 * 96C0 dispatches original 2A701 (args zero, AX ignored), shared by the
 * translation-time wrapper and scene-prefix expiration. It may mutate DS.
 * Failures are not transactional: earlier observable effects remain. */
typedef int (*xeen_service_fn)(void *, struct xeen_movement *, xeen_service, uint16_t, uint16_t,
                               uint16_t *);
typedef struct xeen_movement {
    uint8_t ds[65536];
    xeen_service_fn service;
    void *user;
    xeen_status status;
    /* The gate's door scene (178E2..178F7) ran; the resumed step
     * takes the gate's AX = 1 without calling it again (cleared there). */
    uint8_t gate_open;
    /* 17264 stopped at its 173A0(0) call (1731B) with its SI (the
     * cell's damage, 1733D); clear for 173A0's other callers (17F11).
     * 1 here (a movement); the session re-owns it as 2 (Rest's wake) or 4
     * (Shoot/Cast's 17EFA) and writes 3 (operation 52's 17608): the
     * session's enum surface_owner values. */
    uint8_t fall_caller;
    uint16_t fall_damage;
    /* 17264 stopped at its 17334 edge step (19D94) with its SI
     * (the destination cell's damage from 1C1C1(2), computed before the
     * load); 17339 runs the 1733D damage scene after the load. */
    uint16_t edge_damage;
} xeen_movement;
uint16_t xeen_read16(const xeen_movement *, uint16_t);
void xeen_write16(xeen_movement *, uint16_t, uint16_t);

/** Reads the 32-bit dword at DS offset A as two words (the high word at A+2, wrapping at 64K). */
static inline uint32_t xeen_ds_read32(const xeen_movement *m, uint16_t a)
{
    return (uint32_t)xeen_read16(m, a) | (uint32_t)xeen_read16(m, (uint16_t)(a + 2u)) << 16;
}

/** Writes V to DS offset A as two words (the high word at A+2, wrapping at 64K). */
static inline void xeen_ds_write32(xeen_movement *m, uint16_t a, uint32_t v)
{
    xeen_write16(m, a, (uint16_t)v);
    xeen_write16(m, (uint16_t)(a + 2u), (uint16_t)(v >> 16));
}
int xeen_signed_byte(uint8_t);
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_navigation xeen_navigation_get(const xeen_movement *);
/* Stores the party's direction, position and map (DS ED96..ED99). */
void xeen_navigation_set(xeen_movement *, xeen_navigation);
/* Initialize zero state and verified offset/shift tables. Does not install stubs. */
void xeen_movement_init(xeen_movement *, xeen_service_fn, void *);
/* Query supports all recovered indices 0..47, directions 0..3. Cache searches
 * bounded to nine slots; absent IDs report CACHE_MISS, never a game rejection.
 * DS address arithmetic wraps to 16 bits, including unusual negative coords. */
xeen_status xeen_navigation_query(xeen_movement *, uint16_t index, uint16_t *ax);
/* The boundary a caller reports when one of these queries fails (their original entry addresses; the movement
 * status says why): a navigation query (1C1C1), the cell metadata query xeen_metadata(_at) (1C844) and the maze
 * cache's cell lookup xeen_maze_lookup (1C680). */
enum xeen_query_boundary {
    XEEN_NAVIGATION_QUERY = 0x1c1c1,
    XEEN_METADATA_QUERY = 0x1c844,
    XEEN_MAZE_QUERY = 0x1c680
};
/* A native wall store with no game caller (test fixtures set walls with it): the wall nibble VALUE (0..15) on side
 * DIRECTION (0..3, its shift XEEN_DS_WALL_SHIFT(direction, 2)) of cell (x, y), resolved through the 3x3 cache like
 * 1C680 (y first, then x; F494 = the slot). It is not the original's wall setter 2DE1D (that is xeen_maze_set_wall,
 * which Bash uses since Phase 332, owner-approved): it writes none of 1C680's scratch words F48E, F490, F492, and it
 * refuses with XEEN_BAD_INPUT a missing neighbour or a cell past the maps, where 2DE1D writes the last found slot's
 * cell (Phase 330 kit); a direction above 3, a value above 15 or a shift above 15 is XEEN_BAD_INPUT (native bounds). */
xeen_status xeen_wall_set(xeen_movement *, int16_t x, int16_t y, uint16_t direction,
                          uint16_t value);
xeen_status xeen_translation_gate(xeen_movement *, uint16_t command, uint16_t *allowed);
void xeen_forward_delta(xeen_movement *);
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_charge_translation_time(xeen_movement *);
/* Only original 1B51A..1B571: phase advancement and gated countdown.
 * Caller has reached the boundary AFTER BIOS sampling and the 9877 guard.
 * Does not check that guard, sample time, process records, or execute a scene.
 * Eligible countdown 1->0 requires XEEN_96C0; missing/unhandled service returns
 * SERVICE_REQUIRED with earlier writes visible. Callback mutations are retained.
 * Like other operations, starts a new status; m must be non-NULL. */
xeen_status xeen_scene_prefix(xeen_movement *);
/* Boundary helper requires at least one coordinate bit 10 set. */
xeen_status xeen_boundary_commit(xeen_movement *);
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_post_translation(xeen_movement *);
/* The party cell's metadata: xeen_metadata_at with the party's signed ED97/ED98 (the caller after a rejected move,
 * the post-action tail, the world entry and the scene light). */
xeen_status xeen_metadata(xeen_movement *);
/* Original 1C844 with its two stack words (x, y, signed16). Missing slot/link rejects. */
xeen_status xeen_metadata_at(xeen_movement *, int16_t x, int16_t y);
/* The 1C680 maze-cache resolution of (x, y) on map ED99 (bit 4 of y
 * then of x selects the 302h..308h neighbour; F494 = the slot). Returns the
 * slot with x, y reduced to 0..15, -1 for a map missing from the cache, -2 for
 * a missing neighbour (F494 keeps the previous slot). */
int xeen_cache_traverse(xeen_movement *, int *x, int *y, uint16_t *id);
/* Original 1C952 (called by 1C9D4 after query 2 and by outdoor 246CD):
 * with 3067 set, 1C844 metadata, light-counter EFF8 decrement when F486,
 * and the C128 exploration bit when any member has skill byte B449; then
 * F60C = (EFF8==0 && C0D8&4000) ? 0 : 4. Metadata failure keeps writes. */
xeen_status xeen_scene_light(xeen_movement *);
/* Executes F2 and one common continuation, stopping before input polling.
 * allowed is gate result; F60A/F64A indicate exit. No keyboard emulation. */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_process_forward(xeen_movement *, uint16_t *allowed);
#endif
