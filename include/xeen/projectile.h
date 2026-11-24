#ifndef XEEN_PROJECTILE_H
#define XEEN_PROJECTILE_H
#include "xeen/audio.h"
#include "xeen/sprites.h"
/* Original 4128F presentation/travel ownership, before its 38049 action loop.
 * Zero-initialize for each NEW delivery; retain across suspended advances.
 * DS97D0 remains the sole action queue; no second gameplay queue is stored. */
typedef struct {
    uint8_t started, complete, finished, pow_id;
    uint16_t handle, effect, scenes;
    char resource[16];
} xeen_projectile_delivery;
/* 41298..414B0: inspect original queued DB IDs, select/load pow*.icn, request
 * native audio and install projectile handles/scales. Preserves the original
 * physical-only queue FX lookup through 97F4. Unsupported DB/types or missing
 * resources reject before gameplay writes/effect request. */
xeen_status xeen_projectile_setup(xeen_projectile_delivery *, xeen_movement *, const uint8_t *,
                                  size_t, xeen_sprite_cache *, const xeen_sprite_archives *,
                                  xeen_native_audio *, uint32_t *boundary);
/* One semantic 1B503 frame: original prefix, monster visibility/attacker refresh,
 * 2469F/25390 completion scan, then original 1B0F6 animation/tier travel.
 * Does not rasterize a frame or advance gameplay time. Completion includes the
 * final empty redraw. Native host events remain owned by the caller; no player
 * command is consumed here. Returns OK, with p->complete exposing 414C4 readiness.
 * An unsupported partial frame retains writes and must not be retried blindly. */
xeen_status xeen_projectile_advance(xeen_projectile_delivery *, xeen_movement *, const uint8_t *,
                                    size_t, uint32_t *boundary);
/* Clears member MEMBER's projectile records (the four levels' scales to 0, far to near, then their handles to
 * FFFFh): 39654's per-member clear, which the volley also applies to a member whose missile hit (37506/37581). */
void xeen_projectile_clear_member(xeen_movement *, unsigned member);
/* 39654: 97F7 = 0, every member's projectile records cleared, the missile slots (XEEN_DS_MISSILE_SLOTS) zeroed;
 * the end of a missile attack, shared by xeen_projectile_finish and combat's volley. */
void xeen_projectile_end_attack(xeen_movement *);
/* Original 39654 cleanup; allowed after completion only. Idempotent. Does NOT
 * clear 97F6/97D0, map the combat party, resolve damage or claim the outer
 * caller's next input boundary. Those belong to 4128F's action owner. */
xeen_status xeen_projectile_finish(xeen_projectile_delivery *, xeen_movement *, uint32_t *boundary);
#endif
