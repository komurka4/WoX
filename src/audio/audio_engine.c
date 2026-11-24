#include "xeen/audio_engine.h"
#include "audio/audio_archive.h"
#include "audio/cd_stream.h"
#include "audio/midi_output.h"
#include "audio/mt32.h"
#include "audio/music.h"
#include "audio/synth.h"
#include "audio/synth_stream.h"
#include "xeen/byte_order.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
/* The XEEN.DAT side of the reused XEEN.EXE audio modules. */


enum {
    C_EFFECT,
    C_VOICE,
    C_SONG,
    C_BUFFER,
    C_BUFFER_START,
    C_CONTROL,
    C_VOLUME,
    C_CD_PLAY,
    C_CD_STOP,
    C_VOICE_STOP,
    C_MUSIC_RESTART /* Phase 308 */
};
typedef struct {
    int kind;
    unsigned value;
    uint8_t *bytes;
    size_t size;
    unsigned track;
    int32_t from, to;
} command;
/* The CD channel's ring of decoded 44.1 kHz stereo frames (0.5 s). */
#define CD_RING 22050u
#define QUEUE   64u
#define PIT_HZ  UINT64_C(1193182)
struct xeen_audio_engine {
    /* Phase 308: the music driver's fade word as the worker last saw it, and the commands it has taken but not yet
     * applied (xeen_audio_engine_music_fading counts them, with the queued ones, as pending). */
    SDL_atomic_t fade_seen, inflight;
    xeen_audio_mode mode;
    xeen_wox_config config;
    CC drivers;
    CC intro_drivers; /* INTRO.CC (canmus); its archive NULL when absent */
    const xeen_archive *cc[2];
    xeen_audio_sink sink;
    xeen_audio_event_fn on_event;
    void *on_event_context;
    /* Music: one sequencer; one target (a software synth or a MIDI device). */
    Music *music;
    /* Phase 286: sbpro_family, the FM drivers (their mixer events reach the renderer); config_8d4c, XEEN.CFG's 8D4C
     * given to the driver's init. */
    int sbpro_family;
    unsigned config_8d4c;
    XeenSynth *synth;
    xeen_audio_sc55_boot *sc55_boot; /* Phase 290: borrowed from the options during the open only */
    SynthStream *stream;
    MidiOutput *midi;
    char music_error[256];
    Blob buffer; /* the F42A song (worker-owned) */
    /* The voice: the callback reads it; it is replaced under the device lock. */
    Voice voice;
    size_t voice_count;
    uint64_t voice_position;
    unsigned voice_percent, music_percent;
    unsigned voc_trim; /* 0 the player's decoder; else the XEEN.DAT driver's layout */
    unsigned voc_rate; /* the driver layout's rate (Hz) */
    unsigned voc_none; /* nullsnd: no voice plays */
    unsigned voc_source; /* Phase 286: the Sound Source's layout */
    unsigned voc_driver; /* Phase 287: the driver's model kind (xeen_voc_driver_marks_music, xeen_voc_driver_pit) */
    /* The CD channel. The worker (offline: render) owns the stream
     * and fills the ring; the callback consumes it under the device lock. */
    char cd_dir[1024], cd_error[1200];
    CdStream *cd;
    int16_t cd_ring[2u * CD_RING];
    unsigned cd_read, cd_fill, cd_rate, cd_phase, cd_percent;
    int cd_active, cd_end, cd_stalled, cd_broken; /* Broken = a read error ended it */
    /* Realtime. */
    SDL_AudioDeviceID device;
    SDL_Thread *worker;
    SDL_mutex *lock;
    SDL_atomic_t stop;
    command queue[QUEUE];
    unsigned head, count;
    uint64_t origin, frequency;
    /* Offline clock (PIT cycles; remainder in 32000ths). */
    uint64_t pit, pit_rest;
    /* The boss screen's pause. Realtime: the device stops (the
     * synth worker's ring simply fills), the sequencer's PIT clock stands at
     * the pause and the paused span is removed from it at the resume, so the
     * song continues where it was (no catch-up, no restart). External MIDI
     * gets All Notes Off from the worker (a held note is not resumed).
     * Offline: render returns silence and advances nothing. */
    SDL_atomic_t paused;
    SDL_SpinLock clock_lock;
    uint64_t paused_at;
    int notes_off;
    xeen_audio_engine_stats stats;
};

int16_t xeen_audio_mix_sample(int music, unsigned music_percent, int voice)
{
    /* platform_sdl.c mix_audio: music at its percent plus the voice, master Q16, clamp. */
    int sample = (int)((int64_t)music * music_percent / 100) + voice;
    sample = (int)((int64_t)sample * XEEN_AUDIO_MASTER_GAIN_Q16 / 65536);
    return (int16_t)(sample > 32767 ? 32767 : sample < -32768 ? -32768 : sample);
}
/* The device callback's lock: the worker and the API hold it while they change state the callback
 * reads (voice, CD ring). An offline engine has no device and needs none. */
static inline void engine_lock(xeen_audio_engine *e)
{
    if (e->device) {
        SDL_LockAudioDevice(e->device);
    }
}
static inline void engine_unlock(xeen_audio_engine *e)
{
    if (e->device) {
        SDL_UnlockAudioDevice(e->device);
    }
}
/* platform_sdl.c platform_next_voice_sample: linear interpolation of PCM8 at
 * the VOC rate onto 32 kHz, the Q16 reference gain, the user percent. */
