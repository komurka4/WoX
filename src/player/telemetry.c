/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Record bounded main-thread and audio-callback diagnostics in separate buffers. The
 * xeen_trace entry points use a supplied clock and write only at close; platform and runtime
 * submit events after opening telemetry explicitly.
 */
#include "telemetry.h"
#include "audio/file_io.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
/* Bound both streams together. Count overflow explicitly instead of wrapping
 * over earlier diagnostics. The audio callback performs no allocation, I/O
 * or mutex operations.
 */
#define MAIN_CAP 131072u
#define AUDIO_CAP 65536u

typedef struct {
    const char *kind;
    uint64_t stamp;
    uint64_t value;
    uint64_t a;
    uint64_t b;
    char label[96];
} TraceRow;

static TraceRow *main_rows;
static TraceRow *audio_rows;
static size_t main_count;
static size_t audio_count;
static uint64_t main_dropped;
static uint64_t audio_dropped;
static uint64_t cue_serial;
static char path[4096];
static uint64_t (*trace_clock)(void);

/* Set the borrowed clock callback used by diagnostics. Call during setup before producers run;
 * a null callback restores the default behavior. The callback must remain valid while tracing
 * uses it.
 */
void xeen_trace_set_clock(uint64_t (*clock_fn)(void))
{
    trace_clock = clock_fn;
}

/* Return the current diagnostic timestamp, or zero when tracing is disabled. Calls the
 * configured clock without allocation or file output.
 */
uint64_t xeen_trace_now(void)
{
    return main_rows && trace_clock ? trace_clock() : 0;
}

/* Report whether optional telemetry buffers are active. Reads process-wide setup state without
 * allocation or file output.
 */
int xeen_trace_enabled(void)
{
    return main_rows != NULL;
}

/* Initialize optional process-wide bounded telemetry from the environment. Owns allocated
 * main-thread and audio-thread buffers until close. Missing or invalid configuration leaves
 * tracing disabled; allocation failure also emits a diagnostic. The output file is opened only
 * at close.
 */
void xeen_trace_open(void)
{
    const char *value = NULL;
    if (main_rows || !xeen_environment("XEEN_TELEMETRY", path, sizeof(path), &value) || !value ||
        !*value) {
        return;
    }
    if (value != path) {
        size_t n = strlen(value);
        if (n >= sizeof(path)) {
            return;
        }
        memcpy(path, value, n + 1);
    }
    main_rows = calloc(MAIN_CAP, sizeof(*main_rows));
    audio_rows = calloc(AUDIO_CAP, sizeof(*audio_rows));
    if (!main_rows || !audio_rows) {
        free(main_rows);
        free(audio_rows);
        main_rows = audio_rows = NULL;
        fprintf(stderr, "telemetry: buffer allocation failed\n");
        return;
    }
    main_count = audio_count = 0;
    main_dropped = audio_dropped = cue_serial = 0;
}

/* Fill a caller-owned diagnostic row, copying a bounded label and retaining the borrowed kind
 * pointer until telemetry close. Requires valid row storage, allocates nothing and has no
 * failure status.
 */
static void telemetry_set_row(TraceRow *row, const char *kind, uint64_t stamp, uint64_t value,
                              uint64_t a, uint64_t b, const char *label)
{
    row->kind = kind;
    row->stamp = stamp;
    row->value = value;
    row->a = a;
    row->b = b;
    if (label) {
        size_t n = strlen(label);
        if (n >= sizeof(row->label)) {
            n = sizeof(row->label) - 1;
        }
        memcpy(row->label, label, n);
        row->label[n] = 0;
    }
}

/* Append a record to the main-thread diagnostic buffer. The kind pointer must remain valid
 * until close; label text is copied with a fixed bound. Never allocates or performs file
 * output. A full buffer counts dropped records.
 */
void xeen_trace_main(const char *kind, uint64_t stamp, uint64_t value, uint64_t a, uint64_t b,
                     const char *label)
{
    if (!main_rows) {
        return;
    }
    if (main_count == MAIN_CAP) {
        main_dropped++;
        return;
    }
    telemetry_set_row(main_rows + main_count++, kind, stamp, value, a, b, label);
}

