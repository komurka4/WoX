#ifndef XEEN_PLATFORM_H
#define XEEN_PLATFORM_H
#include "xeen/input_poll.h"
#include "xeen/timer.h"
/* Native services only. No callback dispatches gameplay or modifies its DS.
 * Headless mode has no autonomous time: advance() explicitly supplies ticks.
 * Window close (and XKEY_ESCAPE) requests host shutdown, without sending a
 * game command; the physical Escape key is the game's Esc.
 * Indexed pixels are 320x200 and palette entries are original 6-bit DAC RGB.
 * Keys pass through xeen_input_translate_key(). a window has the
 * mouse (pointer and presses through xeen_platform_mouse_event); headless
 * has none unless a test sends mouse events (which enables it). */
typedef struct xeen_platform xeen_platform;
xeen_platform *xeen_platform_open(const char *title, unsigned scale, int headless, char *error,
                                  size_t error_size);
void xeen_platform_close(xeen_platform *);
/* Phase 333 (owner request; a platform adaptation, not original behaviour): on Windows the process is made DPI aware
 * (per monitor, else system-wide), so Windows' display scaling (125 %, 200 %, ...) does not enlarge and blur the
 * window: its client area is 320x200 times [display] scale in the screen's own pixels. Called once at the start of a
 * window session, before any window or dialog exists; elsewhere it does nothing. */
void xeen_platform_physical_pixels(void);
int xeen_platform_present(xeen_platform *, const uint8_t *pixels, const uint8_t *palette);
void xeen_platform_pump(xeen_platform *);
int xeen_platform_should_close(const xeen_platform *);
xeen_input_provider xeen_platform_input(xeen_platform *);
xeen_status xeen_platform_bios(void *, xeen_timer_registers *);
xeen_status xeen_platform_advance(xeen_platform *, uint64_t ticks);
void xeen_platform_wait(xeen_platform *, unsigned milliseconds);
/* Phase 287: the game held for PIT_CYCLES of the 1193182 Hz clock (the PC speaker's blocking voice start). Headless:
 * no wait; the BIOS clock advances BIOS_TICKS. A window: waits that long (its events queued, as during any wait);
 * with CLOCK_STANDS the span is removed from the game's clock (the BIOS clock stands, as the boss screen's), else the
 * clock runs on (BIOS_TICKS is not used: the clock is the real one). A window under a test clock does not wait. */
void xeen_platform_hold(xeen_platform *, uint64_t pit_cycles, uint64_t bios_ticks, int clock_stands);
/* Tests/replays may queue logical keys only in explicitly headless mode. */
int xeen_platform_queue_key(xeen_platform *, xeen_native_key);
/* Interactive frontend. The window backends report every physical
 * key event here (headless tests may too): a press is queued for the game's
 * polls, an auto-repeat only while the queue is empty (no backlog), a
 * release is never a key. Returns 0 when the event was dropped. 
 * XKEY_CTRL in the key is Ctrl held at the press (both backends). */
int xeen_platform_key_event(xeen_platform *, xeen_native_key, int pressed, int repeat);
/* Ctrl+Q quits at every menu
 * screen. A fresh Ctrl+Q press with ANY other modifiers or keys held
 * (other_modifiers is ignored; kept for callers; never an auto-repeat) is
 * queued as its code 11h marked; the poll that reads it asks the
 * menu-context query (game thread, live state: the original's DS 8C77):
 * true -> the close request, no key; false -> the plain 11h. While the boss
 * screen shows, a fresh Ctrl+Q is taken by it and decided at the game
 * thread's next xeen_platform_pump: true -> the close request (not a
 * dismissal); false -> it hides the screen as any key, reaching no poll.
 * Ctrl+C is no shortcut: its plain 03h everywhere. xeen_platform_key_event
 * is this with other_modifiers 0. */
int xeen_platform_key_event_modified(xeen_platform *, xeen_native_key, int pressed, int repeat,
                                     int other_modifiers);