static int next_voice(xeen_audio_engine *e)
{
    int sample = 0;
    size_t i = (size_t)(e->voice_position / XEEN_AUDIO_RATE_HZ);
    if (e->voice.samples && i < e->voice_count) {
        unsigned fraction = (unsigned)(e->voice_position % XEEN_AUDIO_RATE_HZ);
        int first = (int)e->voice.samples[i] - 128;
        int second = i + 1u < e->voice_count ? (int)e->voice.samples[i + 1u] - 128 : first;
        sample = first * 256 + ((second - first) * 256 * (int)fraction) / (int)XEEN_AUDIO_RATE_HZ;
        e->voice_position += e->voice.rate;
    }
    return (int)((int64_t)sample * XEEN_AUDIO_VOICE_GAIN_Q16 * e->voice_percent / (65536LL * 100));
}
/* The track has played to its end: it is no longer active, and counts as completed unless it broke. */
static void cd_finish(xeen_audio_engine *e)
{
    e->cd_active = 0;
    if (!e->cd_broken) {
        ++e->stats.cd_completed;
    }
}
/* Channel C (0 left, 1 right) between frames A and B at the stream's phase, scaled by its volume. */
static int cd_sample(const xeen_audio_engine *e, const int16_t *a, const int16_t *b, unsigned c)
{
    return (int)((int64_t)(a[c] + (int)((int64_t)(b[c] - a[c]) * e->cd_phase / XEEN_AUDIO_RATE_HZ)) * e->cd_percent /
                 100);
}
/* The CD channel's next 32 kHz frame: linear interpolation of the
 * ring's frames at the stream's rate, x cd_volume (100 = the unscaled CD PCM:
 * XEEN.DAT sets no CD mixer level and the player has no reference). An empty
 * ring before the stream's end is an underrun (silence, nothing skipped). */
static void next_cd(xeen_audio_engine *e, int *left, int *right)
{
    const int16_t *a, *b;
    *left = *right = 0;
    if (!e->cd_active) {
        return;
    }
    if (e->cd_fill < 2u) {
        if (!e->cd_end) {
            ++e->stats.cd_underruns;
            return;
        }
        if (!e->cd_fill) {
            cd_finish(e);
            return;
        }
        a = b = e->cd_ring + 2u * e->cd_read;
    } else {
        a = e->cd_ring + 2u * e->cd_read;
        b = e->cd_ring + 2u * ((e->cd_read + 1u) % CD_RING);
    }
    *left = cd_sample(e, a, b, 0u);
    *right = cd_sample(e, a, b, 1u);
    e->cd_phase += e->cd_rate;
    while (e->cd_phase >= XEEN_AUDIO_RATE_HZ && e->cd_fill) {
        e->cd_phase -= XEEN_AUDIO_RATE_HZ;
        e->cd_read = (e->cd_read + 1u) % CD_RING;
        --e->cd_fill;
    }
    if (!e->cd_fill && e->cd_end) {
        cd_finish(e);
    }
}
/**
 * Mixes FRAMES stereo frames of all audio sources into OUT: the music synthesizer (scaled by the
 * music volume, except for an external MIDI device), the effects, the voice and CD audio.
 */
static void mix_output(xeen_audio_engine *e, int16_t *out, size_t frames)
{
    /* The music percent scales the software synths only (the player's rule:
     * external MIDI volume belongs to the device). The MT-32 is raised by XEEN_MT32_LEVEL_PERCENT to the SC-55's
     * level (Phase 285A, owner-approved, not original: DOS has no reference level for the modules' analogue
     * outputs). */
    unsigned mp = e->config.system == XEEN_MUSIC_EXTERNAL ? 100u
                  : e->config.system == XEEN_MUSIC_MT32   ? e->music_percent * XEEN_MT32_LEVEL_PERCENT / 100u
                                                          : e->music_percent;
    for (size_t i = 0; i < frames; ++i) {
        int v = next_voice(e), l, r;
        next_cd(e, &l, &r);
        out[2 * i] = xeen_audio_mix_sample(out[2 * i], mp, v + l);
        out[2 * i + 1] = xeen_audio_mix_sample(out[2 * i + 1], mp, v + r);
    }
}
/**
 * SDL audio callback: fills the device's STREAM from the engine's output stream (silence without
 * one).
 */
static void device_callback(void *u, Uint8 *stream, int bytes)
{
    xeen_audio_engine *e = u;
    size_t frames = (size_t)bytes / 4u;
    synth_stream_read(e->stream, (int16_t *)(void *)stream, frames); /* silence without a stream */
    mix_output(e, (int16_t *)(void *)stream, frames);
    ++e->stats.callbacks;
    e->stats.rendered_frames += frames;
}
/**
 * Returns the engine's current time in PIT ticks: the offline clock, or the real time converted to
 * ticks in realtime mode.
 */
static uint64_t now_pit(xeen_audio_engine *e)
{
    uint64_t t;
    if (e->mode == XEEN_AUDIO_OFFLINE) {
        return e->pit;
    }
    SDL_AtomicLock(&e->clock_lock);
    t = (SDL_AtomicGet(&e->paused) ? e->paused_at : SDL_GetPerformanceCounter()) - e->origin;
    SDL_AtomicUnlock(&e->clock_lock);
    return t / e->frequency * PIT_HZ + (t % e->frequency) * PIT_HZ / e->frequency;
}
/* MusicEmit: the driver's messages to the synth (its worker, or directly
 * offline) or the MIDI device. */
static void emit(void *u, uint64_t pit, const uint8_t *b, size_t n)
{
    xeen_audio_engine *e = u;
    if (e->on_event) {
        e->on_event(e->on_event_context, pit, b, n);
    }
    /* Phase 286: an MPU command-port byte, or an SB mixer write on a MIDI path, is not MIDI: the synthesizer never
     * receives it. A UART driver's data-port bytes (wavemus, tapmus; their original 9Fh volume byte and tapmus's
     * stray DFh included) go to Nuked-SC55 unchanged: the emulated module's own MIDI receiver (its MCU's UART, fed
     * byte by byte) parses them, as the real module did. */
    if (n == 4 && (b[0] == MUSIC_EVENT_MPU_COMMAND || (b[0] == MUSIC_EVENT_MIXER && !e->sbpro_family))) {
        return;
    }
    if (e->midi) {
        (void)midi_output_send(e->midi, b, n);
    } else if (e->stream) {
        synth_stream_message(e->stream, b, n);
    } else if (e->synth) {
        synth_message(e->synth, b, n);
    }
}
/* Phase 286: what source (the Disney Sound Source) plays: per interrupt but the last (xeen_voc_driver_count) the 8
 * bytes at even offsets from its position (20h, then 17 bytes on each time), at 8 samples per 1364 PIT cycles. */
