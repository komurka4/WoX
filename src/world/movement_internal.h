/*
 * The movement module's internals shared with its test support (src/test_support/movement_test_support.c, Phase
 * 284). Not part of the public API (include/xeen/movement.h) and not installed.
 */
#ifndef XEEN_MOVEMENT_INTERNAL_H
#define XEEN_MOVEMENT_INTERNAL_H
#include "xeen/movement.h"
#include <stdint.h>
/* DS byte, word read and word write at A of the movement state `m` in scope. */
#define B(a)    (m->ds[(uint16_t)(a)])
#define W(a)    xeen_read16(m, (uint16_t)(a))
#define S(a, v) xeen_write16(m, (uint16_t)(a), (uint16_t)(v))
/* A 16-bit word as the original's signed value, widened to 32 bits. */
int32_t xeen_movement_signed_word(uint16_t w);
/* One host service S(a, b): its result, 0 when it was refused (m->status then XEEN_SERVICE_REQUIRED) or a refusal
 * is pending. */
uint16_t xeen_movement_request_service(xeen_movement *m, xeen_service s, uint16_t a, uint16_t b);
#endif
