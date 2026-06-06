#include <algorithm>
#include <atomic>
#include <cmath>
#include <dwmapi.h>
#include <fstream>
#include <gdiplus.h>
#include <iostream>
#include <shlobj.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "shared/ControlPanel.h"
#include "shared/Detector.h"
#include "shared/EnhancedLogging.h"
#include "shared/Input.h"
#include "shared/Logic.h"
#include "shared/Overlay.h"
#include "shared/Profile.h"
#include "shared/State.h"
#include "shared/Tray.h"
#include "shared/Updater.h"
#include <QCoreApplication>
#include <QGuiApplication>

#include <psapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")

using namespace Gdiplus;

void PerformanceMonitorThread();
#include "shared/State.h"

// Global State
// Global handles defined in State.h/cpp
ULONG_PTR g_gdiplusToken;
FovDetector g_detector;



HWINEVENTHOOK g_hWinEventHook = NULL;

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread,
                           DWORD dwmsEventTime) {
  if (event == EVENT_SYSTEM_FOREGROUND) {
    bool isFortnite = false;
    if (hwnd) {
      wchar_t cls[64] = {};
      if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, L"UnrealWindow") == 0) {
        isFortnite = true;
      }
    }

    static bool lastFortniteFocused = false;
    bool currentFortniteFocused = isFortnite;

    if (!lastFortniteFocused && currentFortniteFocused) {
      g_lockTriggerReason = 3; // Alt-Tab Return
      g_lockCount++;

      // Preserve angle - snapshot the current value so queued deltas don't apply
      g_logic.Bake();

      // NOW safe to update the cache.
      g_fortniteFocusedCache = currentFortniteFocused;

      // Re-assert TOPMOST so the overlay doesn't stay hidden behind Fortnite
      if (g_hHUD && !g_diagNoTopmost.load()) {
        SetWindowPos(g_hHUD, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      }

      // Run BlockInput on a detached thread so it doesn't freeze the Qt UI pump
      std::thread([]() {
        g_blockInputActive = true;
        BlockInput(TRUE);
        Sleep(300);
        BlockInput(FALSE);
        g_blockInputActive = false;
        if (g_running.load()) LOG_INFO("Alt-tab cooldown active (300ms BlockInput)");
      }).detach();
    } else {
      g_fortniteFocusedCache = currentFortniteFocused;
    }

    // Any focus transition: ask the HUD thread to re-evaluate which hotkeys
    // should be registered (Fortnite-focused vs not).
    if (lastFortniteFocused != currentFortniteFocused) {
      if (g_hHUD) {
        PostMessage(g_hHUD, WM_APP + 1, currentFortniteFocused ? 1 : 0, 0);
      }
    }
    lastFortniteFocused = currentFortniteFocused;
  }
}

// FOV Detector Thread - Now focused solely on ROI scanning
// Inter-scan sleep while Fortnite is focused. ~200Hz detection cadence —
// far faster than needed for dive/glide edge detection (the transition lock
// runs for 700ms) while keeping the scanner near-idle on the CPU.
static const DWORD kDetectorScanSleepMs = 4;
// Sleep while Fortnite is NOT focused. Nothing to scan, so stay near-idle.
static const DWORD kDetectorIdleSleepMs = 150;

void DetectorThread() {
  bool lastDiving = false;
  ULONGLONG peakMatchTimestamp = 0;
  RECT cachedMonitorRect = {};
  int cachedScreenIdx = -1;
  int cachedDisplayGen = -1;

  while (g_running) {
    if (!g_allProfiles.empty() && !g_isSelectionActive.load()) {
      Profile &p = g_allProfiles[g_selectedProfileIdx];
      g_logic.LoadProfile(p.sensitivityX);
      g_requiredMatchCount =
          (int)((p.diveGlideMatch / 100.0f) * (p.roi_w * p.roi_h));
      g_hudDecimalPlaces = p.hudDecimalPlaces;
      g_atomicShieldEnabled = p.atomicShield;
      g_directHardwareModeEnabled = p.directHardwareMode;

      bool currentFortniteFocused = g_fortniteFocusedCache.load();
      g_isCursorVisible = IsCursorCurrentlyVisible();

      // Only scan ROI when Fortnite is the foreground window
      if (currentFortniteFocused) {
        int curDisplayGen = g_displayChangeGen.load();
        if (g_screenIndex != cachedScreenIdx || curDisplayGen != cachedDisplayGen) {
          cachedMonitorRect = GetMonitorRectByIndex(g_screenIndex);
          cachedScreenIdx = g_screenIndex;
          cachedDisplayGen = curDisplayGen;
        }
        RECT mRect = cachedMonitorRect;
        RoiConfig cfg = {
            p.roi_x + mRect.left, p.roi_y + mRect.top, p.roi_w, p.roi_h,
            p.target_color,       p.tolerance};
        ULONGLONG startMs = GetTickCount64();
        g_matchCount = g_detector.Scan(cfg);
        ULONGLONG endMs = GetTickCount64();
        ULONGLONG scanMs = endMs - startMs;
        g_detectionDelayMs = scanMs;

        // Scanner CPU %: time spent scanning vs total loop period
        // (scan time + the inter-scan sleep below).
        int cpuPct =
            (scanMs > 0)
                ? (int)((scanMs * 100) / (scanMs + kDetectorScanSleepMs))
                : 0;
        g_scannerCpuPct = cpuPct;

        // Peak match tracking (2s decay window)
        int currentMatch = g_matchCount.load();
        ULONGLONG now = GetTickCount64();
        if (now - peakMatchTimestamp > 2000) {
          g_peakMatchCount = currentMatch;
          peakMatchTimestamp = now;
        } else if (currentMatch > g_peakMatchCount.load()) {
          g_peakMatchCount = currentMatch;
        }

        // Update Atomic Shield timestamp
        if (currentMatch >= g_requiredMatchCount.load()) {
          g_lastValidMatchTime = GetTickCount64();
        }
      } else {
        // Fortnite not focused, reset detection to 0
        g_matchCount = 0;
        g_detectionDelayMs = 0;
        g_scannerCpuPct = 0;
      }

      bool scanMatch = (g_matchCount.load() >= g_requiredMatchCount.load());
      bool shielded = g_atomicShieldEnabled.load() &&
                      (GetTickCount64() - g_lastValidMatchTime.load() < 25);
      bool nowDiving = scanMatch || shielded;

      // FOV Transition Locking: BlockInput during glide<->dive changes
      // to prevent mouse movement from corrupting the angle during the
      // sensitivity scale switch.
      if (!g_blockInputActive.load()) {
        // Edge: Gliding -> Diving
        if (nowDiving && !lastDiving &&
            (GetTickCount64() - g_lastLockTime > 500)) {
          g_lastLockTime = GetTickCount64();
          g_lockTriggerReason = 1; // Glide -> Dive
          g_lockCount++;
          g_logic.Bake();

          std::thread([]() {
            g_blockInputActive = true;
            BlockInput(TRUE);
            Sleep(700);
            BlockInput(FALSE);
            g_blockInputActive = false;
            if (g_running.load()) {
              g_lastLockTime = GetTickCount64();
              LOG_INFO("Transition: glide->dive, 700ms BlockInput");
            }
          }).detach();
        }

        // Edge: Diving -> Gliding
        if (!nowDiving && lastDiving &&
            (GetTickCount64() - g_lastLockTime > 500)) {
          g_lastLockTime = GetTickCount64();
          g_lockTriggerReason = 2; // Dive -> Glide
          g_lockCount++;
          g_logic.Bake();

          std::thread([]() {
            g_blockInputActive = true;
            BlockInput(TRUE);
            Sleep(700);
            BlockInput(FALSE);
            g_blockInputActive = false;
            if (g_running.load()) {
              g_lastLockTime = GetTickCount64();
              LOG_INFO("Transition: dive->glide, 700ms BlockInput");
            }
          }).detach();
        }
      }

      lastDiving = nowDiving;
      g_isDiving = nowDiving;
      g_logic.SetDivingState(nowDiving);
    }

    // Throttle the loop instead of busy-spinning. Back-to-back scans (or a
    // bare _mm_pause when idle) peg a full CPU core for no benefit. Sleep a
    // short slice while in-game for a snappy ~200Hz scan, and a long slice
    // when tabbed out or selecting an ROI/colour (nothing to detect then).
    bool active = g_fortniteFocusedCache.load() && !g_isSelectionActive.load() &&
                  !g_allProfiles.empty();
    Sleep(active ? kDetectorScanSleepMs : kDetectorIdleSleepMs);
  }
}