static int voc_source_decode(const uint8_t *bytes, size_t n, Voice *v)
{
    uint32_t irqs = xeen_voc_driver_count(bytes, n, XEEN_VOICE_MODEL_SOURCE, 0), count, k = 0;
    memset(v, 0, sizeof *v);
    if (irqs < 2u) {
        return 0;
    }
    if (irqs == UINT32_MAX) {
        irqs = 65536u / XEEN_VOC_SOURCE_STEP; /* 3855: the wrapping, never-ending voice: one pass of the 16-bit
                                               * position through the segment, 17 bytes an interrupt */
    }
    count = (irqs - 1u) * XEEN_VOC_SOURCE_PER_IRQ;
    if (!(v->samples = malloc(count))) {
        return 0;
    }
    for (uint32_t j = 0; j + 1u < irqs; ++j) {
        size_t at = XEEN_VOC_FIRST_SAMPLES + (size_t)j * XEEN_VOC_SOURCE_STEP;
        for (unsigned i = 0; i < XEEN_VOC_SOURCE_PER_IRQ; ++i, at += 2u) {
            v->samples[k++] = at < n ? bytes[at] : XEEN_VOC_SILENCE;
        }
    }
    v->count = count;
    v->rate = (unsigned)((XEEN_PIT_HZ * XEEN_VOC_SOURCE_PER_IRQ + XEEN_VOC_SOURCE_IRQ_PIT / 2u) / XEEN_VOC_SOURCE_IRQ_PIT);
    v->playback_count = count;
    return 1;
}
/* What XEEN.DAT's digital drivers play (blastsnd/prosnd/specsnd
 * entry 3, phase169/PREFLIGHT.md): the bytes from offset 20h, as many as
 * xeen_voc_driver_samples counts (the first block's 16-bit length at 1Bh
 * minus the driver's trim, plus one), at the fixed time constant BCh (one
 * sample per 68 microseconds). The file's signature, header size, block
 * type and own time constant are not read (sphinx13.voc's "BullShit Voice
 * File" plays). Samples past the file read as silence (80h). */
static int voc_driver_decode(const uint8_t *bytes, size_t n, unsigned trim, unsigned rate, Voice *v)
{
    uint32_t count = xeen_voc_driver_samples(bytes, n, trim);
    size_t i;
    memset(v, 0, sizeof *v);
    if (!count || !(v->samples = malloc(count))) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        v->samples[i] = XEEN_VOC_FIRST_SAMPLES + i < n ? bytes[XEEN_VOC_FIRST_SAMPLES + i]
                                                       : XEEN_VOC_SILENCE;
    }
    v->count = count;
    v->rate = rate ? rate : XEEN_VOC_DRIVER_RATE_HZ;
    v->playback_count = count;
    return 1;
}
/* One voice at a time: V (a decoded VOC, or none to stop) replaces the playing one under the device lock,
 * from its first sample (the player's single voice slot; the original's one digital channel); the old
 * voice's storage is freed after the lock. */
static void replace_voice(xeen_audio_engine *e, const Voice *v)
{
    Voice old;
    engine_lock(e);
    old = e->voice;
    e->voice = *v;
    e->voice_count = voice_playable_count(v);
    e->voice_position = 0;
    engine_unlock(e);
    voice_free(&old);
}
/** Starts voice BYTES (a VOC file of N bytes), replacing the voice that was playing: decoded as the configured digital
 * driver plays it (the Sound Source's layout, the SB / PAS drivers' trimmed bytes, else the player's VOC decoder). */
static void set_voice(xeen_audio_engine *e, const uint8_t *bytes, size_t n)
{
    Blob b = {(uint8_t *)(uintptr_t)bytes, n};
    Voice v;
    memset(&v, 0, sizeof v);
    if (e->voc_none) {
        ++e->stats.voices; /* nullsnd's entry 3 plays nothing (it answers AX = 0) */
        return;
    }
    if (e->voc_source ? !voc_source_decode(bytes, n, &v)
        : e->voc_trim ? !voc_driver_decode(bytes, n, e->voc_trim, e->voc_rate, &v)
                      : !voc_decode(&b, &v)) {
        ++e->stats.voice_errors;
        return;
    }
    ++e->stats.voices;
    replace_voice(e, &v);
}
/* The CD channel's lifecycle (the worker's, or offline the caller's
 * thread). The ring is reset under the device lock; decoding is outside it. */
static void cd_reset(xeen_audio_engine *e)
{
    CdStream *old = e->cd;
    e->cd = NULL;
    engine_lock(e);
    e->cd_read = e->cd_fill = e->cd_phase = 0;
    e->cd_active = 0;
    e->cd_end = 1;
    e->cd_broken = 0;
    engine_unlock(e);
    cd_stream_close(old);
}
/** Records a CD-audio failure WHY (counted, and reported once in realtime mode). */
static void cd_fail(xeen_audio_engine *e, const char *why)
{
    ++e->stats.cd_errors;
    snprintf(e->cd_error, sizeof e->cd_error, "%s", why);
    if (e->mode == XEEN_AUDIO_REALTIME) {
        fprintf(stderr, "audio: %s\n", why);
    }
}
/** Starts CD audio: TRACK of DISC from frame FROM to TO, opening its decoded stream. */
static void cd_play(xeen_audio_engine *e, unsigned disc, unsigned track, int32_t from, int32_t to)
{
    char why[512];
    CdStream *s;
    cd_reset(e);
    ++e->stats.cd_plays;
    if (!(s = cd_stream_open(e->cd_dir, disc, track, from, to, why, sizeof why))) {
        cd_fail(e, why);
        return;
    }
    e->cd = s;
    engine_lock(e);
    e->cd_rate = cd_stream_rate(s);
    e->cd_end = 0;
    e->cd_active = 1;
    engine_unlock(e);
}
/* The CD rips' decode: at most CD_CHUNK_FRAMES stereo frames at a time, and only when at least CD_MIN_ROOM frames of
 * the ring are free (a refill does not run for a few frames). */
