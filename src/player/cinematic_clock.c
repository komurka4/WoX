/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Compute carry-clock samples, elapsed ticks and deadlines with bounded integer arithmetic.
 * The cinematic_clock entry points supply approximate_clock and runtime with a mathematical
 * clock independent of native device scheduling.
 */
#include "cinematic_clock.h"
#include <stddef.h>

/* Initialize caller-owned carry-clock storage immediately before its first interrupt time.
 * Takes the initial tick baseline and carry word; returns 1 on success or 0 for null storage.
 * A supplied nonzero carry is a boundary input whose reachability is the caller's
 * responsibility.
 */
int cinematic_clock_init(CinematicClock *clock, uint64_t first_irq_pit, uint32_t bios_ticks,
                         uint16_t carry)
{
    if (!clock) {
        return 0;
    }
    clock->first_irq_pit = first_irq_pit;
    clock->initial_bios_ticks = bios_ticks;
    clock->initial_carry = carry;
    return 1;
}

/* Project the carry clock at a requested absolute time into caller-owned sample storage.
 * Borrows clock state; interrupts exactly at the query time count. Returns 1 on success or 0
 * for null inputs. Grouped arithmetic avoids overflow without mutating the clock.
 */
int cinematic_clock_read(const CinematicClock *clock, uint64_t pit, CinematicClockSample *sample)
{
    CinematicClockSample value;
    uint64_t groups;
    uint32_t tail;
    if (!clock || !sample) {
        return 0;
    }
    value.irq_count =
        pit < clock->first_irq_pit ? 0 : (pit - clock->first_irq_pit) / CINEMATIC_CLOCK_QUANTUM + 1;
    /* 65536 additions return to the same carry and advance BIOS16390 times.
     * Split before multiplying: irq_count*16390 can overflow at UINT64_MAX PIT.
     */
    groups = value.irq_count / UINT64_C(65536);
    tail = (uint32_t)(value.irq_count % UINT64_C(65536)) * CINEMATIC_CLOCK_QUANTUM +
           clock->initial_carry;
    value.bios_advances = groups * CINEMATIC_CLOCK_QUANTUM + tail / UINT32_C(65536);
    value.carry = (uint16_t)tail;
    value.bios_ticks = clock->initial_bios_ticks + (uint32_t)value.bios_advances;
    *sample = value;
    return 1;
}

/* Compute monotonic tick advances between reset and query times in caller output storage.
 * Borrows the clock; returns 1 on success or 0 for backwards time or invalid inputs. Callers
 * apply narrower timer arithmetic explicitly.
 */
int cinematic_clock_elapsed(const CinematicClock *clock, uint64_t reset_pit, uint64_t pit,
                            uint64_t *elapsed)
{
    CinematicClockSample start;
    CinematicClockSample end;
    if (!elapsed || pit < reset_pit || !cinematic_clock_read(clock, reset_pit, &start) ||
        !cinematic_clock_read(clock, pit, &end)) {
        return 0;
    }
    *elapsed = end.bios_advances - start.bios_advances;
    return 1;
}

/* Find the earliest representable time at or after reset whose elapsed ticks meet the
 * threshold. Borrows the clock and writes caller output only on success. Returns 0 for an
 * unreachable or invalid deadline; zero ticks returns the reset time.
 */
int cinematic_clock_deadline(const CinematicClock *clock, uint64_t reset_pit, uint64_t ticks,
                             uint64_t *out)
{
    uint64_t low = reset_pit;
    uint64_t high = UINT64_MAX;
    uint64_t delta;
    uint64_t middle;
    if (!out || !cinematic_clock_elapsed(clock, reset_pit, high, &delta) || delta < ticks) {
        return 0;
    }
    /* At most64 probes. The inverse has no overflowing tick*65536 product. */
    while (low < high) {
        middle = low + (high - low) / 2;
        if (!cinematic_clock_elapsed(clock, reset_pit, middle, &delta)) {
            return 0;
        }
        if (delta >= ticks) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    *out = low;
    return 1;
}
