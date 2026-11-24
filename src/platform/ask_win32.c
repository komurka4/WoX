/* Phase 335 (owner request): Windows questions shown before the game's window exists (xeen_platform_ask), and
 * starting another program beside the game (xeen_platform_run). Host behaviour, not original. The question is a
 * small modal window in the style of a message box: the question icon and text on the window colour, the buttons on
 * a strip below, with an optional third button on the strip's left (the missing wox.cfg's "Run setup"). */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include "xeen/platform.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

enum { ASK_EXTRA = 100 };

typedef struct {
    HFONT font;
    HICON icon;
    int dpi, done, result;
    int strip_top;        /* the button strip's top (client pixels) */
    RECT text;            /* where the text is drawn */
    wchar_t message[2048];
} ask_state;

static ask_state ask;

/** Pixels for N 96-dpi units. */
static int ask_px(int n)
{
    return MulDiv(n, ask.dpi, 96);
}
static LRESULT CALLBACK ask_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ASK_EXTRA) {
                /* the program this button starts (xeen_platform_run) may come to the front: wox, still the
                 * foreground process now, passes that right on before it ends */
                AllowSetForegroundWindow(ASFW_ANY);
            }
            if (id == IDYES || id == IDNO || id == ASK_EXTRA || id == IDCANCEL) {
                ask.result = id == IDYES ? 1 : id == ASK_EXTRA ? 2 : 0; /* Esc: No */
                ask.done = 1;
            }
            return 0;
        }
        case WM_CLOSE:
            ask.result = 0;
            ask.done = 1;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            RECT c, strip;
            HDC dc = BeginPaint(w, &ps);
            HGDIOBJ old;
            GetClientRect(w, &c);
            strip = c;
            strip.top = ask.strip_top;
            c.bottom = ask.strip_top;
            FillRect(dc, &c, GetSysColorBrush(COLOR_WINDOW));
            FillRect(dc, &strip, GetSysColorBrush(COLOR_BTNFACE));
            if (ask.icon) {
                DrawIconEx(dc, ask_px(20), ask_px(20), ask.icon, ask_px(32), ask_px(32), 0, NULL, DI_NORMAL);
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            old = SelectObject(dc, ask.font);
            DrawTextW(dc, ask.message, -1, &ask.text, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
            SelectObject(dc, old);
            EndPaint(w, &ps);
            return 0;
        }
    }
    return DefWindowProcW(w, msg, wp, lp);
}
/** A push button of the question window at pixels X, Y, W, H. */
static HWND ask_button(HWND parent, const wchar_t *text, int id, DWORD style, int x, int y, int w, int h)
{
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)ask.font, TRUE);
    return b;
}
/** The width of TEXT in the question's font, in pixels. */
static int ask_text_width(HDC dc, const wchar_t *text)
{
    SIZE s = {0, 0};
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &s);
    return s.cx;
}
/** Common Controls 6 for the buttons (themed, as in a message box): wox.exe has no manifest of its own, so the
 * one Windows ships in shell32.dll (resource 124) is activated while the window exists. 0 when unavailable. */
