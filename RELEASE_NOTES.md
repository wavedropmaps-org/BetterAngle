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
