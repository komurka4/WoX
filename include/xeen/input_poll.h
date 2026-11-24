#ifndef XEEN_INPUT_POLL_H
#define XEEN_INPUT_POLL_H
#include "xeen/audio.h"
#include "xeen/sprites.h"
#include "xeen/presentation.h"
/* Native keyboard ABI: low byte is the original driver command (not a BIOS
 * scan code), high byte is original modifier state. Mouse x is screen column,
 * y screen row. click=1 means a queued press (the original driver queues a
 * left or right press alike and 4E18F returns only 0 or 1); the poll's >1
 * branch (18A15: Esc until release) is kept but unreachable from it. A disabled mouse clears both DS coordinates, like 4E225. DS F604
 * owns X and F602 owns Y (the original call passes the pointers reversed). */
typedef struct {
    uint16_t x, y, click;
    int enabled;
} xeen_mouse_sample;
typedef int (*xeen_key_provider)(void *, uint16_t *);
typedef int (*xeen_mouse_provider)(void *, xeen_mouse_sample *);
typedef struct {
    xeen_key_provider keyboard;
    xeen_mouse_provider mouse;
    void *user;
} xeen_input_provider;
typedef enum {
    XKEY_UP,
    XKEY_RIGHT,
    XKEY_DOWN,
    XKEY_LEFT,
    XKEY_ENTER,
    XKEY_ESCAPE,
    XKEY_SPACE,
    XKEY_F1,
    XKEY_F2,
    XKEY_F3,
    XKEY_F4,
    XKEY_F5,
    XKEY_F6,
    XKEY_1,
    XKEY_2,
    XKEY_3,
    XKEY_A,
    XKEY_B,
    XKEY_S,
    XKEY_R,
    XKEY_Y,
    XKEY_N,
    /* The original Esc command (1Bh) delivered to the game. XKEY_ESCAPE is
     * the host's close request and never reaches the game. */
    XKEY_GAME_ESCAPE,
    /* main-menu letters and the remaining digits (original codes). */
    XKEY_L,
    XKEY_C,
    XKEY_O,
    XKEY_V,
    XKEY_0,
    XKEY_4,
    XKEY_5,
    XKEY_6,
    XKEY_7,
    XKEY_8,
    XKEY_9,
    /* Tab (the control panel), Backspace and the other letters
     * (panel keys and save names). */
    XKEY_TAB,
    XKEY_BACKSPACE,
    XKEY_D,
    XKEY_E,
    XKEY_F,
    XKEY_G,
    XKEY_H,
    XKEY_I,
    XKEY_J,
    XKEY_K,
    XKEY_M,
    XKEY_P,
    XKEY_Q,
    XKEY_T,
    XKEY_U,
    XKEY_W,
    XKEY_X,
    XKEY_Z,
    /* F7 and F8 (4FEFF's CS:7AF8: scan 41h CFh, 42h D0h). */
    XKEY_F7,
    XKEY_F8,
    /* The printable punctuation 21h..2Fh, 3Ah..40h, 5Bh..60h and
     * 7Bh..7Eh, in that order (their ASCII codes): 2894F's words take any
     * character 20h..7Fh (the mirror's "darzog's tower", "&"). */
    XKEY_PUNCT_FIRST,
    XKEY_PUNCT_LAST = XKEY_PUNCT_FIRST + 31,
    /* The commands 61h..7Ah. The original keyboard driver never
     * returns them; 18C0C answers a click on a button whose code is one
     * (the character view's stat buttons a..t, creation's class buttons
     * a..j). The headless host sends them for the button's click. */
    XKEY_LOWER_FIRST,
    XKEY_LOWER_LAST = XKEY_LOWER_FIRST + 25,
    /* The keyboard driver's other extended commands (4FEFF's
     * CS:7AF8 scan table): Home (scan 47h) DCh, End (4Fh) DDh, PgUp (49h)
     * DEh, PgDn (51h) DFh, Ins (52h) E0h, F9 (43h) D1h. The Quests viewer
     * 3E24F (CS:072E: Home, End, PgUp, PgDn) and the spell lists of 3F05D
     * (CS:1728: PgUp, PgDn) read them; the other polls take them as any
     * unlisted key. F10 (44h, D2h) is 189E3's own 2675C screen: not mapped
     * (the frontend's boss key instead). */
    XKEY_HOME,
    XKEY_END,
    XKEY_PGUP,
    XKEY_PGDN,
    XKEY_INSERT,
    XKEY_F9,
    XKEY_EXTENDED_FIRST = XKEY_HOME,
    XKEY_EXTENDED_LAST = XKEY_F9,
    /* F10 is the frontend's boss key (xeen_platform_key_event);
     * it is no game command (translates to 0) and is never queued. */
    XKEY_F10 = XKEY_F9 + 1
} xeen_native_key;
/* The punctuation key of ASCII code C (XKEY_PUNCT_*), 0 if none. */
int xeen_input_punctuation_key(unsigned, xeen_native_key *);
/* The key of character C: a letter (either case) or digit its command key, space XKEY_SPACE, punctuation its
 * XKEY_PUNCT_* key; 0 for any other character. */
