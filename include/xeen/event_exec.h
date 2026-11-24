#ifndef XEEN_EVENT_EXEC_H
#define XEEN_EVENT_EXEC_H
#include "xeen/events.h"
/* A bounded slice of 310B4, beginning at 31429 after resource preparation.
 * All storage is borrowed and must outlive the call. DS is caller-owned.
 * Resource bytes are mutable only through the recovered opcode-24 path;
 * record lengths and index keys remain unchanged.
 * This does not perform the dispatcher prologue, resource loading or cleanup. */
typedef enum {
    XEE_DONE,
    XEE_NO_MATCH,
    XEE_UNSUPPORTED,
    XEE_EXTERNAL,
    XEE_MALFORMED,
    XEE_BUDGET,
    XEE_INTERNAL
} xeen_exec_status;
typedef struct {
    int16_t x, y;      /* original BP-1E / BP-20 */
    uint16_t sequence; /* original BP-4, wraps at 16 bits */
    uint16_t row, offset;
    uint8_t operation, direction_mismatch, reload_pending,
            event_flag; /* BP-50, host marker, BP-4F */
    size_t steps;
    uint32_t boundary; /* analysis address, never runtime CS:IP */
    xeen_exec_status logical_result;
} xeen_event_state;
/* 15616 copies text selected by the unsigned byte at record+6 to DS:A827,
 * bounded by 1000 (the +6 read can cross a row boundary). Returning 1 means the caller performed that service;
 * 0 suspends at the call; other values report an internal/service error.
 * At 4A1B7 the caller must perform the resource preparation/reload service;
 * selector is zero. At 1C1C1 selector is query index 1; the service must
 * perform that original map query and retain DS:F490 for operation 54. The resource/index must remain coherent across this call.
 * No opcode-specific gameplay service is silently substituted. */
typedef int (*xeen_event_service_fn)(void *, uint32_t address, uint8_t text_selector, uint8_t *ds,
                                     size_t ds_size);
typedef struct {
    uint16_t row, offset, sequence_before, sequence_after;
    uint8_t operation;
    const uint8_t *record;
    size_t bytes;
    xeen_exec_status result;
    uint32_t boundary;
    int8_t branch_taken; /* -1 for no condition, else 0/1 */
} xeen_event_trace;
typedef void (*xeen_event_trace_fn)(void *, const xeen_event_trace *);
/* Initializes only observed selection locals. DS initialization belongs to the
 * caller. A full 64KiB logical DS view is required; never cast host structs. */
xeen_exec_status xeen_event_begin(xeen_event_state *, const uint8_t *, size_t);
/* Executes at most budget additional operations. May be called again after a
 * budget stop. Unsupported/external stops leave the current record pending.
 * Restarting a query-service stop repeats common text copy; only budget, text
 * and reload suspension points support continuation without repeating a service.
 * Text copy is performed before dispatch, including unsupported operations.
 * XEE_DONE/NO_MATCH are logical results in state; loop exits return XEE_EXTERNAL
 * at 33690, because the original cleanup remains unreconstructed. */
xeen_exec_status xeen_event_execute(uint8_t *, size_t, const xeen_event_index *, uint8_t *, size_t,
                                    xeen_event_state *, size_t budget, xeen_event_service_fn,
                                    void *, xeen_event_trace_fn, void *);
/* Expanded original interpreter state. The five return slots correspond to
 * BP-2A/BP-34/BP-3E; stack_index starts at FFFF as at original 313F8.
 * phase is a host suspension marker, not gameplay state: 0=new record,
 * 1=handler, 2=3000D, 3=50ABB, 4=common text copy, 5=operation1 formatted/presentation pending,
 * 6=operation2 overlay formatted/scene pending, 7=operation24 handle commit pending,
 * 8=operation17 location (277E5) pending: the host runs it and resumes with location_ax;
 * 14=operation48's choice made (choice_ax), its 4A1B7 restore pending,
 * 15=operation21's word entered (word_ax), 16=operation22's scene pending,
 * 17=operation52's fall done by the host: the 313CE restart.
 * The values are native, never original state; XEEN_EXEC_PHASE_* names each one after the operation
 * that writes it (src/events/event_exec.c), and execute accepts a resumed record only in a phase its
 * operation can be suspended in (phase_accepts_operation). */
