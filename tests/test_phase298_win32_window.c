/* Phase 298: platform.c's Win32 backend (window procedure, UI-owner thread, lifecycle, boss screen, fullscreen,
 * fatal dialog, paint) run on Linux under a logging mock of the Win32 API (tests/win32_mock), with checks.
 *
 * The real unit is #included with _WIN32 defined (SIDE_UNIT, default ../src/platform/platform.c; Phase 298's
 * side-by-side differential builds the same file against its frozen base unit); <windows.h> and <mmsystem.h> are
 * the mock headers (types and the real message, key and style values). The unit's threads are real: CreateThread
 * starts a pthread running the unit's ui_main, whose GetMessageA loop dispatches into the unit's window_proc.
 *
 * Determinism: a baton lets exactly one of the two threads run at a time. The game thread (the case driver) hands
 * it to the UI thread wherever the real game thread would block or give the UI thread time: Sleep, waiting on an
 * event or the thread, a cross-thread SetWindowPos (a sent WM_SIZE), and the driver's `settle` after each posted
 * message. The UI thread hands it back when its GetMessageA would block (no posted message, no quit, nothing
 * invalid) or when its thread ends. This is one legal interleaving of the real program; the transcript is then a
 * deterministic function of the case. A lock held across a hand-over (the real program would block there) is a
 * FAIL.
 *
 * The mock's Windows semantics (only what the unit relies on): GetMessageA serves posted messages first, then a
 * posted quit, then WM_PAINT while the window has an invalid region (BeginPaint validates it); a key message
 * carries its own keyboard state (GetKeyState / GetKeyboardState during its dispatch); SendMessageA on the window's
 * thread calls the window procedure directly; CreateWindowA sends WM_GETMINMAXINFO, WM_NCCREATE, WM_CREATE, WM_SIZE;
 * ShowWindow sends WM_SHOWWINDOW, WM_ACTIVATE, WM_SETFOCUS and invalidates; DestroyWindow sends WM_DESTROY,
 * WM_NCDESTROY; SetWindowPos with a size sends WM_SIZE; DefWindowProcA answers WM_NCCREATE TRUE, keeps WM_SETICON /
 * WM_GETICON, and turns Alt+F4 into a sent WM_CLOSE. Everything else is logged and answered 0. It is not Windows:
 * a real desktop's focus, activation, Alt+Tab and modal loops are the owner's manual checks (Phase 298 report).
 *
 * Checked: the named scenarios' facts (F10 and the calculator, Ctrl+Q's contexts, Ctrl+C's plain code, Alt+Enter,
 * focus loss, Alt+F4, the modal paint timer, the fatal dialog, the SC-55 start-up ready / closed / F10, a failed
 * window, close at the calculator, wheel and pointer), and for every case (the named ones and N random ones of
 * random options and scripts) no FAIL from the mock (thread affinity, lock hand-overs, double releases, hangs) and
 * nothing left at the end (windows, threads, events, DCs, GDI objects, icons, timers). XEEN_WIN32_TRANSCRIPT=1
 * prints the transcript (every mock call with its thread, the platform's fields after each step).
 *
 * usage: test_phase298_win32_window [N [FIRST]]   (the named scenarios, then N random cases from seed FIRST) */
#define _GNU_SOURCE 1
#define _WIN32 1
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(HARNESS_GCOV)
void __gcov_dump(void);
#define CHILD_EXIT(c) (fflush(stdout), __gcov_dump(), _exit(checks_failed ? 5 : (c)))
#else
#define CHILD_EXIT(c) (fflush(stdout), _exit(checks_failed ? 5 : (c)))
#endif
#if !defined(SIDE_UNIT)
#define SIDE_UNIT "../src/platform/platform.c"
#endif
#include SIDE_UNIT
#include "../src/app/host_startup.c"

