/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Load the optional Munt service, validate supplied ROMs and expose MT-32 synthesis through
 * XeenSynth. mt32_open creates state for synth and synth_stream; message delivery, rendering
 * and teardown share one serialized owner.
 */
#ifndef XEEN_MT32_H
#define XEEN_MT32_H
#include <stddef.h>
#include <stdint.h>

/* All calls, including message delivery, belong to one synchronized thread.
 * Rendered samples are signed 16-bit interleaved stereo at exactly 32000 Hz.
 * No library/ROM availability is required for a silent build.
 */
typedef struct XeenSynth XeenSynth;
/* Open the optional Munt backend with the approved user-supplied ROM pair; a null directory
 * uses the default ROM folder. Return an owned common synth for synth_close or mt32_close, or
 * NULL on missing support, library, ROM or allocation failure. Serialize all later operations
 * and stop streaming before close.
 */
XeenSynth *mt32_open(const char *directory);
/* Release owned common synth state through the MT-32 API name; null is ignored. The caller
 * must detach and stop its streaming worker first. No status or allocation is returned.
 */
void mt32_close(XeenSynth *s);
#endif