void xeen_platform_set_menu_quit_query(xeen_platform *, int (*menu_context)(void *), void *user);
unsigned long xeen_platform_menu_quits(const xeen_platform *);
/* The backends' physical key maps (X11 keysyms, Win32 virtual keys);
 * 0 for keys the game has no command for. Pure functions. */
int xeen_platform_x11_key(unsigned long keysym, xeen_native_key *);
int xeen_platform_win32_key(unsigned virtual_key, xeen_native_key *);
/* One key press as text (Phase 269): the X11 keysym of the key's first level (XLookupKeysym 0) or the Win32 virtual
 * key with, for Win32 OEM keys, UNSHIFTED the layout's plain character (MapVirtualKey); PRODUCED the character the
 * layout makes of the keystroke (XLookupString, ToUnicodeEx); SHIFT and CTRL held. Commands, letters, the keypad and
 * Ctrl chords map as the physical maps above (Ctrl's bit is the caller's); digits, space and punctuation by
 * xeen_input_text_key (the layout's character when the original's reader returns it, else the reader's own Shift).
 * Pure functions; 0 for no game key. */
int xeen_platform_x11_text(unsigned long keysym, unsigned produced, int shift, int ctrl, xeen_native_key *);
int xeen_platform_win32_text(unsigned virtual_key, unsigned unshifted, unsigned produced, int shift, int ctrl,
                             xeen_native_key *);
/* Display only: the 320x200 frame is shown at the largest integer scale
 * that fits the window, centred (letterboxed); with the correction its
 * rows are stretched to 320x240 (the 4:3 monitor's shape). */
void xeen_platform_set_aspect(xeen_platform *, int correct);
/* Phase 332 (wox.cfg [display] locked_scale, an owner-approved host option, not original): LOCKED keeps the picture at
 * the open's scale (with the aspect) in a larger window or in fullscreen, centred with borders, and gives the window
 * that size as its minimum; a smaller screen still shows the largest scale that fits. Headless: the geometry only. */
void xeen_platform_set_locked_scale(xeen_platform *, int locked);
/* Phase 335 (owner request, a host adaptation): the largest scale from 1 to SCALE whose window (320 x 200, or 240 with
 * ASPECT, times it) fits AVAIL_W x AVAIL_H client pixels; SCALE when either is 0 (unknown). Pure. A window opens at
 * the fitting scale (and xeen_platform_set_aspect fits it again): the configured scale is the most it uses. */
unsigned xeen_platform_fit_scale(unsigned scale, int aspect, unsigned avail_w, unsigned avail_h);
/* The window's scale in use (the configured one, or less when it did not fit the screen). */
unsigned xeen_platform_scale(const xeen_platform *);
/* Switch to fullscreen (or back) as Alt+Enter does; wox.cfg's
 * [display] fullscreen applies it once at startup. No effect headless. */
void xeen_platform_set_fullscreen(xeen_platform *, int on);
/* A window repaints its cursor at the pointer
 * on its own (Win32: the UI thread at each mouse move; X11: during the
 * game thread's waits) when the painted cursor is stale: shown over a
 * presented frame and not at the pointer (or hidden but still painted).
 * Headless never is. The count is developer evidence. */
int xeen_platform_cursor_stale(xeen_platform *);
unsigned long xeen_platform_cursor_repaints(xeen_platform *);
/* A frame the original shows for a moment inside one game step (a
 * pressed button between its 50BF6 flips, a combat scene before its 1C1B3
 * frame wait ends): presents it, returns once it is on the window (Windows:
 * painted by the UI-owner thread, at most 50 ms waited; X11 paints at once)
 * and not before UNTIL_NS (xeen_platform_now_ns; 0: no hold). Headless: 1,
 * nothing shown, no wait. */
int xeen_platform_present_hold(xeen_platform *, const uint8_t *pixels, const uint8_t *palette,
                               uint64_t until_ns);
uint64_t xeen_platform_now_ns(void);
/* Phase 272: a fatal error's modal message. Windows: one standard MessageBox (error icon, OK only) owned by the
 * window and shown by its UI-owner thread while the calling (game) thread waits for the OK, so the window keeps
 * repainting its last presented frame behind it (WM_PAINT from the retained frame; no clear, flip, resize or
 * fullscreen change); without a window (early errors, headless) an unowned one. Returns 1 after the OK, 0 when
 * no dialog was shown (another platform, or a second call: at most one per process). */
