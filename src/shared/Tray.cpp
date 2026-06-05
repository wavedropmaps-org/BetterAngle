#include "shared/Tray.h"
#include <iostream>

// Resource ID for the icon defined in resource.rc
#ifndef IDI_ICON1
#define IDI_ICON1 101
#endif

void AddSystrayIcon(HWND hwnd, HINSTANCE hInstance) {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)); // Load BetterAngle icon from resources

    if (!nid.hIcon) {
        // Fallback to application icon if BetterAngle icon resource fails
        nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    }

    if (!nid.hIcon) {
        // Last resort fallback to Windows default icon
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    // Set Tooltip
    lstrcpy(nid.szTip, L"BetterAngle Pro (Right Click to Exit)");

    BOOL addResult = Shell_NotifyIcon(NIM_ADD, &nid);
    if (!addResult) {
        // Log failure but don't crash
        std::cerr << "Failed to add system tray icon. GetLastError=" << GetLastError() << std::endl;
        return;
    }

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

void ShowTrayContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit BetterAngle");
    
    POINT pt;
    GetCursorPos(&pt);

    // The HUD window is WS_EX_TRANSPARENT (click-through overlay).
    // SetForegroundWindow fails silently on transparent windows, which causes
    // TrackPopupMenu to dismiss immediately.  Temporarily strip the flag so
    // the popup menu actually appears and can be clicked.
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    bool wasTransparent = (exStyle & WS_EX_TRANSPARENT) != 0;
    if (wasTransparent) {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
    }

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    
    // Restore click-through if it was set before
    if (wasTransparent) {
        LONG current = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE, current | WS_EX_TRANSPARENT);
    }

    // Post benign message to the window to ensure menu closes properly if user clicks away
    // (This is a documented Windows bug workaround)
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);

    if (cmd == ID_TRAY_EXIT) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}

void UpdateTrayTooltip(HWND hwnd, float angle) {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    swprintf_s(nid.szTip, _countof(nid.szTip), L"BetterAngle Pro  |  %.1f°", angle);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void RemoveSystrayIcon(HWND hwnd) {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}
