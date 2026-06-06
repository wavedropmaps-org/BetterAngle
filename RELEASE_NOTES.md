### BetterAngle Pro v6.0.0
Major release. Consolidates a full pass of overlay stability, input-safety, quality-of-life, and performance work.

**Overlay & Multi-Monitor**
- Fixed the decimal angle HUD leaving a frozen ghost/duplicate on the old monitor when dragging the dashboard across monitors (DWM surface now blanked before the move).
- HUD auto-snaps to Fortnite's monitor on startup if the game is already running.
- Crosshair and decimal HUD now correctly hide when Fortnite is not the focused window.

**Input Safety**
- Fixed ESC (and other keys) leaking into Explorer / tab-switching: the overlay no longer intercepts input or steals keyboard focus from other apps (`WS_EX_NOACTIVATE` + corrected click-through).

**Quality of Life**
- "Reset HUD Position" button to recover an off-screen HUD.
- Dashboard window position is remembered across restarts.
- System tray tooltip shows the live angle.
- Fortnite running/focused/monitor status surfaced in the Debug tab.

**Update Channels**
- Stable / Beta update channels via `MIN_STABLE_VERSION`: normal users are only notified once a build is graduated to stable; a Beta toggle in the Debug tab opts testers into pre-releases. Active channel shown in the Updates tab.

**Performance**
- Eliminated a detector thread that was pegging a full CPU core 100% of the time (now throttled, near-idle when tabbed out).
- Removed disk thrashing while dragging the dashboard window.
- Cached the Debug-tab monitor lookup.

### BetterAngle Pro v5.5.317
- Automated build release.

### BetterAngle Pro v5.5.316
- **Cleanup: Cache Debug-Tab Fortnite Monitor Lookup**. The "Fortnite Monitor" label in the Debug tab ran a window search + full monitor enumeration on every debug refresh (every 10ms while the Debug tab is open). The resolved monitor index is now cached for 500ms, so the expensive lookup runs ~2?/sec instead of ~100?/sec. The live focused/active state still updates on every refresh. Only affects the Debug tab while it's open; no gameplay impact.

### BetterAngle Pro v5.5.315
- **Performance: Fix Disk Thrashing on Dashboard Drag**. The dashboard position-save added in v5.5.311 called `SaveSettings()` (a full settings.json rewrite) on every `onXChanged`/`onYChanged` ? which fire 100+ times per second while dragging the window. Dragging the dashboard was writing the whole settings file to disk hundreds of times a second, causing needless disk I/O, SSD wear, and possible drag stutter. The position is now updated in memory only; the existing 30-second auto-save timer and the on-exit save persist it. Matches how the HUD drag already worked (save once on release).

### BetterAngle Pro v5.5.314
- **Performance: Massive CPU Reduction in Detector Thread**. The ROI scanner thread was busy-spinning on `_mm_pause()` with no sleep, pegging a full CPU core at 100% at all times ? including when Fortnite wasn't even focused (where it did nothing but spin). It now throttles properly: a 4ms sleep between scans while in-game (~200Hz detection, still far faster than dive/glide edge detection needs since the transition lock runs for 700ms) and a 150ms sleep when tabbed out or selecting an ROI/colour. This drops idle/background CPU usage dramatically and significantly lowers in-game CPU with no perceptible change to detection responsiveness. The scanner-CPU% debug gauge now reflects the real loop period.

### BetterAngle Pro v5.5.313
- **Fix: Bogus "NOT DETECTING ? CHECK ROI" Warning**. Removed the HUD warning added in v5.5.311. It was triggered by `matchPct == 0`, but that match ratio measures the FOV/glide indicator colour inside the ROI ? which is legitimately 0 during most of normal gameplay (the indicator is only on-screen at specific moments). The angle readout updates from raw mouse deltas independently of ROI detection, so the warning fired constantly even when everything was working correctly. Fortnite detection status remains available in the Debug tab where it belongs.

