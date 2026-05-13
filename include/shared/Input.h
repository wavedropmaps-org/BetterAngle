#ifndef INPUT_H
#define INPUT_H

#include <windows.h>

#include <vector>

void StartPollingThread();

// Raw Input Mouse Tracking (Delta only)
void RegisterRawMouse(HWND hwnd);
int GetRawInputDeltaX(LPARAM lparam);

// Runtime input gating helpers
bool IsFortniteForeground();
bool IsCursorCurrentlyVisible();

// Nitro Anti-Ghosting (Delta-Only)
std::vector<bool> GetGamingKeyState();
void SyncGamingKeysNitro(const std::vector<bool>& initialState);

#endif // INPUT_H
