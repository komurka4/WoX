/* SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef XEEN_PLAYER_HOST_H
#define XEEN_PLAYER_HOST_H
/* Phase 308: the services one movie plays with (player_host.c serves xeen.h's platform_* and
 * cc_open/cc_close from them). NULL unbinds. */
#include "xeen.h"
#include "xeen/movie.h"
void player_host_bind(const xeen_movie_services *services);
/* One diagnostic line (printf format) to the bound log, else stderr. */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void player_log(const char *format, ...);
#endif