/* ---- the transcript ---------------------------------------------------------------------------------------- */
static _Thread_local int me_ui; /* 1 on the UI-owner thread */
static int report_fd = 1, checks_failed; /* the test's own output (the transcript may go to /dev/null) */
static void LOG(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    fputs(me_ui ? "U " : "G ", stdout);
    vprintf(fmt, a);
    va_end(a);
}
static void FAIL(const char *why)
{
    LOG("FAIL: %s\n", why);
    if (report_fd != 1) {
        dprintf(report_fd, "FAIL: %s\n", why);
    }
    fflush(stdout);
    _exit(4);
}
/* A failed check: reported on the test's own output (report_fd, kept when the transcript is off), case exit 5. */
static void check(int ok, const char *what)
{
    if (!ok) {
        LOG("CHECK FAILED: %s\n", what);
        if (report_fd != 1) {
            dprintf(report_fd, "CHECK FAILED: %s\n", what);
        }
        ++checks_failed;
    }
}
static unsigned seed;
static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}
static unsigned below(unsigned n) { return n ? rnd() % n : 0; }
static uint64_t fnv(const void *v, size_t n)
{
    const uint8_t *p = v;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* A message's LPARAM for the transcript: the pointer-carrying ones (CREATESTRUCT, the fatal request) as "ptr". */
static unsigned long lp_of(UINT m, LPARAM l)
{
    return (m == 0x0081u || m == 0x0001u || m == 0x8005u || m == 0x0024u) && l ? 0xb0b0b0b0ul : (unsigned long)l;
}
/* ---- options of the case --------------------------------------------------------------------------------- */
static struct {
    int load_image_fails, create_window_fails, create_thread_fails, placement_fails, monitor_fails, dead_keys,
            dib_fails, menu_answer;
} opt;

/* ---- the baton ----------------------------------------------------------------------------------------------- */
static pthread_mutex_t baton_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t baton_c = PTHREAD_COND_INITIALIZER;
static int turn_ui;         /* whose turn */
static int ui_started, ui_running; /* a UI thread exists / has not ended */
/* Game thread: let the UI thread run until it is idle or ended. */
static void run_ui(void)
{
    if (me_ui) {
        FAIL("run_ui on the UI thread");
    }
    if (!ui_running) {
        return;
    }
    pthread_mutex_lock(&baton_m);
    turn_ui = 1;
    pthread_cond_broadcast(&baton_c);
    while (turn_ui) {
        pthread_cond_wait(&baton_c, &baton_m);
    }
    pthread_mutex_unlock(&baton_m);
}
/* UI thread: give the baton back and wait for it again. */
static void ui_idle(void)
{
    pthread_mutex_lock(&baton_m);
    turn_ui = 0;
    pthread_cond_broadcast(&baton_c);
    while (!turn_ui) {
        pthread_cond_wait(&baton_c, &baton_m);
    }
    pthread_mutex_unlock(&baton_m);
}

/* ---- handles ----------------------------------------------------------------------------------------------- */
enum { H_FREE, H_EVENT, H_THREAD };
typedef struct {
    int kind, set, open;
    pthread_t tid;
    LPTHREAD_START_ROUTINE start;
    LPVOID arg;
    DWORD code;
} handle;
static handle handles[16];
static int handle_index(HANDLE h)
{
    intptr_t i = (intptr_t)h - 0x1000;
    if (i < 0 || i >= 16 || !handles[i].open) {
        return -1;
    }
    return (int)i;
}
static HANDLE new_handle(int kind)
{
    for (int i = 0; i < 16; ++i) {
        if (handles[i].kind == H_FREE) {
            memset(&handles[i], 0, sizeof handles[i]);
            handles[i].kind = kind;
            handles[i].open = 1;
            return (HANDLE)(intptr_t)(0x1000 + i);
        }
    }
    FAIL("out of handles");
    return NULL;
}
static int signalled(int i) { return handles[i].kind == H_EVENT ? handles[i].set : !ui_running; }

/* ---- the window ---------------------------------------------------------------------------------------------- */
struct mock_window {
    int alive, invalid, shown;
    LONG_PTR userdata, style;
    HICON icon[3];
    char title[160];
    RECT client;
};
static struct mock_window the_window;
static WNDPROC wndproc;
static ULONG_PTR class_icon, class_icon_small;
typedef struct {
    UINT message;
    WPARAM w;
    LPARAM l;
    BYTE keys[256];
    int quit;
} queued;
static queued msgq[64];
static int qhead, qtail, quit_posted;
static BYTE keys_now[256], async_buttons; /* the dispatched message's keyboard state; the physical buttons */
static int live_dc, live_gdi, live_icons, timer_on, painting;
static int64_t qpc;
static unsigned sleeps;
static HWND as_hwnd(void) { return &the_window; }
static int window_ok(HWND h) { return h == &the_window && the_window.alive; }
static void push(UINT m, WPARAM w, LPARAM l, const BYTE *keys, int quit)
{
    if (qtail - qhead >= 64) {
        FAIL("message queue full");
    }
    queued *q = &msgq[qtail++ % 64];
    q->message = m;
    q->w = w;
    q->l = l;
    q->quit = quit;
    memcpy(q->keys, keys ? keys : keys_now, 256);
}
static LRESULT call_proc(UINT m, WPARAM w, LPARAM l)
{
    LRESULT r;
    if (!wndproc) {
        FAIL("no window procedure");
    }
    r = wndproc(as_hwnd(), m, w, l);
    LOG("  proc(%04x,%lx,%lx)=%ld\n", m, (unsigned long)w, lp_of(m, l), (long)r);
    return r;
}

/* ---- kernel ------------------------------------------------------------------------------------------------ */
static void *thread_main(void *u)
{
    handle *h = u;
    me_ui = 1;
    pthread_mutex_lock(&baton_m);
    while (!turn_ui) {
        pthread_cond_wait(&baton_c, &baton_m);
    }
    pthread_mutex_unlock(&baton_m);
    h->code = h->start(h->arg);
    LOG("thread end %u\n", (unsigned)h->code);
    pthread_mutex_lock(&baton_m);
    ui_running = 0;
    turn_ui = 0;
    pthread_cond_broadcast(&baton_c);
    pthread_mutex_unlock(&baton_m);
    return NULL;
}
HANDLE CreateThread(void *s, size_t st, LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD f, DWORD *id)
{
    (void)s; (void)st; (void)f; (void)id;
    LOG("CreateThread%s\n", opt.create_thread_fails ? " -> fails" : "");
    if (opt.create_thread_fails || ui_started) {
        return NULL;
    }
    HANDLE hh = new_handle(H_THREAD);
    handle *h = &handles[handle_index(hh)];
    h->start = start;
    h->arg = arg;
    ui_started = ui_running = 1;
    if (pthread_create(&h->tid, NULL, thread_main, h)) {
        FAIL("pthread_create");
    }
    return hh;
}
HANDLE CreateEventA(void *s, BOOL manual, BOOL initial, LPCSTR n)
{
    (void)s; (void)n;
    HANDLE h = new_handle(H_EVENT);
    handles[handle_index(h)].set = initial;
    LOG("CreateEvent(%d,%d)=%lx\n", manual, initial, (unsigned long)(uintptr_t)h);
    return h;
}
BOOL SetEvent(HANDLE h)
{
    int i = handle_index(h);
    LOG("SetEvent(%lx)\n", (unsigned long)(uintptr_t)h);
    if (i < 0 || handles[i].kind != H_EVENT) {
        FAIL("SetEvent on no event");
    }
    handles[i].set = 1;
    return TRUE;
}
BOOL CloseHandle(HANDLE h)
{
    int i = handle_index(h);
    LOG("CloseHandle(%lx)\n", (unsigned long)(uintptr_t)h);
    if (i < 0) {
        FAIL("CloseHandle of a closed or unknown handle");
    }
    if (handles[i].kind == H_THREAD) {
        if (ui_running) {
            FAIL("thread handle closed while the thread runs (not joined)");
        }
        pthread_join(handles[i].tid, NULL);
    }
    handles[i].open = 0;
    handles[i].kind = H_FREE;
    return TRUE;
}
static DWORD wait_any(DWORD n, const HANDLE *hs)
{
    for (unsigned spins = 0;; ++spins) {
        for (DWORD k = 0; k < n; ++k) {
            int i = handle_index(hs[k]);
            if (i < 0) {
                FAIL("wait on a closed handle");
            }
            if (signalled(i)) {
                return WAIT_OBJECT_0 + k;
            }
        }
        if (!ui_running || spins > 4) {
            FAIL("wait that nothing can end (hang)");
        }
        run_ui();
    }
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    LOG("WaitForSingleObject(%lx,%x)\n", (unsigned long)(uintptr_t)h, ms);
    DWORD r = wait_any(1, &h);
    LOG("  -> %u\n", r);
    return r;
}
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms)
{
    LOG("WaitForMultipleObjects(%u,%d,%x)\n", n, all, ms);
    DWORD r = wait_any(n, hs);
    LOG("  -> %u\n", r);
    return r;
}
DWORD GetThreadId(HANDLE h)
{
    LOG("GetThreadId\n");
    return handle_index(h) >= 0 ? 4242u : 0u;
}
/* Asynchronous user input delivered while the game thread sleeps (the script's `during` messages). */
typedef struct { UINT m; WPARAM w; LPARAM l; BYTE keys[256]; } async_msg;
static async_msg during[32];
static int during_n, during_i;
void Sleep(DWORD ms)
{
    if (me_ui) {
        FAIL("Sleep on the UI thread");
    }
    LOG("Sleep(%u)\n", ms);
    qpc += (int64_t)ms * 10000;
    if (++sleeps > 3000) {
        FAIL("too many sleeps (hang)");
    }
    if (during_i < during_n && window_ok(as_hwnd())) {
        async_msg *a = &during[during_i++];
        LOG("input %04x %lx %lx\n", a->m, (unsigned long)a->w, (unsigned long)a->l);
        push(a->m, a->w, a->l, a->keys, 0);
    } else if (sleeps % 64 == 0 && window_ok(as_hwnd())) {
        BYTE none[256] = {0};
        LOG("input (auto) key A\n");
        push(WM_KEYDOWN, 'A', 1, none, 0);
    }
    run_ui();
}
void InitializeCriticalSection(CRITICAL_SECTION *c)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&c->m, &a);
    c->depth = 0;
    c->owner = -1;
    LOG("InitializeCriticalSection\n");
}
void DeleteCriticalSection(CRITICAL_SECTION *c)
{
    LOG("DeleteCriticalSection depth=%d\n", c->depth);
    if (c->depth) {
        FAIL("lock deleted while held");
    }
    pthread_mutex_destroy(&c->m);
}
void EnterCriticalSection(CRITICAL_SECTION *c)
{
    if (c->depth && c->owner != me_ui) {
        FAIL("lock held by the other thread at a hand-over (deadlock)");
    }
    pthread_mutex_lock(&c->m);
    c->owner = me_ui;
    ++c->depth;
    LOG("lock %d\n", c->depth);
}
void LeaveCriticalSection(CRITICAL_SECTION *c)
{
    if (!c->depth || c->owner != me_ui) {
        FAIL("unlock of a lock not held");
    }
    LOG("unlock %d\n", c->depth);
    --c->depth;
    pthread_mutex_unlock(&c->m);
}
LONG InterlockedExchange(volatile LONG *t, LONG v)
{
    LONG old = *t;
    *t = v;
    LOG("InterlockedExchange(%d)=%d\n", v, old);
    return old;
}
BOOL QueryPerformanceCounter(LARGE_INTEGER *c)
{
    qpc += 1000;
    c->QuadPart = qpc;
    return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f)
{
    f->QuadPart = 10000000;
    return TRUE;
}
HMODULE GetModuleHandleA(LPCSTR n)
{
    (void)n;
    return (HMODULE)(uintptr_t)0x400000;
}
FARPROC GetProcAddress(HMODULE m, LPCSTR n)
{
    (void)m;
    (void)n;
    return NULL;
}
int MultiByteToWideChar(UINT cp, DWORD f, LPCSTR s, int n, LPWSTR out, int size)
{
    int k = 0;
    (void)cp; (void)f; (void)n;
    for (; s[k] && k < size - 1; ++k) {
        out[k] = (wchar_t)(unsigned char)s[k];
    }
    if (s[k]) {
        LOG("MultiByteToWideChar overflow\n");
        return 0;
    }
    out[k] = 0;
    LOG("MultiByteToWideChar(%s)=%d\n", s, k + 1);
    return k + 1;
}
UINT timeBeginPeriod(UINT p) { LOG("timeBeginPeriod(%u)\n", p); return 0; }
UINT timeEndPeriod(UINT p) { LOG("timeEndPeriod(%u)\n", p); return 0; }

