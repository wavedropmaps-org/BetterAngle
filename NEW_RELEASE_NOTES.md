### BetterAngle Pro v5.5.205
- **perf: Throttled Cursor Visibility Check.** Instead of calling `GetCursorInfo` 1000+ times per second, the check now runs every 16ms. This significantly reduces User32 call overhead and jitter on lower-end CPUs.
