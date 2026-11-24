/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Coordinate indexed frames, palettes, speech, music, clocks and input for scene scripts. The
 * cine entry points share one Cinematic state with player, scene modules and player_host
 * (Phase 308: the game's window, input and audio sink); optional diagnostics record that same
 * invocation.
 */
#ifndef XEEN_RUNTIME_H
#define XEEN_RUNTIME_H
#include "xeen.h"
#include "queen_selector.h"
#include "xeen/audio.h"
#include "cinematic_clock.h"
#include "approximate_clock.h"

/* Cinematic timestamps are free-running PIT-cycle units. Scene scripts use
 * these helpers to share clocks, media and platform services.
 */
typedef struct {
    uint8_t pixels[XEEN_PIXELS];
    uint8_t background[2][XEEN_PIXELS];
    uint8_t palette[768];
    uint8_t rgb[768];
    uint64_t now;
    uint64_t reset;
    uint64_t voice_end;
    const char *dump;
    FILE *times;
    FILE *events;
    int result;
    int opened;
    int subtitles;
    int aborted;
    const char *evidence_stop;
    QueenSelector selector;
    uint64_t clock_origin;
    uint64_t clock_start;
    unsigned last_track;
    int disc_known;
    const char *data;
    int fire_enabled;
    /* Phase 308: the game's audio sink (NULL: silent) and its music driver's fade query; the
     * music selector the song choices follow (14eb8), and CFG byte 26 (DS 4298) with its 1A52F
     * write (the Darkside intro's short form; owner decision D2). */
    const xeen_audio_sink *audio;
    int (*music_fading)(void *);
    void *music_user;
    unsigned music_selector;
    uint8_t cfg[30];
    int (*write_cfg)(void *, const uint8_t *, size_t);
    void *cfg_user;
    int cfg_written;
    int short_intro;
    /* Only Darkside/World endings opt in. One service epoch per invocation. */
    CinematicClock ending_clock;
    int ending_timeline;
    int approximate;
    int hash_only;
    int original_timer_quirks;
    uint16_t selector_accumulator;
    ApproximateClock pc_clock;
    uint64_t primary_reset_tick;
    uint64_t secondary_reset_tick;
    uint64_t middle_reset_tick;
    FILE *hashes;
    FILE *coverage;
    uint64_t presents;
    int palette_present;
} Cinematic;

/* Initialize approximate timing from borrowed date/time strings and a real or virtual clock
 * selection. Missing live values use the host calendar; virtual defaults use midnight on the
 * default date. Returns 1 after the initial read, or 0 on invalid input or clock failure.
 */
int cine_clock_start(Cinematic *cinematic, int virtual_time, const char *date, const char *time);
/* Perform one clock read and advance music to the resulting time. Borrows the cinematic; site
 * is a reserved diagnostic tag currently ignored. Returns 1 on success, or 0 and marks
 * playback failed on clock overflow.
 */
int cine_clock_read(Cinematic *cinematic, uint32_t site);
/* Synchronize the cinematic clock with the host and advance the active sequencer. Borrows the
 * initialized cinematic; virtual time is not debited here. Updates cached time and music state
 * without returning a status.
 */
void cine_sync(Cinematic *cinematic);
/* Read packed hour/minute and second/hundredth words into caller outputs. The accumulator
 * input is ignored; callers retain their selector accumulator separately. Borrows the
 * cinematic, advances its clock, and returns 1 on success or 0 on clock failure.
 */
int cine_dos_time(Cinematic *cinematic, uint16_t previous_accumulator, uint16_t *time_word_high,
                  uint16_t *time_word_low);
/* Append a present index, site, kind and loop-state tuple to the optional coverage stream.
 * Borrows all inputs; no output is produced when coverage is disabled. Stream errors are
 * detected when the cinematic closes.
 */
void cine_coverage_state(Cinematic *cinematic, uint32_t site, const char *kind, unsigned a,
                         unsigned b);
/* Read the clock and save the secondary timer baseline in the borrowed cinematic. A failed
 * read marks playback failed and leaves the baseline unchanged.
 */
int cine_secondary_reset(Cinematic *cinematic);
/* Read the clock and save the middle timer baseline in the borrowed cinematic. A failed read
 * marks playback failed and leaves the baseline unchanged.
 */
int cine_middle_reset(Cinematic *cinematic);
/* Read elapsed ticks since the middle timer reset using timer-word wrap rules. Borrows and
 * advances the cinematic; returns zero and marks playback failed if the clock read fails.
 */
uint64_t cine_middle_elapsed(Cinematic *cinematic);
/* Read elapsed ticks since the secondary timer reset using timer-word wrap rules. Borrows and
 * advances the cinematic; returns zero and marks playback failed if the clock read fails.
 */
uint64_t cine_secondary_elapsed(Cinematic *cinematic);
/* Initialize the ending timer service at the current cinematic time. Borrows and updates the
 * cinematic; returns 0 for null state or an already initialized legacy timeline, otherwise the
 * clock initialization status. Approximate mode programs its divisor without setting the
 * legacy timeline flag.
 */
int cine_ending_timeline_start(Cinematic *cinematic);
/* Return cached BIOS ticks without performing a fresh clock read. Borrows initialized
 * cinematic state; approximate time wraps daily, while legacy ending time projects the
 * carry-clock advances.
 */
uint64_t cine_tick(Cinematic *cinematic);
/* Initialize caller-owned cinematic storage for live or headless playback. Borrows the dump
 * path, opens an owned timing stream when requested, and initializes the platform for live
 * use. Returns 1 on success or 0 on failure; call cine_close to release any attached partial
 * resources.
 */
int cine_open(Cinematic *cinematic, const char *dump);
/* Stop music workers and platform audio, release attached decoders and synths, close the
 * currently attached diagnostic streams, and close the live platform. Borrows initialized
 * cinematic storage and returns its final playback result; close errors mark failure.
 */
int cine_close(Cinematic *cinematic);
/* Stop owned media and diagnostic services while preserving the last live video frame.
 * Idempotent; cine_close must subsequently release the retained platform/window. */
int cine_close_services(Cinematic *cinematic);
/* Read an archive member into a caller-owned Blob, with optional tracing and an archive-aware
 * failure diagnostic. Borrows the cinematic, archive and name. Returns 1 on success or 0 on
 * read failure; the caller frees a successful Blob.
 */
int cine_load(CC *cc, const char *name, Blob *b);
/* Read the clock and set the primary timer baseline and cached reset time. Borrows the
 * cinematic; a failed read marks playback failed and leaves the baseline unchanged.
 */
void cine_reset(Cinematic *cinematic);
/* Read primary elapsed ticks and update the selector accumulator in approximate mode. Borrows
 * and advances the cinematic; returns the timer-word projection there or the legacy elapsed
 * count. Clock errors mark playback failed.
 */
uint64_t cine_elapsed(Cinematic *cinematic);
/* Read primary elapsed time and compare it with the requested tick threshold. Borrows and
 * advances the cinematic; approximate mode uses a signed timer-word comparison and legacy mode
 * uses the full elapsed count.
 */
int cine_elapsed_before(Cinematic *cinematic, unsigned ticks);
/* Wait for primary elapsed ticks while servicing the clock and music, optionally polling
 * input. Borrows and updates the cinematic and returns the wait status. Clock failures remain
 * recorded in its result.
 */
int cine_wait_ticks(Cinematic *cinematic, unsigned ticks, int poll);
/* Wait for a key or the requested hold interval while servicing playback. Borrows the
 * cinematic; headless mode waits only for the interval. Returns 1 on completion or 0 on global
 * abort or a failed underlying wait.
 */
int cine_wait_key_ticks(Cinematic *cinematic, unsigned ticks);
/* Wait for the requested interval while discarding ordinary keys and Escape. Borrows and
 * updates cinematic timing and music; returns 1 on completion or 0 on global abort or a failed
 * underlying wait.
 */
int cine_wait_fire_ticks(Cinematic *cinematic, unsigned ticks);
/* Wait until an absolute time, optionally polling input, while servicing music. Borrows and
 * updates the cinematic; headless legacy mode advances directly to the deadline. Returns 1
 * after waiting or 0 on a clock or input stop.
 */
int cine_wait_cycles(Cinematic *cinematic, uint64_t deadline, int poll);
/* Advance to the next nominal display frame while servicing the cinematic clock. Borrows and
 * updates playback state; live mode may wait on the host. Clock failures mark the cinematic
 * failed.
 */
int cine_retrace(Cinematic *cinematic);
/* Copy the selected saved image bank into the cinematic framebuffer. Borrows and updates the
 * cinematic; invalid bank numbers have no effect and no allocation occurs.
 */
void cine_restore(Cinematic *cinematic, unsigned bank);
/* Compose the framebuffer from the tail of the first saved bank and the head of the second.
 * Borrows and updates the cinematic; row displacement is clamped to the image height.
 */
void cine_restore_vertical(Cinematic *cinematic, unsigned rows);
/* Compose every framebuffer row from the right part of the first saved bank and the left part
 * of the second. Borrows and updates the cinematic; the split is clamped to the image width.
 */
void cine_restore_horizontal(Cinematic *cinematic, unsigned split);
/* Draw a borrowed sprite Blob into the cinematic framebuffer using its default position.
 * Returns 1 on success; decoder failure may leave partial pixels, marks playback failed and
 * emits a diagnostic.
 */
int cine_draw(Cinematic *cinematic, const Blob *b, unsigned frame);
/* Draw a borrowed sprite Blob at the requested position in the cinematic framebuffer. Returns
 * 1 on success; decoder failure may leave partial pixels, marks playback failed and emits a
 * diagnostic.
 */
int cine_draw_at(Cinematic *cinematic, const Blob *b, unsigned frame, int x, int y, unsigned scale);
/* Draw a borrowed sprite Blob with the requested effect into the cinematic framebuffer.
 * Returns 1 on success; decoder failure may leave partial pixels, marks playback failed and
 * emits a diagnostic.
 */
int cine_draw_effect(Cinematic *cinematic, const Blob *b, unsigned frame, int x, int y,
                     unsigned flags);
/* Hash and present the current image, writing requested headless dumps and timing records or
 * submitting live pixels. Borrows and updates the cinematic. Returns 1 on success or 0 on
 * failure; the present counter and partial outputs may already have advanced.
 */
int cine_present(Cinematic *cinematic, const char *file_stem, const char *csv_label);
/* Append a timing row and borrowed label to the optional timestamp stream. Borrows cinematic
 * state and allocates nothing; stream errors are detected at close.
 */
void cine_record(Cinematic *cinematic, const char *label);
/* Present palette steps from the requested fade level through the endpoint, servicing retrace
 * between steps. Borrows and updates the cinematic; returns 0 for a zero increment or failed
 * present, otherwise 1. Updates the transient palette-present marker around each present.
 */
int cine_fade(Cinematic *cinematic, int in, unsigned increment, const char *prefix);
/* Poll live input and update the borrowed cinematic result on Escape or global abort. Returns
 * 1 to continue, including headless mode, or 0 to stop. Escape skips playback; global abort
 * marks failure and records the abort.
 */
int cine_poll(Cinematic *cinematic);
/* Start a borrowed decoded voice and record its cue. Tracks busy duration from the full
 * decoded sample count; caption-only approximate mode ends it immediately. Live output copies
 * the samples; an output error emits a diagnostic without changing the playback result.
 */
void cine_voice(Cinematic *cinematic, const Voice *voice, const char *name);
/* Stop live voice output when attached and clear the modeled busy interval at the cached time.
 * Borrows and updates the cinematic without a fresh clock read.
 */
void cine_stop_voice(Cinematic *cinematic);
/* Synchronize the clock and report whether the modeled voice deadline is still ahead. Borrows
 * and updates the cinematic; this does not query the platform audio device.
 */
int cine_sound_busy(Cinematic *cinematic);
/* Record a named music cue and route it to the attached sequencer and system worker at cached
 * time. Borrows the cinematic and name, performs no clock read, and has no immediate failure
 * status.
 */
void cine_music(Cinematic *cinematic, unsigned command);
/* Read a temporary archive song and copy it to attached music services using the default mode.
 * Borrows the cinematic and name; inactive music or read failure has no effect. Temporary
 * archive bytes are freed before return.
 */
void cine_music_load(Cinematic *cinematic, CC *archive, const char *name);
/* Play the Clouds introduction from the logo and lake reveal through Crodo's narration and the
 * closing hands. Borrows the cinematic and archive, owns temporary scene assets, and frees
 * them before returning the cinematic result: completed, failed or skipped.
 */
int clouds_opening(Cinematic *cinematic, CC *archive);
/* Route a control value to attached music services at cached cinematic time. Borrows the
 * cinematic and performs no clock read or allocation.
 */
void cine_music_control(Cinematic *cinematic, unsigned control);
/* Service pending sequencer and worker fades in clock quanta until complete. Borrows and
 * updates the cinematic, ignores ordinary keys and Escape, and returns 0 on global abort or a
 * failed wait, otherwise 1.
 */
int cine_music_wait_fade(Cinematic *cinematic);
/* Route a volume value to attached music services at cached cinematic time. Borrows the
 * cinematic and performs no clock read or allocation.
 */
void cine_music_volume(Cinematic *cinematic, unsigned volume);
/* Play the Darkside introduction from the publisher logos and pyramid through the queen and
 * later dialogue. Borrows the cinematic and archive, releases temporary scene assets and
 * returns the cinematic result: completed, failed or skipped.
 */
int darkside_play(Cinematic *cinematic, CC *cc);
/* Phase 308: 1A515's configuration write (byte 26 set, 30 bytes through the host). */
void darkside_intro_write_cfg(Cinematic *cinematic);
#endif
