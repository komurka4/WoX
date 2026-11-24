/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Hash byte buffers and streams for resource identities and diagnostic frame comparisons. The
 * xeen_sha256 entry points use caller-owned state or digest storage; runtime and synth helpers
 * decide what to hash.
 */
#ifndef XEEN_SHA256_H
#define XEEN_SHA256_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} XeenSha256;

/* Initialize caller-owned digest state. Requires nonnull storage and allocates nothing. */
void xeen_sha256_init(XeenSha256 *s);
/* Consume borrowed bytes into initialized caller-owned digest state. Updates the partial block
 * and byte count; the caller keeps ownership of input storage.
 */
void xeen_sha256_update(XeenSha256 *s, const void *data, size_t size);
/* Finalize initialized digest state into the caller's digest buffer, then clear the entire
 * state. Requires valid buffers and allocates nothing.
 */
void xeen_sha256_finish(XeenSha256 *s, uint8_t digest[32]);
/* Hash borrowed bytes into the caller's digest buffer using temporary stack state. Allocates
 * nothing and does not retain the input.
 */
void xeen_sha256(const void *data, size_t size, uint8_t digest[32]);
/* Hash from the borrowed stream's current position through EOF into caller output. Does not
 * rewind or close the stream. Returns 1 on success or 0 for null inputs or a read error;
 * output is unchanged on failure.
 */
int xeen_sha256_stream(FILE *stream, uint8_t digest[32]);
#endif
