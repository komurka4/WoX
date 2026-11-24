/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Choose animation variants from explicit time inputs and three mutable selector words.
 * queen_select and queen_select_range preserve width and carry behavior for runtime and scene
 * callers; queen_clock formats time inputs for diagnostics.
 */
#include "queen_selector.h"

#define SELECTOR_INITIAL_WORD_0 51487
#define SELECTOR_INITIAL_WORD_1 19363
#define SELECTOR_INITIAL_WORD_2 20690
#define SELECTOR_NONNEGATIVE_MASK 0x7fff
#define SELECTOR_DAY_HUNDREDTHS 8640000
#define SELECTOR_PIT_HZ 1193182
#define SELECTOR_DAY_SECONDS 86400
#define SELECTOR_HOUR_HUNDREDTHS 360000
#define SELECTOR_MINUTE_HUNDREDTHS 6000

/* Add with a 16-bit result and publish the carry through the borrowed flag.
 * Keep carry separate so selector arithmetic reproduces chained operations.
 */
static uint16_t selector_add_with_carry(uint16_t a, uint16_t b, unsigned *carry)
{
    uint32_t sum = (uint32_t)a + b + *carry;
    *carry = sum >> 16;
    return (uint16_t)sum;
}

/* Subtract words and the incoming borrow, returning the low word and writing the outgoing
 * borrow through caller storage. Requires a valid flag pointer and allocates nothing.
 */
static uint16_t selector_subtract_with_borrow(uint16_t a, uint16_t b, unsigned *carry)
{
    uint32_t sub = (uint32_t)b + *carry;
    *carry = (uint32_t)a < sub;
    return (uint16_t)((uint32_t)a - sub);
}

/* Rotate a word right through the caller's carry bit and publish its low bit as the next
 * carry. Requires a valid flag pointer and allocates nothing.
 */
static uint16_t selector_rotate_with_carry(uint16_t a, unsigned *carry)
{
    unsigned next = a & 1;
    uint16_t result = (uint16_t)((a >> 1) | (*carry << 15));
    *carry = next;
    return result;
}

/* Initialize the caller-owned selector's three mutable words to their fixed startup values.
 * Requires nonnull storage and allocates nothing.
 */
void queen_selector_init(QueenSelector *selector)
{
    selector->word[0] = SELECTOR_INITIAL_WORD_0;
    selector->word[1] = SELECTOR_INITIAL_WORD_1;
    selector->word[2] = SELECTOR_INITIAL_WORD_2;
}

/* Advance the caller-owned selector using an accumulator and packed time words, then select
 * within an inclusive interval. The caller supplies a positive nonzero interval width. Returns
 * the selected word and retains the advanced selector state; allocates nothing.
 */
uint16_t queen_select_range(QueenSelector *selector, uint16_t accumulator, uint16_t time_word_high,
                            uint16_t time_word_low, uint16_t minimum, uint16_t maximum)
{
    unsigned carry = 0;
    time_word_low ^= selector->word[0];
    time_word_high = selector_add_with_carry(time_word_high, selector->word[1], &carry);
    accumulator = selector_subtract_with_borrow(accumulator, selector->word[2], &carry);
    time_word_high = selector_add_with_carry(time_word_high, time_word_high, &carry);
    accumulator = selector_add_with_carry(accumulator, accumulator, &carry);
    time_word_high = selector_add_with_carry(time_word_high, time_word_high, &carry);
    time_word_low = selector_rotate_with_carry(time_word_low, &carry);
    time_word_high = selector_add_with_carry(time_word_high, time_word_high, &carry);
    time_word_low = selector_rotate_with_carry(time_word_low, &carry);
    time_word_high = selector_add_with_carry(time_word_high, time_word_high, &carry);
    accumulator = selector_add_with_carry(accumulator, accumulator, &carry);
    time_word_low = selector_rotate_with_carry(time_word_low, &carry);
    time_word_high = selector_add_with_carry(time_word_high, time_word_high, &carry);
    selector->word[0] = selector_add_with_carry(selector->word[0], time_word_low, &carry);
    selector->word[1] = selector_subtract_with_borrow(selector->word[1], time_word_high, &carry);
    selector->word[2] ^= accumulator;
    carry = 0;
    accumulator = selector_add_with_carry(accumulator, time_word_high, &carry);
    accumulator = selector_subtract_with_borrow(accumulator, time_word_low, &carry);
    /* Clearing the sign bit gives a nonnegative remainder in the requested
     * inclusive range. The queen mouth selector uses the range zero to five.
     */
    return (uint16_t)(minimum + (accumulator & SELECTOR_NONNEGATIVE_MASK) %
                                    ((unsigned)maximum + 1 - minimum));
}

/* Advance the caller-owned selector from the accumulator and packed time words and return an
 * index from zero through five. Requires initialized state and allocates nothing.
 */
uint16_t queen_select(QueenSelector *selector, uint16_t accumulator, uint16_t time_word_high,
                      uint16_t time_word_low)
{
    return queen_select_range(selector, accumulator, time_word_high, time_word_low, 0, 5);
}

/* Project an origin in hundredths and elapsed timer cycles to packed time-of-day words. Writes
 * caller outputs, wraps at midnight and allocates nothing.
 */
void queen_clock(uint64_t origin, uint64_t cycles, uint16_t *time_word_high,
                 uint16_t *time_word_low)
{
    uint64_t t = (origin % SELECTOR_DAY_HUNDREDTHS +
                  (cycles / SELECTOR_PIT_HZ % SELECTOR_DAY_SECONDS) * 100 +
                  (cycles % SELECTOR_PIT_HZ) * 100 / SELECTOR_PIT_HZ) %
                 SELECTOR_DAY_HUNDREDTHS;
    *time_word_high =
        (uint16_t)(((t / SELECTOR_HOUR_HUNDREDTHS) << 8) | ((t / SELECTOR_MINUTE_HUNDREDTHS) % 60));
    *time_word_low = (uint16_t)((((t / 100) % 60) << 8) | (t % 100));
}
