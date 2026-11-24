/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Render the game font for captions, Clouds credits and ending pages. text_draw and the
 * ending-context entry points lay out borrowed text into indexed pixels; subtitles and
 * ending_pages supply their text and alignment state.
 */
#include "text.h"
#include <string.h>

#define TEXT_FONT_BYTES 4352
#define TEXT_WIDTH_TABLE_OFFSET_BYTES 4096
#define TEXT_GLYPH_BYTES 16
#define TEXT_GLYPH_ROWS 8
#define TEXT_GLYPH_COLUMNS 8
#define TEXT_SPACE_ADVANCE_PIXELS 4
#define TEXT_INPUT_CHARACTER_LIMIT 1000
#define TEXT_PAGE_CHARACTER_LIMIT 16384
#define TEXT_LINE_ADVANCE_PIXELS 10
#define TEXT_LAST_ROW_OFFSET_PIXELS 9
#define TEXT_CENTER_SPAN_PIXELS 321
#define TEXT_SUBTITLE_COLOR_INDEX 198
#define TEXT_FIRST_PRINTABLE_CHARACTER 32
#define TEXT_CHARACTER_MASK 127
#define TEXT_SUBTITLE_COLOR_CODE 35

enum {
    TEXT_CONTROL_ALIGN = 3,
    TEXT_CONTROL_X = 9,
    TEXT_CONTROL_NEWLINE = 10,
    TEXT_CONTROL_Y = 11,
    TEXT_CONTROL_COLOR = 12,
    TEXT_CONTROL_ERROR = 0,
    TEXT_CONTROL_APPLIED = 1,
    TEXT_CONTROL_PAGE_DONE = 2
};

/* Draw the nontransparent pixels of one normal-font glyph with its descender offset and
 * caller-supplied color. Font, clip and indexed destination are borrowed and must be valid.
 * Writes are clipped to both screen and text bounds; no status is returned.
 */
static void text_draw_glyph(const Blob *font, unsigned character, int x, int y, uint8_t color_index,
                            const TextClip *clip, uint8_t *pixels)
{
    unsigned row;
    unsigned column;
    int descender_offset =
        character == 'g' || character == 'p' || character == 'q' || character == 'y';
    for (row = 0; row < TEXT_GLYPH_ROWS; row++) {
        size_t glyph_offset = character * TEXT_GLYPH_BYTES + row * 2;
        unsigned bits = font->data[glyph_offset] | (unsigned)font->data[glyph_offset + 1] << 8;
        for (column = 0; column < TEXT_GLYPH_COLUMNS; column++) {
            int pixel_x = x + (int)column;
            int pixel_y = y + (int)row + descender_offset;
            if (((bits >> (column * 2)) & 3) && pixel_x >= 0 && pixel_x < XEEN_WIDTH_PIXELS &&
                pixel_y >= 0 && pixel_y < XEEN_HEIGHT_PIXELS && pixel_x >= clip->x &&
                pixel_y >= clip->y && (int64_t)pixel_x < (int64_t)clip->x + clip->width &&
                (int64_t)pixel_y < (int64_t)clip->y + clip->height) {
                pixels[pixel_y * XEEN_WIDTH_PIXELS + pixel_x] = color_index;
            }
        }
    }
}

/* Consume a fixed-width decimal text control from the borrowed cursor, treating spaces as zero
 * digits. Write the parsed output and return one on success; return zero for truncation or a
 * nondigit. A failed parse can still advance the cursor.
 */
static int text_parse_digits(const unsigned char **cursor, unsigned digit_count, int *value)
{
    unsigned digit_index;
    int parsed_value = 0;
    for (digit_index = 0; digit_index < digit_count; digit_index++) {
        unsigned character = (**cursor) & TEXT_CHARACTER_MASK;
        if (!character) {
            return 0;
        }
        (*cursor)++;
        if (character == ' ') {
            character = '0';
        }
        if (character < '0' || character > '9') {
            return 0;
        }
        parsed_value = parsed_value * 10 + (int)character - '0';
    }
    *value = parsed_value;
    return 1;
}

/* Draw the supported cinematic control prefix and caption text through a borrowed font and
 * clip rectangle. All buffers remain caller-owned. Return one on success or zero for malformed
 * font, controls or arguments; drawing is not rolled back on error.
 */
