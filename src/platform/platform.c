#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "xeen/platform.h"
#include "xeen/wox_config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#elif defined(XEEN_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#endif

struct xeen_platform {
    int headless, closing, aspect, have_frame;
    int locked_scale; /* [display] locked_scale (an owner-approved host option): the picture keeps SCALE */
    unsigned scale, width, height;
    /* Phase 335: the configured scale (scale is the fitting one) and the client area available on the screen when
     * the window opened (0: unknown, as headless) */
    unsigned scale_wanted, avail_w, avail_h;
    uint64_t origin_ns, sampled_ticks, presented_ns;
    unsigned cursor_w, cursor_h; /* The INT 33h range's cursor size */
    xeen_bios_ticks bios;
    xeen_input_queue queue;
    /* The last presented frame (re-presented on expose/resize). */
    uint8_t pixels[64000], palette[768];
    /* The frame as painted, with the original cursor
     * composited at the live pointer when the host asks for it. */
    uint8_t shown[64000], cursor[32 * 32], cursor_mask[32 * 32];
    unsigned cursor_cw, cursor_ch;
    int painted_x, painted_y;
    int cursor_hidden;             /* The game's mouse is off (CS:16BE 0) */
    unsigned long cursor_repaints; /* Repaints for the pointer alone */
    /* The boss screen (its own image and palette; the
     * game's frame above is never touched), the window identity it swaps,
     * and the game clock's paused time. */
    int boss, swallow;
    unsigned long boss_entries, boss_exits, boss_consumed;
    uint64_t boss_since_ns, paused_ns;
    char title_now[128], title_saved[128];
    uint8_t boss_pixels[64000], boss_palette[768];
    xeen_platform_boss_hook boss_hook;
    void *boss_user;
    /* Ctrl+Q at every menu screen (the menu-context query), and a
     * Ctrl+Q press seen while the boss screen shows, decided on the game's
     * thread (xeen_platform_pump). */
    int (*menu_query)(void *);
    void *menu_user;
    unsigned long menu_quits;
    int boss_menu_quit;
    /* The left and
     * right buttons held, from every press and release (also outside the
     * image, where no game event is made). */
    unsigned buttons_held;
    /* Wheel capture on,
     * pending notches (> 0 up), the last notch's logical point, Win32's
     * partial delta (below 120). */
    int wheel_on, wheel, wheel_delta;
    uint16_t wheel_x, wheel_y;
    uint64_t (*clock)(void *);
    void *clock_user;
    #if defined(_WIN32)
    HWND window;
    uint32_t *rgb;
    int timer_period;
    /* Alt+Enter's borderless fullscreen and the windowed frame
     * it restores. */
    int fullscreen;
    WINDOWPLACEMENT placement;
    LONG_PTR style;
    int ctrl; /* Ctrl down as this window's own key messages say */
    /* The window belongs to
     * a UI-owner thread that pumps its messages, so Windows' modal move/size
     * and menu loops (WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE) block only that
     * thread: the game thread keeps running, presenting and (with the audio
     * engine's own threads) playing. The lock guards the frame, the input
     * queue, the pointer and the flags shared with it; the window procedure
     * releases it before DefWindowProc runs (the modal loop is inside it).
     * A 10 ms timer during a modal loop paints submitted frames. */
    CRITICAL_SECTION lock;
    HANDLE ui_thread, ui_ready;
    int ui_ok, dirty;
    unsigned modal;
    unsigned long paints;
    HICON icons[2][2]; /* [game, calculator][big, small] */
#elif defined(XEEN_HAVE_X11)
    Display *display;
    Window window;
    GC gc;
    Atom wm_delete;
    int fullscreen;
    XImage *image;
    uint8_t held[256];
    Cursor blank; /* Kept to restore after the boss screen */
    unsigned long *icon_data[2];
    size_t icon_len[2];
#endif
};
#if defined(_WIN32)
#define LOCK(p)           EnterCriticalSection(&(p)->lock)
#define UNLOCK(p)         LeaveCriticalSection(&(p)->lock)
#define XEEN_UI_TIMER     132u
#define WM_APP_CLOSE      (WM_APP + 1)
#define WM_APP_FULLSCREEN (WM_APP + 2)
/* WM_APP + 3 was a test-only modal loop (removed in Phase 285; its only sender went in Phase 284). */
#define WM_APP_BOSS_HIDE  (WM_APP + 4) /* A boss Ctrl+Q outside the menu: as any key */
#define WM_APP_FATAL      (WM_APP + 5) /* Phase 272: the fatal error's MessageBox (LPARAM a fatal_request) */
/* The game thread's fatal message for the UI-owner thread; DONE is set after the OK. */
typedef struct {
    const wchar_t *title, *text;
    HANDLE done;
} fatal_request;
#else
#define LOCK(p)   ((void)(p))
#define UNLOCK(p) ((void)(p))
#endif
/**
 * Returns a monotonic time in nanoseconds (QueryPerformanceCounter on Windows, CLOCK_MONOTONIC
 * elsewhere).
 */
