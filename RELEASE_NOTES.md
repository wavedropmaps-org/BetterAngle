### BetterAngle Pro v5.5.223
- Automated build release.

### BetterAngle Pro v5.5.222
- **fix: Angle frozen for ~1s after BlockInput releases.** `BlockInput(TRUE)` disrupts Fortnite's cursor-hiding state. After `BlockInput(FALSE)`, the game takes up to ~1s to re-hide the cursor, during which `CURSOR_SHOWING` is set and `!g_isCursorVisible` was blocking angle updates. Added a 200ms grace period after `g_blockInputActive` drops false where cursor visibility is ignored, so angle resumes immediately after the lock regardless of how long the game takes to re-hide the cursor.

### BetterAngle Pro v5.5.221
- **fix: Angle tracking resumes instantly when BlockInput releases.** Previously `g_mouseSuspendedUntil` (a timer) was gating raw input angle updates, causing the angle to stay frozen for the remainder of the timer even after `BlockInput(FALSE)` had already fired. Now the angle gate is tied directly to `g_blockInputActive` ? the moment the BlockInput worker calls `BlockInput(FALSE)`, angle tracking resumes. The LOCKING UI indicator still uses `g_mouseSuspendedUntil` unchanged.

### BetterAngle Pro v5.5.220
- **tweak: Alt-tab BlockInput lock duration increased from 250ms to 300ms.**

### BetterAngle Pro v5.5.219
- Automated build release.

### BetterAngle Pro v5.5.218
- **tweak: FOV transition BlockInput duration increased to 350ms for both glide?dive directions.**

### BetterAngle Pro v5.5.217
- **tweak: Alt-tab BlockInput lock duration reduced from 400ms to 250ms.**

### BetterAngle Pro v5.5.216
- **fix: Alt-tab lock ROI box never showed LOCKING state.** The alt-tab lock in `FocusMonitorThread` was signaling `BlockInput` but not setting `g_mouseSuspendedUntil`, so the ROI visualizer (which shows purple LOCKING when that timer is active) always showed GLIDING/DIVING during the alt-tab lock. Now sets `g_mouseSuspendedUntil = GetTickCount64() + 400` before signaling the worker, matching what the FOV transition locks do.

### BetterAngle Pro v5.5.215
- **fix: Alt-tab BlockInput lock silently suppressed during gameplay.** `FocusMonitorThread` had a third guard `(GetTickCount64() - g_lastLockTime > 500)` on the alt-tab lock. `g_lastLockTime` is updated by every FOV transition lock (glide?dive), which happen continuously during a skydive ? keeping `g_lastLockTime` always within the last 500ms and permanently blocking the alt-tab lock. Removed this guard from the alt-tab path; the existing `unfocusedMs >= 500` check is sufficient to distinguish real alt-tabs from overlay/notification blips.

### BetterAngle Pro v5.5.214
- **fix: BlockInput lock breaking after 10ms.** `g_lockDurationMs.exchange(0)` zeroes the value the instant the worker reads it, so the `g_lockDurationMs.load() == 0` check inside the loop was always true and broke the lock after a single tick. Removed that check entirely ? the loop now runs for the full requested duration and only exits early if Fortnite loses focus (via `g_fortniteFocusedCache`), which is the same abort mechanism used by the FOV transition locks.

### BetterAngle Pro v5.5.213
- **fix: Alt-tab BlockInput lock not holding (0ms effective lock).** The BlockInput worker loop had `IsFortniteForeground()` as its loop *condition*, evaluated before the first `Sleep(10)`. During the alt-tab animation, Windows briefly flickers the foreground window (taskbar/switcher) before settling on Fortnite ? so the worker thread's cold `thread_local` cache returned `false` on the very first check, the loop body never ran, and `BlockInput(FALSE)` fired instantly after `BlockInput(TRUE)`. Fixed by removing the unreliable direct call from the loop condition and instead checking `g_fortniteFocusedCache` (the value maintained by FocusMonitorThread) inside the loop body after the first sleep tick has already elapsed.

### BetterAngle Pro v5.5.212
- **fix: Decimal digit display bug.** The HUD was extracting decimal digits by re-multiplying a `double` by 10 or 100 and casting to `int`, which fails when floating-point representation rounds the result down (e.g. `179.3 * 10.0 = 1792.9999...` ? `(int) = 1792` ? shows `.2` instead of `.3`). Fixed by converting to a scaled integer once with `llround`, then using pure integer `%` and `/` for all digit extraction ? guaranteed correct for every angle.
- **fix: `Norm360` slow-path removed.** Replaced `while` subtraction loops with `fmod`, which is O(1) regardless of how large the accumulated angle value is.

