#include "xeen/map_objects.h"
#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/event_dispatch.h"
#include "xeen/character_record.h"
#include "xeen/treasure.h"
#include <string.h>
/*
 * Where the event dispatcher stops when its host did not serve a request; each value is the request's own address
 * (the service, enum xeen_event_service, or the dispatcher's own step), as the stop registry
 * (tools/xeen_replay/stops.json) and the traces report it.
 */
enum {
    DISPATCH_STOP_TREASURE_DIALOG = 0x2ea68,  /* XEEN_REQUEST_TREASURE_DIALOG */
    DISPATCH_STOP_OPENED_CHEST = 0x336dc,     /* XEEN_REQUEST_OPENED_CHEST */
    DISPATCH_STOP_NOTHING_HERE = 0x337d7,     /* XEEN_REQUEST_NOTHING_HERE */
    DISPATCH_STOP_REFUSED_SOUND = 0x3385a,    /* the refusal's sound at a record facing another way */
    DISPATCH_STOP_FREE_SPRITES = 0x4aa3e,     /* XEEN_REQUEST_FREE_SPRITES */
    DISPATCH_STOP_WINDOW_RESTORE = 0x4f57b,   /* XEEN_REQUEST_WINDOW_RESTORE */
    DISPATCH_STOP_RESTORE_EVENTS = 0x4a1b7,   /* XEEN_REQUEST_RESTORE_EVENTS */
    DISPATCH_STOP_PARTY_BAR = 0x2abb8         /* XEEN_REQUEST_PARTY_BAR */
};
/** Reads the little-endian word at DS offset A of D (the offset and its successor wrap at 64K). */
static uint16_t ds_word(const uint8_t *d, unsigned a)
{
    return (uint16_t)(d[(uint16_t)a] | (uint16_t)((uint16_t)d[(uint16_t)(a + 1u)] << 8));
}
/** Writes V as a little-endian word at DS offset A of D (wrapping at 64K). */
static void ds_put_word(uint8_t *d, unsigned a, uint16_t v)
{
    d[(uint16_t)a] = (uint8_t)v;
    d[(uint16_t)(a + 1u)] = (uint8_t)(v >> 8);
}
/** Interprets a byte as the original's signed value (CBW). */
static int signed_byte(uint8_t v)
{
    return v < 128u ? (int)v : (int)v - 256;
}
/** Returns nonzero when a treasure is pending (DS 9A10 items or any word of 9A12..9A19). */
static int treasure_pending(const uint8_t *d)
{
    return d[XEEN_DS_TREASURE_ITEMS_PENDING] || ds_word(d, XEEN_DS_TREASURE_GEMS) ||
           ds_word(d, XEEN_DS_TREASURE_GEMS + 2u) || ds_word(d, XEEN_DS_TREASURE_GOLD) ||
           ds_word(d, XEEN_DS_TREASURE_GOLD + 2u);
}
/*
 * The dispatcher's treasure stash (xeen_event_dispatch.treasure_stash): bytes 0..2 of each of the pending treasure's
 * forty 4-byte item rows (ten per category, the misc rows first), then the pending items flag (9A10) and the eight
 * currency bytes (9A12..9A19: the gems, then the gold dword).
 */
enum {
    STASH_ITEMS_FLAG = 0xa0,
    STASH_CURRENCY = 0xa1,
    STASH_CURRENCY_BYTES = 8
};
/* Copies N bytes of the pending treasure at LIVE into the stash at KEPT, or with RESTORE back over the live ones. */
static void stash_copy(uint8_t *live, uint8_t *kept, size_t n, int restore)
{
    if (restore) {
        memcpy(live, kept, n);
    } else {
        memcpy(kept, live, n);
    }
}
/*
 * Copies the pending treasure into the dispatcher's stash S, or with RESTORE back over the live one (a copy each
 * way, not a swap): bytes 0..2 of each item row, the pending items flag and the currency. Byte 3 of each row is not
 * kept. Original: 31111 (stash), 33896 (restore).
 */
