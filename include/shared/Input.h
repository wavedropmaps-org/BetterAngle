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

// Hardware-direct scancode injection (v5.5.252)
void SendHardwareKey(BYTE scancode, bool pressed);
void SendDirectMovement(char direction, bool pressed);

#endif // INPUT_H
