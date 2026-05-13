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

std::vector<bool> GetGamingKeyState() {
  std::vector<bool> state;
  state.reserve(std::size(g_gamingKeys));
  for (int vk : g_gamingKeys) {
    // FRESH SCAN: Ensure snapshot is absolute latest
    state.push_back((GetAsyncKeyState(vk) & 0x8000) != 0);
  }
  return state;
}

void SyncGamingKeysNitro(const std::vector<bool> &preState) {
  // GUARD: Prevent back-to-back rapid transitions from interrupting a
  // fix sequence that is mid-execution (caused missed fixes when
  // transitions fired ~1 second apart).
  if (g_ghostFixInProgress.exchange(true)) {
    LOG_WARN("GhostFix: Already in progress — skipping duplicate.");
    return;
  }

  ULONGLONG fixStart = GetTickCount64();
  g_wPostUnlock = GetAsyncKeyState('W');

  // RAW INPUT HARD-RESET (v5.5.80):
  // BlockInput freezes the Windows Async Key State Table. Key releases during
  // the lock are discarded, causing ghost keys.
  //
  // Phase 1 — SHOCK: Unconditionally release all keys held before the lock.
  // Phase 2 — THAW: Short Sleep(5) for the async key state table to register
  //   the Shock KeyUp. Minimal delay — we want the Reboot signal to land fast.
  // Phase 3 — RESTORE (REBOOT SIGNAL): Unconditionally re-press ALL preState
  //   keys with a 3-pulse typematic burst using KEYEVENTF_SCANCODE. We do NOT
  //   gate on GetAsyncKeyState because after Shock, the HID parser won't
  //   generate a new make event for a key already pressed in the previous USB
  //   report — so GetAsyncKeyState returns false even when the user is still
  //   holding the key. This was the root cause of the double-press bug.
  // Phase 4 — RAW INPUT CORRECTION: Reset arrays and open a 200ms collection
  //   window (g_ghostFixInProgress=false) so only REAL hardware typematic
  //   events are recorded. Combined rule: only keep pressed if Mk=1 AND Br=0.
  //   All other combinations → kill ghost (user released the key).

  // Store preState for forensics overlay, and RESET postState to prevent
  // stale values from a prior lock cycle (0/1/0 Logic Hallucination bug).
  for (size_t i = 0; i < preState.size() && i < 4; ++i) {
    g_preState[i] = preState[i];
    g_postState[i] = false; // Will be set true only if we actually restore
  }

  std::vector<INPUT> releaseInputs;
  std::string log =
      "Shock&Restore [W:" + std::to_string(g_wPostUnlock.load()) + "]: ";

  extern std::atomic<bool> g_fortniteFocusedCache;

  // Step 1: UNCONDITIONAL SHOCK — Release every key that was held before lock.
  // ALWAYS run the shock — ghost keys must be cleared even when tabbed out,
  // otherwise they persist and the character moves when the user tabs back in.
  // We do NOT read post/phys from the frozen table to decide — we just release.
  for (size_t i = 0; i < preState.size(); ++i) {
    if (preState[i]) {
      int vk = g_gamingKeys[i];

      // WHITELIST CHECK: STRICTLY W, A, S, D, SPACE ONLY
      bool whitelisted = false;
      for (int wvk : g_gamingKeys)
        if (vk == wvk)
          whitelisted = true;
      if (!whitelisted)
        continue;

      UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
      if (scanCode == 0)
        continue;

      INPUT input = {0};
      input.type = INPUT_KEYBOARD;
      input.ki.wVk = (WORD)vk;
      input.ki.wScan = (WORD)scanCode;
      input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
      releaseInputs.push_back(input);
      log += std::to_string(vk) + "↑ ";
    }
  }

  if (!releaseInputs.empty()) {
    UINT sent = SendInput((UINT)releaseInputs.size(), releaseInputs.data(),
                          sizeof(INPUT));
    if (sent != releaseInputs.size()) {
      LOG_ERROR("Shock FAIL: sent %u/%u", sent, (UINT)releaseInputs.size());
      log += "(SHOCK_FAIL:" + std::to_string(sent) + "/" +
             std::to_string(releaseInputs.size()) + ") ";
    }
  } else {
    log += "(NoShock) ";
  }

  // Step 2: THAW — Short delay for async key state table to register Shock.
  Sleep(5);

  // Step 3: UNCONDITIONAL RESTORE — Re-press ALL keys that were held before
  // the lock. We do NOT check GetAsyncKeyState because after Shock sends a
  // synthetic KeyUp, the HID parser won't generate a new make event for a key
  // already pressed in the previous USB report. GetAsyncKeyState is therefore
  // unreliable — it returns false even when the user is still holding the key.
  // This was the root cause of the double-press bug.
  bool anyRestored = false;
  bool anyCorrected = false;
  if (g_fortniteFocusedCache.load()) {
    for (size_t i = 0; i < preState.size(); ++i) {
      if (preState[i]) {
        int vk = g_gamingKeys[i];
        UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (scanCode == 0)
          continue;

        // TYPEMATIC BURST: 3 pulses, 10ms gaps to simulate hardware repeat
        for (int burst = 0; burst < 3; burst++) {
          INPUT input = {0};
          input.type = INPUT_KEYBOARD;
          input.ki.wVk = (WORD)vk;
          input.ki.wScan = (WORD)scanCode;
          input.ki.dwFlags = KEYEVENTF_SCANCODE; // KeyDown
          SendInput(1, &input, sizeof(INPUT));
          if (burst < 2)
            Sleep(10);
        }

        if (i < 4)
          g_postState[i] = true; // Always true — we unconditionally restored
        anyRestored = true;
        log += std::to_string(vk) + "↓(x3) ";
      }
    }
    log += anyRestored ? "(Restored)" : "(Clean)";

    // No Verify phase: GetAsyncKeyState is unreliable after Shock/Restore and
    // was the source of false 1/1/0 "Thaw Failure" diagnostics. We trust the
    // pre-lock snapshot plus Raw Input correction instead.
    g_ghostFixVerifyOk = true;

    // Step 4: RAW INPUT CORRECTION — Kill ghost-walk for keys the user
    // released during the lock.
    //
    // CONTAMINATION FIX: During Shock&Restore, g_ghostFixInProgress was true,
    // so the WM_INPUT handler ignored our synthetic SendInput events. Now we
    // reset the arrays and open the collection window (g_ghostFixInProgress=
    // false) so only REAL hardware typematic events are recorded.
    //
    // CORRECTION RULE: kill ghost if Br=1 OR Mk=0 (i.e. breakDetected ||
    // !makeDetected). This is broader than the simple "Br=1 AND Mk=0" check
    // because it also handles:
    //
    //   Mk=0 Br=0 → released during lock, BOTH events swallowed by Windows
    //                (simple rule would MISS this — key stays ghost-pressed!)
    //   Mk=1 Br=1 → released after lock, Break is the user's final action
    //                (simple rule would MISS this — ghost-walk persists!)
    //
    // Only Mk=1 Br=0 (typematic Make with no Break) proves the user is still
    // holding the key. All other combinations → kill ghost.
    //
    //   Mk=1 Br=0 → still holding → leave pressed
    //   Mk=0 Br=0 → released during lock (swallowed) → kill ghost
    //   Mk=1 Br=1 → released after lock → kill ghost (Break is final)
    //   Mk=0 Br=1 → released, no typematic → kill ghost
    for (int i = 0; i < 256; i++) {
      g_rawKeyUpDetected[i] = false;
      g_rawKeyMakeDetected[i] = false;
    }
    g_ghostFixInProgress = false; // Open collection window
    Sleep(200);
    for (size_t i = 0; i < preState.size() && i < 4; ++i) {
      if (preState[i]) {
        int vk = g_gamingKeys[i];
        bool breakDetected = g_rawKeyUpDetected[vk].load();
        bool makeDetected = g_rawKeyMakeDetected[vk].load();

        if (breakDetected || !makeDetected) {
          // Break detected OR no Make → user released → kill ghost
          // Covers: Br=1 Mk=0 (released during lock, Break swallowed)
          //         Br=0 Mk=0 (released during lock, both swallowed)
          //         Br=1 Mk=1 (released after lock, Break is final action)
          UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
          if (scanCode == 0)
            continue;

          INPUT input = {0};
          input.type = INPUT_KEYBOARD;
          input.ki.wVk = (WORD)vk;
          input.ki.wScan = (WORD)scanCode;
          input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
          SendInput(1, &input, sizeof(INPUT));

          g_postState[i] = false;
          anyCorrected = true;
          LOG_WARN("RawCorrected: key %d ghost killed (Br=%d Mk=%d)", vk,
                   breakDetected ? 1 : 0, makeDetected ? 1 : 0);
          log += "(Corrected:" + std::to_string(vk) + ") ";
        } else {
          // Mk=1 Br=0 → user still holding → no correction needed
        }
      }
    }
    if (!anyCorrected) {
      log += "(NoCorrection)";
    }
    if (anyCorrected) {
      g_activeFallback = 5; // FB5: Raw Input correction applied
    } else if (anyRestored) {
      g_activeFallback = 0; // Clean — no fallback needed
    }
  } else {
    g_ghostFixVerifyOk = true; // No restore needed = no verify needed
    g_activeFallback = 0;
    log += "(NoRestore:Unfocused)";
  }

  ULONGLONG fixEnd = GetTickCount64();
  g_ghostFixDurationMs = (long long)(fixEnd - fixStart);
  g_tableRefreshed = true;
  g_wPostFlush = GetAsyncKeyState('W');
  g_nitroSyncLog = log;
  // Only log to file on issues; routine success is captured in nitroSyncLog
  if (anyCorrected) {
    LOG_WARN("GhostFix: %llums — %s", (unsigned long long)(fixEnd - fixStart),
             log.c_str());
  }
  g_hasSynced = true;
  g_ghostFixInProgress = false;

  // Re-register Raw Input for normal app operation
  extern HWND g_hMsgWnd;
  if (g_hMsgWnd)
    RegisterRawMouse(g_hMsgWnd);
}