### BetterAngle Pro v5.5.312
- **Feature: Stable / Beta Update Channels**. Added a `MIN_STABLE_VERSION` file to the repo. The update checker only notifies users when the latest release has been graduated to stable (i.e. its version >= MIN_STABLE_VERSION). Push test builds freely ? users stay quiet until you bump the file. A "Beta Channel" toggle in the Debug tab bypasses the gate and shows every release including pre-releases immediately, for testers. The active channel (STABLE / BETA) is shown as a badge in the Updates tab. Toggling the channel re-runs the update check instantly.

### BetterAngle Pro v5.5.311
- **QoL: Startup Monitor Auto-Detection**. If Fortnite is already running when BetterAngle launches, the HUD now snaps to Fortnite's monitor immediately instead of defaulting to monitor 0.
- **QoL: HUD Position Reset Button**. Added a "Reset HUD Position" button in the General tab (Display & Monitor section). Moves the HUD back to the default top-left position ? recovery path for users who accidentally drag it off-screen.
- **QoL: Fortnite Monitor Status in Debug Tab**. The Debug tab now shows which monitor Fortnite is running on (e.g. "Monitor 2 (Active)") alongside the existing Running/Focused rows.
- **QoL: "NOT DETECTING ? CHECK ROI" Overlay Warning**. When Fortnite is the foreground window but detection ratio is 0%, a small warning appears on the HUD so users know to re-set their ROI instead of wondering why the angle isn't updating.
- **QoL: Dashboard Window Position Remembered**. The control panel now restores to wherever it was last positioned when relaunching, instead of always opening at screen centre.
- **QoL: Tray Tooltip Shows Current Angle**. Hovering the system tray icon now shows the live angle readout (e.g. "BetterAngle Pro  |  45.3?"), updated every 500ms.

### BetterAngle Pro v5.5.310
- **Fix: ESC Opens File Explorer / Switches Tabs (regression)**. Two compounding issues caused the overlay to intercept keypresses from other apps. (1) The click-through logic was removing `WS_EX_TRANSPARENT` whenever Fortnite was not focused ? making the full-screen invisible overlay capture all mouse clicks from Chrome, Discord, etc. and potentially steal keyboard focus. Fixed: `WS_EX_TRANSPARENT` is now only removed when the user is actively dragging the HUD, doing ROI/colour selection, or holding Ctrl in-game. (2) `WS_EX_NOACTIVATE` was missing from the HUD window creation, meaning any click that reached the overlay could give it focus. Added `WS_EX_NOACTIVATE` to the initial `exStyle` so the overlay can never take keyboard focus regardless of transparency state.

### BetterAngle Pro v5.5.309
- **Fix: Crosshair and Decimal HUD Visible When Fortnite Not Focused**. The overlay was staying visible whenever the BetterAngle dashboard window was open, even when the user was in Discord, Chrome, or any other app. `IsWindowVisible` returns `true` if a window has `WS_VISIBLE` regardless of whether it is the active foreground window, so `panelVisible` was almost always `true`. Removed `panelVisible` from the overlay visibility condition. The overlay now only shows when Fortnite is the foreground window, when the HUD is being actively dragged (Ctrl+drag), or during ROI/colour selection.

### BetterAngle Pro v5.5.308
- **Fix: Decimal Angle HUD Ghost on Cross-Monitor Drag**. Fixed a DWM residual-surface bug where dragging the dashboard to a new monitor left a frozen ghost copy of the decimal angle readout on the old monitor. The root cause was that `g_screenIndex` was updated to the new monitor **before** the layered window's existing surface was cleared, so `SW_HIDE` alone had no old-position record to flush from DWM's compositor. The fix pushes a zero-alpha blank frame via `UpdateLayeredWindow` at the old monitor's coordinates ? while `g_screenIndex` still points there ? before updating the index and moving the HWND. This explicitly tells DWM to clear that surface, eliminating the duplicate.

