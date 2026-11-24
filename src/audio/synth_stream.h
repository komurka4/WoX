/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Transport queued MIDI to a synth worker and published stereo PCM to the audio callback.
 * synth_stream_open and close control worker lifetime; read never calls the synth or takes its
 * queue mutex.
 */
#ifndef XEEN_SYNTH_STREAM_H
#define XEEN_SYNTH_STREAM_H
#include "synth.h"
#include <stdint.h>
/* Worker-owned synthesis, bounded PCM/MIDI transport. Synth ownership stays
 * with the caller, which must stop the stream before closing the synth.
 */
typedef struct SynthStream SynthStream;

typedef struct {
    uint64_t rendered_frames;
    uint64_t render_ticks;
    uint64_t max_render_ticks;
    unsigned underflows;
    unsigned missing_frames;
    unsigned queue_overflows;
} SynthStreamStats;

/* Start a worker for the borrowed synth and prefill its bounded PCM ring before returning
 * owned stream state. Return NULL on invalid synth, allocation, worker or prefill-timeout
 * failure. The caller keeps the synth alive until synth_stream_close returns.
 */
SynthStream *synth_stream_open(XeenSynth *);
/* Copy borrowed MIDI bytes into the bounded worker queue. Null, empty or stopped inputs are
 * ignored; overflow counts a diagnostic and stops the stream. The caller can reuse its message
 * storage immediately after return.
 */
void synth_stream_message(SynthStream *, const uint8_t *, size_t);
/* Copy published stereo S16 PCM into the caller-owned callback buffer without locking or
 * calling the synth. Zero-fill missing frames and count underflow; a null stream yields
 * silence and null output is ignored. Only one callback consumer may read each stream.
 */
void synth_stream_read(SynthStream *, int16_t *, size_t);
/* Signal and join the owned stream worker, optionally copy its counters into caller-owned
 * stats, then free stream storage. Null is ignored. Detach the audio callback first; borrowed
 * synth ownership stays with the caller.
 */
void synth_stream_close(SynthStream *, SynthStreamStats *);
#endif
