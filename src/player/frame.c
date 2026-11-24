/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Decode compressed cinematic background frames with an adaptive Huffman tree and sliding
 * dictionary. frame_decode writes caller-owned indexed pixels for scene loaders; it shares
 * Blob and screen types with xeen.h.
 */
#include "frame.h"
#include <string.h>

/* The adaptive tree contains 314 symbols: 256 literals and match lengths
 * from 3 to 60. Its 627 nodes share a 4096-byte dictionary. Header bytes
 * zero and one alternate through the initial dictionary; the next two bytes
 * give the output size in big-endian order.
 */
enum {
    FRAME_SYMBOL_COUNT = 314,
    FRAME_NODE_COUNT = 627,
    FRAME_ROOT_NODE = 626,
    FRAME_RING_BYTES = 4096,
    FRAME_HEADER_BYTES = 4,
    FRAME_MAX_MATCH_BYTES = 60,
    FRAME_LITERAL_SYMBOLS = 256,
    FRAME_RING_MASK = 4095,
    FRAME_REBUILD_FREQUENCY = 0x8000,
    FRAME_FREQUENCY_SENTINEL = 0xffff,
    FRAME_BYTE_BITS = 8,
    FRAME_MATCH_SYMBOL_BIAS = 253,
    FRAME_POSITION_LOW_BITS = 6,
    FRAME_POSITION_LOW_MASK = 63,
    FRAME_POSITION_LENGTH_BIAS = 2
};

typedef struct {
    unsigned frequency[FRAME_NODE_COUNT + 1];
    unsigned parent[FRAME_NODE_COUNT + FRAME_SYMBOL_COUNT];
    unsigned child[FRAME_NODE_COUNT];
    const uint8_t *input;
    size_t size;
    size_t at;
    unsigned bits;
    unsigned remaining;
    uint8_t ring[FRAME_RING_BYTES];
    unsigned cursor;
} Decoder;

/* Read the next most-significant input bit into the borrowed output. Decoder owns only its
 * cursor, not its input bytes. Return one on success or zero at truncated input.
 */
static int frame_read_bit(Decoder *decoder, unsigned *value)
{
    if (!decoder->remaining) {
        if (decoder->at >= decoder->size) {
            return 0;
        }
        decoder->bits = decoder->input[decoder->at++];
        decoder->remaining = FRAME_BYTE_BITS;
    }
    *value = (decoder->bits >> --decoder->remaining) & 1;
    return 1;
}

/* Initialize the caller-owned adaptive Huffman tree with equal leaf frequencies and matching
 * parent links. The decoder embeds all tree storage, so this operation allocates nothing and
 * cannot fail.
 */
static void frame_initialize_tree(Decoder *decoder)
{
    unsigned leaf_index;
    unsigned parent_index;
    for (leaf_index = 0; leaf_index < FRAME_SYMBOL_COUNT; leaf_index++) {
        decoder->frequency[leaf_index] = 1;
        decoder->child[leaf_index] = leaf_index + FRAME_NODE_COUNT;
        decoder->parent[leaf_index + FRAME_NODE_COUNT] = leaf_index;
    }
    for (leaf_index = 0, parent_index = FRAME_SYMBOL_COUNT; parent_index < FRAME_NODE_COUNT;
         leaf_index += 2, parent_index++) {
        decoder->frequency[parent_index] =
            decoder->frequency[leaf_index] + decoder->frequency[leaf_index + 1];
        decoder->child[parent_index] = leaf_index;
        decoder->parent[leaf_index] = decoder->parent[leaf_index + 1] = parent_index;
    }
    decoder->frequency[FRAME_NODE_COUNT] = FRAME_FREQUENCY_SENTINEL;
    decoder->parent[FRAME_ROOT_NODE] = 0;
}

/* Rebalance the borrowed adaptive tree when its frequency total reaches the limit. Halve
 * weights while preserving symbol order; this in-place operation allocates nothing and has no
 * failure result.
 */
