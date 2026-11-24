/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Record bounded main-thread and audio-callback diagnostics in separate buffers. The
 * xeen_trace entry points use a supplied clock and write only at close; platform and runtime
 * submit events after opening telemetry explicitly.
 */
#ifndef XEEN_TELEMETRY_H
#define XEEN_TELEMETRY_H
#include <stdint.h>
/* Initialize optional process-wide bounded telemetry from the environment. Owns allocated
 * main-thread and audio-thread buffers until close. Missing or invalid configuration leaves
 * tracing disabled; allocation failure also emits a diagnostic. The output file is opened only
 * at close.
 */
void xeen_trace_open(void);
/* Set the borrowed clock callback used by diagnostics. Call during setup before producers run;
 * a null callback restores the default behavior. The callback must remain valid while tracing
 * uses it.
 */
void xeen_trace_set_clock(uint64_t (*clock_fn)(void));
/* Return the current diagnostic timestamp, or zero when tracing is disabled. Calls the
 * configured clock without allocation or file output.
 */
uint64_t xeen_trace_now(void);
/* Write bounded telemetry and drop metadata, then release owned buffers. Call only after the
 * audio producer has stopped. Output errors are diagnostic and do not alter the player's
 * result; no producer may use the buffers afterward.
 */
void xeen_trace_close(void);
/* Report whether optional telemetry buffers are active. Reads process-wide setup state without
 * allocation or file output.
 */
int xeen_trace_enabled(void);
/* Append a record to the main-thread diagnostic buffer. The kind pointer must remain valid
 * until close; label text is copied with a fixed bound. Never allocates or performs file
 * output. A full buffer counts dropped records.
 */
void xeen_trace_main(const char *kind, uint64_t stamp, uint64_t value, uint64_t a, uint64_t b,
                     const char *label);
/* Append a record to the audio-thread diagnostic buffer. Requires the single audio producer
 * and a kind pointer valid until close; never allocates, locks or performs file output. A full
 * buffer counts dropped records.
 */
void xeen_trace_audio(const char *kind, uint64_t stamp, uint64_t value, uint64_t a, uint64_t b);
/* When tracing is enabled, advance the process-wide cue serial and append a main-thread cue
 * record with a copied name. Returns the serial for later audio correlation, or zero when
 * disabled; no file output occurs here.
 */
uint64_t xeen_trace_cue(uint64_t stamp, uint64_t source, uint64_t present, const char *name);
/* Return the most recent process-wide cue serial for diagnostic correlation. Reads the shared
 * serial without allocation or file output.
 */
uint64_t xeen_trace_last_cue(void);
/* Record a modal page and flush its diagnostic message to standard error. Borrows the page
 * name; call from the main thread. This is diagnostic output rather than playback
 * synchronization.
 */
void xeen_trace_modal(const char *name, unsigned page, uint64_t stamp);
#endif
