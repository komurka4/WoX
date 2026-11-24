#ifndef XEEN_EVENT_UI_H
#define XEEN_EVENT_UI_H
#include "xeen/event_exec.h"
/* Gameplay-facing presentation contract, not a VGA emulation. These requests
 * require the provider to present all text, including pagination, before it
 * returns completion. No pointer survives a callback: the controller owns its
 * snapshot. The caller must not mutate DS/resources/execution during callbacks.
 * Provider presentation side effects (resources/windows/input routing) remain
 * external; only input/result interpretation is reconstructed here. */
typedef enum {
    XUI_CONFIRM,
    XUI_DISMISS,
    XUI_PRESENT,
    XUI_TEXT,
    XUI_CHARACTER,
    XUI_TREASURE,
    XUI_CHOICE
} xeen_ui_kind;
typedef struct {
    xeen_ui_kind kind;
    uint16_t face, mode;
    uint8_t primary[1000], secondary[1000];
    size_t primary_size, secondary_size; /* includes terminating NUL */
} xeen_ui_request;
typedef struct {
    uint16_t input;
} xeen_ui_response;
/* present: 1 accepted, 0 unavailable, -1 failure. poll: 1 explicit completion,
 * 0 pending, -1 failure. present is called once per accepted request. For mode2
 * and text-only requests, completion explicitly acknowledges presentation; input
 * must be zero. For mode0 Y/N/Escape are supported; dismissal and treasure
 * screens require nonzero input. Character selection accepts Escape or original
 * command bytes C9..CE (F1..F6), bounded by the active party and action state.
 * Unknown mode values are retained as external boundaries. */
typedef int (*xeen_ui_present_fn)(void *, const xeen_ui_request *);
typedef int (*xeen_ui_poll_fn)(void *, xeen_ui_response *);
typedef struct {
    xeen_ui_present_fn present;
    xeen_ui_poll_fn poll;
    void *user;
    xeen_ui_request active;
    uint8_t pending;
    uint16_t last_ax, last_input;
    size_t presentations, completions;
} xeen_event_ui;
void xeen_event_ui_init(xeen_event_ui *, xeen_ui_present_fn, xeen_ui_poll_fn, void *);
/* Also recognizes 2F2FF(1,0) as any-key dismissal, 2F2FF(0,0) as the
 * yes/no (Y, N or Escape), 2F9BA as original
 * F1..F6/Escape character selection, and 2EA68 as reward acknowledgement;
 * mode/routing state and
 * scene/timer services belong to the connected game/modal owner.
 * Install as event-resource fallback. Does not acknowledge unrelated services.
 * Snapshot and completion validation precede DS input-state writes. On pending
 * resume the scalar arguments AND both complete text spans must still match. */
int xeen_event_ui_service(void *, const xeen_event_request *, uint8_t *, size_t, uint16_t *);
/* The same service with 3000D's name and text given (3000D takes
 * both as far pointers: an event's are A827 and A43F, the arena's 360C4
 * passes DS:6426's name and its own texts). Other requests ignore them. */
int xeen_event_ui_service_texts(void *, const xeen_event_request *, uint8_t *, size_t, uint16_t *,
                                const uint8_t *name, const uint8_t *text);
/* Exact scripted requests, borrowed immutable array. No wildcard/default input.
 * pending_polls is an explicit deterministic delay. Exhaustion/mismatch latch
 * errors; neither consumes a response. Each request contains full bounded text.
 * Trace is represented by consumed index, active index, counters and errors. */
typedef struct {
    xeen_ui_request request;
    xeen_ui_response response;
    size_t pending_polls;
} xeen_ui_script_entry;
typedef enum {
    XUI_SCRIPT_OK,
    XUI_SCRIPT_EXHAUSTED,
    XUI_SCRIPT_MISMATCH,
    XUI_SCRIPT_PROTOCOL
} xeen_ui_script_status;
typedef struct {
    const xeen_ui_script_entry *entries;
    size_t count, consumed, remaining_polls;
    uint8_t active;
    xeen_ui_script_status status;
} xeen_ui_script;
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
void xeen_ui_script_init(xeen_ui_script *, const xeen_ui_script_entry *, size_t);
int xeen_ui_script_present(void *, const xeen_ui_request *);
int xeen_ui_script_poll(void *, xeen_ui_response *);
#endif
