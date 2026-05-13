#include "shared/Input.h"
#include "shared/EnhancedLogging.h"
#include "shared/State.h"
#include <cwchar>
#include <fstream>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <vector>
#include <windows.h>

namespace {
bool IsFortniteProcessName(const wchar_t *processName) {
  if (!processName || !processName[0])
    return false;

  const wchar_t *knownPrefixes[] = {L"FortniteClient-Win64-Shipping",
                                    L"FortniteLauncher", L"FortniteClient"};

  for (const wchar_t *prefix : knownPrefixes) {
    const size_t prefixLen = wcslen(prefix);
    if (_wcsnicmp(processName, prefix, prefixLen) == 0) {
      return true;
    }
  }

  return false;
}

const wchar_t *GetProcessBaseName(HWND hwnd, wchar_t *buffer,
                                  DWORD bufferCount) {
  if (!hwnd || !buffer || bufferCount == 0)
    return L"";

  buffer[0] = L'\0';

  DWORD processId = 0;
  GetWindowThreadProcessId(hwnd, &processId);
  if (processId == 0)
    return L"";

  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process)
    return L"";

  DWORD size = bufferCount;
  if (!QueryFullProcessImageNameW(process, 0, buffer, &size) || size == 0) {
    CloseHandle(process);
    buffer[0] = L'\0';
    return L"";
  }

  CloseHandle(process);

  const wchar_t *lastSlash = wcsrchr(buffer, L'\\');
  const wchar_t *lastForwardSlash = wcsrchr(buffer, L'/');
  const wchar_t *baseName = buffer;

  if (lastSlash && lastForwardSlash)
    baseName =
        (lastSlash > lastForwardSlash) ? lastSlash + 1 : lastForwardSlash + 1;
  else if (lastSlash)
    baseName = lastSlash + 1;
  else if (lastForwardSlash)
    baseName = lastForwardSlash + 1;

  return baseName;
}
} // namespace

#include "shared/EnhancedLogging.h"

bool IsFortniteForeground() {
  thread_local HWND s_lastFg = NULL;
  thread_local bool s_lastResult = false;

  HWND fg = GetForegroundWindow();
  if (!fg) {
    s_lastFg = NULL;
    return false;
  }

  auto updateFortniteRect = [](HWND wnd) {
    g_fortniteWindow = wnd;
    RECT cr;
    if (GetClientRect(wnd, &cr)) {
      POINT ptTL = {cr.left, cr.top};
      POINT ptBR = {cr.right, cr.bottom};
      ClientToScreen(wnd, &ptTL);
      ClientToScreen(wnd, &ptBR);
      g_fortniteRect = {ptTL.x, ptTL.y, ptBR.x, ptBR.y};
    }
  };

  if (fg == s_lastFg) {
    if (s_lastResult) updateFortniteRect(fg);
    return s_lastResult;
  }

  s_lastFg = fg;
  s_lastResult = false;

  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  if (pid == 0) {
    return false;
  }

  // Method 1: Try OpenProcess + QueryFullProcessImageNameW (Fastest)
  wchar_t processPath[MAX_PATH] = {};
  const wchar_t *processName = GetProcessBaseName(fg, processPath, MAX_PATH);
  if (processName && processName[0] && IsFortniteProcessName(processName)) {
    s_lastResult = true;
    updateFortniteRect(fg);
    return true;
  }

  // Method 2: Fallback using CreateToolhelp32Snapshot (Slowest, only if needed)
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(snap, &pe)) {
      do {
        if (pe.th32ProcessID == pid) {
          CloseHandle(snap);
          s_lastResult = IsFortniteProcessName(pe.szExeFile);
          if (s_lastResult) updateFortniteRect(fg);
          return s_lastResult;
        }
      } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
  }

  return false;
}

bool IsCursorCurrentlyVisible() {
  CURSORINFO cursorInfo = {sizeof(CURSORINFO)};
  if (!GetCursorInfo(&cursorInfo))
    return true;
  return (cursorInfo.flags & CURSOR_SHOWING) != 0;
}

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

static bool g_pollingRunning = false;

// The "Essential 5" - Core Movement Cluster (v5.5.59)
static const int g_gamingKeys[] = {'W', 'A', 'S', 'D', VK_SPACE};

// Physical Truth Table (v5.1.16)
// Using std::atomic<bool> g_physicalKeys[256] from State.h

void StartPollingThread() {
  std::thread([]() {
    timeBeginPeriod(1); // Force 1ms Windows resolution
    while (g_running) {
      for (int vk : g_gamingKeys) {
        g_physicalKeys[vk].store((GetAsyncKeyState(vk) & 0x8000) != 0,
                                 std::memory_order_relaxed);
      }
      // Also poll LBUTTON for HUD dragging (fixes legacy bug)
      g_physicalKeys[VK_LBUTTON].store(
          (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
          std::memory_order_relaxed);
      Sleep(1);
    }
    timeEndPeriod(1);
  }).detach();

  LOG_INFO("High-Performance Polling Thread started (TRUE 1ms Hardware Scan)");
}

void RegisterRawMouse(HWND hwnd) {
  RAWINPUTDEVICE rid[2];

  // Mouse
  rid[0].usUsagePage = 0x01;
  rid[0].usUsage = 0x02;
  rid[0].dwFlags = RIDEV_INPUTSINK;
  rid[0].hwndTarget = hwnd;

  // Keyboard
  rid[1].usUsagePage = 0x01;
  rid[1].usUsage = 0x06;
  rid[1].dwFlags = RIDEV_INPUTSINK;
  rid[1].hwndTarget = hwnd;

  if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
    LOG_ERROR("Failed to register raw input devices (Mouse+Keyboard).");
  }
}

int GetRawInputDeltaX(LPARAM lparam) {
  UINT dwSize = 0;
  GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &dwSize,
                  sizeof(RAWINPUTHEADER));
  if (dwSize == 0 || dwSize > 64)
    return 0;

  BYTE lpb[64];
  if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, lpb, &dwSize,
                      sizeof(RAWINPUTHEADER)) != dwSize)
    return 0;

  RAWINPUT *raw = (RAWINPUT *)lpb;
  if (raw->header.dwType == RIM_TYPEMOUSE) {
    return raw->data.mouse.lLastX;
  }
  return 0;
}

