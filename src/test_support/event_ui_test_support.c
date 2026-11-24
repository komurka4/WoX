/*
 * The scripted event UI (a recorded sequence of presentation requests and answers) used only by the tests.
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/events/event_ui.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/event_ui.h"
#include "../events/event_ui_internal.h"
#include "xeen/character_record.h"
#include "xeen/input_poll.h"
#include <string.h>

/**
 * Initializes a scripted UI that answers presentation requests from the N ENTRIES in order; a NULL
 * entry list with N > 0 puts the script in its protocol-error state.
 */
void xeen_ui_script_init(xeen_ui_script *s, const xeen_ui_script_entry *entries, size_t n)
{
    if (!s) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->entries = entries;
    s->count = n;
    if (!entries && n) {
        s->status = XUI_SCRIPT_PROTOCOL;
    }
}
/**
 * Present callback of the scripted UI: accepts the next request when it matches the script's next
 * entry. Returns -1 and records the error state on a mismatch, a request while another is active,
 * or an exhausted script.
 */
int xeen_ui_script_present(void *user, const xeen_ui_request *q)
{
    xeen_ui_script *s = user;
    if (!s || !q || s->status != XUI_SCRIPT_OK) {
        return -1;
    }
    if (s->active) {
        s->status = XUI_SCRIPT_PROTOCOL;
        return -1;
    }
    if (s->consumed == s->count) {
        s->status = XUI_SCRIPT_EXHAUSTED;
        return -1;
    }
    if (!xeen_ui_same_request(q, &s->entries[s->consumed].request)) {
        s->status = XUI_SCRIPT_MISMATCH;
        return -1;
    }
    s->remaining_polls = s->entries[s->consumed].pending_polls;
    s->active = 1;
    return 1;
}
/**
 * Poll callback of the scripted UI: after the entry's number of empty polls, answers the active
 * request with the entry's response; returns -1 and records a protocol error without one.
 */
int xeen_ui_script_poll(void *user, xeen_ui_response *answer)
{
    xeen_ui_script *s = user;
    if (!s || !answer || s->status != XUI_SCRIPT_OK) {
        return -1;
    }
    if (!s->active) {
        s->status = XUI_SCRIPT_PROTOCOL;
        return -1;
    }
    if (s->remaining_polls) {
        --s->remaining_polls;
        return 0;
    }
    *answer = s->entries[s->consumed].response;
    ++s->consumed;
    s->active = 0;
    return 1;
}