### BetterAngle Pro v5.5.211
- **fix: Build error ? removed orphaned `g_lastScanUsedDxgi` assignments.** The identifier was written in `Detector.cpp` but never declared in `State.h` or defined in `State.cpp`, causing MSVC C2065 errors. The value was never read anywhere so the two assignments were simply removed.

### BetterAngle Pro v5.5.210
- **perf: 3-Pixel Tripwire Pre-arm.** After ROI calibration, BetterAngle learns one representative matching pixel per horizontal third of the ROI. Before each full scan, those 3 pixels are checked via 3 ? 1?1 GPU copies (DXGI). If 2-of-3 match the target colour, the glide?dive lock fires immediately ? roughly 200?s faster than waiting for the full ROI scan to complete. The tripwire positions are persisted in the profile JSON and re-learned automatically whenever the user recalibrates.

### BetterAngle Pro v5.5.209
- Automated build release.

### BetterAngle Pro v5.5.206
- Automated build release.

### BetterAngle Pro v5.5.205
- Automated build release.

### BetterAngle Pro v5.5.202
- Automated build release.

### BetterAngle Pro v5.5.201
- **fix: 2dp angle font size.** Removed the smaller 48px fallback font for 2-decimal mode ? both 1dp and 2dp now use the same 54px bold font.

### BetterAngle Pro v5.5.200
- Automated build release.

### BetterAngle Pro v5.5.198
- Automated build release.

### BetterAngle Pro v5.5.197
- **fix: HUD angle segment spacing.** Switched to `StringFormat::GenericTypographic()` for all angle digit measurements and draws, eliminating the per-segment internal padding that GDI+ normally adds. The whole number, decimal point, and decimal digits now sit flush with no gaps.

### BetterAngle Pro v5.5.196
- Automated build release.

### BetterAngle Pro v5.5.205
- **perf: Throttled Cursor Visibility Check.** Instead of calling `GetCursorInfo` 1000+ times per second, the check now runs every 16ms. This significantly reduces User32 call overhead and jitter on lower-end CPUs.

### BetterAngle Pro v5.5.204
- **perf: Early-Exit Scan logic.** The scanner now stops immediately once your required match percentage is reached. This reduces CPU usage during active locks.
- **chore: Removed ineffective GDI sub-frame check.** Eliminates micro-stuttering caused by context-switching to the message queue during high-speed scans.

### BetterAngle Pro v5.5.199
- **fix: Input ghosting / Auto-mantle fixed.** Removed unused keyboard raw input registration that caused Windows to delay key delivery (especially Space).
- **fix: Alt-tab cooldown guard.** Added a timing guard to prevent firing a 400ms lock immediately after an FOV-edge lock if focus shifted.
- **fix: Proper Raw Input cleanup.** MsgWndProc now falls through to DefWindowProc for WM_INPUT to correctly free internal buffers.

### BetterAngle Pro v5.5.195
- **feat: Colour-Coded Angle Display.** Whole number renders in Green, 1st decimal in Cyan, 2nd decimal in Yellow for instant visual parsing.
- **feat: HUD Decimal Precision Toggle.** New "Decimal Places" dropdown in the General tab lets you switch between 1 or 2 decimal places. Font auto-scales to fit.
- **perf: Zero-Latency Spin Waits.** Replaced `Sleep(1)` with `_mm_pause()` spin loops in both the detector thread and DXGI timeout path. Scanner reacts to new frames instantly (~0.5?2ms latency reduction).
- **fix: BlockInput Thread Affinity.** FocusMonitorThread no longer calls `BlockInput(FALSE)` from the wrong thread ? signals the worker thread to release instead, preventing permanent keyboard lockouts on alt-tab.
- **chore: Removed dead `FlushPendingInputMessages` function.**
- ?? Colour matching engine (`Detector.cpp CountMatches`) is **unchanged** from v5.5.156 ? no AVX2, no Chebyshev, no byte-drift risk.

### BetterAngle Pro v5.5.155
- Restored DXGI Desktop Duplication scanner ? ~3-5x faster than the BitBlt path. Coupled with a one-shot DXGI sample at colour-pick time so the saved `target_color` is the exact byte the scanner sees (eliminates the GDI/DXGI byte drift that previously caused "no colour match"). Multi-adapter HMONITOR matching ensures the duplication targets the correct monitor's output. ReinitDisplay now runs only on the detector thread (or before it starts), avoiding races with in-flight `Scan` / `SamplePixelDXGI`. Falls back to BitBlt automatically for HDR monitors or unreachable outputs.