/* Append a record to the audio-thread diagnostic buffer. Requires the single audio producer
 * and a kind pointer valid until close; never allocates, locks or performs file output. A full
 * buffer counts dropped records.
 */
void xeen_trace_audio(const char *kind, uint64_t stamp, uint64_t value, uint64_t a, uint64_t b)
{
    if (!audio_rows) {
        return;
    }
    if (audio_count == AUDIO_CAP) {
        audio_dropped++;
        return;
    }
    telemetry_set_row(audio_rows + audio_count++, kind, stamp, value, a, b, NULL);
}

/* When tracing is enabled, advance the process-wide cue serial and append a main-thread cue
 * record with a copied name. Returns the serial for later audio correlation, or zero when
 * disabled; no file output occurs here.
 */
uint64_t xeen_trace_cue(uint64_t stamp, uint64_t source, uint64_t present, const char *name)
{
    if (!main_rows) {
        return 0;
    }
    xeen_trace_main("cue", stamp, source, ++cue_serial, present, name);
    return cue_serial;
}

/* Return the most recent process-wide cue serial for diagnostic correlation. Reads the shared
 * serial without allocation or file output.
 */
uint64_t xeen_trace_last_cue(void)
{
    return cue_serial;
}

/* Record a modal page and flush its diagnostic message to standard error. Borrows the page
 * name; call from the main thread. This is diagnostic output rather than playback
 * synchronization.
 */
void xeen_trace_modal(const char *name, unsigned page, uint64_t stamp)
{
    if (!main_rows) {
        return;
    }
    xeen_trace_main("modal", stamp, 0, page, 0, name);
    fprintf(stderr, "telemetry-modal: %s %u\n", name, page);
    fflush(stderr);
}

/* Write borrowed label text as a quoted CSV field, doubling embedded quotes. Borrows the
 * stream and label; output errors remain in the stream for close-time reporting.
 */
static void telemetry_write_quoted(FILE *file, const char *label)
{
    fputc('"', file);
    for (; *label; label++) {
        if (*label == '"') {
            fputc('"', file);
        }
        fputc(*label, file);
    }
    fputc('"', file);
}

/* Write borrowed diagnostic rows to the supplied stream in buffer order. Borrows the stream
 * and stream name and allocates nothing; output errors remain in the stream for close-time
 * reporting.
 */
static void telemetry_write_rows(FILE *file, const char *stream, const TraceRow *rows, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const TraceRow *r = rows + i;
        fprintf(file, "%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",", stream, r->kind,
                r->stamp, r->value, r->a, r->b);
        telemetry_write_quoted(file, r->label);
        fputc('\n', file);
    }
}

/* Write bounded telemetry and drop metadata, then release owned buffers. Call only after the
 * audio producer has stopped. Output errors are diagnostic and do not alter the player's
 * result; no producer may use the buffers afterward.
 */
void xeen_trace_close(void)
{
    FILE *file;
    if (!main_rows) {
        return;
    }
    file = xeen_fopen(path, "wb");
    if (file) {
        fputs("stream,kind,stamp,value,a,b,label\n", file);
        telemetry_write_rows(file, "main", main_rows, main_count);
        telemetry_write_rows(file, "audio", audio_rows, audio_count);
        fprintf(file,
                "meta,dropped,0,0,%" PRIu64 ",%" PRIu64
                ",\"main/audio rows; nonzero invalidates completeness\"\n",
                main_dropped, audio_dropped);
        fputs("meta,limitations,0,0,0,0,\"SDL callback API exposes no physical device latency or "
              "hardware underrun count; buffer duration is an estimate; present return excludes "
              "compositor scanout\"\n",
              file);
        if (fclose(file)) {
            fprintf(stderr, "telemetry: write failed\n");
        }
    } else {
        fprintf(stderr, "telemetry: cannot write diagnostic file\n");
    }
    free(main_rows);
    free(audio_rows);
    main_rows = audio_rows = NULL;
}
