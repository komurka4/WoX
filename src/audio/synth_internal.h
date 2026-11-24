/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Define the private XeenSynth operations table and backend context. mt32 and sc55_loader
 * construct it; synth dispatches its message, render and close operations under the caller's
 * serialization.
 */
#ifndef XEEN_SYNTH_INTERNAL_H
#define XEEN_SYNTH_INTERNAL_H
#include "synth.h"

struct XeenSynth {
    void *context;
    void (*message)(void *, const uint8_t *, size_t);
    void (*render)(void *, int16_t *, size_t);
    void (*close)(void *);
};
#endif
