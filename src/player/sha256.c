/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Hash byte buffers and streams for resource identities and diagnostic frame comparisons. The
 * xeen_sha256 entry points use caller-owned state or digest storage; runtime and synth helpers
 * decide what to hash.
 */
#include "sha256.h"
#include <string.h>

static uint32_t sha256_rotate_right(uint32_t x, unsigned n)
{
    return x >> n | x << (32 - n);
}

static uint32_t sha256_read_u32_be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Compress one complete borrowed digest block into caller-owned hash state. Requires a full
 * block and initialized state, allocates nothing and has no failure path.
 */
static void sha256_process_block(XeenSha256 *state, const uint8_t *p)
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    uint32_t a = state->h[0];
    uint32_t b = state->h[1];
    uint32_t c = state->h[2];
    uint32_t d = state->h[3];
    uint32_t e = state->h[4];
    uint32_t f = state->h[5];
    uint32_t g = state->h[6];
    uint32_t h = state->h[7];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = sha256_read_u32_be(p + 4 * i);
    }
    for (unsigned i = 16; i < 64; i++) {
        uint32_t x = w[i - 15];
        uint32_t y = w[i - 2];
        w[i] = w[i - 16] + (sha256_rotate_right(x, 7) ^ sha256_rotate_right(x, 18) ^ (x >> 3)) +
               w[i - 7] + (sha256_rotate_right(y, 17) ^ sha256_rotate_right(y, 19) ^ (y >> 10));
    }
    for (unsigned i = 0; i < 64; i++) {
        uint32_t t =
            h +
            (sha256_rotate_right(e, 6) ^ sha256_rotate_right(e, 11) ^ sha256_rotate_right(e, 25)) +
            ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t u =
            (sha256_rotate_right(a, 2) ^ sha256_rotate_right(a, 13) ^ sha256_rotate_right(a, 22)) +
            ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + u;
    }
    state->h[0] += a;
    state->h[1] += b;
    state->h[2] += c;
    state->h[3] += d;
    state->h[4] += e;
    state->h[5] += f;
    state->h[6] += g;
    state->h[7] += h;
}

/* Initialize caller-owned digest state. Requires nonnull storage and allocates nothing. */
void xeen_sha256_init(XeenSha256 *state)
{
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(state->h, initial, sizeof(initial));
    state->used = 0;
    state->bytes = 0;
}

/* Consume borrowed bytes into initialized caller-owned digest state. Updates the partial block
 * and byte count; the caller keeps ownership of input storage.
 */
void xeen_sha256_update(XeenSha256 *state, const void *data, size_t size)
{
    const uint8_t *p = data;
    state->bytes += (uint64_t)size;
    while (size) {
        size_t n = 64 - state->used;
        if (n > size) {
            n = size;
        }
        memcpy(state->block + state->used, p, n);
        state->used += n;
        p += n;
        size -= n;
        if (state->used == 64) {
            sha256_process_block(state, state->block);
            state->used = 0;
        }
    }
}

/* Finalize initialized digest state into the caller's digest buffer, then clear the entire
 * state. Requires valid buffers and allocates nothing.
 */
void xeen_sha256_finish(XeenSha256 *state, uint8_t digest[32])
{
    uint64_t bits = state->bytes * 8;
    state->block[state->used++] = 0x80;
    if (state->used > 56) {
        memset(state->block + state->used, 0, 64 - state->used);
        sha256_process_block(state, state->block);
        state->used = 0;
    }
    memset(state->block + state->used, 0, 56 - state->used);
    for (unsigned i = 0; i < 8; i++) {
        state->block[63 - i] = (uint8_t)(bits >> (8 * i));
    }
    sha256_process_block(state, state->block);
    for (unsigned i = 0; i < 8; i++) {
        for (unsigned j = 0; j < 4; j++) {
            digest[4 * i + j] = (uint8_t)(state->h[i] >> (24 - 8 * j));
        }
    }
    memset(state, 0, sizeof(*state));
}

/* Hash borrowed bytes into the caller's digest buffer using temporary stack state. Allocates
 * nothing and does not retain the input.
 */
void xeen_sha256(const void *data, size_t size, uint8_t digest[32])
{
    XeenSha256 state;
    xeen_sha256_init(&state);
    xeen_sha256_update(&state, data, size);
    xeen_sha256_finish(&state, digest);
}

/* Hash from the borrowed stream's current position through EOF into caller output. Does not
 * rewind or close the stream. Returns 1 on success or 0 for null inputs or a read error;
 * output is unchanged on failure.
 */
int xeen_sha256_stream(FILE *f, uint8_t digest[32])
{
    XeenSha256 state;
    uint8_t b[8192];
    size_t n;
    if (!f || !digest) {
        return 0;
    }
    xeen_sha256_init(&state);
    while ((n = fread(b, 1, sizeof(b), f))) {
        xeen_sha256_update(&state, b, n);
    }
    if (ferror(f)) {
        return 0;
    }
    xeen_sha256_finish(&state, digest);
    return 1;
}
