/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Send complete MIDI messages to a Windows or Linux system destination. midi_output_open, send
 * and close own the platform device; music_output serializes these calls without changing
 * instruments or event bytes.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "midi_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

/* The SysEx queue: MIDI_SYSEX_SLOTS messages of up to MIDI_SYSEX_BYTES each may be in the driver at once; a slot is
 * free again once the driver has finished with its header. */
#define MIDI_SYSEX_SLOTS 32u
#define MIDI_SYSEX_BYTES 65536u
typedef struct {
    MIDIHDR header;
    uint8_t bytes[MIDI_SYSEX_BYTES];
    int prepared;
} LongMessage;

struct MidiOutput {
    HMIDIOUT handle;
    LongMessage messages[MIDI_SYSEX_SLOTS];
    int failed;
};

/* Translate a WinMM result to success or a readable diagnostic for the borrowed operation
 * name. Return one for success, zero after reporting an error; allocate no retained storage.
 */
static int midi_report_windows_error(MMRESULT code, const char *operation)
{
    char message[256];
    if (code == MMSYSERR_NOERROR) {
        return 1;
    }
    if (midiOutGetErrorTextA(code, message, sizeof(message)) != MMSYSERR_NOERROR) {
        snprintf(message, sizeof(message), "error %u", (unsigned)code);
    }
    fprintf(stderr, "Music unavailable: %s: %s\n", operation, message);
    return 0;
}

/* Open an owned system MIDI destination, using device on Windows or port on Linux. Borrow both
 * selection strings and return state for midi_output_close. Return NULL with a diagnostic when
 * the device, optional service or native setup is unavailable.
 */
MidiOutput *midi_output_open(const char *device, const char *port)
{
    MidiOutput *m;
    UINT selected = MIDI_MAPPER;
    char *end;
    long value;
    (void)port;
    if (device) {
        errno = 0;
        value = strtol(device, &end, 10);
        if (*device && !*end && !errno && value >= -1 &&
            (value == -1 || (unsigned long)value < midiOutGetNumDevs())) {
            selected = (UINT)value;
        } else {
            int found = 0;
            for (UINT i = 0; i < midiOutGetNumDevs(); i++) {
                MIDIOUTCAPSW caps;
                char name[256];
                if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) {
                    continue;
                }
                if (!WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, sizeof(name), NULL,
                                         NULL)) {
                    continue;
                }
                if (!strcmp(name, device)) {
                    selected = i;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Music unavailable: MIDI device not found: %s\n", device);
                return NULL;
            }
        }
    }
    m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    if (!midi_report_windows_error(midiOutOpen(&m->handle, selected, 0, 0, CALLBACK_NULL),
                                   "midiOutOpen")) {
        free(m);
        return NULL;
    }
    fprintf(stderr, "Music: winmm MIDI output %s\n", device ? device : "mapper (-1)");
    return m;
}

/* Send one complete borrowed MIDI message without instrument remapping. Windows copies
 * asynchronous SysEx bytes before returning. Return one when accepted, zero for invalid length
 * or native delivery failure; earlier messages are not rolled back.
 */
int midi_output_send(MidiOutput *m, const uint8_t *b, size_t n)
{
    if (!m || m->failed) {
        return 0;
    }
    if (!n || n > MIDI_SYSEX_BYTES) {
        return 0;
    }
    if (b[0] == 0xf0) {
        LongMessage *slot = NULL;
        for (unsigned i = 0; i < MIDI_SYSEX_SLOTS; i++) {
            LongMessage *s = &m->messages[i];
            if (s->prepared) {
                MMRESULT r = midiOutUnprepareHeader(m->handle, &s->header, sizeof(s->header));
                if (r == MMSYSERR_NOERROR) {
                    s->prepared = 0;
                } else if (r != MIDIERR_STILLPLAYING) {
                    midi_report_windows_error(r, "midiOutUnprepareHeader");
                    m->failed = 1;
                    return 0;
                }
            }
            if (!slot && !s->prepared) {
                slot = s;
            }
        }
        if (!slot) {
            fprintf(stderr, "Music stopped: MIDI SysEx queue full\n");
            m->failed = 1;
            return 0;
        }
        memset(&slot->header, 0, sizeof(slot->header));
        memcpy(slot->bytes, b, n);
        slot->header.lpData = (LPSTR)slot->bytes;
        slot->header.dwBufferLength = (DWORD)n;
        if (!midi_report_windows_error(
                    midiOutPrepareHeader(m->handle, &slot->header, sizeof(slot->header)),
                    "midiOutPrepareHeader")) {
            m->failed = 1;
            return 0;
        }
        slot->prepared = 1;
        if (!midi_report_windows_error(
                    midiOutLongMsg(m->handle, &slot->header, sizeof(slot->header)),
                    "midiOutLongMsg")) {
            m->failed = 1;
            return 0;
        }
    } else {
        DWORD packed = 0;
        if (n > 3) {
            return 0;
        }
        for (size_t i = 0; i < n; i++) {
            packed |= (DWORD)b[i] << (8 * i);
        }
        if (!midi_report_windows_error(midiOutShortMsg(m->handle, packed), "midiOutShortMsg")) {
            m->failed = 1;
            return 0;
        }
    }
    return 1;
}