static uint64_t native_ns(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * UINT64_C(1000000000) +
           (uint64_t)(counter.QuadPart % frequency.QuadPart) * UINT64_C(1000000000) /
                   (uint64_t)frequency.QuadPart;
#else
    struct timespec t = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0 && timespec_get(&t, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
#endif
}
/* The platform's monotonic clock (a test may replace it). */
static uint64_t now_ns(const xeen_platform *p)
{
    return p->clock ? p->clock(p->clock_user) : native_ns();
}
/* Letters and digits: the original command is the upper-case character;
 * punctuation is its own character (X11 keysyms below 80h are
 * ASCII; Win32 virtual keys name no punctuation: the text path maps it). */
static int character_key(unsigned c, xeen_native_key *key)
{
    int alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ';
    return alnum && xeen_input_character_key(c, key);
}
/**
 * Maps an X11 character C to a native key (letters, digits and punctuation); returns 0 for others.
 */
static int x11_character_key(unsigned c, xeen_native_key *key)
{
    return character_key(c, key) || xeen_input_punctuation_key(c, key);
}
/* The X11 keysyms (keysymdef.h) and Win32 virtual keys (winuser.h) the host maps, by their header names: this unit
 * builds without those headers. */
enum {
    X11_XK_Up = 0xff52,
    X11_XK_Down = 0xff54,
    X11_XK_Left = 0xff51,
    X11_XK_Right = 0xff53,
    X11_XK_KP_Up = 0xff97,
    X11_XK_KP_Down = 0xff99,
    X11_XK_KP_Left = 0xff96,
    X11_XK_KP_Right = 0xff98,
    X11_XK_Return = 0xff0d,
    X11_XK_KP_Enter = 0xff8d,
    X11_XK_Escape = 0xff1b,
    X11_XK_Tab = 0xff09,
    X11_XK_ISO_Left_Tab = 0xfe20,
    X11_XK_BackSpace = 0xff08,
    X11_XK_F1 = 0xffbe,
    X11_XK_F2 = 0xffbf,
    X11_XK_F3 = 0xffc0,
    X11_XK_F4 = 0xffc1,
    X11_XK_F5 = 0xffc2,
    X11_XK_F6 = 0xffc3,
    X11_XK_F7 = 0xffc4,
    X11_XK_F8 = 0xffc5,
    X11_XK_Home = 0xff50,
    X11_XK_End = 0xff57,
    X11_XK_Prior = 0xff55,
    X11_XK_Next = 0xff56,
    X11_XK_Insert = 0xff63,
    X11_XK_KP_Home = 0xff95,
    X11_XK_KP_End = 0xff9c,
    X11_XK_KP_Prior = 0xff9a,
    X11_XK_KP_Next = 0xff9b,
    X11_XK_KP_Insert = 0xff9e,
    X11_XK_F9 = 0xffc6,
    X11_XK_F10 = 0xffc7,
    X11_XK_KP_Begin = 0xff9d,
    X11_XK_KP_Multiply = 0xffaa,
    X11_XK_KP_Add = 0xffab,
    X11_XK_KP_Subtract = 0xffad,
    X11_XK_KP_Divide = 0xffaf,
};
enum {
    W32_VK_UP = 0x26,
    W32_VK_DOWN = 0x28,
    W32_VK_LEFT = 0x25,
    W32_VK_RIGHT = 0x27,
    W32_VK_RETURN = 0x0d,
    W32_VK_ESCAPE = 0x1b,
    W32_VK_TAB = 0x09,
    W32_VK_BACK = 0x08,
    W32_VK_F1 = 0x70,
    W32_VK_F2 = 0x71,
    W32_VK_F3 = 0x72,
    W32_VK_F4 = 0x73,
    W32_VK_F5 = 0x74,
    W32_VK_F6 = 0x75,
    W32_VK_F7 = 0x76,
    W32_VK_F8 = 0x77,
    W32_VK_HOME = 0x24,
    W32_VK_END = 0x23,
    W32_VK_PRIOR = 0x21,
    W32_VK_NEXT = 0x22,
    W32_VK_INSERT = 0x2d,
    W32_VK_F9 = 0x78,
    W32_VK_F10 = 0x79,
    W32_VK_OEM_PLUS = 0xbb,
    W32_VK_CLEAR = 0x0c,
    W32_VK_NUMPAD0 = 0x60,
    W32_VK_NUMPAD9 = 0x69,
    W32_VK_MULTIPLY = 0x6a,
    W32_VK_ADD = 0x6b,
    W32_VK_SUBTRACT = 0x6d,
    W32_VK_DECIMAL = 0x6e,
    W32_VK_DIVIDE = 0x6f,
};
/* X11 keysyms (keysymdef.h values). Physical Escape is the game's Esc. */
int xeen_platform_x11_key(unsigned long keysym, xeen_native_key *key)
{
    static const struct {
        unsigned long symbol;
        xeen_native_key key;
    } table[] = {{X11_XK_Up, XKEY_UP},
                 {X11_XK_Down, XKEY_DOWN},
                 {X11_XK_Left, XKEY_LEFT},
                 {X11_XK_Right, XKEY_RIGHT},
                 {X11_XK_KP_Up, XKEY_UP},
                 {X11_XK_KP_Down, XKEY_DOWN},
                 {X11_XK_KP_Left, XKEY_LEFT},
                 {X11_XK_KP_Right, XKEY_RIGHT}, /* keypad */
                 {X11_XK_Return, XKEY_ENTER},
                 {X11_XK_KP_Enter, XKEY_ENTER},
                 {X11_XK_Escape, XKEY_GAME_ESCAPE},
                 {X11_XK_Tab, XKEY_TAB},
                 {X11_XK_ISO_Left_Tab, XKEY_TAB},
                 {X11_XK_BackSpace, XKEY_BACKSPACE},
                 {X11_XK_F1, XKEY_F1},
                 {X11_XK_F2, XKEY_F2},
                 {X11_XK_F3, XKEY_F3},
                 {X11_XK_F4, XKEY_F4},
                 {X11_XK_F5, XKEY_F5},
                 {X11_XK_F6, XKEY_F6},
                 {X11_XK_F7, XKEY_F7},
                 {X11_XK_F8, XKEY_F8}, /* F7 CFh, F8 D0h (17E10) */
                 /* Home, End, Prior, Next, Insert (and their keypad keys:
                  * the same scan codes with NumLock off), F9 and F10 (the boss key). */
                 {X11_XK_Home, XKEY_HOME},
                 {X11_XK_End, XKEY_END},
                 {X11_XK_Prior, XKEY_PGUP},
                 {X11_XK_Next, XKEY_PGDN},
                 {X11_XK_Insert, XKEY_INSERT},
                 {X11_XK_KP_Home, XKEY_HOME},
                 {X11_XK_KP_End, XKEY_END},
                 {X11_XK_KP_Prior, XKEY_PGUP},
                 {X11_XK_KP_Next, XKEY_PGDN},
                 {X11_XK_KP_Insert, XKEY_INSERT},
                 {X11_XK_F9, XKEY_F9},
                 {X11_XK_F10, XKEY_F10}}; /* F10 the boss key */
    size_t i;
    if (!key) {
        return 0;
    }
    for (i = 0; i < sizeof table / sizeof table[0]; ++i) {
        if (table[i].symbol == keysym) {
            *key = table[i].key;
            return 1;
        }
    }
    /* The keypad's characters, whatever Num Lock says (the original's scan table: 4Ch '5', 37h '*', 4Eh '+', 4Ah
     * '-'; the '/' key sends scan 35h) */
    switch (keysym) {
        case X11_XK_KP_Begin:
            return character_key('5', key);
        case X11_XK_KP_Multiply:
            return xeen_input_punctuation_key('*', key);
        case X11_XK_KP_Add:
            return xeen_input_punctuation_key('+', key);
        case X11_XK_KP_Subtract:
            return xeen_input_punctuation_key('-', key);
        case X11_XK_KP_Divide:
            return xeen_input_punctuation_key('/', key);
        default:
            break;
    }
    return keysym < 0x80ul && x11_character_key((unsigned)keysym, key);
}
/* The keypad's character keysyms (X11_XK_KP_Begin ..) */
static int x11_keypad_character(unsigned long keysym)
{
    return keysym == X11_XK_KP_Begin || keysym == X11_XK_KP_Multiply || keysym == X11_XK_KP_Add ||
           keysym == X11_XK_KP_Subtract || keysym == X11_XK_KP_Divide;
}
int xeen_platform_x11_text(unsigned long keysym, unsigned produced, int shift, int ctrl, xeen_native_key *key)
{
    int letter = (keysym >= 'a' && keysym <= 'z') || (keysym >= 'A' && keysym <= 'Z');
    if (!key) {
        return 0;
    }
    if (x11_keypad_character(keysym)) {
        if (!xeen_platform_x11_key(keysym, key)) {
            return 0;
        }
        /* the reader's Shift on the keypad's character: keypad 5 is '%' */
        return ctrl || !shift || keysym != X11_XK_KP_Begin || xeen_input_text_key('5', 0, 1, key);
    }
    if (keysym >= 0x100ul || letter || ctrl) {
        return xeen_platform_x11_key(keysym, key); /* the commands, letters and Ctrl chords as before */
    }
    /* digits, space and punctuation (and a layout's other characters): the keystroke's text */
    return xeen_input_text_key(keysym < 0x80ul ? (unsigned)keysym : 0u, produced, shift, key);
}
/* Win32 virtual-key codes. Physical Escape is the game's Esc. */
int xeen_platform_win32_key(unsigned virtual_key, xeen_native_key *key)
{
    static const struct {
        unsigned code;
        xeen_native_key key;
    } table[] = {{W32_VK_UP, XKEY_UP},
                 {W32_VK_DOWN, XKEY_DOWN},
                 {W32_VK_LEFT, XKEY_LEFT},
                 {W32_VK_RIGHT, XKEY_RIGHT},
                 {W32_VK_RETURN, XKEY_ENTER},
                 {W32_VK_ESCAPE, XKEY_GAME_ESCAPE},
                 {W32_VK_TAB, XKEY_TAB},
                 {W32_VK_BACK, XKEY_BACKSPACE},
                 {W32_VK_F1, XKEY_F1},
                 {W32_VK_F2, XKEY_F2},
                 {W32_VK_F3, XKEY_F3},
                 {W32_VK_F4, XKEY_F4},
                 {W32_VK_F5, XKEY_F5},
                 {W32_VK_F6, XKEY_F6},
                 {W32_VK_F7, XKEY_F7},
                 {W32_VK_F8, XKEY_F8}, /* F7 CFh, F8 D0h (17E10) */
                 /* VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_INSERT, VK_F9. */
                 {W32_VK_HOME, XKEY_HOME},
                 {W32_VK_END, XKEY_END},
                 {W32_VK_PRIOR, XKEY_PGUP},
                 {W32_VK_NEXT, XKEY_PGDN},
                 {W32_VK_INSERT, XKEY_INSERT},
                 {W32_VK_F9, XKEY_F9},
                 {W32_VK_F10, XKEY_F10}}; /* VK_F10, the boss key */
    size_t i;
    if (!key) {
        return 0;
    }
    for (i = 0; i < sizeof table / sizeof table[0]; ++i) {
        if (table[i].code == virtual_key) {
            *key = table[i].key;
            return 1;
        }
    }
    /* VK_OEM_PLUS, the key of scan code 0Dh, which 4FEFF's
     * CS:7AF8 reads as '=' (17E20, the minimap). */
    if (virtual_key == W32_VK_OEM_PLUS) {
        return xeen_input_punctuation_key('=', key);
    }
    /* The keypad with Num Lock on: the original's scan table reads the keypad the same way either way (47h Home,
     * 48h Up, 49h PgUp, 4Bh Left, 4Ch '5', 4Dh Right, 4Fh End, 50h Down, 51h PgDn, 52h Ins; 53h Del none; 37h '*',
     * 4Eh '+', 4Ah '-'; '/' sends 35h). VK_CLEAR is keypad 5 with Num Lock off. */
    if (virtual_key >= W32_VK_NUMPAD0 && virtual_key <= W32_VK_NUMPAD9) {
        static const xeen_native_key keypad[10] = {XKEY_INSERT, XKEY_END,  XKEY_DOWN, XKEY_PGDN, XKEY_LEFT,
                                                   XKEY_5,      XKEY_RIGHT, XKEY_HOME, XKEY_UP,   XKEY_PGUP};
        *key = keypad[virtual_key - W32_VK_NUMPAD0];
        return 1;
    }
    switch (virtual_key) {
        case W32_VK_CLEAR:
            return character_key('5', key);
        case W32_VK_MULTIPLY:
            return xeen_input_punctuation_key('*', key);
        case W32_VK_ADD:
            return xeen_input_punctuation_key('+', key);
        case W32_VK_SUBTRACT:
            return xeen_input_punctuation_key('-', key);
        case W32_VK_DIVIDE:
            return xeen_input_punctuation_key('/', key);
        default:
            break;
    }
    return ((virtual_key >= 'A' && virtual_key <= 'Z') ||
            (virtual_key >= '0' && virtual_key <= '9') || virtual_key == ' ') &&
           character_key(virtual_key, key);
}
int xeen_platform_win32_text(unsigned virtual_key, unsigned unshifted, unsigned produced, int shift, int ctrl,
                             xeen_native_key *key)
{
    int letter = virtual_key >= 'A' && virtual_key <= 'Z';
    int text = (virtual_key >= '0' && virtual_key <= '9') || virtual_key == ' ';
    xeen_native_key mapped;
    if (!key) {
        return 0;
    }
    if (!text && virtual_key != W32_VK_OEM_PLUS && xeen_platform_win32_key(virtual_key, &mapped)) {
        /* the commands, letters, the keypad: as mapped; the reader's Shift makes keypad 5 '%' */
        *key = mapped;
        if (!ctrl && shift && !letter && mapped == XKEY_5) {
            return xeen_input_text_key('5', 0, 1, key);
        }
        return 1;
    }
    if (letter || virtual_key == W32_VK_DECIMAL) {
        return 0; /* (keypad Del, scan 53h: no character in the original's table) */
    }
    if (text) {
        unshifted = virtual_key;
    } else if (unshifted >= 0x80u) {
        unshifted = 0; /* a layout's dead or non-ASCII key */
    }
    if (ctrl) {
        /* Ctrl: the plain character, as the original's Ctrl (4FE85) takes it before Shift */
        return (text || xeen_input_reader_character(unshifted)) && xeen_input_character_key(unshifted, key);
    }
    return xeen_input_text_key(unshifted, produced, shift, key);
}
static void window_identity(xeen_platform *p);
static void fit_window(xeen_platform *p);
#if defined(_WIN32)
static void window_identity_force(xeen_platform *p);
#endif
/* The boss screen on or off (Windows: under the lock, on the UI
 * thread). The title and icon follow; the hook (the host's audio) is told. */
static void boss_set(xeen_platform *p, int on)
{
    if (!p || (on != 0) == (p->boss != 0)) {
        return;
    }
    if (on) {
        p->boss_since_ns = now_ns(p);
        p->boss = 1;
        ++p->boss_entries;
        memcpy(p->title_saved, p->title_now, sizeof p->title_saved);
        snprintf(p->title_now, sizeof p->title_now, "%s", "Calculator");
    } else {
        uint64_t t = now_ns(p);
        p->paused_ns += t >= p->boss_since_ns ? t - p->boss_since_ns : 0;
        p->boss = 0;
        ++p->boss_exits;
        memcpy(p->title_now, p->title_saved, sizeof p->title_now);
    }
    window_identity(p);
    if (p->boss_hook) {
        p->boss_hook(p->boss_user, p->boss);
    }
}
/* One physical key event seen by the boss screen. F10 shows it;
 * while it shows, a press that is no modifier (nor an Alt chord) hides it;
 * every key event then is consumed. Returns 1 when consumed. */
static int boss_event(xeen_platform *p, int f10, int modifier, int pressed, int repeat)
{
    if (p->boss) {
        if (pressed) {
            ++p->boss_consumed;
            if (!repeat && !modifier) {
                boss_set(p, 0);
                p->swallow = 1;
            }
        }
        return 1;
    }
    if (!f10) {
        return 0;
    }
    if (pressed && !repeat) {
        boss_set(p, 1);
    }
    return 1;
}
/* The queue mark (above every
 * game code, whose high byte is 0; keyboard() removes it) of a fresh Ctrl+Q
 * press with any other modifiers or keys held (Shift, Alt, Super: the
 * original's reader 4FE2A takes the Ctrl path before Shift's and 18A97 drops
 * Alt's AH, so 189E3's menu quit sees 11h for all of them); never an
 * auto-repeat. Ctrl+C is no chord: its plain 03h. */
#define QUIT_CHORD 0x8000u
/** Translates a native key to its game code, marking a fresh Ctrl+Q with the quit chord bit. */
static uint16_t chord_code(xeen_native_key key, int repeat)
{
    uint16_t code = xeen_input_translate_key(key, 0);
    if (key == (xeen_native_key)(XKEY_Q | XKEY_CTRL) && !repeat) {
        code = (uint16_t)(code | QUIT_CHORD);
    }
    return code;
}
int xeen_platform_key_event(xeen_platform *p, xeen_native_key key, int pressed, int repeat)
{
    return xeen_platform_key_event_modified(p, key, pressed, repeat, 0);
}
/* One physical key event KEY: PRESSED or released, REPEAT for an auto-repeat. A press enters the queue (the BIOS
 * buffer the game's polls read); an auto-repeat only while the queue is empty, so a held key repeats at the game's
 * pace without a backlog; a release is not a key. XKEY_ESCAPE is the host's close request, never a game command. The
 * boss screen takes its keys first (a fresh Ctrl+Q waits for the game's thread, boss_menu_quit; boss_event's F10 and
 * hiding key), and the key that hid it repeats into nothing until its release. OTHER_MODIFIERS (modifiers beside
 * Ctrl) is not used: Ctrl+Q's chord allows any. Returns 1 when the event was taken (queued, a release, the close
 * request, a boss key), 0 when it was dropped (outside the queue's keys, a repeat into a non-empty queue, a repeat of
 * the key that hid the boss screen). */
int xeen_platform_key_event_modified(xeen_platform *p, xeen_native_key key, int pressed, int repeat,
                                     int other_modifiers)
{
    unsigned base = (unsigned)key & ~(unsigned)XKEY_CTRL;
    (void)other_modifiers; /* Ctrl+Q's chord allows any */
    /* A fresh Ctrl+Q while the boss screen shows waits for the
     * game's thread (boss_menu_quit_resolve): the menu's -> the close, else
     * it hides the screen as any key does. */
    if (p && p->boss && pressed && !repeat && key == (xeen_native_key)(XKEY_Q | XKEY_CTRL)) {
        p->boss_menu_quit = 1;
        ++p->boss_consumed;
        return 1;
    }
    if (p && boss_event(p, base == (unsigned)XKEY_F10, 0, pressed, repeat)) {
        return 1;
    }
    /* The key that hid the boss screen repeats into nothing until
     * its release (or the next fresh press). */
    if (p && p->swallow) {
        if (!pressed) {
            p->swallow = 0;
            return 1;
        }
        if (repeat) {
            ++p->boss_consumed;
            return 0;
        }
        p->swallow = 0;
    }
    /* The extended keys (Home .. F9) come from the window too. */
    if (!p || (base > (unsigned)XKEY_PUNCT_LAST &&
               (base < (unsigned)XKEY_EXTENDED_FIRST || base > (unsigned)XKEY_EXTENDED_LAST))) {
        return 0;
    }
    if (!pressed) {
        return 1;
    }
    if (base == (unsigned)XKEY_ESCAPE) {
        p->closing = 1;
        return 1;
    }
    if (repeat && p->queue.key_count) {
        return 0;
    }
    return xeen_input_queue_key(&p->queue, chord_code(key, repeat));
}
/* The displayed rectangle: the largest integer multiple of 320x200 (320x240
 * with the 4:3 correction) that fits, centred; smaller windows shrink. With the
 * scale locked the multiple is at most the platform's scale: a larger window or
 * screen shows the picture at that scale with borders, a smaller one (a
 * fullscreen on a small screen) still the largest that fits. */
static void geometry(const xeen_platform *p, unsigned *x, unsigned *y, unsigned *w, unsigned *h)
{
    unsigned logical = p->aspect ? 240u : 200u, s = p->width / 320u;
    if (p->height / logical < s) {
        s = p->height / logical;
    }
    if (p->locked_scale && p->scale && s > p->scale) {
        s = p->scale;
    }
    if (s) {
        *w = 320u * s;
        *h = logical * s;
    } else if (p->width * logical <= p->height * 320u) {
        *w = p->width ? p->width : 1u;
        *h = *w * logical / 320u;
    } else {
        *h = p->height ? p->height : 1u;
        *w = *h * 320u / logical;
    }
    if (!*w) {
        *w = 1;
    }
    if (!*h) {
        *h = 1;
    }
    *x = p->width > *w ? (p->width - *w) / 2u : 0;
    *y = p->height > *h ? (p->height - *h) / 2u : 0;
}
/* The painted frame; with a host cursor, the cursor
 * image drawn where the pointer is now. */
static void compose(xeen_platform *p)
{
    unsigned x, y;
    int px = p->queue.current_mouse.x, py = p->queue.current_mouse.y;
    p->painted_x = -1;
    p->painted_y = -1;
    if (p->boss) {
        memcpy(p->shown, p->boss_pixels, sizeof p->shown);
        return;
    }
    memcpy(p->shown, p->pixels, sizeof p->shown);
    if (!p->cursor_cw || !p->queue.current_mouse.enabled || p->cursor_hidden) {
        return;
    }
    for (y = 0; y < p->cursor_ch && py + (int)y < 200; ++y) {
        for (x = 0; x < p->cursor_cw && px + (int)x < 320; ++x) {
            if (p->cursor_mask[y * 32u + x]) {
                p->shown[(unsigned)(py + (int)y) * 320u + (unsigned)(px + (int)x)] =
                        p->cursor[y * 32u + x];
            }
        }
    }
    p->painted_x = px;
    p->painted_y = py;
}
/* The painted frame's host cursor is not
 * where the pointer is now (a host cursor shown over a presented frame).
 * The window then repaints the last presented frame with the cursor at the
 * pointer, independently of the game thread's presents: a frame hold
 * (1C1B3's two BIOS ticks, a retrace) or a long game step no longer
 * freezes the pointer. The frame itself, the game's mouse samples and
 * every game timing are unchanged. */
static int cursor_stale(const xeen_platform *p)
{
    if (p->headless || !p->have_frame || p->boss || !p->cursor_cw ||
        !p->queue.current_mouse.enabled) {
        return 0;
    }
    if (p->cursor_hidden) {
        return p->painted_x != -1;
    }
    return p->painted_x != p->queue.current_mouse.x || p->painted_y != p->queue.current_mouse.y;
}
void xeen_platform_set_cursor(xeen_platform *p, const uint8_t *pixels, const uint8_t *opacity,
                              unsigned w, unsigned h)
{
    unsigned x, y;
    if (!p) {
        return;
    }
    LOCK(p);
    p->cursor_cw = pixels && opacity && w <= 32u && h <= 32u ? w : 0;
    p->cursor_ch = p->cursor_cw ? h : 0;
    for (y = 0; y < p->cursor_ch; ++y) {
        for (x = 0; x < w; ++x) {
            p->cursor[y * 32u + x] = pixels[y * w + x];
            p->cursor_mask[y * 32u + x] = opacity[y * w + x];
        }
    }
    UNLOCK(p);
}
void xeen_platform_show_cursor(xeen_platform *p, int shown)
{
    if (p) {
        LOCK(p);
        p->cursor_hidden = !shown;
        UNLOCK(p);
    }
}
/* The inverse of paint()'s sampling (column x*320/w, row y*200/h)
 * over the same rectangle as geometry(). */
/* The page position of window point (WX, WY) for a window of WIDTH x HEIGHT (geometry's rectangle; LOCKED_SCALE 0
 * when the scale is not locked). Returns 0 outside the picture. */
static int map_window_point(unsigned width, unsigned height, int aspect, unsigned locked_scale, int wx, int wy,
                            unsigned *lx, unsigned *ly)
{
    xeen_platform shape;
    unsigned x, y, w, h;
    if (!lx || !ly) {
        return 0;
    }
    memset(&shape, 0, sizeof shape);
    shape.width = width;
    shape.height = height;
    shape.aspect = aspect != 0;
    shape.locked_scale = locked_scale != 0;
    shape.scale = locked_scale;
    geometry(&shape, &x, &y, &w, &h);
    if (wx < (int)x || wy < (int)y || wx >= (int)(x + w) || wy >= (int)(y + h)) {
        return 0;
    }
    *lx = (unsigned)(wx - (int)x) * 320u / w;
    *ly = (unsigned)(wy - (int)y) * 200u / h;
    return 1;
}
int xeen_platform_map_pointer(unsigned width, unsigned height, int aspect, int wx, int wy,
                              unsigned *lx, unsigned *ly)
{
    return map_window_point(width, height, aspect, 0, wx, wy, lx, ly);
}
int xeen_platform_map_pointer_locked(unsigned width, unsigned height, int aspect, unsigned locked_scale, int wx,
                                     int wy, unsigned *lx, unsigned *ly)
{
    return map_window_point(width, height, aspect, locked_scale, wx, wy, lx, ly);
}
/* The platform's own window point: its size, aspect and locked scale. */
static int map_pointer(const xeen_platform *p, int wx, int wy, unsigned *lx, unsigned *ly)
{
    return map_window_point(p->width, p->height, p->aspect, p->locked_scale ? p->scale : 0u, wx, wy, lx, ly);
}
/* LX, LY held in the INT 33h range for the cursor size (x 2..318 - width, y 2..198 - height). */
static void clamp_pointer(const xeen_platform *p, unsigned *lx, unsigned *ly)
{
    unsigned right = 318u - p->cursor_w, bottom = 198u - p->cursor_h;
    *lx = *lx < 2u ? 2u : *lx > right ? right : *lx;
    *ly = *ly < 2u ? 2u : *ly > bottom ? bottom : *ly;
}
static void clamp_sample(const xeen_platform *p, xeen_mouse_sample *s)
{
    unsigned x = s->x, y = s->y;
    clamp_pointer(p, &x, &y);
    s->x = (uint16_t)x;
    s->y = (uint16_t)y;
}
/* A new range also moves the pointer (and the queued presses) into it, as INT 33h functions 7 and 8 do: a position
 * taken under the earlier range (the SC-55 start-up's window has none yet, so x up to 318) would otherwise stay
 * past the screen for the cursor while the pointer is outside the window. */
void xeen_platform_set_pointer_limits(xeen_platform *p, unsigned cursor_width,
                                      unsigned cursor_height)
{
    size_t i;
    if (p) {
        LOCK(p);
        p->cursor_w = cursor_width < 316u ? cursor_width : 316u;
        p->cursor_h = cursor_height < 196u ? cursor_height : 196u;
        if (p->queue.current_mouse.enabled) {
            clamp_sample(p, &p->queue.current_mouse);
        }
        for (i = 0; i < p->queue.mouse_count; ++i) {
            clamp_sample(p, &p->queue.mice[i]);
        }
        UNLOCK(p);
    }
}
/* The pointer inside the image moves the mouse to the logical
 * point, held in the original INT 33h range (4E0EE..4E10C: x 2..318 - cursor
 * width, y 2..198 - cursor height); a left or right press (button 1 or 2)
 * queues a click there, as the 4E172 callback (mask 0Ah) does. A release
 * and every point outside the image are no event. */
int xeen_platform_mouse_event(xeen_platform *p, int wx, int wy, int button, int pressed)
{
    unsigned lx, ly;
    xeen_mouse_sample sample;
    if (p && (button == 1 || button == 2)) {
        if (pressed) {
            p->buttons_held |= 1u << button;
        } else {
            p->buttons_held &= ~(1u << button);
        }
    }
    if (p && p->boss) {
        return 1; /* No mouse for the game while the boss screen shows */
    }
    if (!p || !map_pointer(p, wx, wy, &lx, &ly)) {
        return 0;
    }
    clamp_pointer(p, &lx, &ly);
    sample = (xeen_mouse_sample){(uint16_t)lx, (uint16_t)ly, 0, 1};
    if (!button || !pressed) {
        if (button && !pressed) {
            return 1;
        }
        return xeen_input_queue_mouse(&p->queue, sample);
    }
    if (button != 1 && button != 2) {
        return 0;
    }
    sample.click = 1;
    return xeen_input_queue_mouse(&p->queue, sample);
}
/* See platform.h. */
void xeen_platform_set_wheel(xeen_platform *p, int on)
{
    if (p) {
        LOCK(p);
        p->wheel_on = on != 0;
        p->wheel = 0;
        p->wheel_delta = 0;
        UNLOCK(p);
    }
}
/* NOTCHES of the wheel, up (> 0) or down (< 0), at window pixel WX, WY (the X11 buttons 4/5, Win32 WM_MOUSEWHEEL per
 * 120): recorded for xeen_platform_take_wheel at the logical point a click there would have (clamped as a click's);
 * a notch in the other direction drops the pending ones; at most 16 are kept either way. Dropped while the wheel is
 * off, while the boss screen shows, or outside the image (returns 0). */
static int xeen_platform_wheel_event(xeen_platform *p, int wx, int wy, int notches)
{
    unsigned lx, ly;
    if (!p || !p->wheel_on || p->boss || !notches) {
        return 0;
    }
    if (!map_pointer(p, wx, wy, &lx, &ly)) {
        return 0;
    }
    clamp_pointer(p, &lx, &ly);
    if ((p->wheel > 0 && notches < 0) || (p->wheel < 0 && notches > 0)) {
        p->wheel = 0;
    }
    p->wheel += notches;
    if (p->wheel > 16) {
        p->wheel = 16;
    }
    if (p->wheel < -16) {
        p->wheel = -16;
    }
    p->wheel_x = (uint16_t)lx;
    p->wheel_y = (uint16_t)ly;
    return 1;
}
int xeen_platform_wheel_input(xeen_platform *p, int wx, int wy, int notches)
{
    return xeen_platform_wheel_event(p, wx, wy, notches);
}
int xeen_platform_take_wheel(xeen_platform *p, uint16_t *x, uint16_t *y)
{
    int d = 0;
    if (!p || !x || !y) {
        return 0;
    }
    xeen_platform_pump(p);
    LOCK(p);
    if (p->wheel_on && p->wheel) {
        d = p->wheel > 0 ? 1 : -1;
        p->wheel -= d;
        *x = p->wheel_x;
        *y = p->wheel_y;
    }
    UNLOCK(p);
    return d;
}
#if defined(_WIN32)
static void paint(xeen_platform *p);
/* Alt+Enter's borderless window over the whole monitor (the image keeps its integer scale, centred, as in any window
 * size), with the windowed style and placement saved for the way back. Returns 0, nothing changed, when the
 * placement or the monitor cannot be read. */
static int enter_fullscreen(xeen_platform *p)
{
    HWND w = p->window;
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    p->placement.length = sizeof p->placement;
    if (!GetWindowPlacement(w, &p->placement) ||
        !GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi)) {
        return 0;
    }
    p->style = GetWindowLongPtrA(w, GWL_STYLE);
    /* WS_SYSMENU stays (it draws nothing without a caption): a window without it answers the shell's icon queries
     * with none, and a boss-screen icon change in fullscreen made the taskbar cache that. */
    SetWindowLongPtrA(w, GWL_STYLE,
                      (p->style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_SYSMENU);
    SetWindowPos(w, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    p->fullscreen = 1;
    return 1;
}
/* The saved windowed style and placement back. */
static void leave_fullscreen(xeen_platform *p)
{
    HWND w = p->window;
    SetWindowLongPtrA(w, GWL_STYLE, p->style);
    SetWindowPlacement(w, &p->placement);
    SetWindowPos(w, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    p->fullscreen = 0;
}
/* Alt+Enter: fullscreen and back. The icons a WM_SETICON gave the borderless popup do not survive the style change
 * back, so the title and icons are set again after every change, for the current (game or boss) state. */
static void toggle_fullscreen(xeen_platform *p)
{
    if (p->fullscreen) {
        leave_fullscreen(p);
    } else if (!enter_fullscreen(p)) {
        return;
    }
    window_identity_force(p);
    InvalidateRect(p->window, NULL, TRUE);
}
/* Ctrl held at this key message: its keyboard state (GetKeyState) or the window's own VK_CONTROL messages (p->ctrl,
 * cleared when the focus leaves, so it cannot stay stuck). */
static int ctrl_held(const xeen_platform *p)
{
    return (GetKeyState(VK_CONTROL) & 0x8000) || p->ctrl;
}
/* One Win32 key message's game key (xeen_platform_win32_text): the layout's plain character of a key that is no
 * letter (MapVirtualKey; a dead key none) and, with Shift (no Ctrl), the character the keystroke makes
 * (ToUnicodeEx with flag 4: the keyboard state, a pending dead key, is left as it was). No WM_CHAR is used, so a
 * keystroke is one key. Ctrl's bit is the caller's. */
static int win32_event_key(const xeen_platform *p, unsigned vk, LPARAM l, xeen_native_key *key)
{
    int ctrl = ctrl_held(p), shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    unsigned unshifted = 0, produced = 0;
    if (!(vk >= 'A' && vk <= 'Z')) {
        UINT u = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
        unshifted = (u & 0x80000000u) ? 0u : (unsigned)(u & 0xffffu);
        if (shift && !ctrl) {
            BYTE state[256];
            WCHAR text[4];
            if (GetKeyboardState(state) &&
                ToUnicodeEx(vk, (UINT)((l >> 16) & 0xff), state, text, 4, 4u, GetKeyboardLayout(0)) == 1) {
                produced = (unsigned)text[0];
            }
        }
    }
    return xeen_platform_win32_text(vk, unshifted, produced, shift, ctrl, key);
}
/* ---- The window procedure ------------------------------------------------------------------------------------
 * The window belongs to the UI-owner thread (ui_main), which runs its message loop; the game thread only posts to
 * it (WM_APP_*) and invalidates it. window_proc answers every message on that thread in two tiers:
 * - ui_thread_message: the modal-loop bracket and its paint timer, the game thread's requests and WM_DESTROY. They
 *   take the lock themselves or not at all: what they call dispatches further messages or waits for the user
 *   (DestroyWindow, the fatal MessageBoxW).
 * - shared_state_message: every other message, under the lock (the frame, the input queue, the pointer and the
 *   flags shared with the game thread). Its reply may ask for DefWindowProcA, which is called after the lock is
 *   released: the modal move/size and menu loops run inside it, and the game thread keeps presenting meanwhile.
 *   A message sent from code already under the lock (window_identity's WM_SETICON, a fullscreen toggle's WM_SIZE)
 *   nests: the lock is recursive, and that message's DefWindowProcA runs inside the outer hold; none of these
 *   starts a modal loop. */

/* A handler's answer: VALUE, or DefWindowProcA's (to_default), called once the lock is released. */
typedef struct {
    LRESULT value;
    int to_default;
} win32_reply;
static win32_reply reply(LRESULT value)
{
    win32_reply r = {value, 0};
    return r;
}
static win32_reply reply_default(void)
{
    win32_reply r = {0, 1};
    return r;
}
/* One key message as this window reads it. */
typedef struct {
    WPARAM vk;   /* the virtual key */
    LPARAM l;    /* the message's own (win32_event_key reads its scan code) */
    int pressed; /* WM_KEYDOWN, WM_SYSKEYDOWN */
    int repeat;  /* bit 30, the key was already down: a press's auto-repeat (always set on a release) */
    int alt;     /* bit 29, Alt held */
    int system;  /* WM_SYSKEYDOWN, WM_SYSKEYUP: the system's (DefWindowProcA) unless the window consumes it */
} win32_keystroke;
static win32_keystroke decode_keystroke(UINT message, WPARAM w, LPARAM l)
{
    win32_keystroke k;
    k.vk = w;
    k.l = l;
    k.pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    k.repeat = (l & (1L << 30)) != 0;
    k.alt = (l & (1L << 29)) != 0;
    k.system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    return k;
}
/* A modifier or lock key: it neither hides the calculator nor is a key press of its own there. */
static int modifier_key(WPARAM vk)
{
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN ||
           vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL || vk == VK_LSHIFT ||
           vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU ||
           vk == VK_RMENU;
}
/* Alt+Enter's press (not its auto-repeat) toggles the borderless fullscreen; the key is no game key and, kept from
 * DefWindowProcA, makes no menu beep. */
static win32_reply alt_enter(xeen_platform *p, const win32_keystroke *k)
{
    if (!k->repeat) {
        toggle_fullscreen(p);
    }
    return reply(0);
}
/* A fresh Ctrl+Q (any other modifiers) while the calculator shows is decided on the game thread
 * (boss_menu_quit_resolve: the menu's close, else it hides the calculator as any key does). */
static int calculator_ctrl_q(xeen_platform *p, const win32_keystroke *k)
{
    if (!p->boss || !k->pressed || k->repeat || k->vk != 'Q' || !ctrl_held(p)) {
        return 0;
    }
    p->boss_menu_quit = 1;
    ++p->boss_consumed;
    return 1;
}
/* F10, and every key while the calculator shows, are the calculator's (boss_event: F10 shows it; a press that is no
 * modifier nor Alt chord hides it). F10 never reaches DefWindowProcA (its menu-bar activation); the Alt chords stay
 * the system's (Alt+F4, Alt+Space). */
static win32_reply calculator_key(xeen_platform *p, const win32_keystroke *k)
{
    int f10 = k->vk == VK_F10, alt_chord = k->alt && !f10;
    (void)boss_event(p, f10, modifier_key(k->vk) || alt_chord, k->pressed, k->repeat);
    return alt_chord && k->system ? reply_default() : reply(0);
}
/* A game key: the layout's character or extended key (win32_event_key) with Ctrl's bit, to the input queue. */
static void game_key(xeen_platform *p, const win32_keystroke *k)
{
    xeen_native_key key;
    if (win32_event_key(p, (unsigned)k->vk, k->l, &key)) {
        /* The other modifiers held: xeen_platform_key_event_modified takes them but no longer uses them (Ctrl+Q's
         * chord allows any). */
        int other = (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000) ||
                    (GetKeyState(VK_RWIN) & 0x8000) || k->alt;
        if (ctrl_held(p)) {
            key = (xeen_native_key)(key | XKEY_CTRL);
        }
        (void)xeen_platform_key_event_modified(p, key, k->pressed, k->repeat, other);
    }
}
/* WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP. */
static win32_reply on_key(xeen_platform *p, UINT message, WPARAM w, LPARAM l)
{
    win32_keystroke k = decode_keystroke(message, w, l);
    if (message == WM_SYSKEYDOWN && k.vk == VK_RETURN) {
        return alt_enter(p, &k);
    }
    if (k.vk == VK_CONTROL) {
        p->ctrl = k.pressed;
    }
    if (calculator_ctrl_q(p, &k)) {
        return reply(0);
    }
    if (p->boss || k.vk == VK_F10) {
        return calculator_key(p, &k);
    }
    game_key(p, &k);
    return k.system ? reply_default() : reply(0);
}
/* The pointer moved, or the left or right button pressed or released, at the message's client point. The UI thread
 * repaints a cursor the pointer left behind itself (WM_PAINT, coalesced by Windows): the game thread may be inside a
 * frame hold or a game step (the owner's combat lag and rotation freeze). */
static win32_reply on_pointer(xeen_platform *p, HWND window, UINT message, LPARAM l)
{
    int x = (int)(short)LOWORD(l), y = (int)(short)HIWORD(l);
    int button = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP   ? 1
                 : message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 2
                                                                        : 0;
    (void)xeen_platform_mouse_event(p, x, y, button,
                                    message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN);
    if (cursor_stale(p)) {
        p->dirty = 1;
        ++p->cursor_repaints;
        InvalidateRect(window, NULL, FALSE);
    }
    return reply(0);
}
/* With the wheel captured, the delta in notches of 120 (a remainder waits for the next message), each at the
 * message's point (given on the screen). Not captured: Windows' default, as before. */
static win32_reply on_mouse_wheel(xeen_platform *p, HWND window, WPARAM w, LPARAM l)
{
    POINT pt;
    if (!p->wheel_on) {
        return reply_default();
    }
    pt.x = (short)LOWORD(l);
    pt.y = (short)HIWORD(l);
    (void)ScreenToClient(window, &pt);
    p->wheel_delta += (short)HIWORD(w);
    while (p->wheel_delta >= 120) {
        p->wheel_delta -= 120;
        (void)xeen_platform_wheel_event(p, (int)pt.x, (int)pt.y, 1);
    }
    while (p->wheel_delta <= -120) {
        p->wheel_delta += 120;
        (void)xeen_platform_wheel_event(p, (int)pt.x, (int)pt.y, -1);
    }
    return reply(0);
}
/* The client size (the image is centred at its integer scale in it); the whole window is repainted. */
static win32_reply on_size(xeen_platform *p, HWND window, LPARAM l)
{
    p->width = LOWORD(l);
    p->height = HIWORD(l);
    InvalidateRect(window, NULL, FALSE);
    return reply(0);
}
/* The retained frame, painted whole (paint, the black borders included): the update region is only validated. */
static win32_reply on_paint(xeen_platform *p, HWND window)
{
    PAINTSTRUCT ps;
    BeginPaint(window, &ps);
    EndPaint(window, &ps);
    paint(p);
    return reply(0);
}
/* The game draws its own cursor: over the client no system pointer while the game shows, the arrow over the
 * calculator. Elsewhere (the frame, the caption) Windows' default. */
static win32_reply on_set_cursor(xeen_platform *p, LPARAM l)
{
    if (LOWORD(l) != HTCLIENT) {
        return reply_default();
    }
    SetCursor(p->boss ? LoadCursor(NULL, IDC_ARROW) : NULL);
    return reply(TRUE);
}
/* The focus left (Alt+Tab, a click elsewhere): their releases may never come here, so Ctrl and the held buttons are
 * forgotten. */
static void on_focus_lost(xeen_platform *p)
{
    p->ctrl = 0;
    p->buttons_held = 0;
}
/* Every message but ui_thread_message's, under the lock. */
static win32_reply shared_state_message(xeen_platform *p, HWND window, UINT message, WPARAM w,
                                        LPARAM l)
{
    switch (message) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            return on_key(p, message, w, l);
        case WM_SYSCHAR: /* Alt+Enter's character is eaten (no menu beep); the others are the system's */
            return w == '\r' ? reply(0) : reply_default();
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            return on_pointer(p, window, message, l);
        case WM_MOUSEWHEEL:
            return on_mouse_wheel(p, window, w, l);
        case WM_SETCURSOR:
            return on_set_cursor(p, l);
        case WM_PAINT:
            return on_paint(p, window);
        case WM_ERASEBKGND: /* paint covers the whole client: nothing to erase */
            return reply(1);
        case WM_SIZE:
            return on_size(p, window, l);
        case WM_KILLFOCUS:
            on_focus_lost(p);
            return reply_default();
        case WM_CLOSE: /* the close button, Alt+F4: a request the game thread answers */
            p->closing = 1;
            return reply(0);
        default:
            return reply_default();
    }
}
/* While a modal move/size or menu loop runs (inside DefWindowProcA, blocking this thread's own loop), a 10 ms timer
 * paints the frames the game thread submits. The loops may nest: the timer runs from the first start to the last
 * end. */
static void modal_loop_started(xeen_platform *p, HWND window)
{
    if (!p->modal++) {
        SetTimer(window, XEEN_UI_TIMER, 10, NULL);
    }
}
static void modal_loop_ended(xeen_platform *p, HWND window)
{
    if (p->modal && !--p->modal) {
        KillTimer(window, XEEN_UI_TIMER);
    }
}
static void paint_submitted_frame(xeen_platform *p)
{
    LOCK(p);
    if (p->dirty) {
        paint(p);
    }
    UNLOCK(p);
}
/* The game thread's display mode request (xeen_platform_set_fullscreen): toggled only when it differs. */
static void fullscreen_request(xeen_platform *p, WPARAM on)
{
    LOCK(p);
    if ((on != 0) != (p->fullscreen != 0)) {
        toggle_fullscreen(p);
    }
    UNLOCK(p);
}
/* Phase 272: the game thread's fatal error (xeen_platform_fatal_dialog), owned by the window. The MessageBox's modal
 * loop runs on this thread and dispatches WM_PAINT, which repaints the retained frame; the window takes no input
 * until the OK, which DONE tells the waiting game thread. */
static void fatal_dialog_request(HWND window, fatal_request *q)
{
    (void)MessageBoxW(window, q->text, q->title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    SetEvent(q->done);
}
/* The game thread's verdict on a Ctrl+Q at the calculator outside the menu (boss_menu_quit_resolve): it hides the
 * calculator as any key does, the key's repeats and release swallowed. */
static void calculator_hide_request(xeen_platform *p)
{
    LOCK(p);
    if (p->boss) {
        boss_set(p, 0);
        p->swallow = 1;
    }
    UNLOCK(p);
}
/* The window is gone (WM_APP_CLOSE's DestroyWindow): the game thread sees no window, the message loop ends. */
static void window_destroyed(xeen_platform *p)
{
    LOCK(p);
    p->window = NULL;
    UNLOCK(p);
    PostQuitMessage(0);
}
/* The messages outside the lock: returns 1 when MESSAGE was handled here (its answer is 0), 0 when it continues to
 * shared_state_message (the modal loops' start and end, another timer: then DefWindowProcA's). */
static int ui_thread_message(xeen_platform *p, HWND window, UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
        case WM_GETMINMAXINFO:
            /* The locked scale's picture as the smallest client area while windowed (in fullscreen the popup takes
             * the monitor's size and geometry fits it). */
            if (p->locked_scale && !p->fullscreen) {
                RECT r = {0, 0, (LONG)(320u * p->scale), (LONG)((p->aspect ? 240u : 200u) * p->scale)};
                MINMAXINFO *mm = (MINMAXINFO *)l;
                AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
                mm->ptMinTrackSize.x = r.right - r.left;
                mm->ptMinTrackSize.y = r.bottom - r.top;
                return 1;
            }
            return 0;
        case WM_ENTERSIZEMOVE:
        case WM_ENTERMENULOOP:
            modal_loop_started(p, window);
            return 0;
        case WM_EXITSIZEMOVE:
        case WM_EXITMENULOOP:
            modal_loop_ended(p, window);
            return 0;
        case WM_TIMER:
            if (w != XEEN_UI_TIMER) {
                return 0;
            }
            paint_submitted_frame(p);
            return 1;
        case WM_APP_FULLSCREEN:
            fullscreen_request(p, w);
            return 1;
        case WM_APP_FATAL:
            fatal_dialog_request(window, (fatal_request *)l);
            return 1;
        case WM_APP_CLOSE:
            DestroyWindow(window);
            return 1;
        case WM_APP_BOSS_HIDE:
            calculator_hide_request(p);
            return 1;
        case WM_DESTROY:
            window_destroyed(p);
            return 1;
        default:
            return 0;
    }
}
/* The window's platform: CreateWindowA's parameter, kept in GWLP_USERDATA at WM_NCCREATE; NULL for the messages
 * before it (WM_GETMINMAXINFO). */
static xeen_platform *window_platform(HWND window, UINT message, LPARAM l)
{
    xeen_platform *p = (xeen_platform *)GetWindowLongPtr(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        p = (xeen_platform *)((CREATESTRUCTA *)l)->lpCreateParams;
        SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)p);
    }
    return p;
}
/** Win32 window procedure (the UI-owner thread): turns the window's messages into platform state. */
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    xeen_platform *p = window_platform(window, message, l);
    win32_reply r;
    if (!p) {
        return DefWindowProcA(window, message, w, l);
    }
    if (ui_thread_message(p, window, message, w, l)) {
        return 0;
    }
    LOCK(p);
    r = shared_state_message(p, window, message, w, l);
    UNLOCK(p);
    return r.to_default ? DefWindowProcA(window, message, w, l) : r.value;
}
/* An icon from the embedded resource (1 the game, 2 the
 * calculator: xeen_icons.rc, Explorer's too) at the system size, else from
 * the generated ARGB table (the nearest size not smaller). */