static void frame_rebuild_tree(Decoder *decoder)
{
    unsigned node_index;
    unsigned parent_index;
    unsigned insertion_index;
    unsigned combined_frequency;
    for (node_index = 0, parent_index = 0; node_index < FRAME_NODE_COUNT; node_index++) {
        if (decoder->child[node_index] >= FRAME_NODE_COUNT) {
            decoder->frequency[parent_index] = (decoder->frequency[node_index] + 1) / 2;
            decoder->child[parent_index++] = decoder->child[node_index];
        }
    }
    for (node_index = 0, parent_index = FRAME_SYMBOL_COUNT; parent_index < FRAME_NODE_COUNT;
         node_index += 2, parent_index++) {
        combined_frequency = decoder->frequency[node_index] + decoder->frequency[node_index + 1];
        insertion_index = parent_index;
        while (insertion_index && combined_frequency < decoder->frequency[insertion_index - 1]) {
            insertion_index--;
        }
        memmove(decoder->frequency + insertion_index + 1, decoder->frequency + insertion_index,
                (parent_index - insertion_index) * sizeof(decoder->frequency[0]));
        memmove(decoder->child + insertion_index + 1, decoder->child + insertion_index,
                (parent_index - insertion_index) * sizeof(decoder->child[0]));
        decoder->frequency[insertion_index] = combined_frequency;
        decoder->child[insertion_index] = node_index;
    }
    for (node_index = 0; node_index < FRAME_NODE_COUNT; node_index++) {
        parent_index = decoder->child[node_index];
        decoder->parent[parent_index] = node_index;
        if (parent_index < FRAME_NODE_COUNT) {
            decoder->parent[parent_index + 1] = node_index;
        }
    }
}

/* Increase a decoded symbol's frequency and reorder the borrowed adaptive tree. Rebuild at the
 * frequency limit; this in-place operation has no failure result.
 */
static void frame_update_tree(Decoder *decoder, unsigned symbol)
{
    unsigned node;
    unsigned next_node;
    unsigned frequency;
    unsigned child_node;
    if (decoder->frequency[FRAME_ROOT_NODE] == FRAME_REBUILD_FREQUENCY) {
        frame_rebuild_tree(decoder);
    }
    node = decoder->parent[symbol + FRAME_NODE_COUNT];
    do {
        frequency = ++decoder->frequency[node];
        next_node = node + 1;
        if (frequency > decoder->frequency[next_node]) {
            do {
                next_node++;
            } while (frequency > decoder->frequency[next_node]);
            next_node--;
            decoder->frequency[node] = decoder->frequency[next_node];
            decoder->frequency[next_node] = frequency;
            child_node = decoder->child[node];
            decoder->child[node] = decoder->child[next_node];
            decoder->child[next_node] = child_node;
            child_node = decoder->child[node];
            decoder->parent[child_node] = node;
            if (child_node < FRAME_NODE_COUNT) {
                decoder->parent[child_node + 1] = node;
            }
            child_node = decoder->child[next_node];
            decoder->parent[child_node] = next_node;
            if (child_node < FRAME_NODE_COUNT) {
                decoder->parent[child_node + 1] = next_node;
            }
            node = next_node;
        }
        node = decoder->parent[node];
    } while (node);
}