/* Reset the WinMM device, unprepare every queued SysEx header and close the device, reporting
 * each refusal, then free the owned output object, unless a header or the device could not be
 * released (the driver may still hold the buffers). Requires nonnull object storage; returns no
 * close status.
 */
static void midi_close_device(MidiOutput *m)
{
    int retained = 0;
    midi_report_windows_error(midiOutReset(m->handle), "midiOutReset");
    for (unsigned i = 0; i < MIDI_SYSEX_SLOTS; i++) {
        if (m->messages[i].prepared) {
            if (!midi_report_windows_error(
                        midiOutUnprepareHeader(m->handle, &m->messages[i].header, sizeof(m->messages[i].header)),
                        "midiOutUnprepareHeader on close")) {
                retained = 1;
            }
        }
    }
    if (!midi_report_windows_error(midiOutClose(m->handle), "midiOutClose")) {
        retained = 1;
    }
    /* A broken driver must never receive dangling MIDIHDR/data pointers.
     * This bounded allocation is retained only on failed driver shutdown.
     */
    if (!retained) {
        free(m);
    }
}
#else
#include <sys/types.h>
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <unistd.h>
#define ALSA_FUNCTIONS(X)                                                               \
    X(int, snd_seq_open, (snd_seq_t **, const char *, int, int))                        \
    X(int, snd_seq_close, (snd_seq_t *))                                                \
    X(int, snd_seq_set_client_name, (snd_seq_t *, const char *))                        \
    X(int, snd_seq_client_id, (snd_seq_t *))                                            \
    X(int, snd_seq_create_simple_port, (snd_seq_t *, const char *, unsigned, unsigned)) \
    X(int, snd_seq_connect_to, (snd_seq_t *, int, int, int))                            \
    X(int, snd_seq_client_info_malloc, (snd_seq_client_info_t **))                      \
    X(void, snd_seq_client_info_free, (snd_seq_client_info_t *))                        \
    X(void, snd_seq_client_info_set_client, (snd_seq_client_info_t *, int))             \
    X(int, snd_seq_client_info_get_client, (const snd_seq_client_info_t *))             \
    X(const char *, snd_seq_client_info_get_name, (const snd_seq_client_info_t *))      \
    X(int, snd_seq_query_next_client, (snd_seq_t *, snd_seq_client_info_t *))           \
    X(int, snd_seq_port_info_malloc, (snd_seq_port_info_t **))                          \
    X(void, snd_seq_port_info_free, (snd_seq_port_info_t *))                            \
    X(void, snd_seq_port_info_set_client, (snd_seq_port_info_t *, int))                 \
    X(void, snd_seq_port_info_set_port, (snd_seq_port_info_t *, int))                   \
    X(int, snd_seq_port_info_get_port, (const snd_seq_port_info_t *))                   \
    X(unsigned, snd_seq_port_info_get_capability, (const snd_seq_port_info_t *))        \
    X(const char *, snd_seq_port_info_get_name, (const snd_seq_port_info_t *))          \
    X(int, snd_seq_query_next_port, (snd_seq_t *, snd_seq_port_info_t *))               \
    X(int, snd_midi_event_new, (size_t, snd_midi_event_t **))                           \
    X(void, snd_midi_event_free, (snd_midi_event_t *))                                  \
    X(long, snd_midi_event_encode,                                                      \
      (snd_midi_event_t *, const unsigned char *, long, snd_seq_event_t *))             \
    X(int, snd_seq_event_output_direct, (snd_seq_t *, snd_seq_event_t *))

