/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Transport queued MIDI to a synth worker and published stereo PCM to the audio callback.
 * synth_stream_open and close control worker lifetime; read never calls the synth or takes its
 * queue mutex.
 */
#include "synth_stream.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Render ahead by 128 ms. Spare ring capacity distinguishes full from empty
 * without a callback lock. MIDI arriving during rendering applies to the next
 * generated block, while the sequencer retains its event order and timestamps.
 */
enum {
    SYNTH_PCM_CAPACITY_FRAMES = 8192,
    SYNTH_PCM_TARGET_FRAMES = 4096,
    SYNTH_RENDER_BLOCK_FRAMES = 256,
    SYNTH_MIDI_CAPACITY_BYTES = 131072,
    SYNTH_PREFILL_TIMEOUT_MS = 5000 /* a host limit (not original): the stream gives up when its worker cannot prefill */
};

struct SynthStream {
    XeenSynth *synth;
    SDL_Thread *thread;
    SDL_mutex *mutex;
    SDL_atomic_t read;
    SDL_atomic_t write;
    SDL_atomic_t stop;
    SDL_atomic_t underflows;
    SDL_atomic_t missing;
    SDL_atomic_t overflows;
    int16_t pcm[SYNTH_PCM_CAPACITY_FRAMES * 2];
    uint8_t midi[SYNTH_MIDI_CAPACITY_BYTES];
    size_t midi_count;
    uint64_t rendered;
    uint64_t render_ticks;
    uint64_t max_render_ticks;
};

/** Returns how many frames the stream's ring buffer holds. */
static int synth_stream_queued_frames(SynthStream *stream)
{
    return (SDL_AtomicGet(&stream->write) - SDL_AtomicGet(&stream->read) +
            SYNTH_PCM_CAPACITY_FRAMES) %
           SYNTH_PCM_CAPACITY_FRAMES;
}

/* Consume copied MIDI on the synth-owner thread and publish fixed PCM blocks through atomic
 * ring indices. Snapshot queued messages and stop together so final messages drain before
 * exit. Context and synth are borrowed from the stream; return zero after shutdown.
 */
static int synth_stream_render_worker(void *context)
{
    SynthStream *stream = context;
    uint8_t messages[SYNTH_MIDI_CAPACITY_BYTES];
    int16_t block[SYNTH_RENDER_BLOCK_FRAMES * 2];
    for (;;) {
        size_t count = 0;
        size_t pos = 0;
        int write;
        int stopping;
        uint64_t start;
        uint64_t elapsed;
        SDL_LockMutex(stream->mutex);
        count = stream->midi_count;
        memcpy(messages, stream->midi, count);
        stream->midi_count = 0;
        stopping = SDL_AtomicGet(&stream->stop);
        SDL_UnlockMutex(stream->mutex);
        while (pos < count) {
            uint32_t length;
            memcpy(&length, messages + pos, sizeof(length));
            pos += sizeof(length);
            synth_message(stream->synth, messages + pos, length);
            pos += length;
        }
        /* Sample stop with the queue snapshot. Sampling it after delivery
         * could abandon final messages enqueued during that delivery.
         */
        if (stopping) {
            break;
        }
        if (synth_stream_queued_frames(stream) >= SYNTH_PCM_TARGET_FRAMES) {
            SDL_Delay(1);
            continue;
        }
        start = SDL_GetPerformanceCounter();
        synth_render(stream->synth, block, SYNTH_RENDER_BLOCK_FRAMES);
        elapsed = SDL_GetPerformanceCounter() - start;
        stream->render_ticks += elapsed;
        if (elapsed > stream->max_render_ticks) {
            stream->max_render_ticks = elapsed;
        }
        write = SDL_AtomicGet(&stream->write);
        /* BLOCK divides capacity, so the producer never wraps inside a block. */
        memcpy(stream->pcm + 2 * write, block, sizeof(block));
        SDL_AtomicSet(&stream->write,
                      (write + SYNTH_RENDER_BLOCK_FRAMES) % SYNTH_PCM_CAPACITY_FRAMES);
        stream->rendered += SYNTH_RENDER_BLOCK_FRAMES;
    }
    return 0;
}

/* Start a worker for the borrowed synth and prefill its bounded PCM ring before returning
 * owned stream state. Return NULL on invalid synth, allocation, worker or prefill-timeout
 * failure. The caller keeps the synth alive until synth_stream_close returns.
 */