int text_draw(const Blob *font, const char *formatted, const TextClip *clip, uint8_t *pixels)
{
    const unsigned char *cursor = (const unsigned char *)formatted;
    const unsigned char *text_start;
    int x = 0;
    int y = 0;
    int centered = 0;
    int has_color = 0;
    int text_width = 0;
    int control_value;
    size_t index;
    size_t text_length;
    if (!font || !font->data || font->size != TEXT_FONT_BYTES || !formatted || !pixels || !clip ||
        clip->width < 0 || clip->height < 0) {
        return 0;
    }
    /* Cinematics need only this control subset and the normal font bank.
     * Reject unsupported controls rather than inventing their layout.
     */
    while (*cursor && ((*cursor & TEXT_CHARACTER_MASK) < 32)) {
        unsigned character = *cursor++ & TEXT_CHARACTER_MASK;
        switch (character) {
        case TEXT_CONTROL_COLOR:
            if (!text_parse_digits(&cursor, 2, &control_value) ||
                control_value != TEXT_SUBTITLE_COLOR_CODE) {
                return 0;
            }
            has_color = 1;
            break;
        case TEXT_CONTROL_ALIGN:
            if ((*cursor++ & TEXT_CHARACTER_MASK) != 'c') {
                return 0;
            }
            centered = 1;
            break;
        case TEXT_CONTROL_Y:
            if (!text_parse_digits(&cursor, 3, &y)) {
                return 0;
            }
            break;
        case TEXT_CONTROL_X:
            if (!text_parse_digits(&cursor, 3, &x)) {
                return 0;
            }
            break;
        default:
            return 0;
        }
    }
    if (!has_color) {
        return 0;
    }
    text_start = cursor;
    text_length = strlen((const char *)text_start);
    /* Captions show at most 40 characters. The larger safety bound also
     * prevents coordinate overflow for malformed callers.
     */
    if (text_length > TEXT_INPUT_CHARACTER_LIMIT) {
        return 0;
    }
    for (index = 0; index < text_length; index++) {
        unsigned character = text_start[index] & TEXT_CHARACTER_MASK;
        if (character < TEXT_FIRST_PRINTABLE_CHARACTER) {
            return 0;
        }
        text_width += character == TEXT_FIRST_PRINTABLE_CHARACTER
                          ? TEXT_SPACE_ADVANCE_PIXELS
                          : font->data[TEXT_WIDTH_TABLE_OFFSET_BYTES + character];
    }
    if (centered) {
        x = ((x ? 2 * x : TEXT_CENTER_SPAN_PIXELS) - text_width + 1) / 2;
    }
    for (index = 0; index < text_length; index++) {
        unsigned character = text_start[index] & TEXT_CHARACTER_MASK;
        if (character == TEXT_FIRST_PRINTABLE_CHARACTER) {
            x += TEXT_SPACE_ADVANCE_PIXELS;
            continue;
        }
        /* Glyph shade zero is transparent; subtitle color 35 maps other shades to 198. */
        text_draw_glyph(font, character, x, y, TEXT_SUBTITLE_COLOR_INDEX, clip, pixels);
        x += font->data[TEXT_WIDTH_TABLE_OFFSET_BYTES + character];
    }
    return 1;
}

/* Draw the five fixed Clouds credit labels through the borrowed normal font into caller-owned
 * pixels. Return one on success or zero for an invalid font or destination.
 */
int text_draw_clouds_credits(const Blob *font, uint8_t *pixels)
{
    /* Credits use fixed positions, centered alignment and color zero. Keep this
     * narrow layout separate from the bounded subtitle control parser.
     */
    static const struct {
        const char *text;
        int center;
        int y;
    } labels[] = {{"King Burlock", 40, 82},
                  {"Peasants", 40, 190},
                  {"Lord Xeen", 247, 82},
                  {"Xeen's Pet", 258, 190},
                  {"Crodo", 150, 179}};

    static const TextClip screen = {0, 0, XEEN_WIDTH_PIXELS, XEEN_HEIGHT_PIXELS};
    size_t i;
    size_t j;
    if (!font || !font->data || font->size != TEXT_FONT_BYTES || !pixels) {
        return 0;
    }
    for (i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        const unsigned char *s = (const unsigned char *)labels[i].text;
        int width = 0;
        int x;
        for (j = 0; s[j]; j++) {
            width += s[j] == ' ' ? TEXT_SPACE_ADVANCE_PIXELS
                                 : font->data[TEXT_WIDTH_TABLE_OFFSET_BYTES + s[j]];
        }
        /* Explicitly positioned centered text rounds its origin upward. */
        x = (2 * labels[i].center - width + 1) / 2;
        for (j = 0; s[j]; j++) {
            if (s[j] == ' ') {
                x += TEXT_SPACE_ADVANCE_PIXELS;
                continue;
            }
            text_draw_glyph(font, s[j], x, labels[i].y, 25, &screen, pixels);
            x += font->data[TEXT_WIDTH_TABLE_OFFSET_BYTES + s[j]];
        }
    }
    return 1;
}