int xeen_input_character_key(unsigned c, xeen_native_key *);
/* The original keyboard reader's Shift (4FE2A with Shift held and Ctrl not): plain character C becomes the shifted
 * character of the highest scan code whose plain character it is (the driver's tables CS:7AF8, CS:7AC8, CS:7AE0), or
 * stays C. So '1'..'0' give "!@#$%^&*()", '=' '+', '`' '~', '\\' '|', '[' '{', ']' '}', ';' ':', '\'' '"', ',' '<',
 * '.' '>', '/' '?', '5' (also keypad 5) '%'; '-' stays '-' (the keypad's 4Ah is found first), letters and '*' stay. */
unsigned xeen_input_shift_character(unsigned c);
/* Whether the original's keyboard reader can return printable character C (20h..7Eh): a plain character of its scan
 * table or one Shift makes of it. Every printable ASCII character but '_' (in the shift table, never reached) and the
 * lower-case letters. */
int xeen_input_reader_character(unsigned c);
/*
 * A text key of a host keyboard layout (no Ctrl): UNSHIFTED the key's plain character, PRODUCED the character the
 * layout makes of the keystroke with SHIFT's state. The layout's character when the original's reader can return it;
 * otherwise, with Shift, the reader's own Shift rule on the plain character (a US layout's Shift+'-' is '-' as in the
 * original); otherwise none (0). Sets *KEY as xeen_input_character_key.
 */
int xeen_input_text_key(unsigned unshifted, unsigned produced, int shift, xeen_native_key *);
/*
 * The key codes of the original's keyboard poll (xeen_input_translate_key, the extended keys from its CS:7AF8):
 * letters and digits as themselves, Enter 0Dh, Esc 1Bh, F1..F8 C9h..D0h, the arrows F0h..F3h, Home/End DCh/DDh,
 * PgUp/PgDn DEh/DFh.
 */
#define XEEN_KEY_BACKSPACE 0x08u /* the line entries delete with it or Left (28ABA, the name entry) */
#define XEEN_KEY_ENTER     0x0du
#define XEEN_KEY_ESCAPE    0x1bu
#define XEEN_KEY_SPACE     0x20u /* also the wait command (the caller's table sends it to 17DA8) */
#define XEEN_KEY_F1        0xc9u
#define XEEN_KEY_F6        0xceu
#define XEEN_KEY_F8        0xd0u
#define XEEN_KEY_F10       0xd2u /* the original's DOS shell (XEEN_POLL_DOS_SHELL); the frontend's boss key takes it */
#define XEEN_KEY_CTRL_Q    0x11u /* in a menu, the program's exit (XEEN_POLL_QUIT) */
#define XEEN_KEY_HOME      0xdcu /* read only by the Quests viewer (CS:072E) */
#define XEEN_KEY_END       0xddu
#define XEEN_KEY_PAGE_UP   0xdeu
#define XEEN_KEY_PAGE_DOWN 0xdfu
#define XEEN_KEY_LEFT      0xf0u /* the arrow keys (also the movement commands' codes, XEEN_MOVE_*) */
#define XEEN_KEY_RIGHT     0xf1u
#define XEEN_KEY_UP        0xf2u
#define XEEN_KEY_DOWN      0xf3u