static HICON argb_icon(int which, int size)
{
    size_t n, i, best = 0;
    const xeen_icon_image *im = xeen_platform_icon_images(which, &n);
    BITMAPINFO bi;
    void *bits = NULL;
    HBITMAP color, mask;
    ICONINFO ii;
    HICON icon;
    HDC dc;
    if (!im || !n) {
        return NULL;
    }
    for (i = 0; i < n; ++i) {
        if (im[i].size >= (unsigned)size) {
            best = i;
            break;
        } else {
            best = i;
        }
    }
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = (LONG)im[best].size;
    bi.bmiHeader.biHeight = -(LONG)im[best].size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    dc = GetDC(NULL);
    color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color || !bits) {
        return NULL;
    }
    memcpy(bits, im[best].argb,
           (size_t)im[best].size * im[best].size * 4u); /* 0xAARRGGBB = B,G,R,A bytes */
    mask = CreateBitmap((int)im[best].size, (int)im[best].size, 1, 1, NULL);
    memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    if (mask) {
        DeleteObject(mask);
    }
    return icon;
}
/** Loads the window's large and small icons for the normal and boss-key states. */
static void win32_icons(xeen_platform *p)
{
    int which, k;
    for (which = 0; which < 2; ++which) {
        for (k = 0; k < 2; ++k) {
            int size = GetSystemMetrics(k ? SM_CXSMICON : SM_CXICON);
            p->icons[which][k] = (HICON)LoadImageA(
                    GetModuleHandle(NULL), MAKEINTRESOURCEA(1 + which), IMAGE_ICON, size, size, 0);
            if (!p->icons[which][k]) {
                p->icons[which][k] = argb_icon(which, size);
            }
        }
    }
}
/* XEEN_WINDOW_LOG=1 logs, after every title or
 * icon change, the style and what WM_GETICON (big, small, small2) and the
 * class report, to stderr. */
