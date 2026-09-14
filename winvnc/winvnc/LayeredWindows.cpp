/////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2002-2024 UltraVNC Team Members. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
//  If the source code for the program is not available from the place from
//  which you received this file, check
//  https://uvnc.com/
//
////////////////////////////////////////////////////////////////////////////


#include "stdhdrs.h"
#if !defined(AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_)
#define AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_
#if _MSC_VER > 1000
#endif // _MSC_VER > 1000
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <winsock2.h>
#include <windows.h>
#endif // !defined(AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_)
#include <time.h>
#include "stdhdrs.h"
#include "resource.h"
#include "vncdesktop.h"
#include "vncdesktopthread.h"
#include "vncOSVersion.h"
#include "LayeredWindows.h"
#include "HelperOverlayPolicy.h"
#include <string>

HWND LayeredWindows::hwnd;
HINSTANCE LayeredWindows::hInst;
int LayeredWindows::wd;
int LayeredWindows::ht;

// Owned by one helper instance; only the UI thread accesses HWND/GDI objects.
// A shared lifetime lets teardown return safely even if the UI thread stalls.
struct LayeredWindows::BorderState {
    RECT bounds = {};
    std::wstring text;
    HANDLE stop = NULL;
    HANDLE thread = NULL;
    HWND window = NULL;
    HFONT font = NULL;
    HPEN pen = NULL;
    ~BorderState() {
        if (font) DeleteObject(font);
        if (pen) DeleteObject(pen);
        if (thread) CloseHandle(thread);
        if (stop) CloseHandle(stop);
    }
};

LayeredWindows::LayeredWindows()
{
   wd = 0;
   ht = 0;
   hwnd = NULL;
   hInst = NULL;
}
LayeredWindows::~LayeredWindows()
{
    StopBorderWindow();
}

HBITMAP LayeredWindows::DoGetBkGndBitmap2(IN CONST UINT uBmpResId)
{
    static HBITMAP hbmBkGnd = NULL;
    if (NULL == hbmBkGnd)
    {
        char WORKDIR[MAX_PATH];
        char mycommand[MAX_PATH];
        if (GetModuleFileName(NULL, WORKDIR, MAX_PATH)) {
            char* p = strrchr(WORKDIR, '\\');
            if (p == NULL) return 0;
            *p = '\0';
        }
        strcpy_s(mycommand, WORKDIR);
        strcat_s(mycommand, "\\background.bmp");

        hbmBkGnd = (HBITMAP)LoadImage(NULL, mycommand, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        if (hbmBkGnd == NULL) {
            hbmBkGnd = (HBITMAP)LoadImage(
                GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_LOGO64), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        }
        BITMAPINFOHEADER h2;
        h2.biSize = sizeof(h2);
        h2.biBitCount = 0;
        HDC hxdc = CreateDC("DISPLAY", NULL, NULL, NULL);
        GetDIBits(hxdc, hbmBkGnd, 0, 0, NULL, (BITMAPINFO*)&h2, DIB_RGB_COLORS);
        wd = h2.biWidth; ht = h2.biHeight;
        DeleteDC(hxdc);
        if (NULL == hbmBkGnd)
            hbmBkGnd = (HBITMAP)-1;
    }
    return (hbmBkGnd == (HBITMAP)-1)
        ? NULL : hbmBkGnd;
}


BOOL LayeredWindows::DoSDKEraseBkGnd2(IN CONST HDC hDC, IN CONST COLORREF crBkGndFill)
{
    HBITMAP hbmBkGnd = DoGetBkGndBitmap2(0);
    if (hDC && hbmBkGnd)
    {
        RECT rc;
        if ((ERROR != GetClipBox(hDC, &rc)) && !IsRectEmpty(&rc)) {
            HDC hdcMem = CreateCompatibleDC(hDC);
            if (hdcMem) {
                HBRUSH hbrBkGnd = CreateSolidBrush(crBkGndFill);
                if (hbrBkGnd) {
                    HGDIOBJ hbrOld = SelectObject(hDC, hbrBkGnd);
                    if (hbrOld) {
                        SIZE size = { (rc.right - rc.left), (rc.bottom - rc.top) };
                        if (PatBlt(hDC, rc.left, rc.top, size.cx, size.cy, PATCOPY)) {
                            HGDIOBJ hbmOld = SelectObject(hdcMem, hbmBkGnd);
                            if (hbmOld) {
                                StretchBlt(hDC, 0, 0, size.cx, size.cy, hdcMem, 0, 0, wd, ht, SRCCOPY);
                                SelectObject(hdcMem, hbmOld);
                            }
                        }
                        SelectObject(hDC, hbrOld);
                    }
                    DeleteObject(hbrBkGnd);
                }
                DeleteDC(hdcMem);
            }
        }
    }
    return TRUE;
}

LRESULT CALLBACK  LayeredWindows::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CREATE:
        SetTimer(hwnd, 100, 20, NULL);
        break;
    case WM_TIMER:
        if (wParam == 100)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    case WM_ERASEBKGND:
        DoSDKEraseBkGnd2((HDC)wParam, RGB(0, 0, 0));
        return true;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LONG_PTR)GetStockObject(NULL_BRUSH);
    case WM_DESTROY:
        KillTimer(hwnd, 100);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