static void stash(uint8_t *d, uint8_t *s, int restore)
{
    static const unsigned arrays[4] = {XEEN_DS_TREASURE_MISC, XEEN_DS_TREASURE_ACCESSORIES, XEEN_DS_TREASURE_ARMOR,
                                       XEEN_DS_TREASURE_WEAPONS};
    for (unsigned a = 0; a < 4u; ++a) {
        for (unsigned i = 0; i < 10u; ++i) {
            stash_copy(d + arrays[a] + i * 4u, s + a * 40u + i * 4u, 3, restore);
        }
    }
    stash_copy(d + XEEN_DS_TREASURE_ITEMS_PENDING, s + STASH_ITEMS_FLAG, 1, restore);
    stash_copy(d + XEEN_DS_TREASURE_GEMS, s + STASH_CURRENCY, STASH_CURRENCY_BYTES, restore);
}
/** Returns nonzero when the stash S holds a treasure: its items flag or any currency byte is set. */
static int has_stashed_items(const uint8_t *s)
{
    for (unsigned i = STASH_ITEMS_FLAG; i < STASH_CURRENCY + STASH_CURRENCY_BYTES; ++i) {
        if (s[i]) {
            return 1;
        }
    }
    return 0;
}
/**
 * Ends the dispatch at unsupported original address A: records the boundary and returns
 * XD_INPUT_OR_SERVICE.
 */
static xeen_dispatch_status stop(xeen_event_dispatch *c, uint32_t a)
{
    c->boundary = a;
    return c->status = XD_INPUT_OR_SERVICE;
}
/* The live button set's six far pointers (x, y, width, height, key, enabled) and the empty tables 1919B points them
 * at when a new screen starts without buttons. */
static const unsigned button_live[] = {XEEN_DS_BUTTON_X_TABLE,     XEEN_DS_BUTTON_Y_TABLE,
                                       XEEN_DS_BUTTON_WIDTH_TABLE, XEEN_DS_BUTTON_HEIGHT_TABLE,
                                       XEEN_DS_BUTTON_KEY_TABLE,   XEEN_DS_BUTTON_ENABLED_TABLE};
static const unsigned button_stacks[] = {XEEN_DS_BUTTON_X_STACK,     XEEN_DS_BUTTON_Y_STACK,
                                         XEEN_DS_BUTTON_WIDTH_STACK, XEEN_DS_BUTTON_HEIGHT_STACK,
                                         XEEN_DS_BUTTON_KEY_STACK,   XEEN_DS_BUTTON_ENABLED_STACK};
static const uint16_t button_empty[] = {0x1bc4, 0x1bc8, 0x1bc9, 0x1bca, 0x1bcb, 0x1bcc};
/* Copies the live set into, or back from, the stacks' slots of BUTTON_DEPTH (the original's signed byte depth and
 * 16-bit slot offsets). */
static void button_stack_copy(uint8_t *d, int save)
{
    int depth = signed_byte(d[XEEN_DS_BUTTON_DEPTH]);
    for (unsigned i = 0; i < 6; ++i) {
        unsigned slot = XEEN_DS_BUTTON_TABLE_SLOT(button_stacks[i], depth);
        if (save) {
            ds_put_word(d, slot, ds_word(d, button_live[i]));
            ds_put_word(d, slot + 2u, ds_word(d, button_live[i] + 2u));
        } else {
            ds_put_word(d, button_live[i], ds_word(d, slot));
            ds_put_word(d, button_live[i] + 2u, ds_word(d, slot + 2u));
        }
    }
    {
        unsigned slot = XEEN_DS_BUTTON_SPRITES_SLOT(depth);
        if (save) {
            ds_put_word(d, slot, ds_word(d, XEEN_DS_BUTTON_SPRITES));
        } else {
            ds_put_word(d, XEEN_DS_BUTTON_SPRITES, ds_word(d, slot));
        }
    }
}
/* One level deeper, the live set copied into that level's slots. */
static void button_stack_push_copy(uint8_t *d)
{
    d[XEEN_DS_BUTTON_DEPTH] = (uint8_t)(d[XEEN_DS_BUTTON_DEPTH] + 1u);
    button_stack_copy(d, 1);
}
/*
 * Saves the live button set on the button stack and leaves it live (190ED alone): a window that keeps the screen's
 * buttons working while it is shown, such as Identify Monster's or the NPC's yes/no form, which then installs its
 * own tables over them. The live set is rewritten from a copy taken first, as the native always has: at depth 127
 * the x stack's slot overlaps the live sprite word, which the original's 190ED would leave overwritten (a state
 * that takes 127 nested pushes).
 */
