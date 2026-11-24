/*
 * Phase 269: the keyboard's printable characters on both window backends (headless; the backends' pure maps).
 *
 * The original reads the keyboard itself: its INT 9 handler 4FEFF queues the character of each scan code from its
 * table CS:7AF8, and its reader 4FE2A applies Shift when the key is read (Ctrl first: the character - 40h), by the
 * shifted character of the highest scan code whose character it is (CS:7AC8 / CS:7AE0). So on a US keyboard every
 * printable ASCII character but '_' and the lower-case letters reaches the game; Shift+'-' stays '-' (keypad 4Ah is
 * found first); keypad 5 is '5' and Shift makes it '%'. The Win32 backend mapped only letters, digits, space and '='
 * (the owner's report: no shifted symbols, no '-', no brackets); X11 ignored Shift.
 *
 * Checked: the reader's Shift rule and character set (with negative controls), a US layout's keys on Win32 and X11
 * (each one game code, as the original), two other layouts' characters, Ctrl, the keypad, the letters, and one key
 * per press through the platform's queue (no duplicate text, auto-repeat as before).
 */
#include "xeen/platform.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)

/* Win32 virtual keys (winuser.h) */
enum {
    VK_OEM_1 = 0xba, VK_OEM_PLUS = 0xbb, VK_OEM_COMMA = 0xbc, VK_OEM_MINUS = 0xbd, VK_OEM_PERIOD = 0xbe,
    VK_OEM_2 = 0xbf, VK_OEM_3 = 0xc0, VK_OEM_4 = 0xdb, VK_OEM_5 = 0xdc, VK_OEM_6 = 0xdd, VK_OEM_7 = 0xde,
    VK_OEM_102 = 0xe2, VK_NUMPAD0 = 0x60, VK_NUMPAD5 = 0x65, VK_NUMPAD8 = 0x68, VK_DECIMAL = 0x6e,
    VK_MULTIPLY = 0x6a, VK_ADD = 0x6b, VK_SUBTRACT = 0x6d, VK_DIVIDE = 0x6f, VK_CLEAR = 0x0c
};

/* The game code of a mapped key (0: none) */
static unsigned code_of(int mapped, xeen_native_key k)
{
    return mapped ? xeen_input_translate_key(k, 0) : 0u;
}
static unsigned w32(unsigned vk, unsigned unshifted, unsigned produced, int shift, int ctrl)
{
    xeen_native_key k;
    int ok = xeen_platform_win32_text(vk, unshifted, produced, shift, ctrl, &k);
    if (ok && ctrl) {
        k = (xeen_native_key)(k | XKEY_CTRL);
    }
    return code_of(ok, k);
}
static unsigned x11(unsigned long sym, unsigned produced, int shift, int ctrl)
{
    xeen_native_key k;
    int ok = xeen_platform_x11_text(sym, produced, shift, ctrl, &k);
    if (ok && ctrl) {
        k = (xeen_native_key)(k | XKEY_CTRL);
    }
    return code_of(ok, k);
}