SynthStream *synth_stream_open(XeenSynth *synth)
{
    SynthStream *stream;
    uint32_t start;
    if (!synth) {
        return NULL;
    }
    stream = calloc(1, sizeof(*stream));
    if (!stream) {
        return NULL;
    }
    stream->synth = synth;
    stream->mutex = SDL_CreateMutex();
    if (stream->mutex) {
        stream->thread = SDL_CreateThread(synth_stream_render_worker, "xeen-synth", stream);
    }
    if (!stream->thread) {
        synth_stream_close(stream, NULL);
        return NULL;
    }
    /* Attach only after prefill; startup cannot count as a device underflow. */
    start = SDL_GetTicks();
    while (synth_stream_queued_frames(stream) < SYNTH_PCM_TARGET_FRAMES) {
        if (SDL_GetTicks() - start > SYNTH_PREFILL_TIMEOUT_MS) {
            synth_stream_close(stream, NULL);
            return NULL;
        }
        SDL_Delay(1);
    }
    return stream;
}

/* Copy borrowed MIDI bytes into the bounded worker queue. Null, empty or stopped inputs are
 * ignored; overflow counts a diagnostic and stops the stream. The caller can reuse its message
 * storage immediately after return.
 */
void synth_stream_message(SynthStream *stream, const uint8_t *bytes, size_t length_bytes)
{
    uint32_t length;
    if (!stream || !bytes || !length_bytes || SDL_AtomicGet(&stream->stop)) {
        return;
    }
    SDL_LockMutex(stream->mutex);
    if (length_bytes > SYNTH_MIDI_CAPACITY_BYTES - sizeof(length) ||
        length_bytes + sizeof(length) > SYNTH_MIDI_CAPACITY_BYTES - stream->midi_count) {
        if (!SDL_AtomicAdd(&stream->overflows, 1)) {
            fputs("Music stopped: synthesizer MIDI queue full\n", stderr);
        }
        SDL_AtomicSet(&stream->stop, 1);
    } else {
        length = (uint32_t)length_bytes;
        memcpy(stream->midi + stream->midi_count, &length, sizeof(length));
        stream->midi_count += sizeof(length);
        memcpy(stream->midi + stream->midi_count, bytes, length_bytes);
        stream->midi_count += length_bytes;
    }
    SDL_UnlockMutex(stream->mutex);
}

/* Copy published stereo S16 PCM into the caller-owned callback buffer without locking or
 * calling the synth. Zero-fill missing frames and count underflow; a null stream yields
 * silence and null output is ignored. Only one callback consumer may read each stream.
 */
void synth_stream_read(SynthStream *stream, int16_t *samples, size_t frames)
{
    size_t available;
    size_t first;
    size_t take;
    int read;
    if (!samples) {
        return;
    }
    if (!stream) {
        memset(samples, 0, frames * 2 * sizeof(*samples));
        return;
    }
    read = SDL_AtomicGet(&stream->read);
    available = (size_t)synth_stream_queued_frames(stream);
    take = frames < available ? frames : available;
    first = take < SYNTH_PCM_CAPACITY_FRAMES - (size_t)read
                    ? take
                    : SYNTH_PCM_CAPACITY_FRAMES - (size_t)read;
    memcpy(samples, stream->pcm + 2 * read, first * 2 * sizeof(*samples));
    if (take > first) {
        memcpy(samples + first * 2, stream->pcm, (take - first) * 2 * sizeof(*samples));
    }
    SDL_AtomicSet(&stream->read, (read + (int)take) % SYNTH_PCM_CAPACITY_FRAMES);
    if (take < frames) {
        memset(samples + take * 2, 0, (frames - take) * 2 * sizeof(*samples));
        SDL_AtomicAdd(&stream->underflows, 1);
        SDL_AtomicAdd(&stream->missing, (int)(frames - take));
    }
}

/* Signal and join the owned stream worker, optionally copy its counters into caller-owned
 * stats, then free stream storage. Null is ignored. Detach the audio callback first; borrowed
 * synth ownership stays with the caller.
 */
void synth_stream_close(SynthStream *stream, SynthStreamStats *stats)
{
    if (!stream) {
        return;
    }
    SDL_AtomicSet(&stream->stop, 1);
    if (stream->thread) {
        SDL_WaitThread(stream->thread, NULL);
    }
    if (stats) {
        stats->rendered_frames = stream->rendered;
        stats->render_ticks = stream->render_ticks;
        stats->max_render_ticks = stream->max_render_ticks;
        stats->underflows = (unsigned)SDL_AtomicGet(&stream->underflows);
        stats->missing_frames = (unsigned)SDL_AtomicGet(&stream->missing);
        stats->queue_overflows = (unsigned)SDL_AtomicGet(&stream->overflows);
    }
    if (stream->mutex) {
        SDL_DestroyMutex(stream->mutex);
    }
    free(stream);
}