static HANDLE themed_context(ULONG_PTR *cookie)
{
    wchar_t shell[MAX_PATH + 16];
    ACTCTXW ctx;
    HANDLE h;
    UINT len = GetSystemDirectoryW(shell, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return INVALID_HANDLE_VALUE;
    }
    wcscat(shell, L"\\shell32.dll");
    memset(&ctx, 0, sizeof ctx);
    ctx.cbSize = sizeof ctx;
    ctx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    ctx.lpSource = shell;
    ctx.lpResourceName = MAKEINTRESOURCEW(124);
    h = CreateActCtxW(&ctx);
    if (h != INVALID_HANDLE_VALUE && !ActivateActCtx(h, cookie)) {
        ReleaseActCtx(h);
        h = INVALID_HANDLE_VALUE;
    }
    return h;
}
int xeen_platform_ask(const char *title, const char *text, const char *extra_button)
{
    static const wchar_t cls[] = L"WoxQuestion";
    wchar_t wide_title[128], extra[64] = L"";
    NONCLIENTMETRICSW m;
    WNDCLASSEXW wc;
    ULONG_PTR cookie = 0;
    HANDLE context;
    HWND w, yes;
    HDC dc;
    RECT r;
    MSG msg;
    int text_w, client_w, client_h, top_h, button_w, extra_w = 0;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    if (!title || !text || !MultiByteToWideChar(CP_ACP, 0, title, -1, wide_title, 128) ||
        !MultiByteToWideChar(CP_ACP, 0, text, -1, ask.message, 2048) ||
        (extra_button && !MultiByteToWideChar(CP_ACP, 0, extra_button, -1, extra, 64))) {
        return -1;
    }
    dc = GetDC(NULL);
    ask.dpi = GetDeviceCaps(dc, LOGPIXELSY);
    m.cbSize = sizeof m;
    ask.font = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0) ? CreateFontIndirectW(&m.lfMessageFont)
                                                                              : NULL;
    if (!ask.font) {
        ask.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    SelectObject(dc, ask.font);
    /* the text's own width, from 300 to 460 units, then its height wrapped to that width */
    text_w = ask_px(460);
    r.left = r.top = 0;
    r.right = text_w;
    r.bottom = 0;
    DrawTextW(dc, ask.message, -1, &r, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    if (r.right < ask_px(300)) {
        r.right = ask_px(300);
    }
    text_w = r.right;
    button_w = ask_px(88);
    if (*extra) {
        extra_w = ask_text_width(dc, extra) + ask_px(28);
        extra_w = extra_w < button_w ? button_w : extra_w;
    }
    ReleaseDC(NULL, dc);
    top_h = (r.bottom > ask_px(32) ? r.bottom : ask_px(32)) + ask_px(40);
    client_w = ask_px(66) + text_w + ask_px(24);
    if (client_w < extra_w + 2 * button_w + ask_px(52)) {
        client_w = extra_w + 2 * button_w + ask_px(52);
    }
    client_h = top_h + ask_px(48);
    ask.strip_top = top_h;
    SetRect(&ask.text, ask_px(66), ask_px(20), ask_px(66) + text_w, ask_px(20) + r.bottom);
    ask.icon = LoadIconW(NULL, (LPCWSTR)IDI_QUESTION);
    ask.done = 0;
    ask.result = 0;

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = ask_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = cls;
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1)); /* the game's icon, when the executable has it */
    RegisterClassExW(&wc);
    context = themed_context(&cookie);
    SetRect(&r, 0, 0, client_w, client_h);
    AdjustWindowRectEx(&r, style, FALSE, ex);
    w = CreateWindowExW(ex, cls, wide_title, style, (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                        (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 3, r.right - r.left,
                        r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!w) {
        if (context != INVALID_HANDLE_VALUE) {
            DeactivateActCtx(0, cookie);
            ReleaseActCtx(context);
        }
        return -1;
    }
    {
        int y = top_h + ask_px(11), h = ask_px(26), right = client_w - ask_px(12);
        if (*extra) {
            ask_button(w, extra, ASK_EXTRA, BS_PUSHBUTTON, ask_px(12), y, extra_w, h);
        }
        yes = ask_button(w, L"&Yes", IDYES, BS_DEFPUSHBUTTON, right - 2 * button_w - ask_px(8), y, button_w, h);
        ask_button(w, L"&No", IDNO, BS_PUSHBUTTON, right - button_w, y, button_w, h);
    }
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    SetFocus(yes);
    MessageBeep(MB_ICONQUESTION);
    while (!ask.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND f = GetFocus(); /* Enter presses the focused button, Yes by default */
            int id = f ? GetDlgCtrlID(f) : IDYES;
            SendMessageW(w, WM_COMMAND, (WPARAM)(id == IDNO || id == ASK_EXTRA ? id : IDYES), 0);
            continue;
        }
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(w);
    if (context != INVALID_HANDLE_VALUE) {
        DeactivateActCtx(0, cookie);
        ReleaseActCtx(context);
    }
    if (ask.font != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(ask.font);
    }
    return ask.result;
}
int xeen_platform_run(const char *path)
{
    char command[MAX_PATH + 8], dir[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char *slash;
    if (snprintf(command, sizeof command, "\"%s\"", path) >= (int)sizeof command ||
        snprintf(dir, sizeof dir, "%s", path) >= (int)sizeof dir) {
        return 0;
    }
    slash = strrchr(dir, '\\');
    if (slash) {
        *slash = 0; /* the program starts in its own folder */
    }
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, slash ? dir : NULL, &si, &pi)) {
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
