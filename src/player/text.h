/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Render the game font for captions, Clouds credits and ending pages. text_draw and the
 * ending-context entry points lay out borrowed text into indexed pixels; subtitles and
 * ending_pages supply their text and alignment state.
 */
#ifndef XEEN_TEXT_H
#define XEEN_TEXT_H
#include "xeen.h"

typedef struct {
    int x;
    int y;
    int width;
    int height;
} TextClip;

/* Draw the supported cinematic control prefix and caption text through a borrowed font and
 * clip rectangle. All buffers remain caller-owned. Return one on success or zero for malformed
 * font, controls or arguments; drawing is not rolled back on error.
 */
int text_draw(const Blob *font, const char *formatted, const TextClip *clip, uint8_t *pixels);
/* Format a centered caption in the subtitle color and draw it into borrowed pixels. The font
 * and text remain caller-owned. Return one on success or zero for invalid inputs, an oversized
 * formatted string or unsupported text.
 */
int text_draw_subtitle(const Blob *font, const char *text, uint8_t *pixels);
/* Draw the five fixed Clouds credit labels through the borrowed normal font into caller-owned
 * pixels. Return one on success or zero for an invalid font or destination.
 */
int text_draw_clouds_credits(const Blob *font, uint8_t *pixels);
/* Draw an ending page starting left-aligned, with the supplied bounds, position and color. All
 * inputs and pixels are borrowed. Return the stateful renderer's success result; zero can
 * leave partial drawing.
 */
int text_draw_ending_context(const Blob *font, const char *body, int left, int top, int right,
                             int bottom, int x, int y, unsigned initial_color, uint8_t *pixels);
/* Lay out an ending page within the supplied bounds and draw through the borrowed font into
 * pixels. Read and update the caller-owned center-alignment state across controls. Return one
 * at completion or the bottom boundary; return zero for malformed inputs or unsupported
 * controls, retaining any partial drawing.
 */
int text_draw_ending_context_state(const Blob *font, const char *body, int left, int top, int right,
                                   int bottom, int x, int y, unsigned initial_color,
                                   uint8_t *pixels, int *center);
#endif
