/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Project host or virtual elapsed PIT cycles into BIOS time and calendar values. The pc_clock
 * entry points use cinematic_clock for the carry service; runtime supplies clock reads without
 * capture-derived delays.
 */
#ifndef XEEN_APPROXIMATE_CLOCK_H
#define XEEN_APPROXIMATE_CLOCK_H
#include <stdint.h>
#include "cinematic_clock.h"

enum {
    PC_PIT_HZ = 1193182,
    PC_BIOS_DAY = 0x1800b0,
    PC_VIRTUAL_STEP = 16390
};

typedef struct {
    uint64_t elapsed;
    uint64_t host_origin;
    uint64_t reads;
    uint64_t pit_origin;
    uint64_t bios_base;
    unsigned divisor;
    unsigned year;
    unsigned month;
    unsigned day;
    int virtual_time;
    CinematicClock carry_clock;
} ApproximateClock;

/* Initialize caller-owned clock storage from borrowed calendar strings and host origin.
 * Validates the supported calendar and time, quantizes to the preceding BIOS tick and clamps
 * within the requested day. Returns 1 on success or 0 without initializing storage on invalid
 * input.
 */
int pc_clock_init(ApproximateClock *c, int virtual_time, uint64_t host, const char *date,
                  const char *time);
/* Update real-clock elapsed cycles from a nondecreasing host time. Borrows the initialized
 * clock; virtual mode is unchanged. This does not count as a virtual clock read.
 */
void pc_clock_sync(ApproximateClock *c, uint64_t host);
/* Perform one modeled clock read. Virtual mode debits one fixed quantum and real mode
 * synchronizes with the host; updates the read count. Borrows the clock and returns 0 on
 * elapsed overflow, otherwise 1.
 */
int pc_clock_read(ApproximateClock *c, uint64_t host);
/* Select the supported timer divisor while preserving elapsed time and the BIOS baseline.
 * Borrows and updates the clock; returns 1 on success or 0 for an unsupported divisor.
 */
int pc_clock_program(ApproximateClock *c, unsigned divisor);
/* Return monotonic BIOS tick advances from borrowed initialized clock state. Reads cached
 * state without advancing time or allocating memory.
 */
uint64_t pc_clock_ticks(const ApproximateClock *c);
/* Return the daily wrapped BIOS tick word from borrowed clock state. Reads cached state
 * without advancing time or allocating memory.
 */
uint32_t pc_clock_bios(const ApproximateClock *c);
/* Return the modeled binary square-wave counter from borrowed clock state. Reads cached state
 * without advancing time or allocating memory.
 */
uint16_t pc_clock_pit(const ApproximateClock *c);
/* Write packed hour/minute and second/hundredth words to caller outputs from borrowed clock
 * state. Uses cached daily time without advancing the clock.
 */
void pc_clock_time(const ApproximateClock *c, uint16_t *time_word_high, uint16_t *time_word_low);
/* Write calendar year and packed month/day to caller outputs from borrowed clock state.
 * Advances the initial calendar by elapsed days without modifying or advancing the clock.
 */
void pc_clock_date(const ApproximateClock *c, uint16_t *year, uint16_t *md);
#endif
