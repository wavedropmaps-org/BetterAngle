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

// Helper function to flush pending input messages before blocking
static void FlushPendingInputMessages() {
  MSG msg;
  // Remove all pending keyboard and mouse messages from the queue
  while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
  }
  while (PeekMessageW(&msg, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
  }
  // Also flush any other input messages
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
  }
}

// High-frequency thread to detect Fortnite focus changes instantly (Alt-Tab
// detection)
void FocusMonitorThread() {
  bool lastFortniteFocused = false;
  while (g_running) {
    bool currentFortniteFocused = IsFortniteForeground();
    g_fortniteFocusedCache = currentFortniteFocused;

    // Detect Alt-Tab back into Fortnite with ultra-low latency (1ms polling)
    if (!lastFortniteFocused && currentFortniteFocused) {
      // ALT-TAB COOLDOWN: BlockInput locks the mouse at the OS level so
      // physical mouse movement during the focus switch can't affect
      // Fortnite's FOV. Uses same Shock & Restore pattern as glide/dive.
      g_mouseSuspendedUntil = GetTickCount64() + 400;
      g_lockTriggerReason = 3; // Alt-Tab Return
      g_lockCount++;

      std::thread([]() {
        std::lock_guard<std::mutex> lock(g_lockMutex);
        g_lockInProgress = true;
        g_lockThreadId = GetCurrentThreadId();

        for (int i = 0; i < 256; i++) {
          g_rawKeyUpDetected[i] = false;
          g_rawKeyMakeDetected[i] = false;
        }
        auto initialState = GetGamingKeyState(); // Pre-lock snapshot

        {
          std::lock_guard<std::mutex> bLock(g_blockInputMutex);
          g_blockInputActive = true;
          BlockInput(TRUE);
          Sleep(400);
          BlockInput(FALSE);
          g_blockInputActive = false;
        }

        SyncGamingKeysNitro(initialState); // Shock & Restore

        g_lockInProgress = false;
      }).detach();

      LOG_INFO("Alt-tab cooldown active (400ms BlockInput + Nitro sync)");
    }
    lastFortniteFocused = currentFortniteFocused;
    Sleep(0); // Max CPU performance for lightning fast focus detection
  }
}