### BetterAngle Pro v5.5.307
- **Fix: Critical Cross-Thread HUD Desync**. Fixed a severe race condition where the Qt QML background thread was trying to directly move the Win32 HUD window (`SetWindowPos` and `ShowWindow`) while the Win32 main thread was simultaneously trying to draw to it at 100fps. This cross-thread interference corrupted Windows DWM, creating the illusion of duplicated "ghost" windows across monitors. The backend now safely uses `PostMessage` to serialize all monitor boundary switches directly onto the Win32 rendering thread.

### BetterAngle Pro v5.5.305
- **Fix: HUD Ghosting on Old Monitor**. Fixed a known Windows Desktop Window Manager (DWM) bug where moving a hardware-accelerated Layered Window across monitor boundaries would leave a frozen ghost artifact on the old monitor while drawing live on the new monitor. The backend now forces a DWM surface purge (`SW_HIDE` -> `SW_SHOWNOACTIVATE`) when crossing monitors to guarantee the old screen is cleared.

### BetterAngle Pro v5.5.304
- **Fix: HUD Duplication on Cross-Monitor Drag**. Fixed a compile error in the cross-monitor sync logic where the monitor rectangle was undeclared. This ensures the HUD sync correctly tracks absolute monitor coordinates during drags.

### BetterAngle Pro v5.5.302
- **Feature: Fully Unlinked Local Dragging**. The HUD and dashboard windows can now be dragged completely independently on the same screen (Ctrl+Drag the HUD, or drag the dashboard normally). They will only follow each other if the dashboard is moved to a completely different monitor, keeping them bundled per-screen but separate locally.

### BetterAngle Pro v5.5.300
- **Fix: ESC Key Opens File Explorer / Switches Tabs**. The HUD overlay's `WM_KEYDOWN` handler was swallowing ALL keypresses (returning 0 unconditionally), not just ESC during ROI selection. When the overlay wasn't click-through (e.g. while tabbed out of Fortnite), this caused Windows to misinterpret missing key events as shell hotkey ghosts ? opening File Explorer or triggering Alt+Tab.
- **Fix: Overlay Vanishes on Alt-Tab**. The dashboard panel handle (`g_hPanel`) was set to a dummy sentinel value `(HWND)1` which always failed `IsWindow()`. This caused the overlay visibility check to think the dashboard was never open, hiding the HUD and crosshair whenever Fortnite lost focus. Now extracts the real native HWND from the Qt window.
- **Fix: Overlay Not Restoring on Tab-Back**. The WM_TIMER handler used a live `IsFortniteForeground()` call for the click-through toggle but the overlay visibility used the event-hook cache `g_fortniteFocusedCache`. These could disagree during focus transitions, causing `WS_EX_TRANSPARENT` to flip-flop and create a brief focus loop. Now uses the event-hook cache consistently.
- **Fix: System Tray Menu Not Clickable**. The tray context menu's `SetForegroundWindow` was failing silently because the HUD window has `WS_EX_TRANSPARENT`. Now temporarily strips the click-through flag before showing the popup menu.
- **Fix: Overlay Showing When Dashboard Minimized**. The overlay visibility check now excludes minimized (iconic) windows. `IsWindowVisible` returns TRUE for minimized windows, so the overlay would stay on-screen even when the dashboard was in the taskbar and Fortnite wasn't focused.
### BetterAngle Pro v5.5.297
- Automated build release.

### BetterAngle Pro v5.5.296
- Automated build release.

### BetterAngle Pro v5.5.295
- Automated build release.

### BetterAngle Pro v5.5.294
- Automated build release.

### BetterAngle Pro v5.5.293
- Automated build release.

