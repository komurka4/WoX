#ifndef XEEN_EVENT_DISPATCH_H
#define XEEN_EVENT_DISPATCH_H
#include "xeen/event_exec.h"
/* Bounded 310B4 lifecycle around the existing interpreter. Other original
 * segments are explicit per-instance storage, never aliased into logical DS.
 * Caller supplies the analysis relocation DS segment (581D for load 1000).
 * UI remains the existing external presentation abstraction. */
typedef enum {
    XD_RUNNING,
    XD_INPUT_OR_SERVICE,
    XD_RETURNED,
    XD_RETURNED_EXIT,
    XD_UNSUPPORTED,
    XD_INPUT_ERROR,
    XD_INTERNAL
} xeen_dispatch_status;
typedef struct {
    xeen_event_execution execution;
    uint32_t boundary;
    uint16_t return_ax, ds_segment;
    uint8_t phase, saved_mode, saved_layout, selected_character;
    /* Original 50D4:1BD5..1C7D (DOS memory, written only by 31111): four
     * 10-slot item arrays (bytes 0..2 of each 4-byte record: 9970 at +0,
     * 9998 +28h, 99C0 +50h, 99E8 +78h), then 9A10 (+A0h), 9A12..9A15
     * (+A1h) and 9A16..9A19 (+A5h). Byte 3 of a record is never written. */
    uint8_t treasure_stash[169];
    int window;
    xeen_dispatch_status status;
} xeen_event_dispatch;
/*
 * The button-set stack (xeen/ds_globals.h: BUTTON_DEPTH and the *_STACK slots). XEEN_BUTTON_PUSH:
 * 190ED saves the live button set one level deeper, then 1919B empties it (the six table pointers
 * to the empty tables in `segment`, BUTTON_SPRITES FFFFh). XEEN_BUTTON_POP: 18FFF restores the set
 * saved at the current level and goes one level up; `segment` is not used. There are no bound
 * checks, as in the original.
 */
#define XEEN_BUTTON_POP  0
#define XEEN_BUTTON_PUSH 1
void xeen_button_stack(uint8_t *ds, int push, uint16_t segment);
/* Saves the live button set on the stack without clearing it (the original's push alone, without the reset):
 * the screen's buttons stay live; xeen_button_stack's pop restores the saved set as usual. */
void xeen_button_stack_save(uint8_t *ds);
/* Points the live button set at the empty tables in SEGMENT, with no button sprites (the original's 1919B). */
void xeen_button_tables_clear(uint8_t *ds, uint16_t segment);
/* Begin once per original dispatcher invocation; never use to resume. */
xeen_dispatch_status xeen_event_dispatch_begin(xeen_event_dispatch *, uint8_t *, size_t, uint16_t);
/* Services/core suspension preserves phase, DS and resource mutations. Zero
 * callback means unavailable. Only interpreter/resource-copy suspensions resume;
 * window38 can close through an explicit 4F57B service; cache release and
 * pending settlement remain hard boundaries.
 * RETURNED_EXIT is an actual RETF contract whose
 * F60A/F64A state forbids normal navigation; it is not gameplay success. */
xeen_dispatch_status xeen_event_dispatch_run(xeen_event_dispatch *, uint8_t *, size_t,
                                             const xeen_event_index *, uint8_t *, size_t, size_t,
                                             xeen_event_request_fn, void *);
#endif
