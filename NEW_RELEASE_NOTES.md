### BetterAngle Pro v5.5.289
- **Fix: Zero-Latency Focus Hooks**. Replaced the old spin-loop focus polling with native OS `SetWinEventHook` listeners. The app now receives focus change events instantly from the Windows kernel with zero CPU usage.
- **Fix: Alt-Tab Input Gate**. Reverted the raw input message handler to use the synchronized focus cache guard. This ensures the 200ms `BlockInput` lock is armed *before* any queued background mouse deltas can bypass the guard during an Alt-Tab transition, permanently fixing the angle jumping.