/* ---- windows and messages -------------------------------------------------------------------------------- */
ATOM RegisterClassExA(const WNDCLASSEXA *c)
{
    LOG("RegisterClassEx(%s, icon %lx/%lx, cursor %lx)\n", c->lpszClassName, (unsigned long)(uintptr_t)c->hIcon,
        (unsigned long)(uintptr_t)c->hIconSm, (unsigned long)(uintptr_t)c->hCursor);
    wndproc = c->lpfnWndProc;
    class_icon = (ULONG_PTR)c->hIcon;
    class_icon_small = (ULONG_PTR)c->hIconSm;
    return 0xC001;
}
HWND CreateWindowA(LPCSTR cls, LPCSTR title, LONG style, int x, int y, int w, int h, HWND parent, HMENU menu,
                   HINSTANCE inst, LPVOID param)
{
    CREATESTRUCTA cs;
    (void)parent; (void)menu;
    LOG("CreateWindow(%s,%s,%lx,%d,%d,%d,%d)%s\n", cls, title, (unsigned long)style, x, y, w, h,
        opt.create_window_fails ? " -> fails" : "");
    if (opt.create_window_fails) {
        return NULL;
    }
    memset(&the_window, 0, sizeof the_window);
    the_window.alive = 1;
    the_window.style = style;
    snprintf(the_window.title, sizeof the_window.title, "%s", title);
    the_window.client = (RECT){0, 0, w - 16, h - 39};
    memset(&cs, 0, sizeof cs);
    cs.lpCreateParams = param;
    cs.hInstance = inst;
    cs.cx = w;
    cs.cy = h;
    cs.style = style;
    cs.lpszName = title;
    cs.lpszClass = cls;
    (void)call_proc(0x0024u /* WM_GETMINMAXINFO: before WM_NCCREATE, no platform yet */, 0, 0);
    if (!call_proc(WM_NCCREATE, 0, (LPARAM)&cs)) {
        the_window.alive = 0;
        return NULL;
    }
    (void)call_proc(WM_CREATE, 0, (LPARAM)&cs);
    (void)call_proc(WM_SIZE, 0, (LPARAM)((the_window.client.bottom << 16) | (the_window.client.right & 0xffff)));
    return as_hwnd();
}
BOOL DestroyWindow(HWND h)
{
    LOG("DestroyWindow\n");
    if (!me_ui) {
        FAIL("DestroyWindow off the window's thread");
    }
    if (!window_ok(h)) {
        return FALSE;
    }
    (void)call_proc(WM_DESTROY, 0, 0);
    (void)call_proc(WM_NCDESTROY, 0, 0);
    the_window.alive = 0;
    if (timer_on) {
        LOG("timer ended with the window\n");
        timer_on = 0;
    }
    return TRUE;
}
BOOL ShowWindow(HWND h, int how)
{
    LOG("ShowWindow(%d)\n", how);
    if (!window_ok(h)) {
        return FALSE;
    }
    the_window.shown = 1;
    (void)call_proc(WM_SHOWWINDOW, 1, 0);
    (void)call_proc(WM_ACTIVATE, 1, 0);
    (void)call_proc(WM_SETFOCUS, 0, 0);
    the_window.invalid = 1;
    return FALSE;
}
BOOL AdjustWindowRect(RECT *r, LONG style, BOOL menu)
{
    LOG("AdjustWindowRect(%ld,%ld,%ld,%ld,%lx,%d)\n", (long)r->left, (long)r->top, (long)r->right,
        (long)r->bottom, (unsigned long)style, menu);
    r->left -= 8;
    r->top -= 31;
    r->right += 8;
    r->bottom += 8;
    return TRUE;
}
LRESULT DefWindowProcA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LOG("DefWindowProc(%04x,%lx,%lx)\n", m, (unsigned long)w, lp_of(m, l));
    if (!window_ok(h) && m != WM_NCCREATE) {
        return 0;
    }
    switch (m) {
        case WM_NCCREATE:
            return TRUE;
        case WM_SETICON: {
            HICON old = the_window.icon[w % 3];
            the_window.icon[w % 3] = (HICON)l;
            return (LRESULT)old;
        }
        case WM_GETICON:
            return (LRESULT)the_window.icon[w % 3];
        case WM_SYSKEYDOWN:
            if (w == VK_F4 && (l & (1L << 29))) {
                LOG("  Alt+F4 -> WM_CLOSE\n");
                (void)call_proc(WM_CLOSE, 0, 0);
            }
            return 0;
        default:
            return 0;
    }
}
LRESULT SendMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LOG("SendMessage(%04x,%lx,%lx)\n", m, (unsigned long)w, lp_of(m, l));
    if (!me_ui) {
        FAIL("SendMessage from the game thread");
    }
    if (!window_ok(h)) {
        return 0;
    }
    return call_proc(m, w, l);
}
BOOL PostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LOG("PostMessage(%04x,%lx,%lx)%s\n", m, (unsigned long)w, lp_of(m, l), window_ok(h) ? "" : " -> no window");
    if (!window_ok(h)) {
        return FALSE;
    }
    push(m, w, l, NULL, 0);
    return TRUE;
}
BOOL PostThreadMessageA(DWORD id, UINT m, WPARAM w, LPARAM l)
{
    LOG("PostThreadMessage(%u,%04x)\n", id, m);
    (void)w; (void)l;
    if (!ui_running) {
        return FALSE;
    }
    push(m, 0, 0, NULL, m == WM_QUIT);
    return TRUE;
}
void PostQuitMessage(int code)
{
    LOG("PostQuitMessage(%d)\n", code);
    quit_posted = 1;
}
BOOL GetMessageA(MSG *msg, HWND h, UINT a, UINT b)
{
    (void)h; (void)a; (void)b;
    for (;;) {
        if (qhead != qtail) {
            queued *q = &msgq[qhead++ % 64];
            if (q->quit) {
                LOG("GetMessage -> WM_QUIT (thread message)\n");
                return FALSE;
            }
            memcpy(keys_now, q->keys, 256);
            msg->hwnd = as_hwnd();
            msg->message = q->message;
            msg->wParam = q->w;
            msg->lParam = q->l;
            LOG("GetMessage -> %04x %lx %lx\n", q->message, (unsigned long)q->w, lp_of(q->message, q->l));
            return TRUE;
        }
        if (quit_posted) {
            quit_posted = 0;
            LOG("GetMessage -> WM_QUIT\n");
            return FALSE;
        }
        if (window_ok(as_hwnd()) && the_window.invalid) {
            msg->hwnd = as_hwnd();
            msg->message = WM_PAINT;
            msg->wParam = 0;
            msg->lParam = 0;
            LOG("GetMessage -> WM_PAINT\n");
            return TRUE;
        }
        ui_idle();
    }
}
BOOL TranslateMessage(const MSG *m)
{
    (void)m;
    return FALSE;
}
LRESULT DispatchMessageA(const MSG *m) { return call_proc(m->message, m->wParam, m->lParam); }
LONG_PTR GetWindowLongPtrA(HWND h, int i)
{
    if (!window_ok(h) && !(h == &the_window && i == GWLP_USERDATA)) {
        return 0;
    }
    return i == GWLP_USERDATA ? the_window.userdata : i == GWL_STYLE ? the_window.style : 0;
}
LONG_PTR SetWindowLongPtrA(HWND h, int i, LONG_PTR v)
{
    LONG_PTR old = GetWindowLongPtrA(h, i);
    LOG("SetWindowLongPtr(%d,%lx)\n", i, i == GWLP_USERDATA ? (unsigned long)(v != 0) : (unsigned long)v);
    if (i == GWLP_USERDATA) {
        the_window.userdata = v;
    } else if (i == GWL_STYLE) {
        the_window.style = v;
    }
    return old;
}
ULONG_PTR GetClassLongPtrA(HWND h, int i)
{
    (void)h;
    return i == GCLP_HICON ? class_icon : i == GCLP_HICONSM ? class_icon_small : 0;
}
ULONG_PTR SetClassLongPtrA(HWND h, int i, LONG_PTR v)
{
    ULONG_PTR old = GetClassLongPtrA(h, i);
    LOG("SetClassLongPtr(%d,%lx)\n", i, (unsigned long)v);
    if (i == GCLP_HICON) {
        class_icon = (ULONG_PTR)v;
    } else if (i == GCLP_HICONSM) {
        class_icon_small = (ULONG_PTR)v;
    }
    return old;
}
BOOL SetWindowTextA(HWND h, LPCSTR t)
{
    LOG("SetWindowText(%s)\n", t);
    if (!me_ui) {
        FAIL("SetWindowText from the game thread");
    }
    if (window_ok(h)) {
        snprintf(the_window.title, sizeof the_window.title, "%s", t);
    }
    return TRUE;
}
BOOL SetWindowPos(HWND h, HWND after, int x, int y, int w, int hh, UINT f)
{
    LOG("SetWindowPos(%lx,%d,%d,%d,%d,%x)\n", (unsigned long)(uintptr_t)after, x, y, w, hh, f);
    if (!window_ok(h)) {
        return FALSE;
    }
    if (!(f & SWP_NOSIZE)) {
        int popup = (the_window.style & WS_POPUP) != 0;
        int cw = popup ? w : w - 16, ch = popup ? hh : hh - 39;
        the_window.client = (RECT){0, 0, cw, ch};
        if (me_ui) {
            (void)call_proc(WM_SIZE, 0, (LPARAM)((ch << 16) | (cw & 0xffff)));
        } else { /* a cross-thread SetWindowPos: the owner thread gets the sent WM_SIZE; the caller waits */
            LOG("  (sent to the window's thread)\n");
            push(WM_SIZE, 0, (LPARAM)((ch << 16) | (cw & 0xffff)), NULL, 0);
            run_ui();
        }
    }
    return TRUE;
}
BOOL GetWindowPlacement(HWND h, WINDOWPLACEMENT *p)
{
    LOG("GetWindowPlacement(len %u)%s\n", p->length, opt.placement_fails ? " -> fails" : "");
    if (opt.placement_fails || !window_ok(h)) {
        return FALSE;
    }
    p->flags = 0;
    p->showCmd = 1;
    p->rcNormalPosition = (RECT){100, 50, 100 + the_window.client.right + 16, 50 + the_window.client.bottom + 39};
    return TRUE;
}
BOOL SetWindowPlacement(HWND h, const WINDOWPLACEMENT *p)
{
    LOG("SetWindowPlacement(%ld,%ld,%ld,%ld)\n", (long)p->rcNormalPosition.left, (long)p->rcNormalPosition.top,
        (long)p->rcNormalPosition.right, (long)p->rcNormalPosition.bottom);
    if (!window_ok(h)) {
        return FALSE;
    }
    {
        int cw = p->rcNormalPosition.right - p->rcNormalPosition.left - 16,
            ch = p->rcNormalPosition.bottom - p->rcNormalPosition.top - 39;
        the_window.client = (RECT){0, 0, cw, ch};
        (void)call_proc(WM_SIZE, 0, (LPARAM)((ch << 16) | (cw & 0xffff)));
    }
    return TRUE;
}
HMONITOR MonitorFromWindow(HWND h, DWORD f)
{
    (void)h;
    LOG("MonitorFromWindow(%u)\n", f);
    return (HMONITOR)(uintptr_t)0x3000;
}
BOOL GetMonitorInfoA(HMONITOR m, MONITORINFO *mi)
{
    LOG("GetMonitorInfo(size %u)%s\n", mi->cbSize, opt.monitor_fails ? " -> fails" : "");
    (void)m;
    if (opt.monitor_fails) {
        return FALSE;
    }
    mi->rcMonitor = (RECT){0, 0, 1920, 1080};
    mi->rcWork = (RECT){0, 0, 1920, 1040};
    return TRUE;
}
BOOL InvalidateRect(HWND h, const RECT *r, BOOL erase)
{
    LOG("InvalidateRect(%s,%d)\n", r ? "rect" : "all", erase);
    if (!window_ok(h)) {
        return FALSE;
    }
    the_window.invalid = 1;
    return TRUE;
}
HDC BeginPaint(HWND h, PAINTSTRUCT *ps)
{
    LOG("BeginPaint\n");
    if (!me_ui || painting) {
        FAIL("BeginPaint off the window's thread or nested");
    }
    painting = 1;
    the_window.invalid = 0;
    ps->hdc = (HDC)(uintptr_t)0x5000;
    (void)h;
    return ps->hdc;
}
BOOL EndPaint(HWND h, const PAINTSTRUCT *ps)
{
    (void)h; (void)ps;
    LOG("EndPaint\n");
    if (!painting) {
        FAIL("EndPaint without BeginPaint");
    }
    painting = 0;
    return TRUE;
}
BOOL ScreenToClient(HWND h, POINT *pt)
{
    (void)h;
    LOG("ScreenToClient(%ld,%ld)\n", (long)pt->x, (long)pt->y);
    pt->x -= 108;
    pt->y -= 81;
    return TRUE;
}
uintptr_t SetTimer(HWND h, uintptr_t id, UINT ms, void *f)
{
    (void)h; (void)f;
    LOG("SetTimer(%lu,%u)\n", (unsigned long)id, ms);
    if (timer_on) {
        FAIL("timer set twice");
    }
    timer_on = 1;
    return id;
}
BOOL KillTimer(HWND h, uintptr_t id)
{
    (void)h;
    LOG("KillTimer(%lu)\n", (unsigned long)id);
    if (!timer_on) {
        FAIL("KillTimer of no timer");
    }
    timer_on = 0;
    return TRUE;
}
HCURSOR LoadCursorA(HINSTANCE i, LPCSTR n)
{
    (void)i;
    LOG("LoadCursor(%lu)\n", (unsigned long)(uintptr_t)n);
    return (HCURSOR)(uintptr_t)0xA770;
}
HCURSOR SetCursor(HCURSOR c)
{
    LOG("SetCursor(%lx)\n", (unsigned long)(uintptr_t)c);
    return NULL;
}
static int fatal_shown;
static void narrow(char *out, size_t n, const wchar_t *w)
{
    size_t k = 0;
    for (; w[k] && k + 1 < n; ++k) {
        out[k] = (char)w[k];
    }
    out[k] = 0;
}
int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags)
{
    char a[160], b[160];
    narrow(a, sizeof a, title);
    narrow(b, sizeof b, text);
    LOG("MessageBox(owner %d, \"%s\", \"%s\", %x)\n", owner != NULL, a, b, flags);
    fatal_shown += owner != NULL;
    if (owner && !me_ui) {
        FAIL("owned MessageBox off the owner's thread");
    }
    return 1;
}
int GetSystemMetrics(int i) { return i == SM_CXICON ? 32 : 16; }
/* Phase 335: no work area (the scale is kept, the transcript unchanged) */
BOOL SystemParametersInfoA(UINT a, UINT b, void *c, UINT d) { (void)a; (void)b; (void)c; (void)d; return FALSE; }
static int images;
HANDLE LoadImageA(HINSTANCE i, LPCSTR n, UINT t, int w, int h, UINT f)
{
    (void)i; (void)t; (void)f;
    LOG("LoadImage(%lu,%d,%d)%s\n", (unsigned long)(uintptr_t)n, w, h, opt.load_image_fails ? " -> none" : "");
    if (opt.load_image_fails) {
        return NULL;
    }
    ++live_icons;
    return (HANDLE)(uintptr_t)(0x7000 + ++images);
}
BOOL DestroyIcon(HICON i)
{
    LOG("DestroyIcon(%lx)\n", (unsigned long)(uintptr_t)i);
    if (live_icons <= 0) {
        FAIL("DestroyIcon of no live icon");
    }
    --live_icons;
    return TRUE;
}
HICON CreateIconIndirect(ICONINFO *ii)
{
    LOG("CreateIconIndirect(%d, mask %d, color %d)\n", ii->fIcon, ii->hbmMask != NULL, ii->hbmColor != NULL);
    ++live_icons;
    return (HICON)(uintptr_t)(0x7800 + ++images);
}