int xeen_platform_fatal_dialog(xeen_platform *, const char *title, const char *text);
/* Phase 335 (owner request): a Yes/No question before any window exists, with an optional third button
 * EXTRA_BUTTON (NULL: none) on the left of the button strip. Windows: a small modal window in the style of a message
 * box (src/platform/ask_win32.c); 1 for Yes, 0 for No (also closing it or Esc), 2 for the third button. -1 when no
 * dialog can be shown (another platform). */
int xeen_platform_ask(const char *title, const char *text, const char *extra_button);
/* Phase 335: starts program PATH (in its own folder) without waiting for it. 1 when started; 0 on failure or on a
 * platform without it (only Windows starts one). */
int xeen_platform_run(const char *path);
/* The logical 320x200 point under window pixel (wx, wy) of a
 * width x height window (the same letterboxed, optionally 4:3 image as the
 * presentation); 0 outside the image. Pure: both backends use it. */
int xeen_platform_map_pointer(unsigned width, unsigned height, int aspect, int wx, int wy,
                              unsigned *lx, unsigned *ly);
/* xeen_platform_map_pointer with a locked scale LOCKED_SCALE (0: not locked; xeen_platform_set_locked_scale). */
int xeen_platform_map_pointer_locked(unsigned width, unsigned height, int aspect, unsigned locked_scale, int wx,
                                     int wy, unsigned *lx, unsigned *ly);
/* One physical pointer event in window pixels. Button 0 is a
 * motion; 1/2 are the left/right buttons (the original driver treats both
 * presses alike: one queued click at the pointer). A release, a point
 * outside the image or a full press stack (19) is no event (returns 0 when
 * dropped). Headless tests may call it too. */
int xeen_platform_mouse_event(xeen_platform *, int wx, int wy, int button, int pressed);
/* Nonzero while the
 * left or right button is held (pending events processed first). */
int xeen_platform_button_held(xeen_platform *);
/*
 * The backends record wheel notches only after xeen_platform_set_wheel(p, 1)
 * (off: the wheel is no event, as before): a notch up or down at a window pixel
 * (the X11 buttons 4/5, Win32 WM_MOUSEWHEEL per 120), dropped outside the image,
 * while the boss screen shows or while off; its position is the logical point a
 * click there would have. xeen_platform_take_wheel takes one pending notch: +1
 * up, -1 down, 0 none, with its X, Y. The game never sees a notch: the add-on
 * decides what it means. */
void xeen_platform_set_wheel(xeen_platform *, int on);
int xeen_platform_take_wheel(xeen_platform *, uint16_t *x, uint16_t *y);
/* NOTCHES of the wheel at window pixel WX, WY as a backend's wheel event records them (the same rules: dropped while
 * off, while the boss screen shows or outside the image; returns 0 when dropped). Phase 330: the headless replay's
 * developer-only wheel input (panel tokens ^{ and ^}); a real window's notches come from its backend only. */
int xeen_platform_wheel_input(xeen_platform *, int wx, int wy, int notches);
/* The cursor size that bounds the INT 33h range (x 2..318 - width,
 * y 2..198 - height), from mouse.icn's frame. */
void xeen_platform_set_pointer_limits(xeen_platform *, unsigned cursor_width,
                                      unsigned cursor_height);
/* The pointer's current logical position (pending window events
 * processed first, as INT 33h fn 3 reports it); 0 without a mouse. */
int xeen_platform_pointer(xeen_platform *, uint16_t *x, uint16_t *y);
/* The original cursor image
 * (w x h <= 32 x 32, opacity per pixel) composited by the window at the
 * live pointer each time it paints (60 Hz while the pointer moves), like a
 * hardware cursor; NULL removes it. The original draws its cursor into the
 * page only at its presents (4E295 from 4EA85/4EF87): not during a scene's
 * 1C1B3 frame wait. */
