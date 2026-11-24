/*
 * Archive directory and map record helpers used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/core/resources.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/resources.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>

/** Encrypts byte I of a .CC archive directory; the inverse of xeen_directory_decode. */
uint8_t xeen_directory_encode(uint8_t b, size_t i)
{
    uint8_t v = (uint8_t)((size_t)b - 0xacu - (i % 256u) * 0x67u);
    return (uint8_t)((unsigned)v >> 2 | (unsigned)v << 6);
}
xeen_resource_status xeen_map_decode(const uint8_t *p, size_t n, xeen_map_record *r)
{
    if (!p || !r) {
        return XR_BAD_INPUT;
    }
    if (n != sizeof(r->bytes)) {
        return XR_BAD_MAP;
    }
    memcpy(r->bytes, p, n);
    return XR_OK;
}
uint16_t xeen_map_tile(const xeen_map_record *r, unsigned x, unsigned y)
{
    return x < 16 && y < 16 ? xeen_get_le16(r->bytes + y * 32 + x * 2) : 0;
}
uint8_t xeen_map_metadata(const xeen_map_record *r, unsigned x, unsigned y)
{
    return x < 16 && y < 16 ? r->bytes[XEEN_MAP_RECORD_METADATA + y * 16 + x] : 0;
}
uint8_t xeen_map_threshold(const xeen_map_record *r)
{
    return r->bytes[XEEN_MAP_RECORD_THRESHOLD];
}
/**
 * Movement service callback that answers XEEN_LOAD_MAP from the resource archive.
 *
 * Prepares the requested map (with the loader's flag-19h patch inputs) into the navigation cache
 * and copies it to the DS image; any other service goes to the loader's fallback. Returns 0 when
 * the service is not served or the map cannot be prepared (last_status says why).
 */
int xeen_resource_service(void *u, xeen_movement *m, xeen_service service, uint16_t arg,
                          uint16_t arg2, uint16_t *result)
{
    xeen_resource_loader *l = u;
    xeen_navigation_cache c;
    xeen_patch_inputs patch;
    if (!l || !m || !result) {
        return 0;
    }
    if (service != XEEN_LOAD_MAP) {
        return l->fallback ? l->fallback(l->fallback_user, m, service, arg, arg2, result) : 0;
    }
    patch.world = m->ds[XEEN_DS_WORLD];
    patch.flag19 = l->flag19;
    l->last_status = xeen_cache_prepare(l->archive, l->bank, arg, patch, &c);
    if (l->last_status != XR_OK) {
        return 0;
    }
    l->last_status = xeen_cache_commit(&c, m);
    *result = 0;
    return l->last_status == XR_OK;
}