#define CD_CHUNK_FRAMES 2048u
#define CD_MIN_ROOM     512u
/* The realtime worker's pass period (paused or not), in milliseconds. */
#define WORKER_PERIOD_MS 2
/* MIDI: the control change status (with the channel in its low nibble) and the All Notes Off controller. */
#define MIDI_CONTROL_CHANGE 0xb0u
#define MIDI_ALL_NOTES_OFF  123u
/** Decodes CD audio into the engine's ring buffer until it is full or the track ends. */
static void cd_refill(xeen_audio_engine *e)
{
    int16_t chunk[2u * CD_CHUNK_FRAMES];
    char why[512];
    while (e->cd) {
        unsigned room, start;
        size_t got;
        engine_lock(e);
        room = CD_RING - e->cd_fill;
        start = (e->cd_read + e->cd_fill) % CD_RING;
        engine_unlock(e);
        if (room < CD_MIN_ROOM) {
            return;
        }
        if (room > CD_CHUNK_FRAMES) {
            room = CD_CHUNK_FRAMES;
        }
        if (room > CD_RING - start) {
            room = CD_RING - start;
        }
        got = cd_stream_read(e->cd, chunk, room, why, sizeof why);
        if (!got) {
            if (*why) {
                cd_fail(e, why);
            }
            cd_stream_close(e->cd);
            e->cd = NULL;
            engine_lock(e);
            e->cd_end = 1;
            e->cd_broken = *why != 0;
            if (!e->cd_fill) {
                if (e->cd_active && !e->cd_broken) {
                    ++e->stats.cd_completed;
                }
                e->cd_active = 0;
            }
            engine_unlock(e);
            return;
        }
        e->stats.cd_frames += got;
        engine_lock(e);
        memcpy(e->cd_ring + 2u * start, chunk, got * 4u);
        e->cd_fill += (unsigned)got;
        engine_unlock(e);
    }
}
/**
 * Applies one queued audio command (effect, voice, song, control, volume, CD) at the current time.
 */
static void apply_command(xeen_audio_engine *e, command *c)
{
    uint64_t pit = now_pit(e);
    switch (c->kind) {
        case C_EFFECT:
            ++e->stats.effects;
            if (e->music) {
                music_command(e->music, c->value, pit);
            }
            break;
        case C_VOICE:
            /* Phase 287: ibmsnd/adsnd/source store the music driver's word 0Fh = 2 at the start (a new start first
             * stops the playing voice) and 0 at the interrupt that ends it. */
            if (e->music && xeen_voc_driver_marks_music(e->voc_driver)) {
                uint64_t length = xeen_voc_driver_pit(c->bytes, c->size, e->voc_driver, e->voc_trim);
                music_voice(e->music, pit, length > UINT64_MAX - pit ? UINT64_MAX : pit + length);
            }
            set_voice(e, c->bytes, c->size);
            break;
        case C_SONG: {
            Blob b = {c->bytes, c->size};
            ++e->stats.songs;
            if (e->music) {
                music_load(e->music, &b, pit);
            }
            break;
        }
        case C_BUFFER:
            free(e->buffer.data);
            e->buffer.data = c->bytes;
            e->buffer.size = c->size;
            c->bytes = NULL;
            break;
        case C_BUFFER_START:
            ++e->stats.songs;
            if (e->music && e->buffer.data) {
                music_load(e->music, &e->buffer, pit);
            }
            break;
        case C_CONTROL:
            ++e->stats.controls;
            if (e->music) {
                music_control(e->music, c->value, pit);
            }
            break;
        case C_CD_PLAY:
            cd_play(e, c->value, c->track, c->from, c->to);
            break;
        case C_CD_STOP:
            ++e->stats.cd_stops;
            cd_reset(e);
            break;
        case C_MUSIC_RESTART:
            /* Phase 308: entry 3 (music_close: notes, effect programs and the driver's shutdown messages), then
             * entry 0 of a fresh driver (music_open_driver), as XEEN.EXE's cleanup and XEEN.DAT's start; the synth,
             * its stream and the MIDI device stay open. */
            if (e->music) {
                music_close(e->music, pit);
                e->music = music_open_driver(&e->intro_drivers, xeen_music_system_driver(e->config.system),
                                             e->config_8d4c, emit, e, pit);
                if (!e->music) {
                    snprintf(e->music_error, sizeof e->music_error, "the music driver did not restart");
                }
            }
            break;
        case C_VOICE_STOP: {
            Voice none;
            if (e->music && xeen_voc_driver_marks_music(e->voc_driver)) {
                music_voice(e->music, pit, pit); /* entry 3(0): the end routine stores 0 */
            }
            ++e->stats.voice_stops;
            memset(&none, 0, sizeof none);
            replace_voice(e, &none);
            break;
        }
        case C_VOLUME: /* the only kind left: the default is never another one */
        default:
            ++e->stats.volumes;
            if (e->music && c->value <= 127u) {
                music_volume(e->music, c->value, pit);
            }
            break;
    }
    free(c->bytes);
    c->bytes = NULL;
}
/**
 * The audio worker thread: applies the queued commands in batches and keeps the CD buffer filled
 * until the engine stops.
 */
