/* SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * Phase 335 (owner report: F10 during the intro on Linux freezes the game or hangs it): platform.c's X11 waits while
 * the boss screen shows, under the Phase 334 mock Xlib (tests/test_phase334_x11_paint.c, #included with its event
 * queue). The movie player (src/app/host_movie.c host_wait_until) waits on the platform clock, which stands still
 * while the calculator shows, calling only xeen_platform_wait; so the key that hides the calculator must be read
 * there. Checked: F10 read in a wait shows the calculator; a key read in a wait hides it (and reaches no game poll);
 * a movie-style wait on the stopped clock ends once a key arrives; a Ctrl+Q outside the menu hides it in a wait too.
 * No X server or real window (AGENTS.md section 5).
 */
#define X11_MOCK_EVENTS 1
#define PHASE334_MAIN phase334_main
#include "test_phase334_x11_paint.c"

static int failures;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}
static void key(unsigned long sym, int pressed, unsigned state)
{
    XEvent e;
    static Time t = 1000;
    memset(&e, 0, sizeof e);
    e.xkey.type = pressed ? KeyPress : KeyRelease;
    e.xkey.keycode = (unsigned)sym;
    e.xkey.state = state;
    e.xkey.time = t += 10;
    mock_push(&e);
}
static void stroke(unsigned long sym, unsigned state)
{
    key(sym, 1, state);
    key(sym, 0, state);
}
/** xeen_platform_wait(1 ms) until DONE(p) or N calls; the calls made. */
static int wait_for(xeen_platform *p, int (*done)(xeen_platform *), int n)
{
    int i;
    for (i = 0; i < n && !done(p); ++i) {
        xeen_platform_wait(p, 1);
    }
    return i;
}
static int boss_on(xeen_platform *p) { return p->boss; }
static int boss_off(xeen_platform *p) { return !p->boss; }
static int not_menu(void *u) { (void)u; return 0; }

int main(void)
{
    char error[160];
    xeen_platform *p;
    uint16_t code = 0;
    xeen_input_provider provider;
    transcript = fopen("/dev/null", "w");
    if (!transcript) {
        return 2;
    }
    p = xeen_platform_open("WoX", 2, 0, error, sizeof error);
    if (!p) {
        printf("FAIL open: %s\n", error);
        return 1;
    }
    memset(p->pixels, 7, sizeof p->pixels);
    memset(p->palette, 40, sizeof p->palette);
    p->have_frame = 1;
    xeen_platform_set_menu_quit_query(p, not_menu, NULL);

    stroke(0xffc7ul /* F10 */, 0);
    wait_for(p, boss_on, 5);
    check(p->boss == 1, "F10 read during a wait shows the calculator");
    {
        /* the movie's clock stands while it shows */
        uint64_t a = xeen_platform_clock_ns(p), b;
        struct timespec d = {0, 3000000L};
        nanosleep(&d, NULL);
        b = xeen_platform_clock_ns(p);
        check(a == b, "the platform clock stands while the calculator shows");
    }
    stroke('a', 0);
    check(wait_for(p, boss_off, 20) < 20 && !p->boss, "a key read during a wait hides the calculator");
    provider = xeen_platform_input(p);
    check(!provider.keyboard(provider.user, &code) || code == 0, "the hiding key reaches no game poll");

    /* host_wait_until (src/app/host_movie.c): the calculator shows first, then a wait for a clock target 2 ms
     * ahead on the stopped clock, so only the hiding key (queued after 5 calls) can end it; bounded, so that the old
     * behaviour (no events read while the calculator shows) fails rather than hangs. Independent of the machine's
     * load: nothing races the 2 ms. */
    {
        uint64_t target;
        int calls = 0, shown_after = 0;
        stroke(0xffc7ul, 0);
        wait_for(p, boss_on, 5);
        target = xeen_platform_clock_ns(p) + 2000000u;
        while (xeen_platform_clock_ns(p) < target && calls < 3000) {
            if (calls == 5) {
                shown_after = p->boss; /* still showing after 5 waits: the clock stood */
                stroke(' ', 0);
            }
            xeen_platform_wait(p, 1);
            ++calls;
        }
        check(shown_after && !p->boss && calls < 3000, "a movie wait on the stopped clock ends after the hiding key");
    }

    /* Ctrl+Q while it shows, outside the menu: it hides (boss_menu_quit_resolve), also from a wait */
    stroke(0xffc7ul, 0);
    wait_for(p, boss_on, 5);
    stroke('q', ControlMask);
    check(wait_for(p, boss_off, 20) < 20 && !p->boss && !p->closing,
          "Ctrl+Q outside the menu hides the calculator during a wait, no close");

    xeen_platform_close(p);

    /* the window scale fits the screen: 1920x1080 less the frame allowance (80) is 1000 lines: 6 -> 5, with the
     * CRT aspect 4; a large screen keeps 6 */
    fake_screen_w = 1920;
    fake_screen_h = 1080;
    p = xeen_platform_open("WoX", 6, 0, error, sizeof error);
    check(p && xeen_platform_scale(p) == 5u && p->width == 1600u && p->height == 1000u,
          "scale 6 opens at 5 (1600x1000) on a 1920x1080 screen");
    if (p) {
        xeen_platform_set_aspect(p, 1);
        check(xeen_platform_scale(p) == 4u && p->width == 1280u && p->height == 960u, "with the CRT aspect: 4 (1280x960)");
        xeen_platform_set_aspect(p, 0);
        check(xeen_platform_scale(p) == 5u, "without it again: 5");
        xeen_platform_close(p);
    }
    fake_screen_w = 3840;
    fake_screen_h = 2160;
    p = xeen_platform_open("WoX", 6, 0, error, sizeof error);
    check(p && xeen_platform_scale(p) == 6u && p->width == 1920u, "a 3840x2160 screen keeps scale 6");
    xeen_platform_close(p);
    fclose(transcript);
    printf("phase335 x11 boss wait: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    (void)phase334_main;
    return failures != 0;
}
