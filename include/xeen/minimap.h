#ifndef XEEN_MINIMAP_H
#define XEEN_MINIMAP_H
#include "xeen/presentation.h"
/* Original 1C680 maze lookup (x, y, mask, shift), signed16 coordinates.
 * x or y > 31: F48E = 8888, result 8888. Resolves the cached slot (F494) and
 * at most one neighbour per axis (y first). A zero y link sets F492 = 1, a
 * zero x link (or map id 0 when no axis wrapped: the original reuses DX) sets
 * F492 = W(F608); both return F608 ? 000F : 8888. Otherwise F490 is the
 * surface type (F608: BDCC word & F, else BFCC byte & 7), F492 the seen flag
 * (C0EA[F490] is 0A or 0F, else the stepped-on bit at C128+slot*37C), and
 * the result (BDCC word >> shift) & mask. A slot not in the native cache
 * rejects with XEEN_CACHE_MISS (the original searches unbounded). */
xeen_status xeen_maze_lookup(xeen_movement *, int16_t x, int16_t y, uint16_t mask, uint16_t shift,
                             uint16_t *result);
/* Original 2DE1D(x, y, side, wall), the wall setter of the door code and of
 * event operation 19: 1C680(x, y, 0, 0) for its F494, then the slot's BDCC
 * word at (y & 15, x & 15) &= W(18C2 + 2 side) and |= wall << B(7D2 + 60h
 * side) (SHL by CL: a count of 16..31 gives 0). Sides 0..3 only (the
 * tables' rows); a lookup failure is returned with no write. */
xeen_status xeen_maze_set_wall(xeen_movement *, int16_t x, int16_t y, unsigned side, uint16_t wall);
/* Original 2DE91(x, y, flag): 1C680(x, y, 0, 0), then the slot's BFCC cell
 * byte at (y & 15, x & 15) |= flag. */
xeen_status xeen_maze_set_cell_flag(xeen_movement *, int16_t x, int16_t y, uint8_t flag);

/* Original 1E467 minimap, called at 1BF5D. Both branches: minimap off (ED9F
 * and EDA0 zero: one draw) and the automap/wizard-eye grid (outdoor F608!=0
 * or indoor passes). Draws are (handle, frame, x, y, 0, 0, window1) through
 * driver 4AC92, in original order. Runs on a copy of DS and commits only on
 * genuine return; a lookup failure leaves DS unchanged. Gates 986E/9876/9894
 * return without drawing. Writes F48E/F490/F492/F494 and EDA0 as original. */
#define XEEN_MINIMAP_MAX_DRAWS 512u
typedef struct {
    size_t count;
    xeen_sprite_draw draws[XEEN_MINIMAP_MAX_DRAWS];
} xeen_minimap_draws;
xeen_status xeen_scene_minimap(xeen_movement *, xeen_minimap_draws *);
/* The same with the driver's tests: FATAL(user, draw) nonzero for a
 * draw 4AC92 would not return from (4AD19). The pass then ends at that draw
 * (the last listed), DS committed as it stands there, *STOPPED = 1. */
typedef int (*xeen_minimap_driver_fn)(void *user, const xeen_sprite_draw *);
xeen_status xeen_scene_minimap_driver(xeen_movement *, xeen_minimap_draws *,
                                      xeen_minimap_driver_fn fatal, void *user, int *stopped);
/* Original 1DA2D, the map of the M command (17DC4). xeen_map_begin
 * is 1DA40..1DB52 after 1871B(5): [BP-2] = F47C x 2; F494 = the slot of ED99
 * (the original's unbounded F46A search; a map outside the native's nine
 * cache slots rejects with XEEN_CACHE_MISS, nothing written); the view's
 * origin ([BP-4], [BP-6]) and the party arrow's offsets ([BP-0Bh], [BP-0Ch])
 * from ED97/ED98, the clamps 8/17h and 8/18h and DS words C0D4, C0D0, C0D2,
 * C0CE (absolute: slot 0's links) and DS:4F8E/4F9E. */
typedef struct {
    uint16_t anim;
    int16_t x, y;
    uint8_t arrow_x, arrow_y, blink;
} xeen_map_view;
xeen_status xeen_map_begin(xeen_movement *, xeen_map_view *);
/* One pass of 1DB77..1E35B (between 4E38C(5, "\r") and 4EA1C): outdoors
 * (F608) the three 16x16 passes (C0EA surfaces, C0DA + 10h, the 8-bit layer
 * + 20h), indoors [BP-2] = ([BP-2] + 2) mod 8 and the floor, edge, corner,
 * wall and fog passes (the CS:0A29/0A0B/09ED/09CF tables, the minimap's);
 * then (FRAMED: the draws before it) 4F6F8(5), and indoors the 34h frame at
 * (4Ch, 1Eh), outdoors with [BP-8] the F458 arrow ED96 + 1. Every draw is
 * 4AC92(handle, frame, x, y, 0, 0, 0). Writes F48E/F490/F492 as 1C680 does;
 * runs on a copy of DS and commits only when the pass completes. */
#define XEEN_MAP_MAX_DRAWS 1600u
typedef struct {
    size_t count, framed;
    xeen_sprite_draw draws[XEEN_MAP_MAX_DRAWS];
} xeen_map_draws;
xeen_status xeen_scene_map(xeen_movement *, xeen_map_view *, xeen_map_draws *);
#endif
