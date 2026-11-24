/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Project host or virtual elapsed PIT cycles into BIOS time and calendar values. The pc_clock
 * entry points use cinematic_clock for the carry service; runtime supplies clock reads without
 * capture-derived delays.
 */
#include "approximate_clock.h"
#include <stdio.h>
#include <string.h>

#define PC_SERVICE_RELOAD_PIT_CYCLES 16390
#define PC_BIOS_RELOAD_PIT_CYCLES 65536
#define PC_DAY_HUNDREDTHS 8640000
#define PC_HOUR_HUNDREDTHS 360000
#define PC_MINUTE_HUNDREDTHS 6000

static int pc_calendar_is_leap_year(unsigned year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static unsigned pc_calendar_month_days(unsigned year, unsigned month)
{
    static const unsigned MONTH_DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return MONTH_DAYS[month - 1] + (month == 2 && pc_calendar_is_leap_year(year));
}

/* Initialize caller-owned clock storage from borrowed calendar strings and host origin.
 * Validates the supported calendar and time, quantizes to the preceding BIOS tick and clamps
 * within the requested day. Returns 1 on success or 0 without initializing storage on invalid
 * input.
 */
int pc_clock_init(ApproximateClock *clock, int virtual_time, uint64_t host, const char *date,
                  const char *time)
{
    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    unsigned hundredth;
    int used = 0;
    if (!clock || !date || !time || sscanf(date, "%u-%u-%u%n", &year, &month, &day, &used) != 3 ||
        used != 10 || date[used] || year < 1980 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > pc_calendar_month_days(year, month)) {
        return 0;
    }
    used = 0;
    if (sscanf(time, "%u:%u:%u.%u%n", &hour, &minute, &second, &hundredth, &used) != 4 ||
        used != 11 || time[used] || hour > 23 || minute > 59 || second > 59 || hundredth > 99) {
        return 0;
    }
    /* Time is quantized to the preceding BIOS tick, as DOS2Ch cannot
     * represent independent hundredths between BIOS updates.
     */
    memset(clock, 0, sizeof(*clock));
    clock->year = year;
    clock->month = month;
    clock->day = day;
    clock->bios_base = ((uint64_t)(hour * PC_HOUR_HUNDREDTHS + minute * PC_MINUTE_HUNDREDTHS +
                                   second * 100 + hundredth) *
                        PC_PIT_HZ) /
                       (PC_BIOS_RELOAD_PIT_CYCLES * 100ULL);
    /* Standard BIOS midnight threshold is slightly earlier than24h at the
     * specified nominal oscillator. Saturate unrepresentable late hundredths
     * to the final tick of the requested day, never advance its date at init.
     */
    if (clock->bios_base >= PC_BIOS_DAY) {
        clock->bios_base = PC_BIOS_DAY - 1;
    }
    clock->host_origin = host;
    clock->virtual_time = virtual_time;
    clock->divisor = PC_BIOS_RELOAD_PIT_CYCLES;
    return 1;
}

/* Update real-clock elapsed cycles from a nondecreasing host time. Borrows the initialized
 * clock; virtual mode is unchanged. This does not count as a virtual clock read.
 */
void pc_clock_sync(ApproximateClock *clock, uint64_t host)
{
    if (!clock->virtual_time && host >= clock->host_origin) {
        clock->elapsed = host - clock->host_origin;
    }
}

/* Perform one modeled clock read. Virtual mode debits one fixed quantum and real mode
 * synchronizes with the host; updates the read count. Borrows the clock and returns 0 on
 * elapsed overflow, otherwise 1.
 */
int pc_clock_read(ApproximateClock *clock, uint64_t host)
{
    if (clock->virtual_time) {
        if (clock->elapsed > UINT64_MAX - PC_VIRTUAL_STEP) {
            return 0;
        }
        clock->elapsed += PC_VIRTUAL_STEP;
    } else {
        pc_clock_sync(clock, host);
    }
    clock->reads++;
    return 1;
}

/* Select the supported timer divisor while preserving elapsed time and the BIOS baseline.
 * Borrows and updates the clock; returns 1 on success or 0 for an unsupported divisor.
 */
int pc_clock_program(ApproximateClock *clock, unsigned divisor)
{
    if (!clock ||
        (divisor != PC_SERVICE_RELOAD_PIT_CYCLES && divisor != PC_BIOS_RELOAD_PIT_CYCLES)) {
        return 0;
    }
    clock->bios_base = pc_clock_ticks(clock);
    clock->pit_origin = clock->elapsed;
    clock->divisor = divisor;
    if (divisor == PC_SERVICE_RELOAD_PIT_CYCLES) {
        return cinematic_clock_init(&clock->carry_clock,
                                    clock->elapsed + PC_SERVICE_RELOAD_PIT_CYCLES, 0, 0);
    }
    return 1;
}

/* Return monotonic BIOS tick advances from borrowed initialized clock state. Reads cached
 * state without advancing time or allocating memory.
 */
uint64_t pc_clock_ticks(const ApproximateClock *clock)
{
    CinematicClockSample sample;
    if (clock->divisor == PC_SERVICE_RELOAD_PIT_CYCLES) {
        cinematic_clock_read(&clock->carry_clock, clock->elapsed, &sample);
        return clock->bios_base + sample.bios_advances;
    }
    return clock->bios_base + (clock->elapsed - clock->pit_origin) / PC_BIOS_RELOAD_PIT_CYCLES;
}

/* Return the daily wrapped BIOS tick word from borrowed clock state. Reads cached state
 * without advancing time or allocating memory.
 */
uint32_t pc_clock_bios(const ApproximateClock *clock)
{
    return (uint32_t)(pc_clock_ticks(clock) % PC_BIOS_DAY);
}

/* Return the modeled binary square-wave counter from borrowed clock state. Reads cached state
 * without advancing time or allocating memory.
 */
uint16_t pc_clock_pit(const ApproximateClock *clock)
{
    /* Binary square-wave mode with an even reload counts down by two
     * in both halves of the oscillator period.
     */
    uint64_t phase = (clock->elapsed - clock->pit_origin) % (clock->divisor / 2);
    return (uint16_t)(clock->divisor - 2 * phase);
}

/* Write packed hour/minute and second/hundredth words to caller outputs from borrowed clock
 * state. Uses cached daily time without advancing the clock.
 */
void pc_clock_time(const ApproximateClock *clock, uint16_t *time_word_high, uint16_t *time_word_low)
{
    uint64_t cs = (uint64_t)pc_clock_bios(clock) * PC_BIOS_RELOAD_PIT_CYCLES * 100 / PC_PIT_HZ;
    cs %= PC_DAY_HUNDREDTHS;
    *time_word_high =
        (uint16_t)(((cs / PC_HOUR_HUNDREDTHS) << 8) | ((cs / PC_MINUTE_HUNDREDTHS) % 60));
    *time_word_low = (uint16_t)((((cs / 100) % 60) << 8) | (cs % 100));
}

/* Write calendar year and packed month/day to caller outputs from borrowed clock state.
 * Advances the initial calendar by elapsed days without modifying or advancing the clock.
 */
void pc_clock_date(const ApproximateClock *clock, uint16_t *year, uint16_t *md)
{
    unsigned year_value = clock->year;
    unsigned month = clock->month;
    unsigned day = clock->day;
    uint64_t extra = pc_clock_ticks(clock) / PC_BIOS_DAY;
    while (extra) {
        unsigned left = pc_calendar_month_days(year_value, month) - day + 1;
        if (extra < left) {
            day += (unsigned)extra;
            break;
        }
        extra -= left;
        day = 1;
        if (++month == 13) {
            month = 1;
            year_value++;
        }
    }
    *year = (uint16_t)year_value;
    *md = (uint16_t)(month * 256 + day);
}
