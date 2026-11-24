#include "xeen/party.h"
#include "xeen/main_menu.h"
#include "xeen/ds_globals.h"
#include "xeen/character_record.h"
#include "xeen/byte_order.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
/* One 8.3 part into width bytes, blank padded; '*' fills the rest with '?'. */
static const char *part(const char *s, char *out, unsigned width)
{
    unsigned i = 0;
    memset(out, ' ', width);
    for (; *s && *s != '.'; ++s) {
        if (*s == '*') {
            while (i < width) {
                out[i++] = '?';
            }
            continue;
        }
        if (i < width) {
            out[i++] = (char)toupper((unsigned char)*s);
        }
    }
    return *s == '.' ? s + 1 : s;
}
int xeen_dos_match(const char *pattern, const char *name)
{
    char p[11], n[11];
    unsigned i;
    if (!pattern || !name) {
        return 0;
    }
    (void)part(part(pattern, p, 8), p + 8, 3);
    (void)part(part(name, n, 8), n + 8, 3);
    for (i = 0; i < 11u; ++i) {
        if (p[i] != '?' && p[i] != n[i]) {
            return 0;
        }
    }
    return 1;
}
/**
 * Reads the header of a save file (its name and the party's names, or the full roster when FULL is
 * set) into OUT; returns 0 for a file that is not a save.
 */
int xeen_save_header_read(const uint8_t *f, size_t n, int full, xeen_save_header *out)
{
    size_t at;
    unsigned i, count;
    const uint8_t *roster, *party;
    if (!f || !out || n < 2u) {
        return 0;
    }
    /* The file's directory: a count word, then 8-byte entries; the save's name follows it. */
    at = (size_t)xeen_get_le16(f) * 8u + 2u;
    memset(out, 0, sizeof *out);
    if (at + XEEN_GAME_NAME_BYTES > n) {
        return 0;
    }
    memcpy(out->name, f + at, XEEN_GAME_NAME_BYTES);
    if (!full) {
        return 1;
    }
    if (at + XEEN_GAME_NAME_BYTES + XEEN_ROSTER_BYTES + XEEN_DARK_PARTY_BYTES > n) {
        return 0;
    }
    roster = f + at + XEEN_GAME_NAME_BYTES;
    party = roster + XEEN_ROSTER_BYTES; /* the maze.pty block from XEEN_DS_PARTY_FILE */
    out->preference = party[XEEN_PARTY_FILE_OFFSET(XEEN_DS_GAME_PREFERENCE)];
    count = party[XEEN_PARTY_FILE_OFFSET(XEEN_DS_PARTY_COUNT)];
    for (i = 0; i < count && i < 0x5f8u; ++i) {
        /* 19A95: roster + id*162h, unsigned id; a record past the roster
         * buffer is refused. */
        const uint8_t *ch;
        unsigned level;
        if ((size_t)party[XEEN_PARTY_FILE_OFFSET(XEEN_DS_PARTY_MEMBERS) + i] * XEEN_CHARACTER_BYTES +
                XEEN_CHARACTER_BYTES >
            XEEN_ROSTER_BYTES) {
            return 0;
        }
        ch = roster + (size_t)party[XEEN_PARTY_FILE_OFFSET(XEEN_DS_PARTY_MEMBERS) + i] * XEEN_CHARACTER_BYTES;
        level = xeen_character_current_level(ch);
        if (ch[0] && out->level < level) {
            out->level = (uint8_t)level;
        }
    }
    return 1;
}
unsigned xeen_resident_table(xeen_movement *m, uint16_t base, uint16_t segment, const uint8_t *r,
                             size_t n, unsigned limit)
{
    unsigned counter = 1;
    size_t i, size = xeen_read16(m, XEEN_DS_LOADED_FILE_SIZE);
    xeen_write16(m, base, 0);
    xeen_write16(m, (uint16_t)(base + 2u), segment);
    for (i = 0; i < size && i < n; ++i) {
        if (!r[i] && counter < limit) {
            xeen_write16(m, (uint16_t)(base + 4u * counter), (uint16_t)(i + 1u));
            xeen_write16(m, (uint16_t)(base + 4u * counter + 2u), segment);
            ++counter;
        }
    }
    return counter;
}
/* The C runtime's days of the year before MONTH (0..12), from M's DS (8BF2). */
static unsigned days_before(const xeen_movement *m, unsigned month)
{
    return xeen_read16(m, (uint16_t)(XEEN_DS_RTL_MONTH_CUMULATIVE_DAYS + 2u * month));
}
/* 15308 __isDST(hour, yday, month 0, year - 1970): the month of yday from
 * the 8BF2 cumulative table in M's DS (one day less from day 59 of a leap year);
 * April from its first Sunday at 2:00 (the last Sunday before 1987), to
 * October's last Sunday at 1:00. */
