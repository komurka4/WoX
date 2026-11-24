#ifndef XEEN_AUDIO_ENGINE_H
#define XEEN_AUDIO_ENGINE_H
#include "xeen/audio.h"
#include "xeen/resources.h"
#include "xeen/wox_config.h"
#include <stddef.h>
#include <stdint.h>
/* Native playback of the game's audio requests through the XEEN.EXE
 * player's audio modules (src/audio/): the original INTRO.CC music driver
 * the configured system names (xeen_music_system_driver; Phase 286 every
 * driver but the PC speaker's) interpreted by native sequencers, its VOC decoder, its 32 kHz
 * mixer gains. It is a sink: it never changes or reports back game state.
 *
 * Realtime mode: the game thread only queues requests (bounded, never waits);
 * a sequencer worker owns the music driver and advances it on its own
 * monotonic PIT clock, so music, effects and voices keep playing while the
 * game thread is blocked (for example by a window drag); SDL's audio callback
 * mixes the synth worker's PCM with the voice. Offline mode (tests): no
 * threads and no device; requests apply at once and xeen_audio_engine_render
 * advances a deterministic clock by the rendered frames. */
typedef enum {
    XEEN_AUDIO_REALTIME,
    XEEN_AUDIO_OFFLINE
} xeen_audio_mode;
typedef void (*xeen_audio_event_fn)(void *, uint64_t pit, const uint8_t *, size_t);
typedef struct {
    const xeen_wox_config *config;
    const xeen_archive *drivers; /* XEEN.CC (the VOC names' archive); no music driver is read from it */
    /* INTRO.CC: the music drivers promus, rolmus and canmus (XEEN.DAT loads its drivers from INTRO.CC: 15E12's
     * 17FFE("intro.cc", 4)). NULL: no music (no other driver or archive stands in). */
    const xeen_archive *intro_drivers;
    const xeen_archive *cc[2];   /* VOC names: DS 94 = 0 XEEN.CC, 1 DARK.CC */
    xeen_audio_mode mode;
    /* Offline only: every music/effect message the driver emits, in order. */
    xeen_audio_event_fn on_event;
    void *on_event_context;
    /* The directory of the CD rips (xeen02.ogg .. xeen60.ogg;
     * xeen_cd_track_file). NULL or "": CD voice requests fail and are counted. */
    const char *cd_dir;
    /* The configured digital driver's length trim (prosnd/specsnd
     * 0Ah, blastsnd 0Eh): voices play as that driver plays them (from offset
     * 20h, xeen_voc_driver_samples bytes, one per 68 us). 0: the XEEN.EXE
     * player's VOC decoder. */
    unsigned voc_trim;
    /* Phase 285A, the configured digital driver (XEEN.CFG's sound word): the driver's playback rate in Hz for
     * voc_trim's layout (0: the SB drivers' 14706; specsnd XEEN_VOC_PAS_RATE_HZ), and voc_none 1 for nullsnd
     * (XEEN.CFG sound 3 or 4: a VOC start plays nothing; the request is counted). */
    unsigned voc_rate_hz;
    unsigned voc_none;
    /* Phase 286: 1 for the Disney Sound Source's layout (source, XEEN.CFG sound 5): per driver interrupt the 8 bytes at
     * even offsets from its position, then 17 bytes on, about 6998 Hz (XEEN_VOC_SOURCE_*). */
    unsigned voc_source;
    /* Phase 287: the configured digital driver's model kind (audio.h XEEN_VOICE_MODEL_*; 0 none or unknown). For
     * ibmsnd, adsnd and source a voice marks the FM music driver's word 0Fh from its start to its end or stop
     * (music_voice: the effects leave channel 7 alone); the PC speaker's voice is not heard (voc_none). */
    unsigned voc_driver;
    /* Phase 286: the game's XEEN.CFG image (NULL: unknown, the genuine file's values assumed): its word 8D4C (offset
     * 14h) goes to the music driver's init (the SB Pro / SB16 mixer writes). */
    const uint8_t *xeen_cfg;
    size_t xeen_cfg_size;
    /* Phase 290: an SC-55 boot started by the host (NULL: the open boots one itself); borrowed. */
    struct xeen_audio_sc55_boot *sc55_boot;
} xeen_audio_engine_options;
typedef struct xeen_audio_engine xeen_audio_engine;
#define XEEN_AUDIO_RATE_HZ 32000u
/* Phase 290 (owner-approved host start-up, not original behaviour): the SC-55 engine (sc55, wave, tap10) booted on its
 * own worker thread, so that a window shows "Please wait, initializing SC-55..." meanwhile. The worker makes exactly
 * the call xeen_audio_engine_open makes otherwise (synth_open_sc55: the library, the ROM hashes, the 24,000,000-step
 * firmware warm-up) and touches nothing else; no message, render or close reaches that engine until it is joined.
 * start returns NULL for another system or when no thread could start (the open then boots as before). finished
 * never blocks (1 once the worker returned, or for NULL); succeeded joins and says whether an engine booted. An
 * engine open given the handle in its options takes the booted engine (nothing boots twice); close joins (waiting
 * for a boot still running: it cannot be interrupted) and closes an engine no open took. */