enum {
    XEEN_EXEC_PHASE_NEW_RECORD = 0,   /* select a record (no suspension pending) */
    XEEN_EXEC_PHASE_HANDLER = 1,      /* the record's text is copied: its handler runs */
    XEEN_EXEC_PHASE_NPC_TALK = 2,     /* operation 5's 3000D */
    XEEN_EXEC_PHASE_SOUND = 3,        /* operation 6's 50ABB */
    XEEN_EXEC_PHASE_COPY_PENDING = 4, /* the common 15616 text copy */
    XEEN_EXEC_PHASE_MAIN_TEXT = 5,    /* operation 1's formatted text, its 315F4 pending */
    XEEN_EXEC_PHASE_SIGN_SCENE =
            6, /* operations 2/3/4/39: the overlay formatted, its scene pending */
    XEEN_EXEC_PHASE_AFTER_EVENT = 7, /* operation 24's records rewritten, the 4A24C store pending */
    XEEN_EXEC_PHASE_LOCATION = 8,    /* operation 17's 277E5 location, run by the host */
    XEEN_EXEC_PHASE_TELEPORT = 9,    /* operations 7/31's 3193B teleport, run by the host */
    XEEN_EXEC_PHASE_MEMBER_LOOP = 10, /* operations 12/28/29/38's member loop at loop_member */
    XEEN_EXEC_PHASE_SCENE = 11,       /* operations 41/53/50's scene */
    XEEN_EXEC_PHASE_PAGED_TEXT = 12,  /* their text (316DC / 31633) */
    XEEN_EXEC_PHASE_REMOVE_STORE =
            13, /* operations 14/46: records disabled, the 4A24C store pending */
    XEEN_EXEC_PHASE_CHOICE_MADE =
            14, /* operation 48's 2F5B1 answer (choice_ax), its 4A1B7 pending */
    XEEN_EXEC_PHASE_WORD_ENTERED = 15,   /* operation 21/55's 2E8E5 answer (word_ax) */
    XEEN_EXEC_PHASE_DAMAGE_SCENE = 16,   /* operations 22/33's 1B503(1) scene */
    XEEN_EXEC_PHASE_FALL_DONE = 17,      /* operation 52's 173A0(1) fall, done by the host */
    XEEN_EXEC_PHASE_TWO_LINES_WAIT = 18, /* operation 49's 2F2FF wait */
    /* 20..26: operation 20's chest lock (PICK_* in event_exec.c) */
    XEEN_EXEC_PHASE_RANDOM_DAMAGE = 27 /* operation 33's 2E68D, the roll kept in choice_ax */
};
typedef struct {
    xeen_event_state state;
    uint16_t stack_index;
    int16_t return_x[5], return_y[5];
    uint16_t return_sequence[5];
    uint8_t phase, selected_character;
    /* BP-18 (31C62, every operation 12: the 3369E window close and
     * 2ABB8(1) before 2F869) and BP-A (the dispatcher's AX; operation 17
     * stores the 277E5 location result, 3246F). location_ax is the host's
     * result for a pending operation 17 (phase 8). location_moved,
     * the location loaded another map (the 33EB1 pyramid, the 34FAA Reaper):
     * 3246F then ends the script without the record, whose map is gone. */
    uint8_t window_cleanup, location_moved;
    uint16_t return_ax, location_ax;
    /* Operation 12's member loop (31F58/32063/3215C: BP-6 the next
     * member, BP-16 its take mode, a 2B9C7 portrait flash pending for
     * loop_member) and BP-4E (313D9: the paged display's one 1B503(1) per
     * dispatch, 316EB). Phases 10 (operation 12 resumed at loop_member),
     * 11 (operation 41's scene) and 12 (its text, the host's 316DC pages). */
    uint8_t loop_member, loop_take, loop_flash, scene_shown;
    /* Operation 48's 2F5B1 answer (BP-6), kept in phase 14 while
     * its 4A1B7 restore is pending. also the 504DA result an
     * operation passes to 2E68D (operation 20's trap type in phase 26,
     * operation 33's damage in phase 27), kept while 2E68D's 30E2D(2) scene
     * suspends it, so the roll is not drawn again (the phases are exclusive;
     * the layout is unchanged). */
    uint16_t choice_ax;
    /* Operation 21's 2E8E5 answer (BP-E) in phase 15 (the host ran
     * the word entry and resumes with it); phase 16 is operation 22's
     * 1B503(1) (BP-4E) and phase 17 operation 52's 173A0(1), done by the
     * host, before the 313CE restart. */
    /* Operation 20's 2DECE lock-pick runs in phases 20..25 (20 the
     * trap roll, 21 the trap, 22 the value and roll, 23 the 1B503(1), 24
     * and 25 the 18F2F(A827, 1) after a pick and after a failure); no other
     * state crosses a suspension (the layout is unchanged). */
    /* word_ax is BP-E itself, a local of the 310B4 frame that the
     * frame never initialises: written by operations 21/55 (3291A: 2E8E5's
     * answer, a mirror row + 1) and 7/31 (31955: the map; 319D2: the mirror
     * row's map), read by operation 7/31's map 0 (3196B). The original
     * writes only 0..3Fh, FFFFh and 0..FFh there, so XEEN_BP_E_UNSET
     * (xeen_event_execution_begin) is "not written in this invocation"; the
     * 313CE restart (operation 31) keeps the frame. The layout is unchanged. */
    uint16_t word_ax;
} xeen_event_execution;
#define XEEN_BP_E_UNSET 0xfffeu
/*
 * The services an event operation requests from its host (xeen_event_request.address). Each is named for what it
 * does; its value is the address of the original routine the request stands for, which the traces, the stop
 * registry and the original-byte harness all report. A request's arguments are that routine's.
 */