static int dos_dst(const xeen_movement *m, unsigned hour, unsigned yday, unsigned year)
{
    unsigned month = 0, day = yday, sunday, epoch_day;
    if (yday >= 59u && !((year + 70u) & 3u)) {
        --day;
    }
    while (month < 12u && days_before(m, month) <= day) {
        ++month;
    }
    if (month < 4u || month > 10u) {
        return 0;
    }
    if (month != 4u && month != 10u) {
        return 1;
    }
    /* The last day of the switch's week (April's 7th from 1987, else the month's last), then back to
     * its Sunday: EPOCH_DAY counts days from 1970 plus 4 (1 January 1970 was a Thursday), so its
     * remainder by 7 is the weekday, 0 a Sunday. */
    sunday = year > 16u && month == 4u ? days_before(m, month - 1u) + 7u : days_before(m, month);
    if ((year + 1970u) & 3u) {
        --sunday;
    }
    epoch_day = (uint16_t)(365u * year) + ((year + 1u) >> 2) + sunday + 4u;
    sunday -= (uint16_t)epoch_day % 7u;
    if (month == 4u) {
        return yday > sunday || (yday == sunday && hour >= 2u);
    }
    return yday < sunday || (yday == sunday && hour <= 1u);
}
/**
 * Sets the C runtime's time-zone variables as the original's tzset leaves them without TZ: EST/EDT,
 * 18000 seconds west, daylight saving on.
 */
static void tz_set(xeen_movement *m)
{
    xeen_write16(m, XEEN_DS_CRT_DAYLIGHT, 1);
    xeen_write16(m, XEEN_DS_CRT_TIMEZONE + 2u, 0);
    xeen_write16(m, XEEN_DS_CRT_TIMEZONE, 0x4650);
    memcpy(m->ds + XEEN_DS_CRT_TZNAME_STD, "EST", 4);
    memcpy(m->ds + XEEN_DS_CRT_TZNAME_DST, "EDT", 4);
}
uint32_t xeen_dos_time(xeen_movement *m, const xeen_dos_clock *c)
{
    int16_t year = (int16_t)(c->year - 1980u), hours;
    unsigned days = 0, i;
    uint32_t x;
    tz_set(m); /* 1511A: no TZ in the environment */
    x = xeen_ds_read32(m, XEEN_DS_CRT_TIMEZONE) + 315532800u; /* 1 January 1980 in seconds since 1970 */
    x += (uint32_t)(int32_t)(year >> 2) * 126230400u + (uint32_t)(year & 3) * 31536000u;
    if (year & 3) {
        x += 86400u;
    }
    for (i = c->month ? c->month - 1u : 0u; i > 0u;) {
        days += (unsigned)(int8_t)m->ds[XEEN_DS_RTL_DOS_MONTH_DAYS + --i]; /* 14E68 [si+8BE6], cbw */
    }
    days += c->day - 1u;
    if (c->month > 2u && !(c->year & 3u)) {
        ++days;
    }
    hours = (int16_t)(days * 24u + c->hour);
    if (xeen_read16(m, XEEN_DS_CRT_DAYLIGHT) && dos_dst(m, c->hour, days, (uint16_t)(c->year - 1970u))) {
        --hours;
    }
    x += (uint32_t)(int32_t)hours * 3600u;
    x += (uint32_t)c->minute * 60u + c->second;
    return x;
}
/* The C runtime's struct tm (nine 16-bit ints), as comtime (131AE) fills its static copy at XEEN_DS_CRT_TM and asctime
 * (133A5) reads it. */
typedef struct crt_tm {
    int16_t sec, min, hour, mday, mon, year, wday, yday, isdst; /* year from 1900; mon and yday from 0; wday 0 Sunday */
} crt_tm;
enum {
    FOUR_YEAR_HOURS = 35064,   /* 1461 days */
    FOUR_YEAR_DAYS = 1461,
    COMMON_YEAR_HOURS = 8760,  /* 365 days */
    LEAP_YEAR_HOURS = 8784,    /* 366 days */
    LEAP_DAY = 60              /* 29 February, one-based in a leap year */
};
/* The month and day of DAY, the one-based day of YEAR (from 1900), by the C runtime's month table. In a leap year day
 * 60 is 29 February and every later day is taken one back, onto the common year's table. */
static void crt_month_day(const uint8_t *ds, uint16_t year, int32_t day, crt_tm *tm)
{
    int leap = !(year & 3u);
    uint16_t mon;
    if (leap && day == LEAP_DAY) {
        tm->mon = 1;
        tm->mday = 29;
        return;
    }
    if (leap && day > LEAP_DAY) {
        --day;
    }
    for (mon = 0; (int32_t)(int8_t)ds[XEEN_DS_RTL_MONTH_DAYS + mon] < day; ++mon) {
        day -= (int8_t)ds[XEEN_DS_RTL_MONTH_DAYS + mon];
    }
    tm->mon = (int16_t)mon;
    tm->mday = (int16_t)(uint16_t)day;
}
/* comtime(local, 1): the broken-down time of LOCAL seconds since 1970 (a negative time is 1970's start). The year and
 * the day count of the whole four-year cycles are 16-bit, as the original's; every year divisible by four is a leap
 * year. With DAYLIGHT set and the US rule (dos_dst) in force the hour moves on by one, which can carry into the next
 * day. Original: 131AE (signed long division 10748/10757; 15308 the daylight test). */
