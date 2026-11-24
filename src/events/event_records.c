#include "xeen/events.h"
#include <string.h>
/** Interprets a byte as the original's signed value (CBW). */
static int16_t signed_byte(uint8_t b)
{
    return (int16_t)(b < 128u ? (int)b : (int)b - 256);
}
xeen_event_status xeen_events_decode(const uint8_t *records, size_t size, const uint8_t *texts,
                                     size_t text_size, xeen_event_index *out)
{
    xeen_event_index next = {0};
    size_t offset = 0;
    if (!out || (!records && size) || (!texts && text_size)) {
        return XE_BAD_INPUT;
    }
    if (size > UINT16_MAX || text_size > UINT16_MAX) {
        return XE_CAPACITY;
    }
    /* Original 2C6CE is a do/while; empty input would read outside the buffer.
     * The native API defines empty input as an empty index. */
    while (offset < size) {
        size_t length = records[offset];
        xeen_event_record *r;
        if (length < 4u || length > 127u) {
            return XE_BAD_LENGTH;
        }
        if (length + 1u > size - offset) {
            return XE_TRUNCATED;
        }
        if (next.record_count == XEEN_EVENT_MAX_RECORDS) {
            return XE_CAPACITY;
        }
        r = &next.records[next.record_count++];
        r->field1 = signed_byte(records[offset + 1u]);
        r->field2 = signed_byte(records[offset + 2u]);
        r->field3 = signed_byte(records[offset + 3u]);
        r->field4 = records[offset + 4u];
        r->offset = (uint16_t)offset;
        offset += length + 1u;
    }
    /* 2C661: zero the offset table then append position+1 at every NUL. */
    for (size_t i = 0; i < text_size; ++i) {
        if (!texts[i]) {
            if (next.text_terminators == XEEN_EVENT_MAX_TEXTS) {
                return XE_CAPACITY;
            }
            next.text_offsets[++next.text_terminators] = (uint16_t)(i + 1u);
        }
    }
    next.text_count = (uint8_t)next.text_terminators;
    *out = next;
    return XE_OK;
}
