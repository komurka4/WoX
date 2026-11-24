/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Compute carry-clock samples, elapsed ticks and deadlines with bounded integer arithmetic.
 * The cinematic_clock entry points supply approximate_clock and runtime with a mathematical
 * clock independent of native device scheduling.
 */
#ifndef XEEN_CINEMATIC_CLOCK_H
#define XEEN_CINEMATIC_CLOCK_H
#include <stdint.h>

/* Arithmetic model of the cinematic carry clock. A 16-bit accumulator
 * advances BIOS time whenever it wraps. bios_advances is monotonic; the
 * projected 32-bit bios_ticks wraps without a daily or calendar reset.
 */
enum {
    CINEMATIC_CLOCK_QUANTUM = 16390,
    CINEMATIC_CLOCK_INITIAL_CARRY = 0
};

typedef struct {
    uint64_t first_irq_pit;
    uint32_t initial_bios_ticks;
    uint16_t initial_carry;
} CinematicClock;

typedef struct {
    uint64_t irq_count;
    uint64_t bios_advances;
    uint32_t bios_ticks; /* base+advances modulo2^32; no BIOS midnight reset. */
    uint16_t carry;
} CinematicClockSample;

/* Initialize caller-owned carry-clock storage immediately before its first interrupt time.
 * Takes the initial tick baseline and carry word; returns 1 on success or 0 for null storage.
 * A supplied nonzero carry is a boundary input whose reachability is the caller's
 * responsibility.
 */
int cinematic_clock_init(CinematicClock *clock, uint64_t first_irq_pit, uint32_t bios_ticks,
                         uint16_t carry);
/* Project the carry clock at a requested absolute time into caller-owned sample storage.
 * Borrows clock state; interrupts exactly at the query time count. Returns 1 on success or 0
 * for null inputs. Grouped arithmetic avoids overflow without mutating the clock.
 */
int cinematic_clock_read(const CinematicClock *clock, uint64_t pit, CinematicClockSample *sample);
/* Compute monotonic tick advances between reset and query times in caller output storage.
 * Borrows the clock; returns 1 on success or 0 for backwards time or invalid inputs. Callers
 * apply narrower timer arithmetic explicitly.
 */
int cinematic_clock_elapsed(const CinematicClock *clock, uint64_t reset_pit, uint64_t pit,
                            uint64_t *elapsed);
/* Find the earliest representable time at or after reset whose elapsed ticks meet the
 * threshold. Borrows the clock and writes caller output only on success. Returns 0 for an
 * unreachable or invalid deadline; zero ticks returns the reset time.
 */
int cinematic_clock_deadline(const CinematicClock *clock, uint64_t reset_pit, uint64_t ticks,
                             uint64_t *out);
#endif