// FOV Detector Thread - Now focused solely on ROI scanning
void DetectorThread() {
  bool lastDiving = false;
  ULONGLONG peakMatchTimestamp = 0;

  while (g_running) {
    if (!g_allProfiles.empty() && g_currentSelection == NONE) {
      Profile &p = g_allProfiles[g_selectedProfileIdx];
      g_logic.LoadProfile(p.sensitivityX);
      g_requiredMatchCount =
          (int)((p.diveGlideMatch / 100.0f) * (p.roi_w * p.roi_h));

      bool currentFortniteFocused = g_fortniteFocusedCache.load();
      g_isCursorVisible = IsCursorCurrentlyVisible();

      // Only scan ROI when Fortnite is the foreground window
      if (currentFortniteFocused) {
        RECT mRect = GetMonitorRectByIndex(g_screenIndex);
        RoiConfig cfg = {
            p.roi_x + mRect.left, p.roi_y + mRect.top, p.roi_w, p.roi_h,
            p.target_color,       p.tolerance};
        ULONGLONG startMs = GetTickCount64();
        g_matchCount = g_detector.Scan(cfg);
        ULONGLONG endMs = GetTickCount64();
        ULONGLONG scanMs = endMs - startMs;
        g_detectionDelayMs = scanMs;

        // Scanner CPU %: time spent scanning vs total loop period
        int cpuPct = (scanMs > 0) ? (int)((scanMs * 100) / (scanMs + 10)) : 0;
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
      } else {
        // Fortnite not focused, reset detection to 0
        g_matchCount = 0;
        g_detectionDelayMs = 0;
        g_scannerCpuPct = 0;
      }

      bool nowDiving = (g_matchCount.load() >= g_requiredMatchCount.load());

      if (GetTickCount64() >= g_mouseSuspendedUntil) {
        // Edge: Gliding -> Diving (Nitro)
        if (nowDiving && !lastDiving &&
            (GetTickCount64() - g_lastLockTime > 500)) {
          g_lastLockTime = GetTickCount64();
          g_mouseSuspendedUntil = GetTickCount64() + 700;

          std::thread([]() {
            std::lock_guard<std::mutex> lock(g_lockMutex);
            g_lockInProgress = true;
            g_lockCount++;
            g_lockThreadId = GetCurrentThreadId();
            ULONGLONG start = GetTickCount64();

            for (int i = 0; i < 256; i++) {
              g_rawKeyUpDetected[i] = false;
              g_rawKeyMakeDetected[i] = false;
            }
            g_wPreLock =
                (GetAsyncKeyState('W') & 0x8000) != 0 ? (short)1 : (short)0;
            auto initialState = GetGamingKeyState(); // Pre-lock snapshot

            {
              std::lock_guard<std::mutex> lock(g_blockInputMutex);
              g_blockInputActive = true;
              BlockInput(TRUE);
              Sleep(700);
              BlockInput(FALSE);
              g_blockInputActive = false;
            }

            g_lockDurationMs = (long long)(GetTickCount64() - start);
            SyncGamingKeysNitro(initialState); // Nitro Flush + Delta sync

            g_lastLockTime = GetTickCount64();
            g_lockInProgress = false;
          }).detach();

          LOG_INFO("Transition: glide->dive, Nitro Delta sync (700ms)");
          g_lockTriggerReason = 1; // Glide → Dive
        }
        // Edge: Diving -> Gliding (Nitro)
        else if (!nowDiving && lastDiving &&
                 (GetTickCount64() - g_lastLockTime > 500)) {
          g_lastLockTime = GetTickCount64();
          g_mouseSuspendedUntil = GetTickCount64() + 1000;

          std::thread([]() {
            std::lock_guard<std::mutex> lock(g_lockMutex);
            g_lockInProgress = true;
            g_lockCount++;
            g_lockThreadId = GetCurrentThreadId();
            ULONGLONG start = GetTickCount64();

            for (int i = 0; i < 256; i++) {
              g_rawKeyUpDetected[i] = false;
              g_rawKeyMakeDetected[i] = false;
            }
            g_wPreLock =
                (GetAsyncKeyState('W') & 0x8000) != 0 ? (short)1 : (short)0;
            auto initialState = GetGamingKeyState(); // Pre-lock snapshot

            {
              std::lock_guard<std::mutex> lock(g_blockInputMutex);
              g_blockInputActive = true;
              BlockInput(TRUE);
              Sleep(1000);
              BlockInput(FALSE);
              g_blockInputActive = false;
            }

            g_lockDurationMs = (long long)(GetTickCount64() - start);
            SyncGamingKeysNitro(initialState); // Nitro Flush + Delta sync

            g_lastLockTime = GetTickCount64();
            g_lockInProgress = false;
          }).detach();

          LOG_INFO("Transition: dive->glide, Nitro Delta sync (1000ms)");
          g_lockTriggerReason = 2; // Dive → Glide
        }
      }

      // Reset UI tracker once timer expires
      if (g_mouseSuspendedUntil > 0 &&
          GetTickCount64() >= g_mouseSuspendedUntil) {
        g_mouseSuspendedUntil = 0;
      }

      lastDiving = nowDiving;
      g_isDiving = nowDiving;
      g_logic.SetDivingState(nowDiving);
    }
    Sleep(1); // CPU Fix: Drops usage from 100% to ~1%
  }
}

