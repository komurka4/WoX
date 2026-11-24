/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Translate native paths to the player's UTF-8 boundary. xeen_fopen supports archive, config
 * and synth modules with read-only case-tolerant asset lookup (Windows: through wide paths).
 */
#ifndef XEEN_FILE_IO_H
#define XEEN_FILE_IO_H
#include <stdio.h>
#include <stddef.h>
/* Paths use UTF-8. Read lookup tolerates ASCII filename case and separators. */
/* Open the borrowed UTF-8 path and mode through the native file API, normalizing separators
 * and retrying read-mode asset lookup with ASCII case matching. Return a caller-owned FILE to
 * close, or NULL on conversion, allocation or open failure. Component lookup never renames
 * files.
 */
FILE *xeen_fopen(const char *path, const char *mode);
/* Return an owned UTF-8 path beside the executable for the borrowed plain filename.
 * Free the result; NULL indicates invalid input, lookup or allocation failure. */
char *xeen_application_path(const char *filename);
/* Phase 308: environment value NAME as UTF-8 (*VALUE NULL when unset; Windows converts into BUFFER). 0: too long
 * or not convertible. */
int xeen_environment(const char *name, char *buffer, size_t capacity, const char **value);
#endif
