/* Phase 331: the setup tool's Win32 window (Windows). Shows and collects the values of setup_model.c; every rule is
 * there. Owner-approved host tool, not original behaviour. Paths are kept in the ANSI code page, as wox opens them
 * (fopen); a folder whose name that code page cannot hold is refused with a message. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include "setup_model.h"
#include "setup_title.h" /* SETUP_MAIN_TITLE_W: the main window title with SETUP_VERSION (Phase 338) */
#include "xeen/wox_config.h"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ID_EDIT = 100,   /* + setup_dir */
    ID_BROWSE = 200, /* + setup_dir */
    ID_SPEECH = 300,
    ID_MUSIC,
    ID_CREATE_CFG,
    ID_ROM_GET,
    ID_FULLSCREEN = 310,
    ID_SCALE,
    ID_ASPECT,
    ID_LOCKED,
    ID_ADDONS = 320,
    ID_BUGFIXES,
    ID_SAVE = 330,
    ID_RERUN,
    ID_CHOICE_ONE = 340,
    ID_CHOICE_TWO,
    ID_FLAG = 400,  /* + flag index */
    ID_STYLE = 460, /* + style index */
};
/* The download thread's messages to the settings window (Phase 335). */
enum { WM_DOWNLOAD_PROGRESS = WM_APP + 1, WM_DOWNLOAD_DONE };

typedef struct {
    char exe_dir[SETUP_PATH_MAX], wox_path[SETUP_PATH_MAX + 16];
    setup_doc doc;
    setup_settings s;
    setup_xeen_cfg cfg;
    int cfg_ok;
    setup_cfg_state cfg_state; /* the last read_cfg's state (Create default... for ABSENT and INVALID) */
    char cfg_dir_read[SETUP_PATH_MAX], data_before[SETUP_PATH_MAX];
    HWND window, edit[SETUP_DIRS], speech, music, cfg_note, cfg_create, rom_note, rom_get, aspect_note;
    HWND fullscreen, scale, aspect, locked;
    HFONT font, bold, big;
    int dpi;
    int done;        /* the main loop ends */
    int downloading; /* a ROM download runs (the ROM note shows its progress) */
    int rerun;       /* the window closed after a new first run (Initial setup...): it is built again */
    setup_rom_set download_set;
    /* the window's state when it was built (Phase 335: Cancel asks only after a change) */
    setup_settings initial;
    LRESULT initial_speech, initial_music;
} app_state;

static app_state app;
static const wchar_t main_class[] = L"WoxSetupMain", modal_class[] = L"WoxSetupModal";
static const wchar_t *const dir_labels[SETUP_DIRS] = {L"XEEN.CFG location",   L"Data location",
                                                      L"Save location",       L"MT-32 roms location",
                                                      L"SC-55 roms location", L"CD tracks location"};

/* ------------------------------------------------------------------ text and layout helpers */
/** Pixels for N 96-dpi units. */
static int px(int n)
{
    return MulDiv(n, app.dpi, 96);
}
/** ANSI-code-page text S as UTF-16 into OUT (N characters). */
static void to_wide(const char *s, wchar_t *out, int n)
{
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, out, n)) {
        out[0] = 0;
    }
}
/** UTF-16 W in the ANSI code page into OUT; 0 when a character cannot be represented (or it does not fit). */
static int to_ansi(const wchar_t *w, char *out, int n)
{
    BOOL lossy = FALSE;
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, out, n, NULL, &lossy)) {
        out[0] = 0;
        return 0;
    }
    return !lossy;
}
static void message(HWND owner, const char *text, const wchar_t *title, UINT flags, int *answer)
{
    wchar_t w[2400];
    int r;
    to_wide(text, w, 2400);
    r = MessageBoxW(owner, w, title, flags);
    if (answer) {
        *answer = r;
    }
}
static int ask(HWND owner, const char *text, const wchar_t *title, UINT icon)
{
    int r;
    message(owner, text, title, MB_YESNO | icon, &r);
    return r == IDYES;
}
static void tell(HWND owner, const char *text, const wchar_t *title, UINT icon)
{
    message(owner, text, title, MB_OK | icon, NULL);
}
/** A child control of PARENT at 96-dpi units X, Y, W, H with the window font. */
static HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
                    int id, HFONT font)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)(font ? font : app.font), TRUE);
    return c;
}
/** A group box of PARENT at 96-dpi units X, Y, W, H, titled TEXT in bold: below its siblings (WS_CLIPSIBLINGS at the
 * bottom of the Z order), so it never paints over the controls inside it. */
