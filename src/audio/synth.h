/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Dispatch messages, rendering and close operations through the common XeenSynth interface.
 * Public synth and MT-32 wrappers use the private operations table populated by mt32 or
 * sc55_loader.
 */
#ifndef XEEN_SYNTH_H
#define XEEN_SYNTH_H
#include "mt32.h"
/* Load the optional SC-55 C ABI and open its engine with the borrowed ROM directory. Return
 * owned common synth state for synth_close, or NULL on unavailable support, library, ABI, ROM
 * or allocation failure. The streaming worker must stop before teardown unloads the library.
 */
XeenSynth *synth_open_sc55(const char *directory);
/* synth_open_sc55 reporting the boot's progress to PROGRESS (may be NULL) on the opening thread: 0 once the library
 * is known to report it (its optional sc55_open_progress), then the firmware warm-up's per mille up to 1000. An
 * older library opens without any call. */
XeenSynth *synth_open_sc55_progress(const char *directory, void (*progress)(void *user, unsigned permille),
                                    void *user);
/* Open the optional dual-YM3812 renderer; no ROM directory is needed. */
XeenSynth *synth_open_sbpro(void);
/* Deliver borrowed MIDI bytes through the common synth operations table. Null synth state is
 * ignored; backend-specific validation and diagnostics handle bad messages. The caller
 * serializes delivery with rendering and close.
 */
void synth_message(XeenSynth *, const uint8_t *, size_t);
/* Fill caller-owned interleaved stereo S16 frames through the common synth backend. A null
 * synth produces silence and a null destination is ignored. Serialize calls with messages and
 * close; backend failures follow that backend's silence and diagnostic rules.
 */
void synth_render(XeenSynth *, int16_t *, size_t);
/* Close the owned backend and free its common wrapper; null is ignored. Detach and join any
 * streaming worker before this call. No input byte buffer or external ROM file is owned or
 * modified here.
 */
void synth_close(XeenSynth *);
#endif
