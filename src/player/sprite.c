/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Decode and compose sprite cells, including cinematic scaling and clipping. The sprite_draw
 * entry points write caller-owned indexed pixels used by runtime and ending scenes; archive
 * loading stays in cc.
 */
#include "xeen.h"

#define SPRITE_CELL_HEADER_BYTES 8
#define SPRITE_MAX_SCALE_INDEX 15
#define SPRITE_CLIPPED_EFFECT 0x0800
#define SPRITE_WINDOW_TOP_ROW 17

enum {
    SPRITE_PACKET_LITERAL = 0,
    SPRITE_PACKET_REPEAT = 1,
    SPRITE_PACKET_PAIR = 2,
    SPRITE_PACKET_DELTA = 3
};

#define SPRITE_LITERAL_LIMIT 0x40
#define SPRITE_REPEAT_LIMIT 0x60
#define SPRITE_BACKREF_LIMIT 0x80
#define SPRITE_PAIR_LIMIT 0xa0
#define SPRITE_SKIP_LIMIT 0xc0
#define SPRITE_PACKET_COUNT_MASK 31

static unsigned sprite_read_u16_le(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

/* Decode one sprite cell into the borrowed framebuffer. Each cell carries
 * its own origin; skip packets encode transparency. Consume whole packets
 * even when clipping hides their pixels. Return zero for malformed data.
 */
static int sprite_decode_cell(const Blob *sprite, size_t cell_offset, int offset_x, int offset_y,
                              int clip_above_screen, unsigned top_row, unsigned bottom_row,
                              uint8_t *pixels)
{
    int origin_x;
    int origin_y;
    int y;
    unsigned height;
    size_t cursor = cell_offset;
    if (cursor > sprite->size || sprite->size - cursor < SPRITE_CELL_HEADER_BYTES) {
        return 0;
    }
    origin_x = (int)sprite_read_u16_le(sprite->data + cursor);
    origin_y = (int)sprite_read_u16_le(sprite->data + cursor + 4);
    height = sprite_read_u16_le(sprite->data + cursor + 6);
    cursor += SPRITE_CELL_HEADER_BYTES;
    if (height > XEEN_HEIGHT_PIXELS || origin_x > XEEN_WIDTH_PIXELS ||
        origin_y > XEEN_HEIGHT_PIXELS) {
        return 0;
    }
    /* Window clipping deliberately consumes one fewer encoded record than the
     * clipped row count. A transparent record counts once, even when its
     * extra-row byte skips several rows. Preserve this off-by-one behavior.
     */
    if (clip_above_screen && offset_y + origin_y < 0) {
        unsigned drop = (unsigned)(-(offset_y + origin_y));
        unsigned j;
        if (drop >= height) {
            return 1;
        }
        for (j = 1; j < drop; j++) {
            unsigned length;
            if (cursor >= sprite->size) {
                return 0;
            }
            length = sprite->data[cursor++];
            if (!length) {
                if (cursor >= sprite->size) {
                    return 0;
                }
                cursor++;
            } else {
                if (length > sprite->size - cursor) {
                    return 0;
                }
                cursor += length;
            }
        }
        height -= drop;
        offset_y = -origin_y;
    }
    for (y = 0; y < (int)height; y++) {
        unsigned length;
        size_t end;
        int x;
        if (cursor >= sprite->size) {
            return 0;
        }
        length = sprite->data[cursor++];
        if (!length) {
            unsigned skip;
            if (cursor >= sprite->size) {
                return 0;
            }
            skip = sprite->data[cursor++]; /* A skip that exhausts the signed remaining width ends
                                              this row. */
            y += (int)skip;
            continue;
        }
        if (length > sprite->size - cursor) {
            return 0;
        }
        end = cursor + length;
        x = offset_x + origin_x + sprite->data[cursor++];
        while (cursor < end) {
            unsigned opcode = sprite->data[cursor++];
            unsigned pixel_count = 0;
            unsigned pixel_index;
            size_t source_offset = 0;
            uint8_t first_color = 0;
            uint8_t second_color = 0;
            int packet_mode;
            if (opcode < SPRITE_LITERAL_LIMIT) {
                pixel_count = opcode + 1;
                packet_mode = SPRITE_PACKET_LITERAL;
            } else if (opcode < SPRITE_REPEAT_LIMIT) {
                pixel_count = (opcode & SPRITE_PACKET_COUNT_MASK) + 3;
                packet_mode = SPRITE_PACKET_REPEAT;
            } else if (opcode < SPRITE_BACKREF_LIMIT) {
                unsigned back;
                pixel_count = (opcode & SPRITE_PACKET_COUNT_MASK) + 4;
                packet_mode = SPRITE_PACKET_LITERAL;
                if (end - cursor < 2) {
                    return 0;
                }
                back = sprite_read_u16_le(sprite->data + cursor);
                cursor += 2;
                if (back > cursor) {
                    return 0;
                }
                source_offset = cursor - back;
                if (pixel_count > sprite->size - source_offset) {
                    return 0;
                }
            } else if (opcode < SPRITE_PAIR_LIMIT) {
                pixel_count = ((opcode & SPRITE_PACKET_COUNT_MASK) + 2) * 2;
                packet_mode = SPRITE_PACKET_PAIR;
            } else if (opcode < SPRITE_SKIP_LIMIT) {
                x += (int)(opcode & SPRITE_PACKET_COUNT_MASK) + 1;
                continue;
            } else {
                pixel_count = (opcode & 7) + 3;
                packet_mode = SPRITE_PACKET_DELTA;
            }
            if (packet_mode == SPRITE_PACKET_LITERAL && opcode < SPRITE_LITERAL_LIMIT) {
                if (pixel_count > end - cursor) {
                    return 0;
                }
                source_offset = cursor;
                cursor += pixel_count;
            }
            if (packet_mode == SPRITE_PACKET_REPEAT || packet_mode == SPRITE_PACKET_DELTA) {
                if (cursor >= end) {
                    return 0;
                }
                first_color = sprite->data[cursor++];
            }
            if (packet_mode == SPRITE_PACKET_PAIR) {
                if (end - cursor < 2) {
                    return 0;
                }
                first_color = sprite->data[cursor++];
                second_color = sprite->data[cursor++];
            }
            for (pixel_index = 0; pixel_index < pixel_count; pixel_index++, x++) {
                static const int delta[16] = {0, 1,  1,  1,  2,  2,  3,  3,
                                              0, -1, -1, -1, -2, -2, -3, -3};
                uint8_t pixel = packet_mode == SPRITE_PACKET_LITERAL
                                    ? sprite->data[source_offset + pixel_index]
                                : packet_mode == SPRITE_PACKET_PAIR
                                    ? (pixel_index & 1 ? second_color : first_color)
                                    : first_color;
                int yy = offset_y + origin_y + y;
                if (x >= 0 && x < XEEN_WIDTH_PIXELS && yy >= (int)top_row && yy < (int)bottom_row) {
                    pixels[yy * XEEN_WIDTH_PIXELS + x] = pixel;
                }
                if (packet_mode == SPRITE_PACKET_DELTA) {
                    first_color =
                        (uint8_t)(first_color + delta[((opcode >> 3) & 7) * 2 + (pixel_index & 1)]);
                }
            }
            if (x > 65535) {
                return 0;
            }
        }
    }
    return 1;
}

/* Advance the repeating scale mask, including positions with transparent pixels. */
static unsigned sprite_advance_scale_mask(uint16_t *mask)
{
    unsigned bit = *mask >> 15;
    *mask = (uint16_t)((*mask << 1) | bit);
    return bit;
}

/* Count retained horizontal pixels while advancing the borrowed scale mask.
 * Only complete groups of eight contribute the high byte; partial groups
 * return the low byte even for wide extents.
 */
static unsigned sprite_count_horizontal_pixels(uint16_t *mask, unsigned n)
{
    unsigned i;
    unsigned total = 0;
    for (i = 0; i < n; i++) {
        total += sprite_advance_scale_mask(mask);
    }
    return n % 8 ? total & 255 : total;
}

static unsigned sprite_count_vertical_pixels(uint16_t *mask, unsigned n)
{
    unsigned i;
    unsigned total = 0;
    for (i = 0; i < n; i++) {
        total += sprite_advance_scale_mask(mask);
    }
    return total & 255;
}

/* Decode one scaled sprite cell at caller-supplied offsets into borrowed indexed pixels.
 * Transparent skips advance the repeating scale masks just like visible pixels. Return one on
 * success, zero for malformed packets or bounds; partial drawing remains on error.
 */
static int sprite_decode_scaled_cell(const Blob *sprite, size_t cell_offset, int offset_x,
                                     int offset_y, unsigned scale, uint8_t *pixels)
{
    static const uint16_t masks[16] = {0xffff, 0xffef, 0xefef, 0xefee, 0xeeee, 0xeeae,
                                       0xaeae, 0xaeaa, 0xaaaa, 0xaa8a, 0x8a8a, 0x8a88,
                                       0x8888, 0x8880, 0x8080, 0x8000};
    static const int delta[16] = {0, 1, 1, 1, 2, 2, 3, 3, 0, -1, -1, -1, -2, -2, -3, -3};
    size_t cursor = cell_offset;
    unsigned xo;
    unsigned yo;
    unsigned width;
    unsigned height;
    unsigned row = 0;
    uint16_t hm;
    uint16_t vm;
    uint16_t phase;
    int origin_x;
    int clip_y;
    int destination_offset;
    int saved;
    if (cursor > sprite->size || sprite->size - cursor < SPRITE_CELL_HEADER_BYTES) {
        return 0;
    }
    xo = sprite_read_u16_le(sprite->data + cursor);
    width = sprite_read_u16_le(sprite->data + cursor + 2);
    yo = sprite_read_u16_le(sprite->data + cursor + 4);
    height = sprite_read_u16_le(sprite->data + cursor + 6);
    cursor += SPRITE_CELL_HEADER_BYTES;
    if (height > XEEN_HEIGHT_PIXELS || width > XEEN_WIDTH_PIXELS || xo > XEEN_WIDTH_PIXELS ||
        yo > XEEN_HEIGHT_PIXELS) {
        return 0;
    }
    hm = vm = masks[scale];
    origin_x = offset_x + (int)sprite_count_horizontal_pixels(&hm, xo);
    phase = hm;
    origin_x += (int)((width - sprite_count_horizontal_pixels(&hm, width)) / 2);
    clip_y = offset_y + (int)sprite_count_vertical_pixels(&vm, yo);
    destination_offset = clip_y * XEEN_WIDTH_PIXELS + origin_x;
    saved = destination_offset + XEEN_WIDTH_PIXELS;
    while (row < height) {
        unsigned length;
        size_t end;
        int write_row;
        int x;
        uint16_t mask;
        if (cursor >= sprite->size) {
            return 0;
        }
        length = sprite->data[cursor++];
        row++;
        if (!length) {
            unsigned pixel_count;
            unsigned pixel_index;
            if (cursor >= sprite->size) {
                return 0;
            }
            pixel_count = sprite->data[cursor++];
            if (pixel_count > height - row) {
                return 0;
            }
            row += pixel_count;
            pixel_index = sprite_count_vertical_pixels(&vm, pixel_count + 1);
            clip_y += 1 + (int)pixel_index;
            saved += (int)pixel_index * XEEN_WIDTH_PIXELS;
            destination_offset = saved;
            continue;
        }
        if (length > sprite->size - cursor) {
            return 0;
        }
        end = cursor + length;
        write_row = (int)sprite_advance_scale_mask(&vm);
        if (!write_row) {
            cursor = end;
            continue;
        }
        write_row = clip_y >= 0 && clip_y < XEEN_HEIGHT_PIXELS;
        clip_y++;
        if (!write_row) {
            cursor = end;
            destination_offset = saved;
            saved += XEEN_WIDTH_PIXELS;
            continue;
        }
        mask = phase;
        x = destination_offset + (int)sprite_count_horizontal_pixels(&mask, sprite->data[cursor++]);
        while (cursor < end) {
            unsigned opcode = sprite->data[cursor++];
            unsigned pixel_count;
            unsigned pixel_index;
            size_t source_offset = 0;
            uint8_t first_color = 0;
            uint8_t second_color = 0;
            int packet_mode;
            if (opcode < SPRITE_LITERAL_LIMIT) {
                pixel_count = opcode + 1;
                packet_mode = SPRITE_PACKET_LITERAL;
                if (pixel_count > end - cursor) {
                    return 0;
                }
                source_offset = cursor;
                cursor += pixel_count;
            } else if (opcode < SPRITE_REPEAT_LIMIT) {
                pixel_count = (opcode & SPRITE_PACKET_COUNT_MASK) + 3;
                packet_mode = SPRITE_PACKET_REPEAT;
                if (cursor >= end) {
                    return 0;
                }
                first_color = sprite->data[cursor++];
            } else if (opcode < SPRITE_BACKREF_LIMIT) {
                unsigned back;
                pixel_count = (opcode & SPRITE_PACKET_COUNT_MASK) + 4;
                packet_mode = SPRITE_PACKET_LITERAL;
                if (end - cursor < 2) {
                    return 0;
                }
                back = sprite_read_u16_le(sprite->data + cursor);
                cursor += 2;
                if (back > cursor) {
                    return 0;
                }
                source_offset = cursor - back;
                if (pixel_count > sprite->size - source_offset) {
                    return 0;
                }
            } else if (opcode < SPRITE_PAIR_LIMIT) {
                pixel_count = ((opcode & SPRITE_PACKET_COUNT_MASK) + 2) * 2;
                packet_mode = SPRITE_PACKET_PAIR;
                if (end - cursor < 2) {
                    return 0;
                }
                first_color = sprite->data[cursor++];
                second_color = sprite->data[cursor++];
            } else if (opcode < SPRITE_SKIP_LIMIT) {
                x += (int)sprite_count_horizontal_pixels(&mask,
                                                         (opcode & SPRITE_PACKET_COUNT_MASK) + 1);
                continue;
            } else {
                pixel_count = (opcode & 7) + 3;
                packet_mode = SPRITE_PACKET_DELTA;
                if (cursor >= end) {
                    return 0;
                }
                first_color = sprite->data[cursor++];
            }
            for (pixel_index = 0; pixel_index < pixel_count; pixel_index++) {
                uint8_t pixel = packet_mode == SPRITE_PACKET_LITERAL
                                    ? sprite->data[source_offset + pixel_index]
                                : packet_mode == SPRITE_PACKET_PAIR
                                    ? (pixel_index & 1 ? second_color : first_color)
                                    : first_color;
                if (sprite_advance_scale_mask(&mask)) {
                    if (write_row && x >= 0 && x < XEEN_PIXELS &&
                        x - (destination_offset - origin_x) >= 0 &&
                        x - (destination_offset - origin_x) < XEEN_WIDTH_PIXELS) {
                        pixels[x] = pixel;
                    }
                    x++;
                }
                if (packet_mode == SPRITE_PACKET_DELTA) {
                    first_color =
                        (uint8_t)(first_color + delta[((opcode >> 3) & 7) * 2 + (pixel_index & 1)]);
                }
            }
        }
        destination_offset = saved;
        saved += XEEN_WIDTH_PIXELS;
    }
    return 1;
}

/* Draw one frame at the supplied signed offsets and scale-table index into borrowed pixels.
 * The sprite remains caller-owned. Return one on success, zero for invalid coordinates, scale,
 * frame or packet data; partial drawing can remain on error.
 */
int sprite_draw_scaled(const Blob *sprite, unsigned frame, int x, int y, unsigned scale,
                       uint8_t *pixels)
{
    size_t pos;
    unsigned i;
    if (scale > SPRITE_MAX_SCALE_INDEX || x < -32768 || x > 32767 || y < -32768 || y > 32767) {
        return 0;
    }
    if (sprite->size < 2 || frame >= sprite_read_u16_le(sprite->data)) {
        return 0;
    }
    if ((size_t)sprite_read_u16_le(sprite->data) > (sprite->size - 2) / 4) {
        return 0;
    }
    pos = 2 + (size_t)frame * 4;
    for (i = 0; i < 2; i++) {
        unsigned at = sprite_read_u16_le(sprite->data + pos + i * 2);
        if (i == 1 && !at) {
            continue;
        }
        if (scale ? !sprite_decode_scaled_cell(sprite, at, x, y, scale, pixels)
                  : !sprite_decode_cell(sprite, at, x, y, 0, 0, XEEN_HEIGHT_PIXELS, pixels)) {
            return 0;
        }
    }
    return 1;
}

/* Draw one unscaled frame at its stored origin into borrowed indexed pixels. Return one on
 * success or zero for a malformed sprite or frame; an error can leave partial drawing.
 */
int sprite_draw(const Blob *sprite, unsigned frame, uint8_t *pixels)
{
    return sprite_draw_scaled(sprite, frame, 0, 0, 0, pixels);
}

/* Draw a normal sprite or the clipped cinematic effect into borrowed pixels. Nonzero effect
 * flags accept only the supported clipping effect with scale zero. Return one on success, zero
 * for unsupported inputs or malformed data; errors do not roll back drawing.
 */
int sprite_draw_effect(const Blob *sprite, unsigned frame, int x, int y, unsigned scale,
                       unsigned flags, uint8_t *pixels)
{
    size_t pos;
    unsigned i;
    if (!flags) {
        return sprite_draw_scaled(sprite, frame, x, y, scale, pixels);
    }
    if (flags != SPRITE_CLIPPED_EFFECT || scale || x < -32768 || x > 32767 || y < -32768 ||
        y > 32767) {
        return 0;
    }
    if (sprite->size < 2 || frame >= sprite_read_u16_le(sprite->data) ||
        (size_t)sprite_read_u16_le(sprite->data) > (sprite->size - 2) / 4) {
        return 0;
    }
    pos = 2 + (size_t)frame * 4;
    for (i = 0; i < 2; i++) {
        unsigned at = sprite_read_u16_le(sprite->data + pos + i * 2);
        if (i == 1 && !at) {
            continue;
        }
        if (!sprite_decode_cell(sprite, at, x, y, 1, 0, XEEN_HEIGHT_PIXELS, pixels)) {
            return 0;
        }
    }
    return 1;
}

/* Draw one frame inside the ending window, from row 17 to the caller-supplied bottom. Borrow
 * sprite and destination storage. Return one on success, zero on invalid frame or data;
 * partially drawn pixels remain on error.
 */
int sprite_draw_windowed(const Blob *sprite, unsigned frame, int x, int y, unsigned bottom_row,
                         uint8_t *pixels)
{
    size_t pos;
    unsigned i;
    if (bottom_row > XEEN_HEIGHT_PIXELS || bottom_row < SPRITE_WINDOW_TOP_ROW || x < -32768 ||
        x > 32767 || y < -32768 || y > 32767) {
        return 0;
    }
    if (sprite->size < 2 || frame >= sprite_read_u16_le(sprite->data) ||
        (size_t)sprite_read_u16_le(sprite->data) > (sprite->size - 2) / 4) {
        return 0;
    }
    pos = 2 + (size_t)frame * 4;
    for (i = 0; i < 2; i++) {
        unsigned at = sprite_read_u16_le(sprite->data + pos + i * 2);
        if (i == 1 && !at) {
            continue;
        }
        /* Check the destination row before reading pixels. A World ending cell has
         * a non-image header, but all its rows remain below this window without
         * wrapping. Its unused packet bytes therefore have no visible effect.
         */
        if (at <= sprite->size && sprite->size - at >= 8) {
            unsigned first =
                (unsigned)(y + (int)sprite_read_u16_le(sprite->data + at + 4)) & 65535u;
            unsigned rows = sprite_read_u16_le(sprite->data + at + 6);
            if (first >= bottom_row && first + rows <= 65536u) {
                continue;
            }
        }
        if (!sprite_decode_cell(sprite, at, x, y, 0, SPRITE_WINDOW_TOP_ROW, bottom_row, pixels)) {
            return 0;
        }
    }
    return 1;
}