static int worker(void *u)
{
    xeen_audio_engine *e = u;
    while (!SDL_AtomicGet(&e->stop)) {
        command batch[QUEUE];
        unsigned n = 0;
        SDL_LockMutex(e->lock);
        while (e->count) {
            batch[n++] = e->queue[e->head];
            e->head = (e->head + 1u) % QUEUE;
            --e->count;
        }
        SDL_AtomicSet(&e->inflight, (int)n);
        SDL_UnlockMutex(e->lock);
        for (unsigned i = 0; i < n; ++i) {
            apply_command(e, &batch[i]);
        }
        SDL_AtomicSet(&e->fade_seen, e->music ? (int)music_fading(e->music) : 0);
        SDL_AtomicSet(&e->inflight, 0);
        if (SDL_AtomicGet(&e->paused)) {
            if (e->midi && !e->notes_off) { /* A device keeps sounding by itself */
                for (unsigned ch = 0; ch < 16u; ++ch) {
                    uint8_t m[3] = {(uint8_t)(MIDI_CONTROL_CHANGE | ch), MIDI_ALL_NOTES_OFF, 0};
                    (void)midi_output_send(e->midi, m, 3);
                }
                e->notes_off = 1;
            }
            SDL_Delay(WORKER_PERIOD_MS);
            continue;
        }
        e->notes_off = 0;
        if (e->music) {
            music_advance(e->music, now_pit(e));
            SDL_AtomicSet(&e->fade_seen, (int)music_fading(e->music));
        }
        cd_refill(e);
        SDL_Delay(WORKER_PERIOD_MS);
    }
    return 0;
}
/* The game thread's side: copy and queue (never waits on audio). */
/**
 * Hands command C to the worker: applied at once when there is none (offline: apply_command takes
 * its bytes), else queued when the queue has room (the worker takes them). Returns 0 when the queue
 * was full (the drop counted; C's bytes stay the caller's), 1 otherwise.
 */
static int hand_over(xeen_audio_engine *e, command *c)
{
    int queued = 0;
    if (e->mode == XEEN_AUDIO_OFFLINE) {
        apply_command(e, c);
        return 1;
    }
    SDL_LockMutex(e->lock);
    if (e->count < QUEUE) {
        e->queue[(e->head + e->count) % QUEUE] = *c;
        ++e->count;
        queued = 1;
    } else {
        ++e->stats.dropped;
    }
    SDL_UnlockMutex(e->lock);
    return queued;
}
/**
 * Queues an audio command for the worker (copying its BYTES), or applies it directly when there is
 * no worker; a dropped command's copy is freed.
 */
static void submit(xeen_audio_engine *e, int kind, unsigned value, const uint8_t *bytes, size_t n)
{
    command c = {kind, value, NULL, n, 0, 0, 0};
    if (bytes && n) {
        if (!(c.bytes = malloc(n))) {
            ++e->stats.dropped;
            return;
        }
        memcpy(c.bytes, bytes, n);
    }
    if (!hand_over(e, &c)) {
        free(c.bytes);
    }
}
/** Audio sink entry: play sound effect N. */
static void s_effect(void *u, uint16_t n)
{
    submit(u, C_EFFECT, n, NULL, 0);
}
/** Audio sink entry: play the voice in B (N bytes). */
static void s_voice(void *u, const uint8_t *b, size_t n)
{
    submit(u, C_VOICE, 0, b, n);
}
/** Audio sink entry: stop the voice. */
static void s_voice_stop(void *u)
{
    submit(u, C_VOICE_STOP, 0, NULL, 0);
}
/** Audio sink entry (Phase 308): the music driver's shutdown and fresh start between two programs. */
static void s_music_restart(void *u)
{
    submit(u, C_MUSIC_RESTART, 0, NULL, 0);
}
/** Audio sink entry: play voice NAME from archive CC (looked up in the engine's archives). */
static void s_voice_name(void *u, const char *name, unsigned cc)
{
    xeen_audio_engine *e = u;
    CC a = {e->cc[cc ? 1 : 0]};
    Blob b;
    if (cc_read(&a, name, &b)) {
        submit(e, C_VOICE, 0, b.data, b.size);
        free(b.data);
    } else {
        ++e->stats.voice_errors;
    }
}
/** Audio sink entry: load song B (N bytes). */
static void s_song(void *u, const uint8_t *b, size_t n)
{
    submit(u, C_SONG, 0, b, n);
}
/** Audio sink entry: load the buffered song B (N bytes). */
static void s_buffer(void *u, const uint8_t *b, size_t n)
{
    submit(u, C_BUFFER, 0, b, n);
}
/** Audio sink entry: start the buffered song. */
static void s_buffer_start(void *u)
{
    submit(u, C_BUFFER_START, 0, NULL, 0);
}
/** Audio sink entry: music driver control V. */
static void s_music(void *u, unsigned v)
{
    submit(u, C_CONTROL, v, NULL, 0);
}
/** Audio sink entry: music volume V. */
static void s_volume(void *u, unsigned v)
{
    submit(u, C_VOLUME, v, NULL, 0);
}
/** Hands a CD command (no bytes) to the worker. */
static void submit_cd(xeen_audio_engine *e, command c)
{
    (void)hand_over(e, &c);
}
/** Audio sink entry: play CD TRACK of DISC from frame FROM to TO. */
static void s_cd_play(void *u, unsigned disc, unsigned track, int32_t from, int32_t to)
{
    submit_cd(u, (command){C_CD_PLAY, disc, NULL, 0, track, from, to});
}
/** Audio sink entry: stop CD audio. */
static void s_cd_stop(void *u)
{
    submit_cd(u, (command){C_CD_STOP, 0, NULL, 0, 0, 0, 0});
}
void xeen_audio_engine_voice_options(xeen_audio_engine_options *o, unsigned kind, unsigned trim, int audible)
{
    o->voc_trim = kind == XEEN_VOICE_MODEL_ADLIB ? 0x22u : trim;
    o->voc_rate_hz = kind == XEEN_VOICE_MODEL_PAS || kind == XEEN_VOICE_MODEL_ADLIB ? XEEN_VOC_PAS_RATE_HZ : 0u;
    o->voc_source = kind == XEEN_VOICE_MODEL_SOURCE;
    o->voc_none = kind == XEEN_VOICE_MODEL_NONE || !audible;
    o->voc_driver = kind;
}