int main(void)
{
    /* A US keyboard: each key's plain and shifted characters as the layout makes them, and what the original
     * returns (4FE2A over CS:7AF8/7AC8/7AE0) for the plain and the Shift press. */
    static const struct {
        unsigned vk;
        char plain, layout_shift, original_shift;
    } us[] = {{'1', '1', '!', '!'},           {'2', '2', '@', '@'},           {'3', '3', '#', '#'},
              {'4', '4', '$', '$'},           {'5', '5', '%', '%'},           {'6', '6', '^', '^'},
              {'7', '7', '&', '&'},           {'8', '8', '*', '*'},           {'9', '9', '(', '('},
              {'0', '0', ')', ')'},           {VK_OEM_MINUS, '-', '_', '-'},  {VK_OEM_PLUS, '=', '+', '+'},
              {VK_OEM_4, '[', '{', '{'},      {VK_OEM_6, ']', '}', '}'},      {VK_OEM_5, '\\', '|', '|'},
              {VK_OEM_1, ';', ':', ':'},      {VK_OEM_7, '\'', '"', '"'},     {VK_OEM_3, '`', '~', '~'},
              {VK_OEM_COMMA, ',', '<', '<'},  {VK_OEM_PERIOD, '.', '>', '>'}, {VK_OEM_2, '/', '?', '?'},
              {' ', ' ', ' ', ' '}};
    unsigned i, c, n;
    xeen_native_key k;

    /* 1. The reader's Shift rule (4FE2A) */
    for (i = 0; i < sizeof us / sizeof us[0]; ++i) {
        CHECK(xeen_input_shift_character((unsigned char)us[i].plain) == (unsigned char)us[i].original_shift);
    }
    CHECK(xeen_input_shift_character('-') == '-'); /* the original's quirk: keypad 4Ah wins the reverse search */
    CHECK(xeen_input_shift_character('*') == '*' && xeen_input_shift_character('+') == '+');
    CHECK(xeen_input_shift_character('Q') == 'Q' && xeen_input_shift_character(0xf2) == 0xf2);
    /* Its character set: printable ASCII but '_' and the lower case (the negative controls) */
    for (c = 0x20, n = 0; c <= 0x7e; ++c) {
        int expect = c != '_' && !(c >= 'a' && c <= 'z');
        CHECK(xeen_input_reader_character(c) == expect);
        n += (unsigned)expect;
    }
    CHECK(n == 68u && !xeen_input_reader_character(0x7f) && !xeen_input_reader_character(0x0d));

    /* 2. Win32, US layout: one game code per keystroke, the original's */
    for (i = 0; i < sizeof us / sizeof us[0]; ++i) {
        unsigned vk = us[i].vk, plain = (unsigned char)us[i].plain;
        CHECK(w32(vk, plain, plain, 0, 0) == plain);
        CHECK(w32(vk, plain, (unsigned char)us[i].layout_shift, 1, 0) == (unsigned char)us[i].original_shift);
    }
    /* the negative control: the Phase 268 map, Shift ignored and no OEM key but '=' */
    CHECK(xeen_platform_win32_key('1', &k) && code_of(1, k) == '1' && !xeen_platform_win32_key(VK_OEM_MINUS, &k) &&
          !xeen_platform_win32_key(VK_OEM_4, &k));
    /* Letters: the command, Shift or not; Ctrl: the character - 40h (Ctrl before Shift, 4FE2A) */
    CHECK(w32('U', 'U', 'U', 0, 0) == 'U' && w32('D', 'D', 'D', 1, 0) == 'D' && w32('Q', 'Q', 0, 0, 1) == 0x11);
    CHECK(w32(VK_OEM_4, '[', 0, 1, 1) == 0x1b && w32(VK_OEM_5, '\\', 0, 0, 1) == 0x1c && w32('1', '1', 0, 1, 1) == '1');
    /* The keypad as the original's scan table, whatever Num Lock says; keypad Del none */
    CHECK(w32(VK_NUMPAD8, '8', '8', 0, 0) == 0xf2 && w32(VK_NUMPAD0, '0', '0', 0, 0) == 0xe0);
    CHECK(w32(VK_NUMPAD5, '5', '5', 0, 0) == '5' && w32(VK_NUMPAD5, '5', 0, 1, 0) == '%' && w32(VK_CLEAR, 0, 0, 0, 0) == '5');
    CHECK(w32(VK_MULTIPLY, '*', '*', 0, 0) == '*' && w32(VK_ADD, '+', '+', 1, 0) == '+' &&
          w32(VK_SUBTRACT, '-', '-', 0, 0) == '-' && w32(VK_DIVIDE, '/', '/', 0, 0) == '/' && !w32(VK_DECIMAL, '.', '.', 0, 0));
    /* The commands keep their keys (Enter, Esc, arrows, F8) */
    CHECK(w32(0x0d, '\r', 0, 0, 0) == 0x0d && w32(0x1b, 0x1b, 0, 1, 0) == 0x1b && w32(0x26, 0, 0, 1, 0) == 0xf2 &&
          w32(0x77, 0, 0, 0, 0) == 0xd0);

    /* 3. Other layouts: the layout's character when the original has it, else the original's Shift, else none */
    CHECK(w32('7', '7', '/', 1, 0) == '/' && w32(VK_OEM_PLUS, '+', '*', 1, 0) == '*');  /* German: Shift+7, '+' */
    CHECK(!w32(VK_OEM_4, 0xdf, 0xdf, 0, 0) && w32(VK_OEM_4, 0xdf, '?', 1, 0) == '?');  /* German 'ß' none, '?' */
    CHECK(w32(VK_OEM_102, '<', '<', 0, 0) == '<' && w32(VK_OEM_102, '<', '>', 1, 0) == '>');
    CHECK(!w32(VK_OEM_7, 0x80000000u, 0, 0, 0) && w32(VK_OEM_1, 0, 0, 1, 0) == 0); /* a dead key: none */
    CHECK(w32('2', '2', 0xe9, 0, 0) == '2' && w32('2', '2', '2', 1, 0) == '2');      /* AZERTY: the digit */

    /* 4. X11 (the keysym of the first level; ASCII keysyms are their characters) */
    for (i = 0; i < sizeof us / sizeof us[0]; ++i) {
        unsigned plain = (unsigned char)us[i].plain;
        CHECK(x11(plain, plain, 0, 0) == plain);
        CHECK(x11(plain, (unsigned char)us[i].layout_shift, 1, 0) == (unsigned char)us[i].original_shift);
    }
    CHECK(x11('u', 'U', 1, 0) == 'U' && x11('d', 0, 0, 0) == 'D' && x11('[', 0, 1, 1) == 0x1b);
    CHECK(x11(0xff9d, 0, 0, 0) == '5' && x11(0xff9d, 0, 1, 0) == '%' && x11(0xffaa, 0, 0, 0) == '*' &&
          x11(0xffad, 0, 1, 0) == '-' && x11(0xff97, 0, 1, 0) == 0xf2 && x11(0xff52, 0, 0, 0) == 0xf2);
    CHECK(x11(0xdf, '?', 1, 0) == '?' && !x11(0xdf, 0, 0, 0) && !x11(0xfe52, 0, 0, 0)); /* ß, dead circumflex */

    /* 5. Through the platform's queue: one code per press, no text event besides; a held key's repeats only while
     *    the queue is empty (as before) */
    {
        char error[160];
        xeen_platform *p = xeen_platform_open("headless test", 2, 1, error, sizeof error);
        xeen_input_provider input;
        uint16_t key;
        static const char typed[] = "!@#$%^&*()-=[]\\;'`,./<>?{}|:\"~+";
        CHECK(p && !error[0]);
        input = xeen_platform_input(p);
        for (i = 0; typed[i]; ++i) {
            CHECK(xeen_input_character_key((unsigned char)typed[i], &k));
            CHECK(xeen_platform_key_event(p, k, 1, 0) && xeen_platform_key_event(p, k, 0, 0));
            CHECK(input.keyboard(input.user, &key) && key == (unsigned char)typed[i]);
            CHECK(input.keyboard(input.user, &key) && key == 0); /* exactly one */
        }
        CHECK(xeen_input_character_key('-', &k) && xeen_platform_key_event(p, k, 1, 0) &&
              !xeen_platform_key_event(p, k, 1, 1)); /* a repeat behind a queued key is dropped */
        CHECK(input.keyboard(input.user, &key) && key == '-' && input.keyboard(input.user, &key) && key == 0);
        CHECK(!xeen_input_text_key('_', '_', 0, &k) && !xeen_input_text_key('a', 'a', 0, &k)); /* not the reader's */
        xeen_platform_close(p);
    }
    puts("phase269 keyboard: OK");
    return 0;
}