// Screen Snapshot for Flicker-Free Selection (v4.9.15)
void CaptureDesktop() {
  int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);

  HDC hdcScreen = GetDC(NULL);
  HDC hdcMem = CreateCompatibleDC(hdcScreen);
  if (g_screenSnapshot)
    DeleteObject(g_screenSnapshot);
  g_screenSnapshot = CreateCompatibleBitmap(hdcScreen, sw, sh);
  HGDIOBJ hOld = SelectObject(hdcMem, g_screenSnapshot);

  // Capture the entire virtual desktop
  BitBlt(hdcMem, 0, 0, sw, sh, hdcScreen, sx, sy, SRCCOPY);

  SelectObject(hdcMem, hOld);
  ReleaseDC(NULL, hdcScreen);
  DeleteDC(hdcMem);
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
bool RefreshHotkeys(HWND hWnd) {
  if (!hWnd)
    return false;

  // Cache the current keybinds to avoid unnecessary re-registration
  static Keybinds lastKeybinds = {};
  static int lastProfileIdx = -1;

  if (g_allProfiles.empty())
    return false;

  Profile &p = g_allProfiles[g_selectedProfileIdx];

  // Check if keybinds have actually changed
  bool keybindsChanged = (g_selectedProfileIdx != lastProfileIdx) ||
                         (p.keybinds.toggleMod != lastKeybinds.toggleMod ||
                          p.keybinds.toggleKey != lastKeybinds.toggleKey) ||
                         (p.keybinds.roiMod != lastKeybinds.roiMod ||
                          p.keybinds.roiKey != lastKeybinds.roiKey) ||
                         (p.keybinds.crossMod != lastKeybinds.crossMod ||
                          p.keybinds.crossKey != lastKeybinds.crossKey) ||
                         (p.keybinds.zeroMod != lastKeybinds.zeroMod ||
                          p.keybinds.zeroKey != lastKeybinds.zeroKey);

  if (!keybindsChanged) {
    // Keybinds haven't changed, no need to re-register
    return true;
  }

  // Unregister all hotkeys first
  for (int i = 1; i <= 6; i++) {
    UnregisterHotKey(hWnd, i);
  }

  // Small delay to allow system to process unregistration (optional but can
  // help)
  Sleep(10);

  // Register new hotkeys with MOD_NOREPEAT to prevent key repeat issues
  // MOD_NOREPEAT (0x4000) prevents the hotkey from firing repeatedly when held
  // down
  bool ok = true;
  std::vector<std::pair<int, std::wstring>> failedHotkeys;

  auto registerWithErrorCheck = [&](int id, UINT mod, UINT vk,
                                    const wchar_t *name) -> bool {
    if (vk == 0) {
      // Zero key means hotkey is disabled
      return true;
    }

    // Apply MOD_NOREPEAT flag
    UINT flags = mod; // Removed MOD_NOREPEAT for compat

    if (!RegisterHotKey(hWnd, id, flags, vk)) {
      DWORD err = GetLastError();
      std::wstring errorMsg = GetLastErrorString();
      failedHotkeys.push_back({id, L"Hotkey " + std::wstring(name) +
                                       L" failed: " + errorMsg + L" (Error " +
                                       std::to_wstring(err) + L")"});
      return false;
    }
    return true;
  };

  ok &= registerWithErrorCheck(1, p.keybinds.toggleMod, p.keybinds.toggleKey,
                               L"Toggle Panel");
  ok &= registerWithErrorCheck(2, p.keybinds.roiMod, p.keybinds.roiKey,
                               L"ROI Select");
  ok &= registerWithErrorCheck(3, p.keybinds.crossMod, p.keybinds.crossKey,
                               L"Crosshair Toggle");
  ok &= registerWithErrorCheck(4, p.keybinds.zeroMod, p.keybinds.zeroKey,
                               L"Zero Angle");

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
          if (g_ghostFixInProgress.load()) {
            // CONTAMINATION FIX: Skip events during Shock&Restore — our
            // synthetic SendInput calls would set Br=1 (Shock KeyUp) and
            // Mk=1 (Restore KeyDown), making the correction logic think the
            // user released and re-pressed the key. Real hardware events are
            // collected in a dedicated window after Restore completes.
          } else {
            if (raw->data.keyboard.Flags & RI_KEY_BREAK) {
              g_rawKeyUpDetected[raw->data.keyboard.VKey] = true;
            } else {
              g_rawKeyMakeDetected[raw->data.keyboard.VKey] = true;
            }
          }
        }
      }
    }

    int dx = GetRawInputDeltaX(lParam);

    ULONGLONG now = GetTickCount64();
    bool isMouseSuspended =
        (g_mouseSuspendedUntil > 0 && now < g_mouseSuspendedUntil);

    const bool allowAngleUpdate =
        (g_fortniteFocusedCache && !g_isCursorVisible && !isMouseSuspended);

    if (allowAngleUpdate) {
      g_logic.Update(dx);
    }
    return 0;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

