/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Decode compressed cinematic background frames with an adaptive Huffman tree and sliding
 * dictionary. frame_decode writes caller-owned indexed pixels for scene loaders; it shares
 * Blob and screen types with xeen.h.
 */
#ifndef XEEN_FRAME_H
#define XEEN_FRAME_H
#include "xeen.h"
/* Decode a compressed background into the caller-owned destination of pixel_count bytes.
 * Return one after writing the requested number of pixels with a matching header; trailing
 * input is ignored. Return zero for invalid or truncated input, possibly after partial output.
 */
int frame_decode(const Blob *source, uint8_t *dst, size_t dst_size);
#endif
