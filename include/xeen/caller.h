#ifndef XEEN_CALLER_H
#define XEEN_CALLER_H
#include "xeen/movement.h"
/* The exploration loop's two input points (their original addresses, used as boundaries and lifecycle states). */
enum xeen_loop_poll {
    XEEN_LOOP_COMMAND_POLL = 0x17b09,       /* the loop asks the input poll for the next command */
    XEEN_LOOP_IDLE_WAIT = 0x17b12           /* the poll gave no command: the loop waits for the frame timer's next
                                             * tick before polling again */
};
/* The loop's next owners after a step or a dispatch (their original addresses): the fall, and the party cell's step
 * event. */
enum xeen_loop_continuation {
    XEEN_LOOP_AFTER_DISPATCH = 0x17c03,     /* a dispatched command returned to the loop (xeen_caller_after_dispatch) */
    XEEN_LOOP_FALL = 0x173a0,               /* a fall is pending: the fall scene runs next */
    XEEN_LOOP_STEP_EVENT = 0x17ae5          /* the party's cell has a step event: it runs next */
};
typedef struct {
    xeen_status status;
    uint32_t boundary;
} xeen_caller_result;
/* Begin at original 17C03 with dispatcher AX. Every return is a bounded
 * continuation, never permission to accept another frontend command. Command
 * handlers stop before their first instruction; unsupported services are never
 * called via a success stub. F64A is stored even when subsequent work blocks.
 * 17F28 is the unexecuted session-exit path, not completed shutdown.
 * Metadata failure reports 1C844 (entry to the metadata service); its status
 * identifies invalid or missing map state. Other boundaries are exact next
 * original instructions. m==NULL returns BAD_INPUT,boundary 17C03. */
xeen_caller_result xeen_caller_after_dispatch(xeen_movement *, uint16_t dispatcher_ax);
/* 1 when BOUNDARY is one of the CS:0EAC table's 26 targets. */
int xeen_caller_is_command_target(uint32_t boundary);
/* The loop's tail after a command, original 17F07: reached by an unmatched dispatch (17C29) and by the post-action
 * step after its toggles (17D98; xeen_post_action_resume_audio). A pending fall (8C85) stops at 173A0 before its
 * scene, a defeated party (F60A) or a set exit word (F64A) at 17F28, otherwise the join 17ACC
 * (xeen_caller_after_rejected_move). m==NULL returns what the join returns for it. */
xeen_caller_result xeen_caller_after_command(xeen_movement *);
/* The join 17ACC, shared by a rejected move and the loop's tail (17F25 jmp 17ACC): the party cell's metadata
 * (1C844; its failure reports XEEN_METADATA_QUERY with the query's status), the cell's step event (F482: stop
 * 17AE5), a treasure dialog (2EA68 owning one: stop 2EA8E), else the next poll 4EA0D. No movement, time charge or
 * successful-command count is manufactured. m==NULL returns BAD_INPUT, boundary 17ACC. */
xeen_caller_result xeen_caller_after_rejected_move(xeen_movement *);
#endif