// HUD Window Procedure
LRESULT CALLBACK HUDWndProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {
  case WM_CREATE:
    RefreshHotkeys(hWnd);
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
        CaptureDesktop(); // Capture before dimming
        g_currentSelection = SELECTING_ROI;
        g_isSelectionActive = true;
        long exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TRANSPARENT;
        SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);
        SetForegroundWindow(hWnd);
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
    if (lParam == WM_RBUTTONUP) {
      ShowTrayContextMenu(hWnd);
    } else if (lParam == WM_LBUTTONDBLCLK) {
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
    }
    return 0;
  case WM_LBUTTONDOWN:
    if (g_currentSelection == SELECTING_ROI) {
      POINT cur;
      GetCursorPos(&cur);
      g_startPoint = cur;
      g_selectionRect = {cur.x, cur.y, cur.x, cur.y};
    } else if (g_currentSelection == SELECTING_COLOR) {
      LOG_INFO("Stage 2 LBUTTONDOWN executed");
      // STAGE 2: PRECISION COLOR PICK (Snap-Shot Bypass)
      LOG_INFO("Stage 2 LBUTTONDOWN: Starting to finalize selection");
      if (g_screenSnapshot) {
        LOG_TRACE("Sampling color from g_screenSnapshot...");
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HGDIOBJ hOld = SelectObject(hdcMem, g_screenSnapshot);

        int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);

        POINT cur;
        GetCursorPos(&cur);
        // Adjust color sample coord by the same virtual screen offset used in
        // CaptureDesktop
        COLORREF pixel = GetPixel(hdcMem, cur.x - sx, cur.y - sy);

        g_pickedColor = pixel;
        g_targetColor = pixel;
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        LOG_TRACE("Color sampled successfully.");
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

      if (!g_allProfiles.empty()) {
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
      static ULONGLONG s_bootTime = GetTickCount64();
      if (GetTickCount64() - s_bootTime < 2500)
        return 0;

      if (g_currentSelection == NONE) {
        bool lDown = g_physicalKeys[VK_LBUTTON];
        POINT pt;
        GetCursorPos(&pt);

        bool canDrag = !IsFortniteForeground();

        if (lDown && !g_isDraggingHUD && canDrag) {
          if (pt.x >= g_hudX && pt.x <= g_hudX + 260 && pt.y >= g_hudY &&
              pt.y <= g_hudY + 150) {
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
          g_hudX = g_dragStartHUD.x + (pt.x - g_dragStartMouse.x);
          g_hudY = g_dragStartHUD.y + (pt.y - g_dragStartMouse.y);
          InvalidateRect(hWnd, NULL, FALSE);
        }

        // Adjust click-through based on Fortnite focus
        bool fnFocused = IsFortniteForeground();
        long ex = GetWindowLong(hWnd, GWL_EXSTYLE);
        if (fnFocused) {
          // When Fortnite is focused, make HUD transparent to clicks
          if (!(ex & WS_EX_TRANSPARENT)) {
            SetWindowLong(hWnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
          }
        } else {
          // When not focused, ensure HUD receives mouse events for dragging
          if (ex & WS_EX_TRANSPARENT) {
            SetWindowLong(hWnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
          }
        }
      }

      g_isCursorVisible = IsCursorCurrentlyVisible();
      float ang = g_logic.GetAngle();

      // Clear the forced redraw flag occasionally set elsewhere
      g_forceRedraw.store(false);

      // Unconditionally draw overlay at 60FPS to keep Debug stats (FPS/Delay)
      // synced live
      DrawOverlay(hWnd, ang, g_showCrosshair);
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

  case WM_SYSCOMMAND:
    // Block F10 from opening the system menu (interferes with Fn+F10 keybind)
    if ((wParam & 0xFFF0) == SC_KEYMENU)
      return 0;
    break;

  case WM_CLOSE:
    g_running = false;
    PostQuitMessage(0);
    QCoreApplication::quit();
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

  g_hHUD = CreateWindowEx(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
      L"BetterAngleHUD", L"BetterAngle HUD", WS_POPUP, screenX, screenY,
      screenW, screenH, NULL, NULL, hInstance, NULL);

  AddSystrayIcon(g_hHUD);
  LOG_INFO("HUD created: hwnd=0x%p", g_hHUD);
  LogWindowInfo(L"HUD handle", g_hHUD);
  ShowControlPanel(); // Force Dashboard to show on startup
  SetWindowPos(g_hHUD, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  UpdateWindow(g_hHUD);
  SetTimer(g_hHUD, 1, 10, NULL);    // 100fps (~10ms) Repaint Timer
  SetTimer(g_hHUD, 2, 30000, NULL); // 30s Auto-Save Timer

  std::thread detThread(DetectorThread);
  std::thread focusThread(FocusMonitorThread);
  std::thread perfThread(PerformanceMonitorThread);

  // Run Qt Event Loop
  int exitCode = app.exec();
  LOG_INFO("Qt event loop exited with code=%d", exitCode);

  g_running = false;
  if (detThread.joinable())
    detThread.join();
  if (focusThread.joinable())
    focusThread.join();
  if (perfThread.joinable())
    perfThread.join();

  // Final Save on Exit
  if (!g_allProfiles.empty()) {
    Profile &p = g_allProfiles[g_selectedProfileIdx];
    p.crossPulse = g_crossPulse;
    p.Save(GetProfilesPath() + p.name + L".json");
  }

  SaveSettings();

  RemoveSystrayIcon(g_hHUD);
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