void xeen_platform_set_cursor(xeen_platform *, const uint8_t *pixels, const uint8_t *opacity,
                              unsigned w, unsigned h);
/* Whether the composited cursor is shown (the game's mouse flag
 * CS:16BE: 4E231 turns it off, 4E06B on); shown after open. Display only. */
void xeen_platform_show_cursor(xeen_platform *, int shown);
/* Presents the frame if it changed since the last present, at most every
 * 16 ms (a newer change is shown by a later call). */
int xeen_platform_refresh(xeen_platform *, const uint8_t *pixels, const uint8_t *palette);
/* The window icons and the F10 "boss key".
 *
 * Icons: the owner's icon/wox_icon.bmp (which = 0) and a calculator icon
 * drawn by src/icon/make_icons.py (which = 1), as square ARGB images
 * (0xAARRGGBB, rows top-down), generated at build time; count 0 when the
 * build had no Python (no window icon then). */
typedef struct {
    unsigned size;
    const uint32_t *argb;
} xeen_icon_image;
const xeen_icon_image *xeen_platform_icon_images(int which, size_t *count);
/* The boss screen: F10 (XKEY_F10, X11 F10, VK_F10) turns the same window into
 * a static calculator (xeen_boss_render: its own 320x200 indexed image and
 * 6-bit palette; the game's frame and palette are kept untouched), titles it
 * "Calculator" and gives it the calculator icon. The next key press other
 * than a modifier (Shift, Ctrl, Alt, Super, the locks) or an Alt chord
 * (Alt+Tab, Alt+F4 stay the system's; Alt+Enter still toggles fullscreen)
 * restores the game frame, the exact prior title and the game icon; that key
 * and its auto-repeats until its release are consumed (no game key). Mouse
 * events while it shows are none. The original F10 (189E3's 2675C DOS
 * prompt, D2h) is replaced: F10 never reaches the game.
 * While it shows, a window's game thread is held inside the platform's next
 * call (pump, keyboard, mouse), so no poll, scene, RNG draw or audio request
 * happens, and the BIOS clock stops: the shown time is not caught up later.
 * Headless (tests): the state changes at once and nothing blocks. */
void xeen_boss_render(uint8_t pixels[64000], uint8_t palette[768]);
typedef struct {
    int active, icon; /* icon: 0 the game's, 1 the calculator's */
    unsigned long entries, exits, consumed;
    char title[128];
} xeen_platform_boss_info;
void xeen_platform_boss_info_get(xeen_platform *, xeen_platform_boss_info *);
/* Called at every change (active 1 shown, 0 gone), on the thread that saw
 * the key (Windows: the UI-owner thread): the host pauses its audio there. */
typedef void (*xeen_platform_boss_hook)(void *, int active);
void xeen_platform_set_boss_hook(xeen_platform *, xeen_platform_boss_hook, void *);
/* What the window shows now (the boss screen, or the game frame with the
 * host cursor); headless keeps the last presented frame for this. */
void xeen_platform_shown(xeen_platform *, uint8_t pixels[64000], uint8_t palette[768]);
/* Phase 290 (owner-approved host start-up): ends a wait the host made before the game's start (the SC-55 boot's
 * loading message): the keys, presses and wheel notches queued meanwhile are discarded (they never reach a poll; the
 * pointer and the buttons held stay) and the game clock starts now, as it did at open (the BIOS clock was not read). */
void xeen_platform_startup_done(xeen_platform *);
/* Test control: a replacement monotonic clock (ns) for the BIOS clock and
 * the boss screen's pause accounting; xeen_platform_clock_ns is the game
 * clock: real time minus the time the boss screen showed. */
void xeen_platform_set_test_clock(xeen_platform *, uint64_t (*now)(void *), void *);
uint64_t xeen_platform_clock_ns(xeen_platform *);
/* The X11 _NET_WM_ICON payload (width, height, ARGB per image, as longs);
 * OUT NULL returns the item count; 0 when CAP is too small. Pure. */
size_t xeen_platform_x11_icon_payload(int which, unsigned long *out, size_t cap);
#endif
