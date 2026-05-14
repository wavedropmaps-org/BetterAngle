#include "shared/Tray.h"
#include <iostream>

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
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void RemoveSystrayIcon(HWND hwnd) {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}