bool LayeredWindows::create_black_window(void)
{
    WNDCLASSEX wndClass;
    ZeroMemory(&wndClass, sizeof(wndClass));
    wndClass.cbSize = sizeof(wndClass);
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = hInst;
    wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndClass.hIconSm = NULL;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = "blackscreen";
    RegisterClassEx(&wndClass);

    RECT clientRect;
    clientRect.left = 0;
    clientRect.top = 0;
    clientRect.right = GetSystemMetrics(SM_CXSCREEN);
    clientRect.bottom = GetSystemMetrics(SM_CYSCREEN);

    UINT x(GetSystemMetrics(SM_XVIRTUALSCREEN));
    UINT y(GetSystemMetrics(SM_YVIRTUALSCREEN));
    UINT cx(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    UINT cy(GetSystemMetrics(SM_CYVIRTUALSCREEN));

    clientRect.left = x;
    clientRect.top = y;
    clientRect.right = x + cx;
    clientRect.bottom = y + cy;

    AdjustWindowRect(&clientRect, WS_CAPTION, FALSE);
    hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, "blackscreen", "blackscreen",
        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
        CW_USEDEFAULT, CW_USEDEFAULT, cx, cy, NULL, NULL, hInst, NULL);
    typedef DWORD(WINAPI* PSLWA)(HWND, DWORD, BYTE, DWORD);

    PSLWA pSetLayeredWindowAttributes = NULL;
    HMODULE hDLL = LoadLibrary("user32");
    if (hDLL) pSetLayeredWindowAttributes = (PSLWA)GetProcAddress(hDLL, "SetLayeredWindowAttributes");

#ifndef _X64
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_DLGFRAME | WS_THICKFRAME);
    SetWindowLong(hwnd, GWL_STYLE, style);
#else
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~(WS_DLGFRAME | WS_THICKFRAME);
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
#endif

    if (pSetLayeredWindowAttributes != NULL) {
#ifndef _X64
        SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);
#else
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);
#endif
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }
    if (pSetLayeredWindowAttributes != NULL)
        pSetLayeredWindowAttributes(hwnd, RGB(255, 255, 255), 255, LWA_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, cx, cy, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    if (VNC_OSVersion::getInstance()->OS_WIN10_TRANS)
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    return true;
}

DWORD WINAPI LayeredWindows::BlackWindow(LPVOID lpParam)
{
    HDESK desktop;
    desktop = OpenInputDesktop(0, FALSE,
        DESKTOP_CREATEMENU | DESKTOP_CREATEWINDOW |
        DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
        DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS |
        DESKTOP_SWITCHDESKTOP | GENERIC_WRITE
    );

    HDESK old_desktop = GetThreadDesktop(GetCurrentThreadId());
    DWORD dummy{};

    char new_name[256]{};
    if (desktop) {
        GetUserObjectInformation(desktop, UOI_NAME, &new_name, 256, &dummy);
        SetThreadDesktop(desktop);
    }

    create_black_window();
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0) != 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    vnclog.Print(LL_INTERR, VNCLOG("end BlackWindow \n"));
    SetThreadDesktop(old_desktop);
    if (desktop) CloseDesktop(desktop);

    return 0;
}