### BetterAngle Pro v5.5.154
- Automated build release.

### BetterAngle Pro v5.5.153
- Multi-monitor fixes:
  - **Wrong screen captured during ROI/colour selection**: the snapshot is the full virtual desktop but the HUD was always blitting from snapshot (0,0) ? secondary monitors saw the primary monitor's content (Discord/browser) instead of Fortnite. Now blits from the configured monitor's slice.
  - **Hot-plugging a 2nd monitor broke detection**: `WM_DISPLAYCHANGE` now (a) bumps a generation counter so the detector invalidates its cached monitor rect, and (b) auto-tracks Fortnite's monitor via `MonitorFromWindow` so `g_screenIndex` follows the game even if Windows renumbers indices.

### BetterAngle Pro v5.5.152
- Automated build release.

### BetterAngle Pro v5.5.151
- Reverted the DXGI Desktop Duplication scanner back to GDI BitBlt. The color picker (CaptureDesktop) and the detector now sample from the same source again, so picked colors actually match what the scanner sees ? fixes "no colour match" after picking a color. Loses some scan-speed performance but trades it for reliable detection.

### BetterAngle Pro v5.5.150
- Automated build release.

### BetterAngle Pro v5.5.149
- Fix: pressing the ROI hotkey no longer alt-tabs the user out of Fortnite. Removed the `SetForegroundWindow` focus-steal and added `WS_EX_NOACTIVATE` to the HUD overlay so it can catch mouse events for ROI/color drawing without taking focus from the game.

### BetterAngle Pro v5.5.148
- Automated build release.

### BetterAngle Pro v5.5.147
- Automated build release.

### BetterAngle Pro v5.5.146
- Pre-spawned BlockInput worker eliminates ~5-20ms thread-creation latency on FOV transitions. Lock now fires sub-millisecond after pixel detection.

### BetterAngle Pro v5.5.145
- Automated build release.

### BetterAngle Pro v5.5.144
- Automated build release.

### BetterAngle Pro v5.5.143
- Automated build release.

### BetterAngle Pro v5.5.142
- Automated build release.

### BetterAngle Pro v5.5.141
- Automated build release.

### BetterAngle Pro v5.5.140
- Automated build release.

### BetterAngle Pro v5.5.139
- Automated build release.

### BetterAngle Pro v5.5.138
- Automated build release.

### BetterAngle Pro v5.5.137
- Automated build release.

### BetterAngle Pro v5.5.136
- Automated build release.

### BetterAngle Pro v5.5.135
- Automated build release.

### BetterAngle Pro v5.5.134
- Automated build release.

### BetterAngle Pro v5.5.133
- Automated build release.

### BetterAngle Pro v5.5.132
- Automated build release.

### BetterAngle Pro v5.5.131
- Automated build release.

### BetterAngle Pro v5.5.130
- Automated build release.

### BetterAngle Pro v5.5.129
- Automated build release.

### BetterAngle Pro v5.5.128
- Automated build release.

### BetterAngle Pro v5.5.127
- Automated build release.

### BetterAngle Pro v5.5.126
- Automated build release.

### BetterAngle Pro v5.5.125
- Automated build release.

### BetterAngle Pro v5.5.124
- Automated build release.

### BetterAngle Pro v5.5.123
- Automated build release.

### BetterAngle Pro v5.5.122
- Automated build release.

### BetterAngle Pro v5.5.121
- Automated build release.

### BetterAngle Pro v5.5.120
- Automated build release.

### BetterAngle Pro v5.5.119
- Automated build release.

### BetterAngle Pro v5.5.118
- Automated build release.

### BetterAngle Pro v5.5.117
- Automated build release.

### BetterAngle Pro v5.5.116
- Automated build release.

### BetterAngle Pro v5.5.115
- Automated build release.

### BetterAngle Pro v5.5.114
- Automated build release.

### BetterAngle Pro v5.5.113
- Automated build release.

### BetterAngle Pro v5.5.112
- Automated build release.

### BetterAngle Pro v5.5.111
- Automated build release.

### BetterAngle Pro v5.5.110
- Automated build release.

### BetterAngle Pro v5.5.109
- Automated build release.

### BetterAngle Pro v5.5.108
- **Architecture reset**: Removed all SHOCK/RESTORE/typematic-detection logic. BlockInput alone is sufficient to prevent keyboard + mouse movement during FOV transitions. The per-key contamination filtering and raw-input correction approach (v5.5.106-107) proved unreliable for hold-through-lock cases.
- **Pending**: Future work to explore mouse-only locking alternatives to address the remaining hold-through-lock double-press behavior.

