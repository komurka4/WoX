/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Hold the optional backend ABI and its dynamic-loader reference. Callers retain the library
 * until every object created through its table has been destroyed. No game data is owned. */
#ifndef XEEN_SBPRO_LOADER_H
#define XEEN_SBPRO_LOADER_H
#include "sbpro.h"

typedef struct {
    void *handle;
    const SBProAPI *api;
} SBProLibrary;

/* Open beside the executable, or use the explicit XEEN_SBPRO_LIBRARY override. Returns NULL
 * after a diagnostic on unavailable support, load/ABI failure (a wrong version or size, or any
 * member missing) or allocation failure; the caller owns the result. */
SBProLibrary *sbpro_library_open(void);
/* Release a loader reference after its library-owned objects have stopped; null is safe. */
void sbpro_library_close(SBProLibrary *library);
#endif