void xeen_button_stack_save(uint8_t *d)
{
    uint8_t live[XEEN_DS_BUTTON_SET_BYTES];
    memcpy(live, d + XEEN_DS_BUTTON_SET, sizeof live);
    button_stack_push_copy(d);
    memcpy(d + XEEN_DS_BUTTON_SET, live, sizeof live);
}
/* Points the live button set at the empty tables in SEGMENT (DS 1BC4..1BCC), with no button sprites (FFFFh): a screen
 * without buttons, as a push leaves it. Original: 1919B. */
void xeen_button_tables_clear(uint8_t *d, uint16_t segment)
{
    for (unsigned i = 0; i < 6; ++i) {
        ds_put_word(d, button_live[i], button_empty[i]);
        ds_put_word(d, button_live[i] + 2u, segment);
    }
    ds_put_word(d, XEEN_DS_BUTTON_SPRITES, 65535);
}
/*
 * Push (190ED, then 1919B): the live button set is saved on the stack and replaced by the empty tables, so the new
 * screen starts without buttons. Pop (18FFF): the saved set is live again.
 */
void xeen_button_stack(uint8_t *d, int push, uint16_t segment)
{
    if (push) {
        button_stack_push_copy(d);
        xeen_button_tables_clear(d, segment);
    } else {
        button_stack_copy(d, 0);
        d[XEEN_DS_BUTTON_DEPTH] = (uint8_t)(d[XEEN_DS_BUTTON_DEPTH] - 1u);
    }
}
/*
 * xeen_event_dispatch_run's phases (xeen_event_dispatch.phase), in the original's order: 310F1's start, the
 * 313FD loop, the script's end (33690/33698), then for an operation 12 (BP-18) 33698's cleanup (186C0's
 * window close at 3369E, 4A1B7 in mode 9, 2ABB8(1) at 336A3), the defeat check 2F869, the treasure
 * (2EA68, 336DC), the dispatcher's own 186C0 close and 4A1B7, and the tail 337D7..33B36.
 */