/* Ctrl held when the key was pressed, OR'd into a native key. The
 * original INT 9 handler 4FEFF records CS:7C9D (scan 1Dh) with each key and
 * the reader 4FE85 returns the command minus 40h when it is >= 40h: Ctrl+Left
 * F0h -> B0h (strafe left), Ctrl+Right F1h -> B1h, Ctrl+A 41h -> 01h. */
#define XKEY_CTRL 0x100u
uint16_t xeen_input_translate_key(xeen_native_key, uint8_t modifiers);
/* Deterministic provider for tests/replays; production frontends implement
 * the provider callbacks instead. Queues consume one key and one mouse sample
 * per call, independently; a click accepted by the poll leaves keys queued. */
#define XEEN_INPUT_QUEUE_MAX 64u
/* The mouse queue is the original press stack (4E172/4E18F): at
 * most 19 positions, the newest popped first; current_mouse is the pointer
 * position (and the driver's presence) returned when the stack is empty. */
#define XEEN_INPUT_MOUSE_STACK 19u
typedef struct {
    uint16_t keys[XEEN_INPUT_QUEUE_MAX];
    size_t key_head, key_count;
    xeen_mouse_sample mice[XEEN_INPUT_QUEUE_MAX];
    size_t mouse_head, mouse_count;
    xeen_mouse_sample current_mouse;
} xeen_input_queue;
void xeen_input_queue_init(xeen_input_queue *);
int xeen_input_queue_key(xeen_input_queue *, uint16_t);
int xeen_input_queue_mouse(xeen_input_queue *, xeen_mouse_sample);
xeen_input_provider xeen_input_queue_provider(xeen_input_queue *);
/* pending_music replaces the original separate code-segment byte 1658.
 * Song bytes are owned here after successful resource extraction. */
/* The poll's stack residues (xeen_input_poll.last_residue): a key that pressed an enabled live button, any other key,
 * a click on an enabled button (the analysis placement's values; see the field). */
#define XEEN_POLL_RESIDUE_BUTTON_KEY 0x8722u
#define XEEN_POLL_RESIDUE_KEY        0x4853u
#define XEEN_POLL_RESIDUE_CLICK      0x581du
typedef struct {
    uint8_t pending_music, awaiting_mouse_release;
    uint16_t ds_segment;
    uint8_t *song;
    size_t song_size;
    unsigned long polls, music_loads;
    /* A 989C key table on the original's stack (2F5B1's BP-10h:
     * 989C/989E = SS:offset, not DS): its bytes, kept while installed. */
    uint16_t stack_segment, stack_offset;
    uint8_t stack_keys[10], stack_count;
    /* The live tables (98AC..989A) may point into a caller's
     * stack frame (the main menu 1660E's BP-38..BP-1, Other Options 16B23):
     * the native keeps the frame's bytes here under a marker far address
     * frame_segment:frame_base..+frame_size; the table reads resolve those
     * pointers into this image (no host or original stack address). */
    uint16_t frame_segment, frame_base;
    uint8_t frame[0x40], frame_size;
    /* The word the last poll leaves 16h below its caller's SP
     * (3BAEE's unwritten [BP-4] reads it), from original-byte runs of
     * 189E3 (phase92/residue_probe.py): a keyboard key that pressed an
     * enabled live button 8722h (18B0A..18BF1: 50BF6(27h)'s return from
     * 4EF71); any other keyboard key 4853h (4FD76's 4FD2A/4FD39 INT 1Ah
     * pushes its CS there: the analysis placement's segment, a load-
     * dependent value under DOS); a click on an enabled button 581Dh (the
     * caller's DS that 50BF6 pushes there from 18C0C's feedback: the same
     * placement); 0 for any other click or no key (not modelled):
     * XEEN_POLL_RESIDUE_BUTTON_KEY, _KEY and _CLICK. */
    uint16_t last_residue;
    /* Owner-approved add-ons (NOT original): a native button set the caller
     * installs for one poll in place of the live DS tables' buttons (98AC..
     * 9896) for 18C0C's click test and 18AC5's key feedback; the portraits
     * (98D6) are still tested first. NULL (the default) is the original. */
    const struct xeen_poll_buttons *addon_buttons;
    /* Owner-approved add-ons (NOT original, Phase 211): buttons the caller
     * adds for one poll beside the live DS tables' (which stay in force):
     * tested after the portraits and before the tables for a click, and for
     * a key before the tables' feedback. NULL (the default) is the original. */
    const struct xeen_poll_buttons *addon_extra;
} xeen_input_poll;
/* Button i: the rectangle x, y, w, h, the command key, enabled (pressed
 * and released when chosen, as 18B0A..18BF1 do), and draw (the add-on's
 * frame of button i, pressed or not, into the presentation's framebuffer). */