### BetterAngle Pro v5.5.292
- **Fix: Alt+Tab Lag**. Throttled overlay redraws to ~10fps when Fortnite is not the foreground window. The 60fps `UpdateLayeredWindow` calls were causing DWM contention during Alt+Tab transitions, making tabbing in and out feel very laggy. Full 60fps is maintained while in-game.
- **Fix: HUD Overlay Hidden from Task Switcher**. The HUD overlay no longer appears as a separate "BetterAngle HUD" thumbnail in Alt+Tab or Win+Tab (Task View). Added a hidden owner window ? owned popup windows are excluded from the Windows shell's task switcher.
- **Fix: Dashboard Taskbar & Alt+Tab Visibility**. The control panel now minimises to the taskbar instead of fully hiding. This keeps it visible in Alt+Tab, Win+Tab, and the taskbar. Clicking the taskbar icon properly restores and focuses the window (required special handling for frameless Qt windows).
- **Feature: HUD Follows Dashboard**. The HUD angle readout now tracks with the dashboard window ? dragging the control panel to a different position or monitor moves the HUD by the same offset.

### BetterAngle Pro v5.5.291
- **Fix: Restore FOV Transition Lock**. Both glide?dive and dive?glide transitions now correctly block input for 700ms on a detached thread, preventing mouse movement from corrupting the angle during the sensitivity scale switch.
- **Fix: Overlay Disappearing Bug**. Re-assert `HWND_TOPMOST` on the HUD overlay when Fortnite regains focus after an Alt-Tab so the overlay doesn't disappear behind the game window.
- **Tweak: Alt-Tab Cooldown**. Increased the Alt-Tab return `BlockInput` duration from 200ms to 300ms.
- **Cleanup: Dead Input Locking Logic**. Removed unused variables and mutexes (e.g., `g_mouseSuspendedUntil`, `g_lockMutex`, `g_blockInputMutex`, `FlushPendingInputMessages`) that were remnants of the old locking system.

### BetterAngle Pro v5.5.290
- Automated build release.

### BetterAngle Pro v5.5.289
- **Fix: Zero-Latency Focus Hooks**. Replaced the old spin-loop focus polling with native OS `SetWinEventHook` listeners. The app now receives focus change events instantly from the Windows kernel with zero CPU usage.
- **Fix: Alt-Tab Input Gate**. Reverted the raw input message handler to use the synchronized focus cache guard. This ensures the 200ms `BlockInput` lock is armed *before* any queued background mouse deltas can bypass the guard during an Alt-Tab transition, permanently fixing the angle jumping.

### BetterAngle Pro v5.5.287
- **Cleanup: Removed Nitro Transition Locking**. Removed obsolete dive/glide input locking code as ghosting is no longer an issue, which prevents accidental locking conflicts.

### BetterAngle Pro v5.5.285
- **Feature: Dashboard Diagnostic Toggle Buttons**. The 3 auto-mantle diagnostic toggles (Raw Input Sink, Topmost Overlay, 1ms Timer) are now visual switch buttons at the bottom of the Debug tab. No more editing `settings.json` by hand ? just flip the switch, restart, and test. Each toggle shows red when active (feature disabled) and green when normal, with inline explanations.

### BetterAngle Pro v5.5.284
- **Feature: In-Game HUD Repositioning (Ctrl + Drag)**. You can now move the decimal angle box while Fortnite is running ? no need to alt-tab. Hold `Ctrl` and click-and-drag the HUD overlay to any position on screen. Releasing the mouse automatically saves the new position. The HUD now shows a context-aware hint: an amber "Hold Ctrl + drag to move" label while in-game, a cyan ":: releasing saves position" label while actively dragging, and the standard grey drag hint when Fortnite is not focused. The Dashboard Debug tab now includes a tip card explaining this shortcut under "HUD & Monitor Info".

### BetterAngle Pro v5.5.283
- **Feature: Auto-Mantle Diagnostic Toggles**. Added hidden configuration flags (`diagNoRawInput`, `diagNoTopmost`, `diagNoTimer`) to `settings.json`. These toggles allow selectively disabling core OS integrations (raw input sink, topmost overlay, 1ms timer resolution) to isolate the root cause of the Fortnite auto-mantling bug.

