#include <algorithm>
#include <atomic>
#include <cmath>
#include <dwmapi.h>
#include <fstream>
#include <gdiplus.h>
#include <immintrin.h>
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
#include <avrt.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "avrt.lib")

using namespace Gdiplus;

void PerformanceMonitorThread();
#include "shared/State.h"

// Global State
// Global handles defined in State.h/cpp
ULONG_PTR g_gdiplusToken;
FovDetector g_detector;

// Pre-spawned BlockInput worker. Sleeps on g_lockEvent; signaled by FOV-edge
// detection. Skips the ~5-20ms cost of spawning a fresh thread per transition.
// Auto-reset event coalesces back-to-back signals (the most recent duration wins,
// which is fine — both glide↔dive durations are ballpark equivalent).
void StartBlockInputWorker() {
  std::thread([]() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (g_running) {
      // Spin-wait for 1ms to catch immediate SetEvent signals (~0.01ms vs ~1ms scheduler latency)
      ULONGLONG spinStart = GetTickCount64();
      DWORD wait = WAIT_TIMEOUT;
      while (GetTickCount64() - spinStart < 1 && wait == WAIT_TIMEOUT) {
        wait = WaitForSingleObject(g_lockEvent, 0);
        if (wait == WAIT_OBJECT_0) break;
        _mm_pause();
      }
      // Fall back to blocking wait if not signaled during spin
      if (wait != WAIT_OBJECT_0) {
        wait = WaitForSingleObject(g_lockEvent, INFINITE);
      }
      if (wait != WAIT_OBJECT_0 || !g_running) continue;

      int durationMs = g_lockDurationMs.exchange(0);
      if (durationMs <= 0) continue;

      // BlockInput(TRUE/FALSE) MUST both execute on the SAME thread.
      // Windows thread affinity rule: only the thread that blocked can unblock.
      g_blockInputActive = true;
      BlockInput(TRUE);
      int ticks = (durationMs + 9) / 10;
      for (int i = 0; i < ticks && IsFortniteForeground(); i++) Sleep(10);
      BlockInput(FALSE);
      g_blockInputActive = false;
      g_preArmActive = false;
      g_lastLockTime = GetTickCount64();
    }
  }).detach();
}

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
  // Seed initial state from reality. If Fortnite is already focused at
  // startup, lastFortniteFocused must be true so the focus-GAINED edge
  // does NOT fire on iteration 1 — that would call BlockInput(TRUE) and
  // freeze the OS for 400ms (focusLostTime=0 trivially beats the 500ms guard).
  bool lastFortniteFocused = IsFortniteForeground();
  ULONGLONG focusLostTime = GetTickCount64();

  while (g_running) {
    bool currentFortniteFocused = IsFortniteForeground();
    g_fortniteFocusedCache = currentFortniteFocused;

    // Focus LOST edge: abort any active BlockInput via the worker thread.
    // We cannot call BlockInput(FALSE) here because Windows thread affinity
    // rule means only the worker thread (which called BlockInput(TRUE)) can
    // successfully unblock. Instead, zero the duration so the worker's
    // Sleep loop exits on the next 10ms tick and calls BlockInput(FALSE).
    if (lastFortniteFocused && !currentFortniteFocused) {
      focusLostTime = GetTickCount64();
      if (g_blockInputActive.load()) {
        g_lockDurationMs = 0;  // Signal worker to release immediately
      }
      g_preArmActive = false;
      g_mouseSuspendedUntil = 0;
    }

    // Focus GAINED edge: only lock if unfocused for >=500ms (real alt-tab).
    // Shorter gaps are overlay/notification blips (Discord, GeForce, Xbox bar)
    // — locking on those eats keys during normal gameplay.
    if (!lastFortniteFocused && currentFortniteFocused) {
      ULONGLONG unfocusedMs = GetTickCount64() - focusLostTime;
      if (unfocusedMs >= 500 && !g_blockInputActive.load() &&
          (GetTickCount64() - g_lastLockTime.load() > 500)) {
        g_lockDurationMs = 400;
        SetEvent(g_lockEvent);
        LOG_INFO("Alt-tab focus detected (400ms BlockInput for FOV stabilization)");
      }
    }
    lastFortniteFocused = currentFortniteFocused;
    Sleep(1);
  }
}

