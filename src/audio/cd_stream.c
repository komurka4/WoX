#include "audio/cd_stream.h"
#include "xeen/audio.h"
#define STB_VORBIS_HEADER_ONLY
#include "audio/stb_vorbis.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The CD voice segments (cd_stream.h). */

struct CdStream {
    char dir[1024];
    unsigned disc, track, rate;
    stb_vorbis *v;
    uint64_t remaining; /* samples per channel still to play */
    char error[512];    /* a crossing's failure, reported by the next read */
};

/** Formats an error message (format F with A, B and C) into ERR when one is given. */
static void format_error(char *err, size_t n, const char *f, unsigned a, unsigned b, const char *c)
{
    if (err && n) {
        snprintf(err, n, f, a, b, c);
    }
}
/**
 * Opens the Ogg Vorbis file of CD TRACK in the stream's track directory; returns NULL with an error
 * message when it is missing or unreadable.
 */
static stb_vorbis *open_track(CdStream *s, unsigned track, char *err, size_t n)
{
    char name[16], path[1100];
    int e = 0;
    stb_vorbis *v;
    stb_vorbis_info info;
    if (!xeen_cd_track_file(s->disc, track, name, sizeof name)) {
        format_error(err, n, "CD%u has no audio track %u%s", s->disc + 1u, track, "");
        return NULL;
    }
    if (snprintf(path, sizeof path, "%s/%s", s->dir, name) >= (int)sizeof path) {
        format_error(err, n, "CD%u track %u: path too long%s", s->disc + 1u, track, "");
        return NULL;
    }
    if (!(v = stb_vorbis_open_filename(path, &e, NULL))) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f);
            format_error(err, n, "CD%u track %u: not a readable Ogg Vorbis file: %s", s->disc + 1u,
                         track, path);
        } else {
            format_error(err, n, "CD%u track %u: missing %s", s->disc + 1u, track, path);
        }
        return NULL;
    }
    info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.channels > 2 || info.sample_rate < 8000u ||
        info.sample_rate > 96000u || (s->rate && info.sample_rate != s->rate)) {
        format_error(err, n, "CD%u track %u: unsupported layout (channels or sample rate) in %s",
                     s->disc + 1u, track, path);
        stb_vorbis_close(v);
        return NULL;
    }
    s->rate = info.sample_rate;
    return v;
}
/** Returns the length of an open Vorbis stream in samples. */
static uint64_t length_of(stb_vorbis *v)
{
    return stb_vorbis_stream_length_in_samples(v);
}
/* Moves S to TRACK: its file opened (open_track), then the current one closed; returns 0 (S unchanged, the reason
 * in ERR) when TRACK's file cannot be opened. */
static int swap_track(CdStream *s, unsigned track, char *err, size_t n)
{
    stb_vorbis *v = open_track(s, track, err, n);
    if (!v) {
        return 0;
    }
    stb_vorbis_close(s->v);
    s->v = v;
    s->track = track;
    return 1;
}
CdStream *cd_stream_open(const char *dir, unsigned disc, unsigned track, int32_t from, int32_t to,
                         char *err, size_t n)
{
    CdStream *s;
    int64_t at, frame;
    if (err && n) {
        err[0] = 0;
    }
    if (!dir || !*dir) {
        if (err && n) {
            snprintf(err, n, "CD tracks unavailable (no directory; [files] cd_tracks_dir)");
        }
        return NULL;
    }
    if (!xeen_cd_track_file(disc, track, (char[16]){0}, 16)) {
        format_error(err, n, "CD%u has no audio track %u%s", disc + 1u, track, "");
        return NULL;
    }
    if (to <= from) {
        format_error(err, n, "CD%u track %u: empty segment%s", disc + 1u, track, "");
        return NULL;
    }
    if (!(s = calloc(1, sizeof *s))) {
        format_error(err, n, "CD%u track %u: allocation failed%s", disc + 1u, track, "");
        return NULL;
    }
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    s->disc = disc;
    s->track = track;
    if (!(s->v = open_track(s, track, err, n))) {
        free(s);
        return NULL;
    }
    frame = s->rate / 75u; /* samples per CD frame (75 a second; a rate not divisible by 75 truncates) */
    at = (int64_t)from * frame;
    s->remaining = (uint64_t)((int64_t)to - from) * (uint64_t)frame;
    /* Before the track: the previous files' tails, or the disc's first sample. */
    while (at < 0) {
        if (s->track <= XEEN_CD_FIRST_TRACK) {
            s->remaining = s->remaining > (uint64_t)-at ? s->remaining - (uint64_t)-at : 0u;
            at = 0;
            break;
        }
        if (!swap_track(s, s->track - 1u, err, n)) {
            cd_stream_close(s);
            return NULL;
        }
        at += (int64_t)length_of(s->v);
    }
    /* Past the track's end: the next files. */
    while ((uint64_t)at >= length_of(s->v)) {
        at -= (int64_t)length_of(s->v);
        if (s->track >= xeen_cd_last_track(disc)) {
            format_error(err, n, "CD%u track %u: segment starts past the disc's end%s", disc + 1u,
                         track, "");
            cd_stream_close(s);
            return NULL;
        }
        if (!swap_track(s, s->track + 1u, err, n)) {
            cd_stream_close(s);
            return NULL;
        }
    }
    if (at && !stb_vorbis_seek(s->v, (unsigned)at)) {
        format_error(err, n, "CD%u track %u: seek failed%s", disc + 1u, s->track, "");
        cd_stream_close(s);
        return NULL;
    }
    return s;
}
#define CD_READ_CHUNK 4096u /* frames decoded per stb_vorbis call */
size_t cd_stream_read(CdStream *s, int16_t *out, size_t frames, char *err, size_t n)
{
    size_t done = 0;
    if (err && n) {
        err[0] = 0;
    }
    if (!s || !out) {
        return 0;
    }
    if (!s->remaining && *s->error) {
        if (err && n) {
            snprintf(err, n, "%s", s->error);
        }
        return 0;
    }
    while (done < frames && s->remaining) {
        size_t want = frames - done;
        int got;
        if (want > s->remaining) {
            want = (size_t)s->remaining;
        }
        if (want > CD_READ_CHUNK) {
            want = CD_READ_CHUNK;
        }
        got = stb_vorbis_get_samples_short_interleaved(s->v, 2, out + 2u * done, (int)(want * 2u));
        if (got > 0) {
            done += (size_t)got;
            s->remaining -= (uint64_t)got;
            continue;
        }
        /* The file's end: the disc continues in the next track, or ends. */
        if (s->track >= xeen_cd_last_track(s->disc)) {
            s->remaining = 0;
            break;
        }
        if (!swap_track(s, s->track + 1u, s->error, sizeof s->error)) {
            s->remaining = 0;
            if (!done && err && n) {
                snprintf(err, n, "%s", s->error);
            }
            break;
        }
    }
    return done;
}
unsigned cd_stream_rate(const CdStream *s)
{
    return s ? s->rate : 0u;
}
size_t cd_stream_memory(const CdStream *s)
{
    stb_vorbis_info i;
    if (!s || !s->v) {
        return 0;
    }
    i = stb_vorbis_get_info(s->v);
    return (size_t)i.setup_memory_required + (size_t)i.temp_memory_required + sizeof *s;
}
void cd_stream_close(CdStream *s)
{
    if (!s) {
        return;
    }
    if (s->v) {
        stb_vorbis_close(s->v);
    }
    free(s);
}