### BetterAngle Pro v5.5.107
- Automated build release.

### BetterAngle Pro v5.5.106
- Automated build release.

### BetterAngle Pro v5.5.105
- Automated build release.

### BetterAngle Pro v5.5.104
- **GHOST WALK ROOT FIX (true root cause this time)**: The v5.5.102 count-threshold of `Mk>=4` was a heuristic that solved the ghost walk but introduced a double-press regression ? real typematic in the 200 ms window often produced <4 events, so legitimate holds were getting spurious KEYUPs.
  - **What was actually wrong**: synthetic SendInput events from the safety-net (WM_USER+42), SHOCK, and RESTORE were being dispatched to MsgWndProc asynchronously by the main thread's message pump. On a busy frame, some events were dispatched AFTER the lock thread reset the raw-input arrays, contaminating the freshly-cleared 200 ms collection window with 1?3 stray Make events.
  - **The fix**: Added `Sleep(50)` on the lock thread AFTER RESTORE finishes but BEFORE the array reset. The lock thread is a worker thread; sleeping it does NOT block the main thread's pump. During those 50 ms, the pump dispatches all queued synthetic WM_INPUTs while `g_ghostFixInProgress` is still true, so MsgWndProc filters them out. The reset that follows is genuinely clean.
  - **Reverted v5.5.102's threshold**: now that contamination is gone at the source, the original boolean rule (`breakDetected || !makeDetected`) is sound again. No double-press on real holds. No ghost walk on real releases.
- **ALT-TAB / WIN+TAB FIX**: Removed `BlockInput(TRUE)` + `Sleep(400)` + `SyncGamingKeysNitro` from the alt-tab focus-change path. Windows already restores keyboard state correctly on focus changes, so the cooldown was both unnecessary and actively harmful ? it froze user input for 400 ms on every Win+Tab/Alt-Tab and inherited the Shock & Restore double-press. Replaced with a 100 ms `g_mouseSuspendedUntil` that gates angle calculation only, no keyboard interference.
- Net effect: ghost walk dies, double-press regression gone, Win+Tab no longer bugs out.

### BetterAngle Pro v5.5.103
- Automated build release.

### BetterAngle Pro v5.5.102
- **GHOST WALK ROOT-CAUSE FIX**: The post-restore correction logic was tripping a false-positive on contaminating Raw-Input events that leaked past the array reset, falsely concluding "user is still holding W" and skipping the KEYUP that would kill the ghost. Live diagnostics from v5.5.100 captured the smoking gun: `Mk count W:3, Br count W:0, Pre/Corr W:P.` ? 3 contaminating Make events with 0 Breaks, no correction fired.
- **The Fix**: Replaced the boolean correction check (`breakDetected || !makeDetected`) with a count threshold (`makeCount >= 4 && breakCount == 0`). Real typematic in the 200 ms window produces ~5?7 Make events; queued SHOCK/RESTORE contamination tops out around 3. The threshold cleanly separates the two cases without affecting genuine holds.
- **Net effect**: Released-during-lock keys are now reliably killed (no more ghost walk after release-during-transition); held-through-lock keys continue to be left alone (no double-press regression).
- The diagnostic LOG line in `SyncGamingKeysNitro` now logs `(Mk=N Br=N)` counts instead of bools so future regressions are visible at a glance in `debug.log`.

### BetterAngle Pro v5.5.101
- Automated build release.

### BetterAngle Pro v5.5.100
- **Critical Logging Fix**: `EnhancedLogger::Log` now flushes after every write. Previously, log lines stayed in the `std::ofstream` buffer and never reached `%LOCALAPPDATA%\BetterAngle\logs\debug.log` until the app shut down ? which made every diagnostic LOG_INFO line invisible during a live session and broke the Test 2 / Test 4 EAC-filter check.
- **Per-Key Event Counts**: `g_rawMakeCount[256]` and `g_rawBreakCount[256]` now track how many Make / Break Raw-Input events arrive per VK. The previous boolean (`g_rawKeyMakeDetected`) couldn't distinguish "1 contaminating event" from "6 real typematic repeats" ? counts can.
- **Last-Lock Snapshot**: At the end of `SyncGamingKeysNitro`, the per-key Make / Break counts, preState, and corrected-flag are snapshotted into dedicated globals (`g_lastLockMakeCount`, `g_lastLockBreakCount`, `g_lastLockPreState`, `g_lastLockCorrected`). This survives the next lock's array reset, so the operator can read what the correction logic actually saw on the last transition.
- **Three new debug overlay rows** (Column 0, rows 17?20) display the snapshot:
  - `Last Lock:` ? seconds since the last lock completed.
  - `  Mk count:` ? per-key Make-event count during the 200 ms collection window. Real typematic produces ~5?7 events; a contamination-only reading is 1?3.
  - `  Br count:` ? per-key Break-event count. Should be 0 for keys the user is still holding.
  - `  Pre/Corr:` ? `W:PC` means W was in preState (P) and correction fired (C). `W:P.` is the ghost-walk smoking gun (preState held but no correction).