/** Appends a diagnostic line TEXT to the buffer D (N bytes), truncating when it is full. */
static void diag(char *d, size_t n, const char *text)
{
    if (d && n) {
        size_t k = strlen(d);
        if (k + 1u < n) {
            snprintf(d + k, n - k, "%s%s", k ? "; " : "", text);
        }
    }
}
/* Phase 290 (owner-approved host start-up, not original behaviour): the SC-55 engine's boot on a worker thread. */
struct xeen_audio_sc55_boot {
    SDL_Thread *thread;
    SDL_atomic_t finished; /* 1 once the worker has returned (its synth set first) */
    SDL_atomic_t progress; /* the warm-up's per mille, -1 while none is reported */
    XeenSynth *synth;      /* the worker's result; read only after the join */
    int joined;
    char roms[sizeof(((xeen_wox_config *)0)->sc55_roms)];
};
int xeen_audio_sc55_backed(xeen_music_system system)
{
    return system == XEEN_MUSIC_SC55 || system == XEEN_MUSIC_WAVE || system == XEEN_MUSIC_TAP10;
}
/* The boot's progress (per mille, at most 1000) for the host's loading message. */
static void sc55_boot_progress(void *u, unsigned permille)
{
    SDL_AtomicSet(&((xeen_audio_sc55_boot *)u)->progress, permille > 1000u ? 1000 : (int)permille);
}
/** The worker: the unchanged synth_open_sc55 (library, ROM hashes, the firmware warm-up); touches nothing else. */
static int sc55_boot_worker(void *u)
{
    xeen_audio_sc55_boot *b = u;
    b->synth = synth_open_sc55_progress(b->roms, sc55_boot_progress, b);
    SDL_AtomicSet(&b->finished, 1);
    return 0;
}
xeen_audio_sc55_boot *xeen_audio_sc55_boot_start(const xeen_wox_config *c)
{
    xeen_audio_sc55_boot *b;
    if (!c || !xeen_audio_sc55_backed(c->system) || !(b = calloc(1, sizeof *b))) {
        return NULL;
    }
    snprintf(b->roms, sizeof b->roms, "%s", c->sc55_roms);
    SDL_AtomicSet(&b->finished, 0);
    SDL_AtomicSet(&b->progress, -1);
    if (!(b->thread = SDL_CreateThread(sc55_boot_worker, "xeen-sc55-boot", b))) {
        free(b);
        return NULL;
    }
    return b;
}
int xeen_audio_sc55_boot_progress(xeen_audio_sc55_boot *b)
{
    return b ? SDL_AtomicGet(&b->progress) : -1;
}
int xeen_audio_sc55_boot_finished(xeen_audio_sc55_boot *b)
{
    return !b || SDL_AtomicGet(&b->finished) != 0;
}
/** Joins the worker once (waiting for the boot's end when it still runs). */
static void sc55_boot_join(xeen_audio_sc55_boot *b)
{
    if (b && !b->joined) {
        SDL_WaitThread(b->thread, NULL);
        b->thread = NULL;
        b->joined = 1;
    }
}
int xeen_audio_sc55_boot_succeeded(xeen_audio_sc55_boot *b)
{
    sc55_boot_join(b);
    return b && b->synth;
}
/** The booted synth, owned by the caller from now on (joins first); NULL when none or already taken. */
static XeenSynth *sc55_boot_take(xeen_audio_sc55_boot *b)
{
    XeenSynth *s;
    if (!b) {
        return NULL;
    }
    sc55_boot_join(b);
    s = b->synth;
    b->synth = NULL;
    return s;
}
void xeen_audio_sc55_boot_close(xeen_audio_sc55_boot *b)
{
    if (b) {
        sc55_boot_join(b);
        synth_close(b->synth); /* a synth no engine took (null is safe) */
        free(b);
    }
}
/**
 * Opens the configured music synthesizer (FM, the MT-32 or SC-55 emulation, or an external MIDI
 * device); returns 0 when music is off or the device cannot be opened.
 */
static int open_music(xeen_audio_engine *e)
{
    const xeen_wox_config *c = &e->config;
    const char *driver;
    char why[256];
    if (c->system == XEEN_MUSIC_OFF) {
        snprintf(e->music_error, sizeof e->music_error, "music off ([audio] music_override = off)");
        return 0;
    }
    if (!xeen_wox_config_check_roms(c, why, sizeof why)) {
        snprintf(e->music_error, sizeof e->music_error, "%s", why);
        return 0;
    }
    if (c->system == XEEN_MUSIC_EXTERNAL) {
        if (e->mode == XEEN_AUDIO_REALTIME &&
            !(e->midi = midi_output_open(*c->midi_device ? c->midi_device : NULL,
                                         *c->midi_port ? c->midi_port : NULL))) {
            snprintf(e->music_error, sizeof e->music_error,
                     "external MIDI output unavailable (see the midi diagnostic; [audio] "
                     "midi_device / midi_port)");
            return 0;
        }
    } else {
        /* Phase 290: an SC-55 the host's start-up worker booted (xeen_audio_sc55_boot_start: the same
         * synth_open_sc55 call, run while the window shows its loading message) is taken as it is. */
        e->synth = e->sc55_boot && xeen_audio_sc55_backed(c->system)
                           ? sc55_boot_take(e->sc55_boot) /* its result, failed or not: nothing boots twice */
                   : c->system == XEEN_MUSIC_MT32      ? mt32_open(c->mt32_roms)
                   : xeen_audio_sc55_backed(c->system) ? synth_open_sc55(c->sc55_roms)
                                                       : synth_open_sbpro();
        if (!e->synth) {
            snprintf(e->music_error, sizeof e->music_error,
                     "%s synthesizer unavailable (library or ROMs; see the diagnostic above)",
                     xeen_music_system_name(c->system));
            return 0;
        }
        if (e->mode == XEEN_AUDIO_REALTIME && !(e->stream = synth_stream_open(e->synth))) {
            snprintf(e->music_error, sizeof e->music_error, "synthesizer worker failed to start");
            return 0;
        }
    }
    /* The driver XEEN.CFG's music index selects in DOS (15EC9), every one from INTRO.CC (15E12: XEEN.DAT loads its
     * drivers from it; Phase 285 the SC-55, Phase 285A the others, Phase 286 the remaining drivers but the PC
     * speaker's). Without INTRO.CC, or with another build, there is no music and the error names the driver; no
     * other driver or archive stands in. */
    driver = xeen_music_system_driver(c->system);
    e->sbpro_family = c->system == XEEN_MUSIC_SBPRO || c->system == XEEN_MUSIC_ADLIB || c->system == XEEN_MUSIC_SB ||
                      c->system == XEEN_MUSIC_PAS;
    if (e->intro_drivers.archive) {
        e->music = music_open_driver(&e->intro_drivers, driver, e->config_8d4c, emit, e, now_pit(e));
    }
    if (!e->music) {
        snprintf(e->music_error, sizeof e->music_error, "INTRO.CC %s driver missing or of an unknown build", driver);
        return 0;
    }
    return 1;
}
/** Closes the music synthesizer and its output stream. */
static void close_music(xeen_audio_engine *e)
{
    if (e->music) {
        music_close(e->music, now_pit(e));
        e->music = NULL;
    }
    engine_lock(e);
    {
        SynthStream *s = e->stream;
        e->stream = NULL;
        engine_unlock(e);
        synth_stream_close(s, NULL);
    }
    synth_close(e->synth);
    e->synth = NULL;
    midi_output_close(e->midi);
    e->midi = NULL;
}
/* XEEN.CFG's offset of the word the game keeps at DS:8D4C (the music driver's init argument). */
#define XEEN_CFG_WORD_8D4C 0x14u
/**
 * Takes the CD rips' directory DIR (the options' cd_dir) into E, or records in E's CD error why the CD voice is
 * unavailable: no directory configured; a path too long for the engine, or one that is not a directory (both also
 * reported in D).
 */
