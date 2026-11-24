#ifndef XEEN_EVENT_TEXT_H
#define XEEN_EVENT_TEXT_H
#include "xeen/events.h"
#include "xeen/resources.h"
/* Borrowed decoded asset bytes must outlive all copies and remain disjoint
 * from the logical DS view. No allocation. */
typedef struct xeen_event_text_resource {
    const uint8_t *bytes;
    size_t size;
    uint16_t offsets[XEEN_EVENT_MAX_TEXTS + 1u];
    uint16_t terminators;
} xeen_event_text_resource;
xeen_event_status xeen_event_text_bind(xeen_event_text_resource *, const uint8_t *, size_t);
/* Uses the caller's mounted world asset bank, literal aaze name and XOR35.
 * Output/info stay unchanged on failure; caller storage may have been decoded.
 * Index capacity errors map to XR_CAPACITY; other bind errors to XR_BAD_INPUT. */
xeen_resource_status xeen_event_text_load(xeen_event_text_resource *, const xeen_archive *,
                                          uint16_t map_id, uint8_t *storage, size_t capacity,
                                          xeen_resource_info *info);
/* Installs the resource's text index into DS (the offsets and the NUL count; contract at the definition). */
xeen_event_status xeen_event_text_install(const xeen_event_text_resource *, uint8_t *ds,
                                          size_t ds_size);
/* Original unsigned-byte selectors include zero-filled unused table slots.
 * Copies at most count bytes through NUL, padding the rest with zero. A source
 * lacking NUL is valid when count bytes exist. Rejects invalid bounds before
 * writing. Destination must not overlap the borrowed immutable source bytes. */
xeen_event_status xeen_event_text_copy(const xeen_event_text_resource *, unsigned selector,
                                       uint8_t *ds, size_t ds_size, uint16_t destination,
                                       size_t count);
#endif
