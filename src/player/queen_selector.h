/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Choose animation variants from explicit time inputs and three mutable selector words.
 * queen_select and queen_select_range preserve width and carry behavior for runtime and scene
 * callers; queen_clock formats time inputs for diagnostics.
 */
#ifndef XEEN_QUEEN_SELECTOR_H
#define XEEN_QUEEN_SELECTOR_H
#include <stdint.h>

typedef struct {
    uint16_t word[3];
} QueenSelector;

/* Advance the caller-owned selector from the accumulator and packed time words and return an
 * index from zero through five. Requires initialized state and allocates nothing.
 */
uint16_t queen_select(QueenSelector *state, uint16_t accumulator, uint16_t time_word_high,
                      uint16_t time_word_low);
/* Advance the caller-owned selector using an accumulator and packed time words, then select
 * within an inclusive interval. The caller supplies a positive nonzero interval width. Returns
 * the selected word and retains the advanced selector state; allocates nothing.
 */
uint16_t queen_select_range(QueenSelector *state, uint16_t accumulator, uint16_t time_word_high,
                            uint16_t time_word_low, uint16_t minimum, uint16_t maximum);
/* Project an origin in hundredths and elapsed timer cycles to packed time-of-day words. Writes
 * caller outputs, wraps at midnight and allocates nothing.
 */
void queen_clock(uint64_t origin_hundredths, uint64_t elapsed_cycles, uint16_t *time_word_high,
                 uint16_t *time_word_low);
/* Initialize the caller-owned selector's three mutable words to their fixed startup values.
 * Requires nonnull storage and allocates nothing.
 */
void queen_selector_init(QueenSelector *state);
#endif