static void identity_log(xeen_platform *p, const char *why)
{
    static int on = -1;
    HWND w = p->window;
    if (on < 0) {
        const char *e = getenv("XEEN_WINDOW_LOG");
        on = e && *e && *e != '0';
    }
    if (!on || !w) {
        return;
    }
    fprintf(stderr,
            "window %s: boss=%d fullscreen=%d style=%08lx title=\"%s\" icon big=%p small=%p "
            "small2=%p class=%p/%p want=%p/%p\n",
            why, p->boss, p->fullscreen, (unsigned long)GetWindowLongPtrA(w, GWL_STYLE),
            p->title_now, (void *)SendMessageA(w, WM_GETICON, ICON_BIG, 0),
            (void *)SendMessageA(w, WM_GETICON, ICON_SMALL, 0),
            (void *)SendMessageA(w, WM_GETICON, ICON_SMALL2, 0),
            (void *)GetClassLongPtrA(w, GCLP_HICON), (void *)GetClassLongPtrA(w, GCLP_HICONSM),
            (void *)p->icons[p->boss][0], (void *)p->icons[p->boss][1]);
    fflush(stderr);
}
/* The window's own icons (the taskbar, Alt+Tab): ICONS' big, then small; a missing one is left as it is. */
static void send_window_icons(HWND w, HICON const icons[2])
{
    if (icons[0]) {
        SendMessageA(w, WM_SETICON, ICON_BIG, (LPARAM)icons[0]);
    }
    if (icons[1]) {
        SendMessageA(w, WM_SETICON, ICON_SMALL, (LPARAM)icons[1]);
    }
}
/* The title and icons of the current (game or boss) state. */
static void window_identity(xeen_platform *p)
{
    HWND w = p->window;
    if (!w) {
        return;
    }
    SetWindowTextA(w, p->title_now);
    /* The class icons follow too: the shell falls back to them when the
     * window's own are missing (WM_GETICON NULL). */
    if (p->icons[p->boss][0]) {
        SetClassLongPtrA(w, GCLP_HICON, (LONG_PTR)p->icons[p->boss][0]);
    }
    if (p->icons[p->boss][1]) {
        SetClassLongPtrA(w, GCLP_HICONSM, (LONG_PTR)p->icons[p->boss][1]);
    }
    send_window_icons(w, p->icons[p->boss]);
    p->dirty = 1;
    InvalidateRect(w, NULL, FALSE);
    identity_log(p, "identity");
}
/* After a style change: the icons are cleared, then set, so the shell sees a
 * real change and queries the window again (setting the same handle again is
 * no change: a taskbar icon cached as none would stay none). */