/* The dispatcher's return to its caller (the end of its tail 337D7..33B36): the boundary of a finished dispatch. */
enum {
    DISPATCH_RETURN = 0x33b36
};
enum {
    DISPATCH_BEGIN = 1,             /* 310F1: the treasure stash, the execution start */
    DISPATCH_EXECUTE = 2,           /* 313FD: the interpreter, resumed after each service */
    DISPATCH_SCRIPT_END = 3,        /* 33690: F466/F467 = 2; BP-18 selects the cleanup */
    DISPATCH_DEFEAT_CHECK = 4,      /* 2F869: F60A */
    DISPATCH_TREASURE = 5,          /* 2EA68 (a pending treasure), 336DC (a picked chest) */
    DISPATCH_CLOSE_SCAN = 6,        /* 186C0: the next open window, descending */
    DISPATCH_CLOSE_WINDOW = 7,      /* its 4F57B */
    DISPATCH_CLOSE_RELOAD = 8,      /* 186FA: 4A1B7 in mode 9 */
    DISPATCH_TAIL = 9,              /* 337D7, the stash back, 8D56, 33B36: the return */
    DISPATCH_RETURNED = 10,         /* returned (status RETURNED / RETURNED_EXIT) */
    DISPATCH_CLEANUP_SCAN = 11,     /* 3369E's 186C0 for operation 12, as DISPATCH_CLOSE_SCAN */
    DISPATCH_CLEANUP_WINDOW = 12,   /* its 4F57B */
    DISPATCH_CLEANUP_RELOAD = 13,   /* its 4A1B7 in mode 9 */
    DISPATCH_CLEANUP_PARTY_BAR = 14 /* 336A3: 2ABB8(1) */
};
xeen_dispatch_status xeen_event_dispatch_begin(xeen_event_dispatch *c, uint8_t *d, size_t n,
                                               uint16_t segment)
{
    if (!c || !d || n < 65536u) {
        return XD_INPUT_ERROR;
    }
    memset(c, 0, sizeof(*c));
    c->ds_segment = segment;
    c->saved_layout = 255;
    xeen_button_stack(d, XEEN_BUTTON_PUSH, segment);
    ds_put_word(d, XEEN_DS_FIXED_ITEM, 0);
    c->selected_character = 0;
    c->saved_mode = d[XEEN_DS_UI_MODE];
    memset(d + XEEN_DS_MEMBER_FLASHED, 0, 6);
    c->phase = DISPATCH_BEGIN;
    c->status = XD_RUNNING;
    return c->status;
}
/**
 * Asks the host to run the original routine at ADDRESS with ARGUMENT; returns its answer (0 without
 * a request function).
 */
static int request_event_service(xeen_event_request_fn fn, void *u, uint32_t address,
                                 uint16_t argument, uint8_t *d, size_t n)
{
    uint16_t ax = 0;
    xeen_event_request q = {address, argument, 0, 0, 0};
    return fn ? fn(u, &q, d, n, &ax) : 0;
}
/* A stage's answer to the loop: go on with the next phase (no status is stored). */
#define DISPATCH_CONTINUE XD_RUNNING
/* A host service the stage asked for answered neither 1 (performed) nor 0 (not served). */
static xeen_dispatch_status dispatch_internal(xeen_event_dispatch *c)
{
    return c->status = XD_INTERNAL;
}
/*
 * DISPATCH_BEGIN: a treasure already pending (a fight's, say) is set aside in the stash and the live currency and
 * items flag cleared, so the script starts with an empty treasure (the item records stay); without one only the
 * stash's currency is cleared. On Darkside map 29 at (30, 20) the panel's Text option is forced on for the script
 * (TEXT_OFF saved and cleared; dispatch_finish restores it). The lock's pick count is reset, the member selector
 * set to 1 and 97F5 cleared; then the interpreter starts with the whole party selected and its window reload
 * pending (the loop's first restore).
 * Original: 310F1 (31111 the stash, 3135E the currency, 31375 the stash's currency; F384 at 313BD; 9755, 98D7,
 * 97F5; 313EA BP-C = 1; 313FD's initial restore).
 */
static xeen_dispatch_status dispatch_start(xeen_event_dispatch *c, uint8_t *d, size_t n)
{
    if (treasure_pending(d)) {
        stash(d, c->treasure_stash, 0);
        d[XEEN_DS_TREASURE_ITEMS_PENDING] = 0;
        memset(d + XEEN_DS_TREASURE_GEMS, 0, STASH_CURRENCY_BYTES);
    } else {
        memset(c->treasure_stash + STASH_ITEMS_FLAG, 0, 1 + STASH_CURRENCY_BYTES);
    }
    if (d[XEEN_DS_WORLD] && d[XEEN_DS_PARTY_MAP] == 29 && d[XEEN_DS_PARTY_X] == 30 &&
        d[XEEN_DS_PARTY_Y] == 20) {
        c->saved_layout = d[XEEN_DS_TEXT_OFF];
        d[XEEN_DS_TEXT_OFF] = 0;
    }
    d[XEEN_DS_OBJECT_LOCK] = 0;
    d[XEEN_DS_MEMBER_SELECTOR] = 1;
    d[XEEN_DS_EVENT_DAMAGE_TARGET] = 0;
    if (xeen_event_execution_begin(&c->execution, d, n) != XEE_DONE) {
        return c->status = XD_INTERNAL;
    }
    c->execution.selected_character = 1;   /* 313EA BP-C = 1 (member 0), with 98D7 */
    c->execution.state.reload_pending = 1; /* original 313FD, including initial restore */
    c->phase = DISPATCH_EXECUTE;
    return DISPATCH_CONTINUE;
}
/*
 * DISPATCH_EXECUTE (313FD): the interpreter, resumed after each service. The budget bounds one
 * executor call, not a script: the original's 313FD loop has no limit, and Clouds map 2Dh (14,13)'s
 * success branch (the flag-19h grant, its MoveObj and 128 op-35 lines) runs more than 256 operations
 * without a service. A budget return resumes at the same record (the executor returns before reading
 * it); 256 calls remain the native's guard against a script that never ends (an explicit internal
 * error). The script's end (the executor's external boundary 33690) goes on to DISPATCH_SCRIPT_END;
 * any other result returns: a stop, a malformed script, an internal error, or a host service.
 */
