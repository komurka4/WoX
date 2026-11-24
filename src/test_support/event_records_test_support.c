/*
 * Event record queries used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/events/event_records.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/events.h"
#include <string.h>

xeen_event_status xeen_event_text(const xeen_event_index *index, const uint8_t *bytes, size_t size,
                                  size_t n, const uint8_t **text, size_t *length)
{
    size_t start, end;
    if (!index || !bytes || !text || !length) {
        return XE_BAD_INPUT;
    }
    if (n > index->text_terminators || n > XEEN_EVENT_MAX_TEXTS) {
        return XE_BAD_INPUT;
    }
    start = index->text_offsets[n];
    if (start > size) {
        return XE_TRUNCATED;
    }
    end = start;
    while (end < size && bytes[end]) {
        ++end;
    }
    *text = bytes + start;
    *length = end - start;
    return XE_OK;
}