static void window_identity_force(xeen_platform *p)
{
    HWND w = p->window;
    if (!w) {
        return;
    }
    SendMessageA(w, WM_SETICON, ICON_BIG, 0);
    SendMessageA(w, WM_SETICON, ICON_SMALL, 0);
    window_identity(p);
    identity_log(p, p->fullscreen ? "fullscreen" : "windowed");
}
/* The window class (the game's icons, the arrow outside the client) and the window, its client 320x200 times the
 * scale; NULL when it cannot be created. On the UI-owner thread: the window belongs to its creator. */
static HWND create_game_window(xeen_platform *p)
{
    WNDCLASSEXA wc;
    RECT rect = {0, 0, (LONG)(320u * p->scale), (LONG)(200u * p->scale)};
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "XeenNativeExploration";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    win32_icons(p);
    wc.hIcon = p->icons[0][0];
    wc.hIconSm = p->icons[0][1];
    (void)RegisterClassExA(&wc);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    return CreateWindowA(wc.lpszClassName, p->title_now, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                         CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL,
                         wc.hInstance, p);
}
/* The UI-owner thread: creates and shows the window, tells xeen_platform_open (ui_ok, then ui_ready), then runs the
 * window's message loop until WM_QUIT (window_destroyed's). Without a window it ends at once. */
static DWORD WINAPI ui_main(LPVOID u)
{
    xeen_platform *p = u;
    HWND window = create_game_window(p);
    MSG msg;
    LOCK(p);
    p->window = window;
    p->ui_ok = window != NULL;
    UNLOCK(p);
    if (window) {
        send_window_icons(window, p->icons[0]);
        ShowWindow(window, SW_SHOW);
    }
    SetEvent(p->ui_ready);
    if (!window) {
        return 1;
    }
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
#endif
/* The _NET_WM_ICON payload of icon WHICH (each image: width,
 * height, ARGB pixels; CARDINAL items as C longs, as Xlib's format 32). */
size_t xeen_platform_x11_icon_payload(int which, unsigned long *out, size_t cap)
{
    size_t n, i, k = 0, total = 0, j;
    const xeen_icon_image *im = xeen_platform_icon_images(which, &n);
    for (i = 0; im && i < n; ++i) {
        total += 2u + (size_t)im[i].size * im[i].size;
    }
    if (!out) {
        return total;
    }
    if (cap < total) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        out[k++] = im[i].size;
        out[k++] = im[i].size;
        for (j = 0; j < (size_t)im[i].size * im[i].size; ++j) {
            out[k++] = im[i].argb[j];
        }
    }
    return total;
}
#if defined(XEEN_HAVE_X11) && !defined(_WIN32)
static void paint(xeen_platform *p);
/* The window's title (WM_NAME, _NET_WM_NAME, the icon names),
 * icon (_NET_WM_ICON) and pointer (none over the game, the system's over
 * the boss screen) for the current state, then its image. */
static void window_identity(xeen_platform *p)
{
    Display *d = p->display;
    Window w = p->window;
    Atom utf8;
    int len = (int)strlen(p->title_now);
    if (!d || !w) {
        return;
    }
    utf8 = XInternAtom(d, "UTF8_STRING", False);
    XStoreName(d, w, p->title_now);
    XSetIconName(d, w, p->title_now);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_NAME", False), utf8, 8, PropModeReplace,
                    (const unsigned char *)p->title_now, len);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_ICON_NAME", False), utf8, 8, PropModeReplace,
                    (const unsigned char *)p->title_now, len);
    if (p->icon_len[p->boss]) {
        XChangeProperty(d, w, XInternAtom(d, "_NET_WM_ICON", False), XA_CARDINAL, 32,
                        PropModeReplace, (const unsigned char *)p->icon_data[p->boss],
                        (int)p->icon_len[p->boss]);
    }
    if (p->boss) {
        XUndefineCursor(d, w);
    } else if (p->blank) {
        XDefineCursor(d, w, p->blank);
    }
    if (p->have_frame || p->boss) {
        paint(p);
    }
    XFlush(d);
}
/* Phase 335: the height an X11 window loses to its title bar and the desktop's panels. */
#define X11_FRAME_ALLOWANCE 80
/* The X11 window on the default display: the game's size, its GC, the close protocol (WM_DELETE_WINDOW), the blank
 * cursor (the game draws its own: an empty X cursor hides the system pointer over the window), the two window icons
 * (_NET_WM_ICON payloads: the game's and the boss screen's; an icon whose allocation fails is left out) and the
 * title, then the input mask, detectable auto-repeat (held keys report their repeats as presses without releases)
 * and the mapping. Returns 0 when the display cannot be opened (nothing made). */
static int x11_open_window(xeen_platform *p)
{
    static char empty[8];
    XColor black;
    Pixmap bits;
    int screen, which;
    if (!(p->display = XOpenDisplay(NULL))) {
        return 0;
    }
    screen = DefaultScreen(p->display);
    /* Phase 335: the screen less an allowance for the title bar and panels (X11 has no frame size before the
     * window exists); the scale shrinks to fit it */
    if (DisplayWidth(p->display, screen) > 0 && DisplayHeight(p->display, screen) > X11_FRAME_ALLOWANCE) {
        p->avail_w = (unsigned)DisplayWidth(p->display, screen);
        p->avail_h = (unsigned)DisplayHeight(p->display, screen) - X11_FRAME_ALLOWANCE;
        fit_window(p);
    }
    p->window = XCreateSimpleWindow(p->display, RootWindow(p->display, screen), 0, 0, p->width, p->height, 0, 0, 0);
    p->gc = XCreateGC(p->display, p->window, 0, NULL);
    p->wm_delete = XInternAtom(p->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(p->display, p->window, &p->wm_delete, 1);
    memset(&black, 0, sizeof black);
    bits = XCreateBitmapFromData(p->display, p->window, empty, 8, 8);
    p->blank = XCreatePixmapCursor(p->display, bits, bits, &black, &black, 0, 0);
    XFreePixmap(p->display, bits);
    for (which = 0; which < 2; ++which) {
        size_t n = xeen_platform_x11_icon_payload(which, NULL, 0);
        p->icon_data[which] = n ? malloc(n * sizeof *p->icon_data[which]) : NULL;
        p->icon_len[which] = p->icon_data[which] ? xeen_platform_x11_icon_payload(which, p->icon_data[which], n) : 0;
    }
    window_identity(p);
    XSelectInput(p->display, p->window,
                 KeyPressMask | KeyReleaseMask | FocusChangeMask | ExposureMask | StructureNotifyMask |
                         ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    (void)XkbSetDetectableAutoRepeat(p->display, True, NULL);
    XMapWindow(p->display, p->window);
    XFlush(p->display);
    return 1;
}
#elif !defined(_WIN32)
static void window_identity(xeen_platform *p)
{
    (void)p;
}
#endif
#if defined(_WIN32)
/* Starts the UI-owner thread and waits until its window exists or could not be made (ui_ready); with a window, the
 * 1 ms timer resolution, so a 1 ms wait is not the default 15.6 ms scheduler quantum (restored at close). Returns 0
 * without a window: xeen_platform_close then releases what was made. */
static int win32_open_window(xeen_platform *p)
{
    RECT work, frame = {0, 0, 0, 0};
    /* Phase 335: the desktop's work area (without the taskbar) less the window's frame; the scale shrinks to fit */
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0) && AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE)) {
        long w = (work.right - work.left) - (frame.right - frame.left);
        long h = (work.bottom - work.top) - (frame.bottom - frame.top);
        if (w > 0 && h > 0) {
            p->avail_w = (unsigned)w;
            p->avail_h = (unsigned)h;
            fit_window(p);
        }
    }
    p->rgb = malloc(64000u * sizeof *p->rgb);
    p->ui_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (p->rgb && p->ui_ready &&
        (p->ui_thread = CreateThread(NULL, 0, ui_main, p, 0, NULL)) != NULL) {
        WaitForSingleObject(p->ui_ready, INFINITE);
    }
    if (!p->ui_ok) {
        return 0;
    }
    timeBeginPeriod(1);
    p->timer_period = 1;
    return 1;
}
/* Ends the UI-owner thread and waits for it: the window destroys itself on its own thread (WM_APP_CLOSE, then
 * window_destroyed's WM_QUIT); a thread whose window is gone or was never made is sent WM_QUIT directly. */