struct MidiOutput {
    void *library;
    snd_seq_t *seq;
    snd_midi_event_t *coder;
    int source;
    int client;
    int port;
#define DECLARE(ret, name, args) ret(*name) args;
    ALSA_FUNCTIONS(DECLARE)
#undef DECLARE
};

/* Release the attached ALSA coder, sequencer and loaded library, then free the owned output
 * object. Accepts partial initialization but requires nonnull object storage; returns no close
 * status.
 */
static void midi_close_device(MidiOutput *m)
{
    if (m->coder) {
        m->snd_midi_event_free(m->coder);
    }
    if (m->seq) {
        m->snd_seq_close(m->seq);
    }
    if (m->library) {
        dlclose(m->library);
    }
    free(m);
}

/* The failed open: the diagnostic REASON, then the enumeration's client and port records when they
 * are still held (CI, PI, or NULL), then the partial device (midi_close_device); returns NULL.
 */
static MidiOutput *open_failed(MidiOutput *m, const char *reason, snd_seq_client_info_t *ci,
                               snd_seq_port_info_t *pi)
{
    fprintf(stderr, "Music unavailable: %s\n", reason);
    if (ci) {
        m->snd_seq_client_info_free(ci);
    }
    if (pi) {
        m->snd_seq_port_info_free(pi);
    }
    midi_close_device(m);
    return NULL;
}

/* Load libasound.so.2 and every ALSA_FUNCTIONS entry point into M, in the list's order. Return
 * the reason of the first failure (the library, then a missing symbol), or NULL.
 */
static const char *alsa_load(MidiOutput *m)
{
    m->library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!m->library) {
        return "libasound.so.2 unavailable";
    }
#define LOAD(ret, name, args)                                                             \
    {                                                                                     \
        void *symbol = dlsym(m->library, #name);                                          \
        if (!symbol) {                                                                    \
            return "ALSA sequencer API unavailable";                                      \
        }                                                                                 \
        _Static_assert(sizeof(m->name) == sizeof(symbol), "POSIX function pointer size"); \
        memcpy(&m->name, &symbol, sizeof(symbol));                                        \
    }
    ALSA_FUNCTIONS(LOAD)
#undef LOAD
    return NULL;
}

/* Choose the first destination of the sequencer's clients in M->client and M->port, through the
 * borrowed client and port records CI and PI: a port that accepts writes and write subscriptions,
 * skipping the system client 0, this client and anything named Midi Through. Leave both -1 when
 * there is none.
 */
static void alsa_find_destination(MidiOutput *m, snd_seq_client_info_t *ci,
                                  snd_seq_port_info_t *pi)
{
    m->snd_seq_client_info_set_client(ci, -1);
    while (m->client < 0 && m->snd_seq_query_next_client(m->seq, ci) >= 0) {
        int client = m->snd_seq_client_info_get_client(ci);
        const char *name = m->snd_seq_client_info_get_name(ci);
        if (client == 0 || client == m->snd_seq_client_id(m->seq) ||
            (name && strstr(name, "Midi Through"))) {
            continue;
        }
        m->snd_seq_port_info_set_client(pi, client);
        m->snd_seq_port_info_set_port(pi, -1);
        while (m->snd_seq_query_next_port(m->seq, pi) >= 0) {
            unsigned caps = m->snd_seq_port_info_get_capability(pi);
            const char *pn = m->snd_seq_port_info_get_name(pi);
            if ((caps & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) !=
                        (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE) ||
                (pn && strstr(pn, "Midi Through"))) {
                continue;
            }
            m->client = client;
            m->port = m->snd_seq_port_info_get_port(pi);
            break;
        }
    }
}

/* Open an owned system MIDI destination, using device on Windows or port on Linux. Borrow both
 * selection strings and return state for midi_output_close. Return NULL with a diagnostic when
 * the device, optional service or native setup is unavailable. On Linux, in order: the library and
 * its entry points (alsa_load), the sequencer (refused without read/write access to /dev/snd/seq,
 * which avoids ALSA's own stderr line on the common WSL path), this client's name and output
 * port, the destination (PORT as CLIENT:PORT, each 0..255, or the first suitable one:
 * alsa_find_destination), the connection and the encoder. Each failure releases what was set up
 * (open_failed).
 */