- Diagnostic-only release; no behavioural changes to the lock pipeline, Shock & Restore, Fake Death, or correction logic.

### BetterAngle Pro v5.5.99
- Automated build release.

### BetterAngle Pro v5.5.98
- **Ghost-Walk Diagnostic Overlay**: Added four new rows to the debug overlay (Column 0, rows 13?16) that surface the live signals needed to root-cause the remaining ghost-walk cases without rebuilds.
  - **Typematic ? W**: ms gap between successive hardware Make events on `W`. Healthy fast-repeat sits around ~33 ms; values above ~60 ms (or `(press W)` when no recent press) mean the 200 ms post-restore collection window is too short to see typematic, causing the correction logic to false-fire a KEYUP and force a double-press.
  - **SafetyNet Fires**: count of `WM_USER+42` invocations (one per FOV transition).
  - **Sync Skips**: count of `SyncGamingKeysNitro` re-entrancy skips. Any nonzero value is a missed ghost-fix cycle from rapid back-to-back transitions.
  - **Corrections**: total Raw-Input corrections fired, plus the last corrected key and seconds since. If this increments during a transition where the character keeps walking, `SendInput` is not reaching Fortnite (EAC filtering suspected).
- **Test 4 Logging**: Added two `LOG_INFO` lines so the EAC-filtering test can be confirmed from `debug.log` directly:
  - `SafetyNet KEYUP fired for WASD` ? at the top of the `WM_USER+42` handler.
  - `Correction KEYUP for vk=%d` ? immediately after the correction `SendInput` in `SyncGamingKeysNitro`.
  - Reading the log: SafetyNet **and** Correction firing while the character still walks ? `SendInput` is being filtered (EAC). SafetyNet firing but no Correction during ghost walk ? typematic mistiming (the 200 ms window decided the user was still holding when they weren't).
- Diagnostic-only release; no behavioural changes to the lock pipeline, Shock & Restore, Fake Death, or correction logic.

### BetterAngle Pro v5.5.97
- Automated build release.

### BetterAngle Pro v5.5.96
- Automated build release.

### BetterAngle Pro v5.5.95
- Automated build release.

### BetterAngle Pro v5.5.94
- **UI Bug Fix**: Resolved a visual bug in the GDI+ debug overlay where the "Input State" text and "Version" text were overlapping due to a Y-coordinate collision.
- Automated build release.

### BetterAngle Pro v5.5.93
- **Performance Impact Diagnostics Update**: Removed the static GPU usage readout to maintain the clean UI layout while ensuring zero risk from anti-cheat systems. The Performance Impact panel now exclusively focuses on per-process CPU usage and RAM footprint.
- Automated build release.

### BetterAngle Pro v5.5.92
- Automated build release.

### BetterAngle Pro v5.5.91
- **Performance Impact Diagnostics**: Added a live Performance Impact tracking table to the Dashboard under the Debug menu. Tracks per-process CPU usage and RAM footprint natively via Windows API.
- Automated build release.

### BetterAngle Pro v5.5.90
- **Diagnostic Upgrade (Fake Death)**: Implemented an experimental fix for the Ghost Walk bug that tears down and reconstructs the Windows raw input pipeline mid-transition to flush stale `KeyUp` buffers. Includes a hard-coded safety net that manually emits `KeyUp` events for WASD on every transition.
- Automated build release.

### BetterAngle Pro v5.5.89
- **Dynamic Crosshair Centering**: The crosshair now perfectly centers itself on the active Fortnite game window, natively fixing misalignment issues for users playing in Windowed or Custom Stretched resolutions. 
- Automated build release.

### BetterAngle Pro v5.5.88
- **Input System Upgrade**: Completely rewrote the global hotkey engine to use manual hardware polling instead of Windows `RegisterHotKey`. This officially adds full support for mapping custom functions (Dashboard, Crosshair, etc.) to extra mouse buttons (Mouse4/Mouse5) which was previously blocked by the OS.
- Automated build release.

### BetterAngle Pro v5.5.87
- **Stability Fixes**: Resolved IsFortniteForeground data race and eliminated BlockInput thread queuing to prevent mouse freezes.
- **UI Enhancements**: Fixed taskbar disappearing on Alt-Tab and mouse freezes when the in-game map is opened.
- **Engine Safety**: Fixed undefined behavior in Qt initialization and invalid monitor fallback logic.
- Automated build release.

### BetterAngle Pro v5.5.86
- Improved detector and overlay logic for smoother performance
- Automated build release.

### BetterAngle Pro v5.5.85
- Automated build release.

### BetterAngle Pro v5.5.84
- Automated build release.

### BetterAngle Pro v5.5.83
- Automated build release.

### BetterAngle Pro v5.5.82
- Automated build release.

### BetterAngle Pro v5.5.80
- Automated build release.

### BetterAngle Pro v5.5.79
- Automated build release.

### BetterAngle Pro v5.5.78
- Automated build release.

### BetterAngle Pro v5.5.77
- Automated build release.

### BetterAngle Pro v5.5.76
- Automated build release.

### BetterAngle Pro v5.5.75
- Automated build release.

### BetterAngle Pro v5.5.74
- Automated build release.

### BetterAngle Pro v5.5.73
- Automated build release.

### BetterAngle Pro v5.5.72
- Automated build release.

### BetterAngle Pro v5.5.66
- Automated build release.

### BetterAngle Pro v5.5.65
- **Burst Restore Loop**: Implemented 3-pulse typematic simulation to force game engine acknowledgement.
- **Enhanced Thaw Wait**: Increased post-Shock delay to 100ms to guarantee hardware-to-OS synchronization.
- **Typematic Interleaving**: Added 10ms gaps between pulses to span across multiple engine polling ticks.

### BetterAngle Pro v5.5.64
- **Fixed Build Errors**: Resolved undeclared identifier and syntax errors in Overlay.cpp and Input.cpp.
- **Improved Overlay Stability**: Hardened DrawRow calls with explicit string conversions.

### BetterAngle Pro v5.5.63
- **Self-Correcting GhostFix**: Added a Verification step to confirm successful input restoration.
- **Enhanced Reliability**: Added SendInput error handling and diagnostic retry logic.
- **Latency Tracking**: Measured GhostFix execution time for performance tuning.

### BetterAngle Pro v5.5.62
- **Shock & Restore Engine**: Replaces unreliable delta-sync with unconditional release and hardware-thaw restoration.
- **Alt-Tab Lock Guard**: Extended BlockInput to focus switches to prevent FOV drift.
- **Serialized Execution**: Prevents rapid transition overlaps from corrupting input state.

### BetterAngle Pro v5.5.61
- Automated build release.

### BetterAngle Pro v5.5.60
- **Serialized Lock Guard**: Prevents missed GhostFixes during rapid transitions (back-to-back glide/dive).
- **Inventory Safety**: Skips key injection when Fortnite is not focused and enforces a strict whitelist for WASD + Space only.

### BetterAngle Pro v5.5.57
- Automated build release.

### BetterAngle Pro v5.5.56
- Automated build release.

### BetterAngle Pro v5.5.55
- Automated build release.

### BetterAngle Pro v5.5.54
- Automated build release.

### BetterAngle Pro v5.5.23
- **BlockInput Keyboard Ghosting Fixes**:
  - Skip BlockInput for alt-tab transitions (200ms cooldown only)
  - Added 50ms wait for natural async key state table thaw
  - Exclude spacebar from fallbacks to prevent FOV transition loops
  - Added keybd_event fallback for security software blocking SendInput
  - Enhanced ghostkey_fail.log diagnostics
  - Thread-safe BlockInput calls with mutex protection

### BetterAngle Pro v5.5.22
- Automated build release.

### BetterAngle Pro v5.5.21
- Automated build release.

### BetterAngle Pro v5.5.20
- Automated build release.

### BetterAngle Pro v5.5.19
- Automated build release.

### BetterAngle Pro v5.5.18
- Automated build release.

### BetterAngle Pro v5.5.17
- Automated build release.

### BetterAngle Pro v5.5.16
- Automated build release.

### BetterAngle Pro v5.5.15
- Automated build release.

### BetterAngle Pro v5.5.14
- Automated build release.

### BetterAngle Pro v5.5.13
- Automated build release.

### BetterAngle Pro v5.5.12
- Automated build release.

### BetterAngle Pro v5.5.11
- fix: git hijack to neutralize duplicate logic [v5.5.14]

### BetterAngle Pro v5.5.10
- fix: neutralize duplicate workflow logic [v5.5.13]

### BetterAngle Pro v5.5.9
- fix: implemented background release striker [v5.5.12] - chore: auto-increment version to 5.5.8 - chore: auto-increment version to 5.5.8 - fix: decouple release finalizer to prevent build deadlock [v5.5.11] - fix: integrated automated release trigger into CMake [v5.5.10] 

### BetterAngle Pro v5.5.8
- fix: decouple release finalizer to prevent build deadlock [v5.5.11] - fix: integrated automated release trigger into CMake [v5.5.10] 

### BetterAngle Pro v5.5.7
- fix: hardened bump script against concurrency collisions [v5.5.8] - chore: auto-increment version for release 5.5.6 [skip ci] - fix: remove redundant skip-ci from bump logic [v5.5.7] - fix: implemented granular ghost forensics debug rows [v5.5.6] 

### BetterAngle Pro v5.5.6
- fix: implemented granular ghost forensics debug rows [v5.5.6]

### BetterAngle Pro v5.5.5
- Automated build release.

### BetterAngle Pro v5.5.3
- fix: resolve C2065 undeclared identifier 'modStr' in Overlay.cpp [v5.5.3] - fix: self-contained version bump and tagging logic [skip ci] 

### BetterAngle Pro v5.5.2
- fix: Nitro Flush keyboard ghosting resolution [v5.5.2]

### BetterAngle Pro v5.5.1
- Automated build release.

### BetterAngle Pro v5.4.9
- Automated build release.

### BetterAngle Pro v5.4.7
- Automated build release.

### BetterAngle Pro v5.4.4
- chore: triggering first Golden Path release (v5.4.4) - chore: auto-increment version for release [skip ci] 

### BetterAngle Pro v5.4.4
- Update msbuild.yml

### BetterAngle Pro v5.4.3
- feat: implemented 15ms Atomic Shield for settled input snapshots (v5.4.2)

### BetterAngle Pro v5.4.2
- release: BetterAngle Pro v5.4.1 (Enabling Automated Tagging) - release: BetterAngle Pro v5.4.0 (Absolute Restoration Reset) - release: BetterAngle Pro v5.3.1 (Official Absolute Restoration) - release: BetterAngle Pro v5.3.1 (Absolute Restoration) - release: BetterAngle Pro v5.2.0 (Official Absolute Restoration) - release: BetterAngle Pro v5.2.0 (Absolute Restoration) - release: BetterAngle Pro v5.2 (Official Absolute Restoration) - release: v5.2.1 Official Absolute Restoration Suite - release: v5.2.0 Absolute Restoration (Unconditional Input Sync) [skip ci] - release: v5.1.26 Official Triple-Handshake Suite [skip ci] - release: BetterAngle Pro v5.1.24 (Official Deep-Freeze Forensic Suite) - feat: implemented Deep-Freeze forensic diagnostics (v5.1.24) - chore: auto-increment version for release - fix: implemented Iron Flush and resolved permanent ghosting (v5.1.23) - chore: auto-increment version for release - feat: unified Iron-Tight diagnostics across UI (v5.1.22) - chore: auto-increment version for release - fix: resolved g_running linker conflict (v5.1.21) - chore: auto-increment version for release - fix: removed redundant key definition (v5.1.20) - feat: implemented Iron-Tight diagnostic suite (v5.1.19) - chore: auto-increment version for release - fix: resolved compilation apocalypse (v5.1.18) - chore: auto-increment version for release - feat: implemented Absolute Truth diagnostics (v5.1.17) - chore: auto-increment version for release - fix: implemented Atomic Shield thread-safe polling (v5.1.16) - chore: auto-increment version for release - feat: implemented On-Screen Ghost Radar and synced versions (v5.1.15) - chore: auto-increment version for release - fix: resolved LNK2005 multiply defined symbols (v5.1.14) - chore: auto-increment version for release - fix: resolved g_nitroSyncLog build error (v5.1.13) - chore: auto-increment version for release - feat: implemented Ghost Detector Final Form with Phys/Tracked comparison (v5.1.12) - chore: auto-increment version for release - feat: added X-Ray Input Monitor for real-time ghosting diagnostics (v5.1.11) - chore: auto-increment version for release - chore: consolidated all versioning to v5.1.10 - chore: auto-increment version for release - feat: enabled high-performance 1ms timers for polling thread (v5.1.8) - chore: auto-increment version for release - docs: added technical summary of nitro sync logic - chore: auto-increment version for release - feat: finalized bulletproof blueprint for nitro synchronization (v5.1.7) - chore: auto-increment version for release - feat: implemented dedicated polling thread for hardware state tracking (v5.1.6) - chore: auto-increment version for release - feat: deployed shadow hook for zero-ghosting movement (v5.1.5) - chore: auto-increment version for release - fix: resolved build errors in x-ray tracker implementation (v5.1.4) - chore: auto-increment version for release - feat: implemented x-ray physical tracker for bulletproof anti-ghosting (v5.1.3) - chore: auto-increment version for release - fix: added 10ms windows catch-up buffer to resolve ghosting (v5.1.2) - chore: auto-increment version for release - fix: resolved build errors and bumped to v5.1.1 for updater stability - chore: auto-increment version for release 

### BetterAngle Pro v5.4.0
- **CRITICAL BLOCKINPUT FIX:** Fixed scancode injection bug in SyncGamingKeysNitro that caused keyboard ghosting
- **ROOT CAUSE:** Windows async key state table frozen by BlockInput API
- **SOLUTION:** Unconditional KeyUp injection with correct scancode usage (wVk = 0)
- **GHOST ELIMINATION:** Keys released during BlockInput no longer remain "held" after unlock
- **VERSION STABILITY:** Fixed bump_version.ps1 script and version consistency across all files

### BetterAngle Pro v5.3.1
- **ABSOLUTE RESTORATION:** Implemented unconditional pre-lock state capture and force-release logic.
- **GHOST ELIMINATOR:** Bypasses the frozen Windows Async Key Table by restoring hardware state directly after input blocks.
- **Zero-Verification Latency:** Removed redundant post-lock polling; restoration happens instantly upon BlockInput(FALSE).
- **Core Stability:** Hard-locked versioning across all project binaries.

- Automated build release.

### BetterAngle Pro v5.1.16
- Automated build release.

### BetterAngle Pro v5.1.15
- Automated build release.

### BetterAngle Pro v5.1.14
- Automated build release.

### BetterAngle Pro v5.1.13
- Automated build release.

### BetterAngle Pro v5.1.12
- Automated build release.

### BetterAngle Pro v5.1.11
- Automated build release.

### BetterAngle Pro v5.1.10
- Automated build release.

### BetterAngle Pro v5.1.9
- docs: added technical summary of nitro sync logic

### BetterAngle Pro v5.1.8
- Automated build release.

### BetterAngle Pro v5.1.7
- Automated build release.

### BetterAngle Pro v5.1.6
- Automated build release.

### BetterAngle Pro v5.1.5
- Automated build release.

### BetterAngle Pro v5.1.4
- Automated build release.

### BetterAngle Pro v5.1.3
- Automated build release.

### BetterAngle Pro v5.1.2
- Automated build release.

### BetterAngle Pro v5.1.1
- Automated build release.

### BetterAngle Pro v5.0.111
- Automated build release.

### BetterAngle Pro v5.0.109
- Automated build release.

### BetterAngle Pro v5.0.105
- Automated build release.

### BetterAngle Pro v5.0.102
- Automated build release.

### BetterAngle Pro v5.0.101
- Automated build release.

### BetterAngle Pro v5.0.92
- Automated build release.

### BetterAngle Pro v5.0.90
- Automated build release.

### BetterAngle Pro v5.0.88
- Automated build release.

### BetterAngle Pro v5.0.86
- Automated build release.

### BetterAngle Pro v5.0.85
- perf: implemented Mathematical Optimizations including SIMD pixel scanning, Sensitivity Baking, and Integer Comparison Theory for ultra-smooth performance.
- fix: synchronized GitHub Actions and Releases version numbering.

### BetterAngle Pro v5.0.83
- Automated build release.

### BetterAngle Pro v5.0.82
- fix: improved updater reliability with a more robust JSON parser and standard User-Agent to ensure smooth updates from GitHub.

### BetterAngle Pro v5.0.80
- release: baseline stability restoration. This version resets the codebase to the stable v5.0.74 logic, providing a reliable foundation for future manual optimizations.
- perf: implemented ultra-fast focus and detector threads using yield/Sleep(0) and HWND caching for zero-delay input locking.

### BetterAngle Pro v5.0.73
- Automated build release.

### BetterAngle Pro v5.0.72
- Automated build release.

### BetterAngle Pro v5.0.71
- Automated build release.

### BetterAngle Pro v5.0.70
- Automated build release.
