#include "xeen/caller.h"
#include "xeen/ds_globals.h"
#include "xeen/treasure.h"
#include "xeen/gameplay_tick.h"
/*
 * Where the caller's continuation ends after a dispatch or a rejected move: the next owner (the loop's exit, the
 * fall, the step cell's event, a treasure, the next poll). Each value is the original address it reaches
 * (tools/xeen_replay/stops.json).
 */
enum {
    CALLER_STOP_DISPATCH_EXIT = 0x17f28,      /* the dispatch returned an exit */
    CALLER_STOP_FALL = 0x173a0,
    CALLER_STOP_EXIT = 0x17f28,               /* the party is defeated or the loop is to end */
    CALLER_STOP_STEP_EVENT = 0x17ae5,
    CALLER_STOP_TREASURE = 0x2ea8e,
    CALLER_STOP_NEXT_POLL = 0x4ea0d           /* before the BIOS timer call 17AFC's first instruction */
};
/* The original CS:170D command table (at 0EAC): 26 command words followed by their 26 targets.
 * Targets include distinct B0/B1 entry points; no handler is executed here. */
static const uint16_t commands[26] = {0x09, 0x20, 0x3d, 0x42, 0x43, 0x44, 0x49, 0x4d, 0x51,
                                      0x52, 0x53, 0x56, 0x57, 0xb0, 0xb1, 0xc9, 0xca, 0xcb,
                                      0xcc, 0xcd, 0xce, 0xd0, 0xf0, 0xf1, 0xf2, 0xf3};
#define CALLER_COMMANDS (sizeof commands / sizeof commands[0])
static const uint32_t targets[] = {0x17c30, 0x17da8, 0x17e20, 0x17e56, 0x17e30, 0x17d9b, 0x17dd1,
                                     0x17dc4, 0x17e28, 0x17e91, 0x17e98, 0x17e18, 0x17da8, 0x17c56,
                                     0x17c94, 0x17dde, 0x17dde, 0x17dde, 0x17dde, 0x17dde, 0x17dde,
                                     0x17e10, 0x17c50, 0x17c8e, 0x17cdf, 0x17d17};
_Static_assert(sizeof targets / sizeof targets[0] == CALLER_COMMANDS, "one target per command word");
int xeen_caller_is_command_target(uint32_t boundary)
{
    unsigned i;
    for (i = 0; i < CALLER_COMMANDS; i++) {
        if (targets[i] == boundary) {
            return 1;
        }
    }
    return 0;
}
/** Ends the caller continuation at unsupported BOUNDARY (in m->status too). */
static xeen_caller_result stop(xeen_movement *m, uint32_t boundary)
{
    xeen_caller_result r;
    r.status = XEEN_SERVICE_REQUIRED;
    r.boundary = boundary;
    if (m) {
        m->status = r.status;
    }
    return r;
}
/* The exploration command's return to 17B09's loop (contract in xeen/caller.h): the exit word, the command table's
 * target, else the loop's tail 17F07 (an unmatched command: 17C29 jmp 17F07). */
xeen_caller_result xeen_caller_after_dispatch(xeen_movement *m, uint16_t ax)
{
    unsigned i;
    uint16_t command;
    xeen_caller_result r;
    if (!m) {
        r.status = XEEN_BAD_INPUT;
        r.boundary = XEEN_LOOP_AFTER_DISPATCH;
        return r;
    }
    m->status = XEEN_OK;
    xeen_write16(m, XEEN_DS_LOOP_EXIT, ax);
    if (ax) {
        return stop(m, CALLER_STOP_DISPATCH_EXIT);
    }
    command = xeen_read16(m, XEEN_DS_INPUT_CODE);
    for (i = 0; i < CALLER_COMMANDS; i++) {
        if (command == commands[i]) {
            return stop(m, targets[i]);
        }
    }
    return xeen_caller_after_command(m);
}
/* 17F07..17F25 (contract in xeen/caller.h): the fall (8C85), the exit (F60A or F64A), else `jmp 17ACC`. */
xeen_caller_result xeen_caller_after_command(xeen_movement *m)
{
    if (!m) {
        return xeen_caller_after_rejected_move(m);
    }
    if (m->ds[XEEN_DS_FALL_SCENE]) {
        return stop(m, CALLER_STOP_FALL);
    }
    if (m->ds[XEEN_DS_PARTY_DEFEATED] || xeen_read16(m, XEEN_DS_LOOP_EXIT)) {
        return stop(m, CALLER_STOP_EXIT);
    }
    return xeen_caller_after_rejected_move(m);
}

/* 17ACC..17AFC (contract in xeen/caller.h): the cell's metadata (1C844), its step event (17AE5), the treasure check (2EA68),
 * the next poll (4EA0D). */
xeen_caller_result xeen_caller_after_rejected_move(xeen_movement *m)
{
    xeen_caller_result r;
    if (!m) {
        r.status = XEEN_BAD_INPUT;
        r.boundary = XEEN_TICK_MOVE_REJECTED;
        return r;
    }
    if (xeen_metadata(m) != XEEN_OK) {
        r.status = m->status;
        r.boundary = XEEN_METADATA_QUERY;
        return r;
    }
    if (xeen_read16(m, XEEN_DS_CELL_STEP_EVENT)) {
        return stop(m, CALLER_STOP_STEP_EVENT);
    }
    /*2EA68: no or deferred treasure reaches 2F22F's no-effect epilogue;
     * a dialog owned by this join stops at 2EA8E. */
    if (xeen_treasure_check(m) == XEEN_TREASURE_DIALOG) {
        return stop(m, CALLER_STOP_TREASURE);
    }
    return stop(m, CALLER_STOP_NEXT_POLL);
}