static xeen_dispatch_status dispatch_execute(xeen_event_dispatch *c, uint8_t *b, size_t bn,
                                             const xeen_event_index *idx, uint8_t *d, size_t n,
                                             size_t budget, xeen_event_request_fn fn, void *u)
{
    xeen_exec_status s;
    unsigned calls = 0;
    do {
        s = xeen_event_execute_interactive(b, bn, idx, d, n, &c->execution, budget, fn, u, NULL,
                                           NULL);
    } while (s == XEE_BUDGET && ++calls < 256u);
    c->selected_character = c->execution.selected_character;
    c->boundary = c->execution.state.boundary;
    if (s == XEE_EXTERNAL && c->boundary == XEEN_EVENT_SCRIPT_END) {
        c->phase = DISPATCH_SCRIPT_END;
        return DISPATCH_CONTINUE;
    }
    if (s == XEE_UNSUPPORTED) {
        return c->status = XD_UNSUPPORTED;
    }
    if (s == XEE_MALFORMED) {
        return c->status = XD_INPUT_ERROR;
    }
    if (s == XEE_INTERNAL || s == XEE_BUDGET) {
        return c->status = XD_INTERNAL;
    }
    return c->status = XD_INPUT_OR_SERVICE;
}
/*
 * DISPATCH_SCRIPT_END: both Clairvoyance sides stop animating. A script whose take/give operations asked for it
 * (window_cleanup) first closes every open window, restores the events in an event screen and redraws the party bar
 * (the cleanup phases); otherwise the defeat check follows. A handler that stopped never reaches this phase.
 * Original: 33690 (F466 = F467 = 2); 33698 tests BP-18, set by 31C62, for 186C0 and 2ABB8(1).
 */
static void dispatch_script_end(xeen_event_dispatch *c, uint8_t *d)
{
    d[XEEN_DS_CLAIRVOYANCE_RIGHT_STATE] = XEEN_CLAIRVOYANCE_STILL;
    d[XEEN_DS_CLAIRVOYANCE_LEFT_STATE] = XEEN_CLAIRVOYANCE_STILL;
    if (c->execution.window_cleanup) {
        c->window = 40;
        c->phase = DISPATCH_CLEANUP_SCAN;
    } else {
        c->phase = DISPATCH_DEFEAT_CHECK;
    }
}
/*
 * The descending window close (DISPATCH_CLOSE_SCAN, and DISPATCH_CLEANUP_SCAN for the script end's): the next open
 * window on the window stack, from c->window down, is marked closed and the phase becomes WINDOW_PHASE, where the
 * host restores its pixels (the mark is not repeated on resumption). With none left the stack is empty (depth
 * FFFFh) and the phase becomes DONE_PHASE (the events' restore in an event screen).
 * Original: 186C0 (its 4F57B; 3064 = FFFFh); 186FA's 4A1B7 in mode 9.
 */
