/* SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Phase 298: the subset of <windows.h> that src/platform/platform.c's Win32 backend uses, declared for a Linux
 * build of the real unit (types, the real message / key / style values, prototypes). Every function is defined by
 * tests/test_phase298_win32_window.c, which logs each call and serves a scripted message queue. Not a Windows
 * emulation: only what the unit calls, with the semantics that test's header documents. */
#ifndef PHASE298_MOCK_WINDOWS_H
#define PHASE298_MOCK_WINDOWS_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define WINAPI
#define CALLBACK
#define TRUE  1
#define FALSE 0
#define INFINITE 0xffffffffu
#define WAIT_OBJECT_0 0u
#define WAIT_FAILED   0xffffffffu

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef short SHORT;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef unsigned int UINT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef void *LPVOID;
typedef void *HANDLE;
typedef struct mock_window *HWND;
typedef void *HICON, *HDC, *HBRUSH, *HBITMAP, *HINSTANCE, *HMONITOR, *HCURSOR, *HGDIOBJ, *HKL, *HMENU, *HMODULE;
typedef wchar_t WCHAR;
typedef const char *LPCSTR;
typedef const wchar_t *LPCWSTR;
typedef wchar_t *LPWSTR;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef WORD ATOM;

typedef struct { LONG x, y; } POINT;
typedef struct { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; } PAINTSTRUCT;
typedef struct {
    UINT cbSize, style;
    WNDPROC lpfnWndProc;
    int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName, lpszClassName;
    HICON hIconSm;
} WNDCLASSEXA;
typedef struct {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy, cx, y, x;
    LONG style;
    LPCSTR lpszName, lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTA;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct {
    UINT length, flags, showCmd;
    POINT ptMinPosition, ptMaxPosition;
    RECT rcNormalPosition;
} WINDOWPLACEMENT;
typedef struct {
    DWORD biSize;
    LONG biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
typedef struct { BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor; } ICONINFO;
typedef struct { pthread_mutex_t m; int depth; int owner; } CRITICAL_SECTION;
typedef union {
    struct { DWORD LowPart; LONG HighPart; } u;
    int64_t QuadPart;
} LARGE_INTEGER;

#define LOWORD(l)           ((WORD)(((uintptr_t)(l)) & 0xffffu))
#define HIWORD(l)           ((WORD)((((uintptr_t)(l)) >> 16) & 0xffffu))
#define MAKEINTRESOURCEA(i) ((LPCSTR)(uintptr_t)(WORD)(i))
#define IDC_ARROW           MAKEINTRESOURCEA(32512)
#define GetModuleHandle     GetModuleHandleA
#define LoadCursor          LoadCursorA
#define GetWindowLongPtr    GetWindowLongPtrA
#define SetWindowLongPtr    SetWindowLongPtrA

/* Messages (the real values). */
#define WM_NULL          0x0000u
#define WM_CREATE        0x0001u
#define WM_DESTROY       0x0002u
#define WM_GETMINMAXINFO 0x0024u
#define WM_SIZE          0x0005u
#define WM_ACTIVATE      0x0006u
#define WM_SETFOCUS      0x0007u
#define WM_KILLFOCUS     0x0008u
#define WM_PAINT         0x000Fu
#define WM_CLOSE         0x0010u
#define WM_QUIT          0x0012u
#define WM_ERASEBKGND    0x0014u
#define WM_SHOWWINDOW    0x0018u
#define WM_ACTIVATEAPP   0x001Cu
#define WM_SETCURSOR     0x0020u
#define WM_GETICON       0x007Fu
#define WM_SETICON       0x0080u
#define WM_NCCREATE      0x0081u
#define WM_NCDESTROY     0x0082u
#define WM_KEYDOWN       0x0100u
#define WM_KEYUP         0x0101u
#define WM_CHAR          0x0102u
#define WM_SYSKEYDOWN    0x0104u
#define WM_SYSKEYUP      0x0105u
#define WM_SYSCHAR       0x0106u
#define WM_SYSCOMMAND    0x0112u
#define WM_TIMER         0x0113u
#define WM_MOUSEMOVE     0x0200u
#define WM_LBUTTONDOWN   0x0201u
#define WM_LBUTTONUP     0x0202u
#define WM_RBUTTONDOWN   0x0204u
#define WM_RBUTTONUP     0x0205u
#define WM_MBUTTONDOWN   0x0207u
#define WM_MOUSEWHEEL    0x020Au
#define WM_ENTERMENULOOP 0x0211u
#define WM_EXITMENULOOP  0x0212u
#define WM_ENTERSIZEMOVE 0x0231u
#define WM_EXITSIZEMOVE  0x0232u
#define WM_APP           0x8000u
#define HTCAPTION 2
#define HTCLIENT  1
#define ICON_SMALL  0
#define ICON_BIG    1
#define ICON_SMALL2 2

/* Virtual keys (the real values). */
#define VK_LBUTTON   0x01
#define VK_RBUTTON   0x02
#define VK_BACK      0x08
#define VK_TAB       0x09
#define VK_RETURN    0x0D
#define VK_SHIFT     0x10
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_CAPITAL   0x14
#define VK_ESCAPE    0x1B
#define VK_SPACE     0x20
#define VK_LEFT      0x25
#define VK_UP        0x26
#define VK_HOME      0x24
#define VK_LWIN      0x5B
#define VK_RWIN      0x5C
#define VK_NUMPAD5   0x65
#define VK_ADD       0x6B
#define VK_F1        0x70
#define VK_F4        0x73
#define VK_F9        0x78
#define VK_F10       0x79
#define VK_NUMLOCK   0x90
#define VK_SCROLL    0x91
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5
#define VK_OEM_1     0xBA
#define VK_OEM_PLUS  0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_2     0xBF
#define VK_OEM_7     0xDE
#define MAPVK_VK_TO_CHAR 2u

/* Styles, window/class long indices, SetWindowPos flags, GDI, message boxes, monitors, code pages. */
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define WS_POPUP            ((LONG_PTR)0x80000000u)
#define WS_SYSMENU          0x00080000L
#define GWL_STYLE           (-16)
#define GWLP_USERDATA       (-21)
#define GCLP_HICON          (-14)
#define GCLP_HICONSM        (-34)
#define HWND_TOP            ((HWND)0)
#define SWP_NOSIZE          0x0001u
#define SWP_NOMOVE          0x0002u
#define SWP_NOZORDER        0x0004u
#define SWP_FRAMECHANGED    0x0020u
#define SWP_NOOWNERZORDER   0x0200u
#define SW_SHOW             5
#define CW_USEDEFAULT       ((int)0x80000000u)
#define SM_CXICON           11
#define SM_CXSMICON         49
#define IMAGE_ICON          1u
#define BLACK_BRUSH         4
#define COLORONCOLOR        3
#define DIB_RGB_COLORS      0u
#define SRCCOPY             0x00CC0020u
#define MB_OK               0x00000000u
#define MB_ICONERROR        0x00000010u
#define MB_TASKMODAL        0x00002000u
#define MB_SETFOREGROUND    0x00010000u
#define MONITOR_DEFAULTTOPRIMARY 1u
#define CP_ACP              0u

/* Kernel. */
HANDLE CreateThread(void *security, size_t stack, LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD flags, DWORD *id);
HANDLE CreateEventA(void *security, BOOL manual, BOOL initial, LPCSTR name);
BOOL SetEvent(HANDLE);
BOOL CloseHandle(HANDLE);
DWORD WaitForSingleObject(HANDLE, DWORD);
DWORD WaitForMultipleObjects(DWORD, const HANDLE *, BOOL, DWORD);
DWORD GetThreadId(HANDLE);
void Sleep(DWORD);
void InitializeCriticalSection(CRITICAL_SECTION *);
void DeleteCriticalSection(CRITICAL_SECTION *);
void EnterCriticalSection(CRITICAL_SECTION *);
void LeaveCriticalSection(CRITICAL_SECTION *);
LONG InterlockedExchange(volatile LONG *, LONG);
BOOL QueryPerformanceCounter(LARGE_INTEGER *);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *);
HMODULE GetModuleHandleA(LPCSTR);
/* Phase 333: xeen_platform_physical_pixels looks up the DPI functions (the mock has none). */
typedef int (*FARPROC)(void);
FARPROC GetProcAddress(HMODULE, LPCSTR);
int MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int);
/* Windows and messages. */
ATOM RegisterClassExA(const WNDCLASSEXA *);
HWND CreateWindowA(LPCSTR cls, LPCSTR title, LONG style, int x, int y, int w, int h, HWND parent, HMENU menu,
                   HINSTANCE inst, LPVOID param);