enum xeen_event_service {
    XEEN_REQUEST_COPY_TEXT = 0x15616,           /* copy map text ARGUMENT into DS (destination, count) */
    XEEN_REQUEST_SOUND = 0x1880e,               /* play sound effect ARGUMENT */
    XEEN_REQUEST_SCROLL = 0x18f2f,              /* the scroll window over A827, closed by a key */
    XEEN_REQUEST_DRAW_SCENE = 0x1b503,          /* draw the 3D view (ARGUMENT 1: once per dispatch) */
    XEEN_REQUEST_NAVIGATION_QUERY = 0x1c1c1,    /* the map query ARGUMENT; its answer in DS:F490 */
    XEEN_REQUEST_CELL_METADATA = 0x1c844,       /* the cell (x, y)'s metadata words, for operation 35 */
    XEEN_REQUEST_RESET_TEMPORARIES = 0x29168,   /* the party's temporary bonuses end */
    XEEN_REQUEST_PAYMENT_REFUSAL = 0x291fb,     /* the message after operation 12's refused take */
    XEEN_REQUEST_MAXIMUM_HP = 0x2aac2,          /* a member's maximum HP */
    XEEN_REQUEST_PARTY_BAR = 0x2abb8,           /* redraw the party bar (ARGUMENT 1) */
    XEEN_REQUEST_MEMBER_FLASH = 0x2b9c7,        /* flash a member's portrait */
    XEEN_REQUEST_CONDITION_VALUE = 0x2bf6e,     /* a member's value of If condition ARGUMENT */
    XEEN_REQUEST_SET_WALL = 0x2de1d,            /* set the wall of cell (x, y) on a side, for operation 19 */
    XEEN_REQUEST_PARTY_DAMAGE = 0x2e68d,        /* operation 22's party damage, with its damage scene */
    XEEN_REQUEST_TREASURE_DIALOG = 0x2ea68,     /* the treasure's pick-up dialog */
    XEEN_REQUEST_YES_NO = 0x2f2ff,              /* wait for a yes/no answer */
    XEEN_REQUEST_NUMERIC_CHOICE = 0x2f5b1,      /* wait for a number key */
    XEEN_REQUEST_WHO_WILL = 0x2f9ba,            /* the "Who will ..." member prompt */
    XEEN_REQUEST_NPC_DIALOGUE = 0x3000d,        /* an NPC speaks: face ARGUMENT, mode SECOND_ARGUMENT */
    XEEN_REQUEST_GENERATE_ITEM = 0x305e8,       /* generate an item (level, record) */
    XEEN_REQUEST_CLOCK_ADVANCE = 0x30cf3,       /* advance the game clock by ARGUMENT minutes */
    XEEN_REQUEST_STAT_LINE_WINDOW = 0x315ae,    /* a window of one stat line */
    XEEN_REQUEST_TEXT_WINDOW = 0x315f4,         /* show the text at DESTINATION in window ARGUMENT */
    XEEN_REQUEST_MAP_TEXT = 0x31633,            /* show a map text */
    XEEN_REQUEST_PAGED_TEXT = 0x316dc,          /* a paged text display */
    XEEN_REQUEST_PAGED_TEXT_KEY = 0x31793,      /* the paged display's wait for any key */
    XEEN_REQUEST_TWO_LINES_WINDOW = 0x31804,    /* a window of two lines */
    XEEN_REQUEST_MONSTER_WORD = 0x3242c,        /* a word of a map monster's record */
    XEEN_REQUEST_HED_CELL = 0x32e6d,            /* set the party's cell of the loaded .hed, for operation 36 */
    XEEN_REQUEST_VOICE = 0x33064,               /* play a voice file, for operation 40 */
    XEEN_REQUEST_ENDING = 0x33518,              /* the endings' shared tail */
    XEEN_REQUEST_OPENED_CHEST = 0x336dc,        /* the dispatcher's opened-chest message */
    XEEN_REQUEST_NOTHING_HERE = 0x337d7,        /* the dispatcher's "Nothing here." */
    XEEN_REQUEST_NOTHING_HERE_KEY = 0x3381b,    /* "Nothing here."'s wait for any key */
    XEEN_REQUEST_ITEM_CLEAR = 0x43385,          /* clear an item row (record, row, category) */
    XEEN_REQUEST_ITEM_SORT = 0x43428,           /* sort a member's items of a category */
    XEEN_REQUEST_MAXIMUM_SP = 0x46470,          /* a member's maximum spell points */
    XEEN_REQUEST_RESTORE_EVENTS = 0x4a1b7,      /* restore the map's event script from its backing store */
    XEEN_REQUEST_STORE_EVENTS = 0x4a24c,        /* commit the event script back to its backing store */
    XEEN_REQUEST_FREE_SPRITES = 0x4aa3e,        /* free the sprites from handle ARGUMENT up */
    XEEN_REQUEST_WINDOW_RESTORE = 0x4f57b,      /* restore the screen under window ARGUMENT */
    XEEN_REQUEST_RANDOM = 0x504da,              /* a random number in a range */
    XEEN_REQUEST_EVENT_SOUND = 0x50abb          /* operation 6's sound effect */
};
/*
 * The other boundaries at which an execution hands control to its host (xeen_event_state.boundary with an external
 * result): the script's end, and the operations whose work the host performs before the script resumes. As with the
 * services, each value is the original address the traces report.
 */