static void close_scan(xeen_event_dispatch *c, uint8_t *d, uint8_t window_phase, uint8_t done_phase)
{
    while (c->window >= 0) {
        unsigned w = d[XEEN_DS_WINDOW_STACK + (unsigned)c->window];
        if (d[XEEN_DS_WINDOW_OPEN + w]) {
            d[XEEN_DS_WINDOW_OPEN + w] = 0;
            c->phase = window_phase;
            return;
        }
        --c->window;
    }
    ds_put_word(d, XEEN_DS_WINDOW_DEPTH, 65535);
    c->phase = done_phase;
}
/* The window the scan found (its stack entry at c->window): 4F57B's argument. */
static uint16_t scanned_window(const xeen_event_dispatch *c, const uint8_t *d)
{
    return (uint16_t)d[XEEN_DS_WINDOW_STACK + (unsigned)c->window];
}
/*
 * The party-defeat check: 1 unless an active member's worst condition is Confused or milder (Asleep included), or
 * Good: only paralysed, unconscious, dead, stone and eradicated members count as down. Each record's bytes up to its
 * conditions are copied from DS with 16-bit offsets.
 * Original: 2F869 (2AB96 the worst condition).
 */
static uint8_t party_defeated(const uint8_t *d)
{
    unsigned count = xeen_active_party_count(d);
    uint8_t record[XEEN_CHARACTER_CONDITIONS + XEEN_CONDITION_COUNT];
    for (unsigned i = 0; i < count; ++i) {
        unsigned selected = xeen_active_party_slot(d, i);
        unsigned base = XEEN_PARTY_RECORD(selected);
        uint8_t condition;
        for (unsigned j = 0; j < sizeof record; ++j) {
            record[j] = d[(uint16_t)(base + j)];
        }
        condition = xeen_character_worst_condition(record);
        if (condition <= XEEN_CONDITION_CONFUSED || condition == XEEN_CONDITION_GOOD) {
            return 0;
        }
    }
    return 1;
}
/*
 * DISPATCH_TREASURE: the treasure the script left pending is offered (the host's treasure dialog), then a chest
 * whose lock was picked during the script (a positive pick count, an object at the cell) is shown open (the host
 * has the map's object sprites). Then the pick count is reset, the UI mode restored and every window closed.
 * Original: 2EA68, 336DC (9755 > 0; 50D4:677C the object sprite list); 9755 = 0; 186C0 from window 40.
 */
static xeen_dispatch_status dispatch_treasure(xeen_event_dispatch *c, uint8_t *d, size_t n,
                                              xeen_event_request_fn fn, void *u)
{
    if (treasure_pending(d)) {
        int r = request_event_service(fn, u, XEEN_REQUEST_TREASURE_DIALOG, 0, d, n);
        if (r != 1) {
            return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_TREASURE_DIALOG);
        }
    }
    if (signed_byte(d[XEEN_DS_OBJECT_LOCK]) > 0 && d[XEEN_DS_EVENT_OBJECT]) {
        int r = request_event_service(fn, u, XEEN_REQUEST_OPENED_CHEST, 0, d, n);
        if (r != 1) {
            return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_OPENED_CHEST);
        }
    }
    d[XEEN_DS_OBJECT_LOCK] = 0;
    d[XEEN_DS_UI_MODE] = c->saved_mode;
    c->window = 40;
    c->phase = DISPATCH_CLOSE_SCAN;
    return DISPATCH_CONTINUE;
}
/*
 * The tail's notice when no record ran at a cell with an object: "Nothing here." (the host's modal) when no record
 * was there at all; when only records facing another direction were there, the refusal's sound, unless the cell's
 * step event is set. The notice's modal skips the sound test.
 * Original: 337D7 (BP-4F no record ran, 98D9 the object, BP-50 the direction mismatch); 3383D 1880E(15h) when F482
 * is clear (3385A, no DS write), then 33862; the modal 337F0 (1871B(26h), 4E38C(DS:63B1, 26h), 50BF6(26h), the key
 * loop 3380E..33832, 18765(26h)); 3383B JMP 33861.
 */
