#ifndef XEEN_BYTE_ORDER_H
#define XEEN_BYTE_ORDER_H
/*
 * Little-endian access to unaligned bytes: the original's words and
 * dwords as they lie in its data segment, its resource files and its saves.
 * Every helper works on bytes, so the result never depends on the host's
 * byte order or alignment.
 */
#include <stdint.h>

/** Reads the 16-bit little-endian word at P. */
static inline uint16_t xeen_get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/** Writes V to P as a 16-bit little-endian word. */
static inline void xeen_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/** Reads the 32-bit little-endian dword at P. */
static inline uint32_t xeen_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** Writes V to P as a 32-bit little-endian dword. */
static inline void xeen_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/**
 * The original's 2F847: bit N of the bit array at DS offset BASE (0 or 1), most significant bit first; the byte
 * address BASE + N / 8 wraps at 64K like the original's offset.
 */
static inline int xeen_ds_get_bit(const uint8_t *ds, uint16_t base, unsigned n)
{
    return (ds[(uint16_t)(base + (n >> 3))] & (0x80u >> (n & 7u))) != 0;
}

/**
 * The original's 2F8F3: sets (ON nonzero) or clears bit N of the bit array at DS offset BASE, most
 * significant bit first; the byte address BASE + N / 8 wraps at 64K like the original's offset.
 */
static inline void xeen_ds_set_bit(uint8_t *ds, uint16_t base, unsigned n, int on)
{
    uint8_t mask = (uint8_t)(0x80u >> (n & 7u));
    uint16_t a = (uint16_t)(base + (uint16_t)(n >> 3));
    if (on) {
        ds[a] |= mask;
    } else {
        ds[a] &= (uint8_t)~mask;
    }
}

/** Interprets a 16-bit word as the original's signed (two's complement) value. */
static inline int16_t xeen_sign16(uint16_t v)
{
    return (int16_t)(v < 32768u ? (int)v : (int)v - 65536);
}

/** Interprets a byte as the original's signed value (CBW). */
static inline int16_t xeen_sign8(uint8_t b)
{
    return (int16_t)(b < 128u ? (int)b : (int)b - 256);
}

#endif