// Screen Snapshot for Flicker-Free Selection (v4.9.15)
void CaptureDesktop() {
  int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);

  LOG_INFO("CaptureDesktop: Virtual screen dimensions: %dx%d at offset (%d, %d)", sw, sh, sx, sy);

  HDC hdcScreen = GetDC(NULL);
  if (!hdcScreen) {
    LOG_ERROR("CaptureDesktop: Failed to get screen DC");
    return;
  }

  HDC hdcMem = CreateCompatibleDC(hdcScreen);
  if (!hdcMem) {
    LOG_ERROR("CaptureDesktop: Failed to create compatible DC");
    ReleaseDC(NULL, hdcScreen);
    return;
  }

  if (g_screenSnapshot) {
    LOG_TRACE("CaptureDesktop: Deleting previous bitmap");
    DeleteObject(g_screenSnapshot);
  }

  g_screenSnapshot = CreateCompatibleBitmap(hdcScreen, sw, sh);
  if (!g_screenSnapshot) {
    LOG_ERROR("CaptureDesktop: Failed to create compatible bitmap");
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return;
  }
  LOG_TRACE("CaptureDesktop: Bitmap created successfully");

  HGDIOBJ hOld = SelectObject(hdcMem, g_screenSnapshot);
  if (!hOld || hOld == INVALID_HANDLE_VALUE) {
    LOG_ERROR("CaptureDesktop: Failed to select bitmap into DC");
    DeleteObject(g_screenSnapshot);
    g_screenSnapshot = NULL;
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return;
  }

  // Capture the entire virtual desktop
  BOOL bltResult = BitBlt(hdcMem, 0, 0, sw, sh, hdcScreen, sx, sy, SRCCOPY);
  if (!bltResult) {
    LOG_ERROR("CaptureDesktop: BitBlt failed! GetLastError=%lu", GetLastError());
    SelectObject(hdcMem, hOld);
    DeleteObject(g_screenSnapshot);
    g_screenSnapshot = NULL;
    ReleaseDC(NULL, hdcScreen);
    DeleteDC(hdcMem);
    return;
  }
  LOG_TRACE("CaptureDesktop: BitBlt successful, screen captured");

  SelectObject(hdcMem, hOld);
  ReleaseDC(NULL, hdcScreen);
  DeleteDC(hdcMem);
  LOG_INFO("CaptureDesktop: Complete, g_screenSnapshot ready for use");
}

// Helper to get error description for GetLastError()
static std::wstring GetLastErrorString() {
  DWORD error = GetLastError();
  if (error == 0)
    return L"Success";

  wchar_t *buffer = nullptr;
  size_t size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buffer,
      0, NULL);

  std::wstring message(buffer, size);
  LocalFree(buffer);

  // Remove trailing newlines
  while (!message.empty() &&
         (message.back() == L'\n' || message.back() == L'\r')) {
    message.pop_back();
  }

  return message;
}