### BetterAngle Pro v5.5.282
- Automated build release.

### BetterAngle Pro v5.5.281
- **Fix: Hotkey Strobing and Mouse Button Debouncing**. Re-enabled the `MOD_NOREPEAT` flag for keyboard hotkeys and implemented an edge-detection state tracker for custom mouse button hotkeys. This stops the toggles (like the crosshair) from rapidly flickering on and off when you hold the key down or click normally.

### BetterAngle Pro v5.5.280
- **Fix: Crosshair / HUD now hides on Alt-Tab**. When Fortnite loses focus the overlay paints fully transparent (HUD + crosshair both invisible). When Fortnite regains focus the overlay restores to the user's last toggle state - acts as a clean suspend.
- **Fix: Keybind capture in dashboard**. `startKeybindAssignment` now unregisters all OS hotkeys so keypresses (e.g. F10) reach Qt's keyboard pipeline. `endKeybindAssignment` force-refreshes the registration. Previously the registered hotkey ate the keypress before Qt could see it, so the TextField sat empty.
- **Feature: Focus-gated hotkeys**. ROI Select, Crosshair Toggle, and Zero Counter hotkeys are now only registered with the OS while Fortnite is the foreground window. When Fortnite is not focused, the keys pass through to the OS so they can be used in other applications. Dashboard toggle stays bound everywhere.
- **Fix: Decimal angle latency**. Removed the LERP smoothing (factor 0.15 at 60Hz, ~500ms to converge) from the HUD pipeline. The decimal value now reflects the raw mouse-delta accumulator on every frame, no perceived delay.

### BetterAngle Pro v5.5.279
- Automated build release.

### BetterAngle Pro v5.5.278
- **Fix: QML Structure Error**. Corrected Dashboard.qml layout structure where content was orphaned outside parent containers. Relocated TRIGGER CALIBRATION, TARGET COLOR SETTINGS, and HOTKEY CONFIGURATION sections into proper Column hierarchy to fix "Failed to load dashboard UI" error.

### BetterAngle Pro v5.5.277
- **Fix: Duplicate WM_TIMER Handler**. Removed duplicate case WM_TIMER statement causing C2196 compilation error. Consolidated mouse button hotkey polling into unified timer handler.
- **Fix: Missing Resource ID**. Added fallback definition for IDI_ICON1 resource constant to ensure system tray icon loads correctly when resource.h is unavailable.

### BetterAngle Pro v5.5.276
- **Fix: VK_XBUTTON SDK Compatibility**. Added fallback definitions for X mouse button constants to ensure compilation on all Windows SDK versions. Resolves build failures when VK_XBUTTON1/VK_XBUTTON2 are not defined in the system headers.

### BetterAngle Pro v5.5.275
- Automated build release.

### BetterAngle Pro v5.5.274
- Automated build release.

### BetterAngle Pro v5.5.273
- Automated build release.

### BetterAngle Pro v5.5.272
- Automated build release.

### BetterAngle Pro v5.5.271
- Automated build release.

### BetterAngle Pro v5.5.270
- **Fix: ROI Selection Restoration**. Resolved the "double-offset" bug that caused coordinate mismatches on secondary monitors.
- **Feature: Auto-Monitor Detection**. The ROI selection tool now automatically detects which monitor you are clicking on and updates the profile settings instantly.
- **Accuracy: Coordinate Normalization**. Fixed the Stage 2 color picker to correctly map virtual screen space to physical pixels, ensuring 100% target color accuracy.

### BetterAngle Pro v5.5.265
- Automated build release.

### BetterAngle Pro v5.5.262
- **Fix: ROI Selection Restoration**. Resolved the "double-offset" bug that caused coordinate mismatches on secondary monitors.
- **Feature: Auto-Monitor Detection**. The ROI selection tool now automatically detects which monitor you are clicking on and updates the profile settings instantly.
- **Accuracy: Coordinate Normalization**. Fixed the Stage 2 color picker to correctly map virtual screen space to physical pixels, ensuring 100% target color accuracy.