/* Code lengths used to decode the high bits of a dictionary position. */
static const uint8_t FRAME_POSITION_LENGTH[256] = {
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

/* Prefix lookup used with the position code lengths below. */
static const uint8_t FRAME_POSITION_CODE[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,
    6,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,
    9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11,
    12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17,
    18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23,
    24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 35, 35,
    36, 36, 37, 37, 38, 38, 39, 39, 40, 40, 41, 41, 42, 42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
};

/* Decode the next adaptive-Huffman symbol into the borrowed output and update tree weights.
 * Return zero when encoded bits run out; decoder and output storage remain caller-owned.
 */
static int frame_read_symbol(Decoder *decoder, unsigned *symbol)
{
    unsigned node;
    unsigned value;
    node = decoder->child[FRAME_ROOT_NODE];
    while (node < FRAME_NODE_COUNT) {
        if (!frame_read_bit(decoder, &value)) {
            return 0;
        }
        node = decoder->child[node + value];
    }
    *symbol = node - FRAME_NODE_COUNT;
    frame_update_tree(decoder, *symbol);
    return 1;
}

/* Decode a dictionary position and copy its match into borrowed pixels, advancing decoder
 * state and the output cursor. Return zero for truncated bits, an invalid position code or an
 * overlong match; partial output is not rolled back.
 */
static int frame_copy_match(Decoder *decoder, unsigned symbol, uint8_t *pixels, size_t pixel_count,
                            size_t *written)
{
    unsigned index;
    unsigned value;
    unsigned position_prefix;
    unsigned position;
    unsigned length;
    position_prefix = 0;
    for (index = 0; index < FRAME_BYTE_BITS; index++) {
        if (!frame_read_bit(decoder, &value)) {
            return 0;
        }
        position_prefix = (position_prefix << 1) | value;
    }
    position = (unsigned)FRAME_POSITION_CODE[position_prefix] << FRAME_POSITION_LOW_BITS;
    length = FRAME_POSITION_LENGTH[position_prefix];
    if (length < FRAME_POSITION_LENGTH_BIAS) {
        return 0;
    }
    for (index = 0; index < length - FRAME_POSITION_LENGTH_BIAS; index++) {
        if (!frame_read_bit(decoder, &value)) {
            return 0;
        }
        position_prefix = (position_prefix << 1) | value;
    }
    position |= position_prefix & FRAME_POSITION_LOW_MASK;
    position = (decoder->cursor - position - 1) & FRAME_RING_MASK;
    length = symbol - FRAME_MATCH_SYMBOL_BIAS;
    if (length > pixel_count - *written) {
        return 0;
    }
    for (index = 0; index < length; index++) {
        value = decoder->ring[(position + index) & FRAME_RING_MASK];
        pixels[(*written)++] = (uint8_t)value;
        decoder->ring[decoder->cursor] = (uint8_t)value;
        decoder->cursor = (decoder->cursor + 1) & FRAME_RING_MASK;
    }
    return 1;
}

/* Decode a compressed background into the caller-owned destination of pixel_count bytes.
 * Return one after writing the requested number of pixels with a matching header; trailing
 * input is ignored. Return zero for invalid or truncated input, possibly after partial output.
 */
int frame_decode(const Blob *source, uint8_t *pixels, size_t pixel_count)
{
    Decoder decoder = {0};
    size_t written = 0;
    unsigned index;
    unsigned symbol;
    if (!source || !source->data || source->size < FRAME_HEADER_BYTES || !pixels ||
        ((unsigned)source->data[2] * 256 + source->data[3]) != pixel_count) {
        return 0;
    }
    decoder.input = source->data;
    decoder.size = source->size;
    decoder.at = FRAME_HEADER_BYTES;
    decoder.cursor = FRAME_RING_BYTES - FRAME_MAX_MATCH_BYTES;
    for (index = 0; index < FRAME_RING_BYTES - FRAME_MAX_MATCH_BYTES; index++) {
        decoder.ring[index] = source->data[index & 1];
    }
    frame_initialize_tree(&decoder);
    while (written < pixel_count) {
        if (!frame_read_symbol(&decoder, &symbol)) {
            return 0;
        }
        if (symbol < FRAME_LITERAL_SYMBOLS) {
            pixels[written++] = (uint8_t)symbol;
            decoder.ring[decoder.cursor] = (uint8_t)symbol;
            decoder.cursor = (decoder.cursor + 1) & FRAME_RING_MASK;
        } else {
            if (!frame_copy_match(&decoder, symbol, pixels, pixel_count, &written)) {
                return 0;
            }
        }
    }
    return 1;
}
