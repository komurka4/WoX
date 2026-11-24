#ifndef XEEN_ENTITIES_H
#define XEEN_ENTITIES_H
#include "xeen/resources.h"
/* Recover the object-record portion 1A278..1A344 of map initialization.
 * Input is the original decoded maze%c%03u.mob resource, which DOS loads at
 * ES:677C. Writes DS:E000 eight-byte records and DS:9C5A only. This is not
 * complete map initialization: monsters, walls, sprites and caches remain
 * owned by the unexecuted continuation at 1A344. Unwritten bytes retain their
 * prior values. Rejected/truncated inputs do not mutate DS. */
xeen_resource_status xeen_entities_objects(const uint8_t *, size_t, xeen_movement *, uint16_t *);
/* Original 1A071/1A03D destination for the 1452-byte decoded animation resource.
 * This copy does not reproduce archive handles, loader calls or cache state. */
xeen_resource_status xeen_entities_animation(const uint8_t *, size_t, xeen_movement *);
/* Original 1A071/1A03D third load: decoded xeenpic.dat/darkpic.dat (53 bytes)
 * into DS:306D, the per-sprite wall-item frame counts read by 1B0F6. Exact
 * size required; rejected input does not mutate DS. */
xeen_resource_status xeen_entities_wall_frames(const uint8_t *, size_t, xeen_movement *);
/* Original MZ DS:4C27 orientation mapping, retained by C startup BSS clearing.
 * Needed by the selected map scene prefix; not a complete DS initialization. */
xeen_resource_status xeen_entities_orientation(xeen_movement *);
/* Callback receives pre-time-service AX and returns raw mixer output (except
 * map initialization, whose adapter returns 0..7). AH is overwritten at 50525.
 * Caller AL must not be confused with the DOS time service return AL. */
typedef uint16_t (*xeen_entity_random_fn)(void *, uint16_t incoming_ax);
/* The map load's monsters and wall items (original continuation 1A344..1A602):
 * the .mob file's monster and wall-item lists checked first, then installed;
 * the Clouds arena (map 85) keeps its records but a monster count of 0
 * (1A4CA), and on Clouds map 15 with its first three monsters off the map
 * party flag 56 is cleared (1A5BF..1A602). The RNG is an explicit synchronous
 * original inclusive 0..7 service; a missing service or backing rejects
 * before any write. Unwritten record bytes retain prior state; FF mappings
 * are never skipped. The game calls the aliased form (below). */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_resource_status xeen_entities_monsters_walls(const uint8_t *, size_t, const uint8_t *, size_t,
                                                  xeen_movement *, xeen_entity_random_fn, void *);
/* The same over the original's whole 256-row monster table (db_size bytes):
 * a record's row must lie inside the loaded file (file_size) or wholly at or
 * past alias_start (bytes the original holds there, supplied in db). */
xeen_resource_status xeen_entities_monsters_walls_aliased(const uint8_t *, size_t, const uint8_t *,
                                                          size_t, size_t, size_t, xeen_movement *,
                                                          xeen_entity_random_fn, void *);
/* Original 50529..50560 mix after time sampling. State owns the three original
 * CS seed words. ax/cx/dx are explicit post-time-service registers; this is
 * not a conventional seeded PRNG or a production wall-clock adapter. */
typedef struct {
    uint16_t words[3];
} xeen_entity_rng;
uint16_t xeen_entity_rng_mix(xeen_entity_rng *, uint16_t, uint16_t, uint16_t);
/* INT21/AH=2C native service contract: MS-DOS 4 clears AL and preserves AH.
 * Each draw samples hour/minute/second/hundredth once; no BIOS emulation. */
typedef struct {
    uint8_t hour, minute, second, hundredth;
} xeen_rng_time;
typedef struct {
    uint16_t incoming_ax, post_time_ax, cx, dx, raw;
    xeen_entity_rng before, after;
} xeen_rng_transition;
uint16_t xeen_entity_rng_draw(xeen_entity_rng *, uint16_t, xeen_rng_time, xeen_rng_transition *);
/* The carry flag 50525 returns with a traced draw: its final
 * SBB AX,DX (5055D) after ADC AX,CX with the carry XOR cleared. */
int xeen_entity_rng_carry(const xeen_rng_transition *);
/* Original 504E0..504F1: mask sign bit, signed IDIV by wrapped width, add low.
 * Return 0 for divide-by-zero; leave result untouched. All 16-bit ranges,
 * including wrapped/negative widths, retain original signed remainder rules. */
int xeen_entity_rng_range(uint16_t raw, uint16_t low, uint16_t high, uint16_t *result);
#endif