/* ---- input ----------------------------------------------------------------------------------------------- */
SHORT GetKeyState(int vk)
{
    if (!me_ui) {
        FAIL("GetKeyState from the game thread");
    }
    return (SHORT)((keys_now[vk & 255] & 0x80) ? 0x8000 : 0);
}
SHORT GetAsyncKeyState(int vk)
{
    SHORT r = (SHORT)(((vk == VK_LBUTTON && (async_buttons & 1)) || (vk == VK_RBUTTON && (async_buttons & 2))) ? 0x8000 : 0);
    LOG("GetAsyncKeyState(%d)=%x\n", vk, (unsigned)(uint16_t)r);
    return r;
}
BOOL GetKeyboardState(BYTE *s)
{
    memcpy(s, keys_now, 256);
    return TRUE;
}
/* A US layout (VK_OEM_7 a dead key when the case says so). */
static unsigned plain_char(unsigned vk)
{
    static const struct { unsigned vk, c; } t[] = {
        {VK_OEM_1, ';'}, {VK_OEM_PLUS, '='}, {VK_OEM_COMMA, ','}, {VK_OEM_MINUS, '-'}, {0xBE, '.'},
        {VK_OEM_2, '/'}, {0xC0, '`'}, {0xDB, '['}, {0xDC, '\\'}, {0xDD, ']'}, {VK_OEM_7, '\''},
        {VK_SPACE, ' '}, {VK_RETURN, '\r'}, {VK_BACK, 8}, {VK_TAB, 9}, {VK_ESCAPE, 27}, {VK_ADD, '+'},
        {0x6A, '*'}, {0x6D, '-'}, {0x6E, '.'}, {0x6F, '/'}};
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return vk;
    }
    if (vk >= 0x60 && vk <= 0x69) {
        return '0' + vk - 0x60;
    }
    for (size_t i = 0; i < sizeof t / sizeof t[0]; ++i) {
        if (t[i].vk == vk) {
            return t[i].c;
        }
    }
    return 0;
}
UINT MapVirtualKeyW(UINT vk, UINT type)
{
    unsigned c = plain_char(vk);
    UINT r = (opt.dead_keys && vk == VK_OEM_7) ? (0x80000000u | c) : c;
    (void)type;
    return r;
}
int ToUnicodeEx(UINT vk, UINT scan, const BYTE *state, LPWSTR out, int n, UINT flags, HKL kl)
{
    static const char from[] = "1234567890-=[];'`\\,./", to[] = "!@#$%^&*()_+{}:\"~|<>?";
    unsigned c = plain_char(vk);
    const char *k;
    (void)scan; (void)n; (void)kl;
    LOG("ToUnicodeEx(%02x, shift %d, flags %u)\n", vk, (state[VK_SHIFT] & 0x80) != 0, flags);
    if (opt.dead_keys && vk == VK_OEM_7) {
        return -1;
    }
    if (!c) {
        return 0;
    }
    if ((state[VK_SHIFT] & 0x80) && c < 128 && (k = strchr(from, (int)c)) != NULL && c) {
        c = (unsigned char)to[k - from];
    }
    out[0] = (wchar_t)c;
    return 1;
}
HKL GetKeyboardLayout(DWORD t)
{
    (void)t;
    return (HKL)(uintptr_t)0x4090409;
}