LRESULT CALLBACK LayeredWindows::WndBorderProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto state = reinterpret_cast<BorderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        const int saved = SaveDC(hdc);
        if (!saved) { EndPaint(hwnd, &ps); return 0; }
        // Window painting uses client coordinates, not virtual-desktop offsets.
        // The secondary monitor may be to the right, left or above the primary.
        SelectObject(hdc, state->pen);
        SelectObject(hdc, state->font);
        SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        FillRect(hdc, &clientRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        Rectangle(hdc, clientRect.left + 2, clientRect.top + 2,
            clientRect.right - 2, clientRect.bottom - 2);
        SetTextColor(hdc, RGB(255, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        
        if (!state->text.empty()) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.left += 10;
            rc.right -= 10;
            rc.top += 10;
            DrawTextW(hdc, state->text.c_str(), static_cast<int>(state->text.size()), &rc,
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        // These objects belong to LayeredWindows and are reused on repaint.
        // Never delete a pen while it is selected into a DC.
        if (saved) RestoreDC(hdc, saved);
        EndPaint(hwnd, &ps);
    }
                 break;
    case WM_CLOSE:        
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        state->window = NULL;
        SetEvent(state->stop);
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

DWORD WINAPI LayeredWindows::BorderWindow(LPVOID lpParam)
{
    auto holder = static_cast<std::shared_ptr<BorderState>*>(lpParam);
    auto state = *holder;
    delete holder;
    if (WaitForSingleObject(state->stop, 0) == WAIT_OBJECT_0) return 0;
    HDESK desktop = OpenInputDesktop(0, FALSE,
        DESKTOP_CREATEWINDOW | DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS |
        DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    HDESK old_desktop = GetThreadDesktop(GetCurrentThreadId());
    if (!desktop || !SetThreadDesktop(desktop)) {
        if (desktop) CloseDesktop(desktop);
        return 0;
    }
    if (WaitForSingleObject(state->stop, 0) != WAIT_OBJECT_0 && create_border_window(*state)) {
        while (MsgWaitForMultipleObjects(1, &state->stop, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { SetEvent(state->stop); break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (state->window) DestroyWindow(state->window);
    }
    SetThreadDesktop(old_desktop);
    if (desktop) CloseDesktop(desktop);

    return 0;
}

bool LayeredWindows::create_border_window(BorderState& state)
{
    WNDCLASSEXW wndClass;
    ZeroMemory(&wndClass, sizeof(wndClass));
    wndClass.cbSize = sizeof(wndClass);
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WndBorderProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndClass.hIconSm = NULL;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = L"borderscreen";
    if (!RegisterClassExW(&wndClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    HDC dc = GetDC(NULL);
    if (!dc) return false;
    const int height = MulDiv(-18, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, dc);
    state.pen = CreatePen(PS_DASHDOTDOT, 5, RGB(255, 0, 0));
    state.font = CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Verdana");
    if (!state.pen || !state.font) return false;
    const auto& rect = state.bounds;
    state.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT |
        WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"borderscreen", state.text.c_str(),
        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, wndClass.hInstance, &state);
    if (!state.window) return false;
    SetLayeredWindowAttributes(state.window, RGB(255, 255, 255), 0, LWA_COLORKEY);
    if (VNC_OSVersion::getInstance()->OS_WIN10_TRANS)
        SetWindowDisplayAffinity(state.window, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(state.window, SW_SHOWNOACTIVATE);
    return true;
}

bool LayeredWindows::SetBlankMonitor(bool enabled, bool blankMonitorEnabled, bool black_window_active)
{
    if ((!VNC_OSVersion::getInstance()->OS_WIN10_TRANS && VNC_OSVersion::getInstance()->OS_WIN10)
        || VNC_OSVersion::getInstance()->OS_WIN8)
        return false;

    // Also Turn Off the Monitor if allowed ("Blank Screen", "Blank Monitor")
    if (blankMonitorEnabled)
    {
        if (enabled) {
            if (VNC_OSVersion::getInstance()->OS_AERO_ON)
                VNC_OSVersion::getInstance()->DisableAero();

            HANDLE ThreadHandle2 = NULL;
            DWORD dwTId;
            ThreadHandle2 = CreateThread(NULL, 0, BlackWindow, NULL, 0, &dwTId);
            if (ThreadHandle2)
                CloseHandle(ThreadHandle2);
            black_window_active = true;
        }
        else {
            HWND Blackhnd = FindWindow(("blackscreen"), 0);
            if (Blackhnd)
                PostMessage(Blackhnd, WM_CLOSE, 0, 0);
            black_window_active = false;
            VNC_OSVersion::getInstance()->ResetAero();
        }
    }
    return black_window_active;
}

void LayeredWindows::SetBorderWindow(bool enabled, RECT rect, char* infoMsg, bool set_OSD)
{
    StopBorderWindow();
    if (!enabled) return;
    (void)infoMsg;
    (void)set_OSD;
    HelperOverlayPolicy::Snapshot policy;
    const auto result = HelperOverlayPolicy::Read(policy);
    if (result == HelperOverlayPolicy::ReadResult::Ready && !policy.enabled) return;
    auto state = std::make_shared<BorderState>();
    state->bounds = rect;
    // Missing/invalid/expired metadata never hides the local indicator or
    // reuses an earlier operator name. It does not affect authentication.
    state->text = L"Uzaktan destek oturumu aktif";
    if (result == HelperOverlayPolicy::ReadResult::Ready && policy.operatorName[0]) {
        state->text += L" - ";
        static_assert(sizeof(wchar_t) == sizeof(policy.operatorName[0]), "Windows UTF-16 required");
        state->text += reinterpret_cast<const wchar_t*>(policy.operatorName);
    }
    state->stop = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!state->stop) return;
    auto holder = new std::shared_ptr<BorderState>(state);
    state->thread = CreateThread(NULL, 0, BorderWindow, holder, 0, NULL);
    if (!state->thread) { delete holder; return; }
    borderState = state;
}

void LayeredWindows::StopBorderWindow()
{
    auto state = std::move(borderState);
    if (!state) return;
    SetEvent(state->stop);
    // Thread owns its state until it exits; no dangling window/context on timeout.
    WaitForSingleObject(state->thread, 5000);
}