static void win32_stop_ui_thread(xeen_platform *p)
{
    HWND window;
    if (!p->ui_thread) {
        return;
    }
    LOCK(p);
    window = p->window;
    UNLOCK(p);
    if (window) {
        PostMessageA(window, WM_APP_CLOSE, 0, 0);
    } else {
        PostThreadMessageA(GetThreadId(p->ui_thread), WM_QUIT, 0, 0);
    }
    WaitForSingleObject(p->ui_thread, INFINITE);
    CloseHandle(p->ui_thread);
}
/* After the UI thread ended: its start-up event, the timer resolution, the lock, the icons, the RGB buffers. */
static void win32_release(xeen_platform *p)
{
    int a, b;
    if (p->ui_ready) {
        CloseHandle(p->ui_ready);
    }
    if (p->timer_period) {
        timeEndPeriod(1);
    }
    DeleteCriticalSection(&p->lock);
    for (a = 0; a < 2; ++a) {
        for (b = 0; b < 2; ++b) {
            if (p->icons[a][b]) {
                DestroyIcon(p->icons[a][b]);
            }
        }
    }
    free(p->rgb);
}
#endif
void xeen_platform_physical_pixels(void)
{
#if defined(_WIN32)
    /* Looked up at run time: SetProcessDpiAwarenessContext is Windows 10 1703 and later, SetProcessDPIAware Vista. */
    HMODULE user = GetModuleHandleA("user32.dll");
    FARPROC context = user ? GetProcAddress(user, "SetProcessDpiAwarenessContext") : NULL;
    FARPROC system = user ? GetProcAddress(user, "SetProcessDPIAware") : NULL;
    if (context) {
        BOOL(WINAPI * set_context)(HANDLE);
        memcpy(&set_context, &context, sizeof set_context);
        if (set_context((HANDLE)(intptr_t)-4)) { /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
            return;
        }
    }
    if (system) {
        BOOL(WINAPI * set_system)(void);
        memcpy(&set_system, &system, sizeof set_system);
        (void)set_system();
    }
#endif
}
unsigned xeen_platform_fit_scale(unsigned scale, int aspect, unsigned avail_w, unsigned avail_h)
{
    unsigned s = scale;
    if (!avail_w || !avail_h) {
        return scale;
    }
    while (s > 1u && (320u * s > avail_w || (aspect ? 240u : 200u) * s > avail_h)) {
        --s;
    }
    return s;
}
unsigned xeen_platform_scale(const xeen_platform *p)
{
    return p ? p->scale : 0u;
}
/* The window's scale and size from the configured scale, the aspect and the available area (xeen_platform_fit_scale). */
static void fit_window(xeen_platform *p)
{
    p->scale = xeen_platform_fit_scale(p->scale_wanted, p->aspect, p->avail_w, p->avail_h);
    p->width = 320u * p->scale;
    p->height = (p->aspect ? 240u : 200u) * p->scale;
}
xeen_platform *xeen_platform_open(const char *title, unsigned scale, int headless, char *error,
                                  size_t n)
{
    xeen_platform *p;
    const char *failure = "native window backend unavailable in this build";
    if (error && n) {
        error[0] = 0;
    }
    if (scale < 1 || scale > XEEN_WOX_SCALE_MAX) {
        if (error && n) {
            snprintf(error, n, "window scale must be 1..%u", XEEN_WOX_SCALE_MAX);
        }
        return NULL;
    }
    p = calloc(1, sizeof *p);
    if (!p) {
        if (error && n) {
            snprintf(error, n, "platform allocation failed");
        }
        return NULL;
    }
    p->headless = headless != 0;
    p->scale = p->scale_wanted = scale;
    p->origin_ns = native_ns();
    snprintf(p->title_now, sizeof p->title_now, "%s", title ? title : "WoX");
    xeen_boss_render(p->boss_pixels, p->boss_palette);
    p->width = 320u * scale;
    p->height = 200u * scale;
    xeen_input_queue_init(&p->queue);
#if defined(_WIN32)
    InitializeCriticalSection(&p->lock);
#endif
    if (p->headless) {
        return p;
    }
    p->queue.current_mouse =
            (xeen_mouse_sample){160, 100, 0, 1}; /* A window has the mouse (4E0AE) */
#if defined(_WIN32)
    if (win32_open_window(p)) {
        return p;
    }
    failure = "Win32 window allocation failed";
#elif defined(XEEN_HAVE_X11)
    if (x11_open_window(p)) {
        return p;
    }
    failure = "cannot open X11 display; check DISPLAY or use --headless";
#else
    (void)title;
#endif
    if (error && n) {
        snprintf(error, n, "%s", failure);
    }
    xeen_platform_close(p);
    return NULL;
}
void xeen_platform_close(xeen_platform *p)
{
    if (!p) {
        return;
    }
#if defined(_WIN32)
    win32_stop_ui_thread(p);
    win32_release(p);
#elif defined(XEEN_HAVE_X11)
    if (p->image) {
        XDestroyImage(p->image);
    }
    free(p->icon_data[0]);
    free(p->icon_data[1]);
    if (p->display && p->blank) {
        XFreeCursor(p->display, p->blank);
    }
    if (p->display) {
        if (p->gc) {
            XFreeGC(p->display, p->gc);
        }
        if (p->window) {
            XDestroyWindow(p->display, p->window);
        }
        XCloseDisplay(p->display);
    }
#endif
    free(p);
}
#if defined(XEEN_HAVE_X11) && !defined(_WIN32)
/* The window manager's minimum size: the locked picture (320 x 200, or 240 with the aspect, times the scale), or
 * none. The window manager ignores it in fullscreen, where geometry fits a smaller screen. */