static xeen_dispatch_status dispatch_notice(xeen_event_dispatch *c, uint8_t *d, size_t n,
                                            xeen_event_request_fn fn, void *u)
{
    const xeen_event_state *s = &c->execution.state;
    int r;
    if (s->event_flag || !d[XEEN_DS_EVENT_OBJECT]) {
        return DISPATCH_CONTINUE; /* a record ran, or the cell has no object: no notice */
    }
    if (!s->direction_mismatch) {
        r = request_event_service(fn, u, XEEN_REQUEST_NOTHING_HERE, 0, d, n);
        if (r != 1) {
            return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_NOTHING_HERE);
        }
    } else if (!ds_word(d, XEEN_DS_CELL_STEP_EVENT)) {
        r = request_event_service(fn, u, XEEN_REQUEST_SOUND, XEEN_CUE_REFUSED, d, n);
        if (r != 1) {
            return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_REFUSED_SOUND);
        }
    }
    return DISPATCH_CONTINUE;
}
/*
 * DISPATCH_TAIL after the notice: a stash holding a treasure is copied back over the pending one, whatever the
 * script left there (an event's deferred gold is overwritten; items a dialog collected come back). A talking face
 * the script kept has its sprites freed. Then the member selector is 1 again, the Text option restored (see
 * dispatch_start), the portrait flashes of the dispatch cleared and the button set popped; the dispatcher returns
 * the script's AX (0, or operation 17's location result), RETURNED_EXIT when the party is defeated or the
 * exploration loop is to exit (LOOP_EXIT), else RETURNED.
 * Original: 33862 (the stash), 33AE9 (3000D mode 2's face: 4AA3E(8D56), 13FE7(8D56, 0, 10h)), 98D7 = 1, F384,
 * 8C78..8C7D, the pop, BP-A (0 from 313D3), 33B36.
 */
static xeen_dispatch_status dispatch_finish(xeen_event_dispatch *c, uint8_t *d, size_t n,
                                            xeen_event_request_fn fn, void *u)
{
    if (has_stashed_items(c->treasure_stash)) {
        stash(d, c->treasure_stash, 1);
    }
    if (ds_word(d, XEEN_DS_ANIMATION_SPRITES)) {
        int r = request_event_service(fn, u, XEEN_REQUEST_FREE_SPRITES, ds_word(d, XEEN_DS_ANIMATION_SPRITES), d, n);
        if (r != 1) {
            return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_FREE_SPRITES);
        }
        memset(d + XEEN_DS_ANIMATION_SPRITES, 0, 16);
    }
    d[XEEN_DS_MEMBER_SELECTOR] = 1;
    if (c->saved_layout != 255) {
        d[XEEN_DS_TEXT_OFF] = c->saved_layout;
    }
    memset(d + XEEN_DS_MEMBER_FLASHED, 0, 6);
    xeen_button_stack(d, XEEN_BUTTON_POP, c->ds_segment);
    c->return_ax = c->execution.return_ax;
    c->boundary = DISPATCH_RETURN;
    c->phase = DISPATCH_RETURNED;
    return c->status = (d[XEEN_DS_PARTY_DEFEATED] || ds_word(d, XEEN_DS_LOOP_EXIT)) ? XD_RETURNED_EXIT
                                                                         : XD_RETURNED;
}
/**
 * Runs the dispatcher from its phase until it returns, stops or needs the host (see the phases
 * above). A host service answered 0 stops at that service's own address (stop), any answer but 0 or
 * 1 is an internal error; the phase stays at the service, so a resumed call asks again. The window close and the
 * events' reload appear twice, as in the original's two calls of 186C0 and 186FA (operation 12's cleanup at 3369E
 * and the dispatcher's own): each copy has its own phases and stops.
 */