/* ---- GDI --------------------------------------------------------------------------------------------------- */
HDC GetDC(HWND h)
{
    LOG("GetDC(%s)\n", h ? "window" : "screen");
    if (h && !me_ui) {
        FAIL("GetDC of the window off its thread");
    }
    ++live_dc;
    return (HDC)(uintptr_t)(h ? 0x5100 : 0x5200);
}
int ReleaseDC(HWND h, HDC dc)
{
    (void)h;
    LOG("ReleaseDC(%lx)\n", (unsigned long)(uintptr_t)dc);
    if (live_dc <= 0) {
        FAIL("ReleaseDC of no DC");
    }
    --live_dc;
    return 1;
}
HGDIOBJ GetStockObject(int i)
{
    return (HGDIOBJ)(uintptr_t)(0x6000 + i);
}
int FillRect(HDC dc, const RECT *r, HBRUSH b)
{
    (void)dc;
    LOG("FillRect(%ld,%ld,%ld,%ld,%lx)\n", (long)r->left, (long)r->top, (long)r->right, (long)r->bottom,
        (unsigned long)(uintptr_t)b);
    return 1;
}
int SetStretchBltMode(HDC dc, int m)
{
    (void)dc;
    LOG("SetStretchBltMode(%d)\n", m);
    return 1;
}
int StretchDIBits(HDC dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void *bits,
                  const BITMAPINFO *bi, UINT u, DWORD rop)
{
    (void)dc;
    LOG("StretchDIBits(%d,%d,%d,%d <- %d,%d,%d,%d, %ldx%ld/%u, %u, %x, %016llx)\n", x, y, w, h, sx, sy, sw, sh,
        (long)bi->bmiHeader.biWidth, (long)bi->bmiHeader.biHeight, bi->bmiHeader.biBitCount, u, rop,
        (unsigned long long)fnv(bits, (size_t)sw * (size_t)sh * 4u));
    return sh;
}
static void *dib_bits[8];
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT u, void **bits, HANDLE s, DWORD o)
{
    (void)dc; (void)u; (void)s; (void)o;
    LOG("CreateDIBSection(%ldx%ld)%s\n", (long)bi->bmiHeader.biWidth, (long)bi->bmiHeader.biHeight,
        opt.dib_fails ? " -> fails" : "");
    if (opt.dib_fails) {
        *bits = NULL;
        return NULL;
    }
    for (int i = 0; i < 8; ++i) {
        if (!dib_bits[i]) {
            dib_bits[i] = calloc((size_t)bi->bmiHeader.biWidth * (size_t)-bi->bmiHeader.biHeight, 4);
            *bits = dib_bits[i];
            ++live_gdi;
            return (HBITMAP)(uintptr_t)(0x6100 + i);
        }
    }
    FAIL("out of DIBs");
    return NULL;
}
HBITMAP CreateBitmap(int w, int h, UINT planes, UINT bits, const void *data)
{
    (void)data;
    LOG("CreateBitmap(%d,%d,%u,%u)\n", w, h, planes, bits);
    ++live_gdi;
    return (HBITMAP)(uintptr_t)0x6200;
}
BOOL DeleteObject(HGDIOBJ o)
{
    uintptr_t v = (uintptr_t)o;
    LOG("DeleteObject(%lx)\n", (unsigned long)v);
    if (live_gdi <= 0) {
        FAIL("DeleteObject of no object");
    }
    if (v >= 0x6100 && v < 0x6108) {
        free(dib_bits[v - 0x6100]);
        dib_bits[v - 0x6100] = NULL;
    }
    --live_gdi;
    return TRUE;
}

/* ---- the driver ---------------------------------------------------------------------------------------------- */
static xeen_platform *P;
static uint8_t frame[64000], palette[768];
static void boss_hook(void *u, int on)
{
    (void)u;
    LOG("boss_hook(%d)\n", on);
}
static int menu_query(void *u)
{
    (void)u;
    LOG("menu_query -> %d\n", opt.menu_answer);
    return opt.menu_answer;
}
/* The platform's fields (read on the game thread while the UI thread waits for the baton). */
/* The queued presses by field (a sample's padding is indeterminate after a struct copy: Phase 283). */
static uint64_t mice_digest(const xeen_input_queue *q)
{
    uint16_t v[4 * 64];
    size_t n = 0;
    for (size_t i = 0; i < q->mouse_count && n + 4 <= sizeof v / sizeof v[0]; ++i) {
        v[n++] = q->mice[i].x;
        v[n++] = q->mice[i].y;
        v[n++] = q->mice[i].click;
        v[n++] = q->mice[i].enabled;
    }
    return fnv(v, n * sizeof v[0]);
}
static void state(const char *why)
{
    xeen_platform *p = P;
    const xeen_input_queue *q = &p->queue;
    LOG("state %s: closing %d size %ux%u aspect %d frame %d boss %d swallow %d boss %lu/%lu/%lu mq %d/%lu "
        "ctrl %d full %d modal %u dirty %d paints %lu crep %lu held %u wheel %d/%d/%d@%u,%u title \"%s\"/\"%s\" "
        "window %d ui_ok %d keys %u:%016llx mice %u:%016llx cur %u,%u,%u,%u painted %d,%d hidden %d paused %llu "
        "style %lx placement %016llx shown %016llx\n",
        why, p->closing, p->width, p->height, p->aspect, p->have_frame, p->boss, p->swallow, p->boss_entries,
        p->boss_exits, p->boss_consumed, p->boss_menu_quit, p->menu_quits, p->ctrl, p->fullscreen, p->modal,
        p->dirty, p->paints, p->cursor_repaints, p->buttons_held, p->wheel_on, p->wheel, p->wheel_delta,
        p->wheel_x, p->wheel_y, p->title_now, p->title_saved, p->window != NULL, p->ui_ok, (unsigned)q->key_count,
        (unsigned long long)fnv(q->keys, sizeof q->keys), (unsigned)q->mouse_count,
        (unsigned long long)mice_digest(q), q->current_mouse.x, q->current_mouse.y,
        q->current_mouse.click, q->current_mouse.enabled, p->painted_x, p->painted_y, p->cursor_hidden,
        (unsigned long long)p->paused_ns, (unsigned long)p->style,
        (unsigned long long)fnv(&p->placement, sizeof p->placement), (unsigned long long)fnv(p->shown, 64000));
}
static void settle(void) { run_ui(); }
/* A keyboard state: random modifiers (Shift, Ctrl, Alt, Win) at a key message. */
static void random_keys(BYTE *k, int *alt)
{
    memset(k, 0, 256);
    if (below(4) == 0) {
        k[VK_SHIFT] = 0x80;
    }
    if (below(4) == 0) {
        k[VK_CONTROL] = 0x80;
    }
    if (below(6) == 0) {
        k[VK_MENU] = 0x80;
    }
    if (below(10) == 0) {
        k[VK_LWIN] = 0x80;
    }
    *alt = (k[VK_MENU] & 0x80) != 0;
}
static const unsigned VKS[] = {'A', 'Q', 'C', 'Z', '1', '5', '0', VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS,
                               VK_OEM_2, VK_OEM_7, VK_SPACE, VK_RETURN, VK_ESCAPE, VK_F1, VK_F4, VK_F9, VK_F10,
                               VK_LEFT, VK_UP, VK_HOME, VK_NUMPAD5, VK_ADD, VK_BACK, VK_TAB, VK_SHIFT, VK_CONTROL,
                               VK_MENU, VK_LWIN, VK_CAPITAL, VK_LSHIFT, VK_RCONTROL, VK_LMENU, VK_NUMLOCK, 0x07,
                               VK_RSHIFT, VK_LCONTROL, VK_RWIN, VK_SCROLL, VK_RMENU};