static void locked_size_hints(xeen_platform *p)
{
    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    if (p->locked_scale) {
        hints.flags = PMinSize;
        hints.min_width = (int)(320u * p->scale);
        hints.min_height = (int)((p->aspect ? 240u : 200u) * p->scale);
    }
    XSetWMNormalHints(p->display, p->window, &hints);
}
#endif
void xeen_platform_set_aspect(xeen_platform *p, int correct)
{
    if (!p) {
        return;
    }
    LOCK(p);
    p->aspect = correct != 0;
    if (!p->headless) {
        /* Phase 335: the taller picture may need a smaller scale (then a narrower window); else only the height */
        unsigned s = xeen_platform_fit_scale(p->scale_wanted, p->aspect, p->avail_w, p->avail_h);
        if (s != p->scale) {
            p->scale = s;
            p->width = 320u * s;
        }
        p->height = (p->aspect ? 240u : 200u) * p->scale;
    }
    UNLOCK(p);
    if (p->headless) {
        return;
    }
#if defined(_WIN32)
    {
        RECT rect = {0, 0, (LONG)p->width, (LONG)p->height};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(p->window, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER);
    }
#elif defined(XEEN_HAVE_X11)
    if (p->locked_scale) {
        locked_size_hints(p); /* the minimum follows the picture's height */
    }
    XResizeWindow(p->display, p->window, p->width, p->height);
    XFlush(p->display);
#endif
}
void xeen_platform_set_locked_scale(xeen_platform *p, int locked)
{
    if (!p) {
        return;
    }
    LOCK(p);
    p->locked_scale = locked != 0;
    UNLOCK(p);
#if defined(XEEN_HAVE_X11) && !defined(_WIN32)
    if (!p->headless && p->window) {
        locked_size_hints(p);
        XFlush(p->display);
    }
#endif
}
#if defined(XEEN_HAVE_X11) && !defined(_WIN32)
static void paint(xeen_platform *p);
/* _NET_WM_STATE_FULLSCREEN on or off through the window manager. */
static void x11_fullscreen(xeen_platform *p, int on)
{
    XEvent f;
    memset(&f, 0, sizeof f);
    f.xclient.type = ClientMessage;
    f.xclient.window = p->window;
    f.xclient.message_type = XInternAtom(p->display, "_NET_WM_STATE", False);
    f.xclient.format = 32;
    f.xclient.data.l[0] = on ? 1 : 0;
    f.xclient.data.l[1] = (long)XInternAtom(p->display, "_NET_WM_STATE_FULLSCREEN", False);
    f.xclient.data.l[3] = 1;
    XSendEvent(p->display, DefaultRootWindow(p->display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &f);
    XFlush(p->display);
    p->fullscreen = on;
}
#endif
#if defined(XEEN_HAVE_X11) && !defined(_WIN32)
/* Modifier keysyms (Shift..Hyper, Caps/Num/Scroll Lock, AltGr). */
static int x11_modifier(unsigned long sym)
{
    return (sym >= 0xffe1ul && sym <= 0xffeeul) || sym == 0xff7ful || sym == 0xff14ul ||
           sym == 0xfe03ul;
}
/* One X11 key event's game key (xeen_platform_x11_text): the first level's keysym and, with Shift (no Ctrl) on a key
 * that is no letter, the character the layout makes of the keystroke (XLookupString). */
static int x11_event_key(XKeyEvent *e, xeen_native_key *key)
{
    unsigned long sym = XLookupKeysym(e, 0);
    unsigned produced = 0;
    int shift = (e->state & ShiftMask) != 0, ctrl = (e->state & ControlMask) != 0;
    int letter = (sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z');
    if (shift && !ctrl && sym < 0x100ul && !letter) {
        char text[8];
        KeySym produced_sym;
        if (XLookupString(e, text, (int)sizeof text, &produced_sym, NULL) == 1) {
            produced = (unsigned char)text[0];
        }
    }
    return xeen_platform_x11_text(sym, produced, shift, ctrl, key);
}
/* The game key of key event K, with Ctrl from the event's own state (the press's modifiers); 0 for none. */
static int x11_game_key(XKeyEvent *k, xeen_native_key *key)
{
    if (!x11_event_key(k, key)) {
        return 0;
    }
    if (k->state & ControlMask) {
        *key = (xeen_native_key)(*key | XKEY_CTRL);
    }
    return 1;
}
/* Alt (Mod1) + Return or the keypad's Enter: the window manager's fullscreen toggle, no game key. */
static int x11_alt_enter(XKeyEvent *k)
{
    return (k->state & Mod1Mask) &&
           (XLookupKeysym(k, 0) == X11_XK_Return || XLookupKeysym(k, 0) == X11_XK_KP_Enter);
}
/* Alt+Enter's press (not its auto-repeat) asks the window manager for fullscreen (_NET_WM_STATE_FULLSCREEN) and
 * back; its press and release are the key's held state. */
static void x11_alt_enter_event(xeen_platform *p, XKeyEvent *k)
{
    unsigned code = k->keycode & 255u;
    int pressed = k->type == KeyPress;
    if (pressed && !p->held[code]) {
        x11_fullscreen(p, !p->fullscreen);
    }
    p->held[code] = (uint8_t)pressed;
}
/* Without detectable auto-repeat an auto-repeat arrives as a release and a press of the same key with the same
 * time: neither is a new keystroke. For RELEASE: when the next queued event is that press, it is taken from the
 * queue and reaches the game as one auto-repeat (the held state unchanged). Returns 1 then. */
static int x11_take_repeat(xeen_platform *p, XKeyEvent *release)
{
    XEvent next;
    xeen_native_key key;
    if (!XEventsQueued(p->display, QueuedAfterReading)) {
        return 0;
    }
    XPeekEvent(p->display, &next);
    if (next.type != KeyPress || next.xkey.keycode != release->keycode || next.xkey.time != release->time) {
        return 0;
    }
    XNextEvent(p->display, &next);
    if (x11_game_key(&next.xkey, &key)) {
        (void)xeen_platform_key_event(p, key, 1, 1);
    }
    return 1;
}
/* A key event while the boss screen shows: every key reaches it (Alt chords and modifiers do not hide it), except a
 * fresh Ctrl+Q press (any other modifiers), which waits for the game's thread (boss_menu_quit_resolve). Returns 1
 * when the event hid the screen (the game's frame is painted again). */
static int x11_boss_key(xeen_platform *p, XKeyEvent *k, int pressed, int repeat)
{
    unsigned long sym = XLookupKeysym(k, 0);
    if (pressed && !repeat && (k->state & ControlMask) && (sym == 'q' || sym == 'Q')) {
        p->boss_menu_quit = 1;
        ++p->boss_consumed;
        return 0;
    }
    (void)boss_event(p, 0, x11_modifier(sym) || (k->state & Mod1Mask), pressed, repeat);
    return !p->boss;
}
/* One key press or release (Alt+Enter aside): an auto-repeat pair, else the key's held state (a press of a key
 * already held is its auto-repeat), then the boss screen or the game. The game key carries whether Shift, Alt (Mod1)
 * or Super (Mod4) is held (Ctrl+Q's chord allows them). Returns 1 when the frame is to be painted again. */
static int x11_key(xeen_platform *p, XKeyEvent *k)
{
    unsigned code = k->keycode & 255u;
    int pressed = k->type == KeyPress, repeat = 0;
    xeen_native_key key;
    if (!pressed && x11_take_repeat(p, k)) {
        return 0;
    }
    if (pressed) {
        repeat = p->held[code];
        p->held[code] = 1;
    } else {
        p->held[code] = 0;
    }
    if (p->boss) {
        return x11_boss_key(p, k, pressed, repeat);
    }
    if (x11_game_key(k, &key)) {
        (void)xeen_platform_key_event_modified(p, key, pressed, repeat,
                                               (k->state & (ShiftMask | Mod1Mask | Mod4Mask)) != 0);
    }
    return 0;
}
/* Buttons 1 and 3 are left and right; the wheel (4..7) is none, except 4 (up) and 5 (down) pressed for the add-on
 * mouse_scroll (ignored while it is off). */
static void x11_button(xeen_platform *p, XButtonEvent *b)
{
    int pressed = b->type == ButtonPress;
    int button = b->button == Button1 ? 1 : b->button == Button3 ? 2 : 0;
    if (button) {
        (void)xeen_platform_mouse_event(p, b->x, b->y, button, pressed);
    } else if (pressed && (b->button == Button4 || b->button == Button5)) {
        (void)xeen_platform_wheel_event(p, b->x, b->y, b->button == Button4 ? 1 : -1);
    }
}
/* The window's new size; returns 1 when it changed (the frame is painted again at the new size). */
static int x11_resized(xeen_platform *p, const XConfigureEvent *c)
{
    if ((unsigned)c->width == p->width && (unsigned)c->height == p->height) {
        return 0;
    }
    p->width = (unsigned)c->width;
    p->height = (unsigned)c->height;
    return 1;
}
/** Processes the pending X11 events: keys, mouse, focus, resize, close and expose; then paints once when an
 * event asked for it and there is a frame (or the boss screen) to paint. */
static void x11_events(xeen_platform *p)
{
    int expose = 0;
    while (p->window && XPending(p->display)) {
        XEvent e;
        XNextEvent(p->display, &e);
        switch (e.type) {
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == p->wm_delete) {
                p->closing = 1;
            }
            break;
        case DestroyNotify:
            p->closing = 1;
            p->window = 0;
            break;
        case ConfigureNotify:
            expose |= x11_resized(p, &e.xconfigure);
            break;
        case Expose:
            expose = 1;
            break;
        case FocusOut:
            memset(p->held, 0, sizeof p->held); /* nothing stays logically pressed */
            break;
        case MotionNotify:
            (void)xeen_platform_mouse_event(p, e.xmotion.x, e.xmotion.y, 0, 0);
            break;
        case ButtonPress:
        case ButtonRelease:
            x11_button(p, &e.xbutton);
            break;
        case KeyPress:
        case KeyRelease:
            if (x11_alt_enter(&e.xkey)) {
                x11_alt_enter_event(p, &e.xkey);
            } else {
                expose |= x11_key(p, &e.xkey);
            }
            break;
        default:
            break;
        }
    }
    if (expose && (p->have_frame || p->boss)) {
        paint(p);
    }
}
#endif
/* A Ctrl+Q the boss screen took, decided here on the game's
 * thread (held while the screen shows, so the game's state is the one the
 * calculator was shown from): the menu's context -> the close request (the
 * game then leaves through its ordinary close path); otherwise the screen
 * hides as for any key, and the key (its repeats, its release) reaches no
 * game poll. */
static void boss_menu_quit_resolve(xeen_platform *p)
{
    int pending, quit;
    LOCK(p);
    pending = p->boss_menu_quit;
    p->boss_menu_quit = 0;
    UNLOCK(p);
    if (!pending) {
        return;
    }
    quit = p->menu_query && p->menu_query(p->menu_user);
    LOCK(p);
    if (quit) {
        p->closing = 1;
        ++p->menu_quits;
    } else if (p->boss) {
#if defined(_WIN32)
        if (!p->headless && p->window) {
            PostMessageA(p->window, WM_APP_BOSS_HIDE, 0, 0);
        } else {
            boss_set(p, 0);
            p->swallow = 1;
        }
#else
        boss_set(p, 0);
        p->swallow = 1;
#endif
    }
    UNLOCK(p);
}
void xeen_platform_pump(xeen_platform *p)
{
    if (!p) {
        return;
    }
    boss_menu_quit_resolve(p);
    if (p->headless) {
        return;
    }
#if defined(_WIN32)
    /* The UI-owner thread pumps the window's messages. */
    /* The game thread is held while the boss screen shows. */
    for (;;) {
        int on;
        LOCK(p);
        on = p->boss && !p->closing && p->window;
        UNLOCK(p);
        if (!on) {
            break;
        }
        Sleep(10);
        boss_menu_quit_resolve(p);
    }
#elif defined(XEEN_HAVE_X11)
    x11_events(p);
    while (p->boss && !p->closing && p->window) {
        struct timespec delay = {0, 10000000L};
        (void)nanosleep(&delay, NULL);
        x11_events(p);
        boss_menu_quit_resolve(p);
    }
    boss_menu_quit_resolve(p);
#endif
}
/* The initial display mode ([display] fullscreen), as Alt+Enter. */
void xeen_platform_set_fullscreen(xeen_platform *p, int on)
{
    if (!p || p->headless) {
        return;
    }
#if defined(_WIN32)
    {
        HWND window;
        LOCK(p);
        window = p->window;
        UNLOCK(p);
        if (window) {
            PostMessageA(window, WM_APP_FULLSCREEN, (WPARAM)(on != 0), 0);
        }
    }
#elif defined(XEEN_HAVE_X11)
    if ((on != 0) != (p->fullscreen != 0) && p->window) {
        x11_fullscreen(p, on != 0);
    }
#else
    (void)on;
#endif
}
/* The repaints made for the pointer alone (developer evidence). */
unsigned long xeen_platform_cursor_repaints(xeen_platform *p)
{
    unsigned long n;
    if (!p) {
        return 0;
    }
    LOCK(p);
    n = p->cursor_repaints;
    UNLOCK(p);
    return n;
}
int xeen_platform_cursor_stale(xeen_platform *p)
{
    int stale;
    if (!p) {
        return 0;
    }
    LOCK(p);
    stale = cursor_stale(p);
    UNLOCK(p);
    return stale;
}
#if defined(_WIN32)
/* The frames the UI-owner thread has painted so far (xeen_platform_present_hold waits for the next one). */
static unsigned long ui_paints(xeen_platform *p)
{
    unsigned long n;
    if (!p) {
        return 0;
    }
    LOCK(p);
    n = p->paints;
    UNLOCK(p);
    return n;
}
#endif
#if defined(_WIN32)
/* The fatal dialog owned by the window: shown by its own thread (WM_APP_FATAL, fatal_dialog_request) while this
 * thread waits for the OK. Returns 0 when there is no window to own it, or when the UI thread ended without showing
 * it: the caller shows an unowned one. */
static int owned_fatal_dialog(xeen_platform *p, const wchar_t *title, const wchar_t *text)
{
    HWND window;
    fatal_request q;
    if (!p || p->headless || !p->ui_thread) {
        return 0;
    }
    LOCK(p);
    window = p->window;
    UNLOCK(p);
    if (!window) {
        return 0;
    }
    q.title = title;
    q.text = text;
    q.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (q.done && PostMessageA(window, WM_APP_FATAL, 0, (LPARAM)&q)) {
        HANDLE wait[2] = {q.done, p->ui_thread};
        DWORD r = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
        CloseHandle(q.done);
        return r == WAIT_OBJECT_0;
    }
    if (q.done) {
        CloseHandle(q.done);
    }
    return 0;
}
#endif
int xeen_platform_fatal_dialog(xeen_platform *p, const char *title, const char *text)
{
#if defined(_WIN32)
    static LONG shown;
    static wchar_t wide_title[128], wide_text[2048];
    if (InterlockedExchange(&shown, 1)) {
        return 0;
    }
    /* The host's strings come from the ANSI APIs (argv, GetModuleFileNameA): the ANSI code page. */
    if (!title || !MultiByteToWideChar(CP_ACP, 0, title, -1, wide_title, 128)) {
        wcscpy(wide_title, L"WoX - Error");
    }
    if (!text || !MultiByteToWideChar(CP_ACP, 0, text, -1, wide_text, 2048)) {
        wcscpy(wide_text, L"WoX stopped because of an error. See wox.log.");
    }
    if (!owned_fatal_dialog(p, wide_title, wide_text)) {
        (void)MessageBoxW(NULL, wide_text, wide_title,
                          MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
    }
    return 1;
#else
    (void)p;
    (void)title;
    (void)text;
    return 0;
#endif
}
#if !defined(_WIN32)
/* Phase 335: no question window or program start without Windows (ask_win32.c has them). */
int xeen_platform_ask(const char *title, const char *text, const char *extra_button)
{
    (void)title;
    (void)text;
    (void)extra_button;
    return -1;
}
int xeen_platform_run(const char *path)
{
    (void)path;
    return 0;
}
#endif
int xeen_platform_should_close(const xeen_platform *p)
{
    int closing;
    if (!p) {
        return 1;
    }
    LOCK((xeen_platform *)(uintptr_t)p);
    closing = p->closing;
    UNLOCK((xeen_platform *)(uintptr_t)p);
    return closing;
}
/**
 * Keyboard callback of the platform's input provider: returns the next key from the platform's
 * queue (0 when none), pumping window events first.
 */
static int keyboard(void *u, uint16_t *key)
{
    xeen_platform *p = u;
    xeen_input_provider provider;
    if (!p || !key) {
        return 0;
    }
    {
        int got, quit = 0;
        xeen_platform_pump(p);
        LOCK(p);
        provider = xeen_input_queue_provider(&p->queue);
        got = provider.keyboard(provider.user, key);
        UNLOCK(p);
        if (got && (*key & QUIT_CHORD)) {
            /* The menu's context (the host's query, this thread, the
          * game's state now) -> the close request and no key (an empty
          * queue); otherwise the plain 11h (Ctrl+Q's code). */
            *key = (uint16_t)(*key & ~QUIT_CHORD);
            quit = p->menu_query && p->menu_query(p->menu_user);
            if (quit) {
                LOCK(p);
                p->closing = 1;
                ++p->menu_quits;
                UNLOCK(p);
                *key = 0;
            }
        }
        return got;
    }
}
int xeen_platform_pointer(xeen_platform *p, uint16_t *x, uint16_t *y)
{
    if (!p || !x || !y) {
        return 0;
    }
    xeen_platform_pump(p);
    LOCK(p);
    if (!p->queue.current_mouse.enabled) {
        UNLOCK(p);
        return 0;
    }
    *x = p->queue.current_mouse.x;
    *y = p->queue.current_mouse.y;
    UNLOCK(p);
    return 1;
}
int xeen_platform_button_held(xeen_platform *p)
{
    unsigned held;
    if (!p) {
        return 0;
    }
    xeen_platform_pump(p);
    LOCK(p);
#if defined(_WIN32)
    /* A release outside the window sends this window no message: the
     * physical buttons decide (either button; swap-independent). */
    if (p->buttons_held && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
        !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
        p->buttons_held = 0;
    }
#endif
    held = p->buttons_held;
    UNLOCK(p);
    return held != 0;
}
/* 4E18F through the press stack (the window's pointer, or none). */
static int mouse(void *u, xeen_mouse_sample *sample)
{
    xeen_platform *p = u;
    xeen_input_provider provider;
    if (!p || !sample) {
        return 0;
    }
    {
        int got;
        xeen_platform_pump(p);
        LOCK(p);
        provider = xeen_input_queue_provider(&p->queue);
        got = provider.mouse(provider.user, sample);
        UNLOCK(p);
        return got;
    }
}
xeen_input_provider xeen_platform_input(xeen_platform *p)
{
    xeen_input_provider provider = {keyboard, mouse, p};
    return provider;
}
int xeen_platform_queue_key(xeen_platform *p, xeen_native_key key)
{
    /* The headless host also sends the button codes 61h..7Ah
     * (XKEY_LOWER_*: a click's answer); the key events above refuse them. */
    if (!p || !p->headless ||
        ((unsigned)key & ~(unsigned)XKEY_CTRL) > (unsigned)XKEY_EXTENDED_LAST) {
        return 0;
    }
    if (((unsigned)key & ~(unsigned)XKEY_CTRL) == XKEY_ESCAPE) {
        p->closing = 1;
        return 1;
    }
    return xeen_input_queue_key(&p->queue, chord_code(key, 0));
}
#if (defined(XEEN_HAVE_X11) && !defined(_WIN32)) || defined(_WIN32)
/* A palette component of the game's 6-bit DAC (the low 6 bits of V) as an 8-bit value, 0..63 -> 0..255. */
static unsigned dac6_to_8(uint8_t v)
{
    return (v & 63u) * 255u / 63u;
}
#endif
#if defined(XEEN_HAVE_X11) && !defined(_WIN32)
/** Scales an 8-bit colour VALUE into the X11 visual's channel MASK. */
static unsigned long component(unsigned value, unsigned long mask)
{
    unsigned shift = 0;
    unsigned long maximum;
    if (!mask) {
        return 0;
    }
    while (!(mask & 1ul)) {
        mask >>= 1;
        ++shift;
    }
    maximum = mask;
    return (((unsigned long)value * maximum + 127ul) / 255ul) << shift;
}
/* The window's image at its current size: an image of another size is destroyed, a missing one created on the
 * default visual with zeroed data. Returns 0 when none could be made (nothing is painted). */
static int ensure_image(xeen_platform *p)
{
    int screen = DefaultScreen(p->display);
    if (p->image &&
        ((unsigned)p->image->width != p->width || (unsigned)p->image->height != p->height)) {
        XDestroyImage(p->image);
        p->image = NULL;
    }
    if (!p->image) {
        p->image = XCreateImage(p->display, DefaultVisual(p->display, screen),
                                (unsigned)DefaultDepth(p->display, screen), ZPixmap, 0, NULL,
                                p->width, p->height, 32, 0);
        if (!p->image) {
            return 0;
        }
        p->image->data = calloc((size_t)p->image->bytes_per_line, (size_t)p->image->height);
        if (!p->image->data) {
            XDestroyImage(p->image);
            p->image = NULL;
            return 0;
        }
    }
    return 1;
}
/* The 256 pixel values of the shown palette (the boss screen's while it shows) in the image's channel masks. */
static void x11_palette(const xeen_platform *p, unsigned long colors[256])
{
    const uint8_t *pal = p->boss ? p->boss_palette : p->palette;
    unsigned i;
    for (i = 0; i < 256u; ++i) {
        colors[i] = component(dac6_to_8(pal[i * 3u]), p->image->red_mask) |
                    component(dac6_to_8(pal[i * 3u + 1u]), p->image->green_mask) |
                    component(dac6_to_8(pal[i * 3u + 2u]), p->image->blue_mask);
    }
}
/** X11: scales the displayed page into the window with the current palette and puts the image. */
static void paint(xeen_platform *p)
{
    unsigned long colors[256];
    unsigned x, y, dx, dy, dw, dh, *column;
    int direct;
    if (!p->window || !p->width || !p->height || !ensure_image(p)) {
        return;
    }
    compose(p);
    geometry(p, &dx, &dy, &dw, &dh);
    column = malloc(dw * sizeof *column);
    if (!column) {
        return;
    }
    for (x = 0; x < dw; ++x) {
        column[x] = x * 320u / dw;
    }
    x11_palette(p, colors);
    {
        const uint16_t probe = 1; /* the host's byte order: a 32-bit LSB-first image takes the pixels as words */
        direct = p->image->bits_per_pixel == 32 && p->image->byte_order == LSBFirst &&
                 *(const uint8_t *)&probe == 1;
    }
    memset(p->image->data, 0, (size_t)p->image->bytes_per_line * (size_t)p->image->height);
    for (y = 0; y < dh; ++y) {
        const uint8_t *row = p->shown + (y * 200u / dh) * 320u;
        if (direct) {
            uint32_t *out =
                    (uint32_t *)(void *)(p->image->data +
                                         (size_t)(y + dy) * (size_t)p->image->bytes_per_line) +
                    dx;
            for (x = 0; x < dw; ++x) {
                out[x] = (uint32_t)colors[row[column[x]]];
            }
        } else {
            for (x = 0; x < dw; ++x) {
                XPutPixel(p->image, (int)(x + dx), (int)(y + dy), colors[row[column[x]]]);
            }
        }
    }
    free(column);
    XPutImage(p->display, p->window, p->gc, p->image, 0, 0, 0, 0, p->width, p->height);
    XFlush(p->display);
}
#elif defined(_WIN32)
/* COUNT pixels of SRC through the 6-bit palette PAL into RGB as 0x00RRGGBB (a 32-bit DIB's B, G, R bytes). */
static void expand_palette(const uint8_t *src, const uint8_t *pal, uint32_t *rgb, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        unsigned index = (unsigned)src[i] * 3u;
        rgb[i] = ((uint32_t)dac6_to_8(pal[index]) << 16) | ((uint32_t)dac6_to_8(pal[index + 1]) << 8) |
                 (uint32_t)dac6_to_8(pal[index + 2]);
    }
}
/* The client around the displayed rectangle (DX, DY, DW, DH) in black: above, below, left, right. */
static void fill_borders(HDC dc, const xeen_platform *p, unsigned dx, unsigned dy, unsigned dw,
                         unsigned dh)
{
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT r;
    r = (RECT){0, 0, (LONG)p->width, (LONG)dy};
    FillRect(dc, &r, black);
    r = (RECT){0, (LONG)(dy + dh), (LONG)p->width, (LONG)p->height};
    FillRect(dc, &r, black);
    r = (RECT){0, (LONG)dy, (LONG)dx, (LONG)(dy + dh)};
    FillRect(dc, &r, black);
    r = (RECT){(LONG)(dx + dw), (LONG)dy, (LONG)p->width, (LONG)(dy + dh)};
    FillRect(dc, &r, black);
}
/** Win32: stretches the displayed page into the window with the current palette (StretchDIBits). On the UI-owner
 * thread, under the lock. */
static void paint(xeen_platform *p)
{
    unsigned dx, dy, dw, dh;
    HDC dc;
    BITMAPINFO info;
    const uint8_t *pal;
    if (!p->window || (!p->have_frame && !p->boss) || !p->width || !p->height) {
        return;
    }
    p->dirty = 0;
    ++p->paints;
    compose(p);
    pal = p->boss ? p->boss_palette : p->palette;
    geometry(p, &dx, &dy, &dw, &dh);
    expand_palette(p->shown, pal, p->rgb, 64000u);
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = 320;
    info.bmiHeader.biHeight = -200;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    dc = GetDC(p->window);
    fill_borders(dc, p, dx, dy, dw, dh);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, (int)dx, (int)dy, (int)dw, (int)dh, 0, 0, 320, 200, p->rgb, &info, DIB_RGB_COLORS,
                  SRCCOPY);
    ReleaseDC(p->window, dc);
}
#endif
int xeen_platform_present(xeen_platform *p, const uint8_t *pixels, const uint8_t *palette)
{
    if (!p || !pixels || !palette) {
        return 0;
    }
    /* Headless keeps the frame too (xeen_platform_shown). */
    if (p->headless) {
        memcpy(p->pixels, pixels, sizeof p->pixels);
        memcpy(p->palette, palette, sizeof p->palette);
        p->have_frame = 1;
        return 1;
    }
#if defined(_WIN32)
    /* Submit the frame; the UI-owner thread paints it (WM_PAINT,
     * or its timer inside a modal loop). Never waits for the UI thread. */
    {
        HWND window;
        LOCK(p);
        memcpy(p->pixels, pixels, sizeof p->pixels);
        memcpy(p->palette, palette, sizeof p->palette);
        p->have_frame = 1;
        p->presented_ns = native_ns();
        p->dirty = 1;
        window = p->window;
        UNLOCK(p);
        if (window) {
            InvalidateRect(window, NULL, FALSE);
        }
    }
    return 1;
#elif defined(XEEN_HAVE_X11)
    memcpy(p->pixels, pixels, sizeof p->pixels);
    memcpy(p->palette, palette, sizeof p->palette);
    p->have_frame = 1;
    p->presented_ns = native_ns();
    paint(p);
    return 1;
#else
    return 0;
#endif
}
uint64_t xeen_platform_now_ns(void)
{
    return native_ns();
}
int xeen_platform_present_hold(xeen_platform *p, const uint8_t *pixels, const uint8_t *palette,
                               uint64_t until_ns)
{
    if (!p || !pixels || !palette) {
        return 0;
    }
    if (p->headless) {
        return 1;
    }
#if defined(_WIN32)
    {
        unsigned long before = ui_paints(p);
        unsigned k;
        if (!xeen_platform_present(p, pixels, palette)) {
            return 0;
        }
        for (k = 0; k < 50u && ui_paints(p) == before; ++k) {
            Sleep(1);
        }
    }
#else
    if (!xeen_platform_present(p, pixels, palette)) {
        return 0;
    }
#endif
    while (until_ns && native_ns() < until_ns) {
        uint64_t left = (until_ns - native_ns()) / UINT64_C(1000000);
        xeen_platform_wait(p, left > 1u ? (unsigned)(left > 20u ? 20u : left) : 1u);
    }
    return 1;
}
int xeen_platform_refresh(xeen_platform *p, const uint8_t *pixels, const uint8_t *palette)
{
    if (!p || !pixels || !palette) {
        return 0;
    }
    if (p->headless) {
        return 1;
    }
    {
        int same;
        LOCK(p);
        same = p->have_frame &&
               ((!memcmp(p->pixels, pixels, sizeof p->pixels) &&
                 !memcmp(p->palette, palette, sizeof p->palette) &&
                 (!p->cursor_cw ||
                  (p->cursor_hidden ? p->painted_x == -1
                                    : (p->painted_x == p->queue.current_mouse.x &&
                                       p->painted_y == p->queue.current_mouse.y)))) ||
                native_ns() - p->presented_ns < UINT64_C(16000000));
        UNLOCK(p);
        if (same) {
            return 1;
        }
    }
    return xeen_platform_present(p, pixels, palette);
}
xeen_status xeen_platform_advance(xeen_platform *p, uint64_t ticks)
{
    if (!p || !p->headless) {
        return XEEN_BAD_INPUT;
    }
    return xeen_timer_advance(&p->bios, ticks);
}
xeen_status xeen_platform_bios(void *u, xeen_timer_registers *r)
{
    xeen_platform *p = u;
    if (!p || !r) {
        return XEEN_BAD_INPUT;
    }
    if (!p->headless) {
        /* The game clock stops while the boss screen shows. */
        uint64_t now = xeen_platform_clock_ns(p),
                 elapsed = now >= p->origin_ns ? now - p->origin_ns : 0;
        uint64_t ms = elapsed / UINT64_C(1000000);
        uint64_t ticks = ms * UINT64_C(1193182) / UINT64_C(65536000);
        if (ticks > p->sampled_ticks) {
            xeen_status s = xeen_timer_advance(&p->bios, ticks - p->sampled_ticks);
            if (s != XEEN_OK) {
                return s;
            }
            p->sampled_ticks = ticks;
        }
    }
    return xeen_timer_deterministic_bios(&p->bios, r);
}
void xeen_platform_wait(xeen_platform *p, unsigned milliseconds)
{
    if (!p || p->headless) {
        return;
    }
#if defined(_WIN32)
    Sleep(milliseconds);
#elif defined(XEEN_HAVE_X11)
    /* The window's events (as xeen_platform_pump: input is only
     * queued, the game reads it at its polls) and the cursor repainted at
     * the pointer during the wait: frame holds and idle waits. */
    {
        struct timespec delay = {(time_t)(milliseconds / 1000u),
                                 (long)(milliseconds % 1000u) * 1000000L};
        (void)nanosleep(&delay, NULL);
        if (p->window) {
            /* also while the boss screen shows (Phase 335): its hiding key is read here too, as the UI thread does on
             * Windows. A wait on the platform clock (the movie player's, host_movie.c), which stands still while it
             * shows, otherwise never ends. */
            x11_events(p);
            if (p->boss) {
                boss_menu_quit_resolve(p);
            } else if (cursor_stale(p)) {
                ++p->cursor_repaints;
                paint(p);
            }
        }
    }
#else
    {
        struct timespec delay = {(time_t)(milliseconds / 1000u),
                                 (long)(milliseconds % 1000u) * 1000000L};
        (void)nanosleep(&delay, NULL);
    }
#endif
}
void xeen_platform_hold(xeen_platform *p, uint64_t pit_cycles, uint64_t bios_ticks, int clock_stands)
{
    uint64_t span, start, now;
    if (!p) {
        return;
    }
    if (p->headless) {
        (void)xeen_timer_advance(&p->bios, bios_ticks);
        return;
    }
    if (p->clock) {
        return; /* a test clock: no real wait */
    }
    span = pit_cycles / UINT64_C(1193182) * UINT64_C(1000000000) +
           pit_cycles % UINT64_C(1193182) * UINT64_C(1000000000) / UINT64_C(1193182);
    {
        uint64_t game_start = xeen_platform_clock_ns(p), game_end;
        start = now_ns(p);
        for (now = start; now - start < span; now = now_ns(p)) {
            uint64_t left = (span - (now - start) + UINT64_C(999999)) / UINT64_C(1000000);
            xeen_platform_wait(p, left > 10u ? 10u : (unsigned)left);
        }
        game_end = xeen_platform_clock_ns(p); /* a boss screen meanwhile is already excluded */
        if (clock_stands && game_end > game_start) {
            LOCK(p);
            p->paused_ns += game_end - game_start;
            UNLOCK(p);
        }
    }
}
/* The boss screen's state, hook, shown image, clocks. */
void xeen_platform_boss_info_get(xeen_platform *p, xeen_platform_boss_info *info)
{
    if (!p || !info) {
        return;
    }
    LOCK(p);
    info->active = p->boss;
    info->icon = p->boss;
    info->entries = p->boss_entries;
    info->exits = p->boss_exits;
    info->consumed = p->boss_consumed;
    snprintf(info->title, sizeof info->title, "%s", p->title_now);
    UNLOCK(p);
}
void xeen_platform_set_boss_hook(xeen_platform *p, xeen_platform_boss_hook hook, void *user)
{
    if (p) {
        LOCK(p);
        p->boss_hook = hook;
        p->boss_user = user;
        UNLOCK(p);
    }
}
void xeen_platform_set_menu_quit_query(xeen_platform *p, int (*menu_context)(void *), void *user)
{
    if (p) {
        LOCK(p);
        p->menu_query = menu_context;
        p->menu_user = user;
        UNLOCK(p);
    }
}
unsigned long xeen_platform_menu_quits(const xeen_platform *p)
{
    unsigned long n;
    if (!p) {
        return 0;
    }
    LOCK((xeen_platform *)(uintptr_t)p);
    n = p->menu_quits;
    UNLOCK((xeen_platform *)(uintptr_t)p);
    return n;
}
void xeen_platform_shown(xeen_platform *p, uint8_t pixels[64000], uint8_t palette[768])
{
    int x, y;
    if (!p || !pixels || !palette) {
        return;
    }
    LOCK(p);
    x = p->painted_x;
    y = p->painted_y;
    compose(p);
    memcpy(pixels, p->shown, 64000u);
    memcpy(palette, p->boss ? p->boss_palette : p->palette, 768u);
    p->painted_x = x;
    p->painted_y = y;
    UNLOCK(p);
}
void xeen_platform_startup_done(xeen_platform *p)
{
    xeen_mouse_sample pointer;
    if (!p) {
        return;
    }
    LOCK(p);
    pointer = p->queue.current_mouse; /* the pointer and the mouse's presence stay */
    xeen_input_queue_init(&p->queue);
    p->queue.current_mouse = pointer;
    p->wheel = 0;
    p->wheel_delta = 0;
    p->origin_ns = now_ns(p);
    p->paused_ns = 0;
    UNLOCK(p);
}
void xeen_platform_set_test_clock(xeen_platform *p, uint64_t (*now)(void *), void *user)
{
    if (p) {
        LOCK(p);
        p->clock = now;
        p->clock_user = user;
        p->origin_ns = now_ns(p);
        p->paused_ns = 0;
        UNLOCK(p);
    }
}
uint64_t xeen_platform_clock_ns(xeen_platform *p)
{
    uint64_t t;
    if (!p) {
        return 0;
    }
    LOCK(p);
    t = (p->boss ? p->boss_since_ns : now_ns(p)) - p->paused_ns;
    UNLOCK(p);
    return t;
}
