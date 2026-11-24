/* Phase 338: the setup tool's main window title (src/setup/setup_title.h) from SETUP_VERSION of versions.cmake.
 * Usage: xeen_phase338_setup_title_tests SETUP_VERSION (CMake passes the value it read from versions.cmake).
 * Checks both forms, GTK's narrow SETUP_MAIN_TITLE and Win32's wide SETUP_MAIN_TITLE_W, against
 * "WoX - Setup (SETUP_VERSION)" and the generated XEEN_SETUP_VERSION against the argument. Headless. */
#include "setup/setup_title.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures;
#define CHECK(c)                                                                                                     \
    do {                                                                                                             \
        if (!(c)) {                                                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                                    \
            ++failures;                                                                                              \
        }                                                                                                            \
    } while (0)

int main(int argc, char **argv)
{
    char expected[128];
    wchar_t wide[128];
    size_t n, i;
    if (argc != 2) {
        fprintf(stderr, "usage: %s SETUP_VERSION\n", argv[0]);
        return 2;
    }
    n = (size_t)snprintf(expected, sizeof expected, "WoX - Setup (%s)", argv[1]);
    CHECK(n < sizeof expected);
    for (i = 0; i <= n && i < sizeof wide / sizeof *wide; ++i) {
        wide[i] = (wchar_t)(unsigned char)expected[i]; /* the title is ASCII */
    }
    CHECK(!strcmp(XEEN_SETUP_VERSION, argv[1]));
    CHECK(!strcmp(SETUP_MAIN_TITLE, expected));
    CHECK(!wcscmp(SETUP_MAIN_TITLE_W, wide));
    CHECK(sizeof SETUP_MAIN_TITLE == n + 1);
    CHECK(sizeof SETUP_MAIN_TITLE_W == (n + 1) * sizeof(wchar_t));
    printf("%s: \"%s\"\n", failures ? "FAIL" : "ok", SETUP_MAIN_TITLE);
    return failures ? 1 : 0;
}