// Refreshes all global hotkeys for the HUD window
bool RefreshHotkeys(HWND hWnd, bool force) {
  if (!hWnd)
    return false;

  // If a keybind is currently being assigned in the UI, do nothing. All
  // hotkeys were already unregistered in startKeybindAssignment and will be
  // re-registered by endKeybindAssignment via a forced refresh.
  if (g_keybindAssignmentActive.load())
    return false;

  // Cache the current keybinds to avoid unnecessary re-registration
  static Keybinds lastKeybinds = {};
  static int lastProfileIdx = -1;
  static bool lastFortniteFocused = false;

  if (g_allProfiles.empty())
    return false;

  Profile &p = g_allProfiles[g_selectedProfileIdx];
  bool fortniteFocused = g_fortniteFocusedCache.load();

  // Check if anything that affects registration has changed
  bool keybindsChanged = (g_selectedProfileIdx != lastProfileIdx) ||
                         (p.keybinds.toggleMod != lastKeybinds.toggleMod ||
                          p.keybinds.toggleKey != lastKeybinds.toggleKey) ||
                         (p.keybinds.roiMod != lastKeybinds.roiMod ||
                          p.keybinds.roiKey != lastKeybinds.roiKey) ||
                         (p.keybinds.crossMod != lastKeybinds.crossMod ||
                          p.keybinds.crossKey != lastKeybinds.crossKey) ||
                         (p.keybinds.zeroMod != lastKeybinds.zeroMod ||
                          p.keybinds.zeroKey != lastKeybinds.zeroKey) ||
                         (fortniteFocused != lastFortniteFocused);

  if (!keybindsChanged && !force) {
    return true;
  }

  // Unregister all hotkeys first
  for (int i = 1; i <= 6; i++) {
    UnregisterHotKey(hWnd, i);
  }

  // Register new hotkeys with MOD_NOREPEAT to prevent key repeat issues
  // MOD_NOREPEAT (0x4000) prevents the hotkey from firing repeatedly when held
  // down
  bool ok = true;
  std::vector<std::pair<int, std::wstring>> failedHotkeys;

  auto registerWithErrorCheck = [&](int id, UINT mod, UINT vk,
                                    const wchar_t *name) -> bool {
    if (vk == 0) {
      // Zero key means hotkey is disabled
      g_mouseButtonKeybinds[id].store(0, std::memory_order_release);
      g_mouseButtonModifiers[id].store(0, std::memory_order_release);
      return true;
    }

    // Check if this is a mouse button code (0x01=MOUSE1, 0x02=MOUSE2, 0x04=MOUSE3, 0x05=MOUSE4, 0x06=MOUSE5)
    bool isMouseButton = (vk == 0x01 || vk == 0x02 || vk == 0x04 || vk == 0x05 || vk == 0x06);

    if (isMouseButton) {
      // Store mouse button hotkey for polling-based detection instead of RegisterHotKey
      g_mouseButtonKeybinds[id].store(vk, std::memory_order_release);
      g_mouseButtonModifiers[id].store(mod, std::memory_order_release);
      return true;
    }

    // Apply MOD_NOREPEAT flag (0x4000) to prevent strobing when held
    UINT flags = mod | 0x4000;

    if (!RegisterHotKey(hWnd, id, flags, vk)) {
      DWORD err = GetLastError();
      std::wstring errorMsg = GetLastErrorString();
      failedHotkeys.push_back({id, L"Hotkey " + std::wstring(name) +
                                       L" failed: " + errorMsg + L" (Error " +
                                       std::to_wstring(err) + L")"});
      return false;
    }
    // Clear any mouse button binding for this ID since we just registered a normal key
    g_mouseButtonKeybinds[id].store(0, std::memory_order_release);
    g_mouseButtonModifiers[id].store(0, std::memory_order_release);
    return true;
  };

  // Dashboard toggle (id 1) is always registered so the user can open the
  // dashboard from anywhere.
  ok &= registerWithErrorCheck(1, p.keybinds.toggleMod, p.keybinds.toggleKey,
                               L"Toggle Panel");

  // ROI / Crosshair / Zero hotkeys (ids 2-4) only bind when Fortnite is the
  // foreground window. While Fortnite is not focused these keys pass through
  // to the OS so the user can use them normally in other apps.
  if (fortniteFocused) {
    ok &= registerWithErrorCheck(2, p.keybinds.roiMod, p.keybinds.roiKey,
                                 L"ROI Select");
    ok &= registerWithErrorCheck(3, p.keybinds.crossMod, p.keybinds.crossKey,
                                 L"Crosshair Toggle");
    ok &= registerWithErrorCheck(4, p.keybinds.zeroMod, p.keybinds.zeroKey,
                                 L"Zero Angle");
  } else {
    // Clear any stored mouse-button bindings for these ids so the polling
    // loop doesn't fire them either.
    for (int id = 2; id <= 4; id++) {
      g_mouseButtonKeybinds[id].store(0, std::memory_order_release);
      g_mouseButtonModifiers[id].store(0, std::memory_order_release);
    }
  }
  lastFortniteFocused = fortniteFocused;

  // Log failures
  if (!failedHotkeys.empty()) {
    for (const auto &failure : failedHotkeys) {
      OutputDebugStringW((L"BetterAngle: " + failure.second + L"\n").c_str());
    }
  }

  // Update cache
  lastKeybinds = p.keybinds;
  lastProfileIdx = g_selectedProfileIdx;

  return ok;
}

// Message-Only Window for Bullet-Proof Raw Input
LRESULT CALLBACK MsgWndProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  if (message == WM_INPUT) {
    UINT dwSize;
    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize,
                    sizeof(RAWINPUTHEADER));
    if (dwSize > 0) {
      std::vector<BYTE> lpb(dwSize);
      if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize,
                          sizeof(RAWINPUTHEADER)) == dwSize) {
        RAWINPUT *raw = (RAWINPUT *)lpb.data();
        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
          // Keyboard events are no longer tracked for anti-ghosting
        }
      }
    }

    int dx = GetRawInputDeltaX(lParam);

    const bool allowAngleUpdate =
        (g_fortniteFocusedCache && !g_isCursorVisible && !g_blockInputActive.load());

    if (allowAngleUpdate) {
      g_logic.Update(dx);
    }
    return 0;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// Helper function to check mouse button hotkeys and trigger corresponding actions
static void CheckMouseButtonHotkeys(HWND hWnd) {
  if (g_keybindAssignmentActive || !hWnd) {
    return;
  }

  // Allow mouse button hotkeys during selection overlay (they may trigger selection actions)
  // This ensures custom keybinds work during ROI/color selection

  // State tracker to prevent strobing (only fire on key press edge)
  static bool wasPressed[6] = {false, false, false, false, false, false}; // For ids 1 to 4 (using 6 for safety)

  for (int id = 1; id <= 4; id++) {
    UINT mouseBtn = g_mouseButtonKeybinds[id].load(std::memory_order_acquire);
    if (mouseBtn == 0) {
      wasPressed[id] = false;
      continue; // Not a mouse button hotkey
    }

    UINT mod = g_mouseButtonModifiers[id].load(std::memory_order_acquire);

    // Map mouse button codes to virtual key codes for polling
    UINT vk = 0;
    switch (mouseBtn) {
      case 0x01: vk = VK_LBUTTON; break;
      case 0x02: vk = VK_RBUTTON; break;
      case 0x04: vk = VK_MBUTTON; break;
      case 0x05: vk = VK_XBUTTON1; break;
      case 0x06: vk = VK_XBUTTON2; break;
      default: continue;
    }

    // Check if mouse button is pressed
    bool isBtnPressed = (GetAsyncKeyState(vk) & 0x8000) != 0;

    // Check modifiers
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    bool ctrlRequired = (mod & MOD_CONTROL) != 0;
    bool shiftRequired = (mod & MOD_SHIFT) != 0;
    bool altRequired = (mod & MOD_ALT) != 0;

    bool modsMatch = (ctrlRequired == ctrlPressed && shiftRequired == shiftPressed && altRequired == altPressed);

    if (isBtnPressed && modsMatch) {
      // Button and modifiers match - trigger only if not already pressed
      if (!wasPressed[id]) {
        wasPressed[id] = true;
        PostMessage(hWnd, WM_HOTKEY, id, 0);
      }
    } else {
      // If either button is released OR modifiers don't match, reset state
      // (Wait until all required keys are fully released/mismatched to allow re-trigger, or just button release)
      if (!isBtnPressed || !modsMatch) {
        wasPressed[id] = false;
      }
    }
  }
}

