#ifndef XEEN_EVENT_RESOURCES_H
#define XEEN_EVENT_RESOURCES_H
#include "xeen/event_exec.h"
#include "xeen/event_text.h"
/* Caller-owned mutable handle backing and immutable text storage must outlive
 * execution. Resident storage is restored at service 4A1B7 and committed back
 * to the handle at service 4A24C.
 * DS must also be disjoint from these three resource buffers.
 * Mount state/assets explicitly for the desired world; no world is guessed. */
typedef struct {
    uint8_t *backing;
    uint8_t *resident;
    size_t record_bytes;
    uint16_t record_count;
    xeen_event_text_resource texts;
    xeen_resource_status resource_status;
    xeen_event_status text_status;
    xeen_event_request_fn fallback;
    void *fallback_user;
} xeen_event_resources;
/* No allocation. Context/index publish only on success; output byte buffers may
 * change on failure. All three buffers must be disjoint. Successful load clears
 * fallback: install the caller's fallback afterwards. Map name character uses
 * the original signed-word <=99 comparison, with unsigned numeric formatting. */
xeen_resource_status xeen_event_resources_load(xeen_event_resources *, const xeen_archive *state,
                                               const xeen_archive *assets, uint16_t map,
                                               uint8_t *backing, size_t backing_capacity,
                                               uint8_t *resident, size_t resident_capacity,
                                               uint8_t *text_storage, size_t text_capacity,
                                               xeen_event_index *);
/* Installs the text table/count and event resource sizes in a full DS view. */
xeen_event_status xeen_event_resources_prepare(const xeen_event_resources *, uint8_t *, size_t);
/* Implements text copy, backing-to-resident reload, and resident-to-backing
 * handle commit. Other
 * original calls require an explicit fallback. Diagnostic text_status retains
 * copy failure details; returning zero leaves the interpreter suspended. */
int xeen_event_resource_service(void *, const xeen_event_request *, uint8_t *, size_t,
                                uint16_t *ax);
#endif