// ---- Tripwire helpers (v5.5.162) ------------------------------------------
// Auto-learned 3-pixel tripwire: fires BlockInput speculatively when three
// trained ROI pixels all match target colour in the same frame, skipping the
// wait for the full AVX2 ROI scan to confirm. See plan v3 for design rationale.

static bool PixelMatchesTarget(DWORD pix, COLORREF target, int tolerance) {
  int b = (int)(pix & 0xFF);
  int g = (int)((pix >> 8) & 0xFF);
  int r = (int)((pix >> 16) & 0xFF);
  int tr = (int)GetRValue(target);
  int tg = (int)GetGValue(target);
  int tb = (int)GetBValue(target);
  int dr = r - tr, dg = g - tg, db = b - tb;
  return dr * dr + dg * dg + db * db <= tolerance * tolerance;
}

// Promote learning to "ready" once we have >=10 events and >=3 candidates
// with 100% hit rate AND <0.1% noise rate. Returns true on activation.
static bool TryActivateTripwire(Profile &p) {
  if (p.tripwireEvents < 5) return false;
  if (p.tripwireCandidates.size() < 3) return false;

  int qualifiedIdx[9];
  long long qualifiedScore[9];
  int qualifiedCount = 0;
  for (int i = 0; i < (int)p.tripwireCandidates.size() && qualifiedCount < 9; i++) {
    auto &c = p.tripwireCandidates[i];
    if (c.hits != p.tripwireEvents) continue;        // 100% hit rate gate
    if (c.idleSamples < 500) continue;               // need enough idle samples
    if (c.noise * 1000 > c.idleSamples) continue;    // <0.1% noise rate gate
    qualifiedIdx[qualifiedCount] = i;
    qualifiedScore[qualifiedCount] =
        (long long)c.idleSamples - (long long)c.noise * 1000;
    qualifiedCount++;
  }

  if (qualifiedCount < 3) return false;

  // Selection sort the top 3 by score (qualifiedCount <= 9, no perf concern).
  for (int k = 0; k < 3; k++) {
    int bestK = k;
    for (int j = k + 1; j < qualifiedCount; j++) {
      if (qualifiedScore[j] > qualifiedScore[bestK]) bestK = j;
    }
    if (bestK != k) {
      std::swap(qualifiedIdx[k], qualifiedIdx[bestK]);
      std::swap(qualifiedScore[k], qualifiedScore[bestK]);
    }
    p.tripwireActiveIdx[k] = qualifiedIdx[k];
  }

  p.tripwireReady = true;
  p.tripwireSavedRoiX = p.roi_x;
  p.tripwireSavedRoiY = p.roi_y;
  p.tripwireSavedRoiW = p.roi_w;
  p.tripwireSavedRoiH = p.roi_h;
  p.tripwireSavedColor = p.target_color;
  p.tripwireSavedTolerance = p.tolerance;

  std::wstring profilePath = GetProfilesPath() + L"/" + p.name + L".json";
  p.Save(profilePath);
  LOG_INFO("Tripwire activated (3-pixel coincidence trained)");
  return true;
}

