#include "xeen/event_text.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>
/**
 * Binds an event text resource (a map's .txt) to OUT after validating its index; OUT is unchanged
 * on failure.
 */
xeen_event_status xeen_event_text_bind(xeen_event_text_resource *out, const uint8_t *bytes,
                                       size_t size)
{
    xeen_event_index index;
    xeen_event_status status;
    xeen_event_text_resource next;
    if (!out) {
        return XE_BAD_INPUT;
    }
    status = xeen_events_decode(NULL, 0, bytes, size, &index);
    if (status != XE_OK) {
        return status;
    }
    next.bytes = bytes;
    next.size = size;
    next.terminators = index.text_terminators;
    memcpy(next.offsets, index.text_offsets, sizeof next.offsets);
    *out = next;
    return XE_OK;
}
xeen_resource_status xeen_event_text_load(xeen_event_text_resource *out, const xeen_archive *assets,
                                          uint16_t map_id, uint8_t *storage, size_t capacity,
                                          xeen_resource_info *info)
{
    char name[24];
    xeen_resource_info found;
    xeen_resource_status status;
    xeen_event_status bound;
    xeen_event_text_resource next;
    int signed_map = map_id < 32768u ? (int)map_id : (int)map_id - 65536;
    if (!out || !assets || !storage) {
        return XR_BAD_INPUT;
    }
    (void)snprintf(name, sizeof name, "aaze%c%03u.txt", signed_map <= 99 ? '0' : 'x',
                   (unsigned)map_id);
    status = xeen_archive_find_name(assets, name, &found);
    if (status != XR_OK) {
        return status;
    }
    status = xeen_archive_extract(assets, XR_BANK_ASSETS_XOR35, &found, storage, capacity, NULL);
    if (status != XR_OK) {
        return status;
    }
    bound = xeen_event_text_bind(&next, storage, found.size);
    if (bound != XE_OK) {
        return bound == XE_CAPACITY ? XR_CAPACITY : XR_BAD_INPUT;
    }
    *out = next;
    if (info) {
        *info = found;
    }
    return XR_OK;
}
/**
 * Installs a bound text resource's index into DS as the original's text index leaves it (2C68B..2C6C8): the offsets
 * of texts 0..XEEN_EVENT_MAX_TEXTS as little-endian words at XEEN_DS_EVENT_TEXT_OFFSETS and the NUL count at
 * XEEN_DS_EVENT_TEXT_TERMINATORS. XE_BAD_INPUT for a missing resource or DS, or resource bytes inside the DS window
 * (the install must not read what it writes); XE_TRUNCATED when DS cannot hold the offsets; DS unchanged then.
 */
xeen_event_status xeen_event_text_install(const xeen_event_text_resource *resource, uint8_t *ds,
                                          size_t ds_size)
{
    if (!resource || !ds || (!resource->bytes && resource->size)) {
        return XE_BAD_INPUT;
    }
    if (resource->size) {
        uintptr_t source = (uintptr_t)resource->bytes, target = (uintptr_t)ds;
        size_t span = ds_size < 65536u ? ds_size : 65536u;
        if (source <= target ? target - source < resource->size : source - target < span) {
            return XE_BAD_INPUT;
        }
    }
    if (ds_size < XEEN_DS_EVENT_TEXT_OFFSETS + sizeof resource->offsets) {
        return XE_TRUNCATED;
    }
    for (size_t i = 0; i <= XEEN_EVENT_MAX_TEXTS; ++i) {
        xeen_put_le16(ds + XEEN_DS_EVENT_TEXT_OFFSETS + 2u * i, resource->offsets[i]);
    }
    ds[XEEN_DS_EVENT_TEXT_TERMINATORS] = (uint8_t)resource->terminators;
    return XE_OK;
}
xeen_event_status xeen_event_text_copy(const xeen_event_text_resource *resource, unsigned selector,
                                       uint8_t *ds, size_t ds_size, uint16_t destination,
                                       size_t count)
{
    size_t start, copied = 0;
    if (!resource || !ds || (!resource->bytes && resource->size) || selector > 255u) {
        return XE_BAD_INPUT;
    }
    if (ds_size > 65536u) {
        ds_size = 65536u;
    }
    if ((size_t)destination > ds_size || count > ds_size - destination) {
        return XE_TRUNCATED;
    }
    if (count && resource->size) {
        uintptr_t source = (uintptr_t)resource->bytes, target = (uintptr_t)(ds + destination);
        if (source <= target ? target - source < resource->size : source - target < count) {
            return XE_BAD_INPUT;
        }
    }
    start = resource->offsets[selector];
    if (start > resource->size) {
        return XE_TRUNCATED;
    }
    /* The selector one past the last text (an operation whose +6
     * byte is not a text, such as condition 44 of operation 9 on a map with
     * 44 texts) starts at the end of the file: 15616 then copies DOS memory
     * beyond the 9C5C buffer, which the native does not keep; it copies an
     * empty string. The next record's copy pads A827 again. */
    if (start == resource->size && resource->size && selector == resource->terminators) {
        if (count) {
            memset(ds + destination, 0, count);
        }
        return XE_OK;
    }
    while (copied < count) {
        if (copied >= resource->size - start) {
            return XE_TRUNCATED;
        }
        if (!resource->bytes[start + copied++]) {
            break;
        }
    }
    if (copied) {
        memcpy(ds + destination, resource->bytes + start, copied);
    }
    if (count > copied) {
        memset(ds + destination + copied, 0, count - copied);
    }
    return XE_OK;
}