enum xeen_event_continuation {
    XEEN_EVENT_SCRIPT_END = 0x33690,     /* the script ended (the interpreter's exit) */
    XEEN_EVENT_ENTER_LOCATION = 0x3245f, /* operation 17: the host runs the location and resumes with its AX */
    XEEN_EVENT_TELEPORT = 0x3193b,       /* operation 7's teleport */
    XEEN_EVENT_WORD_ENTRY = 0x2e8e5,     /* operation 21's word entry */
    XEEN_EVENT_FALL = 0x3341c            /* operation 52's fall */
};
/* Narrow original service call. 15616: argument=text selector, destination and
 * count are the DS copy span. 3000D: argument=unsigned record+8,
 * second_argument=unsigned record+9; reads DS:A827 and DS:A43F; returns AX.
 * 50ABB: argument is the original signed sound argument encoded as a word.
 * 4A1B7: restore the event resource from its retained backing store.
 * 1C1C1: argument=query index. Returning 0 suspends before service execution;
 * 1 means the service was performed; other values signal service failure.
 * Callback writes to DS and its AX result are retained. */
typedef struct {
    uint32_t address;
    uint16_t argument, second_argument, destination, count;
} xeen_event_request;
typedef int (*xeen_event_request_fn)(void *, const xeen_event_request *, uint8_t *, size_t,
                                     uint16_t *ax);
