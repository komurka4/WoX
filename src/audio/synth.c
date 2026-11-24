/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Dispatch messages, rendering and close operations through the common XeenSynth interface.
 * Public synth and MT-32 wrappers use the private operations table populated by mt32 or
 * sc55_loader.
 */
#include "synth_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Deliver borrowed MIDI bytes through the common synth operations table. Null synth state is
 * ignored; backend-specific validation and diagnostics handle bad messages. The caller
 * serializes delivery with rendering and close.
 */
void synth_message(XeenSynth *synth, const uint8_t *bytes, size_t length_bytes)
{
    if (synth) {
        synth->message(synth->context, bytes, length_bytes);
    }
}

/* Fill caller-owned interleaved stereo S16 frames through the common synth backend. A null
 * synth produces silence and a null destination is ignored. Serialize calls with messages and
 * close; backend failures follow that backend's silence and diagnostic rules.
 */
void synth_render(XeenSynth *synth, int16_t *samples, size_t frames)
{
    if (!samples) {
        return;
    }
    if (synth) {
        synth->render(synth->context, samples, frames);
    } else {
        memset(samples, 0, frames * 2 * sizeof(*samples));
    }
}

/* Close the owned backend and free its common wrapper; null is ignored. Detach and join any
 * streaming worker before this call. No input byte buffer or external ROM file is owned or
 * modified here.
 */
void synth_close(XeenSynth *synth)
{
    if (synth) {
        synth->close(synth->context);
        free(synth);
    }
}



/* Release owned common synth state through the MT-32 API name; null is ignored. The caller
 * must detach and stop its streaming worker first. No status or allocation is returned.
 */
void mt32_close(XeenSynth *synth)
{
    synth_close(synth);
}
