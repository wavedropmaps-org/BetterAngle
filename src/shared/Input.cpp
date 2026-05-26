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

extern std::string g_nitroSyncLog;

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
  static HWND s_lastFg = NULL;
  static bool s_lastResult = false;

  HWND fg = GetForegroundWindow();
  if (!fg) {
    s_lastFg = NULL;
    return false;
  }

  if (fg == s_lastFg) {
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
    if (!g_diagNoTimer.load()) {
      timeBeginPeriod(1); // Force 1ms Windows resolution
    }
    while (g_running) {
      for (int vk : g_gamingKeys) {
        g_physicalKeys[vk].store((GetAsyncKeyState(vk) & 0x8000) != 0,
                                 std::memory_order_relaxed);
      }
      // Poll all mouse buttons for custom keybinds
      g_physicalKeys[VK_LBUTTON].store(
          (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
          std::memory_order_relaxed);
      g_physicalKeys[VK_RBUTTON].store(
          (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
          std::memory_order_relaxed);
      g_physicalKeys[VK_MBUTTON].store(
          (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0,
          std::memory_order_relaxed);
      g_physicalKeys[VK_XBUTTON1].store(
          (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0,
          std::memory_order_relaxed);
      g_physicalKeys[VK_XBUTTON2].store(
          (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0,
          std::memory_order_relaxed);
      Sleep(1);
    }
    if (!g_diagNoTimer.load()) {
      timeEndPeriod(1);
    }
  }).detach();

  LOG_INFO("High-Performance Polling Thread started (TRUE 1ms Hardware Scan)");
}

void RegisterRawMouse(HWND hwnd) {
  if (g_diagNoRawInput.load()) {
    LOG_INFO("Diagnostic: Raw Mouse Input disabled");
    return;
  }
  
  RAWINPUTDEVICE rid[1];

  // Mouse only — keyboard raw input removed in v5.5.179 (Space Bar Ghost Fix).
  // Keyboard RIDEV_INPUTSINK caused auto-mantle by intercepting Space Bar
  // events and feeding them back through WM_INPUT, creating ghost key-presses.
  rid[0].usUsagePage = 0x01;
  rid[0].usUsage = 0x02;
  rid[0].dwFlags = RIDEV_INPUTSINK;
  rid[0].hwndTarget = hwnd;

  if (!RegisterRawInputDevices(rid, 1, sizeof(RAWINPUTDEVICE))) {
    LOG_ERROR("Failed to register raw input devices (Mouse only).");
  }
}

int GetRawInputDeltaX(LPARAM lparam) {
  UINT dwSize;
  GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &dwSize,
                  sizeof(RAWINPUTHEADER));
  if (dwSize == 0)
    return 0;

  std::vector<BYTE> lpb(dwSize);
  if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, lpb.data(), &dwSize,
                      sizeof(RAWINPUTHEADER)) != dwSize)
    return 0;

  RAWINPUT *raw = (RAWINPUT *)lpb.data();
  if (raw->header.dwType == RIM_TYPEMOUSE) {
    return raw->data.mouse.lLastX;
  }
  return 0;
}

// Hardware-direct scancode injection (Movement Cluster)
static const BYTE SCANCODE_W = 0x11;
static const BYTE SCANCODE_A = 0x1E;
static const BYTE SCANCODE_S = 0x1F;
static const BYTE SCANCODE_D = 0x20;

void SendHardwareKey(BYTE scancode, bool pressed) {
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wScan = scancode;
  input.ki.wVk = 0;  // Nullify virtual key to signal hardware-origin

  // Use MapVirtualKey as verification layer for regional keyboard compatibility
  BYTE verifiedScancode = MapVirtualKey(MapVirtualKey(scancode, MAPVK_VSC_TO_VK), MAPVK_VK_TO_VSC);
  if (verifiedScancode != 0) {
    input.ki.wScan = verifiedScancode;
  }

  if (pressed) {
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
  } else {
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
  }

  SendInput(1, &input, sizeof(INPUT));
}

void SendDirectMovement(char direction, bool pressed) {
  BYTE scancode = 0;

  switch (direction) {
    case 'W':
    case 'w':
      scancode = SCANCODE_W;
      break;
    case 'A':
    case 'a':
      scancode = SCANCODE_A;
      break;
    case 'S':
    case 's':
      scancode = SCANCODE_S;
      break;
    case 'D':
    case 'd':
      scancode = SCANCODE_D;
      break;
    default:
      LOG_ERROR("SendDirectMovement: Invalid direction character");
      return;
  }

  SendHardwareKey(scancode, pressed);
}

// End of Input.cpp