xeen_exec_status xeen_event_execution_begin(xeen_event_execution *, const uint8_t *, size_t);
/* Extended entry supports 5,6,25,26,34,51 in addition to the basic entry's set.
 * The old entry retains its seven-value contract for existing consumers.
 * Suspended extended services resume without repeating completed text copies.
 * Sixth nested push / empty pop are explicit bounded-input diagnostics. */
xeen_exec_status xeen_event_execute_expanded(uint8_t *, size_t, const xeen_event_index *, uint8_t *,
                                             size_t, xeen_event_execution *, size_t,
                                             xeen_event_request_fn, void *, xeen_event_trace_fn,
                                             void *);
/* Interactive entry: preserves the two contracts above. Adds operation 1's
 * presentation boundary at 315F4 (window 38, primary text), and operation 32's
 * signed arguments at 2F9BA (destination=1, the third original argument).
 * Operation2 formats the bounded original overlay and requests scene1 at 1B503;
 * scene suspension preserves the text copy, overlay and selected record.
 * Operation 9 condition44/value1 requests original 2F2FF(any-key,0); all
 * other operation9 conditions remain unsupported.
 * Operation 32's ED8C==1 early return and original F1..F6/Escape selection
 * service execute natively. AX==0 exits the record loop; AX!=0 selects that
 * one-based character for following character conditions. The verified Phase
 * 26 subset additionally covers condition opcode8/subcondition9 (signed SP),
 * opcode12 flag set, and the exact opcode20 random-gold tuple used by the
 * Clouds map28 money tree. Other tuples remain explicit unsupported/service
 * boundaries.
 * operation 11 (31BEE: an object moved, or flipped in place),
 * operation 15 (3232F: BP-C and 98D7 for selectors 0..6), operation 12
 * (31C62: the member loop and 2C94C's take modes 20, 34, 104 and give modes
 * 15, 16, 20, 34, 35, 104; a give that 2DBB8 ends requests 2B9C7(member)
 * once per member and dispatch), operation 20 (324E6: 2DECE's pending gold
 * 34 and gems 35 as well as the random gold), operation 41 (316DC: the
 * scene once, window 38 closed, AC0F, window 12 opened, then the host's
 * paged text at 316DC). Other modes stay explicit boundaries. */
xeen_exec_status xeen_event_execute_interactive(uint8_t *, size_t, const xeen_event_index *,
                                                uint8_t *, size_t, xeen_event_execution *, size_t,
                                                xeen_event_request_fn, void *, xeen_event_trace_fn,
                                                void *);

