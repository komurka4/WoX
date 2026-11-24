/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Phase 308: the movie player's platform services over the game's window, input and archives
 * (xeen_movie_services, include/xeen/movie.h). It replaces the standalone player's SDL layer
 * (platform_sdl.c) and its file archive reader (cc.c): the same contracts, one owner (the game).
 * One movie plays at a time (xeen_movie_play is not reentrant), so the bound services are a
 * file-scope pointer, as the former platform's state was.
 */
#include "player_host.h"
#include "telemetry.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const xeen_movie_services *bound;
static uint64_t origin;

void player_host_bind(const xeen_movie_services *services)
{
    bound = services;
    origin = 0;
}

static int host_live(void)
{
    return bound && bound->host.now && bound->host.wait_until && bound->host.poll && bound->host.present;
}

int platform_open(void)
{
    if (!host_live()) {
        return 0;
    }
    origin = bound->host.now(bound->host.user);
    xeen_trace_set_clock(platform_cycles);
    xeen_trace_open();
    return 1;
}

void platform_close(void)
{
    xeen_trace_close();
}

uint64_t platform_cycles(void)
{
    return host_live() ? bound->host.now(bound->host.user) - origin : 0;
}

void platform_wait(uint64_t deadline)
{
    if (host_live()) {
        bound->host.wait_until(bound->host.user, origin + deadline);
    }
}

int platform_escape(void)
{
    return host_live() ? bound->host.poll(bound->host.user, 0) : 0;
}

int platform_pending_key(void)
{
    return host_live() ? bound->host.poll(bound->host.user, 1) : 0;
}

/* The player's RGB is palette_rgb's (s << 2 | s >> 4 of each 6-bit level s), so s = c >> 2 is
 * exact: the window receives the 6-bit DAC values the movie set. */
int platform_present(const uint8_t *pixels, const uint8_t *rgb)
{
    uint8_t dac[XEEN_PALETTE_BYTES];
    size_t i;
    if (!host_live()) {
        return 0;
    }
    for (i = 0; i < XEEN_PALETTE_BYTES; ++i) {
        dac[i] = (uint8_t)(rgb[i] >> 2);
    }
    return bound->host.present(bound->host.user, pixels, dac);
}

/* The last path component of PATH. */
static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return b;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        x = x >= 'a' && x <= 'z' ? x - 32 : x;
        y = y >= 'a' && y <= 'z' ? y - 32 : y;
        if (x != y) {
            return 0;
        }
    }
    return !*a && !*b;
}

int cc_open(CC *cc, const char *path)
{
    const char *name = path ? base_name(path) : "";
    const xeen_archive *a = NULL;
    cc->archive = NULL;
    if (!bound) {
        return 0;
    }
    if (same_name(name, "INTRO.CC")) {
        a = bound->intro;
    } else if (same_name(name, "XEEN.CC")) {
        a = bound->xeen;
    } else if (same_name(name, "DARK.CC")) {
        a = bound->dark;
    }
    cc->archive = a;
    return a != NULL;
}

void cc_close(CC *cc)
{
    if (cc) {
        cc->archive = NULL;
    }
}

void player_log(const char *format, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    if (bound && bound->log) {
        bound->log(bound->log_user, text);
    } else {
        fprintf(stderr, "%s\n", text);
    }
}