typedef struct xeen_audio_sc55_boot xeen_audio_sc55_boot;
int xeen_audio_sc55_backed(xeen_music_system);
xeen_audio_sc55_boot *xeen_audio_sc55_boot_start(const xeen_wox_config *);
int xeen_audio_sc55_boot_finished(xeen_audio_sc55_boot *);
int xeen_audio_sc55_boot_succeeded(xeen_audio_sc55_boot *);
void xeen_audio_sc55_boot_close(xeen_audio_sc55_boot *);
/* Owner-requested start-up bar (host presentation, not original behaviour): the running boot's firmware warm-up in
 * per mille (0..1000), or -1 while none is reported (the library not yet loaded, or one without the optional
 * sc55_open_progress, or NULL). Never blocks. */
int xeen_audio_sc55_boot_progress(xeen_audio_sc55_boot *);
/* CD voice (483C7) is a third source: one segment at a time (a new
 * play replaces it, 483B4 stops it), decoded from the OGG rips on the worker
 * into a bounded ring (CD_RING, 0.5 s), resampled to 32 kHz in the callback
 * and added at cd_volume percent (100 = unscaled; no original level exists).
 * It plays whatever music is (music_override = off silences only the music driver). */
/* Opens the configured backend. Music (and the driver's effects) need a
 * backend; when it cannot open (library, ROM, device) the engine still plays
 * voices and reports the reason (no other backend is substituted). Returns
 * NULL only when nothing can play (no output device in realtime mode,
 * allocation); diag explains either case. */
xeen_audio_engine *xeen_audio_engine_open(const xeen_audio_engine_options *, char *diag, size_t n);
const xeen_audio_sink *xeen_audio_engine_sink(xeen_audio_engine *);
/* 1 when the configured music backend opened (effects and songs play). */
int xeen_audio_engine_music_ready(const xeen_audio_engine *);
const char *xeen_audio_engine_music_error(const xeen_audio_engine *);
/* Phase 308: nonzero while the music driver's fade runs (entry 6 FFE0h), for the movie player; realtime counts a
 * queued or in-flight command as still fading. 0 without music. */
int xeen_audio_engine_music_fading(xeen_audio_engine *);
/* Offline: render interleaved stereo S16 frames (synth, voice, gains, clamp). */
void xeen_audio_engine_render(xeen_audio_engine *, int16_t *stereo, size_t frames);
typedef struct {
    unsigned long effects, voices, voice_errors, songs, controls, volumes, dropped;
    unsigned long voice_stops; /* 187E9(0, 0) stops applied */
    unsigned long callbacks, rendered_frames;
    int voice_active;
    /* CD plays and stops applied, failed plays and read errors,
     * decoded 44.1 kHz frames, ring underruns; a segment still sounding. */
    unsigned long cd_plays, cd_stops, cd_errors, cd_frames, cd_underruns;
    int cd_active;
    /* Segments that sounded to their last frame (a stop, a
     * replacement, an error or an underrun never counts). The transport state
     * is for diagnostics and tests: gameplay's status is the game's model
     * (xeen_native_audio cd_model), never this. */
    unsigned long cd_completed;
    /* boss-screen pauses begun; 1 while paused. */
    unsigned long pauses;
    int paused;
} xeen_audio_engine_stats;
void xeen_audio_engine_get_stats(xeen_audio_engine *, xeen_audio_engine_stats *);
/* The voice fields of O for the voice model KIND (an XEEN_VOICE_MODEL_*, xeen_speech_voice_model) with its driver's
 * length trim TRIM, and whether [audio] speech leaves voices AUDIBLE: adsnd plays L - 21h samples (trim 22h) at the
 * PAS's 14731 Hz, specsnd TRIM at the same rate, the Sound Source its own layout, every other model TRIM at the SB
 * drivers' rate; no voice for the none model or inaudible speech; voc_driver is KIND (Phase 287: the music driver's
 * word 0Fh for ibmsnd, adsnd and source). (The host's options, explore.c; Phase 332 moved them here to be tested.) */
void xeen_audio_engine_voice_options(xeen_audio_engine_options *o, unsigned kind, unsigned trim, int audible);
/* While STALLED, rendering does not decode,
 * as a late worker would; the ring underruns and the segment stays active. */
void xeen_audio_engine_cd_stall(xeen_audio_engine *, int stalled);
void xeen_audio_engine_close(xeen_audio_engine *);
/* Pause or resume all output (the boss screen). Any
 * thread. Music, voice and CD resume where they were; the configured
 * volumes, backend and device are untouched; nothing restarts. Offline:
 * while paused, render returns silence and advances no clock. */
void xeen_audio_engine_pause(xeen_audio_engine *, int paused);
/* The last CD error ("" when none). Read on the game thread after a
 * stats call; the text may lag the worker (diagnostics only). */
const char *xeen_audio_engine_cd_error(const xeen_audio_engine *);
/* The player's mixer constants (platform_sdl.c). */
#define XEEN_AUDIO_VOICE_GAIN_Q16  10868
#define XEEN_AUDIO_MASTER_GAIN_Q16 65536
/* One mixed output sample (exposed for the mixer tests): music S16 at its
 * user percent plus an already gain-scaled voice sample, master gain, clamp. */
int16_t xeen_audio_mix_sample(int music, unsigned music_percent, int voice);
/* Phase 285A (owner-approved, not original behaviour): the MT-32's music level, in percent of the other synths', so
 * that it sounds as loud as the SC-55: +8.2 dB, the mean RMS difference over the 41 shipped song entries of both
 * worlds (20 s each from their start, offline, the user's ROMs; xeen_dat_evidence/phase285A/c/levels_base_summary.txt).
 * The SB Pro was within 0.8 dB already; external MIDI keeps the device's level. */
#define XEEN_MT32_LEVEL_PERCENT 257u
#endif