static void open_cd_dir(xeen_audio_engine *e, const char *dir, char *d, size_t n)
{
    struct stat st;
    char t[1200];
    if (!dir || !*dir) {
        snprintf(e->cd_error, sizeof e->cd_error, "CD tracks unavailable (no directory; [files] cd_tracks_dir)");
        return;
    }
    if (snprintf(e->cd_dir, sizeof e->cd_dir, "%s", dir) >= (int)sizeof e->cd_dir) {
        e->cd_dir[0] = 0; /* too long: reported as not found below */
    }
    if (stat(e->cd_dir, &st) || !S_ISDIR(st.st_mode)) {
        snprintf(t, sizeof t, "CD tracks unavailable: directory not found: %s ([files] cd_tracks_dir)", dir);
        diag(d, n, t);
        snprintf(e->cd_error, sizeof e->cd_error, "%s", t);
        e->cd_dir[0] = 0;
    }
}
/**
 * The realtime output: SDL's audio subsystem, the device (S16 stereo at XEEN_AUDIO_RATE_HZ, 256-frame buffers, fed by
 * device_callback) and the engine's lock, and the performance clock's origin. Returns 0 with the diagnostic in D and
 * everything this opened released again (the device stays paused until the worker runs).
 */
static int open_device(xeen_audio_engine *e, char *d, size_t n)
{
    SDL_AudioSpec want, got;
    e->frequency = SDL_GetPerformanceFrequency();
    e->origin = SDL_GetPerformanceCounter();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        diag(d, n, "audio: SDL audio unavailable:");
        diag(d, n, SDL_GetError());
        return 0;
    }
    memset(&want, 0, sizeof want);
    want.freq = (int)XEEN_AUDIO_RATE_HZ;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = device_callback;
    want.userdata = e;
    if (!(e->device = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0))) {
        diag(d, n, "audio: cannot open the output device:");
        diag(d, n, SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    if (!(e->lock = SDL_CreateMutex())) {
        diag(d, n, "audio: mutex");
        SDL_CloseAudioDevice(e->device);
        e->device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    return 1;
}
xeen_audio_engine *xeen_audio_engine_open(const xeen_audio_engine_options *o, char *d, size_t n)
{
    xeen_audio_engine *e;
    if (d && n) {
        d[0] = 0;
    }
    if (!o || !o->config) {
        diag(d, n, "audio: no configuration");
        return NULL;
    }
    if (!(e = calloc(1, sizeof *e))) {
        diag(d, n, "audio: allocation failed");
        return NULL;
    }
    e->mode = o->mode;
    e->sc55_boot = o->sc55_boot;
    e->config = *o->config;
    e->drivers.archive = o->drivers;
    e->intro_drivers.archive = o->intro_drivers;
    e->cc[0] = o->cc[0];
    e->cc[1] = o->cc[1];
    e->voice_percent = o->config->voice_volume;
    e->music_percent = o->config->music_volume;
    e->on_event = o->mode == XEEN_AUDIO_OFFLINE ? o->on_event : NULL;
    e->on_event_context = o->on_event_context;
    e->sink = (xeen_audio_sink){.effect = s_effect,
                                .voice = s_voice,
                                .voice_name = s_voice_name,
                                .song = s_song,
                                .song_buffer = s_buffer,
                                .song_buffer_start = s_buffer_start,
                                .music = s_music,
                                .volume = s_volume,
                                .cd_play = s_cd_play,
                                .cd_stop = s_cd_stop,
                                .context = e,
                                .voice_stop = s_voice_stop,
                                .music_restart = s_music_restart};
    /* XEEN.CFG's word 8D4C when the file is known, else the genuine file's 1. */
    e->config_8d4c = o->xeen_cfg && o->xeen_cfg_size >= XEEN_CFG_WORD_8D4C + 2u
                             ? xeen_get_le16(o->xeen_cfg + XEEN_CFG_WORD_8D4C)
                             : 1u;
    e->voc_trim = o->voc_trim;
    e->voc_rate = o->voc_rate_hz;
    e->voc_none = o->voc_none;
    e->voc_source = o->voc_source;
    e->voc_driver = o->voc_driver;
    e->cd_percent = o->config->cd_volume;
    e->cd_end = 1;
    open_cd_dir(e, o->cd_dir, d, n);
    if (e->mode == XEEN_AUDIO_REALTIME && !open_device(e, d, n)) {
        free(e);
        return NULL;
    }
    if (!open_music(e)) {
        char t[300];
        close_music(e);
        snprintf(t, sizeof t, "music unavailable: %s", e->music_error);
        diag(d, n, t);
    }
    e->sc55_boot = NULL; /* the caller closes it (and any synth not taken) */
    if (e->mode == XEEN_AUDIO_REALTIME) {
        SDL_AtomicSet(&e->stop, 0);
        if (!(e->worker = SDL_CreateThread(worker, "xeen-audio", e))) {
            diag(d, n, "audio: worker thread failed");
            xeen_audio_engine_close(e);
            return NULL;
        }
        SDL_PauseAudioDevice(e->device, 0);
    }
    return e;
}
const xeen_audio_sink *xeen_audio_engine_sink(xeen_audio_engine *e)
{
    return e ? &e->sink : NULL;
}
int xeen_audio_engine_music_ready(const xeen_audio_engine *e)
{
    return e && e->music;
}
const char *xeen_audio_engine_cd_error(const xeen_audio_engine *e)
{
    return e ? e->cd_error : "no engine";
}
/* Phase 308: the music driver's fade word for the movie player (entry 6 FFE0h, the World ending's waits 1cee4 and
 * 1e9ad): nonzero while a fade runs. Realtime: a command still queued or being applied counts as a fade not yet
 * finished (a fade just requested must not read as already over). Offline: the sequencer's state. */
int xeen_audio_engine_music_fading(xeen_audio_engine *e)
{
    int pending;
    if (!e || !e->music) {
        return 0;
    }
    if (e->mode == XEEN_AUDIO_OFFLINE) {
        return music_fading(e->music) != 0;
    }
    SDL_LockMutex(e->lock);
    pending = e->count != 0 || SDL_AtomicGet(&e->inflight) != 0;
    SDL_UnlockMutex(e->lock);
    return pending || SDL_AtomicGet(&e->fade_seen) != 0;
}
const char *xeen_audio_engine_music_error(const xeen_audio_engine *e)
{
    if (!e) {
        return "no engine";
    }
    if (e->music && *music_error(e->music)) {
        return music_error(e->music);
    }
    return e->music_error;
}
void xeen_audio_engine_pause(xeen_audio_engine *e, int paused)
{
    if (!e || (SDL_AtomicGet(&e->paused) != 0) == (paused != 0)) {
        return;
    }
    if (paused) {
        ++e->stats.pauses;
    }
    if (e->mode == XEEN_AUDIO_OFFLINE) {
        SDL_AtomicSet(&e->paused, paused != 0);
        return;
    }
    SDL_AtomicLock(&e->clock_lock);
    if (paused) {
        e->paused_at = SDL_GetPerformanceCounter();
        SDL_AtomicSet(&e->paused, 1);
    } else {
        e->origin += SDL_GetPerformanceCounter() - e->paused_at;
        SDL_AtomicSet(&e->paused, 0);
    }
    SDL_AtomicUnlock(&e->clock_lock);
    if (e->device) {
        SDL_PauseAudioDevice(e->device, paused != 0);
    }
}
void xeen_audio_engine_render(xeen_audio_engine *e, int16_t *out, size_t frames)
{
    if (!e || !out || e->mode != XEEN_AUDIO_OFFLINE) {
        return;
    }
    if (SDL_AtomicGet(&e->paused)) {
        memset(out, 0, frames * 4u);
        return;
    }
    /* Advance the deterministic PIT clock and render in steps of at most 64
     * frames (2 ms, the realtime worker's cadence). one advance
     * for the whole call delivered a short effect's key-on and key-off
     * before any sample was synthesized (a silent cue offline). */
    for (size_t at = 0; at < frames;) {
        size_t k = frames - at < 64u ? frames - at : 64u;
        e->pit_rest += (uint64_t)k * PIT_HZ;
        e->pit += e->pit_rest / XEEN_AUDIO_RATE_HZ;
        e->pit_rest %= XEEN_AUDIO_RATE_HZ;
        if (e->music) {
            music_advance(e->music, e->pit);
        }
        if (e->synth) {
            synth_render(e->synth, out + 2u * at, k);
        } else {
            memset(out + 2u * at, 0, k * 4u);
        }
        at += k;
    }
    /* The CD ring in chunks a ring can feed (offline decoding). */
    for (size_t at = 0; at < frames;) {
        size_t k = frames - at < 4096u ? frames - at : 4096u;
        if (!e->cd_stalled) {
            cd_refill(e);
        }
        mix_output(e, out + 2u * at, k);
        at += k;
    }
    e->stats.rendered_frames += frames;
}
void xeen_audio_engine_cd_stall(xeen_audio_engine *e, int stalled)
{
    if (e && e->mode == XEEN_AUDIO_OFFLINE) {
        e->cd_stalled = stalled != 0;
    }
}
void xeen_audio_engine_get_stats(xeen_audio_engine *e, xeen_audio_engine_stats *s)
{
    if (!e || !s) {
        return;
    }
    engine_lock(e);
    *s = e->stats;
    s->voice_active = e->voice.samples && e->voice_position / XEEN_AUDIO_RATE_HZ < e->voice_count;
    s->cd_active = e->cd_active;
    s->paused = SDL_AtomicGet(&e->paused) != 0;
    engine_unlock(e);
}
void xeen_audio_engine_close(xeen_audio_engine *e)
{
    if (!e) {
        return;
    }
    if (e->worker) {
        SDL_AtomicSet(&e->stop, 1);
        SDL_WaitThread(e->worker, NULL);
        e->worker = NULL;
    }
    /* Requests queued after the last worker pass are dropped (shutdown). */
    if (e->lock) {
        while (e->count) {
            free(e->queue[e->head].bytes);
            e->head = (e->head + 1u) % QUEUE;
            --e->count;
        }
    }
    close_music(e);
    if (e->device) {
        SDL_CloseAudioDevice(e->device);
        e->device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    if (e->lock) {
        SDL_DestroyMutex(e->lock);
    }
    cd_stream_close(e->cd);
    voice_free(&e->voice);
    free(e->buffer.data);
    free(e);
}
