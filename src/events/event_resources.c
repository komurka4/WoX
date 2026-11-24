#include "xeen/event_resources.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include <stdio.h>
#include <string.h>

/** Returns nonzero when the memory ranges A (AN bytes) and B (BN bytes) overlap. */
static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t av = (uintptr_t)a, bv = (uintptr_t)b;
    if (!an || !bn) {
        return 0;
    }
    return av <= bv ? bv - av < an : av - bv < bn;
}
xeen_resource_status xeen_event_resources_load(xeen_event_resources *out, const xeen_archive *state,
                                               const xeen_archive *assets, uint16_t map,
                                               uint8_t *backing, size_t backing_capacity,
                                               uint8_t *resident, size_t resident_capacity,
                                               uint8_t *text_storage, size_t text_capacity,
                                               xeen_event_index *index)
{
    xeen_event_resources next;
    xeen_event_index decoded;
    xeen_resource_info info;
    xeen_resource_status status;
    size_t bytes = 0;
    char name[32];
    int signed_map = xeen_sign16(map);
    int length;
    if (!out || !state || !assets || !backing || !resident || !text_storage || !index ||
        overlaps(backing, backing_capacity, resident, resident_capacity) ||
        overlaps(backing, backing_capacity, text_storage, text_capacity) ||
        overlaps(resident, resident_capacity, text_storage, text_capacity)) {
        return XR_BAD_INPUT;
    }
    memset(&next, 0, sizeof(next));
    length = snprintf(name, sizeof(name), "maze%c%03u.evt", signed_map <= 99 ? '0' : 'x',
                      (unsigned)map);
    if (length < 0 || (size_t)length >= sizeof(name)) {
        return XR_BAD_INPUT;
    }
    status = xeen_archive_find_named(state, (const char *)name, (size_t)length, &info);
    if (status != XR_OK) {
        return status;
    }
    if (info.size > resident_capacity) {
        return XR_CAPACITY;
    }
    status = xeen_archive_extract(state, XR_BANK_STATE, &info, backing, backing_capacity, &bytes);
    if (status != XR_OK) {
        return status;
    }
    status = xeen_event_text_load(&next.texts, assets, map, text_storage, text_capacity, NULL);
    if (status != XR_OK) {
        return status;
    }
    next.text_status =
            xeen_events_decode(backing, bytes, next.texts.bytes, next.texts.size, &decoded);
    if (next.text_status != XE_OK) {
        return next.text_status == XE_CAPACITY ? XR_CAPACITY : XR_BAD_INPUT;
    }
    memcpy(resident, backing, bytes);
    next.backing = backing;
    next.resident = resident;
    next.record_bytes = bytes;
    next.record_count = decoded.record_count;
    next.resource_status = XR_OK;
    *index = decoded;
    *out = next;
    return XR_OK;
}
xeen_event_status xeen_event_resources_prepare(const xeen_event_resources *ctx, uint8_t *ds,
                                               size_t size)
{
    xeen_event_status status;
    if (!ctx || !ds || size < 65536u || !ctx->backing || !ctx->resident ||
        ctx->record_bytes > 65535u || ctx->texts.size > 65535u) {
        return XE_BAD_INPUT;
    }
    if (overlaps(ctx->backing, ctx->record_bytes, ds, 65536u) ||
        overlaps(ctx->resident, ctx->record_bytes, ds, 65536u)) {
        return XE_BAD_INPUT;
    }
    status = xeen_event_text_install(&ctx->texts, ds, size);
    if (status != XE_OK) {
        return status;
    }
    /* 2C5FD: 9A1A keeps the largest record count; above 6B3h the
     * original opens window 6 with a warning (an explicit stop here). */
    if (ctx->record_count > 0x6b3u) {
        return XE_CAPACITY;
    }
    xeen_put_le16(ds + XEEN_DS_EVENT_RECORD_COUNT, ctx->record_count);
    xeen_put_le16(ds + XEEN_DS_EVENT_BYTES, (uint16_t)ctx->record_bytes);
    xeen_put_le16(ds + XEEN_DS_EVENT_TEXTS_SIZE, (uint16_t)ctx->texts.size);
    if ((unsigned)(ds[XEEN_DS_EVENT_RECORD_COUNT_MAX] | ds[XEEN_DS_EVENT_RECORD_COUNT_MAX + 1] << 8) <
        ctx->record_count) {
        xeen_put_le16(ds + XEEN_DS_EVENT_RECORD_COUNT_MAX, ctx->record_count);
    }
    return XE_OK;
}
int xeen_event_resource_service(void *user, const xeen_event_request *request, uint8_t *ds,
                                size_t size, uint16_t *ax)
{
    xeen_event_resources *ctx = user;
    if (!ctx || !request || !ds || size < 65536u || !ax) {
        return 0;
    }
    if (request->address == XEEN_REQUEST_COPY_TEXT) {
        ctx->text_status = xeen_event_text_copy(&ctx->texts, request->argument, ds, size,
                                                request->destination, request->count);
        return ctx->text_status == XE_OK;
    }
    if (request->address == XEEN_REQUEST_RESTORE_EVENTS) {
        if (ctx->record_bytes > 65535u || ctx->record_count > XEEN_EVENT_MAX_RECORDS ||
            !ctx->backing || !ctx->resident ||
            overlaps(ctx->backing, ctx->record_bytes, ctx->resident, ctx->record_bytes)) {
            ctx->resource_status = XR_BAD_INPUT;
            return 0;
        }
        memcpy(ctx->resident, ctx->backing, ctx->record_bytes);
        ctx->resource_status = XR_OK;
        return 1;
    }
    if (request->address == XEEN_REQUEST_STORE_EVENTS) {
        if (ctx->record_bytes > 65535u || ctx->record_count > XEEN_EVENT_MAX_RECORDS ||
            !ctx->backing || !ctx->resident ||
            overlaps(ctx->backing, ctx->record_bytes, ctx->resident, ctx->record_bytes)) {
            ctx->resource_status = XR_BAD_INPUT;
            return 0;
        }
        memcpy(ctx->backing, ctx->resident, ctx->record_bytes);
        ctx->resource_status = XR_OK;
        return 1;
    }
    return ctx->fallback ? ctx->fallback(ctx->fallback_user, request, ds, size, ax) : 0;
}
