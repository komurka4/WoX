/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The parts of the XEEN.EXE player's xeen.h that its audio modules use (XEEN.DAT Phase 132).
 * The XEEN.DAT audio engine implements cc_read over its own archives.
 */
#ifndef XEEN_AUDIO_ARCHIVE_H
#define XEEN_AUDIO_ARCHIVE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "xeen/resources.h"

/* A borrowed archive of the game's reader. Phase 308: complete here (it was audio_engine.c's) so that the movie
 * player (src/player/) can hold one by value. */
typedef struct CC {
    const xeen_archive *archive;
} CC;
typedef struct {
    uint8_t *data;
    size_t size;
} Blob;
typedef struct {
    uint8_t *samples;
    size_t count; /* Complete decoded storage; legacy busy/event accounting. */
    unsigned rate;
    size_t playback_count; /* Source-valid PCM prefix; zero means count. */
    /* Phase 308: the movie player's owned copy of the encoded VOC (player_voc_decode), which it hands to the audio
     * sink's voice (the engine decodes it as the configured digital driver plays it); NULL elsewhere. */
    uint8_t *encoded;
    size_t encoded_size;
} Voice;

/* Read one named resource, XOR-decoded, into an owned Blob. Return one on success; zero leaves
 * the Blob empty. Implemented by the host.
 */
int cc_read(CC *cc, const char *name, Blob *out);
/* Decode borrowed VOC bytes into caller-owned Voice state. Return one with owned PCM storage,
 * zero on malformed data or allocation failure with voice cleared.
 */
int voc_decode(const Blob *b, Voice *v);
/* Free the voice's owned PCM storage and clear its fields. */
void voice_free(Voice *v);
/* Return the selected shorter PCM prefix, or the full decoded count. */
size_t voice_playable_count(const Voice *v);
#endif
