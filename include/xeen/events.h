#ifndef XEEN_EVENTS_H
#define XEEN_EVENTS_H
#include <stddef.h>
#include <stdint.h>
/* Bounded native reconstruction of 2C661 + 2C6CE. Semantic opcode meanings
 * remain unknown. No packed structs, native endian assumptions or DOS pointers. */
#define XEEN_EVENT_MAX_RECORDS 1715u
#define XEEN_EVENT_MAX_TEXTS   279u
/* Original ten-byte row: three sign-extended bytes, one unsigned byte,
 * and the offset of the original length byte. */
typedef struct {
    int16_t field1, field2, field3;
    uint16_t field4, offset;
} xeen_event_record;
typedef struct {
    xeen_event_record records[XEEN_EVENT_MAX_RECORDS];
    uint16_t text_offsets[XEEN_EVENT_MAX_TEXTS + 1u];
    uint16_t record_count;
    uint8_t text_count;        /* Original AL truncation, even for >255 strings. */
    uint16_t text_terminators; /* Native explicit count, without truncation. */
} xeen_event_index;
typedef enum {
    XE_OK,
    XE_BAD_INPUT,
    XE_TRUNCATED,
    XE_CAPACITY,
    XE_BAD_LENGTH
} xeen_event_status;
/* On error output remains unchanged. Length includes four header fields and
 * payload but excludes its own byte. Original signed length arithmetic accepts
 * dangerous backward steps; native API rejects lengths outside [4,127].
 * Trailing non-NUL text is accepted, as in the original. Offsets are uint16_t. */
xeen_event_status xeen_events_decode(const uint8_t *records, size_t record_bytes,
                                     const uint8_t *texts, size_t text_bytes,
                                     xeen_event_index *out);
/* Resolve one original text index: index zero starts at byte zero; each NUL
 * appends the next start, including a possible one-past-end empty final span.
 * Returns a borrowed span excluding NUL, with a supplied explicit bound. */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_event_status xeen_event_text(const xeen_event_index *, const uint8_t *, size_t, size_t,
                                  const uint8_t **, size_t *);
#endif