#define NVKS (sizeof VKS / sizeof VKS[0])
/* One random key message (with its keyboard state); the vk is biased towards the interesting keys. */
static void key_message(UINT *m, WPARAM *w, LPARAM *l, BYTE *k)
{
    int alt, repeat = below(6) == 0, up = below(3) == 0;
    unsigned vk = below(5) == 0 ? (below(2) ? VK_F10 : 'Q') : VKS[below(NVKS)];
    random_keys(k, &alt);
    if (vk == VK_CONTROL || vk == VK_RCONTROL) {
        k[VK_CONTROL] = up ? 0 : 0x80;
    }
    if (below(8) == 0) { /* Alt+Enter, Alt+F4 */
        vk = below(2) ? VK_RETURN : VK_F4;
        alt = 1;
        k[VK_MENU] = 0x80;
    }
    *w = vk;
    *l = 1 | (LPARAM)(below(0x80) << 16) | (alt ? (1L << 29) : 0) | (repeat || up ? (1L << 30) : 0) |
         (up ? (1L << 31) : 0);
    *m = alt || vk == VK_F10 ? (up ? WM_SYSKEYUP : WM_SYSKEYDOWN) : (up ? WM_KEYUP : WM_KEYDOWN);
    if (below(10) == 0) { /* a system message for a plain key, a plain one for Alt */
        *m = *m == WM_KEYDOWN ? WM_SYSKEYDOWN : *m == WM_SYSKEYDOWN ? WM_KEYDOWN : *m;
    }
}
static LPARAM point(int x, int y) { return (LPARAM)(((uint32_t)(uint16_t)y << 16) | (uint16_t)x); }
static void random_message(UINT *m, WPARAM *w, LPARAM *l, BYTE *k)
{
    unsigned width = P->width ? P->width : 320, height = P->height ? P->height : 200;
    int x = (int)below(width + 40) - 20, y = (int)below(height + 40) - 20;
    memset(k, 0, 256);
    *w = 0;
    *l = 0;
    switch (below(24)) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            key_message(m, w, l, k);
            return;
        case 8: case 9:
            *m = WM_MOUSEMOVE;
            *l = point(x, y);
            return;
        case 10:
            *m = below(2) ? WM_LBUTTONDOWN : WM_RBUTTONDOWN;
            *l = point(x, y);
            return;
        case 11:
            *m = below(2) ? WM_LBUTTONUP : WM_RBUTTONUP;
            *l = point(x, y);
            return;
        case 12:
            *m = WM_MOUSEWHEEL;
            *w = (WPARAM)((uint32_t)(uint16_t)(short)((int)below(7) * 60 - 180) << 16);
            *l = point(x + 108, y + 81);
            return;
        case 13:
            *m = WM_KILLFOCUS;
            return;
        case 14: {
            unsigned cw = 160 + below(1000), ch = below(8) ? 100 + below(800) : 0;
            *m = WM_SIZE;
            *l = (LPARAM)((ch << 16) | cw);
            return;
        }
        case 15:
            *m = WM_SETCURSOR;
            *l = (LPARAM)(below(3) ? HTCLIENT : HTCAPTION);
            return;
        case 16:
            *m = below(2) ? WM_ERASEBKGND : WM_SYSCHAR;
            *w = below(2) ? '\r' : 'x';
            return;
        case 17:
            *m = WM_TIMER;
            *w = below(3) ? 132u : 7u;
            return;
        case 18:
            *m = below(2) ? WM_ENTERSIZEMOVE : WM_ENTERMENULOOP;
            return;
        case 19:
            *m = below(2) ? WM_EXITSIZEMOVE : WM_EXITMENULOOP;
            return;
        case 20:
            *m = below(6) == 0 ? WM_CLOSE : WM_ACTIVATEAPP;
            return;
        case 21:
            *m = WM_PAINT;
            return;
        case 22:
            *m = WM_SETFOCUS;
            return;
        default:
            *m = below(2) ? WM_MBUTTONDOWN : WM_CHAR;
            *w = 'a';
            return;
    }
}
static void post_input(UINT m, WPARAM w, LPARAM l, const BYTE *k)
{
    LOG("input %04x %lx %lx\n", m, (unsigned long)w, (unsigned long)l);
    if (!window_ok(as_hwnd())) {
        LOG("  (no window)\n");
        return;
    }
    push(m, w, l, k, 0);
    settle();
}
static void random_frame(void)
{
    unsigned v = rnd();
    for (unsigned i = 0; i < 64000; ++i) {
        frame[i] = (uint8_t)((i * 7u + v) >> (v % 5));
    }
    for (unsigned i = 0; i < 768; ++i) {
        palette[i] = (uint8_t)((i + v) & 63u);
    }
}
static void queue_during(unsigned n)
{
    during_n = during_i = 0;
    for (unsigned i = 0; i < n && during_n < 32; ++i) {
        async_msg *a = &during[during_n++];
        random_message(&a->m, &a->w, &a->l, a->keys);
    }
}
static int sc55_passes, sc55_finish_at, sc55_progress;
static int stub_finished(void *u)
{
    (void)u;
    LOG("boot finished? pass %d -> %d\n", sc55_passes, sc55_passes >= sc55_finish_at);
    return sc55_passes++ >= sc55_finish_at;
}
static int stub_should_close(void *u) { return xeen_platform_should_close(u); }
static void stub_pump(void *u) { xeen_platform_pump(u); }
static void stub_wait(void *u, unsigned ms) { xeen_platform_wait(u, ms); }
static void stub_trace(void *u, const char *e)
{
    (void)u;
    LOG("startup %s\n", e);
}
static void stub_refresh(void *u)
{
    sc55_progress += 1000 / (sc55_finish_at + 1);
    host_startup_progress_bar(frame, (unsigned)sc55_progress);
    (void)xeen_platform_present(u, frame, palette);
}
/* The SC-55 start-up of explore.c's start_deferred_audio, with a stub boot: the message frame, the wait, then
 * either the close at once (CANCELLED) or the black frame and startup_done (READY). */