MidiOutput *midi_output_open(const char *device, const char *port)
{
    MidiOutput *m = calloc(1, sizeof(*m));
    const char *reason;
    (void)device;
    if (!m) {
        return NULL;
    }
    if ((reason = alsa_load(m)) != NULL) {
        return open_failed(m, reason, NULL, NULL);
    }
    if (access("/dev/snd/seq", R_OK | W_OK) != 0 ||
        m->snd_seq_open(&m->seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) < 0) {
        return open_failed(m, "ALSA sequencer /dev/snd/seq unavailable", NULL, NULL);
    }
    m->snd_seq_set_client_name(m->seq, "WoX");
    m->source = m->snd_seq_create_simple_port(
            m->seq, "XEEN MIDI", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (m->source < 0) {
        return open_failed(m, "cannot create ALSA MIDI output", NULL, NULL);
    }
    m->client = m->port = -1;
    if (port) {
        char tail;
        int c;
        int p;
        if (sscanf(port, "%d:%d%c", &c, &p, &tail) != 2 || c < 0 || c > 255 || p < 0 || p > 255) {
            return open_failed(m, "invalid ALSA port (expected CLIENT:PORT)", NULL, NULL);
        }
        m->client = c;
        m->port = p;
    } else {
        snd_seq_client_info_t *ci = NULL;
        snd_seq_port_info_t *pi = NULL;
        if (m->snd_seq_client_info_malloc(&ci) < 0 || m->snd_seq_port_info_malloc(&pi) < 0) {
            return open_failed(m, "no ALSA MIDI destination (excluding Midi Through)", ci, pi);
        }
        alsa_find_destination(m, ci, pi);
        m->snd_seq_client_info_free(ci);
        m->snd_seq_port_info_free(pi);
    }
    if (m->client < 0) {
        return open_failed(m, "no ALSA MIDI destination (excluding Midi Through)", NULL, NULL);
    }
    if (m->snd_seq_connect_to(m->seq, m->source, m->client, m->port) < 0) {
        return open_failed(m, "cannot connect ALSA MIDI destination", NULL, NULL);
    }
    if (m->snd_midi_event_new(65536, &m->coder) < 0) {
        return open_failed(m, "cannot allocate ALSA MIDI encoder", NULL, NULL);
    }
    fprintf(stderr, "Music: ALSA MIDI output %d:%d\n", m->client, m->port);
    return m;
}

/* A refused send's diagnostic (the message is dropped; earlier messages stay sent); returns 0. */
static int send_failed(void)
{
    fprintf(stderr, "Music stopped: ALSA MIDI output failed\n");
    return 0;
}

/* Send one complete borrowed MIDI message (up to 64 KiB) without instrument remapping: the ALSA encoder turns the
 * bytes into sequencer events, each sent directly as it is complete. Return one when accepted, zero for an invalid
 * length or a failed encode or send (with its diagnostic); the events already sent stay sent.
 */
int midi_output_send(MidiOutput *m, const uint8_t *b, size_t n)
{
    if (!m || !n || n > 65536) {
        return 0;
    }
    while (n) {
        snd_seq_event_t event;
        long used;
        memset(&event, 0, sizeof(event));
        used = m->snd_midi_event_encode(m->coder, b, (long)n, &event);
        if (used <= 0 || (size_t)used > n) {
            return send_failed();
        }
        b += used;
        n -= (size_t)used;
        if (event.type == SND_SEQ_EVENT_NONE) {
            continue;
        }
        snd_seq_ev_set_source(&event, m->source);
        snd_seq_ev_set_dest(&event, m->client, m->port);
        snd_seq_ev_set_direct(&event);
        if (m->snd_seq_event_output_direct(m->seq, &event) < 0) {
            return send_failed();
        }
    }
    return 1;
}
#endif
/* Close owned system MIDI state; null is ignored. A Windows driver that cannot release
 * asynchronous buffers retains the bounded allocation to avoid dangling pointers. The caller
 * must stop message delivery before closing; no status is returned.
 */
void midi_output_close(MidiOutput *m)
{
    if (!m) {
        return;
    }
    midi_close_device(m);
}