/*
 * The script operations (an index record's operation byte), by what each does (event_exec.c's handlers describe
 * them). 0, 13 and 47 have no handler (0 is the original's out-of-range default); none runs above 60. The names
 * follow the handlers' established behaviour; the If, TakeOrGive and ConfirmWord variants share one handler each.
 */
enum xeen_event_op {
    XEEN_OP_DISPLAY_TEXT = 1,
    XEEN_OP_DOOR_TEXT_SMALL = 2,
    XEEN_OP_DOOR_TEXT_LARGE = 3,
    XEEN_OP_SIGN_TEXT = 4,
    XEEN_OP_NPC = 5,
    XEEN_OP_PLAY_FX = 6,
    XEEN_OP_TELEPORT_AND_EXIT = 7,
    XEEN_OP_IF1 = 8,
    XEEN_OP_IF2 = 9,
    XEEN_OP_IF3 = 10,
    XEEN_OP_MOVE_OBJECT = 11,
    XEEN_OP_TAKE_OR_GIVE = 12,
    XEEN_OP_REMOVE = 14,
    XEEN_OP_SELECT_MEMBER = 15,
    XEEN_OP_SPAWN = 16,
    XEEN_OP_TOWN_EVENT = 17,
    XEEN_OP_EXIT = 18,
    XEEN_OP_ALTER_MAP = 19,
    XEEN_OP_GIVE_MULTI = 20,
    XEEN_OP_CONFIRM_WORD = 21,
    XEEN_OP_DAMAGE = 22,
    XEEN_OP_JUMP_RANDOM = 23,
    XEEN_OP_AFTER_EVENT = 24,
    XEEN_OP_CALL_EVENT = 25,
    XEEN_OP_RETURN = 26,
    XEEN_OP_SET_VARIABLE = 27,
    XEEN_OP_TAKE_OR_GIVE_2 = 28,
    XEEN_OP_TAKE_OR_GIVE_3 = 29,
    XEEN_OP_ENDING_CLOUDS = 30,
    XEEN_OP_TELEPORT_AND_CONTINUE = 31,
    XEEN_OP_WHO_WILL = 32,
    XEEN_OP_RANDOM_DAMAGE = 33,
    XEEN_OP_MOVE_WALL_OBJECT = 34,
    XEEN_OP_ALTER_CELL_FLAG = 35,
    XEEN_OP_ALTER_HED = 36,
    XEEN_OP_DISPLAY_STAT = 37,
    XEEN_OP_TAKE_OR_GIVE_4 = 38,
    XEEN_OP_SEAT_TEXT = 39,
    XEEN_OP_PLAY_VOICE = 40,
    XEEN_OP_DISPLAY_BOTTOM = 41,
    XEEN_OP_IF_NO_MONSTER = 42,
    XEEN_OP_SELECT_RANDOM_MEMBER = 43,
    XEEN_OP_GIVE_ENCHANTED = 44,
    XEEN_OP_ITEM_TYPE = 45,
    XEEN_OP_MAKE_NOTHING_HERE = 46,
    XEEN_OP_CHOOSE_NUMBER = 48,
    XEEN_OP_DISPLAY_TWO_LINES = 49,
    XEEN_OP_DISPLAY_LARGE = 50,
    XEEN_OP_EXCHANGE_OBJECTS = 51,
    XEEN_OP_FALL_TO_MAP = 52,
    XEEN_OP_DISPLAY_MAIN = 53,
    XEEN_OP_MAP_QUERY_JUMP = 54,
    XEEN_OP_CONFIRM_WORD_2 = 55,
    XEEN_OP_GOTO_RANDOM = 56,
    XEEN_OP_ENDING_DARKSIDE = 57,
    XEEN_OP_ENDING_WORLD = 58,
    XEEN_OP_FLIP_WORLD = 59,
    XEEN_OP_PLAY_CD = 60
};
#endif