BOOL DestroyWindow(HWND);
BOOL ShowWindow(HWND, int);
BOOL AdjustWindowRect(RECT *, LONG, BOOL);
BOOL SystemParametersInfoA(UINT, UINT, void *, UINT);
#define SPI_GETWORKAREA 0x0030
LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM);
BOOL PostMessageA(HWND, UINT, WPARAM, LPARAM);
BOOL PostThreadMessageA(DWORD, UINT, WPARAM, LPARAM);
void PostQuitMessage(int);
BOOL GetMessageA(MSG *, HWND, UINT, UINT);
BOOL TranslateMessage(const MSG *);
LRESULT DispatchMessageA(const MSG *);
LONG_PTR GetWindowLongPtrA(HWND, int);
LONG_PTR SetWindowLongPtrA(HWND, int, LONG_PTR);
ULONG_PTR GetClassLongPtrA(HWND, int);
ULONG_PTR SetClassLongPtrA(HWND, int, LONG_PTR);
BOOL SetWindowTextA(HWND, LPCSTR);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT *);
BOOL SetWindowPlacement(HWND, const WINDOWPLACEMENT *);
HMONITOR MonitorFromWindow(HWND, DWORD);
BOOL GetMonitorInfoA(HMONITOR, MONITORINFO *);
BOOL InvalidateRect(HWND, const RECT *, BOOL);
HDC BeginPaint(HWND, PAINTSTRUCT *);
BOOL EndPaint(HWND, const PAINTSTRUCT *);
BOOL ScreenToClient(HWND, POINT *);
uintptr_t SetTimer(HWND, uintptr_t, UINT, void *);
BOOL KillTimer(HWND, uintptr_t);
HCURSOR LoadCursorA(HINSTANCE, LPCSTR);
HCURSOR SetCursor(HCURSOR);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
int GetSystemMetrics(int);
HANDLE LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT);
BOOL DestroyIcon(HICON);
HICON CreateIconIndirect(ICONINFO *);
/* Input. */
SHORT GetKeyState(int);
SHORT GetAsyncKeyState(int);
BOOL GetKeyboardState(BYTE *);
UINT MapVirtualKeyW(UINT, UINT);
int ToUnicodeEx(UINT, UINT, const BYTE *, LPWSTR, int, UINT, HKL);
HKL GetKeyboardLayout(DWORD);
/* GDI. */
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
HGDIOBJ GetStockObject(int);
int FillRect(HDC, const RECT *, HBRUSH);
int SetStretchBltMode(HDC, int);
int StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *, UINT, DWORD);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
HBITMAP CreateBitmap(int, int, UINT, UINT, const void *);
BOOL DeleteObject(HGDIOBJ);
#endif