static HWND group_box(HWND parent, const wchar_t *text, int x, int y, int w, int h)
{
    HWND c = control(parent, L"BUTTON", text, BS_GROUPBOX | WS_CLIPSIBLINGS, x, y, w, h, -1, app.bold);
    SetWindowPos(c, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return c;
}
/** Shows or hides note C, and repaints the window under it (a hidden note leaves no text behind). */
static void show_note(HWND c, int visible)
{
    RECT r;
    HWND parent = GetParent(c);
    ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    GetWindowRect(c, &r);
    MapWindowPoints(NULL, parent, (POINT *)&r, 2);
    RedrawWindow(parent, &r, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
static void set_text(HWND c, const char *s)
{
    wchar_t w[SETUP_PATH_MAX];
    to_wide(s, w, SETUP_PATH_MAX);
    SetWindowTextW(c, w);
}
/** The text of edit C in the ANSI code page; 0 when it cannot be held there. */
static int get_text(HWND c, char *out, size_t n)
{
    wchar_t w[SETUP_PATH_MAX];
    GetWindowTextW(c, w, SETUP_PATH_MAX);
    return to_ansi(w, out, (int)n);
}
static void make_fonts(void)
{
    NONCLIENTMETRICSW m;
    HDC dc = GetDC(NULL);
    app.dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    m.cbSize = sizeof m;
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) {
        app.font = app.bold = app.big = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        return;
    }
    app.font = CreateFontIndirectW(&m.lfMessageFont);
    m.lfMessageFont.lfWeight = FW_BOLD;
    app.bold = CreateFontIndirectW(&m.lfMessageFont);
    m.lfMessageFont.lfHeight = m.lfMessageFont.lfHeight * 4 / 3;
    app.big = CreateFontIndirectW(&m.lfMessageFont);
}
/** Runs a modal loop for top-level WINDOW (its OWNER disabled meanwhile) until *DONE becomes nonzero. */
static void modal_loop(HWND window, HWND owner, const int *done)
{
    MSG msg;
    if (owner) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window); /* to the front also when started by wox's Run setup (Phase 335) */
    while (!*done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    DestroyWindow(window);
}
/** A top-level window of client size W x H (96-dpi units) centred on the screen, of class CLS. Not WS_CLIPCHILDREN:
 * a group box does not paint its inside, so the window's own background (COLOR_BTNFACE) must show through it, also
 * where a note was hidden (Phase 333). */
static HWND top_window(const wchar_t *cls, const wchar_t *title, int w, int h, HWND owner, void *data)
{
    RECT r = {0, 0, px(w), px(h)};
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
    return CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls, title, style,
                           (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 3, r.right - r.left,
                           r.bottom - r.top, owner, NULL, GetModuleHandleW(NULL), data);
}

/* ------------------------------------------------------------------ modal windows: the choices and the flag lists */
typedef struct {
    int done, result;
    int flag_list; /* 0: a first-run choice; 1: the Addons or Bugfixes list (its OK keeps the values) */
    /* the first-run choice */
    const wchar_t *title[2], *note[2];
    HFONT note_font[2];
    /* the flag lists */
    int addons;
    HWND check[32], style[SETUP_WIZARD_STYLES];
} modal_state;

/** Paints a choice button: a themed push button with its title in large bold and its note below. */
static void draw_choice(const DRAWITEMSTRUCT *d, const modal_state *m)
{
    int i = d->CtlID == ID_CHOICE_TWO;
    RECT r = d->rcItem, t;
    HTHEME theme = OpenThemeData(d->hwndItem, L"BUTTON");
    int state = d->itemState & ODS_SELECTED ? PBS_PRESSED : d->itemState & ODS_FOCUS ? PBS_DEFAULTED : PBS_NORMAL;
    if (theme) {
        DrawThemeBackground(theme, d->hDC, BP_PUSHBUTTON, state, &r, NULL);
        CloseThemeData(theme);
    } else {
        DrawFrameControl(d->hDC, &r, DFC_BUTTON, DFCS_BUTTONPUSH | (d->itemState & ODS_SELECTED ? DFCS_PUSHED : 0));
    }
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, GetSysColor(COLOR_BTNTEXT));
    t = r;
    t.top += px(i == 0 && m->note[0] ? 26 : 20);
    t.left += px(12);
    t.right -= px(12);
    SelectObject(d->hDC, app.big);
    DrawTextW(d->hDC, m->title[i], -1, &t, DT_CENTER | DT_SINGLELINE);
    if (m->note[i]) {
        t.top += px(30);
        SelectObject(d->hDC, m->note_font[i]);
        DrawTextW(d->hDC, m->note[i], -1, &t, DT_CENTER | DT_WORDBREAK);
    }
    if (d->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -px(4), -px(4));
        DrawFocusRect(d->hDC, &f);
    }
}
static void flags_ok(modal_state *m)
{
    int *values = m->addons ? app.s.addon : app.s.bugfix;
    unsigned count = m->addons ? setup_addon_count : setup_bugfix_count;
    for (unsigned i = 0; i < count; ++i) {
        values[i] = SendMessageW(m->check[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    for (unsigned k = 0; m->addons && k < SETUP_WIZARD_STYLES; ++k) {
        LRESULT v = SendMessageW(m->style[k], CB_GETCURSEL, 0, 0);
        app.s.wizard_style[k] = v < 0 ? SETUP_STYLE_BLACK : (unsigned)v;
    }
}
/** Enables the flags that sit under a parent (damage_numbers.stack) and the Wizard Eye+ styles as their parents. */
static void flags_follow(modal_state *m)
{
    if (!m->addons) {
        return;
    }
    for (unsigned i = 0; i < setup_addon_count; ++i) {
        if (setup_addons[i].parent >= 0) {
            EnableWindow(m->check[i],
                         SendMessageW(m->check[setup_addons[i].parent], BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
    }
    for (unsigned k = 0; k < SETUP_WIZARD_STYLES; ++k) {
        EnableWindow(m->style[k],
                     SendMessageW(m->check[setup_addon_index("wizard_eye_plus")], BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
}
static LRESULT CALLBACK modal_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    modal_state *m = (modal_state *)GetWindowLongPtrW(w, GWLP_USERDATA);
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams);
            break;
        case WM_DRAWITEM:
            draw_choice((const DRAWITEMSTRUCT *)lp, m);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            if (GetWindowLongPtrW((HWND)lp, GWLP_USERDATA) == 1) {
                SetTextColor((HDC)wp, GetSysColor(COLOR_GRAYTEXT));
            }
            SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_CHOICE_ONE: m->result = 1; m->done = 1; return 0;
                case ID_CHOICE_TWO: m->result = 2; m->done = 1; return 0;
                case IDOK:
                    if (m->flag_list) {
                        flags_ok(m);
                        m->result = 1;
                        m->done = 1;
                    }
                    return 0;
                case IDCANCEL: m->done = 1; return 0;
                default:
                    if (LOWORD(wp) >= ID_FLAG && LOWORD(wp) < ID_FLAG + 32) {
                        flags_follow(m);
                    }
                    return 0;
            }
        case WM_CLOSE:
            m->done = 1;
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
/** A gray static text (the modal procedure colours statics whose user data is 1). */
static HWND gray_text(HWND parent, const wchar_t *text, int x, int y, int w, int h)
{
    HWND c = control(parent, L"STATIC", text, SS_LEFT, x, y, w, h, -1, NULL);
    SetWindowLongPtrW(c, GWLP_USERDATA, 1);
    return c;
}
/** One first-run screen. Returns 1, 2 or 0 (closed). */
static int choose(const wchar_t *intro, const wchar_t *heading, modal_state *m, const wchar_t *footer)
{
    HWND w = top_window(modal_class, L"WoX - Setup", 540, 330, NULL, m);
    int y = 18;
    if (intro) {
        control(w, L"STATIC", intro, SS_LEFT, 24, y, 490, 20, -1, NULL);
        y += 24;
    }
    control(w, L"STATIC", heading, SS_LEFT, 24, y, 490, 20, -1, app.bold);
    y += 34;
    control(w, L"BUTTON", m->title[0], BS_OWNERDRAW | WS_TABSTOP, 24, y, 236, 160, ID_CHOICE_ONE, NULL);
    control(w, L"BUTTON", m->title[1], BS_OWNERDRAW | WS_TABSTOP, 280, y, 236, 160, ID_CHOICE_TWO, NULL);
    if (footer) {
        control(w, L"STATIC", footer, SS_LEFT, 24, y + 176, 490, 20, -1, NULL);
    }
    modal_loop(w, NULL, &m->done);
    return m->result;
}
/** The first run: the two choices, then wox.cfg (and the GOG folder) written; REPLACE: an existing wox.cfg is
 * replaced (its welcome says so). 1 done, 0 closed or failed (then nothing was written). */
static int first_run(int replace)
{
    modal_state one, two;
    char err[1600];
    HFONT italic;
    LOGFONTW lf;
    memset(&one, 0, sizeof one);
    memset(&two, 0, sizeof two);
    one.title[0] = L"GOG version";
    one.title[1] = L"Other version";
    one.note[0] = setup_has_gog_image(app.exe_dir) ? L"Recommended" : NULL;
    one.note_font[0] = app.bold;
    one.note[1] = SETUP_WIDE(SETUP_TEXT_OTHER_NOTE);
    one.note_font[1] = app.font;
    if (!choose(replace ? SETUP_WIDE(SETUP_TEXT_WELCOME_REPLACE) : SETUP_WIDE(SETUP_TEXT_WELCOME_NEW),
                L"Which version of the game do you have?", &one, NULL)) {
        return 0;
    }
    GetObjectW(app.font, sizeof lf, &lf);
    lf.lfItalic = TRUE;
    italic = CreateFontIndirectW(&lf);
    two.title[0] = L"Vanilla version";
    two.title[1] = L"Enhanced version";
    two.note[0] = L"For those who want to play like it was 1993, prepared pen and piece of paper and love to spend "
                  L"hours dealing with equipment";
    two.note_font[0] = italic;
    two.note[1] = SETUP_WIDE(SETUP_TEXT_ENHANCED_NOTE);
    two.note_font[1] = italic;
    if (!choose(NULL, L"How would you like to play?", &two, L"You can customize any addons and bugfixes at any time")) {
        DeleteObject(italic);
        return 0;
    }
    DeleteObject(italic);
    if (!setup_first_run(app.wox_path, one.result == 1, two.result == 2, err, sizeof err)) {
        tell(NULL, err, L"Setup", MB_ICONERROR);
        return 0;
    }
    return 1;
}
/** The Addons (ADDONS) or Bugfixes window: its OK keeps the changes in the settings (written by Save only). */
static void flags_window(int addons)
{
    static const wchar_t *const kinds[SETUP_WIZARD_STYLES] = {L"Indoor maps", L"Outdoor maps", L"Sky maps"};
    const setup_flag *flags = addons ? setup_addons : setup_bugfixes;
    unsigned count = addons ? setup_addon_count : setup_bugfix_count;
    const int *values = addons ? app.s.addon : app.s.bugfix;
    int height = 70 + (int)count * 26 + (addons ? 3 * 30 + 10 : 0) + 46, y = 40;
    modal_state m;
    HWND w;
    memset(&m, 0, sizeof m);
    m.flag_list = 1;
    m.addons = addons;
    w = top_window(modal_class, addons ? L"Addons" : L"Bugfixes", 700, height, app.window, &m);
    gray_text(w, addons ? L"Optional additions that are not part of the original game."
                        : L"Corrections of original game errors (not original behaviour).",
              18, 14, 600, 18);
    for (unsigned i = 0; i < count; ++i, y += 26) {
        wchar_t label[200], key[80];
        int indent = flags[i].parent >= 0 ? 24 : 0;
        to_wide(flags[i].label, label, 200);
        to_wide(flags[i].key, key, 80);
        m.check[i] = control(w, L"BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP, 18 + indent, y, 420 - indent, 22,
                             ID_FLAG + (int)i, NULL);
        SendMessageW(m.check[i], BM_SETCHECK, values[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        gray_text(w, key, 470, y + 3, 220, 18);
    }
    if (addons) {
        y += 4;
        for (unsigned k = 0; k < SETUP_WIZARD_STYLES; ++k, y += 30) {
            wchar_t key[80];
            control(w, L"STATIC", kinds[k], SS_LEFT, 66, y + 4, 120, 20, -1, NULL);
            m.style[k] = control(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 190, y, 100, 120,
                                 ID_STYLE + (int)k, NULL);
            for (unsigned v = 0; v < SETUP_WIZARD_STYLES; ++v) {
                wchar_t name[16];
                to_wide(setup_wizard_style_names[v], name, 16);
                SendMessageW(m.style[k], CB_ADDSTRING, 0, (LPARAM)name);
            }
            SendMessageW(m.style[k], CB_SETCURSEL, app.s.wizard_style[k], 0);
            to_wide(setup_wizard_style_keys[k], key, 80);
            gray_text(w, key, 470, y + 4, 220, 18);
        }
    }
    flags_follow(&m);
    control(w, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 700 - 220, height - 42, 100, 28, IDOK, NULL);
    control(w, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 700 - 112, height - 42, 100, 28, IDCANCEL, NULL);
    modal_loop(w, app.window, &m.done);
}

/* ------------------------------------------------------------------ the settings window */
static setup_music music_choice(void)
{
    LRESULT i = SendMessageW(app.music, CB_GETCURSEL, 0, 0);
    return i < 0 ? SETUP_MUSIC_NONE : (setup_music)i;
}
/** The settings with the ROM fields' current text. */
static setup_settings rom_fields(void)
{
    setup_settings s = app.s;
    get_text(app.edit[SETUP_DIR_MT32], s.dir[SETUP_DIR_MT32], sizeof s.dir[0]);
    get_text(app.edit[SETUP_DIR_SC55], s.dir[SETUP_DIR_SC55], sizeof s.dir[0]);
    return s;
}
/** Shows the ROM warning, and the Download button left of it, while the Music choice lacks its ROMs. A running
 * download keeps its progress there until it ends. */
static void update_rom_note(void)
{
    char msg[1600];
    setup_settings s = rom_fields();
    int warn;
    if (app.downloading) {
        return;
    }
    warn = app.cfg_ok && setup_rom_warning(&s, music_choice(), app.exe_dir, msg, sizeof msg);
    if (warn) {
        set_text(app.rom_note, msg);
    }
    show_note(app.rom_note, warn);
    show_note(app.rom_get, warn);
}

/* ------------------------------------------------------------------ the ROM download (Phase 335) */
typedef struct {
    HWND notify;
    setup_rom_set set;
    char dir[SETUP_PATH_MAX];
    wchar_t url[512];
    unsigned char *data;
    size_t size;
    int ok;
    char err[600];
} download_job;

/* The largest archive accepted (the SC-55 one is 21 MB). */
#define DOWNLOAD_MAX ((size_t)64 << 20)

/** WinHTTP's text for error CODE (or the bare number) into OUT. */
static void http_error(DWORD code, char *out, size_t n)
{
    wchar_t w[300];
    char text[300];
    DWORD len = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               GetModuleHandleW(L"winhttp.dll"), code, 0, w, 300, NULL);
    while (len && (w[len - 1] == L'\r' || w[len - 1] == L'\n' || w[len - 1] == L'.')) {
        w[--len] = 0;
    }
    if (len && WideCharToMultiByte(CP_ACP, 0, w, -1, text, (int)sizeof text, NULL, NULL)) {
        snprintf(out, n, "%s (error %lu)", text, (unsigned long)code);
    } else {
        snprintf(out, n, "error %lu", (unsigned long)code);
    }
}
/** Reads the whole response of REQ into J (progress to J->notify). 1 on success. */
static int read_body(download_job *j, HINTERNET req)
{
    DWORD status = 0, total = 0, len = sizeof status;
    size_t cap = 0, reported = 0;
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &len, WINHTTP_NO_HEADER_INDEX)) {
        http_error(GetLastError(), j->err, sizeof j->err);
        return 0;
    }
    if (status != 200u) {
        snprintf(j->err, sizeof j->err, "the server answered HTTP %lu", (unsigned long)status);
        return 0;
    }
    len = sizeof total;
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &total, &len, WINHTTP_NO_HEADER_INDEX)) {
        total = 0; /* not announced: the progress shows the bytes only */
    }
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            http_error(GetLastError(), j->err, sizeof j->err);
            return 0;
        }
        if (!avail) {
            return 1;
        }
        if (avail > DOWNLOAD_MAX - j->size) {
            snprintf(j->err, sizeof j->err, "the download is larger than expected");
            return 0;
        }
        if (j->size + avail > cap) {
            size_t want = cap ? cap * 2u : (total ? (size_t)total : (size_t)1 << 20);
            unsigned char *grown;
            while (want < j->size + avail) {
                want *= 2u;
            }
            if ((grown = realloc(j->data, want)) == NULL) {
                snprintf(j->err, sizeof j->err, "out of memory");
                return 0;
            }
            j->data = grown;
            cap = want;
        }
        if (!WinHttpReadData(req, j->data + j->size, avail, &got)) {
            http_error(GetLastError(), j->err, sizeof j->err);
            return 0;
        }
        j->size += got;
        if (j->size - reported >= ((size_t)256 << 10)) {
            reported = j->size;
            PostMessageW(j->notify, WM_DOWNLOAD_PROGRESS, (WPARAM)j->size, (LPARAM)total);
        }
    }
}
/** Downloads J->url into J->data (HTTPS; WinHTTP follows the archive's redirects). 1 on success. */
static int fetch(download_job *j)
{
    wchar_t host[256], path[1024];
    URL_COMPONENTS u;
    HINTERNET session = NULL, connection = NULL, req = NULL;
    DWORD tls = 0x00000800u | 0x00002000u; /* WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | _TLS1_3 */
    int ok = 0;
    memset(&u, 0, sizeof u);
    u.dwStructSize = sizeof u;
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(j->url, 0, 0, &u)) {
        http_error(GetLastError(), j->err, sizeof j->err);
        return 0;
    }
    session = WinHttpOpen(L"WorldOfXeen-Setup/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (session && !WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof tls)) {
        tls = 0x00000800u; /* a system without TLS 1.3 refuses the pair */
        (void)WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof tls);
    }
    if (session) {
        connection = WinHttpConnect(session, host, u.nPort, 0);
    }
    if (connection) {
        req = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (!req || !WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        http_error(GetLastError(), j->err, sizeof j->err);
    } else {
        ok = read_body(j, req);
    }
    if (req) {
        WinHttpCloseHandle(req);
    }
    if (connection) {
        WinHttpCloseHandle(connection);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    return ok;
}
static DWORD WINAPI download_thread(void *p)
{
    download_job *j = p;
    j->ok = fetch(j);
    if (!PostMessageW(j->notify, WM_DOWNLOAD_DONE, 0, (LPARAM)j)) {
        free(j->data); /* the window is gone */
        free(j);
    }
    return 0;
}
/** "Downloading the SC-55 ROMs... 5.2 of 21.8 MB" in the ROM note. */
static void download_progress(size_t got, size_t total)
{
    char text[200];
    const char *name = setup_rom_set_name(app.download_set);
    if (total) {
        snprintf(text, sizeof text, "Downloading the %s ROMs from archive.org... %.1f of %.1f MB", name,
                 (double)got / 1e6, (double)total / 1e6);
    } else {
        snprintf(text, sizeof text, "Downloading the %s ROMs from archive.org... %.1f MB", name, (double)got / 1e6);
    }
    set_text(app.rom_note, text);
}
/** The Download button: fetches the Music choice's ROM archive on a thread (the window stays responsive). */
static void rom_download(void)
{
    setup_settings s = rom_fields();
    setup_rom_set set = setup_music_rom_set(music_choice());
    download_job *j;
    HANDLE thread;
    if (app.downloading || set == SETUP_ROMS_NONE || (j = calloc(1, sizeof *j)) == NULL) {
        return;
    }
    j->notify = app.window;
    j->set = set;
    setup_rom_set_dir(&s, set, app.exe_dir, j->dir, sizeof j->dir);
    to_wide(setup_rom_set_url(set), j->url, 512);
    app.downloading = 1;
    app.download_set = set;
    EnableWindow(app.rom_get, FALSE);
    set_text(app.rom_get, "Downloading...");
    download_progress(0, 0);
    thread = CreateThread(NULL, 0, download_thread, j, 0, NULL);
    if (!thread) {
        free(j);
        app.downloading = 0;
        EnableWindow(app.rom_get, TRUE);
        set_text(app.rom_get, "Download");
        tell(app.window, "The download could not be started.", L"Download", MB_ICONERROR);
        update_rom_note();
        return;
    }
    CloseHandle(thread);
}
/** The download thread ended: extracts the ROMs (the archive was only in memory), then reports. */
static void rom_download_done(download_job *j)
{
    char msg[1400], err[600];
    const char *name = setup_rom_set_name(j->set);
    int good = 0;
    if (!j->ok) {
        snprintf(msg, sizeof msg, "The %s ROMs could not be downloaded.\n\n%s\n\n%s", name, j->err,
                 setup_rom_set_url(j->set));
    } else if (!setup_rom_install(j->set, j->data, j->size, j->dir, err, sizeof err)) {
        snprintf(msg, sizeof msg, "The %s ROMs could not be installed.\n\n%s", name, err);
    } else {
        snprintf(msg, sizeof msg, "The %s ROMs are installed in %s.", name, j->dir);
        good = 1;
    }
    app.downloading = 0;
    EnableWindow(app.rom_get, TRUE);
    set_text(app.rom_get, "Download");
    update_rom_note();
    tell(app.window, msg, L"Download", good ? MB_ICONINFORMATION : MB_ICONERROR);
    free(j->data);
    free(j);
}

/* ------------------------------------------------------------------ the Music list (owner-drawn, Phase 335) */
/** The Music list's item height: the bold font's line and a small margin. */
static UINT music_item_height(void)
{
    TEXTMETRICW tm;
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, app.bold);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return (UINT)(tm.tmHeight + px(3));
}
/** Paints a Music list item: its label on the left, its note (setup_music_badge) on the right in bold. */
static void draw_music_item(const DRAWITEMSTRUCT *d)
{
    wchar_t text[64] = L"", badge[32];
    RECT r = d->rcItem;
    int selected = (d->itemState & ODS_SELECTED) != 0, disabled = (d->itemState & ODS_DISABLED) != 0;
    const char *note;
    HGDIOBJ old;
    FillRect(d->hDC, &r, GetSysColorBrush(disabled ? COLOR_BTNFACE : selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    if (d->itemID != (UINT)-1) {
        SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, (LPARAM)text);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        r.left += px(4);
        r.right -= px(6);
        old = SelectObject(d->hDC, app.font);
        DrawTextW(d->hDC, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if ((note = setup_music_badge((setup_music)d->itemID)) != NULL) {
            to_wide(note, badge, 32);
            SelectObject(d->hDC, app.bold);
            DrawTextW(d->hDC, badge, -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(d->hDC, old);
    }
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        DrawFocusRect(d->hDC, &d->rcItem);
    }
}
/** (Re)reads XEEN.CFG from the location field: the lists follow it, or are disabled with the reason shown. OFFER_CREATE:
 * a folder without one offers the default first. */
static void read_cfg(int offer_create)
{
    char text[SETUP_PATH_MAX], dir[SETUP_PATH_MAX], note[1600], q[2400];
    setup_cfg_state state;
    get_text(app.edit[SETUP_DIR_XEEN_CFG], text, sizeof text);
    snprintf(app.cfg_dir_read, sizeof app.cfg_dir_read, "%s", text);
    state = setup_xeen_cfg_locate(app.exe_dir, text, &app.cfg, dir, sizeof dir, note, sizeof note);
    if (state == SETUP_CFG_ABSENT && offer_create) {
        setup_create_question(dir, q, sizeof q);
        if (ask(app.window, q, L"XEEN.CFG not found", MB_ICONQUESTION)) {
            if (setup_xeen_cfg_create(dir, q, sizeof q)) {
                state = setup_xeen_cfg_locate(app.exe_dir, text, &app.cfg, dir, sizeof dir, note, sizeof note);
            } else {
                tell(app.window, q, L"XEEN.CFG", MB_ICONERROR);
            }
        }
    }
    app.cfg_ok = state == SETUP_CFG_PROPER;
    app.cfg_state = state;
    EnableWindow(app.speech, app.cfg_ok);
    EnableWindow(app.music, app.cfg_ok);
    SendMessageW(app.speech, CB_SETCURSEL, !app.cfg_ok ? (WPARAM)-1 : setup_xeen_cfg_speech(&app.cfg) ? 0 : 1, 0);
    SendMessageW(app.music, CB_SETCURSEL,
                 !app.cfg_ok        ? (WPARAM)-1
                 : app.s.system_midi ? SETUP_MUSIC_SYSTEM
                                     : (WPARAM)setup_xeen_cfg_music(&app.cfg),
                 0);
    set_text(app.cfg_note, app.cfg_ok ? "" : note);
    show_note(app.cfg_note, !app.cfg_ok);
    ShowWindow(app.cfg_create, state == SETUP_CFG_ABSENT || state == SETUP_CFG_INVALID ? SW_SHOW : SW_HIDE);
    update_rom_note();
}
/** Create default...: the default XEEN.CFG where there is none, or (after a question) in place of an invalid one. */
static void create_cfg(void)
{
    char text[SETUP_PATH_MAX], dir[SETUP_PATH_MAX], reason[600], q[2400];
    if (app.cfg_state != SETUP_CFG_INVALID) {
        read_cfg(1);
        return;
    }
    get_text(app.edit[SETUP_DIR_XEEN_CFG], text, sizeof text);
    setup_resolve(app.exe_dir, text, dir, sizeof dir);
    GetWindowTextA(app.cfg_note, reason, (int)sizeof reason);
    setup_replace_question(dir, reason, q, sizeof q);
    if (ask(app.window, q, L"XEEN.CFG", MB_ICONWARNING)) {
        if (!setup_xeen_cfg_replace(dir, q, sizeof q)) {
            tell(app.window, q, L"XEEN.CFG", MB_ICONERROR);
        }
        read_cfg(0);
    }
}
static void cfg_field_done(void)
{
    char text[SETUP_PATH_MAX];
    get_text(app.edit[SETUP_DIR_XEEN_CFG], text, sizeof text);
    if (strcmp(text, app.cfg_dir_read) != 0) {
        read_cfg(1);
    }
}
static void data_field_done(void)
{
    char data[SETUP_PATH_MAX], cfg[SETUP_PATH_MAX];
    get_text(app.edit[SETUP_DIR_DATA], data, sizeof data);
    get_text(app.edit[SETUP_DIR_XEEN_CFG], cfg, sizeof cfg);
    if (strcmp(data, app.data_before) != 0 && !strcmp(cfg, app.data_before)) {
        set_text(app.edit[SETUP_DIR_XEEN_CFG], data);
        read_cfg(1);
    }
    snprintf(app.data_before, sizeof app.data_before, "%s", data);
}
static void field_done(int k)
{
    if (k == SETUP_DIR_XEEN_CFG) {
        cfg_field_done();
    } else if (k == SETUP_DIR_DATA) {
        data_field_done();
    } else {
        update_rom_note();
    }
}
/** START as a full path with backslashes into OUT (N characters), or its nearest existing parent: the Shell does not
 * parse a path with forward slashes (roms/mt32's default before Phase 333), and a folder not created yet opens at
 * its parent. 0 when no part of it exists. */
static int existing_start(const char *start, wchar_t *out, DWORD n)
{
    wchar_t w[SETUP_PATH_MAX];
    DWORD len;
    to_wide(start, w, SETUP_PATH_MAX);
    len = GetFullPathNameW(w, n, out, NULL); /* also turns each slash into a backslash */
    if (!len || len >= n) {
        return 0;
    }
    for (;;) {
        DWORD a = GetFileAttributesW(out);
        wchar_t *cut;
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
            return 1;
        }
        cut = wcsrchr(out, L'\\');
        if (!cut || cut == out || (cut == out + 2 && out[1] == L':')) {
            return 0;
        }
        *cut = 0;
    }
}
/** The standard folder dialog (IFileOpenDialog, FOS_PICKFOLDERS) starting at START; 1 with the folder in OUT. */
static int pick_folder(const char *start, wchar_t *out, int n)
{
    IFileOpenDialog *d = NULL;
    IShellItem *item = NULL;
    DWORD options;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog,
                                (void **)&d))) {
        return 0;
    }
    if (SUCCEEDED(IFileOpenDialog_GetOptions(d, &options))) {
        IFileOpenDialog_SetOptions(d, options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    {
        wchar_t w[SETUP_PATH_MAX];
        IShellItem *folder = NULL;
        if (existing_start(start, w, SETUP_PATH_MAX) &&
            SUCCEEDED(SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&folder))) {
            IFileOpenDialog_SetFolder(d, folder);
            IShellItem_Release(folder);
        }
    }
    IFileOpenDialog_SetTitle(d, L"Choose a folder");
    if (SUCCEEDED(IFileOpenDialog_Show(d, app.window)) && SUCCEEDED(IFileOpenDialog_GetResult(d, &item))) {
        wchar_t *path = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            lstrcpynW(out, path, n);
            CoTaskMemFree(path);
            ok = 1;
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(d);
    return ok;
}
static void browse(int k)
{
    char start[SETUP_PATH_MAX], text[SETUP_PATH_MAX], chosen[SETUP_PATH_MAX];
    wchar_t w[SETUP_PATH_MAX];
    get_text(app.edit[k], text, sizeof text);
    setup_resolve(app.exe_dir, text, start, sizeof start);
    if (!pick_folder(start, w, SETUP_PATH_MAX)) {
        return;
    }
    if (!to_ansi(w, chosen, sizeof chosen)) {
        tell(app.window,
             "This folder's name has characters the game cannot open on this system. Please choose another folder.",
             L"Setup", MB_ICONWARNING);
        return;
    }
    setup_relative_to(app.exe_dir, chosen, text, sizeof text);
    set_text(app.edit[k], text);
    field_done(k);
}
/** The window's fields and switches into S (the add-on and bug-fix lists are already in app.s). */
static void collect_into(setup_settings *s)
{
    for (unsigned k = 0; k < SETUP_DIRS; ++k) {
        get_text(app.edit[k], s->dir[k], sizeof s->dir[k]);
    }
    s->fullscreen = SendMessageW(app.fullscreen, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->aspect = SendMessageW(app.aspect, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->locked_scale = SendMessageW(app.locked, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s->scale = (unsigned)SendMessageW(app.scale, CB_GETCURSEL, 0, 0) + 1u;
    if (app.cfg_ok) {
        s->system_midi = music_choice() == SETUP_MUSIC_SYSTEM;
    }
}
static void collect(void)
{
    collect_into(&app.s);
}
/** Remembers the window's state as built (window_changed compares with it). */
static void remember_window(void)
{
    app.initial = app.s;
    collect_into(&app.initial);
    app.initial_speech = SendMessageW(app.speech, CB_GETCURSEL, 0, 0);
    app.initial_music = SendMessageW(app.music, CB_GETCURSEL, 0, 0);
}
/** Whether anything Save would write differs from the window as it was built. */
static int window_changed(void)
{
    setup_settings now = app.s;
    collect_into(&now);
    return !setup_settings_same(&now, &app.initial) ||
           SendMessageW(app.speech, CB_GETCURSEL, 0, 0) != app.initial_speech ||
           SendMessageW(app.music, CB_GETCURSEL, 0, 0) != app.initial_music;
}
/** The folders Save needs: a missing save or XEEN.CFG folder is created on request; a missing data folder is only
 * confirmed (setup_missing_folder, setup_missing_data). 1 when Save may go on. */
static int check_folders(void)
{
    char q[2400];
    setup_dir which;
    while (setup_missing_folder(&app.s, app.exe_dir, &which, q, sizeof q)) {
        if (!ask(app.window, q, L"Setup", MB_ICONQUESTION)) {
            return 0;
        }
        if (!setup_make_dirs(app.exe_dir, app.s.dir[which])) {
            tell(app.window, "The folder could not be created.", L"Setup", MB_ICONERROR);
            return 0;
        }
    }
    return !setup_missing_data(&app.s, app.exe_dir, q, sizeof q) || ask(app.window, q, L"Setup", MB_ICONWARNING);
}
static void save(void)
{
    char err[1600];
    for (unsigned k = 0; k < SETUP_DIRS; ++k) {
        char t[SETUP_PATH_MAX];
        if (!get_text(app.edit[k], t, sizeof t)) {
            tell(app.window, "A folder name has characters the game cannot open on this system.", L"Setup",
                 MB_ICONWARNING);
            return;
        }
    }
    cfg_field_done();
    collect();
    if (!check_folders()) {
        return;
    }
    if (!setup_save(app.wox_path, &app.doc, &app.s, app.cfg_ok ? &app.cfg : NULL,
                    SendMessageW(app.speech, CB_GETCURSEL, 0, 0) == 0, music_choice(), err, sizeof err)) {
        tell(app.window, err, L"Setup: not saved", MB_ICONERROR);
        return;
    }
    app.done = 1;
}
/** Initial setup...: the first run again (the main window hidden meanwhile); when it completes, wox.cfg has been
 * replaced and the window is built again from it. Not while a ROM download runs (its result goes to this window). */
static void rerun_first_run(void)
{
    int ok;
    if (app.downloading) {
        tell(app.window, "Please wait until the ROM download ends.", L"Setup", MB_ICONINFORMATION);
        return;
    }
    if (!ask(app.window, SETUP_TEXT_RERUN_QUESTION, L"Initial setup", MB_ICONWARNING)) {
        return;
    }
    ShowWindow(app.window, SW_HIDE); /* only the first run's screens while it runs */
    ok = first_run(1);
    if (!ok) {
        ShowWindow(app.window, SW_SHOW);
        SetForegroundWindow(app.window);
    }
    if (ok) {
        app.rerun = 1;
        app.done = 1;
    }
}
/** Cancel (or closing the window): asks first only when something was changed (Phase 335). */
static void cancel(void)
{
    if (!window_changed() || ask(app.window, "Are you sure you want to abandon all changes?\n\nNothing will be saved.", L"Cancel",
            MB_ICONQUESTION)) {
        app.done = 1;
    }
}
/** The edit control that has the focus (its setup_dir), or -1. */
static int focused_field(void)
{
    HWND f = GetFocus();
    for (int k = 0; k < SETUP_DIRS; ++k) {
        if (f == app.edit[k]) {
            return k;
        }
    }
    return -1;
}
static LRESULT CALLBACK main_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wp), code = HIWORD(wp);
            if (id >= ID_EDIT && id < ID_EDIT + SETUP_DIRS && code == EN_KILLFOCUS) {
                field_done(id - ID_EDIT);
            } else if (id >= ID_BROWSE && id < ID_BROWSE + SETUP_DIRS) {
                browse(id - ID_BROWSE);
            } else if (id == ID_MUSIC && code == CBN_SELCHANGE) {
                update_rom_note();
            } else if (id == ID_ASPECT) {
                show_note(app.aspect_note, SendMessageW(app.aspect, BM_GETCHECK, 0, 0) == BST_CHECKED);
            } else if (id == ID_ROM_GET) {
                rom_download();
            } else if (id == ID_CREATE_CFG) {
                create_cfg();
            } else if (id == ID_ADDONS || id == ID_BUGFIXES) {
                flags_window(id == ID_ADDONS);
            } else if (id == ID_RERUN) {
                rerun_first_run();
            } else if (id == ID_SAVE) {
                save();
            } else if (id == IDCANCEL) {
                cancel();
            } else if (id == IDOK && focused_field() >= 0) {
                field_done(focused_field()); /* Enter in a folder field applies it */
            }
            return 0;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT *m = (MEASUREITEMSTRUCT *)lp;
            if (m->CtlType == ODT_COMBOBOX && m->CtlID == ID_MUSIC) {
                m->itemHeight = music_item_height();
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            if (wp == ID_MUSIC) {
                draw_music_item((const DRAWITEMSTRUCT *)lp);
                return TRUE;
            }
            break;
        case WM_DOWNLOAD_PROGRESS:
            if (app.downloading) {
                download_progress((size_t)wp, (size_t)lp);
            }
            return 0;
        case WM_DOWNLOAD_DONE:
            rom_download_done((download_job *)lp);
            return 0;
        case WM_CTLCOLORBTN:
            SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        case WM_CTLCOLORSTATIC: {
            HWND c = (HWND)lp;
            if (c == app.cfg_note || c == app.rom_note) {
                SetTextColor((HDC)wp, RGB(170, 90, 0));
            } else if (c == app.aspect_note) {
                SetTextColor((HDC)wp, RGB(40, 100, 190));
            }
            SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_CLOSE:
            cancel();
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static void path_row(int y, setup_dir k)
{
    wchar_t w[SETUP_PATH_MAX];
    control(app.window, L"STATIC", dir_labels[k], SS_LEFT, 30, y + 3, 150, 20, -1, NULL);
    to_wide(app.s.dir[k], w, SETUP_PATH_MAX);
    app.edit[k] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", w, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  px(180), px(y), px(340), px(24), app.window, (HMENU)(INT_PTR)(ID_EDIT + (int)k),
                                  GetModuleHandleW(NULL), NULL);
    SendMessageW(app.edit[k], WM_SETFONT, (WPARAM)app.font, TRUE);
    control(app.window, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 528, y, 90, 24, ID_BROWSE + (int)k, NULL);
}
static void build_window(void)
{
    int y = 12;
    app.window = top_window(main_class, SETUP_MAIN_TITLE_W, 640, 690, NULL, NULL);
    /* XEEN.CFG */
    group_box(app.window, L"XEEN.CFG", 14, y, 612, 216); /* three lines of ROM warning (Phase 333) */
    path_row(y + 26, SETUP_DIR_XEEN_CFG);
    app.cfg_note = control(app.window, L"STATIC", L"", SS_LEFT, 180, y + 56, 300, 20, -1, NULL);
    app.cfg_create = control(app.window, L"BUTTON", L"Create default...", BS_PUSHBUTTON | WS_TABSTOP, 488, y + 54,
                             130, 24, ID_CREATE_CFG, NULL);
    control(app.window, L"STATIC", L"Speech", SS_LEFT, 30, y + 89, 140, 20, -1, NULL);
    app.speech = control(app.window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 180, y + 86, 120, 100, ID_SPEECH,
                         NULL);
    SendMessageW(app.speech, CB_ADDSTRING, 0, (LPARAM)L"on");
    SendMessageW(app.speech, CB_ADDSTRING, 0, (LPARAM)L"off");
    control(app.window, L"STATIC", L"Music", SS_LEFT, 30, y + 121, 140, 20, -1, NULL);
    /* Owner-drawn for the right-aligned bold "recommended" (Phase 335); as wide as the folder fields. */
    app.music = control(app.window, L"COMBOBOX", L"",
                        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, 180, y + 118,
                        340, 320, ID_MUSIC, NULL);
    for (unsigned m = 0; m < SETUP_MUSIC_CHOICES; ++m) {
        wchar_t label[64];
        to_wide(setup_music_label((setup_music)m), label, 64);
        SendMessageW(app.music, CB_ADDSTRING, 0, (LPARAM)label);
    }
    app.rom_note = control(app.window, L"STATIC", L"", SS_LEFT, 180, y + 148, 438, 62, -1, NULL);
    /* Phase 335: left of the ROM warning, shown with it */
    app.rom_get = control(app.window, L"BUTTON", L"Download", BS_PUSHBUTTON | WS_TABSTOP, 30, y + 146, 110, 26,
                          ID_ROM_GET, NULL);
    ShowWindow(app.rom_get, SW_HIDE);
    y += 230;
    /* Game files */
    group_box(app.window, L"Game files", 14, y, 612, 190);
    for (unsigned k = SETUP_DIR_DATA; k < SETUP_DIRS; ++k) {
        path_row(y + 24 + ((int)k - 1) * 32, (setup_dir)k);
    }
    snprintf(app.data_before, sizeof app.data_before, "%s", app.s.dir[SETUP_DIR_DATA]);
    y += 204;
    /* Display */
    group_box(app.window, L"Display", 14, y, 612, 92);
    app.fullscreen = control(app.window, L"BUTTON", L"Fullscreen", BS_AUTOCHECKBOX | WS_TABSTOP, 30, y + 24, 200, 22,
                             ID_FULLSCREEN, NULL);
    SendMessageW(app.fullscreen, BM_SETCHECK, app.s.fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    app.locked = control(app.window, L"BUTTON", L"Lock scale", BS_AUTOCHECKBOX | WS_TABSTOP, 250, y + 24, 200, 22,
                         ID_LOCKED, NULL);
    SendMessageW(app.locked, BM_SETCHECK, app.s.locked_scale ? BST_CHECKED : BST_UNCHECKED, 0);
    control(app.window, L"STATIC", L"Window scale", SS_LEFT, 30, y + 57, 100, 20, -1, NULL);
    app.scale = control(app.window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 140, y + 54, 70, 280,
                        ID_SCALE, NULL);
    for (unsigned v = 1u; v <= XEEN_WOX_SCALE_MAX; ++v) {
        char text[8];
        wchar_t label[8];
        snprintf(text, sizeof text, "%u", v);
        to_wide(text, label, 8);
        SendMessageW(app.scale, CB_ADDSTRING, 0, (LPARAM)label);
    }
    SendMessageW(app.scale, CB_SETCURSEL, app.s.scale - 1u, 0);
    app.aspect = control(app.window, L"BUTTON", L"CRT aspect", BS_AUTOCHECKBOX | WS_TABSTOP, 250, y + 56, 100, 22,
                         ID_ASPECT, NULL);
    SendMessageW(app.aspect, BM_SETCHECK, app.s.aspect ? BST_CHECKED : BST_UNCHECKED, 0);
    app.aspect_note = control(app.window, L"STATIC", L"Recommended to set window scale to 5 or 10", SS_LEFT, 354,
                              y + 58, 266, 20, -1, NULL);
    show_note(app.aspect_note, app.s.aspect);
    y += 106;
    /* Enhancements */
    group_box(app.window, L"Enhancements", 14, y, 612, 70);
    control(app.window, L"BUTTON", L"Addons...", BS_PUSHBUTTON | WS_TABSTOP, 30, y + 26, 160, 30, ID_ADDONS, NULL);
    control(app.window, L"BUTTON", L"Bugfixes...", BS_PUSHBUTTON | WS_TABSTOP, 206, y + 26, 160, 30, ID_BUGFIXES, NULL);
    y += 84;
    control(app.window, L"BUTTON", L"Initial setup...", BS_PUSHBUTTON | WS_TABSTOP, 14, y, 130, 30, ID_RERUN, NULL);
    control(app.window, L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, 640 - 228, y, 100, 30, ID_SAVE, NULL);
    control(app.window, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 640 - 118, y, 100, 30, IDCANCEL, NULL);
    read_cfg(0);
    remember_window();
}
static void register_classes(void)
{
    WNDCLASSEXW c;
    memset(&c, 0, sizeof c);
    c.cbSize = sizeof c;
    c.hInstance = GetModuleHandleW(NULL);
    c.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    c.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    /* Phase 333: resource 1, icon/setup_icon.bmp (src/icon/make_icons.py --single) */
    c.hIcon = (HICON)LoadImageW(c.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    c.hIconSm = (HICON)LoadImageW(c.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    c.lpfnWndProc = main_proc;
    c.lpszClassName = main_class;
    RegisterClassExW(&c);
    c.lpfnWndProc = modal_proc;
    c.lpszClassName = modal_class;
    RegisterClassExW(&c);
}
/** SetProcessDPIAware when the system has it (Vista and later), so the window is drawn at the screen's DPI. */
static void dpi_aware(void)
{
    HMODULE user = GetModuleHandleW(L"user32.dll");
    FARPROC f = user ? GetProcAddress(user, "SetProcessDPIAware") : NULL;
    if (f) {
        BOOL(WINAPI * set)(void);
        memcpy(&set, &f, sizeof set);
        set();
    }
}

int main(int argc, char **argv)
{
    char err[1600];
    xeen_wox_config *probe = malloc(sizeof *probe);
    int status = 0;
    if (!probe) {
        return 1;
    }
    dpi_aware();
    InitCommonControls();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    make_fonts();
    register_classes();
    setup_exe_dir(argc > 0 ? argv[0] : NULL, app.exe_dir, sizeof app.exe_dir);
    snprintf(app.wox_path, sizeof app.wox_path, "%s\\wox.cfg", app.exe_dir);
    setup_doc_init(&app.doc);
    if (GetFileAttributesA(app.wox_path) != INVALID_FILE_ATTRIBUTES) {
        if (!xeen_wox_config_load(app.wox_path, probe, err, sizeof err)) {
            char q[2000];
            snprintf(q, sizeof q, "Recreate wox.cfg with the default settings?\n\nwox.cfg has an error: %s", err);
            if (!ask(NULL, q, L"WoX - Setup", MB_ICONWARNING) || !first_run(1)) {
                free(probe);
                return 0;
            }
        }
    } else if (!first_run(0)) {
        free(probe);
        return 0;
    }
    free(probe);
    do { /* again after Initial setup... replaced wox.cfg */
        app.rerun = app.done = 0;
        if (!setup_doc_load(&app.doc, app.wox_path, err, sizeof err)) {
            tell(NULL, err, L"Setup", MB_ICONERROR);
            setup_doc_free(&app.doc);
            return 1;
        }
        setup_settings_from_doc(&app.doc, &app.s);
        build_window();
        modal_loop(app.window, NULL, &app.done);
        setup_doc_free(&app.doc);
        setup_doc_init(&app.doc);
    } while (app.rerun);
    CoUninitialize();
    return status;
}
