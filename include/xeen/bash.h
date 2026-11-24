#ifndef XEEN_BASH_H
#define XEEN_BASH_H
#include "xeen/combat.h"

typedef struct {
    uint8_t first, second, wall, threshold, roll;
    uint16_t might;
    int opened;
} xeen_bash_result;

/* Original 3D689 indoor wall-bash gameplay effects. Party member fields are
 * zero-based in the result; FF means that no second able member was found.
 * The caller owns the preceding 28178 clock call and following 17F07 path. */
xeen_status xeen_bash_wall(xeen_movement *, xeen_entity_random_fn, void *, xeen_native_audio *,
                           xeen_bash_result *);
#endif
