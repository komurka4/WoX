/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Decode VOC speech and choose its playable sample extent. Copied from the XEEN.EXE player's
 * media.c (revision 32627496) for XEEN.DAT Phase 132; only the decode telemetry was removed.
 */
#include "audio_archive.h"
#include <stdlib.h>
#include <string.h>

#define VOC_HEADER_BYTES           26
#define VOC_SIGNATURE_BYTES        20
#define VOC_BLOCK_LENGTH_BYTES     3
#define VOC_PCM_HEADER_BYTES       2
#define VOC_RATE_NUMERATOR_HZ      1000000u
#define VOC_RATE_DIVISOR_BASE      256
#define VOC_UNPLAYED_TRAILER_BYTES 6
/* A block's bytes from its start: the type (1: sound data), the 24-bit length, then a sound-data block's rate and
 * pack (0: unsigned 8-bit) bytes. */
#define VOC_BLOCK_TYPE             0
#define VOC_BLOCK_LENGTH           1
#define VOC_BLOCK_PACK             5
#define VOC_TYPE_SOUND_DATA        1
#define VOC_PACK_8BIT              0

/* Return the selected shorter PCM prefix, or the full decoded count. */
size_t voice_playable_count(const Voice *voice)
{
    return voice->playback_count && voice->playback_count < voice->count ? voice->playback_count
                                                                         : voice->count;
}
void voice_free(Voice *voice)
{
    free(voice->samples);
    free(voice->encoded); /* Phase 308: the movie player's copy (NULL for the game's voices) */
    memset(voice, 0, sizeof(*voice));
}
/* Append one supported VOC block to the caller-owned decoder state and PCM allocation. Advance
 * position only on success and return one; return zero for unsupported format, truncation,
 * inconsistent rate or allocation failure. The outer decoder owns cleanup of any retained
 * samples.
 */
static int voc_append_block(const Blob *encoded, Voice *voice, unsigned block_type,
                            size_t *position, unsigned *rate)
{
    size_t cursor = *position;
    size_t block_size;
    size_t header_size = 0;
    uint8_t *samples;
    if (encoded->size - cursor < VOC_BLOCK_LENGTH_BYTES) {
        return 0;
    }
    block_size = encoded->data[cursor] | ((size_t)encoded->data[cursor + 1] << 8) |
                 ((size_t)encoded->data[cursor + 2] << 16);
    cursor += VOC_BLOCK_LENGTH_BYTES;
    if (block_size > encoded->size - cursor) {
        return 0;
    }
    if (block_type == VOC_TYPE_SOUND_DATA) {
        if (block_size < VOC_PCM_HEADER_BYTES || encoded->data[cursor + 1] != 0) {
            return 0;
        }
        (*rate) = VOC_RATE_NUMERATOR_HZ / (VOC_RATE_DIVISOR_BASE - encoded->data[cursor]);
        header_size = VOC_PCM_HEADER_BYTES;
    } else if (block_type == 2) {
        if (!(*rate)) {
            return 0;
        }
    } else {
        return 0;
    }
    if (voice->rate && voice->rate != (*rate)) {
        return 0;
    }
    if (block_size == header_size) {
        cursor += block_size;
        *position = cursor;
        return 1;
    }
    voice->rate = (*rate);
    samples = realloc(voice->samples, voice->count + block_size - header_size);
    if (!samples && block_size > header_size) {
        return 0;
    }
    voice->samples = samples;
    memcpy(voice->samples + voice->count, encoded->data + cursor + header_size,
           block_size - header_size);
    voice->count += block_size - header_size;
    cursor += block_size;
    *position = cursor;
    return 1;
}

/* Set the borrowed voice's playback metadata for the cinematic single-block PCM layout without
 * changing decoded sample bytes. Other layouts retain their full extent. This in-place
 * selection allocates nothing and has no failure result.
 */
static void voc_select_playable_extent(const Blob *encoded, Voice *voice, size_t first_block,
                                       unsigned blocks)
{
    voice->playback_count = voice->count;
    /* Cinematic PCM resources have a six-byte suffix that the original never
     * plays. Keep decoded storage intact for event and busy accounting, and
     * select the longest playable prefix for this exact single-block format.
     * The rule depends on the resource layout, not sample values.
     */
    if (first_block == VOC_HEADER_BYTES && blocks == 1 && voice->count >= 24 &&
        encoded->data[first_block + VOC_BLOCK_TYPE] == VOC_TYPE_SOUND_DATA &&
        encoded->data[first_block + VOC_BLOCK_LENGTH + 2] == 0 && /* the length's high byte */
        encoded->data[first_block + VOC_BLOCK_PACK] == VOC_PACK_8BIT &&
        ((size_t)encoded->data[first_block + VOC_BLOCK_LENGTH] +
         ((size_t)encoded->data[first_block + VOC_BLOCK_LENGTH + 1] << 8)) == voice->count + VOC_PCM_HEADER_BYTES) {
        voice->playback_count = voice->count - VOC_UNPLAYED_TRAILER_BYTES;
    }
}

/* Decode supported VOC PCM blocks into the borrowed Voice structure. Return one with owned
 * sample storage for voice_free, or zero after releasing and clearing partial decoded storage.
 */
static int voc_decode_payload(const Blob *encoded, Voice *voice)
{
    size_t cursor;
    size_t first_block;
    unsigned rate = 0;
    unsigned blocks = 0;
    int ended = 0;
    memset(voice, 0, sizeof(*voice));
    if (encoded->size < VOC_HEADER_BYTES ||
        memcmp(encoded->data, "Creative Voice File\x1a", VOC_SIGNATURE_BYTES)) {
        return 0;
    }
    cursor = encoded->data[20] | ((unsigned)encoded->data[21] << 8);
    if (cursor < VOC_HEADER_BYTES || cursor > encoded->size) {
        return 0;
    }
    first_block = cursor;
    while (cursor < encoded->size) {
        unsigned block_type = encoded->data[cursor++];
        if (!block_type) {
            ended = 1;
            break;
        }
        blocks++;
        if (!voc_append_block(encoded, voice, block_type, &cursor, &rate)) {
            goto fail;
        }
    }
    if (ended && voice->count) {
        voc_select_playable_extent(encoded, voice, first_block, blocks);
        return 1;
    }
fail:
    voice_free(voice);
    return 0;
}

/* Decode borrowed VOC bytes into caller-owned Voice state. On success return one and transfer
 * the PCM allocation to voice_free. Return zero on malformed data or allocation failure with
 * voice cleared.
 */
int voc_decode(const Blob *encoded, Voice *voice)
{
    return voc_decode_payload(encoded, voice);
}