// HUD Window Procedure
LRESULT CALLBACK HUDWndProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
    RefreshHotkeys(hWnd);
    // Set up a timer to check mouse button hotkeys
    SetTimer(hWnd, 1, 50, NULL); // Check every 50ms
    // Initialize system tray icon when window is fully created
    AddSystrayIcon(hWnd, GetModuleHandle(NULL));
    LOG_INFO("System tray icon added from WM_CREATE");
    return 0;

  case WM_APP + 1:
    // Fortnite focus changed: re-evaluate which hotkeys should be registered.
    // Force = true because the keybinds themselves haven't changed.
    RefreshHotkeys(hWnd, /*force=*/true);
    return 0;

  case WM_HOTKEY:
    // Ignore hotkey actions when user is assigning a keybind in settings
    if (g_keybindAssignmentActive) {
      return 0;
    }
    switch (wParam) {
    case 1: // Toggle Panel
      ShowControlPanel();
      break;
    case 2: // ROI Select Toggle
      if (g_currentSelection == NONE) {
        // Only allow ROI selection when Fortnite is focused
        if (!IsFortniteForeground()) {
          LOG_INFO("ROI selection blocked: Fortnite not focused");
          break;
        }
        LOG_INFO("ROI selection starting: Capturing desktop snapshot");
        CaptureDesktop(); // Capture before dimming
        LOG_INFO("ROI selection: Desktop captured, transitioning to SELECTING_ROI state");
        g_currentSelection = SELECTING_ROI;
        g_isSelectionActive = true;
        long exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TRANSPARENT;
        SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);
        SetForegroundWindow(hWnd);
        LOG_TRACE("ROI selection: Selection window activated");
      } else {
        // Save the current ROI rectangle if valid before exiting selection
        if (!g_allProfiles.empty() &&
            g_selectionRect.right > g_selectionRect.left &&
            g_selectionRect.bottom > g_selectionRect.top) {
          Profile &p = g_allProfiles[g_selectedProfileIdx];
          RECT mRect = GetMonitorRectByIndex(g_screenIndex);
          p.roi_x = g_selectionRect.left - mRect.left;
          p.roi_y = g_selectionRect.top - mRect.top;
          p.roi_w = g_selectionRect.right - g_selectionRect.left;
          p.roi_h = g_selectionRect.bottom - g_selectionRect.top;
          // Keep existing target_color unchanged
          p.Save(GetProfilesPath() + p.name + L".json");
          p.Save(GetProfilesPath() + L"last_calibrated.json");
        }
        g_currentSelection = NONE;
        g_isSelectionActive = false;
        if (g_screenSnapshot) {
          DeleteObject(g_screenSnapshot);
          g_screenSnapshot = NULL;
        }
        SetWindowLong(hWnd, GWL_EXSTYLE,
                      GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        InvalidateRect(hWnd, NULL, FALSE);
        g_forceRedraw = true;
      }
      break;
    case 3:
      g_showCrosshair = !g_showCrosshair;
      g_forceRedraw = true;
      if (!g_allProfiles.empty()) {
        g_allProfiles[g_selectedProfileIdx].showCrosshair = g_showCrosshair;
        g_allProfiles[g_selectedProfileIdx].Save(
            GetProfilesPath() + g_allProfiles[g_selectedProfileIdx].name +
            L".json");
      }
      SaveSettings();
      NotifyBackendCrosshairChanged();
      // Acoustic Cue: High beep for ON, Low beep for OFF
      if (g_showCrosshair) Beep(750, 50);
      else Beep(500, 50);
      break;
    case 4:
      g_currentAngle = 0.0f;
      g_logic.SetZero();
      // Acoustic Cue: Premium reset chime
      Beep(1000, 80);
      break;
    }
    return 0;

  case WM_TRAYICON:
    // NOTIFYICON_VERSION_4 changes the messages sent in lParam.
    // Right-click sends WM_CONTEXTMENU. Left-click sends NIN_SELECT.
    if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) {
      ShowTrayContextMenu(hWnd);
    } else if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT || LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) {
      ShowControlPanel();
    }
    return 0;

  case WM_COMMAND:
    if (LOWORD(wParam) == ID_TRAY_EXIT) {
      SendMessage(hWnd, WM_CLOSE, 0, 0);
    }
    return 0;

  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE && g_currentSelection != NONE) {
      LOG_INFO("Selection cancelled via ESCAPE");
      g_currentSelection = NONE;
      g_isSelectionActive = false;
      if (g_screenSnapshot) {
        DeleteObject(g_screenSnapshot);
        g_screenSnapshot = NULL;
      }
      SetWindowLong(hWnd, GWL_EXSTYLE,
                    GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
      InvalidateRect(hWnd, NULL, FALSE);
      g_forceRedraw = true;
      return 0; // Only consume the key when we actually handled it
    }
    break; // Pass all other keys through to DefWindowProc
  case WM_LBUTTONDOWN:
    if (g_currentSelection == SELECTING_ROI) {
      POINT cur;
      GetCursorPos(&cur);
      g_startPoint = cur;
      g_selectionRect = {cur.x, cur.y, cur.x, cur.y};

      // Auto-detect monitor from start point to ensure offsets are correct
      HMONITOR hMon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi = {sizeof(mi)};
      if (GetMonitorInfo(hMon, &mi)) {
        // Find which index this monitor matches in our list
        for (int i = 0, monCount = GetSystemMetrics(SM_CMONITORS); i < monCount; i++) {
          RECT r = GetMonitorRectByIndex(i);
          if (r.left == mi.rcMonitor.left && r.top == mi.rcMonitor.top) {
            if (g_screenIndex != i) {
              LOG_INFO("Auto-switched g_screenIndex to %d based on selection start point", i);
              g_screenIndex = i;
              g_displayChangeGen++; // Force cache refresh
            }
            break;
          }
        }
      }
    } else if (g_currentSelection == SELECTING_COLOR) {
      LOG_INFO("Stage 2 LBUTTONDOWN executed");
      // STAGE 2: PRECISION COLOR PICK (from captured screenshot)
      LOG_INFO("Stage 2 LBUTTONDOWN: Starting to sample color from screenshot");

      COLORREF sampledColor = CLR_INVALID;  // Track if sampling succeeded

      if (!g_screenSnapshot) {
        LOG_ERROR("CRITICAL BUG: g_screenSnapshot is NULL! Color sampling aborted.");
      } else {
        LOG_TRACE("g_screenSnapshot valid at Stage 2 start");
        HDC hdcScreen = GetDC(NULL);
        if (!hdcScreen) {
          LOG_ERROR("Failed to get screen DC");
        } else {
          LOG_TRACE("Screen DC acquired successfully");

          HDC hdcMem = CreateCompatibleDC(hdcScreen);
          if (!hdcMem) {
            LOG_ERROR("Failed to create compatible DC");
            ReleaseDC(NULL, hdcScreen);
          } else {
            LOG_TRACE("Memory DC created successfully");
            HGDIOBJ hOld = SelectObject(hdcMem, g_screenSnapshot);
            if (!hOld || hOld == INVALID_HANDLE_VALUE) {
              LOG_ERROR("Failed to select bitmap into memory DC");
              DeleteDC(hdcMem);
              ReleaseDC(NULL, hdcScreen);
            } else {
              LOG_TRACE("Bitmap selected into memory DC successfully");

              int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
              int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
              LOG_TRACE("Virtual screen offset: sx=%d, sy=%d", sx, sy);

              // Get monitor offset - critical for multi-monitor setups
              RECT mRect = GetMonitorRectByIndex(g_screenIndex);
              LOG_TRACE("Monitor rect: left=%d, top=%d, right=%d, bottom=%d",
                        mRect.left, mRect.top, mRect.right, mRect.bottom);

              POINT cur;
              GetCursorPos(&cur);
              LOG_TRACE("Cursor position: x=%d, y=%d", cur.x, cur.y);

              // Adjust color sample coord: account for virtual screen offset
              // CaptureDesktop bitblts (sx, sy) to (0, 0)
              int bitmapX = cur.x - sx;
              int bitmapY = cur.y - sy;
              LOG_TRACE("Bitmap coordinates for GetPixel: x=%d, y=%d (absolute desktop mapping)", bitmapX, bitmapY);

              COLORREF pixel = GetPixel(hdcMem, bitmapX, bitmapY);
              if (pixel == CLR_INVALID) {
                LOG_ERROR("GetPixel returned CLR_INVALID! Coordinates may be out of bounds.");
              } else {
                LOG_TRACE("GetPixel succeeded, color=0x%06lX", pixel);
                sampledColor = pixel;  // Mark as successfully sampled
              }

              SelectObject(hdcMem, hOld);
              DeleteDC(hdcMem);
              ReleaseDC(NULL, hdcScreen);
              LOG_TRACE("Color sampling complete.");
            }
          }
        }
      }

      // Only update profile if color sampling succeeded
      if (sampledColor != CLR_INVALID) {
        g_pickedColor = sampledColor;
        g_targetColor = sampledColor;
        LOG_INFO("Color set: g_pickedColor=0x%06lX, g_targetColor=0x%06lX", g_pickedColor, g_targetColor);
      } else {
        LOG_ERROR("Color sampling FAILED - will not update profile to prevent saving invalid color");
      }

      // Finalize and Exit Selection
      LOG_INFO("Resetting selection state...");
      g_currentSelection = NONE;
      g_isSelectionActive = false;
      if (g_screenSnapshot) {
        DeleteObject(g_screenSnapshot);
        g_screenSnapshot = NULL;
      }
      SetWindowLong(hWnd, GWL_EXSTYLE,
                    GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
      InvalidateRect(hWnd, NULL, FALSE);
      g_forceRedraw = true;
      LOG_INFO("Stage 2 Redraw Forced Handle Cleaned.");

      // Only save profile if color sampling was successful
      if (sampledColor != CLR_INVALID && !g_allProfiles.empty()) {
        Profile &p = g_allProfiles[g_selectedProfileIdx];
        RECT mRect = GetMonitorRectByIndex(g_screenIndex);
        p.target_color = g_pickedColor;
        p.roi_x = g_selectionRect.left - mRect.left;
        p.roi_y = g_selectionRect.top - mRect.top;
        p.roi_w = g_selectionRect.right - g_selectionRect.left;
        p.roi_h = g_selectionRect.bottom - g_selectionRect.top;

        // Save to the actual profile path
        std::wstring profilePath = GetProfilesPath() + p.name + L".json";
        LOG_INFO("Calling Save to profilePath");
        p.Save(profilePath);
        LOG_INFO("Save complete");

        // Also maintain the legacy 'last_calibrated' for quick-load logic if
        // needed
        p.Save(GetProfilesPath() + L"last_calibrated.json");
      } else if (sampledColor == CLR_INVALID) {
        LOG_INFO("Profile NOT saved - color sampling failed, preventing invalid data from being stored");
      }
    }
    return 0;

  case WM_MOUSEMOVE:
    if (g_currentSelection != NONE) {
      if (g_currentSelection == SELECTING_ROI && (wParam & MK_LBUTTON)) {
        POINT cur;
        GetCursorPos(&cur);
        g_selectionRect.left = (std::min)(g_startPoint.x, cur.x);
        g_selectionRect.right = (std::max)(g_startPoint.x, cur.x);
        g_selectionRect.top = (std::min)(g_startPoint.y, cur.y);
        g_selectionRect.bottom = (std::max)(g_startPoint.y, cur.y);
      }
      InvalidateRect(hWnd, NULL, FALSE);
    }
    return 0;

  case WM_LBUTTONUP:
    if (g_currentSelection == SELECTING_ROI) {
      // Allow transition to color selection even when Fortnite not focused
      // (safe switch for selection process)
      LOG_INFO("Stage 1 complete: Transitioning from SELECTING_ROI to SELECTING_COLOR");
      LOG_TRACE("ROI rectangle: left=%d, top=%d, right=%d, bottom=%d",
                g_selectionRect.left, g_selectionRect.top,
                g_selectionRect.right, g_selectionRect.bottom);
      LOG_TRACE("g_screenSnapshot at transition: %p", g_screenSnapshot);
      g_currentSelection = SELECTING_COLOR;
      InvalidateRect(hWnd, NULL, FALSE);
    }
    return 0;

  case WM_TIMER: {
    if (wParam == 3) {
      KillTimer(hWnd, 3);
      ShowWindow(hWnd, SW_SHOW);
      SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      UpdateWindow(hWnd);
      return 0;
    }
    if (wParam == 1) { // 60fps HUD / Input processing timer
      CheckMouseButtonHotkeys(hWnd);

      static ULONGLONG s_bootTime = GetTickCount64();
      if (GetTickCount64() - s_bootTime < 2500)
        return 0;

      if (g_currentSelection == NONE) {
        bool lDown = g_physicalKeys[VK_LBUTTON];
        bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        POINT pt;
        GetCursorPos(&pt);

        // Use the event-hook cache consistently (same source as overlay
        // visibility at line 918) to prevent the WS_EX_TRANSPARENT flag
        // from racing against the overlay's suspend/show decision.
        bool fnFocused = g_fortniteFocusedCache.load();
        // Dragging the HUD requires holding Ctrl to prevent accidental drags
        bool canDrag = ctrlDown;

        if (lDown && !g_isDraggingHUD && canDrag) {
          // g_hudX/g_hudY are monitor-relative (the HUD is drawn into a layered
          // window positioned at the active monitor's origin). GetCursorPos is
          // absolute virtual-desktop space, so convert the cursor into the same
          // monitor-relative space before hit-testing — otherwise the test only
          // succeeds on the primary monitor (origin 0,0) and the HUD can't be
          // grabbed on any secondary monitor. Computed here (only on a Ctrl+grab
          // attempt) rather than every tick so the monitor enumeration is rare.
          // The drag delta math below uses absolute deltas, so it is unaffected.
          RECT hudMon = GetMonitorRectByIndex(g_screenIndex);
          int relX = pt.x - hudMon.left;
          int relY = pt.y - hudMon.top;
          if (relX >= g_hudX && relX <= g_hudX + 260 && relY >= g_hudY &&
              relY <= g_hudY + 150) {
            g_isDraggingHUD = true;
            g_dragStartMouse = pt;
            g_dragStartHUD.x = g_hudX;
            g_dragStartHUD.y = g_hudY;
          }
        } else if (!lDown && g_isDraggingHUD) {
          g_isDraggingHUD = false;
          SaveSettings();
        }

        if (g_isDraggingHUD && lDown) {
          int newX = g_dragStartHUD.x + (pt.x - g_dragStartMouse.x);
          int newY = g_dragStartHUD.y + (pt.y - g_dragStartMouse.y);
          int deltaX = newX - g_hudX;
          int deltaY = newY - g_hudY;
          g_hudX = newX;
          g_hudY = newY;

          // The dashboard is no longer dragged locally along with the HUD.

          InvalidateRect(hWnd, NULL, FALSE);
        }

        // Click-through logic: only remove WS_EX_TRANSPARENT when we actually
        // need mouse events. In every other case (including when Fortnite is not
        // focused) keep the overlay click-through so it can't intercept input
        // from other apps and accidentally steal keyboard focus.
        bool needMouseEvents = g_isDraggingHUD ||
                               (g_currentSelection != NONE) ||
                               (fnFocused && ctrlDown);
        long ex = GetWindowLong(hWnd, GWL_EXSTYLE);
        if (needMouseEvents) {
          if (ex & WS_EX_TRANSPARENT)
            SetWindowLong(hWnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
        } else {
          if (!(ex & WS_EX_TRANSPARENT))
            SetWindowLong(hWnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        }
      }

      g_isCursorVisible = IsCursorCurrentlyVisible();
      float ang = g_logic.GetAngle();

      // Direct assignment: no smoothing. The angle value reflects the raw
      // mouse-delta accumulator on every frame for zero perceived delay.
      g_interpolatedAngle = ang;

      // Update tray tooltip with current angle (~2/s, no need to spam NIM_MODIFY)
      {
        static ULONGLONG s_lastTrayTip = 0;
        ULONGLONG nowMs = GetTickCount64();
        if (nowMs - s_lastTrayTip >= 500) {
          s_lastTrayTip = nowMs;
          UpdateTrayTooltip(hWnd, ang);
        }
      }

      // Clear the forced redraw flag occasionally set elsewhere
      g_forceRedraw.store(false);

      // Suspend the overlay (HUD + crosshair) when Fortnite is not the
      // foreground window. The dashboard panel being open (not minimized)
      // also keeps it visible so the user can reposition the HUD or pick
      // crosshair colors.  IsWindowVisible returns TRUE for minimized
      // windows, so we also check !IsIconic to exclude that case.
      bool fortniteFocused = g_fortniteFocusedCache.load();
      bool overlayVisible = fortniteFocused || g_isDraggingHUD ||
                            (g_currentSelection != NONE);

      // Throttle overlay redraws to ~10fps when Fortnite is not the foreground window.
      // 100fps UpdateLayeredWindow calls cause DWM contention during Alt+Tab.
      bool throttle = !fortniteFocused && g_currentSelection == NONE;
      static ULONGLONG s_lastThrottledDraw = 0;
      ULONGLONG nowMs = GetTickCount64();

      if (!throttle || (nowMs - s_lastThrottledDraw >= 100)) {
        if (throttle) s_lastThrottledDraw = nowMs;
        DrawOverlay(hWnd, g_interpolatedAngle.load(), g_showCrosshair,
                    overlayVisible);
      }
    } else if (wParam == 2) { // 30s Auto-Save Periodic Timer
      SaveSettings();
      if (!g_allProfiles.empty() &&
          g_selectedProfileIdx < (int)g_allProfiles.size()) {
        g_allProfiles[g_selectedProfileIdx].Save(
            GetProfilesPath() + g_allProfiles[g_selectedProfileIdx].name +
            L".json");
      }
    }
    return 0;
  }

  case WM_USER + 101: {
    int newScreenIndex = (int)wParam;
    if (newScreenIndex != g_screenIndex) {
      // Blank the layered surface at the OLD monitor position first.
      // UpdateLayeredWindow has already committed pixels there; SW_HIDE alone
      // doesn't flush the DWM surface in time, leaving a ghost copy behind.
      // Pushing a zero-alpha frame while g_screenIndex still points to the old
      // monitor tells DWM to clear that surface before we move the HWND.
      DrawOverlay(hWnd, 0.0, false, false);

      g_screenIndex = newScreenIndex;

      ShowWindow(hWnd, SW_HIDE);

      RECT mRect = GetMonitorRectByIndex(g_screenIndex);
      int screenW = mRect.right - mRect.left;
      int screenH = mRect.bottom - mRect.top;
      SetWindowPos(hWnd, HWND_TOPMOST, mRect.left, mRect.top, screenW, screenH,
                   SWP_NOACTIVATE);

      ShowWindow(hWnd, SW_SHOWNOACTIVATE);
      g_forceRedraw = true;
    }
    return 0;
  }

  // Multi-Monitor Hot-Plug Detection (ported from v5.5.153)
  // When a monitor is plugged in or unplugged, Windows sends WM_DISPLAYCHANGE.
  // We auto-track Fortnite's monitor and resize the overlay to match.
  case WM_DISPLAYCHANGE: {
    int oldScreenIndex = g_screenIndex;
    // Auto-track Fortnite's monitor: hot-plugging a 2nd monitor can renumber
    // monitor indices. Find Fortnite's current monitor and update g_screenIndex.
    HWND fnWnd = FindWindowW(NULL, L"Fortnite  ");
    if (!fnWnd) fnWnd = FindWindowW(NULL, L"Fortnite");
    if (fnWnd && IsWindow(fnWnd)) {
      HMONITOR hFnMon = MonitorFromWindow(fnWnd, MONITOR_DEFAULTTONEAREST);
      struct FindData { HMONITOR target; int currentIndex; int foundIndex; };
      FindData data = {hFnMon, 0, -1};
      EnumDisplayMonitors(NULL, NULL,
        [](HMONITOR h, HDC, LPRECT, LPARAM dwData) -> BOOL {
          auto *d = reinterpret_cast<FindData *>(dwData);
          if (h == d->target) { d->foundIndex = d->currentIndex; return FALSE; }
          d->currentIndex++;
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&data));
      if (data.foundIndex >= 0) g_screenIndex = data.foundIndex;
    }

    if (g_screenIndex != oldScreenIndex) {
      // Blank the old layered surface before moving to prevent a ghost copy
      // being left on the unplugged/renumbered monitor. Same sequence as
      // WM_USER+101 (drag/dropdown path).
      DrawOverlay(hWnd, 0.0, false, false);
      ShowWindow(hWnd, SW_HIDE);
    }

    RECT mRect = GetMonitorRectByIndex(g_screenIndex);
    int screenW = mRect.right - mRect.left;
    int screenH = mRect.bottom - mRect.top;
    SetWindowPos(hWnd, HWND_TOPMOST, mRect.left, mRect.top, screenW, screenH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hWnd, NULL, FALSE);
    return 0;
  }

  case WM_SYSCOMMAND:
    // Block F10 from opening the system menu (interferes with Fn+F10 keybind)
    if ((wParam & 0xFFF0) == SC_KEYMENU)
      return 0;
    break;

  case WM_CLOSE:
    g_running = false;
    DestroyWindow(hWnd);
    return 0;

  case WM_DESTROY:
    g_running = false;
    RemoveSystrayIcon(hWnd);
    QCoreApplication::exit(0);
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// WinMain...

#pragma comment(lib, "winmm.lib")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  // Phase -2: Single Instance Guard (v5.5.75)
  // Create a named mutex to ensure only one instance of the app is running.
  // The mutex name must be unique to the application.
  HANDLE hMutex =
      CreateMutexW(NULL, TRUE, L"BetterAnglePro_SingleInstance_Mutex");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(NULL,
                L"BetterAngle is already running.\n\nCheck your system tray to "
                L"open the dashboard.",
                L"Application Already Running", MB_OK | MB_ICONINFORMATION);
    return 0;
  }

  // Phase -1: Ultra-Fast Timer Precision (v5.1.19)
  timeBeginPeriod(1);
  // Phase -1: DPI Awareness (CRITICAL for multi-monitor alignment)
  // We need Per-Monitor Awareness V2 to ensure that GetSystemMetrics and
  // EnumDisplayMonitors return physical pixels, matching the game's coordinate
  // space exactly.
  BOOL dpiSet = FALSE;
  HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
  if (hUser32) {
    typedef BOOL(WINAPI *
                 SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    auto setDpiContext = (SetProcessDpiAwarenessContextProc)GetProcAddress(
        hUser32, "SetProcessDpiAwarenessContext");
    if (setDpiContext) {
      if (setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        dpiSet = TRUE;
      }
    }
  }
  if (!dpiSet) {
    SetProcessDPIAware();
  }
  InitEnhancedLogging();
  LOG_INFO("WinMain entered");

  int argc = 1;
  char *argv[] = {(char *)"BetterAngle.exe", nullptr};
  QGuiApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(
      false); // Prevent premature exit if windows are still initializing

  // Phase 0: Kick off version check in background — never blocks startup.
  // g_updateAvailable will be set when done; the control panel UPDATES tab
  // shows it.
  std::thread([]() { CheckForUpdates(); }).detach();

  GdiplusStartupInput gdiplusStartupInput;
  GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

  LoadSettings();
  SetLogLevel(LogLevel::Info);
  LogStartup();
  CleanupUpdateJunk();

  g_allProfiles = GetProfiles(GetProfilesPath());
  if (g_allProfiles.empty()) {
    Profile p;
    p.name = L"Default";
    p.tolerance = 2;
    p.sensitivityX = 0.1;
    p.sensitivityY = 0.1;
    // roi_x/y/w/h left at 0: user must run the ROI selector before
    // the detection zone is shown. This avoids a confusing default box.
    p.crossThickness = 1.0f;
    p.crossColor = RGB(255, 0, 0);
    p.Save(GetProfilesPath() + L"Default.json");

    g_allProfiles.push_back(p);
  }

  // Sensitivity is loaded from the JSON profile; Do not blindly overwrite it
  // here.
  if (g_selectedProfileIdx >= (int)g_allProfiles.size()) {
    g_selectedProfileIdx = 0;
  }
  g_currentProfile = g_allProfiles[g_selectedProfileIdx];

  g_selectedProfileIdx = 0;
  bool foundProfile = false;
  for (size_t i = 0; i < g_allProfiles.size(); i++) {
    if (g_allProfiles[i].name == g_lastLoadedProfileName) {
      g_selectedProfileIdx = i;
      foundProfile = true;
      break;
    }
  }

  // Safety: If last profile not found, fall back to what was in settings.json
  // index if that index is valid.
  if (!foundProfile && g_selectedProfileIdx < (int)g_allProfiles.size()) {
    // Keep original g_selectedProfileIdx loaded from settings.json
  } else if (!foundProfile) {
    g_selectedProfileIdx = 0;
  }

  if (g_lastLoadedProfileName.empty() && !g_allProfiles.empty()) {
    g_lastLoadedProfileName = g_allProfiles[0].name;
  }

  g_currentProfile = g_allProfiles[g_selectedProfileIdx];

  // Sync Crosshair Settings from Profile to Global State
  g_crossThickness = g_currentProfile.crossThickness;
  g_crossColor = g_currentProfile.crossColor;
  
  // Only force center if it's a completely fresh start with no history
  if (g_lastLoadedProfileName.empty()) {
    g_crossOffsetX = 0.0f;
    g_crossOffsetY = 0.0f;
  } else {
    g_crossOffsetX = g_currentProfile.crossOffsetX;
    g_crossOffsetY = g_currentProfile.crossOffsetY;
  }
  
  g_crossAngle = g_currentProfile.crossAngle;
  g_crossPulse = g_currentProfile.crossPulse;
  g_showCrosshair = g_currentProfile.showCrosshair;

  // Sync monitor index from profile BEFORE using it to offset ROI coords
  g_screenIndex = g_currentProfile.screenIndex;

  // Sync Trigger Calibration from Profile to Global State
  // Sync Trigger Calibration from Profile to Global State
  RECT mRect = GetMonitorRectByIndex(g_screenIndex);
  g_selectionRect.left = g_currentProfile.roi_x + mRect.left;
  g_selectionRect.top = g_currentProfile.roi_y + mRect.top;
  g_selectionRect.right =
      g_currentProfile.roi_x + g_currentProfile.roi_w + mRect.left;
  g_selectionRect.bottom =
      g_currentProfile.roi_y + g_currentProfile.roi_h + mRect.top;
  g_targetColor = g_currentProfile.target_color;

  g_logic.LoadProfile(g_currentProfile.sensitivityX);

  // Hotkeys are registered exclusively in HUDWndProc WM_CREATE.
  // NULL-window registration would steal WM_HOTKEY messages before HUD can
  // handle them.

  // Message Window for Raw Input (Bypasses Layered Window UI Bugs)
  WNDCLASS wcMsg = {0};
  wcMsg.lpfnWndProc = MsgWndProc;
  wcMsg.hInstance = hInstance;
  wcMsg.lpszClassName = L"BetterAngleMsgWnd";
  RegisterClass(&wcMsg);
  HWND hMsgWnd = CreateWindowEx(0, L"BetterAngleMsgWnd", NULL, 0, 0, 0, 0, 0,
                                HWND_MESSAGE, NULL, hInstance, NULL);
  g_hMsgWnd = hMsgWnd;
  RegisterRawMouse(hMsgWnd);
  StartPollingThread(); // Hardware Polling: Sees through BlockInput
  LOG_INFO("Raw input message window created: hwnd=0x%p", hMsgWnd);

  // Phase 2: Create Control Panel (Interactive) via Qt
  g_hPanel = CreateControlPanel(hInstance);
  LOG_INFO("Control panel created: hwnd=0x%p", g_hPanel);
  LogWindowInfo(L"Control panel handle", g_hPanel);

  // Hidden owner window — owned popups are excluded from Alt+Tab AND
  // Win+Tab (Task View) by the Windows shell.  Without this the HUD
  // overlay appears as a separate thumbnail in the task switcher.
  WNDCLASS wcOwner = {0};
  wcOwner.lpfnWndProc = DefWindowProc;
  wcOwner.hInstance = hInstance;
  wcOwner.lpszClassName = L"BetterAngleHUDOwner";
  RegisterClass(&wcOwner);
  HWND hHUDOwner = CreateWindowEx(
      WS_EX_TOOLWINDOW, L"BetterAngleHUDOwner", L"",
      WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
  // Owner must be "visible" for Windows to suppress the owned window
  // from Task View.  Zero-sized popup is invisible to the user but
  // satisfies the shell's ownership chain requirement.
  ShowWindow(hHUDOwner, SW_SHOWNOACTIVATE);

  // Phase 3: Create HUD Window (Transparent Overlay)
  WNDCLASS wc = {0};
  wc.lpfnWndProc = HUDWndProc;
  wc.hInstance = hInstance;
  wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = L"BetterAngleHUD";
  RegisterClass(&wc);

  // Use selected monitor's RECT for the HUD window (v5.5.76)
  int screenW = mRect.right - mRect.left;
  int screenH = mRect.bottom - mRect.top;
  int screenX = mRect.left;
  int screenY = mRect.top;

  // WS_EX_NOACTIVATE: overlay must never steal keyboard focus, even when
  // WS_EX_TRANSPARENT is temporarily removed for HUD dragging/ROI selection.
  DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
  if (!g_diagNoTopmost.load()) {
    exStyle |= WS_EX_TOPMOST;
  }
  
  // Add WS_EX_TOOLWINDOW just to be extra safe
  exStyle |= WS_EX_TOOLWINDOW;

  g_hHUD = CreateWindowEx(
      exStyle,
      L"BetterAngleHUD", L"BetterAngle HUD", WS_POPUP, screenX, screenY,
      screenW, screenH, hHUDOwner, NULL, hInstance, NULL);

  LOG_INFO("HUD created: hwnd=0x%p", g_hHUD);
  LogWindowInfo(L"HUD handle", g_hHUD);
  ShowControlPanel(); // Force Dashboard to show on startup
  
  if (!g_diagNoTopmost.load()) {
    SetWindowPos(g_hHUD, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    SetWindowPos(g_hHUD, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  
  UpdateWindow(g_hHUD);
  SetTimer(g_hHUD, 1, 10, NULL);    // 100fps (~10ms) Repaint Timer
  SetTimer(g_hHUD, 2, 30000, NULL); // 30s Auto-Save Timer

  // Startup monitor auto-detection: if Fortnite is already running when
  // BetterAngle launches, snap the HUD to its monitor immediately.
  {
    HWND fnWnd = FindWindowW(NULL, L"Fortnite  ");
    if (!fnWnd) fnWnd = FindWindowW(NULL, L"Fortnite");
    if (fnWnd && IsWindow(fnWnd)) {
      HMONITOR hFnMon = MonitorFromWindow(fnWnd, MONITOR_DEFAULTTONEAREST);
      struct FindData { HMONITOR target; int cur; int found; };
      FindData fd = {hFnMon, 0, -1};
      EnumDisplayMonitors(NULL, NULL,
        [](HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL {
          auto *d = reinterpret_cast<FindData *>(p);
          if (h == d->target) { d->found = d->cur; return FALSE; }
          d->cur++;
          return TRUE;
        }, reinterpret_cast<LPARAM>(&fd));
      if (fd.found >= 0 && fd.found != g_screenIndex) {
        g_screenIndex = fd.found;
        RECT mRect = GetMonitorRectByIndex(g_screenIndex);
        SetWindowPos(g_hHUD, HWND_TOPMOST, mRect.left, mRect.top,
                     mRect.right - mRect.left, mRect.bottom - mRect.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        LOG_INFO("Startup monitor auto-detect: Fortnite on monitor %d", g_screenIndex);
      }
    }
  }

  std::thread detThread(DetectorThread);
  std::thread perfThread(PerformanceMonitorThread);

  g_hWinEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                    NULL, WinEventProc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  // Run Qt Event Loop
  int exitCode = app.exec();
  LOG_INFO("Qt event loop exited with code=%d", exitCode);

  g_running = false;
  if (g_hWinEventHook) {
    UnhookWinEvent(g_hWinEventHook);
  }
  if (detThread.joinable())
    detThread.join();
  if (perfThread.joinable())
    perfThread.join();

  // Final Save on Exit
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.crossPulse = g_crossPulse;
    p.Save(GetProfilesPath() + p.name + L".json");
  }

  SaveSettings();

  GdiplusShutdown(g_gdiplusToken);
  ShutdownEnhancedLogging();

  if (hMutex) {
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
  }

  return exitCode;
}

// Performance Monitoring Thread - Logs metrics to perf.log every 5 seconds
void PerformanceMonitorThread() {
  // Initialize the performance log file
  std::wstring perfLogPath = GetAppRootPath() + L"logs\\perf.log";
  PerformanceLogger::Instance().Initialize(perfLogPath);
  LOG_INFO("Performance monitor thread started.");

  while (g_running) {
    // Collect metrics
    PROCESS_MEMORY_COUNTERS pmc;
    double ramMb = 0.0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
      ramMb = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    }

    // Metrics: CPU (Scanner), RAM, Scan Latency, HUD FPS
    PerformanceLogger::Instance().LogMetrics(
        (double)g_scannerCpuPct.load(), ramMb, (int)g_detectionDelayMs.load(),
        0 // FPS tracking removed for stability
    );

    // Wait 5 seconds
    for (int i = 0; i < 50 && g_running; i++) {
      Sleep(100);
    }
  }
}