/* Format a centered caption in the subtitle color and draw it into borrowed pixels. The font
 * and text remain caller-owned. Return one on success or zero for invalid inputs, an oversized
 * formatted string or unsupported text.
 */
int text_draw_subtitle(const Blob *font, const char *text, uint8_t *pixels)
{
    char formatted[80];
    size_t text_length;
    static const TextClip screen = {0, 0, XEEN_WIDTH_PIXELS, XEEN_HEIGHT_PIXELS};
    static const char prefix[] = "\01435\003c\013190\011000";
    if (!text || (text_length = strlen(text)) > 40) {
        return 0;
    }
    memcpy(formatted, prefix, sizeof(prefix) - 1);
    memcpy(formatted + sizeof(prefix) - 1, text, text_length + 1);
    return text_draw(font, formatted, &screen, pixels);
}

/* Ending pages add wrapping and persistent alignment to the normal font renderer. */
static int text_glyph_advance(const Blob *font, unsigned character)
{
    return character == ' ' ? TEXT_SPACE_ADVANCE_PIXELS
                            : font->data[TEXT_WIDTH_TABLE_OFFSET_BYTES + character];
}

typedef struct {
    const unsigned char *end;
    int width;
    int wrapped;
} TextMeasuredLine;

/* Measure the next ending-page span using borrowed font and text bytes, including wrapping and
 * color controls. Fill the caller-owned line result; return zero for malformed controls. The
 * text cursor itself is not advanced.
 */
static int text_measure_page_line(const Blob *font, const unsigned char *cursor, int line_left,
                                  int right, TextMeasuredLine *line)
{
    const unsigned char *measure_cursor = cursor;
    const unsigned char *line_end;
    const unsigned char *last_blank = NULL;
    int line_width = 0;
    int wrap_line = 0;
    /* Color controls participate in measurement; other controls end the span.
     * Alignment still applies when the measured span is empty.
     */
    while (*measure_cursor) {
        unsigned character = *measure_cursor & TEXT_CHARACTER_MASK;
        if (character == TEXT_CONTROL_COLOR) {
            measure_cursor++;
            if (*measure_cursor == 'd') {
                measure_cursor++;
            } else {
                if (!measure_cursor[0] || !measure_cursor[1]) {
                    return 0;
                }
                measure_cursor += 2;
            }
            continue;
        }
        if (character < TEXT_FIRST_PRINTABLE_CHARACTER) {
            break;
        }
        line_width += text_glyph_advance(font, character);
        if (character == ' ') {
            last_blank = measure_cursor;
        }
        if (line_left + line_width >= right) {
            wrap_line = 1;
            break;
        }
        measure_cursor++;
    }
    line_end = measure_cursor;
    if (wrap_line && last_blank && last_blank > cursor) {
        line_end = last_blank;
        while (line_end > cursor && line_end[-1] == ' ') {
            line_end--;
        }
    }
    /* The measured printable range excludes a boundary control. */
    line_width = 0;
    for (measure_cursor = cursor; measure_cursor < line_end;) {
        unsigned character = *measure_cursor++ & TEXT_CHARACTER_MASK;
        if (character == TEXT_CONTROL_COLOR) {
            if (*measure_cursor == 'd') {
                measure_cursor++;
            } else {
                measure_cursor += 2;
            }
        } else if (character >= TEXT_FIRST_PRINTABLE_CHARACTER) {
            line_width += text_glyph_advance(font, character);
        } else {
            break;
        }
    }
    line->end = line_end;
    line->width = line_width;
    line->wrapped = wrap_line;
    return 1;
}

/* Apply one page control to caller-owned cursor, position, color and alignment state. Return
 * zero for malformed or unsupported controls, one to continue, or the page-done result at the
 * bottom boundary. No storage ownership changes.
 */
