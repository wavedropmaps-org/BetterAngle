#ifndef TRAY_H
#define TRAY_H

#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 100)
#define ID_TRAY_EXIT 2001

void AddSystrayIcon(HWND hwnd, HINSTANCE hInstance);
void ShowTrayContextMenu(HWND hwnd);
void RemoveSystrayIcon(HWND hwnd);
void UpdateTrayTooltip(HWND hwnd, float angle);

#endif