### BetterAngle Pro v5.5.261
- Automated build release.

### BetterAngle Pro v5.5.260
- **Fix: Color Selection Pipeline**. Fixed critical coordinate offset bug in Stage 2 color picking?GetPixel was not accounting for monitor window offset on multi-monitor setups. Restructured error handling with proper NULL checks and early exits to prevent dereferencing invalid pointers.
- **Fix: CaptureDesktop() Robustness**. Added proper error handling and cleanup when BitBlt fails; bitmap is now deleted and nullified to prevent stale/corrupted data from being sampled.

### BetterAngle Pro v5.5.259
- Automated build release.

### BetterAngle Pro v5.5.258
- Automated build release.

### BetterAngle Pro v5.5.257
- **Debugging: Color Selection Diagnostics**. Added comprehensive logging to trace screenshot capture, bitmap operations, and pixel sampling to identify color detection failures.
- **Fix: Investigation Tools**. Enhanced logs capture CaptureDesktop() status, Stage 1?2 transitions, and GetPixel() return values for debugging the color selection flow.

### BetterAngle Pro v5.5.256
- Automated build release.

### BetterAngle Pro v5.5.255
- **Feature: Sub-Pixel Interpolation (HUD Smoothing)**. Implemented a LERP-based visual smoothing engine that makes HUD decimals "glide" between values for a premium, high-resolution feel.
- **UI: HUD Smoothing Toggle**. Added a switch to the Engine tab to enable/disable visual interpolation.

### BetterAngle Pro v5.5.254
- Automated build release.

### BetterAngle Pro v5.5.253
- **Feature: Hardware-Direct Input Engine**. Replaced virtual key simulation with raw hardware scancode injection (W: 0x11, A: 0x1E, S: 0x1F, D: 0x20) for improved input fidelity and reduced OS processing overhead.
- **UI: Direct Hardware Input Toggle**. Added new "Engine" tab in Dashboard with toggle to enable/disable direct hardware mode per profile.
- **Compatibility: Regional Keyboard Support**. Implemented MapVirtualKey verification layer to ensure correct scancodes regardless of user's hardware layout.
- **Integration: FOV Transition State Sync**. Direct hardware mode now syncs input state after BlockInput releases during dive/glide transitions.

### BetterAngle Pro v5.5.252
- Automated build release.

### BetterAngle Pro v5.5.251
- **Feature: Atomic Shield (Input Smoothing)**. Implemented a 25ms logic-based smoothing window to prevent input lock flickering during frame stutters or particle interference.
- **UI: Smoothing Toggle**. Added a real-time switch to the Dashboard to enable/disable the Atomic Shield.
- Automated build release.

### BetterAngle Pro v5.5.250
- Automated build release.

### BetterAngle Pro v5.5.249
- Automated build release.

### BetterAngle Pro v5.5.248
- Automated build release.

### BetterAngle Pro v5.5.247
- Automated build release.

### BetterAngle Pro v5.5.246
- Automated build release.

### BetterAngle Pro v5.5.245
- **Emergency Revert: Restored Ultra-Legacy Build**. Rolled back the modern peak restoration. Reverted the codebase to the state of version 5.5.86 (with 700ms skydive stability).

### BetterAngle Pro v5.5.241
- **Stability Fix: Increased Skydive BlockInput to 700ms**. The shorter 300ms window was too aggressive for the legacy v5.5.86 sync logic. Increased back to 700ms to ensure angle stability during transitions. Alt-Tab remains at 200ms.

### BetterAngle Pro v5.5.239
- **Tweak: Faster BlockInput transitions**. Shortened the mouse lock durations for Alt-Tab (200ms) and Skydive transitions (300ms) to improve responsiveness.