static int text_apply_page_control(unsigned character, const unsigned char **cursor, int *x, int *y,
                                   unsigned *color, int *center, int left, int top, int right,
                                   int bottom)
{
    unsigned control_value = 0;
    unsigned digit_count = 0;
    unsigned digit_index;
    if (character == TEXT_CONTROL_ALIGN) {
        if (!*(*cursor)) {
            return TEXT_CONTROL_ERROR;
        }
        *center = *(*cursor)++ == 'c';
        return TEXT_CONTROL_APPLIED;
    }
    if (character == TEXT_CONTROL_NEWLINE) {
        (*x) = left;
        (*y) += TEXT_LINE_ADVANCE_PIXELS;
        while (*(*cursor) == ' ') {
            (*cursor)++;
        }
        if ((*y) + TEXT_LAST_ROW_OFFSET_PIXELS > bottom) {
            return TEXT_CONTROL_PAGE_DONE;
        }
        return TEXT_CONTROL_APPLIED;
    }
    if (character == TEXT_CONTROL_X || character == TEXT_CONTROL_Y) {
        digit_count = 3;
    } else if (character == TEXT_CONTROL_COLOR) {
        if (*(*cursor) == 'd') {
            (*cursor)++;
            (*color) = 25;
            return TEXT_CONTROL_APPLIED;
        }
        digit_count = 2;
    } else {
        return TEXT_CONTROL_ERROR;
    }
    for (digit_index = 0; digit_index < digit_count; digit_index++) {
        unsigned digit = *(*cursor)++;
        if (digit == ' ') {
            digit = '0';
        }
        if (digit < '0' || digit > '9') {
            return TEXT_CONTROL_ERROR;
        }
        control_value = control_value * 10 + digit - '0';
    }
    if (character == TEXT_CONTROL_X) {
        (*x) = left + (int)control_value > right ? right : left + (int)control_value;
    } else if (character == TEXT_CONTROL_Y) {
        (*y) = top + (int)control_value > bottom ? bottom : top + (int)control_value;
    } else {
        if (control_value == 4) {
            (*color) = 1;
        } else if (control_value == 5) {
            (*color) = 31;
        } else if (control_value == 11) {
            (*color) = 66;
        } else if (control_value == 38) {
            (*color) = 25;
        } else if (control_value == 39) {
            (*color) = 49;
        } else {
            return TEXT_CONTROL_ERROR;
        }
    }
    return TEXT_CONTROL_APPLIED;
}

/* Lay out an ending page within the supplied bounds and draw through the borrowed font into
 * pixels. Read and update the caller-owned center-alignment state across controls. Return one
 * at completion or the bottom boundary; return zero for malformed inputs or unsupported
 * controls, retaining any partial drawing.
 */
int text_draw_ending_context_state(const Blob *font, const char *body, int left, int top, int right,
                                   int bottom, int start_x, int start_y, unsigned initial,
                                   uint8_t *pixels, int *center)
{
    const unsigned char *cursor = (const unsigned char *)body;
    int x = start_x;
    int y = start_y;
    unsigned color = initial;
    size_t parsed_characters = 0;
    static const TextClip screen = {0, 0, XEEN_WIDTH_PIXELS, XEEN_HEIGHT_PIXELS};
    if (!font || !font->data || font->size != TEXT_FONT_BYTES || !cursor || !pixels || !center) {
        return 0;
    }
    while (*cursor) {
        TextMeasuredLine line;
        int line_left = *center ? left : x;
        if (!text_measure_page_line(font, cursor, line_left, right, &line)) {
            return 0;
        }
        if (*center) {
            x = ((x == left ? left + right + 1 : 2 * x) - line.width + 1) / 2;
        }
        while (cursor < line.end || (!line.wrapped && cursor == line.end)) {
            unsigned character = *cursor++ & TEXT_CHARACTER_MASK;
            if (++parsed_characters > TEXT_PAGE_CHARACTER_LIMIT) {
                return 0;
            }
            if (!character) {
                return 1;
            }
            if (character >= TEXT_FIRST_PRINTABLE_CHARACTER) {
                if (character != ' ') {
                    text_draw_glyph(font, character, x, y, (uint8_t)color, &screen, pixels);
                }
                x += text_glyph_advance(font, character);
                continue;
            }
            int control_result = text_apply_page_control(character, &cursor, &x, &y, &color, center,
                                                         left, top, right, bottom);
            if (!control_result) {
                return 0;
            }
            if (control_result == TEXT_CONTROL_PAGE_DONE) {
                return 1;
            }
        }
        if (line.wrapped) {
            if (cursor == line.end && !*cursor) {
                return 0;
            }
            while (*cursor == ' ') {
                cursor++;
            }
            x = left;
            y += TEXT_LINE_ADVANCE_PIXELS;
            if (y + TEXT_LAST_ROW_OFFSET_PIXELS > bottom) {
                return 1;
            }
        }
    }
    return 1;
}

/* Draw an ending page starting left-aligned, with the supplied bounds, position and color. All
 * inputs and pixels are borrowed. Return the stateful renderer's success result; zero can
 * leave partial drawing.
 */
int text_draw_ending_context(const Blob *font, const char *body, int left, int top, int right,
                             int bottom, int startx, int starty, unsigned initial, uint8_t *pixels)
{
    int center = 0;
    return text_draw_ending_context_state(font, body, left, top, right, bottom, startx, starty,
                                          initial, pixels, &center);
}