typedef struct xeen_poll_buttons {
    unsigned count;
    uint16_t x[4], y[4], key[4];
    uint8_t w[4], h[4], enabled[4];
    void (*draw)(void *user, unsigned i, int pressed);
    void *user;
} xeen_poll_buttons;
/*
 * Where a poll stopped without a command (xeen_input_poll_result.boundary): the original address of the step that
 * needs what the host did not give, or of the poll's own wait for the mouse button's release.
 */
enum xeen_input_poll_boundary {
    XEEN_POLL_MUSIC_AUDIO = 0x189f3,        /* a song change is pending and no audio sink is given */
    XEEN_POLL_MUSIC_ARCHIVE = 0x18ed4,      /* the song's archive is not loaded (music_unavailable is set) */
    XEEN_POLL_MUSIC_LOAD = 0x18f03,         /* the song's name, resource or memory failed */
    XEEN_POLL_MOUSE_SAMPLE = 0x18a0a,       /* the host gave no mouse sample */
    XEEN_POLL_MOUSE_PRESENT = 0x4e18f,      /* the moved pointer's frame could not be presented */
    XEEN_POLL_MOUSE_RELEASE = 0x18a22,      /* the mouse button is held: the poll waits for its release */
    XEEN_POLL_CLICK_PRESENTATION = 0x18c0c, /* a click, and no presentation to find the control under it */
    XEEN_POLL_CLICK_TABLES = 0x18c6b,       /* the button tables of a click cannot be read */
    XEEN_POLL_KEY_TABLES = 0x18ae7,         /* the button tables of a key cannot be read */
    XEEN_POLL_BUTTON_ROW = 0x18bfe,         /* a button row cannot be read, or the table has no end in 128 rows */
    XEEN_POLL_FEEDBACK_TABLES = 0x18b0a,    /* the chosen button's position or size cannot be read */
    XEEN_POLL_FEEDBACK_DRAW = 0x18b33,      /* the chosen button's pressed frame cannot be drawn */
    XEEN_POLL_FEEDBACK_WINDOW = 0x18b76,    /* window 39 over the button cannot be updated */
    XEEN_POLL_FEEDBACK_DRAIN = 0x18d66,     /* the click's queued mouse samples cannot be drained */
    XEEN_POLL_KEYBOARD = 0x18a89,           /* the host gave no keyboard reading */
    /* Two keys end the poll with the original's own handler instead of a command: */
    XEEN_POLL_DOS_SHELL = 0x2675c,          /* F10: the DOS shell (never reached natively: the boss key takes F10) */
    XEEN_POLL_QUIT = 0x2c80d                /* Ctrl+Q inside a menu: the program's exit ("Exit to DOS!") */
};
typedef struct {
    uint16_t command;
    uint32_t boundary;                      /* an enum xeen_input_poll_boundary, 0 with a command */
    int music_unavailable;
} xeen_input_poll_result;
void xeen_input_poll_init(xeen_input_poll *, uint16_t ds_segment);
void xeen_input_poll_destroy(xeen_input_poll *);
void xeen_input_poll_request_music(xeen_input_poll *, uint16_t flag);
/* Installs count stack key bytes at segment:offset (989C/989E
 * written by the caller); count 0 removes them. */
void xeen_input_poll_stack_keys(xeen_input_poll *, uint16_t segment, uint16_t offset,
                                const uint8_t *keys, unsigned count);
/* installs (size 1..64) or removes (0) the frame image above. */
void xeen_input_poll_frame(xeen_input_poll *, uint16_t segment, uint16_t base, const uint8_t *bytes,
                           unsigned size);
xeen_status xeen_input_poll_run(xeen_input_poll *, xeen_movement *, const xeen_input_provider *,
                                xeen_native_audio *, const xeen_sprite_archives *,
                                xeen_presentation *, xeen_input_poll_result *);
#endif