static int startup_case(void)
{
    host_startup_wait_ops ops = {P, stub_finished, stub_should_close, stub_pump, stub_wait, stub_trace, stub_refresh};
    memset(frame, 0, sizeof frame);
    memset(palette, 0, sizeof palette);
    palette[3] = palette[4] = palette[5] = 63;
    sc55_finish_at = (int)below(12);
    queue_during(below(10));
    if (below(2) && during_n < 32) { /* the window closed during the boot */
        int at = (int)below((unsigned)during_n + 1);
        memmove(&during[at + 1], &during[at], (size_t)(during_n - at) * sizeof during[0]);
        memset(&during[at], 0, sizeof during[at]);
        during[at].m = WM_CLOSE;
        ++during_n;
    }
    (void)xeen_platform_present(P, frame, palette);
    if (host_startup_wait(&ops) != HOST_STARTUP_READY) {
        LOG("startup cancelled\n");
        state("cancelled");
        xeen_platform_close(P);
        P = NULL;
        LOG("boot joined (stub)\n");
        return 0;
    }
    memset(frame, 0, sizeof frame);
    (void)xeen_platform_present(P, frame, palette);
    xeen_platform_startup_done(P);
    state("ready");
    return 1;
}
static void game_call(void)
{
    xeen_input_provider in = xeen_platform_input(P);
    uint16_t key, x, y;
    xeen_mouse_sample s;
    switch (below(22)) {
        case 0: case 1:
            LOG("call pump\n");
            queue_during(below(3));
            xeen_platform_pump(P);
            break;
        case 2: case 3:
            random_frame();
            LOG("call present=%d\n", xeen_platform_present(P, frame, palette));
            break;
        case 4:
            random_frame();
            LOG("call present_hold=%d\n", xeen_platform_present_hold(P, frame, palette, 0));
            break;
        case 5:
            LOG("call refresh=%d\n", xeen_platform_refresh(P, frame, palette));
            break;
        case 6:
            LOG("call set_fullscreen\n");
            xeen_platform_set_fullscreen(P, (int)below(2));
            break;
        case 7:
            LOG("call set_aspect\n");
            xeen_platform_set_aspect(P, (int)below(2));
            break;
        case 8:
            LOG("call set_wheel\n");
            xeen_platform_set_wheel(P, (int)below(3) != 0);
            break;
        case 9: {
            static uint8_t px[11 * 9], op[11 * 9];
            for (unsigned i = 0; i < sizeof px; ++i) {
                px[i] = (uint8_t)(i * 3u);
                op[i] = (uint8_t)((i % 3u) != 0);
            }
            LOG("call set_cursor\n");
            xeen_platform_set_cursor(P, px, op, 9, 11);
            xeen_platform_show_cursor(P, (int)below(4) != 0);
            break;
        }
        case 10:
            LOG("call set_pointer_limits\n");
            xeen_platform_set_pointer_limits(P, 9, 11);
            break;
        case 11: case 12: case 13: {
            int got;
            queue_during(below(2));
            got = in.keyboard(in.user, &key);
            LOG("call keyboard=%d key %04x\n", got, got ? key : 0);
            break;
        }
        case 14:
            memset(&s, 0, sizeof s);
            LOG("call mouse=%d", in.mouse(in.user, &s));
            printf(" %u,%u,%u,%u\n", s.x, s.y, s.click, s.enabled);
            break;
        case 15: {
            int d = xeen_platform_take_wheel(P, &x, &y);
            LOG("call take_wheel=%d %u,%u\n", d, d ? x : 0, d ? y : 0);
            break;
        }
        case 16: {
            int ok = xeen_platform_pointer(P, &x, &y);
            LOG("call pointer=%d %u,%u\n", ok, ok ? x : 0, ok ? y : 0);
            break;
        }
        case 17:
            async_buttons = (BYTE)below(4);
            LOG("call button_held=%d\n", xeen_platform_button_held(P));
            break;
        case 18:
            LOG("call should_close=%d\n", xeen_platform_should_close(P));
            break;
        case 19:
            LOG("call startup_done\n");
            xeen_platform_startup_done(P);
            break;
        case 20:
            if (below(4) == 0) {
                LOG("call fatal_dialog=%d\n",
                    xeen_platform_fatal_dialog(below(4) ? P : NULL, below(2) ? "Title" : NULL, "Text"));
            }
            break;
        default: {
            xeen_platform_boss_info info;
            unsigned ms = 1 + below(30);
            queue_during(below(3));
            LOG("call wait(%u)\n", ms);
            xeen_platform_wait(P, ms);
            xeen_platform_boss_info_get(P, &info);
            LOG("boss info %d %d %lu %lu %lu %s\n", info.active, info.icon, info.entries, info.exits, info.consumed, info.title);
            break;
        }
    }
}
static void tally(void)
{
    int threads = 0, events = 0;
    for (int i = 0; i < 16; ++i) {
        threads += handles[i].open && handles[i].kind == H_THREAD;
        events += handles[i].open && handles[i].kind == H_EVENT;
    }
    LOG("tally: window %d thread-running %d thread-handles %d events %d dc %d gdi %d icons %d timer %d painting %d "
        "queued %d\n", the_window.alive, ui_running, threads, events, live_dc, live_gdi, live_icons, timer_on,
        painting, qtail - qhead);
    check(!the_window.alive && !ui_running && !threads && !events && !live_dc && !live_gdi && !live_icons &&
                  !timer_on && !painting,
          "nothing is left at the end (window, thread, events, DCs, GDI objects, icons, timer)");
}
static void run_case(unsigned s)
{
    char error[160] = "";
    unsigned steps;
    seed = s * 2654435761u + 1u;
    memset(&opt, 0, sizeof opt);
    opt.load_image_fails = below(4) == 0;
    opt.create_window_fails = below(24) == 0;
    opt.create_thread_fails = below(48) == 0;
    opt.placement_fails = below(10) == 0;
    opt.monitor_fails = below(10) == 0;
    opt.dead_keys = below(3) == 0;
    opt.dib_fails = opt.load_image_fails && below(6) == 0;
    opt.menu_answer = below(3) == 0;
    if (below(5) == 0) {
        setenv("XEEN_WINDOW_LOG", "1", 1);
    }
    printf("case %u opts %d%d%d%d%d%d%d%d\n", s, opt.load_image_fails, opt.create_window_fails,
           opt.create_thread_fails, opt.placement_fails, opt.monitor_fails, opt.dead_keys, opt.dib_fails,
           opt.menu_answer);
    P = xeen_platform_open("World of Xeen", 1 + below(3), 0, error, sizeof error);
    if (!P) {
        LOG("open failed: %s\n", error);
        tally();
        return;
    }
    xeen_platform_set_menu_quit_query(P, menu_query, NULL);
    if (below(3)) {
        xeen_platform_set_boss_hook(P, boss_hook, NULL);
    }
    state("open");
    if (below(8) == 0 && !startup_case()) {
        tally();
        return;
    }
    steps = 8 + below(48);
    for (unsigned i = 0; i < steps; ++i) {
        if (below(3)) {
            UINT m;
            WPARAM w;
            LPARAM l;
            BYTE k[256];
            random_message(&m, &w, &l, k);
            post_input(m, w, l, k);
        } else {
            game_call();
        }
        state("step");
    }
    LOG("close\n");
    xeen_platform_close(P);
    P = NULL;
    tally();
}
/* ---- named scenarios (the behaviour capture of the report's message contract) ----------------------------- */
static void key(UINT m, unsigned vk, int repeat, unsigned mods)
{
    BYTE k[256] = {0};
    int alt = (mods & 4) != 0;
    LPARAM l = 1 | (LPARAM)(0x10 << 16) | (alt ? (1L << 29) : 0) |
               (repeat || m == WM_KEYUP || m == WM_SYSKEYUP ? (1L << 30) : 0) |
               (m == WM_KEYUP || m == WM_SYSKEYUP ? (1L << 31) : 0);
    if (mods & 1) k[VK_SHIFT] = 0x80;
    if (mods & 2) k[VK_CONTROL] = 0x80;
    if (mods & 4) k[VK_MENU] = 0x80;
    post_input(m, vk, l, k);
}
static void read_keys(void)
{
    xeen_input_provider in = xeen_platform_input(P);
    uint16_t k;
    int got;
    for (unsigned i = 0; i < 12; ++i) { /* the provider answers 1 with key 0 for an empty queue */
        k = 0xffff;
        got = in.keyboard(in.user, &k);
        LOG("key %d %04x\n", got, k);
        if (!got || !k) {
            break;
        }
    }
    LOG("keys done\n");
}
static void during_key(unsigned vk, unsigned mods)
{
    async_msg *a = &during[during_n++];
    memset(a, 0, sizeof *a);
    a->m = WM_KEYDOWN;
    a->w = vk;
    a->l = 1;
    if (mods & 2) a->keys[VK_CONTROL] = 0x80;
}
static int named_open(void)
{
    char error[160] = "";
    P = xeen_platform_open("World of Xeen", 2, 0, error, sizeof error);
    if (!P) {
        LOG("open failed: %s\n", error);
        return 0;
    }
    xeen_platform_set_menu_quit_query(P, menu_query, NULL);
    xeen_platform_set_boss_hook(P, boss_hook, NULL);
    random_frame();
    (void)xeen_platform_present(P, frame, palette);
    settle();
    state("open");
    return 1;
}
static void named_close(void)
{
    LOG("close\n");
    if (P) {
        xeen_platform_close(P);
    }
    P = NULL;
    tally();
}
static void run_named(unsigned n)
{
    BYTE none[256] = {0};
    seed = 77u + n;
    memset(&opt, 0, sizeof opt);
    during_n = during_i = 0;
    switch (n) {
        case 0:
            printf("named 0: lifecycle: open, paint, resize, focus loss, close request, close\n");
            if (!named_open()) break;
            post_input(WM_SIZE, 0, (LPARAM)((600 << 16) | 800), none);
            post_input(WM_ERASEBKGND, 0, 0, none);
            post_input(WM_KILLFOCUS, 0, 0, none);
            post_input(WM_CLOSE, 0, 0, none);
            LOG("should_close=%d\n", xeen_platform_should_close(P));
            state("closing");
            check(xeen_platform_should_close(P) && P->window != NULL, "WM_CLOSE: a close request, no destroy");
            check(P->width == 800 && P->height == 600, "WM_SIZE: the client size");
            named_close();
            break;
        case 1:
            printf("named 1: F10 shows the calculator; a key hides it and is swallowed with its repeat\n");
            if (!named_open()) break;
            key(WM_SYSKEYDOWN, VK_F10, 0, 0);
            key(WM_SYSKEYUP, VK_F10, 0, 0);
            state("boss");
            check(P->boss && !strcmp(P->title_now, "Calculator") && P->boss_entries == 1, "F10 shows the calculator");
            during_key('A', 0);
            xeen_platform_pump(P);
            state("hidden");
            check(!P->boss && P->swallow && !strcmp(P->title_now, "World of Xeen") && P->boss_exits == 1,
                  "a key hides the calculator (the game's title back) and is swallowed");
            key(WM_KEYDOWN, 'A', 1, 0);
            key(WM_KEYUP, 'A', 0, 0);
            key(WM_KEYDOWN, 'B', 0, 0);
            check(P->queue.key_count == 1 && P->queue.keys[0] == 'B', "the hiding key's repeat and release reach "
                                                                      "no poll; the next key does");
            read_keys();
            state("after");
            named_close();
            break;
        case 2:
            printf("named 2: Ctrl+Q: fresh chord, its repeat, then Ctrl+Q at the calculator outside the menu\n");
            if (!named_open()) break;
            key(WM_KEYDOWN, VK_CONTROL, 0, 2);
            key(WM_KEYDOWN, 'Q', 0, 2);
            key(WM_KEYDOWN, 'Q', 1, 2);
            check(P->queue.key_count == 1 && P->queue.keys[0] == (0x11u | QUIT_CHORD),
                  "a fresh Ctrl+Q is the quit chord; its repeat is not queued behind it");
            read_keys();
            check(!xeen_platform_should_close(P), "outside the menu the chord is the plain 11h");
            key(WM_SYSKEYDOWN, VK_F10, 0, 2);
            key(WM_KEYDOWN, 'Q', 0, 2);
            opt.menu_answer = 0;
            xeen_platform_pump(P);
            state("hidden by Ctrl+Q");
            check(!P->boss && P->swallow && !xeen_platform_should_close(P),
                  "Ctrl+Q at the calculator outside the menu hides it (no close)");
            key(WM_KEYUP, 'Q', 0, 2);
            read_keys();
            named_close();
            break;
        case 3:
            printf("named 3: Ctrl+Q at the calculator in a menu context closes the game\n");
            if (!named_open()) break;
            key(WM_SYSKEYDOWN, VK_F10, 0, 0);
            key(WM_KEYDOWN, 'Q', 0, 2);
            opt.menu_answer = 1;
            xeen_platform_pump(P);
            LOG("should_close=%d menu_quits=%lu\n", xeen_platform_should_close(P), xeen_platform_menu_quits(P));
            check(xeen_platform_should_close(P) && xeen_platform_menu_quits(P) == 1,
                  "Ctrl+Q at the calculator in a menu closes the game");
            named_close();
            break;
        case 4:
            printf("named 4: Ctrl+C is its plain code 03h (no quit chord); Shift+1 is '!'; Escape is the game's Esc\n");
            if (!named_open()) break;
            key(WM_KEYDOWN, 'C', 0, 2);
            key(WM_KEYDOWN, '1', 0, 1);
            key(WM_KEYDOWN, VK_OEM_PLUS, 0, 0);
            key(WM_KEYDOWN, VK_ESCAPE, 0, 0);
            check(P->queue.key_count == 4 && P->queue.keys[0] == 0x03 && P->queue.keys[1] == '!' &&
                          P->queue.keys[2] == '=' && P->queue.keys[3] == 0x1b,
                  "Ctrl+C is 03h (no quit chord), Shift+1 '!', '=' its own, Escape the game's Esc");
            read_keys();
            check(!xeen_platform_should_close(P), "Escape is no close request");
            LOG("should_close=%d\n", xeen_platform_should_close(P));
            named_close();
            break;
        case 5:
            printf("named 5: Alt+Enter toggles fullscreen (not its repeat); its WM_SYSCHAR is eaten\n");
            if (!named_open()) break;
            key(WM_SYSKEYDOWN, VK_RETURN, 0, 4);
            post_input(WM_SYSCHAR, '\r', 1L << 29, none);
            key(WM_SYSKEYDOWN, VK_RETURN, 1, 4);
            state("fullscreen");
            check(P->fullscreen && P->width == 1920 && P->queue.key_count == 0,
                  "Alt+Enter: fullscreen over the monitor, no game key, its repeat no toggle");
            key(WM_SYSKEYDOWN, VK_RETURN, 0, 4);
            state("windowed");
            check(!P->fullscreen && P->width == 640, "Alt+Enter again: the window's placement back");
            xeen_platform_set_fullscreen(P, 1);
            settle();
            xeen_platform_set_fullscreen(P, 1);
            settle();
            state("initial fullscreen request");
            check(P->fullscreen, "the [display] request toggles once, a repeated one not");
            named_close();
            break;
        case 6:
            printf("named 6: Alt+Tab and Alt+F4 stay the system's; focus loss clears Ctrl and the buttons\n");
            if (!named_open()) break;
            key(WM_KEYDOWN, VK_CONTROL, 0, 2);
            post_input(WM_LBUTTONDOWN, 0, point(100, 100), none);
            key(WM_SYSKEYDOWN, VK_MENU, 0, 4);
            key(WM_SYSKEYDOWN, VK_TAB, 0, 4);
            post_input(WM_KILLFOCUS, 0, 0, none);
            post_input(WM_ACTIVATEAPP, 0, 0, none);
            state("after Alt+Tab");
            check(!P->ctrl && !P->buttons_held, "focus loss forgets Ctrl and the held buttons");
            key(WM_SYSKEYDOWN, VK_F4, 0, 4);
            LOG("should_close=%d\n", xeen_platform_should_close(P));
            check(xeen_platform_should_close(P), "Alt+F4 stays the system's: its WM_CLOSE is the close request");
            named_close();
            break;
        case 7:
            printf("named 7: a modal move/size loop paints submitted frames from its 10 ms timer\n");
            if (!named_open()) break;
            post_input(WM_ENTERSIZEMOVE, 0, 0, none);
            post_input(WM_ENTERMENULOOP, 0, 0, none);
            random_frame();
            (void)xeen_platform_present(P, frame, palette);
            push(WM_TIMER, 132u, 0, none, 0);
            post_input(WM_TIMER, 7u, 0, none);
            post_input(WM_EXITMENULOOP, 0, 0, none);
            post_input(WM_EXITSIZEMOVE, 0, 0, none);
            post_input(WM_EXITSIZEMOVE, 0, 0, none);
            state("after modal");
            check(!P->modal && !timer_on && P->paints == 3 && !P->dirty,
                  "the modal loops' timer paints the submitted frame and ends with the last loop");
            named_close();
            break;
        case 8:
            printf("named 8: the fatal dialog on the window's thread, then a second one is refused\n");
            if (!named_open()) break;
            LOG("fatal=%d\n", xeen_platform_fatal_dialog(P, "World of Xeen - Error", "the game stopped"));
            LOG("fatal again=%d\n", xeen_platform_fatal_dialog(P, "x", "y"));
            check(fatal_shown == 1, "one fatal dialog, owned by the window, on its thread");
            named_close();
            break;
        case 9: case 10: case 11: {
            static const char *what[] = {"ready after 3 passes", "closed during the boot", "F10 during the boot"};
            printf("named %u: SC-55 start-up: %s\n", n, what[n - 9]);
            if (!named_open()) break;
            sc55_passes = 0;
            sc55_progress = 0;
            if (n == 10) {
                during[0].m = WM_PAINT;
                during[1].m = WM_CLOSE;
                during_n = 2;
            } else if (n == 11) {
                during[0].m = WM_SYSKEYDOWN;
                during[0].w = VK_F10;
                during[0].l = 1;
                during[1].m = WM_KEYDOWN;
                during[1].w = 'A';
                during[1].l = 1;
                during_n = 2;
            }
            {
                host_startup_wait_ops ops = {P, stub_finished, stub_should_close, stub_pump, stub_wait, stub_trace,
                                             stub_refresh};
                sc55_finish_at = 3;
                memset(frame, 0, sizeof frame);
                (void)xeen_platform_present(P, frame, palette);
                host_startup_outcome outcome = host_startup_wait(&ops);
                check((outcome == HOST_STARTUP_READY) == (n != 10), "the start-up's outcome (close only in case 10)");
                if (outcome != HOST_STARTUP_READY) {
                    LOG("startup cancelled\n");
                    xeen_platform_close(P);
                    P = NULL;
                    LOG("boot joined (stub)\n");
                    tally();
                    break;
                }
                xeen_platform_startup_done(P);
                state("ready");
                check(P->queue.key_count == 0 && sc55_passes == 4, "ready after the boot; no key queued meanwhile");
                check(n != 11 || (P->boss_entries == 1 && P->boss_exits == 1 && !P->boss),
                      "F10 during the boot shows and a key hides the calculator");
                read_keys();
            }
            named_close();
            break;
        }
        case 12:
            printf("named 12: the window cannot be created: open fails, nothing is left\n");
            opt.create_window_fails = 1;
            if (!named_open()) {
                tally();
                break;
            }
            check(0, "an open without a window fails");
            named_close();
            break;
        case 13:
            printf("named 13: close while the calculator shows (the hook is told nothing more)\n");
            if (!named_open()) break;
            key(WM_SYSKEYDOWN, VK_F10, 0, 0);
            named_close();
            break;
        case 14:
            printf("named 14: wheel notches, pointer, buttons, cursor repaint, set-cursor\n");
            if (!named_open()) break;
            xeen_platform_set_wheel(P, 1);
            post_input(WM_MOUSEWHEEL, (WPARAM)(240u << 16), point(300, 200), none);
            post_input(WM_MOUSEWHEEL, (WPARAM)((uint32_t)(uint16_t)(short)-60 << 16), point(300, 200), none);
            post_input(WM_MOUSEWHEEL, (WPARAM)((uint32_t)(uint16_t)(short)-60 << 16), point(300, 200), none);
            {
                static uint8_t px[11 * 9], op[11 * 9];
                memset(op, 1, sizeof op);
                xeen_platform_set_cursor(P, px, op, 9, 11);
                xeen_platform_set_pointer_limits(P, 9, 11);
            }
            post_input(WM_MOUSEMOVE, 0, point(320, 200), none);
            post_input(WM_RBUTTONDOWN, 0, point(321, 201), none);
            post_input(WM_SETCURSOR, 0, HTCLIENT, none);
            post_input(WM_SETCURSOR, 0, HTCAPTION, none);
            async_buttons = 0;
            LOG("button_held=%d\n", xeen_platform_button_held(P));
            state("mouse");
            check(P->wheel == -1 && P->wheel_delta == 0 && P->cursor_repaints == 1 && P->queue.mouse_count == 1 &&
                          !P->buttons_held,
                  "wheel notches of 120 (a direction change drops the pending ones), one pointer repaint, one press");
            named_close();
            break;
        case 15:
            printf("named 15: [display] locked-scale: the minimum track size while windowed, none in fullscreen or unlocked\n");
            if (!named_open()) break;
            {
                MINMAXINFO mm;
                memset(&mm, 0, sizeof mm);
                post_input(WM_GETMINMAXINFO, 0, (LPARAM)&mm, none);
                check(mm.ptMinTrackSize.x == 0 && mm.ptMinTrackSize.y == 0, "unlocked: the system's minimum");
                xeen_platform_set_locked_scale(P, 1);
                post_input(WM_GETMINMAXINFO, 0, (LPARAM)&mm, none);
                LOG("min track %ldx%ld\n", (long)mm.ptMinTrackSize.x, (long)mm.ptMinTrackSize.y);
                check(mm.ptMinTrackSize.x == 640 + 16 && mm.ptMinTrackSize.y == 400 + 39,
                      "locked at scale 2: the 640x400 client and its frame");
                xeen_platform_set_aspect(P, 1);
                post_input(WM_GETMINMAXINFO, 0, (LPARAM)&mm, none);
                check(mm.ptMinTrackSize.y == 480 + 39, "locked with the aspect: 640x480");
                memset(&mm, 0, sizeof mm);
                P->fullscreen = 1;
                post_input(WM_GETMINMAXINFO, 0, (LPARAM)&mm, none);
                P->fullscreen = 0;
                check(mm.ptMinTrackSize.x == 0, "fullscreen: no minimum (geometry fits a smaller monitor)");
            }
            named_close();
            break;
        default:
            break;
    }
}
/* Runs one case (a named scenario, or a random case) in its own process: the unit's fatal dialog and window log keep
 * process statics. Returns 1 when it ended cleanly (no FAIL, no failed check, nothing left). */
