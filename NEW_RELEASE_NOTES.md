### BetterAngle Pro v5.5.195
- **feat: Colour-Coded Angle Display.** Whole number renders in Green, 1st decimal in Cyan, 2nd decimal in Yellow for instant visual parsing.
- **feat: HUD Decimal Precision Toggle.** New "Decimal Places" dropdown in the General tab lets you switch between 1 or 2 decimal places. Font auto-scales to fit.
- **perf: Zero-Latency Spin Waits.** Replaced `Sleep(1)` with `_mm_pause()` spin loops in both the detector thread and DXGI timeout path. Scanner reacts to new frames instantly (~0.5–2ms latency reduction).
- **fix: BlockInput Thread Affinity.** FocusMonitorThread no longer calls `BlockInput(FALSE)` from the wrong thread — signals the worker thread to release instead, preventing permanent keyboard lockouts on alt-tab.
- **chore: Removed dead `FlushPendingInputMessages` function.**
- ⚠️ Colour matching engine (`Detector.cpp CountMatches`) is **unchanged** from v5.5.156 — no AVX2, no Chebyshev, no byte-drift risk.
