#ifndef XEEN_AUDIO_CD_STREAM_H
#define XEEN_AUDIO_CD_STREAM_H
#include <stddef.h>
#include <stdint.h>
/* One PlayCD segment from the owner's OGG rips of the two discs
 * (xeen_cd_track_file), decoded by stb_vorbis as interleaved stereo S16.
 *
 * A disc is its audio tracks' files back to back: 483C7 plays an absolute
 * range, so a segment that starts before its track (from < 0) begins in the
 * previous track's tail and one that runs past the track's end continues in
 * the next track (the rips keep each track's trailing gap; inference from
 * their silent tails, phase134/PREFLIGHT.md). The range is clamped to the
 * disc's first and last audio tracks. One CD frame is rate/75 samples. */
typedef struct CdStream CdStream;
/* NULL with the reason in err (no such track, a file missing or not Vorbis).
 * An empty range (to <= from) opens nothing and reports "empty segment". */
CdStream *cd_stream_open(const char *dir, unsigned disc, unsigned track, int32_t from, int32_t to,
                         char *err, size_t n);
/* Up to frames stereo frames; 0 at the segment's end or on an error (err set,
 * "" at the end). */
size_t cd_stream_read(CdStream *, int16_t *stereo, size_t frames, char *err, size_t n);
unsigned cd_stream_rate(const CdStream *);
/* The decoder's own allocation (stb_vorbis setup + temp memory), in bytes. */
size_t cd_stream_memory(const CdStream *);
void cd_stream_close(CdStream *);
#endif