// FOV Detector Thread - Now focused solely on ROI scanning
void DetectorThread() {
  // Option 2: Scheduler priority boost (v5.5.164)
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  DWORD taskIndex = 0;
  HANDLE taskHandle = AvSetMmThreadCharacteristics(L"Pro Audio", &taskIndex);
  if (taskHandle) {
    LOG_INFO("Detector thread: MMCSS Pro Audio enabled");
  } else {
    LOG_INFO("Detector thread: MMCSS unavailable (Home Edition?), using priority boost alone");
  }

  // Option 3: CPU core pinning (v5.5.164)
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  DWORD numCores = sysInfo.dwNumberOfProcessors;
  if (numCores > 1) {
    // Pin to last core to avoid UI thread (typically core 0)
    DWORD coreIdx = numCores - 1;
    DWORD_PTR affinityMask = 1ULL << coreIdx;
    if (SetThreadAffinityMask(GetCurrentThread(), affinityMask)) {
      LOG_INFO("Detector thread pinned to core %lu", coreIdx);
    } else {
      LOG_INFO("Detector thread: SetThreadAffinityMask failed (may be OK on low-core systems)");
    }
  }

  bool lastDiving = false;
  ULONGLONG peakMatchTimestamp = 0;
  float lastSensX = -1.0f;
  RECT cachedMonitorRect = {};
  int cachedScreenIdx = -1;
  int cachedDisplayGen = -1;
  LARGE_INTEGER frameTime{};

  // Cache QPC frequency once — it never changes at runtime (v5.5.173)
  LARGE_INTEGER qpcFreqCached;
  QueryPerformanceFrequency(&qpcFreqCached);

  // Throttle IsCursorCurrentlyVisible() to every ~5ms (v5.5.173)
  ULONGLONG lastCursorCheckMs = 0;

  timeBeginPeriod(1);
  while (g_running) {
    if (!g_allProfiles.empty() && g_currentSelection == NONE) {
      Profile &p = g_allProfiles[g_selectedProfileIdx];
      if (p.sensitivityX != lastSensX) {
        g_logic.LoadProfile(p.sensitivityX);
        lastSensX = p.sensitivityX;
      }
      g_requiredMatchCount =
          (int)((p.diveGlideMatch / 100.0f) * (p.roi_w * p.roi_h));

      // Cache GetTickCount64() once per iteration to avoid ~10+ redundant
      // kernel transitions per spin cycle (~15-25ns each) (v5.5.173)
      ULONGLONG now = GetTickCount64();

      bool currentFortniteFocused = g_fortniteFocusedCache.load();

      // Throttle IsCursorCurrentlyVisible() to every ~5ms — the cursor state
      // only changes when the user opens inventory/map, not every microsecond.
      // Saves thousands of GetCursorInfo() syscalls per second (v5.5.173)
      if (now - lastCursorCheckMs >= 5) {
        g_isCursorVisible = IsCursorCurrentlyVisible();
        lastCursorCheckMs = now;
      }

      // Only scan ROI when Fortnite is the foreground window
      if (currentFortniteFocused) {
        int curDisplayGen = g_displayChangeGen.load();
        if (g_screenIndex != cachedScreenIdx || curDisplayGen != cachedDisplayGen) {
          cachedMonitorRect = GetMonitorRectByIndex(g_screenIndex);
          // Re-init DXGI duplication for the (possibly new) monitor. Done
          // here on the detector thread to avoid races with the running scan
          // and with SamplePixelDXGI calls from the colour picker.
          g_detector.ReinitDisplay(g_screenIndex);
          cachedScreenIdx = g_screenIndex;
          cachedDisplayGen = curDisplayGen;
        }
        RECT mRect = cachedMonitorRect;
        RoiConfig cfg = {
            p.roi_x, p.roi_y, p.roi_w, p.roi_h,
            p.target_color, p.tolerance};
        // Store screen-space offset for BitBlt fallback
        cfg.monitorOffsetX = mRect.left;
        cfg.monitorOffsetY = mRect.top;

        // --- Tripwire learning state housekeeping (v5.5.162) ---------------
        // Drop learned tripwire if ROI/colour/tolerance changed since saving.
        if (p.tripwireReady &&
            (p.roi_x != p.tripwireSavedRoiX ||
             p.roi_y != p.tripwireSavedRoiY ||
             p.roi_w != p.tripwireSavedRoiW ||
             p.roi_h != p.tripwireSavedRoiH ||
             p.target_color != p.tripwireSavedColor ||
             p.tolerance != p.tripwireSavedTolerance)) {
          p.tripwireCandidates.clear();
          p.tripwireEvents = 0;
          p.tripwireReady = false;
          p.tripwireActiveIdx[0] = p.tripwireActiveIdx[1] =
              p.tripwireActiveIdx[2] = -1;
          LOG_INFO("Tripwire reset (ROI/colour/tolerance changed)");
        }
        // Initialise the 3x3 candidate grid on first encounter.
        if (p.tripwireCandidates.empty() && p.roi_w > 0 && p.roi_h > 0) {
          p.tripwireCandidates.resize(9);
          for (int i = 0; i < 9; i++) {
            p.tripwireCandidates[i].x = (p.roi_w * ((i % 3) * 2 + 1)) / 6;
            p.tripwireCandidates[i].y = (p.roi_h * ((i / 3) * 2 + 1)) / 6;
            p.tripwireCandidates[i].hits = 0;
            p.tripwireCandidates[i].noise = 0;
            p.tripwireCandidates[i].idleSamples = 0;
          }
        }

        DWORD gridSamples[9] = {0};
        frameTime = {};
        ULONGLONG startMs = GetTickCount64();
        int scanResult = g_detector.Scan(cfg, gridSamples,
                                        (const int *)p.tripwireActiveIdx,
                                        p.tripwireReady,
                                        &frameTime,
                                        g_requiredMatchCount.load());
        ULONGLONG endMs = GetTickCount64();
        ULONGLONG scanMs = endMs - startMs;
        g_detectionDelayMs = scanMs;

        // -1 means no new frame was available (DXGI timeout) — skip this cycle
        // entirely to avoid false edge detection from a stale matchCount of 0.
        // -1000 means tripwire pre-arm fired (Option 1, v5.5.164) — skip AVX2 loop.
        if (scanResult < 0) {
          if (scanResult == -1000) {
            // Tripwire pre-arm fired before AVX2 loop
            g_lastLockTime = now;
            g_mouseSuspendedUntil = now + 200;
            g_lockDurationMs = 200;
            g_preArmActive = true;
            g_lastPreArmTime = now;
            SetEvent(g_lockEvent);
            LOG_INFO("Tripwire pre-arm fired (3-pixel coincidence, early)");
          } else if (scanResult == -1) {
            // Sub-frame GDI tripwire check between DXGI frames (v5.5.165)
            if (p.tripwireReady && currentFortniteFocused && !g_isCursorVisible &&
                !g_blockInputActive.load() && !g_preArmActive.load() &&
                now >= g_mouseSuspendedUntil &&
                (now - g_lastLockTime > 500)) {
              static LARGE_INTEGER lastGdiCheck{};
              LARGE_INTEGER nowQpc;
              QueryPerformanceCounter(&nowQpc);
              // Throttle to 100µs via QPC (using cached frequency, v5.5.173)
              if ((nowQpc.QuadPart - lastGdiCheck.QuadPart) * 1000000LL / qpcFreqCached.QuadPart >= 100) {
                lastGdiCheck = nowQpc;
                if (g_detector.CheckTripwireGDI(cfg, p.tripwireActiveIdx,
                                                p.target_color, p.tolerance)) {
                  g_lastLockTime = now;
                  g_mouseSuspendedUntil = now + 200;
                  g_lockDurationMs = 200;
                  g_preArmActive = true;
                  SetEvent(g_lockEvent);
                  LOG_INFO("Sub-frame GDI tripwire fired");
                }
              }
            }
          }
          _mm_pause();
          continue;
        }

        g_matchCount = scanResult;

        // Scanner CPU %: with spin-wait the loop pegs one core when active.
        // The old formula assumed a ~1ms cycle with Sleep(1) and read 0% in
        // spin mode (scanMs is sub-ms / below GetTickCount64 resolution).
        // Hard-coded binary metric: 100 when actively scanning, 0 otherwise.
        g_scannerCpuPct = 100;

        // Peak match tracking (2s decay window)
        int currentMatch = g_matchCount.load();
        if (now - peakMatchTimestamp > 2000) {
          g_peakMatchCount = currentMatch;
          peakMatchTimestamp = now;
        } else if (currentMatch > g_peakMatchCount.load()) {
          g_peakMatchCount = currentMatch;
        }

        // --- Tripwire training & pre-arm (v5.5.162) ------------------------
        bool diving = (currentMatch >= g_requiredMatchCount.load());

        // Train on FOV-rising edge (idle -> diving).
        if (diving && !lastDiving && !p.tripwireCandidates.empty()) {
          for (int i = 0; i < (int)p.tripwireCandidates.size(); i++) {
            if (PixelMatchesTarget(gridSamples[i], p.target_color, p.tolerance)) {
              p.tripwireCandidates[i].hits++;
            }
          }
          p.tripwireEvents++;
          TryActivateTripwire(p);
        }

        // Sample noise during clearly-idle frames (matchCount well below
        // threshold). Builds the per-candidate false-match denominator that
        // the activation gate uses to enforce <0.1% noise.
        if (currentMatch < g_requiredMatchCount.load() / 5 &&
            !p.tripwireCandidates.empty()) {
          for (int i = 0; i < (int)p.tripwireCandidates.size(); i++) {
            p.tripwireCandidates[i].idleSamples++;
            if (PixelMatchesTarget(gridSamples[i], p.target_color, p.tolerance)) {
              p.tripwireCandidates[i].noise++;
            }
          }
        }

        // Pre-arm check: AT LEAST 2 of 3 trained pixels match in this same frame
        // AND matchCount > 0 (co-validation). Fires BlockInput speculatively
        // ~150-200µs before the full scan would have crossed threshold.
        if (p.tripwireReady && currentMatch > 0 &&
            !g_blockInputActive.load() && !g_preArmActive.load() &&
            !g_isCursorVisible &&
            now >= g_mouseSuspendedUntil &&
            (now - g_lastLockTime > 500)) {
          int matchCount = 0;
          for (int k = 0; k < 3; k++) {
            int idx = p.tripwireActiveIdx[k];
            if (idx >= 0 && idx < (int)p.tripwireCandidates.size() &&
                PixelMatchesTarget(gridSamples[idx], p.target_color,
                                   p.tolerance)) {
              matchCount++;
            }
          }
          if (matchCount >= 2) {
            g_lastLockTime = now;
            g_mouseSuspendedUntil = now + 200;
            g_lockDurationMs = 200;
            g_preArmActive = true;
            g_lastPreArmTime = now;
            SetEvent(g_lockEvent);
            LOG_INFO("Tripwire pre-arm fired (2+ pixel match)");
          }
        }
      } else {
        // Fortnite not focused, reset detection to 0
        g_matchCount = 0;
        g_detectionDelayMs = 0;
        g_scannerCpuPct = 0;
      }

      bool nowDiving = (g_matchCount.load() >= g_requiredMatchCount.load());

      // Skip edge detection on first frame after focus return to avoid spurious FOV transition
      if (g_justRefocused.exchange(false)) {
        lastDiving = nowDiving;
      }

      // Only trigger input blocking locks if Fortnite is actually focused AND the cursor is hidden.
      // This prevents the mouse from locking up on the desktop if the user tabs out,
      // or if they open the in-game map/inventory (which shows the cursor and obscures the ROI).
      if (currentFortniteFocused && !g_isCursorVisible && now >= g_mouseSuspendedUntil) {
        // Edge: Gliding -> Diving (Nitro)
        if (nowDiving && !lastDiving && !g_blockInputActive.load() &&
            (now - g_lastLockTime > 500)) {
          g_lastLockTime = now;
          g_mouseSuspendedUntil = now + 200;
          g_lockDurationMs = 200;
          SetEvent(g_lockEvent);
          LOG_INFO("Transition: glide->dive (200ms BlockInput)");
        }
        // Edge: Diving -> Gliding (Nitro)
        else if (!nowDiving && lastDiving && !g_blockInputActive.load() &&
                 (now - g_lastLockTime > 500)) {
          g_lastLockTime = now;
          g_mouseSuspendedUntil = now + 250;
          g_lockDurationMs = 250;
          SetEvent(g_lockEvent);
          LOG_INFO("Transition: dive->glide (250ms BlockInput)");
        }
      }

      // Reset UI tracker once timer expires
      if (g_mouseSuspendedUntil > 0 && now >= g_mouseSuspendedUntil) {
        g_mouseSuspendedUntil = 0;
      }

      if (nowDiving != lastDiving) {
        g_logic.ApplyRetroCorrection(frameTime, nowDiving);
      }
      lastDiving = nowDiving;
      g_isDiving = nowDiving;
    }
    // Spin (peg one core) only while actively scanning; otherwise idle politely.
    // _mm_pause is a CPU hint that yields hyperthread cycles during a spin loop.
    if (g_fortniteFocusedCache.load() && g_currentSelection == NONE) {
      _mm_pause();
    } else {
      Sleep(10);
    }
  }
  timeEndPeriod(1);
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

// Helper for manual global hotkey polling
static bool CheckCustomHotkey(UINT mod, UINT vk, bool& wasPressed) {
    if (vk == 0) {
        wasPressed = false;
        return false;
    }
    bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (mod & MOD_CONTROL) pressed &= (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (mod & MOD_SHIFT) pressed &= (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (mod & MOD_ALT) pressed &= (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (mod & MOD_WIN) pressed &= ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000));
    
    if (pressed && !wasPressed) {
        wasPressed = true;
        return true;
    }
    if (!pressed) {
        wasPressed = false;
    }
    return false;
}

// Refreshes all global hotkeys for the HUD window
bool RefreshHotkeys(HWND hWnd) {
  // Legacy: We now use manual async polling in WM_TIMER so mouse buttons can be bound.
  // RegisterHotKey does not support mouse buttons.
  return true;
}

// Message-Only Window for Bullet-Proof Raw Input
LRESULT CALLBACK MsgWndProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  if (message == WM_INPUT) {
    int dx = GetRawInputDeltaX(lParam);

    ULONGLONG now = GetTickCount64();
    bool isMouseSuspended =
        (g_mouseSuspendedUntil > 0 && now < g_mouseSuspendedUntil);

    const bool allowAngleUpdate =
        (g_fortniteFocusedCache && !g_isCursorVisible && !isMouseSuspended);

    if (allowAngleUpdate) {
      g_logic.Update(dx);
    }
    // Fall through to DefWindowProc — required to free the raw input buffer.
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

    // Removed WM_HOTKEY logic. Now handled in WM_TIMER to support mouse buttons.
    return 0;

  case WM_TRAYICON: {
    // Under NOTIFYICON_VERSION_4 the mouse event is in LOWORD(lParam);
    // raw lParam carries x/y coords so direct comparison always fails.
    WORD evt = LOWORD(lParam);
    if (evt == WM_RBUTTONUP || evt == WM_CONTEXTMENU) {
      ShowTrayContextMenu(hWnd);
    } else if (evt == WM_LBUTTONDBLCLK) {
      ShowControlPanel();
    }
    return 0;
  }

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
        COLORREF bitBltPixel = GetPixel(hdcMem, cur.x - sx, cur.y - sy);

        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);

        // Try a one-shot DXGI sample at the same screen-space pixel. The
        // scanner reads DXGI bytes — saving the DXGI-sampled value as
        // target_color avoids the GDI/DXGI byte drift that otherwise
        // prevents matches. Falls back to the BitBlt sample on failure.
        RECT mRect = GetMonitorRectByIndex(g_screenIndex);
        int monX = cur.x - mRect.left;
        int monY = cur.y - mRect.top;
        COLORREF dxgiPixel = 0;
        bool gotDxgi = g_detector.SamplePixelDXGI(monX, monY, dxgiPixel);

        COLORREF chosen = gotDxgi ? dxgiPixel : bitBltPixel;
        g_pickedColor = chosen;
        g_targetColor = chosen;
        g_lastPickSource = gotDxgi ? 1 : 2;
        LOG_INFO("Color picked: source=%s rgb=(%d,%d,%d)",
                 gotDxgi ? "DXGI" : "BitBlt-fallback",
                 GetRValue(chosen), GetGValue(chosen), GetBValue(chosen));
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

      // --- Manual Hotkey Polling (Supports Mouse Buttons) ---
      if (!g_allProfiles.empty() && !g_keybindAssignmentActive) {
        static bool s_toggleWasPressed = false;
        static bool s_roiWasPressed = false;
        static bool s_crossWasPressed = false;
        static bool s_zeroWasPressed = false;

        Profile &p = g_allProfiles[g_selectedProfileIdx];

        // 1: Toggle Panel
        if (CheckCustomHotkey(p.keybinds.toggleMod, p.keybinds.toggleKey, s_toggleWasPressed)) {
          ShowControlPanel();
        }

        // 2: ROI Select
        if (CheckCustomHotkey(p.keybinds.roiMod, p.keybinds.roiKey, s_roiWasPressed)) {
          if (g_currentSelection == NONE) {
            if (!IsFortniteForeground()) {
              LOG_INFO("ROI selection blocked: Fortnite not focused");
            } else {
              CaptureDesktop();
              g_currentSelection = SELECTING_ROI;
              g_isSelectionActive = true;
              long exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
              exStyle &= ~WS_EX_TRANSPARENT;
              SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);
              // Do NOT SetForegroundWindow(hWnd) — it alt-tabs the user out of
              // Fortnite. With WS_EX_NOACTIVATE on the HUD + WS_EX_TRANSPARENT
              // cleared, the topmost overlay catches mouse events without
              // stealing focus.
            }
          } else {
            if (!g_allProfiles.empty() &&
                g_selectionRect.right > g_selectionRect.left &&
                g_selectionRect.bottom > g_selectionRect.top) {
              RECT mRect = GetMonitorRectByIndex(g_screenIndex);
              p.roi_x = g_selectionRect.left - mRect.left;
              p.roi_y = g_selectionRect.top - mRect.top;
              p.roi_w = g_selectionRect.right - g_selectionRect.left;
              p.roi_h = g_selectionRect.bottom - g_selectionRect.top;
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
        }

        // 3: Toggle Crosshair
        if (CheckCustomHotkey(p.keybinds.crossMod, p.keybinds.crossKey, s_crossWasPressed)) {
          g_showCrosshair = !g_showCrosshair;
          g_forceRedraw = true;
          if (!g_allProfiles.empty()) {
            p.showCrosshair = g_showCrosshair;
            p.Save(GetProfilesPath() + p.name + L".json");
          }
          SaveSettings();
          NotifyBackendCrosshairChanged();
          if (g_showCrosshair) Beep(750, 50);
          else Beep(500, 50);
        }

        // 4: Zero Angle
        if (CheckCustomHotkey(p.keybinds.zeroMod, p.keybinds.zeroKey, s_zeroWasPressed)) {
          g_currentAngle = 0.0f;
          g_logic.SetZero();
          Beep(1000, 80);
        }
      }
      // ------------------------------------------------------

      if (g_currentSelection == NONE) {
        bool lDown = g_physicalKeys[VK_LBUTTON];
        POINT pt;
        GetCursorPos(&pt);

        bool fnFocused = g_fortniteFocusedCache.load();
        bool canDrag = !fnFocused;

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

        // Adjust click-through and Z-order based on Fortnite focus
        long ex = GetWindowLong(hWnd, GWL_EXSTYLE);
        if (fnFocused) {
          // When Fortnite is focused, make HUD transparent to clicks and Topmost
          if (!(ex & WS_EX_TRANSPARENT)) {
            SetWindowLong(hWnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          }
        } else {
          // When not focused, ensure HUD receives mouse events for dragging and drop Topmost
          // Dropping Topmost prevents Windows from hiding the taskbar ("bottom of screen disappearing")
          if (ex & WS_EX_TRANSPARENT) {
            SetWindowLong(hWnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
            SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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

  case WM_DISPLAYCHANGE: {
    // Bump generation so DetectorThread re-resolves its cached monitor rect.
    g_displayChangeGen.fetch_add(1);

    // Auto-track Fortnite's monitor: hot-plugging a 2nd monitor can renumber
    // monitor indices (Windows enumerates by virtual-desktop position). If
    // Fortnite is still on the original physical screen, find its new index
    // and update g_screenIndex so the scanner reads the correct slice.
    if (g_fortniteWindow && IsWindow(g_fortniteWindow)) {
      HMONITOR hFnMon = MonitorFromWindow(g_fortniteWindow, MONITOR_DEFAULTTONEAREST);
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

    RECT mRect = GetMonitorRectByIndex(g_screenIndex);
    int screenW = mRect.right - mRect.left;
    int screenH = mRect.bottom - mRect.top;
    int screenX = mRect.left;
    int screenY = mRect.top;
    SetWindowPos(hWnd, NULL, screenX, screenY, screenW, screenH,
                 SWP_NOACTIVATE | SWP_NOZORDER);
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
  char exeName[] = "BetterAngle.exe";
  char *argv[] = {exeName, nullptr};
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

  // CRITICAL FIX: Reinit DXGI on the profile's saved monitor, not hardcoded 0
  g_detector.ReinitDisplay(g_screenIndex);

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

  // WS_EX_NOACTIVATE: prevents click-on-overlay from stealing focus from Fortnite
  // during ROI/color selection. Without this, clicking the HUD activates it and
  // minimizes the game (alt-tab effect).
  g_hHUD = CreateWindowEx(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
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

  // Auto-reset event: pre-spawned worker waits on this for sub-millisecond lock signal
  g_lockEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  StartBlockInputWorker();

  std::thread detThread(DetectorThread);
  std::thread focusThread(FocusMonitorThread);
  std::thread perfThread(PerformanceMonitorThread);

  // Run Qt Event Loop
  int exitCode = app.exec();
  LOG_INFO("Qt event loop exited with code=%d", exitCode);

  g_running = false;

  // CRITICAL: Force-release any orphaned BlockInput lock from detached threads.
  // Without this, the kernel holds the block for ~5 seconds after process exit.
  BlockInput(FALSE);
  g_blockInputActive = false;

  // Wake the BlockInput worker so it observes g_running=false and exits its WaitForSingleObject.
  if (g_lockEvent) SetEvent(g_lockEvent);

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

  // Balance the timeBeginPeriod(1) from startup to restore system timer resolution
  timeEndPeriod(1);

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