xeen_dispatch_status xeen_event_dispatch_run(xeen_event_dispatch *c, uint8_t *b, size_t bn,
                                             const xeen_event_index *idx, uint8_t *d, size_t n,
                                             size_t budget, xeen_event_request_fn fn, void *u)
{
    if (!c || !d || n < 65536u || !idx || (!b && bn)) {
        return XD_INPUT_ERROR;
    }
    if (c->status == XD_RETURNED || c->status == XD_RETURNED_EXIT) {
        return c->status;
    }
    for (;;) {
        xeen_dispatch_status s = DISPATCH_CONTINUE;
        int r;
        switch (c->phase) {
            case DISPATCH_BEGIN:
                s = dispatch_start(c, d, n);
                break;
            case DISPATCH_EXECUTE:
                s = dispatch_execute(c, b, bn, idx, d, n, budget, fn, u);
                break;
            case DISPATCH_SCRIPT_END:
                dispatch_script_end(c, d);
                break;
            case DISPATCH_CLEANUP_SCAN: /* 3369E's 186C0 */
                close_scan(c, d, DISPATCH_CLEANUP_WINDOW, DISPATCH_CLEANUP_RELOAD);
                break;
            case DISPATCH_CLEANUP_WINDOW:
                r = request_event_service(fn, u, XEEN_REQUEST_WINDOW_RESTORE, scanned_window(c, d), d, n);
                if (r != 1) {
                    return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_WINDOW_RESTORE);
                }
                --c->window;
                c->phase = DISPATCH_CLEANUP_SCAN;
                break;
            case DISPATCH_CLEANUP_RELOAD: /* its 186FA: 4A1B7 in mode 9 */
                if (d[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EVENT) {
                    r = request_event_service(fn, u, XEEN_REQUEST_RESTORE_EVENTS, 0, d, n);
                    if (r != 1) {
                        return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_RESTORE_EVENTS);
                    }
                }
                c->phase = DISPATCH_CLEANUP_PARTY_BAR;
                break;
            case DISPATCH_CLEANUP_PARTY_BAR: /* 336A3: 2ABB8(1), the party bar (presentation). */
                r = request_event_service(fn, u, XEEN_REQUEST_PARTY_BAR, 1, d, n);
                if (r != 1) {
                    return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_PARTY_BAR);
                }
                c->phase = DISPATCH_DEFEAT_CHECK;
                break;
            case DISPATCH_DEFEAT_CHECK: /* 2F869: F60A */
                d[XEEN_DS_PARTY_DEFEATED] = party_defeated(d);
                c->phase = DISPATCH_TREASURE;
                break;
            case DISPATCH_TREASURE:
                s = dispatch_treasure(c, d, n, fn, u);
                break;
            case DISPATCH_CLOSE_SCAN: /* the dispatcher's own 186C0 */
                close_scan(c, d, DISPATCH_CLOSE_WINDOW, DISPATCH_CLOSE_RELOAD);
                break;
            case DISPATCH_CLOSE_WINDOW:
                r = request_event_service(fn, u, XEEN_REQUEST_WINDOW_RESTORE, scanned_window(c, d), d, n);
                if (r != 1) {
                    return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_WINDOW_RESTORE);
                }
                --c->window;
                c->phase = DISPATCH_CLOSE_SCAN;
                break;
            case DISPATCH_CLOSE_RELOAD: /* 186FA: 4A1B7 in mode 9 */
                if (d[XEEN_DS_UI_MODE] == XEEN_UI_MODE_EVENT) {
                    r = request_event_service(fn, u, XEEN_REQUEST_RESTORE_EVENTS, 0, d, n);
                    if (r != 1) {
                        return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_RESTORE_EVENTS);
                    }
                }
                c->phase = DISPATCH_TAIL;
                break;
            case DISPATCH_TAIL: /* 337D7..33B36 */
                if ((s = dispatch_notice(c, d, n, fn, u)) == DISPATCH_CONTINUE) {
                    s = dispatch_finish(c, d, n, fn, u);
                }
                break;
            default:
                return c->status = XD_INTERNAL;
        }
        if (s != DISPATCH_CONTINUE) {
            return s;
        }
    }
}