static int fork_case(int named, unsigned n)
{
    pid_t pid;
    int status = 0, ok;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        setvbuf(stdout, NULL, _IOLBF, 0); /* the unit's window log (stderr) joins the transcript in order */
        dup2(1, 2);
        alarm(20);
        if (named) {
            run_named(n);
        } else {
            run_case(n);
        }
        CHILD_EXIT(0);
    }
    waitpid(pid, &status, 0);
    ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    printf("%s %u end %s %d\n", named ? "named" : "case", n, WIFEXITED(status) ? "exit" : "signal",
           WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
    if (!ok && report_fd != 1) {
        dprintf(report_fd, "%s %u failed (status %d)\n", named ? "named" : "case", n, status);
    }
    return ok;
}
int main(int argc, char **argv)
{
    unsigned n = argc > 1 ? (unsigned)atoi(argv[1]) : 300, first = argc > 2 ? (unsigned)atoi(argv[2]) : 0;
    unsigned failed = 0;
    const char *t = getenv("XEEN_WIN32_TRANSCRIPT");
    if (!(t && *t && *t != '0')) { /* the transcript off: only the failures reach the test's output */
        report_fd = dup(1);
        if (report_fd < 0 || !freopen("/dev/null", "w", stdout)) {
            fprintf(stderr, "cannot redirect the transcript\n");
            return 1;
        }
    }
    for (unsigned k = 0; k < 16; ++k) {
        failed += !fork_case(1, k);
    }
    for (unsigned s = first; s < first + n; ++s) {
        failed += !fork_case(0, s);
    }
    dprintf(report_fd, "phase298 win32 window: %u named + %u random cases, %u failed\n", 15u, n, failed);
    return failed ? 1 : 0;
}