static void crt_comtime(xeen_movement *m, int32_t local, crt_tm *tm)
{
    int32_t hours, cycles;
    uint16_t year, days;
    if (local < 0) {
        local = 0;
    }
    tm->sec = (int16_t)(local % 60);
    tm->min = (int16_t)(local / 60 % 60);
    hours = local / 3600;
    cycles = hours / FOUR_YEAR_HOURS;
    year = (uint16_t)((uint16_t)cycles * 4u + 70u);
    days = (uint16_t)((uint16_t)cycles * FOUR_YEAR_DAYS);
    hours %= FOUR_YEAR_HOURS;
    for (;;) {
        uint16_t year_hours = (year & 3u) ? COMMON_YEAR_HOURS : LEAP_YEAR_HOURS;
        if (hours < (int32_t)year_hours) {
            break;
        }
        days = (uint16_t)(days + year_hours / 24u);
        ++year;
        hours -= year_hours;
    }
    tm->year = (int16_t)year;
    /* HOURS now counts from the year's start */
    tm->isdst = (int16_t)(xeen_read16(m, XEEN_DS_CRT_DAYLIGHT) &&
                          dos_dst(m, (unsigned)(hours % 24), (unsigned)(hours / 24), (unsigned)(uint16_t)(year - 70u)));
    if (tm->isdst) {
        ++hours;
    }
    tm->hour = (int16_t)(hours % 24);
    tm->yday = (int16_t)(uint16_t)(hours / 24);
    /* 1 January 1970 was a Thursday (4) */
    tm->wday = (int16_t)((uint16_t)(days + (uint16_t)tm->yday + 4u) % 7u);
    crt_month_day(m->ds, year, hours / 24 + 1, tm);
}
/* The static struct tm's nine words in DS, in the structure's order. */
static void store_tm(xeen_movement *m, const crt_tm *tm)
{
    const int16_t fields[9] = {tm->sec, tm->min, tm->hour, tm->mday, tm->mon, tm->year, tm->wday, tm->yday, tm->isdst};
    unsigned i;
    for (i = 0; i < 9u; ++i) {
        xeen_write16(m, (uint16_t)(XEEN_DS_CRT_TM + 2u * i), (uint16_t)fields[i]);
    }
}
/* asctime(TM): "Www Mmm dd hh:mm:ss yyyy" and a newline into the runtime's static text, whose DS offset it returns.
 * The day and month names come from the runtime's far-pointer tables (offset words, the segment DS). The text of any
 * 32-bit time is 24 characters; longer output (a corrupted table) is cut at 63. Original: 13447 (133A5's sprintf
 * with DS:8BAF). */
static uint16_t crt_asctime(xeen_movement *m, const crt_tm *tm)
{
    uint8_t *ds = m->ds;
    const char *day = (const char *)ds + xeen_read16(m, (uint16_t)(XEEN_DS_CRT_DAY_NAMES + 4u * (uint16_t)tm->wday));
    const char *month = (const char *)ds + xeen_read16(m, (uint16_t)(XEEN_DS_CRT_MONTH_NAMES + 4u * (uint16_t)tm->mon));
    char text[64];
    int n = snprintf(text, sizeof text, (const char *)ds + XEEN_TEXT_DOS_CTIME_FORMAT, day, month, (int)tm->mday,
                     (int)tm->hour, (int)tm->min, (int)tm->sec, (int)(int16_t)(uint16_t)((uint16_t)tm->year + 1900u));
    if (n < 0) {
        n = 0;
    }
    if (n > (int)sizeof text - 1) {
        n = (int)sizeof text - 1;
    }
    memcpy(ds + XEEN_DS_CRT_ASCTIME_TEXT, text, (size_t)n);
    ds[XEEN_DS_CRT_ASCTIME_TEXT + (unsigned)n] = (uint8_t)10u;
    ds[XEEN_DS_CRT_ASCTIME_TEXT + 1u + (unsigned)n] = 0;
    return XEEN_DS_CRT_ASCTIME_TEXT;
}
uint16_t xeen_dos_ctime(xeen_movement *m, uint32_t t)
{
    crt_tm tm;
    tz_set(m); /* localtime's tzset */
    crt_comtime(m, (int32_t)(t - xeen_ds_read32(m, XEEN_DS_CRT_TIMEZONE)), &tm);
    store_tm(m, &tm);
    return crt_asctime(m, &tm);
}
